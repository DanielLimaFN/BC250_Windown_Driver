# 527 — T3.3: SDMA UTCL1 sem FORCE_INVALIDATION (alinhado ao Linux upstream); critério da Corrida 173

> 29/09/2026, offline. Candidato **3.318.1.0** (inclui HDP.1, T3.1, EVT.1, X1, X2a, X3, X4 e T3.3). **Nada desta faixa foi validado em hardware.**

---

## 1. Por quê

A análise aprofundada dos fontes do Linux upstream (`amdgpu_discovery.c`, `sdma_v5_0.c`, `cyan_skillfish_reg_init.c`, `gmc_v10_0.c`) e Mesa (`ac_cmdbuf_sdma.c`) revelou a causa da quebra de pipeline no SDMA nas últimas 14 corridas:

1. **Assinatura das travas (P0 nas Corridas 168–172):**
   - 14 de 14 travas terminavam durante a execução de um `FILL` de paginação no SDMA.
   - O tráfego de cópia linear (`TRANSFER`) nunca travou a máquina, apenas o preenchimento repetido (`CONST_FILL`).
2. **A divergência com o Upstream Linux (T3.3):**
   - Em `Bc250ActiveFlushMappedGfxhubTlb` (`driver/wdk/bc250_active_miniport.c`), a cada `FLUSH_TLB` executava-se:
     ```c
     utcl1Val |= BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION;
     Bc250MmioTopologyWrite32(&active->Core.MmioTransport, BC250_SDMA0_REG_UTCL1_CNTL, utcl1Val);
     ```
   - Esse bit 14 (`FORCE_INVALIDATION`) **nunca era limpo**. Ele permanecia permanentemente ligado a partir do primeiríssimo `FLUSH_TLB` da inicialização.
   - No Linux upstream (`sdma_v5_0.c:799-803`), `SDMA0_UTCL1_CNTL` é escrito **uma única vez** no boot (`RESP_MODE=3`, `REDO_DELAY=9`). O bit `FORCE_INVALIDATION` **NUNCA** é acionado em tempo de execução.
   - O flush de TLB no Linux (`gmc_v10_0.c:235-300`, `gmc_v10_0_flush_gpu_tlb`) atua exclusivamente via registradores de invalidação do GFXHUB, sem jamais tocar na micro-TLB do SDMA.
3. **Mecanismo da trava:**
   - Com `FORCE_INVALIDATION=1` permanente, a micro-TLB UTCL1 do SDMA era impedida de cachear traduções. A cada batida de burst na rajada de `CONST_FILL` (milhares de dwords consecutivos na velocidade máxima da engine), a micro-TLB emitia uma requisição de invalidação ao GFXHUB.
   - Isso causava estouro nas filas de requisição do GFXHUB (`WRREQ_DATA_FIFO_FULL / RQPG_REQPAGE_FIFO_FULL`), bloqueando a engine SDMA e gerando um deadlock no barramento com o Data Fabric / Memory Controller.
   - O controlador de memória travado parava de emitir ciclos de refresh, o que causava o decaimento progressivo dos dados na DRAM energizada (medido no doc 506) e impedia até mesmo o watchdog do Windows de gerar BSOD.
   - Antes de 18/09, um bug silencioso de comparação impedia que esse bloco fosse executado; corrigido em 18/09, o bit passou a ser gravado em hardware, desencadeando a sequência ininterrupta de travas em 100% das corridas.

---

## 2. A mudança (alinhamento estrito ao Linux upstream)

1. **Remoção de `FORCE_INVALIDATION` no `FLUSH_TLB`:**
   - O bloco que realizava o RMW de `BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION` em `Bc250ActiveFlushMappedGfxhubTlb` foi completamente eliminado. O `FLUSH_TLB` do miniport passa a invalidar a TLB de forma limpa apenas pelo GFXHUB, como no Linux `gmc_v10_0.c`.
