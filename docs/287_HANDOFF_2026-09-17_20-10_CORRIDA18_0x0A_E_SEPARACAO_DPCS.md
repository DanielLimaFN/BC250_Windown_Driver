# 287 — Handoff 17/09/2026 20:10 — Corrida 18: BugCheck 0x0A (IRQL_NOT_LESS_OR_EQUAL) e Desacoplamento Estrito de DPCs

> **Data:** 17/09/2026 20:10 | **Candidato:** Pacote Lab 3.181.1.0 (`bc250mcdm.sys`)  
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Evidência primária:** `docs/evidence/2026-09-17_20-09-16-DESKTOP-HQAG6T4-lab-binding/` e `C:\Windows\MEMORY.DMP`

---

## 1. Resumo Executivo da Corrida 18

Na Corrida 18, o ensaio de conformidade Direct3D 11 (`tools\EXECUTAR_D3D11_NATIVO.cmd -Force`) foi iniciado às 20:09:16.
- A máquina operou em Código 0 durante a submissão inicial de pipelines 3D no Node 1 GFX.
- Às 20:09:23 (7 segundos após o início), ocorreu um BugCheck `0x0000000A: IRQL_NOT_LESS_OR_EQUAL`.
- O fusível `C:\BC250_BOOT_FUSE.dat` desarmou para `State=2 (TRIPPED)` como projetado, impedindo qualquer boot-loop de driver. A máquina reiniciou em segurança no BasicDisplay.

---

## 2. Análise Forense do Crash Dump (`MEMORY.DMP`)

### 2.1. Parâmetros do BugCheck
```text
BugCheck A, {20, 2, 1, fffff8073a28b55e}
Arg1: 0000000000000020 (Endereço de memória referenciado)
Arg2: 0000000000000002 (IRQL = DISPATCH_LEVEL)
Arg3: 0000000000000001 (Operação de ESCRITA)
Arg4: fffff8073a28b55e (Instrução que gerou o acesso: nt!KiExecuteAllDpcs+0x2ee)
```

### 2.2. Call Stack Completa
```text
 # Child-SP          RetAddr               : Call Site
00 fffff904`df6768d8 fffff807`3a40fd29     : nt!KeBugCheckEx
01 fffff904`df6768e0 fffff807`3a40b8e3     : nt!KiBugCheckDispatch+0x69
02 fffff904`df676a20 fffff807`3a28b55e     : nt!KiPageFault+0x463
03 fffff904`df676bb0 fffff807`3a28a864     : nt!KiExecuteAllDpcs+0x2ee
04 fffff904`df676d20 fffff807`3a404195     : nt!KiRetireDpcList+0x1f4
05 fffff904`df676fb0 fffff807`3a403f80     : nt!KxRetireDpcList+0x5
--- INTERRUPT (KiDpcInterrupt) ---
06 nt!KiDispatchInterruptContinue
07 nt!KiDpcInterrupt+0x2e8
08 nt!KiInsertQueueDpc+0x289
09 nt!KeInsertQueueDpc+0x11
0a bc250mcdm+0x41373 (Bc250ActiveRetireAndNotify)
0b bc250mcdm+0x44779 (Bc250DxgkDdiSubmitCommandVirtualActive - Node 1 GFX)
0c bc250mcdm+0x454d4 (Bc250DxgkDdiSubmitCommandVirtualActive entry)
```

### 2.3. Desmontagem da Instrução Culpada
```asm
nt!KiExecuteAllDpcs+0x2ee:
fffff807`3a28b558 4983ff01        cmp     r15, 1
fffff807`3a28b55c 7404            je      nt!KiExecuteAllDpcs+0x2f2
fffff807`3a28b55e 49894720        mov     qword ptr [r15+20h], rax   <=== CRASH!
```

- `r15` é derivado de `r14+0x38` no início do processamento do objeto DPC:
```asm
fffff807`3a28b3af 4d8b7e38        mov     r15, qword ptr [r14+38h] ; r15 = Dpc->DpcData
```
- O offset `+0x38` na estrutura `_KDPC` do Windows x64 armazena o ponteiro `DpcData`.
- No dump da estrutura `ffffb384901a2848` (`&context->RetirementDpc`):
```text
ffffb384`901a2848  00000000`00000113 ffffb384`901a2850
ffffb384`901a2858  ffffb384`901a2850 fffff807`45e71fd0 (Bc250ActiveRetirementDpcRoutine)
ffffb384`901a2868  ffffb384`901a0000 00000000`00000000
ffffb384`901a2878  00000000`00000000 00000000`00000000 (DpcData == 0)
```
- O campo `DpcData` estava `0x0000000000000000`, e `DpcListEntry` apontava para si mesmo (`Flink = Blink = ffffb384901a2850`).
- Ao desreferenciar `[r15 + 0x20]` (tentativa de atualizar telemetria de profiling de DPC em `0x00000020`), ocorreu page fault em `DISPATCH_LEVEL`, deflagrando BugCheck 0x0A.

---

## 3. Causa Raiz Mecânica

1. **Compartilhamento Ilegal de `KDPC` entre Timer Periódico e Enfileiramento Manual:**
   - Em `bc250_active_miniport.c`, `context->RetirementDpc` estava sendo passado para `KeSetTimerEx(&context->RetirementTimer, due, 1, &context->RetirementDpc)` (um timer de 1 ms contínuo).
   - Ao mesmo tempo, `SubmitCommandVirtual` e `Bc250ActiveRetireAndNotify` chamavam `KeInsertQueueDpc(&context->RetirementDpc, NULL, NULL)` no mesmo objeto `KDPC`.
2. **Colisão de Estado na Fila de DPC do Kernel:**
   - No kernel do Windows, o despachante de timers gerencia a lista e o estado `DpcData` do DPC do timer.
   - Quando `KeInsertQueueDpc` é chamado concorrentemente em uma DPC de timer que já está expirando ou sendo drenada por `KiExecuteAllDpcs`, os ponteiros de `DpcListEntry` são corrompidos e `DpcData` é limpo para NULL prematuramente, gerando o crash em `KiExecuteAllDpcs`.

---

## 4. Correção Implementada

1. **Desacoplamento Total de DPCs em `BC250_DXGK_ACTIVE_CONTEXT`:**
   - **`RetirementTimerDpc`**: DPC de propriedade exclusiva do timer periódico de 1 ms (`KeSetTimerEx`). Nunca é submetida a `KeInsertQueueDpc`.
   - **`RetirementNotifyDpc`**: DPC dedicada exclusivamente ao desacoplamento assíncrono de notificações (`KeInsertQueueDpc`). Nunca é passada para timers.
   - **`SdmaRetirementTimerDpc`**: DPC dedicada ao timer do SDMA.
   - **`SdmaRetirementDpc`**: DPC dedicada ao enfileiramento assíncrono manual do SDMA.
2. **Eliminação de Enfileiramentos Redundantes / Incondicionais:**
   - Removido `KeInsertQueueDpc` incondicional logo após a submissão de GFX e Compute. Apenas `Bc250ActiveRetireAndNotify(context, FALSE)` enfileira `RetirementNotifyDpc` se houver fences de fato aposentadas.
3. **Auditoria e Integridade:**
   - Todos os 40 gates de `build-contract.bat` aprovados.
   - Pacote Lab 3.181.1.0 reconstruído, assinado e auditado (`audit-bc250mcdm-lab-package.ps1: PASS`).
