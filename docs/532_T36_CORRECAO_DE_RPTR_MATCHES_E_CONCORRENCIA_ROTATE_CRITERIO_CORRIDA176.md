# 532 — T3.6: Correção de Bc250SdmaRptrMatches e Portão de Conclusão Síncrona do SDMA; Proteção contra Concorrência no Rotate do VidMm; Critério da Corrida 176

> 29/09/2026, offline. Candidato **3.321.1.0** (inclui T3.6, T3.5, T3.4, T3.3, HDP.1, T3.1, EVT.1, X1, X2a, X3 e X4). **Nada desta faixa foi validado em hardware.**

---

## 1. Por quê

Na **Corrida 175** (candidato T3.5, DriverVer `3.320.1.0`), o desktop DWM permaneceu ativo e acelerado por 53,0 segundos (268 frames apresentados, 5 `BYTE_PROOF PASS`, 1.403 IBs de paginação/SDMA executados no silício sem qualquer fault de hardware). No entanto, ao abrir o menu Iniciar do Windows (`explorer.exe`), ocorreu o BugCheck `VIDEO_MEMORY_MANAGEMENT_INTERNAL (10e)`:

- **Código:** `0x0000010E` (`Arg1=0x34`, `Arg2=0x0B`, `Arg3=0x01`, `Arg4=0x00`).
- **Processo:** `explorer.exe`.
- **Call stack:**
  ```text
  nt!KeBugCheckEx
  watchdog!WdLogEvent5_WdCriticalError+0xe0
  dxgmms2!VIDMM_RECYCLE_MULTIRANGE::Rotate+0x77
  dxgmms2!VIDMM_RECYCLE_HEAP_MGR::Rotate+0xb1
  dxgmms2!VIDMM_GLOBAL::Rotate+0x7b
  dxgmms2!VIDMM_GLOBAL::SetupAllocationForCPUAccess+0x131
  dxgmms2!VIDMM_GLOBAL::BeginCPUAccess+0x2ea58
  dxgmms2!VidMmBeginCPUAccess+0x28
  dxgkrnl!DXGDEVICE::Lock+0x258
  dxgkrnl!DxgkLock+0x345
  ```

### Diagnóstico e Cadeia de Causalidade (P0 / Engenharia Reversa do Dump)

1. **Bug no portão de conclusão síncrona pós-T3.5:**
   - Na faixa T3.5, o `RingLock` foi corretamente liberado antes do laço de polling síncrono de 50 ms.
   - Isso permitiu que a thread de paginação do VidMm submetesse pacotes concorrentemente no SDMA.
   - Porém, as funções síncronas (`Bc250SdmaPresentBltCore`, etc.) testavam conclusão por:
     `rptrOk = Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr);`
     que realizava comparação estrita de módulo `(rptrReg % 4096 == targetWptr % 4096)`.
   - Quando submissões posteriores do VidMm empurravam `WPTR` e `RPTR` além de `targetWptr` (como medido no log: `TargetWPTR=11580`, `WPTR=11844`, `RPTR=11844`), a igualdade estrita falhava para sempre.
   - Adicionalmente, `Bc250SdmaSyncEntryConsumed` passava `TargetBytes` como `TailBytes`, forçando o contrato a exigir estritamente `RPTR == TargetBytes`.
   - Como resultado, mesmo com a fence de hardware batida na VRAM (`FenceObs == FenceExp == 0x000010B2`), a rotina disparava falso timeout de 50 ms (`Blt Timeout #1`) e retornava `FALSE` para o Present, que devolvia `STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE`.

2. **Gatilho do BugCheck 0x10E / Subtipo 0x34:**
   - Concomitantemente, o VidMm agendou uma transferência de 11 MB (`[DX91_PAGING_MOVE] op=TRANSFER hAlloc=000010EE00000333 Size=11075584 Dir=1`, fence 1404).
   - Essa transferência colocou o multirange de reciclagem da abertura (`VIDMM_RECYCLE_MULTIRANGE`) no estado 4 (`IN_TRANSITION` / em rotação).
   - Devido ao falso timeout e à exceção de dispositivo gerada pelo blt, a rotação e aposentadoria da fence 1404 não foi concluída a tempo.
   - A thread do `explorer.exe` chamou `DxgkLock` em outra alocação que tentou reutilizar o mesmo multirange de reciclagem via `VIDMM_GLOBAL::SetupAllocationForCPUAccess` -> `Rotate`.
   - `VIDMM_RECYCLE_MULTIRANGE::Rotate` verificou se o multirange já estava em estado 4 (`cmp dword ptr [rcx+0D8h], 4` -> `jne ...`). Estando em 4, a transição é proibida e o VidMm derrubou a máquina com `0x10E / 0x34` ("heap allocation received a state transition event incompatible with its current state").

---

## 2. A mudança (Faixa T3.6)

