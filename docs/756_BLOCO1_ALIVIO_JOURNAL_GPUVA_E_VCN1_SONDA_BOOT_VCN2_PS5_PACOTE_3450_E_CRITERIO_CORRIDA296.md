# 756 — Bloco 1: Alívio do Journal GPUVA/Taint e VCN.1: Sonda/Boot Experimental do VCN 2.0 (PS5 Linux) — Pacote 3450 e Critério da Corrida 296

> **Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2 / Oberon) | Windows 10 22H2 x64  
> **Data:** 14/10/2026  
> **Pacote:** lab `3.450.1.0` (sobre o `3.449.1.0` da Corrida 295 / [doc 754](754_PAG1_ESPERA_DE_PAGINACAO_ADIADA_MENU1_PSO1B_PESQUISA_APOS_BIND_PACOTE_3449_E_CRITERIO_CORRIDA295.md))  
> **Objetivo:** 
> 1. Aliviar saturação do Journal GPUVA e contaminação de taint (Bloco 1 pré-jogos).
> 2. Experimentar a ativação do bloco de vídeo VCN 2.0 utilizando o microcódigo e sequência de boot do PS5 Linux (`ps5_vcn.bin`).

---

## 1. Contexto e Diagnóstico

Nas corridas 294 e 295, com a Pesquisa (`SearchApp.exe` + `msedgewebview2.exe`) rodando nativamente na GPU junto com `dwm.exe` e o menu iniciar, identificamos:
1. **Saturação do Journal GPUVA:** Com múltiplos processos mapeando alocações simultâneas, o limite estático de 4 slots por processo (`BC250_ACTIVE_GPUVA_JOURNAL_SLOTS`) e 16 slots na tabela global de taints (`BC250_ACTIVE_JOURNAL_TAINT_SLOTS`) ficava sob pressão, arriscando rejeições espúrias de `NO_SPACE`. Além disso, se a tabela enchesse uma vez (`JournalTaintAll = 1`), a flag nunca era curada após o esvaziamento.
2. **Oportunidade do VCN 2.0 (Oberon / Cyan Skillfish 2):** A APU BC-250 compartilha a mesma arquitetura do processador do PlayStation 5 (Oberon / Cyan Skillfish 2). No repositório `referencias/ps5-linux-patches`, os desenvolvedores do Linux para PS5 comprovaram que o bloco VCN 2.0.3 não depende de autenticação de firmware pelo PSP SOS (`LOAD_IP_FW`) e não requer doorbells: a CPU carrega `ps5_vcn.bin` diretamente na memória contígua (VRAM ou DRAM) e dispara a VCPU através de MMIO nos registradores UVD/VCN.

---

## 2. Implementações Realizadas

### 2.1 Bloco 1 — Alívio do Journal GPUVA e Taint
* **Expansão de Slots:**
  * `BC250_ACTIVE_GPUVA_JOURNAL_SLOTS`: expandido de 4 para **16 slots** por processo (`driver/wdk/bc250_active_miniport.h`).
  * `BC250_ACTIVE_JOURNAL_TAINT_SLOTS`: expandido de 16 para **64 slots** na tabela de taint global (`driver/wdk/bc250_active_miniport.h`).
  * `active_miniport_test.c`: pool de memória de teste expandido de 8 MiB para 16 MiB para acomodar a estrutura expandida.
* **Cura de Histerese de Taint:**
  * Em `Bc250ActiveDrainGpuvaJournal`, quando todas as entradas de taint são aposentadas e `g_taintCount == 0`, o miniport agora reseta `active->JournalTaintAll = 0`, evitando que a perda temporária de tracking condene permanentemente todos os novos processos.
* **Limpeza no Teardown de Processo:**
  * Em `Bc250ActiveDestroyProcessObject`, se o processo for destruído pelo SO sem ter drenado completamente seus journals/taints, os slots pertencentes àquele `ProcessHandle` são explicitamente zerados e devolvidos ao pool livre.

### 2.2 VCN.1 — Sonda e Boot do VCN 2.0 (PS5 Linux)
* **Arquivos do Módulo:**
  * `driver/wdk/bc250_vcn_probe.h` e `driver/wdk/bc250_vcn_probe.c`.
  * `driver/tests/vcn_probe_contract_test.c` (integrado na suíte do `build-contract.bat`).
