# 206 — Handoff 2026-09-08 15:43 · Primeiro Submit Node 1 da História no Silício · Disputa do VMID 8

> **Corrida:** 08/09 15:43 (`2026-09-08_15-43-55-DESKTOP-HQAG6T4-lab-binding`)  
> **Candidato:** Lab `3.94.2.0` (SYS `8CA949DC`, UMD `35FBD781`, CAT `5FB2E86E`, INF `A7DD66EE`)  
> **Flags:** `-EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod -WatchdogTimeoutSeconds 180`  
> **Resultado:** Código 0 · Faixa B7.1 validada (P0) · Primeiro submit Node 1 no silício (P0, WPTR 204→216) · Tripé Node 1 FAIL-CLOSED (RPTR travou em 204, fence=UINT64_MAX, canários zerados) · Disputa de VMID 8 evidenciada no log · Rollback limpo para BasicDisplay (Código 0).  
> **Evidência arquivada:** `docs/evidence/2026-09-08_15-43-55-DESKTOP-HQAG6T4-lab-binding/`

---

## 1. Faixa B7.1 Validada no Silício: A UMD Desatravancou de Ponta a Ponta

Na corrida de 15:18 (`docs/205`), o harness `bc250-umd-node1-tripod.exe` foi abortado prematuramente porque `MapAllocation` tratava `0x8000000A` (`E_PENDING`) como falha fatal.

A correção implementada na Faixa B7.1 (`bc250_wddm_winsys.c`) foi exercitada integralmente no silício na corrida de 15:43 (`harness.stderr.txt`, PID 10892):

```text
[WDDM_WINSYS pid=10892] Allocate ENTER: Token=0x434D44002A8C0001 Size=65536 Align=4096
[WDDM_WINSYS pid=10892] Allocate LEAVE: hr=0x00000000 hAlloc=0x40000180
[WDDM_WINSYS pid=10892] MapAllocation ENTER: hAlloc=0x40000180 Min=0x10000 Max=0x3FFFFFFFF Pages=16 Exec=1
[WDDM_WINSYS pid=10892] MapAllocation LEAVE: hr=0x8000000A VA=0x11000 PagingFence=7001
[WDDM_WINSYS pid=10892] MakeResident ENTER: hAlloc=0x40000180 hPagingQueue=0x40000100
[WDDM_WINSYS pid=10892] MakeResident LEAVE: hr=0x8000000A PagingFence=7002
[WDDM_WINSYS pid=10892] Allocate ENTER: Token=0x505246002A8C0001 Size=4096 Align=4096
[WDDM_WINSYS pid=10892] Allocate LEAVE: hr=0x00000000 hAlloc=0x400001c0
[WDDM_WINSYS pid=10892] MapAllocation ENTER: hAlloc=0x400001c0 Min=0x10000 Max=0x3FFFFFFFF Pages=1 Exec=0
[WDDM_WINSYS pid=10892] MapAllocation LEAVE: hr=0x8000000A VA=0x21000 PagingFence=7003
[WDDM_WINSYS pid=10892] MakeResident ENTER: hAlloc=0x400001c0 hPagingQueue=0x40000100
[WDDM_WINSYS pid=10892] MakeResident LEAVE: hr=0x8000000A PagingFence=7004
[WDDM_WINSYS pid=10892] WaitPaging ENTER: TargetFence=7004 CurrentFence=7003 SyncObj=0x400000c0
[WDDM_WINSYS pid=10892] WaitPaging LEAVE: hr=0x00000000 TargetFence=7004 CurrentFence=7004
[WDDM_WINSYS pid=10892] Upload ENTER: hAlloc=0x400001c0 Bytes=20
[WDDM_WINSYS pid=10892] Upload LEAVE: hr=0x00000000 Bytes=20
[WDDM_WINSYS pid=10892] Read ENTER: hAlloc=0x400001c0 Bytes=20
[WDDM_WINSYS pid=10892] Read LEAVE: hr=0x00000000 Bytes=20
[WDDM_WINSYS pid=10892] CreateMonitoredFence ENTER: InitialValue=1138425854
[WDDM_WINSYS pid=10892] CreateMonitoredFence LEAVE: hr=0x00000000 hSync=0x40000200 CpuVa=00000000009A0040 GpuVa=0x10040
[WDDM_WINSYS pid=10892] Upload ENTER: hAlloc=0x40000180 Bytes=68
[WDDM_WINSYS pid=10892] Upload LEAVE: hr=0x00000000 Bytes=68
```