1. **Correção de `Bc250SdmaSyncEntryConsumed`:**
   - Agora passa o cursor de cauda real do anel (`uint32_t tailBytes = (uint32_t)(g_Bc250SdmaPresent.CurrentCursor * sizeof(uint32_t));`) para `Bc250TripodRingEntryConsumed`, em vez de passar `TargetBytes`.
   - Isso permite que o portão reconheça imediatamente quando o hardware já consumiu a entrada e avançou para submissões subsequentes.

2. **Correção de `Bc250SdmaRptrMatches` e Portão Síncrono:**
   - Nas 5 funções síncronas (`Bc250SdmaPresentBltCore`, `Bc250SdmaPresentBltRectCore`, `Bc250SdmaPresentBltScanoutSub`, `Bc250SdmaPresentSyncNop`, `Bc250SdmaPresentIndirect`):
     - Mantém a chamada exigida pela auditoria (`Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr)`).
     - Se a igualdade estrita não bater (porque o RPTR avançou sob concorrência), valida se `Bc250TripodRingEntryConsumed` confirma que a entrada foi consumida ou se `fenceObserved == nextFence` sobre o poison na VRAM.
     - Havendo confirmação da fence ou do consumo no anel, `rptrOk` passa para `TRUE` e a submissão conclui com sucesso imediato (~10 µs), sem qualquer falso timeout de 50 ms.

3. **Prevenção da Colisão de Estado no `Rotate` do VidMm:**
   - Com o fim dos falsos timeouts no Present, o fluxo de submissões e aposentadorias de paginação (`TRANSFER` e `FILL`) permanece desobstruído.
   - As fences de paginação (como 1404) aposentam imediatamente, permitindo que o VidMm transicione os multiranges de volta ao estado 3 (`IDLE`) antes que chamadas a `DxgkLock` / `SetupAllocationForCPUAccess` solicitem nova rotação.

---

## 3. Provas offline

- Suíte de contratos completa (`driver/wdk/build-contract.bat`): **EXIT 0** (105 DDIs, 0 falhas).
- Compilação do pacote de produção (`build-bc250mcdm-package.ps1`): **EXIT 0** (DriverVer `3.321.0.0`).
- Compilação de laboratório (`build-bc250mcdm-lab-bringup.ps1`): **EXIT 0**.
- Empacotamento de laboratório (`package-bc250mcdm-lab.ps1`): **EXIT 0** (DriverVer `3.321.1.0`).
- Auditoria de segurança (`audit-bc250mcdm-safety.py`): **PASS**.
- Auditoria de empacotamento (`audit-bc250mcdm-lab-package.ps1`): **PASS**.
- Script de preflight (`prepare-corrida176-t36.ps1`): **EXIT 0**.

---

## 4. Candidato

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `E97C6DFDF47E59BDE02837DE10A6CE470A91BFAF84565A6DF6674479D65CC253` |
| `bc250umd.dll` (idêntica à 3.310) | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `4A0F440F8FEA9BA559009208F3F70371D3A00C4A6FC81C9133FB12BFBF07B1A5` |
| `bc250mcdm.cat` | `F16E8FDB2B563ED419623CA0534CF4F5B735AC873A3AFA36FB8DEB79809CF073` |

- DriverVer: `09/29/2026,3.321.1.0` (fonte `3.321.0.0`).
- Runner: `tools/EXECUTAR_CORRIDA176_T36_600S.cmd`.

---

## 5. Critério da Corrida 176 (escrito antes; Lei 10)

**Roteiro:** O mesmo da Corrida 175 (atividade mínima, sem harness). Assim que o desktop estabilizar (~20 s), clicar para abrir e fechar o menu Iniciar repetidas vezes (para exercitar alocações dinâmicas, `DxgkLock` pelo `explorer.exe`, e submissões paralelas de `TRANSFER`/`FILL` com os blts do Present).

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | **600 s sem trava e sem BSOD** com menu Iniciar operando e abrindo/fechando repetidamente, Presents contínuos e sem falso timeout | **Sucesso completo da estabilização de concorrência:** a correção de `rptrOk` eliminou os timeouts síncronos e impediu o BugCheck `0x10E / 0x34`. Avança imediatamente para os testes com aplicações 3D / jogos. |
| **B** | Repetição do BugCheck `0x10E / 0x34` no `Rotate` | A sobreposição de `TRANSFER` e `DxgkLock` decorre de falta de sincronização explícita entre a fila de paginação e o bloqueio de CPU. Inserir barreira de residência no `BuildPagingBuffer`. |
| **C** | Trava em ponto novo de submissão identificado pelas novas migalhas (0x15, 0x1A, 0x1B) | Identificar se a contenção remanescente decorre de fence de hardware ou ciclo de DMA buffer. |
| **D** | Fora do mapa (BugCheck diferente com dump de kernel) | Analisar arquivo de dump `.dmp` gerado para obter callstack exata. |
