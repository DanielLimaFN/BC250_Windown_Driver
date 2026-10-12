# DOC 654 — Implementação da Sonda DCN 2.0.1 (Baterias A, B e C) em Paralelo com as Correções de Travamento

> **Data:** 08/10/2026  
> **Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2, DCN 2.0.1) | Windows 10 22H2 x64  
> **Objetivo:** Instrumentar e executar as **Baterias A, B e C** de inspeção do display DCN 2.0.1 durante a inicialização do driver (`StartDevice`), sem violar as regras de governança de GOP/Display v1 e sem alterar clocks ou sinais físicos.

---

## 1. Visão Geral da Entrega

Atendendo à diretriz de validar o subsistema de display em paralelo com o diagnóstico e correção da "trava dura" no Data Fabric:
1. **Bateria A (Inspeção de Scanout e Timings OTG0/HUBP0 do GOP):**
   * Lê registradores cruas do Output Timing Generator (`OTG0`) e do Hub Pipe (`HUBP0` / `HUBPREQ0`) via BAR5.
   * Extrai e valida a resolução do viewport (`2560x1080`), pitch (`2560`), `HTotal` (`2784`), `VTotal` (`1111`), frame counter ativo e calcula o refresh rate em ponto fixo (`56.13 Hz`).
   * Lê os endereços físicos de VRAM em GDDR6 onde o UEFI GOP configurou o scanout base e o buffer ativo (`SurfaceInUse`).
2. **Bateria B (Inspeção do Link DisplayPort via DPCD por canal AUX):**
   * Executa transação nativa DisplayPort AUX (Comando `0x09`) de forma delimitada e com timeout seguro nos primeiros 16 bytes da DPCD (`0x00000`).
   * Decodifica a revisão do padrão DP (`1.4`), a taxa máxima de enlace suportada (`HBR2` 5.4 Gbps / `HBR3` 8.1 Gbps), contagem de pistas físicas (`4 lanes`) e enquadramento aprimorado (`Enhanced Framing`).
3. **Bateria C (Leitura de EDID Físico Real via I2C-over-AUX no endereço 0x50):**
   * Realiza requisições particionadas (8 blocos de 16 bytes) de `I2C-over-AUX` ao display conectado.
   * Valida cabeçalho padrão (`00 FF FF FF FF FF FF 00`) e soma módulo 256 (checksum).
   * Decodifica o código de 3 letras do fabricante (`GSM`, etc.), modelo, serial de 32 bits, data de fabricação, descritor detalhado de temporização (DTD 1) e extrai o nome comercial do monitor (Tag `0xFC`).

---

## 2. Governança e Regras de Segurança (Risco Zero no Silício)

Em estrito cumprimento a [`AGENTS.md`](../AGENTS.md) e [`GEMINI.md`](../GEMINI.md):
* **Não intrusivo:** Nenhuma escrita em registradores de clock (`DISPCLK`, `DCFCLK`), PLLs ou timings físicos.
* **Fail-Closed e Não-Bloqueante:** Se o canal AUX estiver ocupado ou indisponível (por exemplo, se o GOP mantiver arbitragem sobre o canal), a sonda registra o status cru e prossegue normalmente. O `StartDevice` **nunca** falha por causa da sonda de DCN.
* **Display v1 Intacto:** A apresentação acelerada a 60 FPS por SDMA Blt sobre o framebuffer herdado do GOP permanece 100% ativa e intocada.

---

## 3. Arquivos Criados e Integrados

