# CLAUDE.md — Diretrizes de Operação para Claude Code (Anthropic)

> **Contexto de Projeto e Guia de Engenharia para Claude Code no Repositório BC250-WinDv**  
> *Target: AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 11 x64 Kernel Driver*

---

## ⚡ Comandos Rápidos de Build, Teste e Auditoria

### 1. Driver de Laboratório (`bc250dbg.sys`)
- **Build**: `dbg\build.bat`
- **Instalar**: `dbg\install.bat`
- **Iniciar Serviço**: `dbg\start.bat`
- **Parar / Desinstalar**: `dbg\uninstall.bat`

### 2. Driver de Produção WDDM MCDM (`bc250mcdm.sys`)
- **Build e Empacotamento Assinado**: `powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-package.ps1`
- **Auditoria de Conformidade DDI**: `python tools\audit-mcdm-ddi-matrix.py`
- **Auditoria de Segurança MCDM**: `python tools\audit-bc250mcdm-safety.py`

### 3. Ensaios Físicos de Hardware
- **Baseline e Responsividade MMIO**: `powershell -ExecutionPolicy Bypass -File tools\capture-baseline.ps1`
- **Validação SDMA no Silício**: `powershell -ExecutionPolicy Bypass -File tools\test-sdma-robustness.ps1 -Execute`
- **Validação GCVM Tradução de Memória**: `powershell -ExecutionPolicy Bypass -File tools\test-gcvm-translate.ps1 -Execute -IAcceptTheRisk`

### 4. Recuperação de Emergência (Safe Mode / Rollback)
- **Restaurar BasicDisplay**: `tools\BC250_RECOVERY.cmd` (ou `C:\BC250_RECOVERY.cmd`)

---

## 🛑 Leis Inegociáveis para o Claude Code

1. **Apresentação Obrigatória da Barra de Status**:
   Sempre inicie suas propostas e análises com a barra de progresso do projeto:
   ```text
   ================================== STATUS DO DRIVER BC-250 ==================================
   Base Geral Pre-WDDM (Lab)        ██████████████████▋░  93%   Estavel e Isolado
   SDMA (DMA Engine & Robustez)     ███████████████████░  95%   Provado em Silicio
   GCVM & Traducao de Memoria       ██████████████████░░  90%   PTE 0x10 provado; sem GPUVA dinamica
   RLC, CP & PM4 Direct Load        ███████████████████░  95%   Fence em VRAM provada
   Telemetria & Observabilidade     █████████████████░░░  85%   93 campos P1
   Compute & Async Queues (MEC/MQD) ██████████████████░░  90%   One-shot provado n=3; fila persistente nao consome
   Miniport WDDM / MCDM Producao    ███████░░░░░░░░░░░░░  35%   P1 em andamento (candidato 3.46)
   =============================================================================================
   Veredito Global: NAO VALIDADO EM HARDWARE
   =============================================================================================
   ```

2. **Tripé de Validação no Silício**:
   Não declare sucesso, aceleração ou conclusão sem o tripé físico comprovado:
   - `RPTR == WPTR` na engine correspondente.
   - Fence física gravada pela GPU na VRAM (com verificação de poison).
   - Dados destino conferidos byte a byte com o gabarito.
   Se qualquer prova estiver ausente: declare **`NÃO VALIDADO EM HARDWARE`**.

3. **Zero Fakes / Zero Software Fallbacks**:
   - Nunca incremente fences ou timestamps via CPU.
   - Nunca use loops de delay (`KeStallExecutionProcessor`, `Sleep`) simulando execução.
   - Nunca retorne `STATUS_SUCCESS` em DDIs sem backend funcional.

4. **Fórmula de Bits SOC15**:
   Ao modificar registradores com máscara:
   `uint32_t val = (lido & ~mask) | (or_val & mask);`

---

## 💻 Padrões de Código C (Windows Kernel Mode)

- **Dialeto**: C99/C11 compatível com MSVC x64 e WDK.
- **Rigor de Compilação**: Todo código deve passar limpo em `/W4 /WX` (zero warnings).
- **Anotações SAL**: Utilize anotações formais da Microsoft (`_In_`, `_Out_`, `_Inout_`, `_Check_return_`, `_Pre_null_`, etc.).
- **Sem CRT de User-Mode**: Proibido `malloc`, `free`, `printf`, `exit`, etc. Use sempre primitivas de kernel:
  - Alocação: `ExAllocatePool2(POOL_FLAG_NON_PAGED, size, tag)` ou `ExAllocatePoolWithTag`.
  - Liberação: `ExFreePoolWithTag(ptr, tag)`.
  - Cópia e Preenchimento: `RtlCopyMemory`, `RtlZeroMemory`, `RtlEqualMemory`.
  - Diagnóstico: `DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, ...)`.
- **Tratamento Defensivo de Erros**:
  - Verifique sempre ponteiros de entrada contra `NULL`.
  - Verifique `NTSTATUS` com `if (!NT_SUCCESS(status)) { ... return status; }`.
  - Garanta que toda alocação ou mapeamento tenha cleanup garantido em caso de falha.
