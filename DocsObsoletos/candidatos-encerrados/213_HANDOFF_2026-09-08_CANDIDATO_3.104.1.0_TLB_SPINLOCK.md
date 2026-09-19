# docs/213_HANDOFF_2026-09-08_CANDIDATO_3.104.1.0_TLB_SPINLOCK.md

# HANDOFF: Passagem de Bastão — Candidato 3.104.0.0 (Lab 3.104.1.0)
> **Data:** 2026-09-08 23:10 | **Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Fonte Única da Verdade:** [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md) | **Roadmap:** [`docs/164_ROADMAP_WDDM_JOGOS.md`](164_ROADMAP_WDDM_JOGOS.md)  
> **Norma de Governança:** [`AGENTS.md`](../AGENTS.md) e [`GEMINI.md`](../GEMINI.md)

---

## 📊 Status Oficial do Driver BC-250

```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pre-WDDM (Lab)        ██████████████████▋░  93%   Estavel e Isolado
SDMA (DMA Engine & Robustez)     ████████████████████  98%   B1 FECHADO: IB WDDM real sob VMID8 com TRIPE completo (5 canarios); wrap do anel causou 0x119
GCVM & Traducao de Memoria       ███████████████████░  95%   MMU global 26 regs, GPUVA de IB resolvida no silicio, Generation=54; Node1 nao provado
RLC, CP & PM4 Direct Load        ███████████████████░  95%   Fence em VRAM provada
Telemetria & Observabilidade     ██████████████████░░  90%   B2 FECHADO: logs da UMD coletados no lab; dwm.exe E_NOTIMPL provado no silicio (P0)
Compute & Async Queues (MEC/MQD) ██████████████████░░  90%   P1 17/17 e P2 16/16 provados; 1º submit Node1 no silicio (WPTR 204->216); tripe Node1 falhou (disputa VMID8)
Miniport WDDM Display+Render     ███████████████▋░░░░  78%   08/09: lab tambem comita -- CommitVidPn + SetVidPnSourceAddress + 60 GetStdAlloc + 367 VSYNC; duas paredes viraram UMA (D3D10/11); tela preta
=============================================================================================
Veredito da Corrida 22:20 (3.103.1.0):
1. BugCheck 0xD1 (AV_CODE_AV_UNLOADED_IP) foi 100% ELIMINADO NO SILÍCIO (descarregamento limpo).
2. O novo crash foi BugCheck 0x119 Subtipo 2 (dxgmms2!VidSchiSendToExecutionQueue + STATUS_IO_DEVICE_ERROR).
3. Causa raiz física comprovada pelo dump de memória: colisão concorrente sem spinlock nos registradores MMIO de TLB do GFXHUB entre ActiveFlushTlb (PASSIVE_LEVEL) e SubmitCommandVirtual (DISPATCH_LEVEL).
4. Candidato 3.104.0.0 (Lab 3.104.1.0) implementado, serializado com KSPIN_LOCK TlbLock, isolado em Bc250ActiveFlushVmid8Only, auditado e empacotado.
=============================================================================================
```

---

## 🎯 1. Onde Queremos Chegar

### A Meta Final Suprema
**Jogos no Windows 10 22H2 rodando na BC-250** — 24 CUs, 16 GB GDDR6, monitor conectado na porta DisplayPort física da própria placa, operando em **D3D12/Vulkan**, **SEM WARP e SEM fallback de software**.

### A Arquitetura Vigente
- **Display v1**: Herança direta do **UEFI GOP**. Modo único fixado, scanout em VRAM real, `DxgkDdiPresent` no modelo **blt** via SDMA, VSync lido do frame counter do bloco OTG (BAR5).
- **Proibição Estrita de DCN**: É terminantemente proibido tentar programar ou escrever em registradores de DCN no caminho crítico atual. Apenas leituras são toleradas.
- **Node 0 (SDMA)**: Engine de paginação e cópia do sistema, submetendo buffers WDDM reais em modo `INDIRECT` sob VMID 8.
- **Node 1 (Compute MEC)**: Anéis de compute direto assíncrono para dispatch de shaders e execução gráfica/computacional sob VMID 9.

---

## 🛠️ 2. O Que Foi Feito Nesta Sessão

### A. Diagnóstico Físico do Crash Dump de 1 GB (`MEMORY.DMP`)
A corrida de laboratório das 22:20 (Candidato `3.103.1.0`) sofreu uma tela azul após a tela preta.
Executamos o script de análise [`tools/analyze-bc250-memory-dump.ps1`](../tools/analyze-bc250-memory-dump.ps1) com CDB e símbolos públicos do kernel Microsoft, arquivando o relatório em [`docs/evidence/2026-09-08_22-20-04-DESKTOP-HQAG6T4-lab-binding/memory-dmp-analysis-symbols-fixed.txt`](../docs/evidence/2026-09-08_22-20-04-DESKTOP-HQAG6T4-lab-binding/memory-dmp-analysis-symbols-fixed.txt).

