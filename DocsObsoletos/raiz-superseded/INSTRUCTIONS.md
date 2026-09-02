# INSTRUCTIONS.md — Guia Mestre do Projeto BC250-WinDv

> **Manual de Referência e Instruções Operacionais para Desenvolvedores e Agentes de Inteligência Artificial**  
> *Target: AMD BC-250 APU (`1002:13FE`, rev `00`) | Windows 11 x64 | WDDM MCDM / Kernel Lab*

---

## 1. 🎯 Identidade e Propósito do Projeto

O **BC250-WinDv** é um projeto de engenharia reversa, bring-up de hardware em baixo nível e desenvolvimento de driver nativo para Windows 11 x64 voltado para a APU **AMD BC-250** (chip customizado derivado de console, codinome **Cyan Skillfish 2 / GC 10.1.3 / GFX1013**).

### Especificações Físicas do Hardware-Alvo
- **Dispositivo PCI**: Vendor `0x1002` (AMD), Device `0x13FE`, Revision `0x00`.
- **Memória Unificada**: 16 GB GDDR6 compartilhados dinamicamente entre CPU e GPU.
- **CPU**: Arquitetura AMD Zen 2 (6 ou 8 núcleos físicos ativos dependendo da revisão/modificação de hardware).
- **GPU (Graphics Core)**: GC / IP v10.1.3 (RDNA 1.0 modificado / Navi10-like SOC15), 24 CUs ativas por padrão (em matriz física de até 40 CUs).
- **IP Blocks**:
  - **PSP (Platform Security Processor)**: Mailbox GPCOM em `0x58000` (SOS Firmware loading, TMR ownership).
  - **SMU (System Management Unit) / SMUIO**: Controle de clocks, limites térmicos e estado físico de GFXOFF (`SMUIO_GFX_MISC_CNTL`).
  - **SDMA (System DMA Engine)**: SDMA0 / SDMA1 (transferências lineares, cópia de buffers, fences físicas).
  - **GCVM / GFXHub v2.0**: Tradução de endereços virtuais de GPU (GPUVA), Page Tables e invalidação de TLB (Engine 17).
  - **CP (Command Processor)**: Microcontroladores PFP, ME, CE e MEC (Compute Queues, Direct Load de microcódigo).
  - **RLC (RunList Controller)**: Gerenciamento de energia e estado da GPU fora da ROM.

---

## 2. ⚖️ Filosofia e Leis Fundamentais de Desenvolvimento

Toda IA, agente ou desenvolvedor que interaja com este repositório **DEVE** seguir estritamente as três leis inegociáveis do projeto:

```text
=============================================================================================
1. TRIPÉ DE EVIDÊNCIA FÍSICA (PROVA NO SILÍCIO)
   - Nenhuma funcionalidade é considerada concluída por compilação, retorno de API ou DLL.
   - O trabalho de GPU exige: (1) RPTR alcançando WPTR, (2) Fence física gravada pela GPU na VRAM 
     com verificação de poison, e (3) Dados destino validados byte a byte.
   - Fences de CPU, WARP, fallbacks de software ou mocks são ESTRITAMENTE PROIBIDOS.

2. METODOLOGIA FAIL-CLOSED & TRANSACIONAL
   - Toda alteração em registradores ou memória segue o ciclo:
     PREFLIGHT -> SNAPSHOT -> APPLY -> READBACK -> PROVA FUNCIONAL -> ROLLBACK / TEARDOWN.
   - Se qualquer passo falhar ou se houver divergência, falha-se fechado imediatamente sem 
     deixar estado corrompido no hardware.

3. COMPUTE-FIRST / MCDM ISOLADO
   - O desenvolvimento foca primariamente no modelo Microsoft MCDM (Class=ComputeAccelerator), 
     operando como Render-Only Adapter totalmente desacoplado do display desktop (BasicDisplay).
   - Isso previne BSODs, telas pretas e instabilidade durante os ensaios no silício.
=============================================================================================
```

---

## 3. 📊 Status Atual do Projeto e Barra de Progresso Obrigatória

Antes de iniciar qualquer planejamento ou implementação, **é obrigatório** apresentar a barra de status atualizada:

```text
================================== STATUS DO DRIVER BC-250 ==================================

Base Geral Pré-WDDM (Lab)        ██████████████████▋░  93%   Estável e Isolado       (+5)
SDMA (DMA Engine & Robustez)     ███████████████████░  95%   Provado em Silício      (+5)
GCVM & Tradução de Memória       █████████████████░░░  85%   Gate 20/20 Aprovado    (+50)
RLC, CP & PM4 Direct Load        █████████░░░░░░░░░░░  45%   RLC Fora da ROM         (+10)
Telemetria & Observabilidade     ████████░░░░░░░░░░░░  40%   GFXOFF Sincronizado     (+5)
Compute & Async Queues (MEC/MQD) ███░░░░░░░░░░░░░░░░░  15%   Bloqueado pelo CP       ( 0)
Miniport WDDM / MCDM Produção    ████░░░░░░░░░░░░░░░░  20%   Estrutura e DDIs        (+2)

Conformidade BC250_HARDWARE_RULES: 72% | Miniport MCDM Funcional: 43% | Memória/GPUVA: 25%
=============================================================================================
Veredito Global: NÃO VALIDADO EM HARDWARE (Aguardando fechamento do Ring PM4)
=============================================================================================
```

---

## 4. 🏗️ Arquitetura do Repositório: Dois Drivers

O projeto é estruturado em duas trilhas complementares de drivers de kernel:

```text
BC250-WinDv/
├── dbg/             # DRIVER DE LABORATÓRIO (bc250dbg.sys)
│                    # Driver KMDF de diagnóstico para engenharia reversa, IOCTLs diretos,
│                    # sondagem de MMIO, carga manual de firmware e ensaios físicos no silício.
│
├── driver/          # DRIVER DE PRODUÇÃO WDDM MCDM (bc250mcdm.sys)
│   ├── core/        # Contratos formais, abstratores de hardware, alocadores VRAM e builders de pacotes
│   ├── wdk/         # Miniport WDDM MCDM (Class=ComputeAccelerator), DDIs, callbacks e empacotamento
│   └── tests/       # Testes unitários de contratos e estruturas de dados de kernel
│
├── inc/             # CONTRATOS FORMAIS E DEFINIÇÕES DE ABI
│   ├── bc250dbg_abi.h  # ABI de comunicação entre User-Mode (CLI/Harness) e Driver de Kernel
│   └── linux_ref/      # Headers de referência autoritativos do AMDGPU (SOC15, GC 10.1.3, PSP, SMU)
│
├── tools/           # SUÍTE DE TESTES, AUTOMAÇÃO E RECUPERAÇÃO
│   ├── BC250_RECOVERY.cmd            # Script mestre de recuperação offline para BasicDisplay
│   ├── build-bc250mcdm-package.ps1   # Pipeline de build e empacotamento assinado do MCDM
│   ├── test-sdma-*.ps1               # Ensaios físicos da engine SDMA
│   ├── test-gcvm-*.ps1               # Ensaios de tradução de memória e page tables
│   └── audit-*.ps1 / *.py            # Scripts de auditoria estática de conformidade e segurança
│
├── firmware/        # BINÁRIOS OFICIAIS DE MICROCODIGO CYAN SKILLFISH
│   ├── cyan_skillfish2_sdma.bin      # Microcódigo autenticado SDMA
│   ├── cyan_skillfish2_rlc.bin       # Microcódigo RLC
│   ├── cyan_skillfish2_pfp.bin / me  # Microcódigos CP (PFP, ME, CE, MEC)
│   └── manifest.json                 # Hashes SHA-256 e tamanhos formais dos blobs
│
├── docs/            # CATÁLOGO DE DOCUMENTAÇÃO TÉCNICA (01 a 41)
│   ├── evidence/    # Registros imutáveis de execuções físicas e dumps no silício
│   └── ...          # Documentos normativos e históricos consolidados
│
└── studio/          # APLICAÇÕES DE CONTROLE E INTERFACE (bc250ctl.exe)
```

---

## 5. 📚 Guia de Leitura e Catálogo da Documentação (`docs/`)

Para entender a fundo o projeto, utilize a seguinte trilha de leitura:

### Fase 1: Fundamentos, Regras e Arquitetura
1. [`BC250_HARDWARE_RULES.md`](BC250_HARDWARE_RULES.md) — Norma suprema de validação física no silício.
2. [`docs/01_EVIDENCIAS_E_LICOES.md`](docs/01_EVIDENCIAS_E_LICOES.md) — O que foi provado, erros do passado e premissas revogadas.
3. [`docs/02_ARQUITETURA_AMDGPU.md`](docs/02_ARQUITETURA_AMDGPU.md) — Modelo de decomposição do kernel Linux AMDGPU aplicado ao Windows.
4. [`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md) — Catálogo normalizado de MMIO SOC15, GCVM, CP, SDMA, PSP e SMU.
5. [`docs/04_PLANO_DO_DRIVER_NOVO.md`](docs/04_PLANO_DO_DRIVER_NOVO.md) — Arquitetura modular, gates e contratos formais.
6. [`docs/09_PONTO_DE_RETOMADA.md`](docs/09_PONTO_DE_RETOMADA.md) — Ponto consolidado de partida, runbook operacional e armadilhas conhecidas.

### Fase 2: Roadmap e Evolução do Driver MCDM
7. [`docs/20_ROADMAP_WDDM_MCDM_VULKAN_LLM.md`](docs/20_ROADMAP_WDDM_MCDM_VULKAN_LLM.md) — Roteiro completo: MCDM -> Vulkan ICD -> `llama.cpp` acelerado.
8. [`docs/19_MARCO_MCDM_FAIL_CLOSED.md`](docs/19_MARCO_MCDM_FAIL_CLOSED.md) — Retirada de ownership fabricado e implantação do fail-closed.
9. [`docs/22_PACOTE_MCDM_FAIL_CLOSED_1.0.1.0.md`](docs/22_PACOTE_MCDM_FAIL_CLOSED_1.0.1.0.md) até [`docs/41_GCVM_BACKING_OWNED_VMID8_INERTE_1.16.0.0.md`](docs/41_GCVM_BACKING_OWNED_VMID8_INERTE_1.16.0.0.md) — Histórico de versões e marcos de validação física do pacote de produção (1.0.1.0 a 1.16.0.0).

---

## 6. 🛠️ Workflows de Compilação, Testes e Recuperação

### Compilação do Driver de Laboratório (`bc250dbg.sys`)
```cmd
:: Executar no prompt do desenvolvedor x64 com WDK 10/11
cd c:\Users\Daniel\Desktop\NamixWDDM\dbg
build.bat
install.bat
start.bat
```

### Compilação do Driver de Produção MCDM (`bc250mcdm.sys`)
```powershell
# Executar a pipeline de build do contrato MCDM
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-package.ps1
```

### Execução de Ensaios Físicos de Hardware
```powershell
# Teste de leitura de baseline e responsividade MMIO
powershell -ExecutionPolicy Bypass -File tools\capture-baseline.ps1

# Teste de robustez de submissões SDMA no silício
powershell -ExecutionPolicy Bypass -File tools\test-sdma-robustness.ps1 -Execute

# Teste de tradução de memória GCVM (exige flag explícita de risco)
powershell -ExecutionPolicy Bypass -File tools\test-gcvm-translate.ps1 -Execute -IAcceptTheRisk
```

### Procedimento de Emergência e Recuperação (Rollback)
Se a máquina travar, perder sinal de vídeo ou o miniport causar instabilidade:
1. Reiniciar no **Modo de Segurança** do Windows.
2. Executar o script mestre: `C:\BC250_RECOVERY.cmd` (ou `tools\BC250_RECOVERY.cmd`).
3. O script remove os drivers experimentais (`oem*.inf`), desabilita serviços e restaura o adaptador para o `BasicDisplay` (Microsoft Basic Display Driver, Código 0).

---

## 7. 🚫 Anti-Padrões e Práticas Terminantemente Proibidas

1. **PROIBIDO** simular trabalho com `KeStallExecutionProcessor`, `Sleep` ou incrementando fences via CPU.
2. **PROIBIDO** retornar `STATUS_SUCCESS` em DDIs de hardware sem backend implementado e provado.
3. **PROIBIDO** escrever cegamente em registradores sem aplicar a máscara SOC15 `(lido & ~mask) | (or_value & mask)`.
4. **PROIBIDO** utilizar endereços em quarentena (`0x58000` fora do protocolo formal, `SETUP_TMR` sem preflight, etc.).
5. **PROIBIDO** declarar qualquer resultado como "acelerado" ou "validado" se faltar qualquer uma das 10 evidências físicas obrigatórias descritas em [`rules.md`](rules.md).