E no `bc250_umd_log_10892.txt`:
```text
15:44:06.871 [pid=10892 bc250-umd-node1-tripod.exe] CommandBufferInit: GpuVA=0x11000 Capacity=16384 dwords
15:44:06.872 [pid=10892 bc250-umd-node1-tripod.exe] NODE1_SUBMISSION_READY: Context=0000000000486420 GpuVA=0x11000 -- NAO VALIDADO EM HARDWARE
15:44:06.893 [pid=10892 bc250-umd-node1-tripod.exe] NODE1_PROOF_BUILD: Dst=0x21000 Values=5 Fence=0x10040/BC250F01 Dwords=17 -- OFFLINE
15:44:06.894 [pid=10892 bc250-umd-node1-tripod.exe] CommandBufferFlush: Node=1 bytes=68 status=OK
```

**Conclusão da Faixa B7.1 (P0):**
A cadeia de infraestrutura WDDM na UMD (alocação, mapeamento GPUVA, residência assíncrona, sincronização de paginação e chamada do runtime `pfnSubmitCommandCb`) operou sem erros.

---

## 2. Primeiro Submit Node 1 no Silício Recebido pelo KMD

Pela primeiríssima vez na história do projeto, a DDI `DxgkDdiSubmitCommandVirtual` recebeu uma submissão direcionada ao **Node 1 (Compute MEC)** (`bc250_display_log-full.txt`):

```text
11875: [McdmSubmitCommandVirtual] Node=1, FenceId=1, DmaSize=68
11877: [SubmitVirtual Node1] GATE ABERTO: GpuvaMappingsOperational=1; publicando IB de 68 bytes em VA=0x11000 no anel persistente MEC (FenceId=1)
```

O KMD validou a cobertura sem gaps da faixa GPUVA (`0x11000`, 68 bytes), adquiriu o snapshot imutável, montou o pacote de 12 dwords (`PACKET3_INDIRECT_BUFFER` de 4 dwords + `RELEASE_MEM` de 8 dwords) no anel persistente MEC e publicou o WPTR no silício:

No arquivo `b7-umd-node1-tripod.json`:
- `P1LastWptrMmioReadback` (antes): **204**
- `P1LastWptrMmioReadback` (depois): **216**
- Delta exato: **+12 dwords**. O ponteiro de escrita avançou fisicamente no hardware.

Além disso, os probes confirmam que a engine e o doorbell do MEC funcionam:
- `P1SubmitAccepted`: **17**
- `P1RetiredTotal`: **17**
- Todas as 17 submissões de 12 dwords executadas durante a inicialização (StartDevice) aposentaram com `RPTR == WPTR` em 1 poll. A hipótese de falha de doorbell está refutada pelos fatos.

---

## 3. O Tripé do Node 1 NÃO Fechou: FAIL-CLOSED no Silício

Apesar da publicação no anel MEC:

1. **`RPTR` não avançou:**
   - `P1LastRptrMmio` (antes): **204**
   - `P1LastRptrMmio` (depois): **204**
   - A GPU não consumiu os 12 dwords do anel MEC.

2. **Cerca monitorada retornou `UINT64_MAX`:**
   ```text
   [WDDM_WINSYS pid=10892] WaitForFenceFromCpu ENTER: TargetFence=3156545281 CurrentFence=1138425854 SyncObj=0x40000200
   [WDDM_WINSYS pid=10892] WaitForFenceFromCpu LEAVE: hr=0x00000000 TargetFence=3156545281 CurrentFence=18446744073709551615
   ```

