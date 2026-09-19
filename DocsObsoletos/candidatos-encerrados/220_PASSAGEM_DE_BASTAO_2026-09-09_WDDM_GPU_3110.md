# Passagem de bastão — WDDM acelerado na BC-250 (Candidato 3.110.1.0)

> **Data:** 2026-09-09 12:18 UTC-3  
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Pacote:** Candidato `3.110.1.0` (Lab Bring-up com RenderNode=1 e Contrato WDDM 2.7)  
> **Status:** Compilado, auditado, assinado e **NÃO INSTALADO** nesta sessão.  
> **Regra 10:** A execução física é prerrogativa e decisão exclusiva do operador humano (Daniel).

---

## 1. Contexto e Diagnóstico da Corrida 3.109.1.0

Na corrida física do candidato 3.109.1.0 (11:36:53):
1. O 1º submit Node 1 avançou `WPTR` de 204 para 216 e publicou o descritor `PACKET3_INDIRECT_BUFFER` (`0x3F`, 17 DW, `VA=0x11000`, VMID 9).
2. O tripé Node 1 falhou fechado: submetido 18, aposentado 17, `LastRptrMmio` no probe permaneceu 204, sem fence nem canários.
3. No TDR às 11:37:13, a nova telemetria capturou no silício:
   `CpcStalled=0x00040000` (`MEC2_WAIT_ON_RCIU_READ`), `CpStalled1=0x00000C00`, `Mec2Hdr=0xC0000A00`, `Mec2Pntr=0x000017FB`, `RPTR=0`, `WPTR=216`.
4. **Auditoria Externa (concordância mútua de 100%):**
   - `Mec2Pntr=0x17FB` e `Mec2Hdr=0xC0000A00` são o baseline de repouso do MEC2 (já medidos no preflight com `CpcStalled=0`); o delta real causado pela submissão foi `CpcStalled: 0 -> 0x00040000`.
   - O microengine MEC2 congelou aguardando leitura da RCIU.
   - Anomalia de RPTR: no probe constava `204` (valor residual do boot bringup), enquanto no MMIO do TDR constava `0`.
   - A GCVM teve probabilidade reduzida, mas não foi totalmente eliminada porque `*_FAULT_ENABLE_DEFAULT` redireciona faults para a dummy page e o TDR lia registradores de fault sem máscara de validade (`ValidReads`).
   - Política estrita de modificação: **nenhuma variável foi alterada às cegas** (zero alteração em `CGTT`, `PRIV_STATE` ou `KMD_QUEUE`). Foco absoluto em fechar as lacunas de observabilidade para provar a causa-raiz no próximo ensaio físico.

---

## 2. O que foi Implementado no Candidato 3.110.0.0 / 3.110.1.0

### A. Telemetria Pré-Submit e Pós-Doorbell no Caminho de Execução (`bc250_active_miniport.c`)
- Criado helper síncrono `Bc250ActiveCaptureMecTelemetry` que opera sob comutação explícita de banco (`SelectedSelector`) com confirmação imediata por readback de `GRBM_GFX_CNTL`:
  - **`[MEC_PRE_SUBMIT]`**: capturado imediatamente antes de `Bc250SubmissionEngineSubmit`, registrando o estado de repouso exato do hardware (RPTR, WPTR, Active, Error, HqStatus, IbCtrl, Vmid, CpfStalled, CpcStalled, Mec1Pntr, Mec2Pntr, CpStat).
  - **`[MEC_POST_DOORBELL]`**: capturado imediatamente após o toque da doorbell física, revelando se o cursor RPTR avança de imediato para 216 e em qual ciclo/estado o stall de RCIU se instala.

### B. Readback de Confirmação do Seletor de Banco HQD (`bc250_submission_engine.c`)
- No avanço do cursor e toque da doorbell (`Bc250SubmissionPublishWptr`):
  - Em `Step 4 QueueBank`, adicionado readback imediato de `GRBM_GFX_CNTL` emitido no log: `(Selector=0x%08X Readback=0x%08X)`.
  - Em `Step 4 DbHit`, adicionado readback de `GRBM_GFX_CNTL` emitido no log.
  - Isso elimina qualquer dúvida sobre qual fila/banco físico estava sendo lido quando `RPTR` ou `WPTR` foram amostrados.