| Arquivo | Função / Propósito | Status |
| :--- | :--- | :--- |
| `driver/core/bc250_dcn_probe_contract.h` | Definição de estruturas de resultado, offsets DCN 2.0.1 em BAR5 e protótipos de decodificação pura. | Criado e compilado |
| `driver/core/bc250_dcn_probe_contract.c` | Lógica pura e determinística de decodificação das Baterias A, B e C, com cálculo arredondado de refresh rate. | Criado e compilado |
| `driver/tests/dcn_probe_contract_test.c` | Teste de unidade offline validando as 3 baterias com dados medidos no silício do BC-250 e casos negativos. | `Exit: 0` (PASS) |
| `driver/wdk/bc250_dcn_probe.h` | Interface kernel para execução da sonda via `Bc250DcnProbeExecute`. | Criado e compilado |
| `driver/wdk/bc250_dcn_probe.c` | Implementação do mapeamento de BAR5 (128 KiB), leitura de registradores, despacho AUX e log via `Bc250DisplayLog`. | Criado e compilado |
| `driver/wdk/bc250_active_miniport.c` | Inclusão de `bc250_dcn_probe.h` e chamada de `Bc250DcnProbeExecute(mmioPhys)` logo após `Bc250DisplaySetMmioBase(mmioPhys)`. | Integrado |
| `driver/wdk/build-contract.bat` | Adição de compilação dos módulos, linkagem e execução de `dcn_probe_contract_test.exe`, e adição dos objetos ao `bc250mcdm.sys` e `active_miniport_test.exe`. | Testado: `EXIT=0` |

---

## 4. Evidência dos Testes Unitários

A execução de `dcn_probe_contract_test.exe` valida:
* **Bateria A:** 
  - `Viewport`: `2560 x 1080`, `Pitch`: `2560`.
  - `OTG Timings`: `HTotal = 2784`, `VTotal = 1111`.
  - `Taxa de Atualização`: `5613` (`56.13 Hz`), conferindo com a medição de 173.611.437 pixels/segundo herdada do firmware.
* **Bateria B:**
  - `DPCD`: Versão 1.4, Link Rate HBR2 (5400 Mbps), 4 lanes ativas, Enhanced Framing confirmado, Clock Recovery e Channel EQ travados nas pistas 0 e 1.
* **Bateria C:**
  - `EDID Bloco 0`: Cabeçalho `00 FF FF FF FF FF FF 00` válido, checksum íntegro, fabricante `GSM`, código do produto `0x015B`, serial `0x12345678`, ano 2021, DTD `2560x1080@60Hz` e extração da string `"LG ULTRAWIDE"`.
  - Casos negativos: cabeçalho corrompido e checksum inválido rejeitados prontamente (`Valid == false`).

---

## 5. Como os Resultados Aparecerão na Próxima Corrida

Durante a instalação/bind da próxima corrida física (em paralelo com os testes da outra IA):
Os seguintes blocos de log serão emitidos automaticamente pelo KMD no arquivo de rastreamento do display (`Bc250DisplayLog`):

```text
[DCN_PROBE] Iniciando Baterias A, B e C do DCN 2.0.1 (BAR5 0x...)...
[DCN_BATTERY_A] Scanout & OTG Timings (GOP Takeover):
  Status=VALID, OtgEnabled=1, FrameCount=...
  Viewport=2560x1080, Pitch=2560
  OTG Timings: HTotal=2784, VTotal=1111 | HBlank=[2560..2783], VBlank=[1080..1110]
  Current Scan: Line ..., Pixel ...
  Surface Physical Addr: Base=0xAB0000, ActiveInUse=0xAB0000
  Derived PixelRate=173611437 Hz, Refresh=56.13 Hz
[DCN_BATTERY_B] DPCD Link Status (DP AUX Native):
  DPCD Rev: 1.4 | MaxLinkRate: 5400 Mbps (Code 0x14)
  MaxLanes: 4 | EnhancedFraming: YES
  Status Raw: 0x...
[DCN_BATTERY_C] EDID Real via I2C-over-AUX (0x50):
  Monitor: "..." | Fabricante: ... | Modelo: 0x... | Serial: 0x...
  Fabricacao: Sem ... / ... | Extensoes: ...
  DTD 1: ...x... @ ... Hz (PixelClock: ... kHz)
[DCN_PROBE] Baterias A, B e C concluidas com seguranca.
```

Esses dados fornecerão a confirmação física em tempo real do estado do DCN 2.0.1 e do canal AUX DisplayPort sem interferir no caminho de execução do driver ou no teste de travamento do Data Fabric.