2. **Garantia de bit 14 zerado no init e no restart:**
   - Em `Bc250SdmaPresentInit` e `Bc250SdmaPresentRestartAfterTdr` (`bc250_sdma_present.c`), a escrita em `BC250_SDMA0_REG_UTCL1_CNTL` inclui explicitamente `BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION` na máscara de limpeza:
     ```c
     (value & ~(0x00000E00u | 0x0000003Eu | BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION)) | (3u << 9) | (9u << 1)
     ```
   - As golden settings de Cyan Skillfish (`sdma_v5_0.c:186-215`) são mantidas: `CHICKEN_BITS = 0x03AB0107`, `UTCL1_PAGE = 0x004C5C00` (`CACHE_WRITE_POLICY_L2__BYPASS`).

---

## 3. Provas offline

- Suíte de contratos completa e testes de unidade: **EXIT 0**.
- Compilação de laboratório (`build-bc250mcdm-lab-bringup.ps1`): **EXIT 0**.
- Auditoria de segurança (`audit-bc250mcdm-safety.py`): **PASS**, com o novo gate `audit_t33_utcl1_no_force_invalidation`.
- **3 mutações testadas e todas reprovadas** (`tools/mutacoes/mutate_t33.py`):
  - M1: `Bc250ActiveFlushMappedGfxhubTlb` volta a referenciar `FORCE_INVALIDATION` (Reprovou: ok).
  - M2: `Bc250SdmaPresentInit` não mascara `FORCE_INVALIDATION` (Reprovou: ok).
  - M3: `Bc250SdmaPresentRestartAfterTdr` não mascara `FORCE_INVALIDATION` (Reprovou: ok).
- Auditoria de empacotamento (`audit-bc250mcdm-lab-package.ps1`): **PASS**.
- Verificação de strings no `.sys` assinado: string antiga `UTCL1 FORCE_INVALIDATION nao aplicado` ausente no binário.
- Script de preflight (`prepare-corrida173-t33.ps1`): **EXIT 0**.

---

## 4. Candidato

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `66581CF4B1C3300F1C8D0BC1713494FCB0B3BC97EB278F54AF1085890BC68E03` |
| `bc250umd.dll` (idêntica à 3.310) | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `DF018CF7B6E0CE6A43898AC80223E31749126A8B64451FD9624F22B891E8E805` |
| `bc250mcdm.cat` | `C0C67B08324EEDBF9A5883438B1EE395E487E693938241345041474F08DE09D4` |

- DriverVer: `09/29/2026,3.318.1.0` (fonte `3.318.0.0`).
- Runner: `tools/EXECUTAR_CORRIDA173_T33_600S.cmd`.

---

## 5. Critério da Corrida 173 (escrito antes; Lei 10)

**Roteiro:** O mesmo da Corrida 165 (atividade mínima, sem harness). Assim que o desktop estabilizar (~20 s), clicar para abrir e fechar o menu Iniciar repetidas vezes (para gerar novas alocações e testar o caminho de paginação).

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | **600 s sem trava** e menu Iniciar aberto e fechado várias vezes sem congelamento e zero paradas no SDMA e Presents fluindo normalmente | **Causa raiz confirmada:** a micro-TLB com `FORCE_INVALIDATION` permanente no runtime era a responsável direta pelas paradas no `FILL`. Procede-se para teste de repetição para validação estatística. |
| **B** | Trava na partida antes de 200 s com a mesma assinatura (último evento = submit de FILL no SDMA sem conclusão) | A invalidação permanente não era o único gatilho; avança-se para **X2b** (substituição de `CONST_FILL` por cópia linear de bloco zerado). |
| **C** | Regressão funcional: faults de GCVM, falhas de tradução de página ou TDR | A micro-TLB exigia alguma invalidação específica não coberta pelo GFXHUB. Investigar antes de nova corrida. |
| **E** | Fora do mapa (BugCheck com dump, falha em DDI diferente) | Descrever antes de julgar. |
