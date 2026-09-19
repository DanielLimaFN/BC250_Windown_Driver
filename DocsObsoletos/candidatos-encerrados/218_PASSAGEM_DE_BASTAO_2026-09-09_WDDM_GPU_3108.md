# Passagem de bastão — WDDM acelerado na BC-250 (Candidato 3.108.1.0)

**Data:** 09/09/2026. **Workspace:** `C:\Users\Daniel\Desktop\NamixWDDM`.  
**Destinatária:** Daniel e próxima IA responsável pela continuidade.

---

## 1. Missão e Ponto Exato de Retomada

Candidato **3.108.1.0 (Lab Bring-up)** construído, auditado, assinado e **NÃO INSTALADO** nesta sessão.
Todas as verificações contratuais (`build-contract.bat`), auditorias de segurança de mutação (`test-mec-mmio-safety.py`), auditoria de safety MCDM (`audit-bc250mcdm-safety.py`), auditoria UMD (`audit-umd-node1-submission.py`) e auditoria de pacote (`audit-bc250mcdm-lab-package.ps1`) passaram com **PASS (STRUCTURAL ONLY) / 0 Falhas / 10 Mutações Rejeitadas / Pacote Lab Assinado**.

Este candidato resolve cirurgicamente o bloqueador isolado na corrida física de 09/09 09:29 (`docs/evidence/2026-09-09_09-29-24-DESKTOP-HQAG6T4-lab-binding/`): **a autorização de VMID para Indirect Buffers no Command Processor do MEC (`CP_HQD_VMID.IB_VMID`)**.

---

## 2. A Causa-Raiz Física Provada pelo Candidato 3.107.1.0

A mineração profunda dos 168 KB de log da corrida 09:29 trouxe três provas físicas (P0) incontestáveis:

1. **O buffer DMA da UMD estava 100% íntegro na VRAM física**:
   - `[SubmitVirtual Node1] IB_CPU_PREVIEW` confirmou os 17 DWORDs do pacote da UMD em PA `0x451FCC000` (DW0=`0xC0073700` WRITE_DATA com 5 canários `0xBC250C01..05`, DW9=`0xC0064900` RELEASE_MEM com fence `0xBC250F01`).
   - Zero corrupção de upload da UMD, zero defeito de mapeamento.

2. **A tradução MMU da GCVM para VMID 9 funcionou com ZERO falhas**:
   - `[NODE1_STOP_GCVM_FAULT] STATUS=0x00000000 ADDR=0x00000000:00000000`
   - `[TDR_MEC_SNAPSHOT] FaultStatus=00000000 FaultAddr=00000000:00000000`
   - A árvore de páginas mapeou com perfeição VA `0x11000` -> PA `0x451FCC000`.

3. **O descritor PM4 emitido no anel estava impecável**:
   - `DESCRIPTOR: Raw=C0023F00,00011000,00000000,09800011`
   - Opcode `0x3F` (`IT_INDIRECT_BUFFER`), Base `0x00011000`, DW3=`0x09800011` (`VMID=9`, bit 23 `VALID=1`, tamanho 17 DWORDs).

4. **Por que o MEC congelou no offset 204 (`RPTR=0` / `PROCESSING_IB=0`)?**
   - O registrador de hardware da fila `CP_HQD_VMID` (`0x0C830`) estava em `0x00000000`.
   - Conforme especificação GFX10 (`inc/linux_ref/gc_10_1_0_sh_mask.h:20211`):
     - bits [3:0] (`VMID`): VMID do anel (Host PA = 0).
     - bits [11:8] (`IB_VMID`): VMID autorizado para IBs.
   - O anel principal roda em Host PA (`VMID = 0`), portanto `CP_HQD_VMID.VMID = 0`.
   - Porém, `CP_HQD_VMID.IB_VMID` também estava em `0`!
   - Quando o Command Packet Fetcher (CPF) do MEC leu no offset 204 o descritor `PACKET3_INDIRECT_BUFFER` com `VMID = 9`, a unidade de despacho do microcode detectou que o VMID requisitado pelo pacote (`9`) não combinava com o VMID autorizado na fila (`IB_VMID = 0`).
   - O CPF paralisou o consumo PM4 no offset 204:
     - `RPTR` não avançou sequer 1 DWORD.
     - `CP_HQD_IB_CONTROL` permaneceu `0x00000000` (bit 31 `PROCESSING_IB = 0`).
     - O hardware não disparou bus error (`CP_HQD_ERROR = 0x00000000`), mas a fila ficou ocupada aguardando autorização (`CP_HQD_HQ_STATUS0 = 0x00008040`, sem `IDLE`), até que o scheduler do dxgkrnl disparou TDR após 2 segundos.

---

## 3. As Modificações Estruturais no Candidato 3.108.1.0

