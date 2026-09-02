# BC250-WinDv

[![Target](https://img.shields.io/badge/Target-Windows_11_x64-blue.svg)](https://microsoft.com)
[![Architecture](https://img.shields.io/badge/Architecture-GFX10.1.3_%2F_Cyan_Skillfish-purple.svg)](docs/02_ARQUITETURA_AMDGPU.md)
[![Status](https://img.shields.io/badge/Status-N%C3%A3o_validado_em_hardware_(WDDM)-orange.svg)](BASE_DE_AVANCO.md)
[![Silicon-Proof](https://img.shields.io/badge/Validation-Fail--Closed_Silicon_Tested-green.svg)](rules.md)

Driver nativo para Windows e suíte de laboratório para engenharia reversa e bring-up de hardware da APU **AMD BC-250** (`1002:13FE`, revisão `00`), baseada no core gráfico **Cyan Skillfish 2 (GC / IP 10.1.3)** com 16 GB de memória GDDR6 compartilhada.

---

## 📊 Status Atual do Desenvolvimento

```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pre-WDDM (Lab)        ██████████████████▊░  93%   Estavel e Isolado
SDMA (DMA Engine & Robustez)     ███████████████████░  95%   Provado em Silicio
GCVM & Traducao de Memoria       ██████████████████░░  90%   PTE 0x10 provado; sem GPUVA dinamica
RLC, CP & PM4 Direct Load        ███████████████████░  95%   Fence em VRAM provada
Telemetria & Observabilidade     █████████████████░░░  85%   Schema 134; anel de DDI e bitmap de QAI
Compute & Async Queues (MEC/MQD) ██████████████████░░  90%   P1 17/17 e P2 16/16 provados no silicio
Miniport WDDM Display+Render     ███████░░░░░░░░░░░░  35%   Codigo 43 no retorno do StartDevice
=============================================================================================
Veredito Global: NAO VALIDADO EM HARDWARE
=============================================================================================
```

> **O que fazer agora:** [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md) e o documento base do
> projeto — Tabela 1 (ja feito, com prova fisica) e Tabela 2 (o que falta, por prioridade,
> com o teste que fecha cada item). Roadmap detalhado em
> [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md).

---

## 🎯 Filosofia e Metodologia

1. **Validação Obrigatória em Silício Real:** Nenhuma funcionalidade é declarada como concluída por simples retorno de função, carregamento de DLL ou stubs de software. O critério inegociável exige o tripé físico:
   - **RPTR** avançando e alcançando o **WPTR**.
   - **Fence física** gravada na VRAM pela própria GPU (com verificação de poison).
   - **Dados íntegros** conferidos byte a byte no destino.
2. **Abordagem Fail-Closed & Transacional:** Toda operação de hardware no laboratório opera sob o modelo `snapshot -> apply -> readback -> rollback`. Nenhuma escrita no hardware é executada sem garantias de reversão limpa.
3. **Display por heranca do UEFI GOP:** o destino e um miniport WDDM full display+render. O DCN **nao** esta no caminho critico: o GOP mantem OTG/DIG/PHY acesos e o driver herda o modo, apresentando por blit via SDMA. O caminho render-only/MCDM foi **reprovado por medicao** e esta encerrado (ver [`docs/162`](docs/162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md)).

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

Consulte o documento normativo [BC250_HARDWARE_RULES.md](rules.md) para detalhes sobre as restrições arquiteturais, métodos de exclusão de fallbacks por software e critérios formais para aprovação de gates.

---

## 🗺️ Próximos Passos
- [ ] Conectar a busca de instrução do CP (Command Processor) à abertura de memória do VMID 0.
- [ ] Executar a primeira submissão no Ring 0 PM4 com verificação de Fence física.
- [ ] Iniciar a implementação das DDIs de produção do miniport WDDM / MCDM.
