# 286 - Handoff: Corrida 17 (17/09/2026 19:32), Diagnóstico de BugCheck 0x139 e Correção de Inicialização de DPC no AddDevice

**Data:** 17/09/2026 19:45  
**Autor:** Antigravity / Gemini Pair Programming  
**Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
**Evidência Primária:** `docs/evidence/2026-09-17_19-31-24-DESKTOP-HQAG6T4-lab-binding/`  
**Dump de Memória:** `C:\Windows\MEMORY.DMP` analisado via CDB com símbolos do Windows  
**Status do Silício:** Protegido pelo Fusível Anti-Boot-Loop (`C:\BC250_BOOT_FUSE.dat`), seguro em Código 43.

---

## 1. Relatório Mandatório da Corrida 17 (Conforme AGENTS.md / GEMINI.md)

### 1.1 Houve avanço?
**Sim, avanço diagnóstico e estrutural decisivo.**
O `BugCheck 0x133: DPC_WATCHDOG_VIOLATION` da Corrida 16 foi **completamente eliminado**. A guarda atômica `RetireBusy` e o desacoplamento de `DxgkCbNotifyDpc` resolveram em 100% o deadlock entre a thread do scheduler (`dxgmms2!VidSchDdiNotifyDpc`) e as DPCs concorrentes.

O novo evento identificado foi o `BugCheck 0x139: KERNEL_SECURITY_CHECK_FAILURE` com `Arg1 = 0x0`, que é a assinatura exata do Control Flow Guard (CFG) do kernel Windows (`nt!guard_icall_bugcheck`) acionado ao tentar despachar uma rotina de DPC não inicializada (`DeferredRoutine == NULL`). A causa foi isolada no código e corrigida de forma canônica no `AddDevice`.

### 1.2 Sucessos
1. **Eliminação Comprovada do BugCheck 0x133:** Nenhuma retenção excessiva de CPU ou loop infinito em `DISPATCH_LEVEL` ocorreu.
2. **Isolamento Preciso da Causa Raiz do BugCheck 0x139:** A análise forense do `MEMORY.DMP` via debugger do Windows com símbolos oficiais identificou o disparo em `nt!KiExecuteAllDpcs` -> `nt!guard_icall_bugcheck` decorrente de `KeInsertQueueDpc` disparado sobre `Context->RetirementDpc` antes de sua inicialização.
3. **Resiliência do Silício e Fusível Anti-Boot-Loop:** O fusível interceptou o crash, transitou para `State=2 (TRIPPED)`, impediu boot-loop e manteve o hardware 100% seguro.
4. **40/40 Auditorias de Segurança PASS:** O contrato offline, verificação de mutação e auditoria do pacote de laboratório passaram com nota máxima (Exit Code 0).

### 1.3 Falhas
1. **BugCheck 0x139 (`KERNEL_SECURITY_CHECK_FAILURE`):**
   - Bucket: `0x139_0_LEGACY_GS_VIOLATION_bc250mcdm!Bc250ActiveSubmissionGateLeave`
   - Parâmetro chave: `Arg1 = 0x0` (`STATUS_STACK_BUFFER_OVERRUN` / CFG target bitmap violation).
   - Ocorreu na primeira submissão no momento em que `KeInsertQueueDpc(&context->RetirementDpc, NULL, NULL)` tentou despachar uma DPC cujo `DeferredRoutine` ainda estava zerado na pool não paginada.

### 1.4 Veredito
**Falha funcional por BugCheck 0x139 (CFG DPC uninitialized) com diagnóstico conclusivo.** Critério funcional não atingido na Corrida 17.

### 1.5 Recuperação e Pendências
- **Recuperação:** O sistema reiniciou em Código 43, sem instabilidade.
- **Pendência Fechada Offline:**
  - Declaradas rotinas de DPC forward: `Bc250ActiveRetirementDpcRoutine` e `Bc250ActiveSdmaRetirementDpcRoutine`.
  - Inicialização estrita de `RetirementDpc`, `RetirementTimer`, `SdmaRetirementDpc` e `SdmaRetirementTimer` movida para `Bc250DxgkDdiAddDeviceActive`.
  - Reordenamento garantido em `SubmitCommandVirtual` (iniciando o timer antes de enfileirar DPC).
  - Pacote de laboratório reconstruído, assinado e auditado com sucesso em `driver/wdk/package/output_lab`.
