# Handoff 229 — Nó 1 Compute e Tripé UMD Fechados no Silício

> **Marco histórico do projeto:** Frente 1 (Nó 1 / Compute MEC / UMD Tripod) 100% comprovada no silício.
> **Data:** 2026-09-09 · **Corrida:** 19:56:09 (`2026-09-09_19-56-09-DESKTOP-HQAG6T4-lab-binding`).
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64.

---

## 1. O Veredito de Hardware (Medição Física P0)

Na corrida de 09/09 às 19:56:09 com o candidato lab `3.119.1.0` e os flags:
`-EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -RunUmdNode1Tripod`,
a pilha de compute e UMD obteve **conclusão integral no silício**.

### 1.1 Evidência no KMD (Drenagem do Anel MEC e Inexistência de Stalls)
Trecho de `bc250_display_log-full.txt:84354-84386`:
```text
[SubmitVirtual Node1] GATE ABERTO: GpuvaMappingsOperational=1; publicando IB de 68 bytes em VA=0x11000 no anel persistente MEC (FenceId=1)
[SubmitVirtual Node1] VMID9_BIND: Root=0x45FFB0000 Submitted=17 Retired=17 Status=0 Ready=1
[SubmitVirtual Node1] HQD_VMID: Target=0x00000900 Readback=0x00000900
[MEC_PRE_SUBMIT] Active=1 RPTR=0 WPTR=0
[MEC_PRE_SUBMIT_CPF] CpValid=0x3FF CpfStat=88004001 CpfBusy=20000000 CpfStalled=00000000 CpcStat=00000000 CpcBusy=00000000 CpcStalledBeforeHqd=00000000 CpcStalled=00000000 Mec1Pntr=00000000 Mec2Pntr=00000491
[SubmissionEngineSubmit] SUCESSO: NewWptr=12 Fence=18 Pad=0 Accepted=18 Kicks=18
[MEC_POST_DOORBELL] Selector=0x00000000 (Req=0x00000008) RestoredSel=0x00000000 HqdValid=0x7F Active=00000001 RPTR=12 WPTR=12 Error=00000000 HqStatus=40008040 IbCtrl=00000000 Vmid=00000900
[SubmitVirtual Node1] SUBMIT SUCESSO: AssignedFence=18 RingWptr=12 WptrMmio=12
[SubmitVirtual Node1] DESCRIPTOR: Raw=C0023F00,00011000,00000000,09800011
[SubmitVirtual Node1] FENCE COMMIT: Token=1 RingFence=18 committed=1 accepted=1 status=0x00000000
[SubmitVirtual Node1] PUBLICADO NO ANEL MEC: FenceId=1 AssignedFence=18 DmaVA=0x11000 DmaSize=68
```
- O par de ponteiros físicos drenou completamente: `RPTR == WPTR == 12`.
- O Command Processor Fetcher (CPF) leu o descritor PM4 e engajou o motor de compute sem stalls (`CpcStalled=00000000`).
- Fence 18 do anel foi commitada.

### 1.2 Evidência na UMD (Harness B7_UMD_NODE1_TRIPOD)
Trecho de `B7_UMD_NODE1_TRIPOD_2026-09-09_19-56-15-346/umd-log.txt:26`:
```text
NODE1_TRIPOD_RESULT: hr=0x00000000 Dst=0x21000 FenceGpuVa=0x10040 FenceObs=0xBC250F01 Poison=1 Submitted=1 Fence=1 Data=1 Cleanup=1 Values=BC250C01,BC250C02,BC250C03,BC250C04,BC250C05
```
Trecho de `harness.stderr.txt:24-25`:
```text
[WDDM_WINSYS pid=5148] WaitForFenceFromCpu ENTER: TargetFence=3156545281 CurrentFence=1138425854 SyncObj=0x40000200
[WDDM_WINSYS pid=5148] WaitForFenceFromCpu LEAVE: hr=0x00000000 TargetFence=3156545281 CurrentFence=3156545281
```
1. A GPU executou o shader de compute a partir da GPUVA `0x11000`.
2. A GPU substituiu o poison da fence monitorada por `3156545281` (`0xBC250F01`), liberando o `WaitForFenceFromCpu` do runtime WDDM com `hr = 0x00000000`.
3. A CPU leu a VRAM física de destino (`0x21000`) e conferiu **todos os cinco canários**:
   `BC250C01`, `BC250C02`, `BC250C03`, `BC250C04`, `BC250C05`.