1. **Programação Física de `CP_HQD_VMID` em `Bc250ActivePrepareComputeVmid` (`bc250_active_miniport.c`)**:
   - Sob o spinlock do motor de submissão (`Context->SubmissionEngine.Lock`):
   - Atribui `ComputeRing.Vmid = BC250_GCVM_COMPUTE_VMID` (9).
   - Seleciona o banco da HQD via `BC250_GRBM_GFX_CNTL_BYTE_OFFSET` com `SelectedSelector`.
   - Grava `targetHqdVmid = ((uint32_t)BC250_GCVM_COMPUTE_VMID << 8)` (`0x00000900`) em `BC250_HQD_MMIO_VMID` (`0x0C830`):
     - `VMID` (bits [3:0]) = 0 (anel principal permanece em Host PA).
     - `IB_VMID` (bits [11:8]) = 9 (autoriza formalmente a execução de IBs no VMID 9).
   - Efetua readback imediato de `BC250_HQD_MMIO_VMID` para certificar retenção no silício.
   - Restaura o seletor original (`OriginalSelector`).
   - Registra no log: `[SubmitVirtual Node1] HQD_VMID: Target=0x%08X Readback=0x%08X`.

2. **Sincronização e Validação em `Bc250SubmissionPublishWptr` (`bc250_submission_engine.c`)**:
   - No Step 4 de publicação de WPTR, com o banco da fila selecionado via MMIO:
   - Lê `BC250_HQD_MMIO_VMID`.
   - Se divergir de `(Engine->ComputeRing.Vmid << 8)`, reprograma e valida.
   - Exibe o valor de `HqdVmid` na telemetria síncrona: `[SubmissionPublishWptr] Step 4 QueueBank: ... HqdVmid=0x%08X`.

3. **Observabilidade Instantânea no TDR (`[TDR_MEC_SNAPSHOT]`)**:
   - Adicionada a leitura de `BC250_HQD_MMIO_VMID` no snapshot do TDR, confirmando se o registrador manteve `0x00000900` em caso de timeout.

---

## 4. Identidade do Pacote Candidato 3.108.1.0

Localização: `driver/wdk/package/output_lab`  
Manifesto: `driver/wdk/package/output_lab.manifest.json`

| Artefato | SHA-256 |
|---|---|
| SYS fonte (compilado lab) | `8A769751C5D5D9739509C4AC79133B2BC2A676E469C369882BA7578D0D5501B6` |
| SYS assinado (pacote) | `07F9CBA05BF23C02FD5A0479BD65D260AAE0F85193E021BC4EAD6C2545EECA08` |
| UMD assinada | `35FBD78186D300D84F25DB76D1AF1C3025C7FECA3211CA422D35619B116AF214` |
| INF assinado | `81C8E62006CEA80A570CB3EB042F533A226B74F422BA4CDAF05076AA2D178C36` |
| CAT assinado | `9A03C57235C0505209B09E8EADBD1E4E949082F6E0E3D331A9977404DDDAE70F` |

Propriedades:
- `DriverVer`: `09/09/2026,3.108.1.0`
- `Profile`: `DISPLAY_RENDER_LAB` (`RenderNode=1`, `RenderOnlyLab=0`)
- `WDDM Contract`: `27` (`WDDM 2.7`)
- `Mode`: `P1 PERSISTENT SUBMISSION`
- `Bring-up ativo compilado`: **SIM** (`BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP` ativo na imagem de laboratório)

---

## 5. Instruções Cirúrgicas para o Ensaio Físico no Hardware (Daniel)

Conforme a **Regra 10**, a execução física é realizada **exclusivamente por Daniel** após cold boot da APU.

### Passo 1: Boot Frio Obrigatório
Desligar e ligar a máquina fisicamente (cold boot) para garantir reset elétrico total dos blocos PCIe, GFXHUB e MEC da BC-250.

### Passo 2: Binding do Candidato 3.108.1.0
Em prompt do PowerShell com privilégios de Administrador na raiz do repositório:
```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod -WatchdogTimeoutSeconds 180
```

### Passo 3: O que observar no log
Procurar as linhas críticas no log pós-corrida:
1. `[SubmitVirtual Node1] HQD_VMID: Target=0x00000900 Readback=0x00000900`
2. `[SubmissionPublishWptr] Step 4 QueueBank: ... HqdVmid=0x00000900`
3. Se o MEC consumiu o pacote no offset 204:
   - `RPTR` avança de 204 para 216!
   - `[MEC_STOP_QUEUE] ... RPTR=216` (ou `RPTR=12` na janela relativa).
   - `CP_HQD_IB_CONTROL` ativa bit 31 (`PROCESSING_IB = 1`).
4. Se o tripé da UMD fecha com sucesso:
   - Os 5 canários (`0xBC250C01..05`) gravados no destino e a fence (`0xBC250F01`) concluída.
