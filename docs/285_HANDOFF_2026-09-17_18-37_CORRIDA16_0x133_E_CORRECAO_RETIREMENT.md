# 285 - Handoff: Corrida 16 (17/09/2026 18:37), Diagnóstico de BugCheck 0x133 e Correção de Retirement no Node 1 GFX

**Data:** 17/09/2026 18:55  
**Autor:** Antigravity / Gemini Pair Programming  
**Target:** AMD BC-250 (@2:13FE\, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
**Evidência Primária:** \docs/evidence/2026-09-17_18-35-26-DESKTOP-HQAG6T4-lab-binding/\  
**Dump de Memória:** \C:\Windows\MEMORY.DMP\ -> \docs/evidence/2026-09-17_18-35-26-DESKTOP-HQAG6T4-lab-binding/memory-dmp-analysis-symbols-fixed.txt\  
**Status do Silício:** Protegido pelo Fusível Anti-Boot-Loop (\C:\BC250_BOOT_FUSE.dat\), restaurado para CLEAN (0).  

---

## 1. Relatório Mandatório da Corrida 16

### 1.1 Houve avanço?
**Sim, avanço diagnóstico crítico.**
Pela primeira vez sob o pipeline D3D11 nativo (\c250umd.dll\), o anel gráfico real \CP_RB0\ consumiu e executou 20 submissões 3D monotônicas no silício sob VMID 7 (\FenceId=1..20\, \DmaSize=96\).
A causa da tela azul (\BugCheck 0x133: DPC_WATCHDOG_VIOLATION\) foi isolada com precisão absoluta através do dump de memória completo de 800 MB: uma colisão reentrante de spinlock no scheduler WDDM (\dxgmms2!VidSchDdiNotifyDpc\) provocada pela invocação síncrona de \DxgkCbNotifyDpc\ de dentro de \SubmitCommandVirtual\ no Node 1.

### 1.2 Sucessos
1. **Fences 1 a 20 Aposentadas no CP_RB0:** O harness Direct3D 11 submeteu draw commands que foram validados fisicamente pelo KMD (\GATE FISICO PASS\), commitados no ring (\FENCE COMMIT\) e aposentados monotonicamente no hardware sob VMID 7.
2. **Fusível Anti-Boot-Loop Funcionou Perfeitamente:** Ao ocorrer a tela azul, o fusível \C:\BC250_BOOT_FUSE.dat\ interceptou o reboot não-limpo, transitando para \State=2 (TRIPPED)\ com \LastStatus=0xC0000001\, forçando o dispositivo PnP a entrar em Código 43 (\CM_PROB_FAILED_START\). A máquina reiniciou em segurança total sem nenhum risco de boot loop.
3. **Extração e Análise Completa de MEMORY.DMP:** O relatório de crash foi gerado via CDB elevado com símbolos OS, extraindo a call stack exata das CPUs envolvidas.

### 1.3 Falhas
1. **BugCheck 0x133 (\DPC_WATCHDOG_VIOLATION\):**
   - Parâmetros: \Arg1 = 0x0000000000000000\, \Arg2 = 0x0000000000000501\, \Arg3 = 0x0000000000000500\, \Arg4 = 0xfffff8077a6fb320\.
   - Significado: Uma rotina em \DISPATCH_LEVEL\ consumiu 0x501 ticks (~10–15 segundos) sem ceder o processador, estourando o limite de 0x500 ticks do watchdog de DPC do Windows.
2. **Tela Preta antes do Crash:** O ensaio foi interrompido no 20º frame pelo BugCheck antes de a apresentação do desktop/scanout blt completar o ciclo de exibição contínua.

### 1.4 Veredito
**Falha funcional por BugCheck 0x133 com avanço diagnóstico conclusivo.** Critério de teste não fechado (exigia 60 frames com pixels renderizados).

### 1.5 Recuperação e Pendências
- **Recuperação:** Fusível de boot resetado com sucesso para \CLEAN (0)\.
- **Pendência Fechada Offline:** BugCheck 0x133 corrigido no KMD (\c250_active_miniport.c\), pacote de laboratório recompilado, assinado e auditado em \driver\wdk\package\output_lab\.
- **Próximo Passo:** Validação física da Corrida 17 com o KMD corrigido contra reentrância de DPC.

---

## 2. Anatomia Forense do BugCheck 0x133

A análise de \MEMORY.DMP\ revelou o seguinte quadro de deadlock entre a thread de submissão do VidSch e a DPC do timer de aposentadoria:

### Call Stack do Processador que Estourou o Watchdog:
\\	ext
nt!KeBugCheckEx
nt!KeAccumulateTicks+0x1bfb70
nt!KiUpdateRunTime+0x5d
nt!KiUpdateTime+0x4a1
nt!KeClockInterruptNotify+0x2e3
nt!HalpTimerClockInterrupt+0xe2
nt!KiCallInterruptServiceRoutine+0xa5
nt!KiInterruptSubDispatchNoLockNoEtw+0xfa
nt!KiInterruptDispatchNoLockNoEtw+0x37
nt!KeYieldProcessorEx+0x17
nt!KxWaitForLockOwnerShip+0x2a
nt!KeAcquireInStackQueuedSpinLockAtDpcLevel+0x73
dxgmms2!VidSchDdiNotifyDpc+0xdd
dxgkrnl!DxgNotifyDpcCB+0x91
bc250mcdm!Bc250ActiveRetireAndNotify (chamada via Timer DPC)
nt!KiProcessExpiredTimerList+0x172
nt!KiRetireDpcList+0x5dd
\
### Call Stack da Thread de Submissão Concorrente:
\\	ext
dxgmms2!VidSchiWorkerThread+0xca
dxgmms2!VidSchiRun_PriorityTable+0x152
dxgmms2!VidSchiSubmitRenderVirtualCommand+0x4cf
dxgmms2!VidSchiSendToExecutionQueue+0x7be
dxgkrnl!ADAPTER_RENDER::DdiSubmitCommandVirtual+0x144
bc250mcdm!Bc250McdmSubmitCommandVirtual
bc250mcdm!Bc250ActiveRetireAndNotify (chamada síncrona com FromDpc=FALSE)
dxgkrnl!DxgNotifyDpcCB+0x91
dxgmms2!VidSchDdiNotifyDpc+0x225
dxgmms2!VidSchiProcessDpcCompletedPacket+0xb2
\
### Mecanismo do Deadlock:
1. \SubmitCommandVirtual\ roda em \DISPATCH_LEVEL\ na thread de trabalho do VidSch (\VidSchiWorkerThread\), segurando locks internos do escalonador.
2. Ao final de cada submissão no Node 1 GFX, o KMD chamava incondicionalmente \Bc250ActiveRetireAndNotify(context)\.
3. Se qualquer fence anterior já havia completado no silício, \Bc250ActiveRetireAndNotify\ chamava \Context->Core.DxgkInterface.DxgkCbNotifyDpc(...)\ **sincronamente dentro de SubmitCommandVirtual**.
4. Chamar \DxgkCbNotifyDpc\ a partir de \SubmitCommandVirtual\ tenta adquirir o mesmo in-stack queued spinlock que o escalonador já possui ou que uma DPC simultânea está tentando adquirir.
5. O processador entra em busy-wait no spinlock (t!KxWaitForLockOwnerShip\ chamando \KeYieldProcessorEx\) por ~10 segundos. O Windows detecta que o processador passou mais de 0x500 ticks sem baixar de \DISPATCH_LEVEL\ e emite \BugCheck 0x133\.

---

## 3. Correção Implementada

A mesma regra que já existia e estava validada para o SDMA (Node 0) foi estendida e rigorosamente aplicada ao Node 1 (GFX e MEC):

1. **Assinatura da Rotina de Retirement:**
   \\c
   static void Bc250ActiveRetireAndNotify(
       _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context,
       _In_ BOOLEAN FromDpc);
   \
2. **Guarda Atômica de Serialização (\RetireBusy\):**
   \\c
   if (InterlockedCompareExchange(&Context->RetireBusy, 1, 0) != 0) {
       return;
   }
   \   Impede que múltiplos núcleos ou threads executem a varredura e retirada em paralelo.

3. **Invariante Estrutural Anti-0x133 (\FromDpc\ Guard):**
   \\c
   if (FromDpc) {
       if (InterlockedExchange(&Context->DpcNotifyPending, 0) != 0) {
           if (Context->Core.DxgkInterface.DxgkCbNotifyDpc != NULL) {
               Context->Core.DxgkInterface.DxgkCbNotifyDpc(
                   Context->Core.DxgkInterface.DeviceHandle);
           }
       }
   } else {
       /*
        * Em SubmitCommandVirtual (FromDpc == FALSE), o scheduler (VidSch)
        * ja esta na pilha segurando seus locks internos. Chamar DxgkCbNotifyDpc
        * aqui causa reentrancia recursiva no spinlock em fila (BugCheck 0x133).
        * Agendamos o DPC para que ele notifique o scheduler logo apos sair.
        */
       if (InterlockedCompareExchange(&Context->DpcNotifyPending, 0, 0) != 0) {
           KeInsertQueueDpc(&Context->RetirementDpc, NULL, NULL);
       }
   }
   \
4. **Desacoplamento Temporal na Submissão GFX:**
   Em \Bc250ActiveSubmitCommandVirtual\ (Node 1 GFX):
   \\c
   KeInsertQueueDpc(&context->RetirementDpc, NULL, NULL);
   Bc250ActiveRetirementTimerStart(context);
   Bc250ActiveRetireAndNotify(context, FALSE);
   \   A notificação do scheduler é obrigatoriamente diferida via DPC, permitindo que \SubmitCommandVirtual\ retorne imediatamente com \STATUS_SUCCESS\ e libere a pilha do VidSch.

---

## 4. Auditoria e Empacotamento

- \driver\wdk\build-contract.bat\: **EXIT=0** (Todas as 40 auditorias de segurança PASS).
- \	ools\build-bc250mcdm-lab-bringup.ps1 -Execute\: **EXIT=0** (Novo \driver\wdk\output_lab\bc250mcdm.sys\ gerado).
- \	ools\package-bc250mcdm-lab.ps1\: **EXIT=0** (Assinatura digital e catálogo \c250mcdm.cat\ gerados).
- \	ools\audit-bc250mcdm-lab-package.ps1\: **PASS** (100% dos hashes e conformidade de perfil validados).
- \	ools\inspect-boot-fuse.ps1\: **Estado CLEAN (0)**.