### C. Máscara de Validade (`ValidReads`) e Leitura Verificada no TDR (`bc250_active_miniport.c`)
- No TDR Quiesce (`Bc250ActiveTdrQuiesceLifecycle`):
  - `FaultValid`: bitmask explícito (bits 0..2) que verifica se a leitura MMIO de `BC250_ACTIVE_GCVM_FAULT_STATUS`, `ADDR_LO` e `ADDR_HI` foi bem-sucedida, prevenindo falsos zeros.
  - `HqdValid`: bitmask explícito (bits 0..11) para todos os 12 registradores da fila HQD.
  - `CpValid`: bitmask explícito (bits 0..11) para os 12 registradores do pipeline CP/CPF/CPC.
  - Readback de `selReadback` e `restReadback` de `GRBM_GFX_CNTL`.

### D. Máscara de Validade no StopDevice e Archive Diagnostics
- Em `Bc250ActiveStopDeviceLifecycle`:
  - `[NODE1_STOP_GCVM_FAULT]` agora emite `Valid=0x%X`, comprovando a integridade das leituras no desligamento.
- Em `Bc250SubmissionEngineArchiveDiagnostics`:
  - `[MEC_STOP_QUEUE]` inclui `Selector=0x%08X (Req=0x%08X) RestoredSel=0x%08X`, confirmando que o encerramento do adaptador também consultou o banco correto.

---

## 3. Identidade Criptográfica do Pacote 3.110.1.0

Localização: `driver/wdk/package/output_lab`  
Manifesto: `driver/wdk/package/output_lab.manifest.json`

| Componente | SHA-256 |
|---|---|
| SYS fonte (compilado lab) | `86A364A9B545834274CE04B5CAD715CBEE7E271C9116851054BEA76C6FC259C7` |
| SYS assinado (pacote) | `196D6E60D9E92DEB03BD579526BC935C7963A592192C3CCDCBA0CEE6DED4C043` |
| UMD assinada | `35FBD78186D300D84F25DB76D1AF1C3025C7FECA3211CA422D35619B116AF214` |
| INF assinado | `666BC41E7B537FABD82CBC9205559E251EBCD03F7EA2668DD08A470621169651` |
| CAT assinado | `8D1ECF11800F3C00F48F54E13CD3A151DB5B134B6BA94AAD31FF5B41C4265DBF` |

Propriedades:
- `DriverVer`: `09/09/2026,3.110.1.0`
- `Profile`: `DISPLAY_RENDER_LAB` (`RenderNode=1`, `RenderOnlyLab=0`)
- `WDDM Contract`: `27` (`WDDM 2.7`)
- `Bring-up ativo`: `BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP` compilado.

Auditorias de segurança executadas e aprovadas:
- `driver\wdk\build-contract.bat`: **EXIT=0** (todas as 40+ suítes passaram)
- `tools/audit-bc250mcdm-safety.py`: **PASS**
- `tools/audit-umd-node1-submission.py`: **PASS**
- `tools/test-compute-vmid-audit.py`: **PASS**
- `tools/test-mec-mmio-safety.py`: **PASS**
- `tools/audit-bc250mcdm-lab-package.ps1`: **PASS**

---

## 4. O que a Próxima Corrida Física Irá Responder

1. **Anomalia RPTR 204 vs 0**:
   - `[MEC_PRE_SUBMIT]` e `[MEC_POST_DOORBELL]` emitirão o valor exato de `RPTR` no instante da submissão com readback explícito do seletor `GRBM_GFX_CNTL`. Veremos se `RPTR` já era 0 antes de submeter ou se avançou de 204 para 216.
2. **Momento do Stall RCIU**:
   - Se `[MEC_POST_DOORBELL]` já mostrar `CpcStalled=0x00040000`, saberemos que a recusa da RCIU acontece na primeira tentativa de leitura de registrador interno disparada pela doorbell.
3. **Integridade das Leituras de GCVM**:
   - Com `FaultValid` e `stopFaultValid`, teremos certeza matemática de que `FaultStatus=0` reflete ausência de fault no silício e não falha de acesso MMIO.
