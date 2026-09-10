# 209 — Instrumentação Total de Submissão Node 1 & Candidato Lab 3.96.1.0

> **Data:** 08/09/2026  
> **Candidato:** Lab `3.96.1.0` (SYS `72A7F0E0`, UMD `35FBD781`, CAT `36EF9166`, INF `ECC9BE53`)  
> **Status:** Compilado, assinado e auditado em `driver\wdk\package\output_lab\` · **NÃO INSTALADO**  
> **Precedente:** Corrida física de 16:44 (`docs/evidence/2026-09-08_16-44-52-DESKTOP-HQAG6T4-lab-binding/`) e Handoffs 206/208.

---

## 1. Diagnóstico Forense da Corrida 16:44 (P0)

Na corrida física de 08/09 às 16:44:
- O sistema subiu sob watchdog de 180s e preservou `BasicDisplay` (Código 0) após o rollback limpo.
- O DWM fail-fastou 8 vezes com `0xe0464645` / `E_NOTIMPL` em `OpenAdapter10_2`, como esperado (`BC250_D3D10_11_NATIVE_READY=0`).
- O fusível anti-boot-loop permaneceu íntegro (`CLEAN (0)`, Boots=11, State=0).
- O harness UMD `bc250-umd-node1-tripod.exe` inicializou a winsys com sucesso (alocou command buffer em `VA=0x11000` e proof buffer em `VA=0x21000`), emitiu o flush de 68 bytes para o Node 1, mas `WaitForFenceFromCpu` retornou `CurrentFence=18446744073709551615` (`UINT64_MAX`).
- A telemetria de hardware registrou:
  - `P1LastWptrMmioReadback`: manteve-se em **204** (na corrida de 15:43 havia avançado para 216).
  - `P1SubmitAccepted`: manteve-se em **17**.
  - O KMD emitiu `[SubmitVirtual Node1] GATE ABERTO: ...`, mas **nem** `VMID9_BIND` **nem** `PUBLICADO NO ANEL MEC` apareceram no log.

### Causa Raiz: Violação da Lei 4 (Silêncio na Falha)
A investigação no código de `bc250_active_miniport.c` revelou que, após a introdução de `Bc250ActivePrepareComputeVmid`:
1. Entre o log de `GATE ABERTO` e o `Bc250SubmissionEngineSubmit`, existiam **cinco pontos de retorno precoce** que retornavam códigos de status de erro ao `dxgkrnl` em silêncio absoluto.
2. Ao receber status de erro em `DxgkDdiSubmitCommandVirtual`, o runtime do DirectX cancela o lote e grava `UINT64_MAX` nas cercas monitoradas associadas.
3. Dentro de `Bc250ActivePrepareComputeVmid`, re-lookups redundantes no registro de objetos dependiam de contadores e sincronizações desnecessárias, enquanto a estrutura `BC250_CONTEXT_SUBMISSION_VIEW` já havia consolidado a raiz validada em Step 2 (`View->RootPageTableAddress`, `View->RootPageTableSegmentId == 1`).
4. Qualquer recusa ou erro transitório causava o aborto instantâneo da submissão sem qualquer rastro no log de display.

---

## 2. Alterações Entregues

### A. Eliminação de Todas as Saídas Silenciosas em `Bc250ActiveSubmitCommandVirtualBody`
Cada ponto de validação e retorno agora emite log explícito no buffer de telemetria:
- **Step 2:** `[SubmitVirtual Node%u] RECUSA: Bc250McdmContextAcquireSubmissionView falhou (objStatus=%u hContext=%p)`
- **Step 3:** `[SubmitVirtual Node%u] RECUSA: Bc250GpuvaSnapshotAcquire falhou (vaStatus=%u)`
- **Step 4:** `[SubmitVirtual Node%u] RECUSA: Bc250GpuvaSpaceValidateRangeCoverage falhou (vaStatus=%u VA=0x%llX Size=%u)`
- **Step 5:** `[SubmitVirtual Node%u] RECUSA: Fence reserve sync falhou (status=0x%08X reserved=%u accepted=%u)`
- **Step 6:** `[SubmitVirtual Node%u] RECUSA: PrepareComputeVmid/Submit falhou (submitStatus=0x%08X)`
- **Step 7:** `[SubmitVirtual Node%u] RECUSA: Fence commit sync falhou (status=0x%08X committed=%u accepted=%u)`

### B. Resolução Direta e Confiável de Raiz em `Bc250ActivePrepareComputeVmid`
- `Bc250ActivePrepareComputeVmid` utiliza diretamente os campos já autenticados e thread-safe de `View`:
  ```c
  if (View->RootPageTableSegmentId == 1u && View->RootPageTableEntries > 0u) {
      rootOffset = View->RootPageTableAddress;
  }
  ```
- Mantém fallback defensivo no registro de objetos, com log detalhado de recusa caso a raiz não seja localizada.
- Instrumenta falhas de `Bc250ActiveQueryRuntimeCaps`, `Bc250SubmissionEnginePoll` e `Bc250GcvmComputeInstall` com telemetria explícita.

### C. Versionamento do Driver
- Fonte WDK incrementado para `3.96.0.0` (`driver/wdk/package/bc250mcdm.inf`).
- Pacote de laboratório gerado como `3.96.1.0`.

---

## 3. Verificação Offline & Auditorias

| Verificação | Comando | Resultado |
|---|---|---|
| Suíte Completa de Contratos WDK | `cmd.exe /c driver\wdk\build-contract.bat` | **EXIT=0** (Todos os testes e matriz DDI aprovados) |
| Auditoria de Segurança WDDM/MCDM | `python tools/audit-bc250mcdm-safety.py` | **PASS** (17 portões de segurança cumpridos) |
| Auditoria de Mutações VMID9 | `python tools/test-compute-vmid-audit.py` | **PASS** (Baseline e 4 mutações rejeitadas com sucesso) |
| Auditoria de Submissão UMD Node 1 | `python tools/audit-umd-node1-submission.py` | **PASS** (Cadeia UMD validada) |
| Build da Imagem de Laboratório | `tools\build-bc250mcdm-lab-bringup.ps1` | **EXIT=0** (Binário `output_lab\bc250mcdm.sys` verificado) |
| Empacotamento do Pacote de Laboratório | `tools\package-bc250mcdm-lab.ps1` | **PASS** (Cat gerado e assinado via certificado de teste) |
| Auditoria do Pacote de Laboratório | `tools\audit-bc250mcdm-lab-package.ps1` | **PASS** (DriverVer 3.96.1.0, WDDM 2.7, DISPLAY_RENDER_LAB) |

---

## 4. Artefatos de Build

- **Diretório:** `driver\wdk\package\output_lab\`
- **Manifesto:** `driver\wdk\package\output_lab.manifest.json`
- **Hashes Criptográficos SHA-256:**
  - `bc250mcdm.sys`: `72A7F0E0882CF719DB73F473181687BBAD1D67B1B734B44ABF725176DC33299D`
  - `bc250umd.dll`: `35FBD78186D300D84F25DB76D1AF1C3025C7FECA3211CA422D35619B116AF214`
  - `bc250mcdm.inf`: `ECC9BE53CFCFE1E254AE3335E67D0949E3CD810D019A6931B0867928BAFDD2E8`
  - `bc250mcdm.cat`: `36EF91663F537B852A7AF79951231B9D456572A30A6179A0DA930DC1BA3335C7`

---

## 5. Protocolo para Próximo Ensaio Físico

> [!CAUTION]
> **Teto: 1 corrida física.** É mandatória a reinicialização a frio do host físico para satisfazer o gatilho de segurança `C:\BC250_REQUIRE_CLEAN_BOOT.json`.

### Passo 1: Reboot a frio
Executar no terminal elevado:
```cmd
shutdown /r /t 0
```

### Passo 2: Execução do Bind de Laboratório com Harness UMD Node 1
Após o boot e login no Windows 10 22H2:
```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod -WatchdogTimeoutSeconds 180 -Execute -IAcceptBlackScreenRisk
```

### Passo 3: Critério de Avaliação nos Logs Físicos
Inspecionar o arquivo `display-log-this-bind.txt` arquivado na pasta de evidência:
1. `[SubmitVirtual Node1] VMID9_BIND: Root=0x... Status=0 Ready=1`
2. `[SubmitVirtual Node1] PUBLICADO NO ANEL MEC: FenceId=1 AssignedFence=... DmaVA=0x11000`
3. Avanço no MMIO: `P1LastWptrMmioReadback` de 204 para 216 dwords.
4. Fechamento do tripé físico: `P1LastRptrMmio == 216` (`RPTR == WPTR`).
5. Escrita física da monitored fence (`0xBC250F01`).
6. Canários em VRAM preenchidos no endereço `0x21000`.
