# BC250-WinDv

[![Target](https://img.shields.io/badge/Target-Windows_11_x64-blue.svg)](https://microsoft.com)
[![Architecture](https://img.shields.io/badge/Architecture-GFX10.1.3_%2F_Cyan_Skillfish-purple.svg)](docs/02_ARQUITETURA_AMDGPU.md)
[![Status](https://img.shields.io/badge/Status-Laborat%C3%B3rio_Pr%C3%A9--WDDM_(93%25)-orange.svg)](docs/08_STATUS_PRE_WDDM.md)
[![Silicon-Proof](https://img.shields.io/badge/Validation-Fail--Closed_Silicon_Tested-green.svg)](BC250_HARDWARE_RULES.md)

Driver nativo para Windows e suíte de laboratório para engenharia reversa e bring-up de hardware da APU **AMD BC-250** (`1002:13FE`, revisão `00`), baseada no core gráfico **Cyan Skillfish 2 (GC / IP 10.1.3)** com 16 GB de memória GDDR6 compartilhada.

---

## 📊 Status Atual do Desenvolvimento

```text
================================== STATUS DO DRIVER BC-250 ==================================

Base Geral Pré-WDDM (Lab)        ██████████████████▋░  93%   Estável e Isolado       (+5)
SDMA (DMA Engine & Robustez)     ███████████████████░  95%   Provado em Silício      (+5)
GCVM & Tradução de Memória       █████████████████░░░  85%   Gate 20/20 Aprovado    (+50)
RLC, CP & PM4 Direct Load        █████████░░░░░░░░░░░  45%   RLC Fora da ROM         (+10)
Telemetria & Observabilidade     ████████░░░░░░░░░░░░  40%   GFXOFF Sincronizado     (+5)
Compute & Async Queues (MEC/MQD) ███░░░░░░░░░░░░░░░░░  15%   Bloqueado pelo CP       ( 0)
Miniport WDDM / MCDM Produção    ████░░░░░░░░░░░░░░░░  20%   Estrutura e DDIs        (+2)

=============================================================================================
Veredito Global: NÃO VALIDADO EM HARDWARE (Aguardando fechamento do Ring PM4)
=============================================================================================
```

---

## 🎯 Filosofia e Metodologia

1. **Validação Obrigatória em Silício Real:** Nenhuma funcionalidade é declarada como concluída por simples retorno de função, carregamento de DLL ou stubs de software. O critério inegociável exige o tripé físico:
   - **RPTR** avançando e alcançando o **WPTR**.
   - **Fence física** gravada na VRAM pela própria GPU (com verificação de poison).
   - **Dados íntegros** conferidos byte a byte no destino.
2. **Abordagem Fail-Closed & Transacional:** Toda operação de hardware no laboratório opera sob o modelo `snapshot -> apply -> readback -> rollback`. Nenhuma escrita no hardware é executada sem garantias de reversão limpa.
3. **Compute-First / MCDM:** Priorização do modelo de computação e render-only adapter isolado do display (`BasicDisplay`) para mitigar riscos de instabilidade do desktop e tela preta durante a evolução do driver.

---

## 🔬 Principais Conquistas Técnicas no Silício

- **PSP & Carga de Firmware:** Protocolo GPCOM em `0x58000` estabelecido; microcódigo oficial (`cyan_skillfish2_sdma.bin` e `cyan_skillfish2_rlc.bin`) autenticado com sucesso pelo SOS (`RespStatus = 0`).
- **SDMA Engine:** Submissões `WRITE_LINEAR` provadas com fences físicas; wraparound de anel, retomada após idle e 10 gates de robustez negativos (`-nokick`, `-badfence`, `-badop`) aprovados.
- **GCVM & Tradução de Memória:** Gate de tradução VMID 0 fechado com **20/20 gates**. Submissões com endereço virtual traduzindo para VRAM física através de Page Tables reais na GPU.
- **RLC fora da ROM:** Microcontrolador RLC retirado do bootloader (`RLC_EXEC_ROM_CODE = 0`, `RLC_GPM_STAT = 0x00B40016`), com estado de registradores 100% idêntico ao driver Linux de referência (`amdgpu`).
- **Telemetria em Alta Frequência:** Amostragem sincronizada por QPC correlacionando o estado de GFXOFF físico (`SMUIO_GFX_MISC_CNTL`), `GRBM_STATUS`, clocks e sensores térmicos.

---

## 📁 Estrutura do Repositório

```text
BC250-WinDv/
├── dbg/             # Driver de laboratório (bc250dbg.sys), hooks de diagnóstico e IOCTLs
├── inc/             # Definições formais de ABI, estruturas de dados e contratos de transporte
├── tools/           # Harness de testes automatizados em PowerShell e scripts de análise
├── firmware/        # Binários de microcódigo Cyan Skillfish e manifestos de integridade
├── docs/            # Catálogo de registradores, lições aprendidas, arquitetura e evidências
│   ├── evidence/    # Registros imutáveis de cada execução e medição física no silício
│   └── ...          # Documentação técnica consolidada (01 a 09)
├── studio/          # CLI (bc250ctl.exe), scripts de build e painel de diagnóstico
└── referencias/     # Dumps comparativos do Linux (amdgpu) e engenharia reversa
```

---

## 🛠️ Como Compilar e Testar

### Pré-requisitos
- Windows 11 x64 com Modo de Teste (`testsigning on`) ativado.
- Windows Driver Kit (WDK) e Windows SDK instalados.
- PowerShell 5.1+.

### 1. Compilação do Driver de Laboratório
```cmd
dbg\build.bat
```

### 2. Instalação e Inicialização
```cmd
dbg\install.bat
dbg\start.bat
```

### 3. Execução dos Testes Automatizados
```powershell
# Verificar responsividade e registradores MMIO
powershell -File tools\test-mmio-responsividade.ps1 -Execute

# Testar robustez e recuperação da SDMA
powershell -File tools\test-sdma-robustness.ps1 -Execute

# Testar tradução de memória GCVM (exige confirmação explícita de risco)
powershell -File tools\test-gcvm-translate.ps1 -Execute -IAcceptTheRisk
```

---

## 📜 Regras de Hardware & Governança

Consulte o documento normativo [BC250_HARDWARE_RULES.md](BC250_HARDWARE_RULES.md) para detalhes sobre as restrições arquiteturais, métodos de exclusão de fallbacks por software e critérios formais para aprovação de gates.

---

## 🗺️ Próximos Passos
- [ ] Conectar a busca de instrução do CP (Command Processor) à abertura de memória do VMID 0.
- [ ] Executar a primeira submissão no Ring 0 PM4 com verificação de Fence física.
- [ ] Iniciar a implementação das DDIs de produção do miniport WDDM / MCDM.