3. **Canários em VRAM permaneceram em zero:**
   ```text
   NODE1_TRIPOD_RESULT: hr=0x80004005 Dst=0x21000 FenceGpuVa=0x10040 FenceObs=0xFFFFFFFFFFFFFFFF Poison=1 Submitted=1 Fence=0 Data=0 Cleanup=1 Values=00000000,00000000,00000000,00000000,00000000
   ```

4. **Veredito formal:**
   O harness encerrou com `exit=1`:
   `NODE1_TRIPOD: FAIL-CLOSED poison=1 submit=1 fence=0 data=0 cleanup=1 -- NAO VALIDADO EM HARDWARE`.

---

## 4. Fechamento da Questão TDR / `DEVICE_REMOVED`

Conferido por busca exata no log integral `bc250_display_log-full.txt` (12.039 linhas):
- `ResetFromTimeout`: **0 chamadas**
- `RestartFromTimeout`: **0 chamadas**
- `QueryEngineStatus`: **0 chamadas**

O dxgkrnl **não** acionou as DDIs de TDR. Adicionalmente, `WaitForFenceFromCpu` retornou `hr=0x00000000` (`S_OK`). A alegação de que `UINT64_MAX` representava um `DEVICE_REMOVED` por TDR era uma hipótese não suportada e fica formalmente descartada.

---

## 5. A Disputa do VMID 8 entre Node 0 (SDMA) e Node 1 (MEC)

A mineração do log `bc250_display_log-full.txt` (linhas 11867 a 11893) revela a colisão direta no silício:

```text
11867: [SetRootPageTable] ROOT_MC: VramBase=0x450000000 + LocalSegBase=0xA8C000 + Offset=0xF524000 -> PT=0x45FFB0000
11872: [SetRootPageTable] [Vmid8Install] readback OK: PT_BASE=45ffb0000 CNTL=0x00555403 (PhysReady=1, Flush=1)
11875: [McdmSubmitCommandVirtual] Node=1, FenceId=1, DmaSize=68
11877: [SubmitVirtual Node1] GATE ABERTO: GpuvaMappingsOperational=1; publicando IB de 68 bytes em VA=0x11000 no anel persistente MEC (FenceId=1)
11884: [Present #3] Blt 11059200 bytes (src 0xAB0000 -> scanout 0x0): SDMA=YES
11888: [SubmitVirtual Node0 SDMA] FenceId=8, DmaSize=32, DmaVA=0x22F0C0
11892: [SubmitVirtual Node0 SDMA] VMID8 CONTEXT SWITCH: Ctx FFFFE68999D486B0 -> FFFFE68999D48500 (Root=0x450A8C000, PT_BASE=0x450A8C000)
```

### Análise da Causa Raiz
1. O processo do harness (PID 10892) instalou sua tabela de páginas no VMID 8: `PT_BASE = 0x45FFB0000`. Neste espaço de endereçamento, o command buffer em `VA=0x11000` está devidamente mapeado.
2. O Node 1 emitiu o IB virtual apontando para `VA=0x11000` sob `VMID=8`.
3. Concorrentemente, a thread de display (`Present #3`) chamou o SDMA (`Node 0`), que identificou troca de contexto e reprogramou o registrador físico do VMID 8 para `PT_BASE = 0x450A8C000` (contexto do DWM / paginação).
4. No instante em que o MEC foi buscar as instruções do IB em `VA=0x11000` sob `VMID=8`, o registrador físico `PT_BASE` do GFXHUB já continha a tabela do DWM, onde `VA=0x11000` não existe.

---

## 6. Próximos Passos Recomendados

1. **Instrumentação Offline da Disputa (sem alteração de fluxo):**
   Registrar no instante do submit do Node 1 a raiz esperada (`targetPtPhys`), a raiz registrada em software (`Vmid8InstalledRootPhysical`) e a raiz física lida diretamente do hardware (`GCVM_VMID8_PT_BASE_LO/HI`).
2. **Definição de Estratégia de Isolamento de VMID:**
   - Opção A: Alocar um VMID exclusivo para o Compute (ex: VMID 9 para Node 1, preservando VMID 8 para SDMA/Present).
   - Opção B: Ensaio limpo isolado (executar o harness Node 1 sem concorrência de Present).
   - Decisão fundamentada após telemetria de medição.