**Resultados Incontestáveis:**
1. **O BugCheck 0xD1 Foi Eliminado 100% no Silício**:
   A correção de teardown (cancelamento incondicional de timers, bloqueio de rearmamento em `StopDeviceReason` e barreira `KeFlushQueuedDpcs`) garantiu que o descarregamento do binário `bc250mcdm.sys` pelo PnP ocorresse sem nenhuma execução residual. Não houve violação de memória descarregada (`AV_CODE_AV_UNLOADED_IP`).
2. **Diagnóstico do Novo BugCheck: 0x119 Subtipo 2**:
   ```text
   BUGCHECK_CODE: 119
   BUGCHECK_P1: 0000000000000002 (The driver failed upon the submission of a command)
   BUGCHECK_P2: ffffffffc0000185 (STATUS_IO_DEVICE_ERROR)
   FAILURE_BUCKET_ID: 0x119_2_DRIVER_FAILED_SUBMIT_COMMAND_dxgmms2!VidSchiSendToExecutionQueue
   ```
3. **Mecanismo Físico Provado no Log Circular de Kernel (`!dbgprint`)**:
   ```text
   [BC250] [SubmitVirtual Node0 SDMA] FenceId=4, DmaSize=32, DmaVA=0x22F080
   [BC250] [SubmitVirtual Node0 SDMA] CTX_PROBE: Ctx=FFFFC60148669530, Proc=FFFFC60148669380, hasRoot=1, targetPtPhys=0x450A8C000, instCtx=FFFFC601486696E0, instRoot=0x45FFBC000
   [BC250] [ActiveFlushTlb] RootSeg=1, RootOff=0xF524000, StartVA=0x11000, EndVA=0x21000...
   [BC250] [FlushMappedGfxhubTlb] Transport State=1, Base=FFFFD70130586000, Len=524288
   [BC250] [FlushMappedGfxhubTlb] Transport State=1, Base=FFFFD70130586000, Len=524288
   [BC250] [FlushMappedGfxhubTlb] Transaction status=8, PhysReady=0, AckObserved=0, Polls=0
   [BC250] [SubmitVirtual Node0 SDMA] VMID8 CONTEXT SWITCH FALHOU: Ctx=FFFFC60148669530, Root=0x450A8C000, Status=6
   [BC250] [SubmitVirtual Node0 SDMA] REJECT VMID8 binding: SubmitCtx=FFFFC60148669530 InstalledCtx=FFFFC601486696E0 Status=6 PhysReady=0 Root=0x45FFBC000
   [BC250] [FlushMappedGfxhubTlb] Transaction status=0, PhysReady=1, AckObserved=1, Polls=1
   [BC250] [SubmitVirtual Node0 SDMA] IB NAO EXECUTADO #1: FenceId=4; completion NAO notificado
   ```
   **Causa Raiz:** O worker de paginação do `dxgmms2` executando `ActiveFlushTlb` (Op=12 a `PASSIVE_LEVEL`) e o scheduler executando `SubmitVirtual Node 0` (comutação de contexto VMID 8 a `DISPATCH_LEVEL`) entraram em `FlushMappedGfxhubTlb` **ao mesmo tempo**. Ambas as threads disputaram os registradores MMIO `0xA3A4`, `0xA3A8`, `0xA2D0` e `0xA318` e o campo compartilhado `active->TlbTransactionEvidence` **sem spinlock**. A Thread 2 leu registradores modificados pela Thread 1, falhou com `status=8` (`BC250_TLB_TRANSACTION_RESTORE_FAILED`), desinstalou o VMID 8, rejeitou a submissão e retornou `STATUS_IO_DEVICE_ERROR`, disparando o 0x119.

---

### B. Implementação do Candidato 3.104.0.0 (Lab 3.104.1.0)
1. **Adição de Spinlock Dedicado**:
   - Inserido `KSPIN_LOCK TlbLock;` na estrutura [`BC250_DXGK_ACTIVE_CONTEXT`](../driver/wdk/bc250_active_miniport.h).
   - Inicializado com `KeInitializeSpinLock(&context->TlbLock);` em `Bc250DxgkDdiAddDeviceActive`.
