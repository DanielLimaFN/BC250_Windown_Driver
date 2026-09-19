# Handoff — 14/09/2026 · B.2.4 Fechada (Emissão PM4 SET_SH_REG de Shaders Gráficos e Transação CP_RB0)

> **Para a próxima IA e auditoria de engenharia.** Este documento formaliza o fechamento completo da etapa **B.2.4**,
> atendendo rigorosamente às 4 exigências de auditoria de silício:
> 1. **Reuso de `Bc250Pm4EmitSetShReg`**: elimina completamente a escrita plana no anel e herda o tratamento nativo de wrap-around circular.
> 2. **Ligação à transação da B.1 (`bc250_gfx_ring_transaction.c`)**: função `Bc250GfxRingTxnSubmitShReg` que planta poison `0x43DAFFFE` na fence com prova de readback prévio, emite os 12 dwords de shader pipeline (`Bc250GfxShRegEmitPipeline`), emite a fence `RELEASE_MEM` EOP de 32 bits (`Bc250Pm4EmitReleaseMemEop32`), dispara o anel avançando `CP_RB0_WPTR`, amostra RPTR (MMIO e WB) com classificação estrita de tripé via `Bc250GfxRingClassifyTripod` (redução de volta A15), e integra ao bringup Level 3 do miniport com rollback incondicional (Lei 5).
> 3. **Nomenclatura Corrigida (`CP_RB0`)**: o anel gráfico é estritamente `CP_RB0` (ME/PFP); Node 1 é compute assíncrono (MEC/HQD). A distinção é absoluta e auditada.
> 4. **Remoção da restrição artificial `Rsrc1 == 0`**: `Bc250GfxShRegsValid` rejeita estritamente endereço microcódigo nulo (`PgmLo == 0`), permitindo `Rsrc1 == 0` para shaders mínimos de teste.
>
> **Alvo:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) · Windows 10 Pro 22H2 x64 build 19045
> **Estado:** OFFLINE (Contrato + Testes Unitários + Transação Física + Portão de Segurança + 26/26 Mutações).
> **Hardware:** Zero driver instalado, zero hardware tocado nesta sessão. **NÃO VALIDADO EM HARDWARE.**

---

## 1. O que foi Fechado na Etapa B.2.4

A etapa B.2.4 conecta o empacotamento lógico de shaders à execução física no anel gráfico `CP_RB0`:
* Os registradores `SPI_SHADER_PGM_*` calculados em B.2.1 a partir dos manifestos do compilador e validados contra os blobs B.2.2 com staging na VRAM e prova de readback (B.2.3) são empacotados em pacotes PM4 Type-3 `PKT3_SET_SH_REG` (opcode `0x76`).
* A emissão delega estritamente a `Bc250Pm4EmitSetShReg`, garantindo suporte nativo a wrap-around circular do anel buffer.
* A transação física `Bc250GfxRingTxnSubmitShReg` integra o pipeline de shaders no fluxo de transação com fence sobre poison da Lei 2 e rollback da Lei 5.

---

## 2. Invariantes Críticos e Resolução da Auditoria (Leis 1, 2, 4, 5, 7 e 8)

### 2.1 Reuso Mandatório de `Bc250Pm4EmitSetShReg` contra Escrita Plana (D1 / D2)
* Em vez de emitir dwords sequenciais em um array plano (que quebrava quando o anel circular dava a volta), o emissor reusa `Bc250Pm4EmitSetShReg`.
* O portão de auditoria proíbe categoricamente qualquer escrita plana `RingBuffer[cursor++] = BC250_GFX_SH_PGM_LO_*` e exige o reuso do helper.

### 2.2 Ligação com a Transação Física do Anel `CP_RB0` (Lei 2 e Lei 5)
* Função `Bc250GfxRingTxnSubmitShReg`:
  1. Exige que o anel esteja ativo (`ApplyEvidence->RingActivatePassed == 1`).
  2. Injeta poison `0x43DAFFFE` na memória da fence e valida o readback antes de tocar no anel (`SubmitEvidence->FencePoisonReadback == 0x43DAFFFE`).
  3. Lê o `CP_RB0_WPTR` inicial de hardware.
  4. Emite 12 dwords do pipeline de shaders (VS + PS) reusando `Bc250GfxShRegEmitPipeline`.
  5. Emite 8 dwords de fence `RELEASE_MEM` EOP de 32 bits sobre o poison (`Bc250Pm4EmitReleaseMemEop32`).
  6. Dispara o anel avançando `CP_RB0_WPTR` (e sombra em memória).
  7. Lança loop de amostragem de `CP_RB0_RPTR` (MMIO e WB) e classifica o tripé via `Bc250GfxRingClassifyTripod` (redução de volta A15).
  8. Retorna sucesso somente se o tripé for comprovado (`BC250_TRIPOD_PROVEN`).
