# Central de Testes e Diagnóstico de Silício — AMD BC-250 (BC250-V3)

Central unificada de testes de silício e verificação de hardware para o driver nativo **AMD BC-250 (Cyan Skillfish / Oberon, `PCI\VEN_1002&DEV_13FE`)**.

Projetada com suporte duplo:
- **GUI (Interface Gráfica para o Usuário):** Aplicação WPF com Dark Cyber Theme, cards de telemetria em tempo real, painel de testes interativo, explorador de registradores e exportador de relatórios.
- **CLI (Interface de Linha de Comando para o Agente e Automação):** Execução automatizada com saídas estruturadas em JSON e tabela consolidada.

---

## 1. Como Executar

### 1.1 Para o Usuário (Interface Gráfica)
Basta dar dois cliques no arquivo:
```cmd
Iniciar_GUI.bat
```
ou pelo PowerShell:
```powershell
powershell -ExecutionPolicy Bypass -File CentralDeTestes.ps1
```

---

### 1.2 Para o Agente / Automação (CLI)
Execução direta via terminal:

```cmd
:: Executar a bateria completa de testes de silício e auditoria de fases
CentralDeTestes_CLI.cmd -All

:: Executar um teste específico (ex: T1, T2, T5, T6)
CentralDeTestes_CLI.cmd -Test T1
CentralDeTestes_CLI.cmd -Test T2
CentralDeTestes_CLI.cmd -Test T5

:: Executar auditoria de uma fase específica (0, 1, 2, 3 ou 4)
CentralDeTestes_CLI.cmd -Phase 0
CentralDeTestes_CLI.cmd -Phase 2

:: Obter resultado estruturado em JSON para consumo programático
CentralDeTestes_CLI.cmd -All -Json

:: Gerar relatório Markdown em reports/
CentralDeTestes_CLI.cmd -All -Report
```

---

## 2. Bateria de Testes Fundamentais (T1 a T10)

Todos os testes seguem rigorosamente o modelo de 3 estados com emissão de critérios físicos antes da medição:

| ID | Teste | Objetivo de Silício | Critério de Aceite |
|---|---|---|---|
| **T1** | Scratch MMIO (0x32D4) | Provar que o MMIO do host alcança o bloco GC | `0x32D4` grava `0x4D585042` e lê o mesmo valor de volta |
| **T2** | Scratch via PM4 | Provar se o Command Processor executa microcódigo | Host escreve A em `0x32D4`, PM4 `WRITE_DATA` escreve B, host lê B |
| **T3** | Write-Stick Sweep | Mapear comportamento de registradores críticos | Sweep em `0xC104`, `0xC150`, `0xDA78`, `0x5C3C`, `0x9C1C` |
| **T4** | Fill-Pattern Detector | Detectar offsets inválidos | Flaggar leituras `0xDEFxDEFx` (ex: `0x8684`) ou `0xBADBAD00` |
| **T5** | Fence Poison & SDMA | Validar engine DMA e medir taxa real | Fence preenchida com `0xDEADBEEF`, GPU atualiza via DMA (> 5 GB/s) |
| **T6** | Carga × Telemetria SMU | Amostrar reação dinâmica do SMU | Amostragem 10Hz de temperatura, frequência e potência |
| **T7** | Validador PSP Offline | Validar estruturas binárias sem tocar no HW | Assertar tamanhos de frame (64B) e alinhamento de GPCOM |
| **T8** | Auditor de Fallback CPU | Detectar simulações proibidas no código | Varredura estática de loops CPU e retornos hardcoded |
| **T9** | Bisseção de Bring-up | Diagnosticar passo exato do kernel | Leitura de `HwBringupStatus`, `Hw_PspTosReg`, etc. do registro |
| **T10** | Baseline Plausibilidade | Impedir falsos positivos | Assertar limites teóricos de throughput e consistência |

---

## 3. Estados de Veredito (Metodologia Honesta)

1. **`PROVADO`:** Todas as condições físicas de silício foram satisfeitas e comprovadas.
2. **`NÃO VALIDADO EM HARDWARE`:** O teste executou mas não houve confirmação no silício (ex: RPTR não avançou ou o valor gravado não foi alterado pela GPU).
3. **`BLOQUEADO`:** Pré-requisito de hardware/firmware ausente (ex: microcódigo não disponível no SOS).
4. **`ERRO`:** Falha de comunicação com o driver WDDM via escape D3DKMT.

---

## 4. Estrutura de Diretórios

```
CentralDeTestes/
├── Iniciar_GUI.bat                 # Launcher 1-click para a interface gráfica (usuário)
├── CentralDeTestes_CLI.cmd         # Launcher de linha de comando (agente / scripts)
├── CentralDeTestes.ps1             # Ponto de entrada PowerShell unificado
├── README.md                       # Documentação da Central de Testes
├── build_all.bat                   # Script de compilação dos binários C via MSVC
├── gui/
│   └── CentralDeTestes_WPF.ps1     # Interface gráfica em WPF (Dark Theme, Cards, Logs)
├── cli/
│   └── Runner_CLI.ps1              # Motor CLI com argument parsing e emissor JSON/Markdown
├── src/
│   ├── test_framework.h            # Header comum com estruturas D3DKMT e macros de veredito
│   ├── t1_scratch_mmio.c           # Fonte do Teste T1
│   ├── t2_pm4_scratch.c            # Fonte do Teste T2
│   ├── t3_write_stick.c            # Fonte do Teste T3
│   ├── t4_fill_pattern.c           # Fonte do Teste T4
│   ├── t5_sdma_fence_poison.c      # Fonte do Teste T5
│   ├── t6_telemetry_correlation.c  # Fonte do Teste T6
│   ├── t7_psp_protocol_validate.c  # Fonte do Teste T7
│   ├── t8_fallback_audit.c         # Fonte do Teste T8
│   ├── t9_bringup_bisect.c         # Fonte do Teste T9
│   ├── t10_baseline_plausibility.c # Fonte do Teste T10
│   └── phase_audit.c               # Auditoria consolidada das Fases 0 a 4
├── bin/                            # Binários compilados (.exe)
└── reports/                        # Relatórios gerados em Markdown e JSON
```