2. **Proteção Atômica de Invalidação de TLB**:
   - Em `Bc250ActiveFlushVmidTlb`:
     - Evidência e status alocados localmente na pilha (`localEvidence`, `localStatus`).
     - Transação `Bc250RunGfxhubTlbTransaction` envolvida estritamente por `KeAcquireSpinLock(&active->TlbLock, &oldIrql)` e `KeReleaseSpinLock(&active->TlbLock, oldIrql)`.
     - `Bc250DisplayLog` executado fora da trava para manter a retenção do spinlock restrita a poucos microssegundos de MMIO.
3. **Isolamento Estrito de Escopo no VMID 8**:
   - Criada a função `Bc250ActiveFlushVmid8Only(void *Context)` que invalida estritamente o VMID 8.
   - Conectada como callback `io.Flush` na comutação de contexto de `Bc250ActiveSubmitCommandVirtualBody` e em `Bc250ActiveSetRootPageTable`. Agora a instalação do VMID 8 no Node 0 não invalida desnecessariamente o VMID 9 do compute.
4. **Novo Portão de Auditoria Estática**:
   - Inserido `audit_tlb_invalidation_safety` em [`tools/audit-bc250mcdm-safety.py`](../tools/audit-bc250mcdm-safety.py), garantindo que a sincronização por spinlock e o isolamento de escopo nunca regridam.
5. **Auditorias e Pacote Concluídos**:
   - `driver\wdk\build-contract.bat`: **EXIT=0 (PASS)**.
   - `python tools/audit-bc250mcdm-safety.py`: **100% PASS**.
   - `python tools/audit-umd-node1-submission.py`: **100% PASS**.
   - Pacote de laboratório construído, catalogado e assinado em `driver/wdk/package/output_lab` (`DriverVer 3.104.1.0`).
   - `powershell tools/audit-bc250mcdm-lab-package.ps1`: **PASS**.

---

## 🛑 3. Onde Estamos Travados (Bloqueadores Ativos)

1. **Fusível de Boot Ativado (`C:\BC250_BOOT_FUSE.dat = TRIPPED`)**:
   - O fusível disparou no BugCheck 0x119 para proteger a máquina de loop de boot.
   - **Ação Obrigatória:** Deve ser resetado com `powershell -ExecutionPolicy Bypass -File tools\inspect-boot-fuse.ps1 -Reset` em terminal PowerShell elevado como Administrador antes de vincular o novo driver.
2. **Candidato 3.104.1.0 Pronto e Não Testado no Silício**:
   - O binário com a correção do spinlock está pronto em `output_lab`, mas ainda **NÃO FOI TESTADO EM HARDWARE**.
3. **Tela Preta no WDDM / Falha do DWM (E_NOTIMPL)**:
   - O adaptador atinge Código 0, cria swapchains, faz `CommitVidPn`, `SetVidPnSourceAddress` e apresenta `Presents SDMA=YES`.
   - Porém, a tela continua preta porque a UMD (`bc250umd.dll`) ainda retorna `E_NOTIMPL` em `OpenAdapter10` e `OpenAdapter10_2`.
   - O `dwm.exe` fail-fasta com código `0x887A0004` (DXGI_ERROR_UNSUPPORTED). Essa é a próxima grande barreira arquitetural (D3D10/11 backend na UMD).
4. **Submissão Node 1 (Compute MEC) Pendente de Validação Física**:
   - O primeiro submit UMD ao anel MEC já foi provado (WPTR 204->216).
   - O teste tripé do Node 1 falhou na corrida 15:43 por disputa do VMID 8. Agora, com o isolamento de escopo e o spinlock, o teste precisa ser executado no silício para verificar se a fence e o payload completam no anel de compute.

---

## ⚠️ 4. Coisas Para EVITAR (Erros Já Cometidos — NUNCA REPETIR)

### 🚫 Erro 1: Usar PushLock ou Primitivas de PASSIVE_LEVEL em IRQL DISPATCH_LEVEL
- **O que aconteceu:** Tentativas de usar `ExAcquirePushLockExclusive(&context->ObjectLock)` ou `KeEnterCriticalRegion()` em rotinas chamadas por `DxgkDdiSubmitCommandVirtual` causaram BugCheck `0xD1` instantâneo.
- **Regra:** `DxgkDdiSubmitCommandVirtual` roda em `DISPATCH_LEVEL` (`IRQL = 2`). Nunca use push locks, mutexes, semáforos ou chamadas de arquivo em disco. **Use estritamente `KSPIN_LOCK` (`KeAcquireSpinLock`/`KeReleaseSpinLock`).**