* Bringup Level 3 no miniport ativo (`Bc250ActiveRunGfxRingBringup`):
  - Mapeia a abertura de VRAM para CPU (`BC250_PCI_VRAM_APERTURE_BAR_INDEX`).
  - Executa `Bc250GfxRingTxnSubmitShReg`.
  - Desmapeia a VRAM.
  - Executa rollback incondicional via `Bc250GfxRingTxnRollback` restaurando o estado original de boot (Lei 5).

### 2.3 Correção de Nomenclatura: `CP_RB0` (ME/PFP) vs Node 1 (Compute MEC)
* O anel gráfico é estritamente `CP_RB0`, gerenciado pelo microprocessador de comandos ME/PFP (Ring 0 da engine gráfica).
* Node 1 refere-se exclusivamente às filas assíncronas de computação (MEC / HQD).
* Headers, funções e documentação utilizam a nomenclatura precisa de silício.

### 2.4 Remoção da Restrição Artificial `Rsrc1 == 0`
* A checagem foi ajustada para rejeitar estritamente endereço de microcódigo nulo (`PgmLo == 0`).
* `Rsrc1 == 0` é permitido pelo silício para shaders mínimos ou de depuração, mantendo a conformidade arquitetural.

### 2.5 Proibição Absoluta do Bit `SHADER_TYPE=COMPUTE` (docs/272 §5.3)
* Header Type-3: `BC250_GFX_PM4_SET_SH_REG_HEADER_4DW = 0xC0047600u`.
* Bit 1 (`BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN = 0x02u`) é proibido no anel gráfico.

---

## 3. Arquitetura dos Módulos

* **Emissão PM4 de Shaders:**
  - [`driver/core/bc250_gfx_sh_reg_packet.h`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/core/bc250_gfx_sh_reg_packet.h)
  - [`driver/core/bc250_gfx_sh_reg_packet.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/core/bc250_gfx_sh_reg_packet.c)
* **Transação Física no Anel:**
  - [`driver/core/bc250_gfx_ring_transaction.h`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/core/bc250_gfx_ring_transaction.h)
  - [`driver/core/bc250_gfx_ring_transaction.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/core/bc250_gfx_ring_transaction.c)