* **Microcódigo PS5:**
  * Arquivo: `firmware/ps5_vcn.bin` (404.544 bytes, SHA-256 `BBD15AB65E76178C2918E8224B6AB4AB1711F1B59D935EE131AB188A9712E596`).
  * Incluído no INF `bc250mcdm.inf` para cópia automática em `System32\drivers\ps5_vcn.bin`.
* **Sequência de Inicialização no Silício:**
  1. Mapeamento de MMIO BAR5 cobrindo os Segmentos 0 e 1 do UVD (offsets `0x1E000..0x22000`).
  2. Leitura passiva de `mmUVD_STATUS` (0x201BC), `mmUVD_POWER_STATUS` (0x1FB10) e `mmUVD_VCPU_CNTL` (0x20160).
  3. Carga do binário `ps5_vcn.bin` do disco e alocação do buffer contíguo de 2 MiB (prioritariamente na VRAM GDDR6, fallback na DRAM do sistema).
  4. Cópia do payload do firmware (404.288 bytes) pulando o header padrão de 256 bytes.
  5. Ungate de clock e reset canônico do PS5 (`UVD_SOFT_RESET = 0x3FFFF`, toggle em `UVD_SOFT_RESET2`, `UVD_SOFT_RESET = 0x8`).
  6. Configuração dos caches BAR da VCPU:
     * Cache 0: Firmware (tamanho alinhado a 64 KB).
     * Cache 1: Stack da VCPU (64 KB).
     * Cache 2: Contexto da VCPU (384 KB).
  7. Liberação da VCPU (`UVD_SOFT_RESET` bit 3 = 0) e habilitação do clock da CPU do VCN (`mmUVD_VCPU_CNTL |= 0x200`).
  8. Polling de prontidão do firmware monitorando `mmUVD_STATUS & 2` por até 1 segundo.
* **Governança e Fail-Closed:**
  * Controlado pela chave de registro `VcnProbe` (padrão 0 em produção; 1 para leitura de status, 2 para boot completo no lab).
  * A rotina sempre retorna `STATUS_SUCCESS` para o `StartDevice` mesmo se o firmware não responder no tempo estipulado, garantindo que o display e o sistema operacional não travem.

---

## 3. Rastreabilidade dos Binários (Pacote 3.450.1.0)

| Arquivo | Tamanho | SHA-256 |
| :--- | :--- | :--- |
| `bc250mcdm.sys` (lab) | 862.208 B | `0F74A48FDBB9FC9F2850016278D2707EFAC6F46B965A36604AB509CDF64D1245` |
| `bc250umd.dll` | 425.472 B | `AF95BB016EB129E2596B07BAEF26CA9FD1260DFCEC68BCD59BC0499586AB27D8` |
| `bc250mcdm.inf` | 9.380 B | `5719382A4727FBB4FEF74921EF990D2F84741ABC7FB88209885BCCBA8B7756D3` |
| `bc250mcdm.cat` | 19.380 B | `75C45E1DAE03951816B7610975F61E06EE0D1592157676F68FFE00B815C65239` |
| `ps5_vcn.bin` | 404.544 B | `BBD15AB65E76178C2918E8224B6AB4AB1711F1B59D935EE131AB188A9712E596` |

* **Portões contratuais WDK:** 275 portões verdes (EXIT 0).
* **Auditoria de segurança (`audit-bc250mcdm-safety.py`):** PASS.
* **Auditoria de pacote lab (`audit-bc250mcdm-lab-package.ps1`):** PASS.

---

## 4. Critério de Aceitação da Corrida 296

1. **Estabilidade de StartDevice:**
   * O driver deve subir o desktop normalmente sob D0 (sem tela preta nem bugcheck).
2. **Verificação do Bloco 1 (Journal GPUVA):**
   * Nenhum log de saturação `NO_SPACE` nas operações do DWM e da Pesquisa.
3. **Verificação da Sonda VCN (`[VCN_PROBE]`):**
   * Inspecionar o log do kernel procurando pelas entradas `[VCN_PROBE]`:
     * Registro do estado inicial do bloco UVD: `STATUS`, `POWER_STATUS`, `VCPU_CNTL`.
     * Registro da alocação de memória (VRAM física ou DRAM).
     * Registro da tentativa de boot: verificar se `mmUVD_STATUS` atingiu o bit 1 (`0x2`) indicando que a VCPU do PS5 subiu e está executando o microcódigo, ou qual valor permaneceu.
