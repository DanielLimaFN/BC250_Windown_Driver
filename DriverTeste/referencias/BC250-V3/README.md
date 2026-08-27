# Driver Nativo AMD BC-250 (BC250-V3 WDDM 2.0+)

**Driver de Kernel WDDM 2.0+ Display-Only Miniport com Controle Direto de Silício**
**Alvo:** AMD BC-250 APU (Cyan Skillfish / Navi12 Lite / Oberon - 24 CUs stock / 1536 Shaders / 256 MB BAR0 mapeada)

> ⚠️ **Status de Aceleração:** Em conformidade com `BC250_HARDWARE_RULES.md`, nenhuma funcionalidade de renderização ou compute de GPU é declarada acelerada sem prova física irrefutável no silício (RPTR avançando, fence escrita pela GPU, resultado divergente do poison e ausência de fallback de CPU).
> **Estado Atual:** **NÃO VALIDADO EM HARDWARE**

---

## 1. Visão Geral

O **BC250-V3** é uma implementação transparente e auditável do driver de kernel para a AMD BC-250 sob Windows.
O driver opera como KMDOD (Kernel Mode Display-Only Driver) com interface D3DKMT Private Escape para controle direto e diagnóstico do silício (MMIO, SMU, PSP, VRAM, SDMA e CP Ring).

```
                            FLUXO DE SILÍCIO BC250-V3 (WDDM 2.0+)
                           
  [ UEFI / BIOS ] ──► [ BARs & GOP ] ──► [ MMIO / KMD ] ──► [ SMU Tabela DMA (Telemetria) ]
                                                                      │
  [ D3DKMT Escapes ] ◄── [ CP & SDMA Rings ] ◄── [ 24 CUs Detectadas ] ◄── [ PSP GPCOM Ring ]
```

---

## 2. Estrutura do Módulo `BC250-V3`

```
BC250-V3/
├── wddm.h                   # Definições de registradores SOC15, BARs, structs de Escape e context do KMD
├── wddm_fw.h                # Microcódigos binários embutidos
├── wddm_hw.c                # Motor de silício: SMU, PSP, GCVM L2 MMU, CP Ring 0, SDMA0, Compute
├── wddm_driver.c            # KMDOD WDDM 2.0+ (DriverEntry, VidPN, PresentDisplayOnly, Escapes)
├── wddm.inf                 # INF PnP limpo para classe Display (Serviço: bc250v3)
├── build.bat                # Script de compilação WDDM 2.0+, catálogo wddm.cat e assinatura
├── install-wddm.ps1         # Script de instalação PnP no Windows
├── diag.ps1                 # Script de diagnóstico e telemetria granular
└── README.md                # Esta documentação
```

---

## 3. Estágios de Controle de Silício e Diagnóstico

1. **PCIe & BARs**:
   - `BAR5`: 512 KB MMIO (`0x00080000`)
   - `BAR0`: Abertura física GDDR6 mapeada (256 MB)
2. **Kernel Entry & Mapeamento**:
   - `DriverEntry` com `DXGKDDI_INTERFACE_VERSION_WDDM2_0` (`0x5023`)
   - `MmMapIoSpaceEx` para MMIO (NonCached) e Framebuffer (WriteCombine)
3. **SMU (System Management Unit)**:
   - Handshake via `TestMessage`
   - Leitura de telemetria por tabela DMA contígua (temperaturas, tensões, clocks e potência comprovadamente variando)
4. **PSP (Platform Security Processor)**:
   - Anel GPCOM de 4 KB em Host RAM contígua (`MmAllocateContiguousMemorySpecifyCache`)
   - Carveout de segurança TMR de 4 MB na VRAM (`offset 0x0F800000`)
   - `fw_type=18` (SMU) aceito pelo SOS; tipos CP/RLC/SDMA retornam `TEE_ERROR_ITEM_NOT_FOUND`
5. **Configuração de Shader Array e WGPs**:
   - `CC_GC_SHADER_ARRAY_CONFIG` (`0x089BC` = `0xFFF80000`): 24 CUs / 12 WGPs / 1536 SPs stock detectados
   - `SPI_PG_ENABLE_STATIC_WGP_MASK` (`0x0935C`): gravável pelo host
6. **GCVM L2 MMU & System Aperture**:
   - Mapeamento contínuo da VRAM GDDR6 e flush de TLB + invalidação de cache HDP
7. **Anéis Circulares de Hardware**:
   - **CP PM4 Ring 0**: offset `0x0FE00000` na VRAM (programável, aguarda microcódigo/MQD para consumo)
   - **SDMA0 Ring**: offset `0x0FD00000` na VRAM (em diagnóstico de estado de latência)
8. **D3DKMT Private Escape Bridge**:
   - Acesso de baixa latência em modo usuário para diagnóstico de hardware sem fallbacks de software

---

## 4. D3DKMT Private Escape Reference

| Command ID | Nome | Descrição |
| :--- | :--- | :--- |
| `0x01` | `BC250_ESCAPE_CMD_SUBMIT_PM4` | Submissão direta de pacotes PM4 no Ring 0 |
| `0x02` | `BC250_ESCAPE_CMD_READ_REG` | Leitura direta de registrador MMIO de 32 bits |
| `0x03` | `BC250_ESCAPE_CMD_WRITE_REG` | Escrita direta em registrador MMIO de 32 bits |
| `0x04` | `BC250_ESCAPE_CMD_QUERY_INFO` | Consulta de estado de anéis, ponteiros e registradores |
| `0xBD04` | `BC250_ESCAPE_SDMA_COPY` | Cópia de memória acelerada pela engine DMA física |
| `0xBD05` | `BC250_ESCAPE_SDMA_BENCHMARK` | Benchmark de largura de banda da SDMA0 |
| `0xBD06` | `BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS` | Consulta de capacidades de compute (1536 Shaders, 12 WGPs) |
| `0xBD07` | `BC250_ESCAPE_CMD_COMPUTE_DISPATCH` | Disparo direto de Compute Shader no hardware |
| `0xBD08` | `BC250_ESCAPE_CMD_VRAM_WRITE` | Escrita direta na GDDR6 via BAR0 |
| `0xBD09` | `BC250_ESCAPE_CMD_VRAM_READ` | Leitura direta da GDDR6 via BAR0 |
| `0xBD0B` | `BC250_ESCAPE_CMD_SUBMIT_IB` | Submissão de Indirect Buffers (IBs) com fence opcional |
| `0xBD10` | `BC250_ESCAPE_CMD_QUERY_TELEMETRY` | Leitura de telemetria SMU (temperatura, voltagem, clocks, potência) |

---

## 5. Compilação e Instalação

### Compilar o Driver:
Execute em prompt de comando ou terminal:
```cmd
cd BC250-V3
build.bat
```

### Instalar no Windows:
Execute em um PowerShell elevado (Como Administrador):
```powershell
cd BC250-V3\package
.\install-wddm.bat
```

### Diagnóstico de Operação:
```powershell
cd BC250-V3
powershell -ExecutionPolicy Bypass -File diag.ps1
```