### 🚫 Erro 2: Rearmar Timers ou Retornar Erro no Caminho de Teardown (`RemoveDevice`)
- **O que aconteceu:** Quando `DxgkDdiStopDeviceActiveReason` recebia `REMOVE_DEVICE` com fences pendentes, retornava `STATUS_DEVICE_BUSY` e chamava `Bc250ActiveRetirementTimerStart()`. O PnP forçava o descarregamento do SYS e o timer disparava 1 ms depois em endereço descarregado, causando `BugCheck 0xD1` (`AV_CODE_AV_UNLOADED_IP`).
- **Regra:** `RemoveDevice` é irrevogável. Se o sistema está removendo o driver, **nunca** retorne ocupado, **nunca** rearme timers, pare o timer incondicionalmente com `Bc250ActiveRetirementTimerStop()`, chame `Bc250DisplayTeardown()` e drene a fila com `KeFlushQueuedDpcs()`.

### 🚫 Erro 3: Acessar Registradores MMIO Globais Sem Sincronização
- **O que aconteceu:** Duas threads acessaram simultaneamente os registradores de TLB do GFXHUB (`0xA3A4`, `0xA3A8`, `0xA2D0`), uma atropelando a outra, resultando em `BC250_TLB_TRANSACTION_RESTORE_FAILED` e `BugCheck 0x119 Subtipo 2`.
- **Regra:** Operações em registradores de controle do chip que envolvem escrita seguida de leitura/restauração precisam ser atômicas e protegidas por spinlock.

### 🚫 Erro 4: Falsos Sucessos (False Success / Lei 4 da Governança)
- **O que aconteceu:** No passado, stubs de TDR retornavam `STATUS_SUCCESS` sem tocar em hardware, e mensagens como "IB EXECUTADO" apareciam quando comandos estavam apenas sendo reimplementados no anel pelo Plano A sem a GPU ler o DMA buffer.
- **Regra:** Toda DDI anunciada deve produzir efeito físico ou falhar visivelmente. O veredito "IB EXECUTADO" é exclusivo do caminho `INDIRECT`.

### 🚫 Erro 5: Reintroduzir Fallback Oculto (Plano A)
- **O que aconteceu:** O Plano A escondia falhas de tradução de GPUVA no Node 0 porque reimplementava os comandos diretamente no anel.
- **Regra:** O padrão arquitetural deliberado é `SdmaPlanoAFallback=0` (somente `INDIRECT`). Se a GPUVA de um IB não resolver, a submissão DEVE falhar visivelmente com `STATUS_IO_DEVICE_ERROR` para que a GCVM seja corrigida, e não mascarada.

### 🚫 Erro 6: Duas Fontes de Verdade para Geometria de GPUVA
- **O que aconteceu:** O blob `UMDRIVERPRIVATE` publicava 48 bits enquanto o KMD reportava 34 bits ao SO, gerando endereços que o hardware não conseguia traduzir.
- **Regra:** A geometria de GPUVA tem fonte única (34 bits derivados de `BC250_ADAPTER_GPUVA_BITS`). Nunca crie uma segunda constante separada.

### 🚫 Erro 7: Esquecer de Resetar o Fusível de Boot
- **O que aconteceu:** Tentar rodar um bind após um crash sem resetar o fusível fazia o `StartDevice` ser rejeitado intencionalmente (`State=2`), deixando o dispositivo em Código 43.
- **Regra:** Sempre confira com `tools\inspect-boot-fuse.ps1` e resete com `-Reset` antes de iniciar uma nova corrida de hardware.

---

## 📋 5. Guia Operacional Imediato para a Próxima IA

O pacote **3.104.1.0** está completamente pronto e auditado. O próximo passo é rodar o teste físico:

### Passo 1: Resetar o Fusível (PowerShell Elevado como Administrador)
```powershell
powershell -ExecutionPolicy Bypass -File tools\inspect-boot-fuse.ps1 -Reset
```
*Deve responder com: `Estado: CLEAN (1)`.*

### Passo 2: Executar o Bind de Laboratório Protegido
```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod -WatchdogTimeoutSeconds 180
```

### O Que Monitorar nos Logs da Corrida:
1. `[FlushMappedGfxhubTlb]`: As linhas de transação não podem mais apresentar `Transaction status=8`. Devem sair com `status=0, PhysReady=1, AckObserved=1`.
2. `[SubmitVirtual Node0 SDMA]`: Deve registrar `VMID8 CONTEXT SWITCH: Ctx ... -> ...` com sucesso, sem nenhuma ocorrência de `REJECT VMID8 binding`.
3. `BugCheck 0x119`: Não deve ocorrer durante a submissão de comandos de paginação ou contexto.
4. `[B7_UMD_NODE1_TRIPOD]`: Acompanhar o envio do comando de compute ao Node 1 e verificar se os canários de dados são escritos e a fence física é observada.
5. `Rollback Online`: O watchdog de 180s ou encerramento deve restaurar o BasicDisplay em Código 0 de forma 100% limpa.