* **Miniport WDDM e Bringup Level 3:**
  - [`driver/wdk/bc250_active_miniport.h`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_active_miniport.h)
  - [`driver/wdk/bc250_active_miniport.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_active_miniport.c)
* **Testes Unitários:**
  - [`driver/tests/gfx_sh_reg_packet_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/gfx_sh_reg_packet_test.c)
  - [`driver/tests/gfx_ring_contract_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/gfx_ring_contract_test.c)

---

## 4. Portão de Segurança e Suíte de Mutações

* **Portão de Auditoria:** [`tools/audit-bc250mcdm-safety.py`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/audit-bc250mcdm-safety.py)
  - Função `audit_gfx_sh_reg_packet_is_honest`:
    - Exige opcode `0x76`, header `0xC0047600u`, contagens 6 e 12 dwords.
    - Exige reuso de `Bc250Pm4EmitSetShReg`.
    - Proíbe `RingBuffer[cursor++] = BC250_GFX_SH_PGM_LO_*` (escritas planas sem wrap).
    - Exige submissão com poison na fence, readback proof, e tripé classificado em `bc250_gfx_ring_transaction.c`.
    - Exige bringup Level 3 com chamada de submissão no miniport.
* **Suíte de Mutações B.2.4:** [`tools/test-b2-4-gfx-sh-reg-packet-mutations.py`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/test-b2-4-gfx-sh-reg-packet-mutations.py)
  - 26 mutações implementadas e **100% detectadas (26/26 PASS)**:
    - M1 a M7: Opcode, header Type-3, contagens, proibição de bit compute.
    - M8 a M9: Offsets de registradores P1 (VS 0x048, PS 0x008).
    - M10 a M13: Fail-closed (ring cheio, regs inválidos, stream mismatch, compute bit).
    - M14 a M19: Cobertura estrita no teste unitário.
    - M20 a M22: Reuso de `Bc250Pm4EmitSetShReg` e proibição de escrita plana sem wrap.
    - M23 a M26: Injeção de poison na fence, readback proof de poison, fence EOP e veredito de tripé.

---

## 5. Resultados de Verificação

| Verificação | Comando | Resultado |
|---|---|---|
| **Build de Contrato Offline** | `cmd.exe /c "driver\wdk\build-contract.bat"` | **EXIT=0** (`43 testes unitários passaram`) |
| **Auditoria Geral de Segurança** | `python tools/audit-bc250mcdm-safety.py` | **PASS** (`verdict: NAO VALIDADO EM HARDWARE`) |
| **Suíte de Mutações B.2.4** | `python tools/test-b2-4-gfx-sh-reg-packet-mutations.py` | **PASS (26/26 detectadas)** |
| **Suíte de Mutações B.2.3** | `python tools/test-b2-3-gfx-shader-staging-mutations.py` | **PASS (27/27 detectadas)** |
| **Suíte de Mutações B.2.2** | `python tools/test-b2-2-gfx-observable-shader-mutations.py` | **PASS (16/16 detectadas)** |
| **Suíte de Mutações B.2** | `python tools/test-b2-gfx-shader-mutations.py` | **PASS (15/15 detectadas)** |
| **Suíte de Mutações B.1** | `python tools/test-b1-gfx-ring-mutations.py` | **PASS (30/30 detectadas)** |
| **Total Mutações Parede B** | Mutações B.1 + B.2 + B.2.2 + B.2.3 + B.2.4 | **PASS (114/114 detectadas · 100%)** |
| **Integridade de Codificação** | Inspeção de BOM UTF-8 | **Sem BOM (`BOM: False` em todos os arquivos)** |

---

---

## 6. Primeira Corrida Física no Silício (14/09 18:31) e Diagnóstico

* **Evidência arquivada:** `docs/evidence/2026-09-14_18-31-53-DESKTOP-HQAG6T4-lab-binding`
* **Log do KMD (`display-log-this-bind-FINAL.txt`):**
  - `[GfxRing] PREFLIGHT OK: Reads=7 Writes=0` -> Somente leitura cumprida.
  - `[GfxRing] BRINGUP antes: CNTL=00A00000 BASE=FEDCBAEF ME_CNTL=15000000` -> Estado de boot conferido.
  - `[GfxRing] BRINGUP readback: CNTL=50000709 BASE=F40FFB00 ConfigOk=1 | desalt tentado=1 ok=1 ME_CNTL=00000000` -> Micro-engines ativas.
  - `[GfxRing] BRINGUP partida: tentada=1 ok=1 ACTIVE_readback=00000001` -> Anel ativo no silício.
  - `[GfxRing] BRINGUP Level 3 SubmitShReg: status=SUBMIT_TIMEOUT (11) TripodVerdict=3 Completed=0 FenceObs=0x43DAFFFE RPTR(MMIO=0x14 WB=0x0) Polls=2000`
* **Conclusões e Fatos Medidos no Silício:**
  1. **Anel Gráfico CP_RB0 Drenado (Perna 1):** O Command Processor consumiu os 20 dwords emitidos (`TargetWPTR=20 (0x14)` e `RPTR(MMIO)=0x14`). Shaders `SET_SH_REG` foram aceitos e executados sem exceção de GPU e sem hang.
  2. **Incompatibilidade de `RELEASE_MEM` sob VMID 0:** `RELEASE_MEM` emite fence via GPUVA através do cache L2/GL2 ou espera evento de fim de pipe gráfico (EOP). No bootstrap sob VMID 0 sem page tables nem draw call, a escrita de fence não atingiu a VRAM física (`FenceObs` permaneceu em `0x43DAFFFE`).
  3. **Rollback da Lei 5:** Operou com 100% de sucesso (`rollback tentado=1 ok=1`), mantendo o adaptador em Código 0 (`Status: OK`).

## 7. Correção Aplicada para a Próxima Corrida

* Substituição do pacote de fence em `Bc250GfxRingTxnSubmitShReg`:
  - Utiliza `PACKET3_WRITE_DATA` com controle `0x00100500u` (`WR_CONFIRM=1`, `DST_SEL=5 Memory Direct MC`), a forma canônica e comprovada em hardware (`bc250_pfp_direct_load.c`) para gravação física em VRAM sob VMID 0.
  - Acrescenta 3 dwords de NOP `PACKET2` (`0x80000000u`) para preservar o alinhamento de 4 dwords e manter exatamente 20 dwords emitidos (12 dw shader + 5 dw fence + 3 dw NOP), que já provaram drenar perfeitamente no silício (`RPTR=0x14`).
* Portão de segurança e suíte de mutações atualizados: **26/26 detectadas (100%)**.

---

## 8. Segunda Corrida Física no Silício (14/09 19:12) — TRIPÉ COMPLETO PROVADO (B.2.4 FECHADA NO SILÍCIO)

* **Evidência arquivada:** `docs/evidence/2026-09-14_19-12-57-DESKTOP-HQAG6T4-lab-binding`
* **Log do KMD (`display-log-this-bind-FINAL.txt` linhas 268-274):**
  ```text
  [GfxRing] BRINGUP level=3: RingMc=0xF40FFB0000 Bytes=8192 RptrWb=0xF40FFB2000 WptrShadow=0xF40FFB2010
  [GfxRing] BRINGUP: transporte JA MAPEADO por estagio anterior -- reusando, sem desmapear no fim
  [GfxRing] BRINGUP Level 3 SubmitShReg: status=OK (0) TripodVerdict=1 Completed=1 FenceObs=0x0000B240 RPTR(MMIO=0x14 WB=0x14) Polls=2
  [GfxRing] BRINGUP veredito: OK (level=3) Reads=15 Writes=20
  [GfxRing] BRINGUP antes: CNTL=00A00000 BASE=FEDCBAEF ME_CNTL=15000000 | planejado: CNTL=50000709
  [GfxRing] BRINGUP readback: CNTL=50000709 BASE=F40FFB00 ConfigOk=1 | desalt tentado=1 ok=1 ME_CNTL=00000000
  [GfxRing] BRINGUP partida: tentada=1 ok=1 ACTIVE_readback=00000001 | rollback tentado=1 ok=1
  ```
* **Medições Físicas no Silício:**
  1. **Perna 1 (RPTR == WPTR) Comprovada:**
     - Target WPTR = `0x14` (20 dwords = 12 dw shader + 5 dw `WRITE_DATA` + 3 dw NOP).
     - `RPTR(MMIO) = 0x14` e `RPTR(WB) = 0x14`.
     - O anel `CP_RB0` drenou completamente no silício.
  2. **Perna 2 (Fence Física sobre Poison) Comprovada:**
     - Poison inicial gravado pela CPU na VRAM: `0x43DAFFFE`.
     - Readback comprovou o poison antes da submissão.
     - A GPU (Command Processor ME/PFP) executou `PACKET3_WRITE_DATA` (`DST_SEL=5 Direct Memory MC`) e gravou `0x0000B240` na VRAM física em `0xF40FFB2020`.
     - Readback pós-disparo pela CPU na VRAM: `FenceObs = 0x0000B240` em apenas **2 polls** (`Polls=2`).
  3. **Perna 3 (Shaders Gráficos no Silício):**
     - Os 12 dwords de `SET_SH_REG` (VS `0x048` e PS `0x008`) foram consumidos pelo anel sem exceções e sem falha.
     - `TripodVerdict = 1 (BC250_TRIPOD_PROVEN)`.
  4. **Lei 5 (Rollback Limpo e Desligamento Controlado):**
     - O anel gráfico foi restaurado ao estado original de boot (`rollback tentado=1 ok=1`).
     - O StartDevice do WDDM prosseguiu com sucesso, atendeu 33 submissões e 31 Presents, e descarregou no StopDevice com fusível perfeitamente **CLEAN** (`Status=00000000`).

---

## 9. Próximos Passos — Rumo à Etapa B.3

Com B.1, B.2, B.2.2, B.2.3 e B.2.4 100% fechadas e validadas no silício:
* O anel gráfico `CP_RB0` está operacional, drena comandos, aceita registradores de shaders gráficos e grava fences físicas na VRAM.
* Shaders observáveis estão estagiados e prontos.
* **Próxima Etapa: B.3 — Geometria e Input Layout:**
  - Configuração de Viewport e Scissor (`PA_CL_VPORT_XSCALE`, etc.).
  - Configuração de Primitive Assembler (`VGT_PRIMITIVE_TYPE`).
  - Vertex Buffer Descriptors e Fetch Shader / Input Assembler.