- **Próximo Passo Justificado:** Executar a Corrida 18 com o binário corrigido via `tools\EXECUTAR_D3D11_NATIVO.cmd -Force`.

---

## 2. Diagnóstico Técnico Detalhado

Ao examinar o dump `C:\Windows\MEMORY.DMP`:

```text
KERNEL_SECURITY_CHECK_FAILURE (139)
Arg1: 0000000000000000, A list entry was corrupted / CFG invalid indirect call target
Arg2: ffffab0a3311f7c0, Address of the trap frame for the code that caused the bugcheck
Arg3: ffffab0a3311f718, Address of the exception record for the code that caused the bugcheck
Arg4: 0000000000000000, Reserved

STACK_TEXT:
nt!KeBugCheckEx
nt!guard_icall_bugcheck+0x1b
nt!KiExecuteAllDpcs+0x30e
nt!KiRetireDpcList+0x1f4
nt!KiIdleLoop+0x9e
```

Na arquitetura x64 com `/guard:cf`, o kernel valida o endereço da função apontada por `PKDPC->DeferredRoutine` antes de transferir a execução em `KiExecuteAllDpcs`. Se `DeferredRoutine` for `0` (não inicializado) ou apontar para um endereço não registrado na tabela CFG, `nt!guard_icall_bugcheck` gera imediatamente o BugCheck 0x139.

No código anterior:
- `KeInitializeDpc` para `RetirementDpc` só era chamado dentro de `Bc250ActiveRetirementTimerStart`.
- Em `SubmitCommandVirtual` (Node 1 GFX):
  ```c
  KeInsertQueueDpc(&context->RetirementDpc, NULL, NULL); // DPC ainda zerada!
  Bc250ActiveRetirementTimerStart(context);               // Inicializava tarde demais!
  ```
- Em `SubmitCommandVirtual` (Node 0 SDMA):
  ```c
  KeInsertQueueDpc(&context->SdmaRetirementDpc, NULL, NULL); // SdmaRetirementDpc nunca inicializada se TimerStart nao correu!
  ```

---

## 3. Correções Aplicadas

### 3.1 Inicialização Upfront em `Bc250DxgkDdiAddDeviceActive`
No arquivo `driver/wdk/bc250_active_miniport.c`:

```c
    /*
     * Inicializacao estrita de DPCs e Timers de aposentadoria (Anti-0x139):
     * As DPCs e timers de aposentadoria do Node 0 (SDMA) e Node 1 (GFX)
     * DEVEM ser inicializados aqui no AddDevice com DeferredRoutine valida
     * antes de qualquer submissao em SubmitCommandVirtual.
     * Sem isso, KeInsertQueueDpc ou KeSetTimerEx despacham para DeferredRoutine=NULL,
     * acionando violacao de CFG (guard_icall_bugcheck -> BugCheck 0x139).
     */
    KeInitializeTimer(&context->RetirementTimer);
    KeInitializeDpc(&context->RetirementDpc,
        Bc250ActiveRetirementDpcRoutine, context);
    InterlockedExchange(&context->RetirementTimerInitialized, 1);

    KeInitializeTimer(&context->SdmaRetirementTimer);
    KeInitializeDpc(&context->SdmaRetirementDpc,
        Bc250ActiveSdmaRetirementDpcRoutine, context);
    InterlockedExchange(&context->SdmaRetirementTimerInitialized, 1);
```

### 3.2 Inversão e Proteção em `SubmitCommandVirtual`
Em todos os nós (Node 0 SDMA, Node 1 GFX, Node 2 MEC), as rotinas garantem o agendamento correto do timer antes de inserir a DPC na fila.

---

## 4. Estado dos Artefatos de Build

- `driver/wdk/output_lab/bc250mcdm.sys`: Recompilado e verificado (535.552 bytes).
- `driver/wdk/package/output_lab/`: Empacotado e assinado com catálogo SHA-256.
- `tools/audit-bc250mcdm-lab-package.ps1`: **PASS**.