4. O processo fez cleanup, desmontou as alocações e retornou código de saída limpo.

---

## 2. Diagnóstico da Tela Preta e do BugCheck 0x119

1. **A Tela Preta:**
   - O miniport está ativo em VidPn único, scanout real e VSync a 60 Hz.
   - O Desktop Window Manager (`dwm.exe`) tenta criar o dispositivo Direct3D 10/11 chamando `OpenAdapter10_2` e `GetCaps(Type=130 - D3D11DDICAPS_3DPIPELINESUPPORT)`.
   - Com `BC250_D3D10_11_NATIVE_READY = 0`, a UMD devolve `E_NOTIMPL`. O DWM não aceita um adaptador sem suporte a pipeline 3D e fail-fasta (`0x887A0004` - `DXGI_ERROR_UNSUPPORTED`).
   - Sem o compositor desktop ativo, a primária fica sem janelas e o monitor permanece preto.

2. **O BugCheck 0x119 aos 74 segundos:**
   - Como a tela estava preta, o operador não pôde confirmar visualmente o display.
   - O Watchdog de Laboratório (`watch-bc250mcdm-lab.ps1`) atingiu o timeout de 75 segundos e iniciou a recuperação automática via `rollback-bc250mcdm-lab.ps1`.
   - O script executou diretamente `pnputil.exe /delete-driver ... /uninstall /force` com o driver de vídeo em execução plena (VSYNC ativo a 60 Hz e VidMm alternando contextos).
   - A remoção surpresa forçada do miniport fez o `dxgmms2.sys` abortar a fila de paginação com `STATUS_IO_DEVICE_ERROR` (`0xC0000185`), gerando o BugCheck:
     `0x00000119 (0x0000000000000002, 0xffffffffc0000185, 0xffffb2098bb79960, 0xffffba83046304b0)`.
   - O fusível anti-boot-loop em `C:\BC250_BOOT_FUSE.dat` interceptou o encerramento não limpo (`State=2 TRIPPED`), bloqueando `StartDevice` no reboot subsequente e mantendo o sistema em Código 43 sem loops de boot.

---

## 3. Correções Aplicadas

1. **Desativação Graciosa no Rollback:**
   - Em `tools/rollback-bc250mcdm-lab.ps1`, adicionado `pnputil.exe /disable-device $instanceId /force` antes de qualquer chamada a `/delete-driver /uninstall /force`.
   - Isso replica o fluxo testado de `C:\BC250_RECOVERY.cmd`, permitindo que o `dxgmms2` desmonte o driver graciosamente antes da desinstalação dos arquivos.

---

## 4. O Próximo Desafio: Frente 2 (D3D10/11 UMD para o DWM)

Com a Frente 1 (Compute MEC / Nó 1) e o Nó 0 (SDMA) totalmente provados em hardware:
- **Próximo bloqueador:** Fazer o DWM receber com sucesso um dispositivo D3D10.1/11 da UMD.
- **Passos:**
  1. Fornecer `D3D11DDI_3DPIPELINELEVEL_10_1` em `GetCaps(D3D11DDICAPS_3DPIPELINESUPPORT)`.
  2. Implementar as funções base do DXGI (`pfnPresent`, `pfnBlt`) na UMD.
  3. Promover a UMD para negociar com o DWM sem entrar em fail-fast.
