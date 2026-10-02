# 552 — Coleta Linux com UMA de 256 MB: `amdgpu` ativo, sem trava sob paginação pesada, e o que isso muda no nosso driver

> **Data:** 01/10/2026, boot Linux às 14:52, coletas às 14:59:58, 15:00:52 e 15:01:08 (ensaio de 600 s, encerrado pelo operador às 15:09:38).
> **Material:** `docs/evidence/2026-10-01_15-01-LINUX-REFERENCIA-UMA256/` (entregue pelo Daniel em `linux/` na raiz e movido para cá) (kit `bc250-linux-referencia.sh` 2.1, pastas `bc250-amdgpu-20261001-145958`, `bc250-linux-20261001-150052` e `bc250-linux-20261001-150108`). Os 185 arquivos `(1)` são cópias **idênticas** (conferido com `cmp`); 382 arquivos lidos, cerca de 190 distintos.
> **Sistema:** Fedora 44 KDE Plasma, kernel 6.19.10, Mesa 26.0.3, BIOS P3.00.
> **Natureza:** referência de hardware com o driver oficial. **Não é corrida do nosso driver** e não valida nada nele.
> **Antecedente:** [doc 549](549_COLETA_LINUX_CMOS_APCB_CONTROLA_A_VRAM_E_AMDGPU_SEM_IP_DISCOVERY.md) (coleta com 2048 MB, em que o `amdgpu` não subiu).

---

## 1. Resumo

| # | Achado | Confiança | Impacto no nosso driver |
|---|---|---|---|
| 1 | **UMA de 256 MB aceita pelo firmware** (`ABL_SIGNATURE`, `UMA_SIZE=0256`, checksum `0x03F9`); a reserva caiu em `0x460000000–0x46FFFFFFF` | P0 | Fecha o risco B do doc 551 §3; a base prevista para a 199 bate |
| 2 | **O `amdgpu` subiu** e o desktop KDE (kwin, plasmashell, Dolphin, Konsole, ~35 clientes) rodou com aceleração real (radeonsi e RADV `gfx1013`) | P0 | O "não subiu" do doc 549 aconteceu com UMA de 2048; com 256 ele sobe (H1 do doc 549 fortalecida, ainda não provada) |
| 3 | **Nenhuma trava** em ~8,5 min com **149 despejos completos da VRAM + 49 da GTT** e o vkcube rodando; dmesg sem timeout de anel, sem reset de GPU, sem MCE | P0 | **Desfecho L-B** da Frente L: a placa aguenta paginação muito mais pesada que a nossa sem travar. A trava dura do Windows passa a apontar mais para o nosso driver (H forte, não prova; uma sessão de 8,5 min) |
| 4 | **Firmware idêntico** ao nosso (CE 0x25, ME 0x63, MEC 0x90, PFP 0x94, RLC 0xD, SDMA 0x34 feature 50) | P0 nos dois lados | Firmware **descartado** como diferença |
| 5 | **A RADV trata a BC-250 como APU:** heap "local" de 5,20 GiB e heap de sistema de 2,60 GiB, sobre 256 MB de VRAM física | P0 (vulkaninfo) + P1 (código) | **A nossa winsys não faz isso** (§3.1): candidata direta à causa das faltas de memória |
| 6 | Com a VRAM cheia (254,6 de 256 MB), o `amdgpu` pôs **534 MiB em GTT**, incluindo **338 MiB de objetos sem acesso de CPU** | P0 | Gabarito do M1b: até imagens "só GPU" vão para a memória do sistema |
| 7 | **(CORRIGIDO no [doc 556](556_W3_SMU_NAO_E_DIFERENCA_SDMA0_E_CLOCK_GATING_NO_RASTRO_CRITERIO_CORRIDA200.md) §1: quem gerencia é o firmware, nos dois sistemas; o `amdgpu` não manda mensagem de relógio no init e não havia governor)** O SMU gerencia relógios e tensões (sclk e VDDNB variando, potência 64–75 W) | P0 do que foi lido; o significado dos valores é H | No Windows não falamos com o SMU; diferença real de configuração |
| 8 | O `amdgpu` usa **SDMA0 e SDMA1** (fences 0x7A6 e 0x656) | P0 | Nós só usamos o SDMA0; não temos `cyan_skillfish2_sdma1.bin` no repositório |
| 9 | Fences por **interrupção** (MSI-X, 37.701 interrupções na sessão) | P0 | Nós aposentamos por polling/DPC/timer |
| 10 | Registro de reset da plataforma: **`0x40080400` = "software wrote 0x6 to 0xCF9" + "a parity error occurred"**, idêntico nos dois boots (13:05 e 14:52) | P0 o valor; significado P1 (decodificação do kernel) | Instrumento novo para a trava dura (§3.4) |
| 11 | Temperaturas: GPU (edge) até **79 °C**, CPU (Tctl) até **82,4 °C**, sem trava | P0 | Temperatura alta **sozinha** não trava a placa nessa janela; a térmica não sai da lista, mas perde força |
| 12 | IOMMU desligado; Resizable BAR suportado (256/512/1024 MB) com 256 atribuído; PCIe 16 GT/s x16 | P0 | Confirma o doc 496 |

---

## 2. Fatos medidos, por área

### 2.1 Memória (o que o `amdgpu` fez com 256 MB)

- **Inicialização:**
  - `VRAM: 256M 0x000000F400000000 - 0x000000F40FFFFFFF`, o mesmo domínio MC que o nosso driver usa (`VramMcStart=0xF400000000`).
  - `GART: 512M 0x0 - 0x1FFFFFFF`, com a tabela em `0xF40FE00000`.
  - `7724M of GTT memory ready`.
  - `reserve 0x400000 from 0xf40f800000 for PSP TMR`.
  - `Detected VRAM RAM=256M, BAR=256M`.
- **VRAM em uso:** 254.590.976 de 268.435.456 bytes (cheia). Buddy allocator com só 13 MiB livres.
- **GTT em uso:** 548.855.808 bytes no momento do inventário; máximo de 749 MB no monitor.
- **Distribuição dos objetos dos processos** (`amdgpu_gem_info`):

| Onde | Acesso de CPU | Objetos | MiB |
|---|---|---:|---:|
| GTT | sem CPU | 39 | 337,9 |
| GTT | CPU obrigatório | 207 | 180,3 |
| GTT | CPU permitido | 8 | 16,0 |
| VRAM | sem CPU | 64 | 129,7 |
| VRAM | CPU obrigatório | 30 | 62,0 |
| VRAM | CPU permitido | 26 | 52,0 |

- **Página de VM:** `vm size is 262144 GB, 4 levels, block size is 9-bit`. O `amdgpu` usa 4 níveis e 48 bits; nós usamos 2 níveis e 34 bits. Não é defeito, mas é diferença registrada.
- **Vulkan (RADV):** `memoryHeaps[0]` = 2,60 GiB sem flag (GTT) e `memoryHeaps[1]` = 5,20 GiB `DEVICE_LOCAL`; 11 tipos de memória. `VK_EXT_memory_budget` presente.

### 2.2 Estabilidade sob carga

- **Linha do tempo:**
  - vkcube por 120 s;
  - depois 300 s de **paginação forçada**: cada leitura de `amdgpu_evict_vram` despeja **toda** a VRAM para a GTT, isso 149 vezes, mais 49 despejos de GTT, com o vkcube rodando;
  - depois o operador abriu o menu duas vezes e as Configurações;
  - fim às 15:09:38, encerrado pelo operador.
- **"A MÁQUINA NÃO TRAVOU"** (`RESUMO_EXECUTIVO.txt`). O dmesg depois do boot tem só `Unsupported clock type`, efeito do próprio script lendo `pp_dpm_dcefclk`/`pcie`, mais avisos de journal/exFAT de um desligamento sujo anterior.
- **Fences no fim:** gfx 0x302E, sdma0 0x7A6, sdma1 0x656, kiq 0x3E; filas de compute paradas em 1. Nenhum reset registrado.
- **Na inicialização:** `Fence fallback timer expired on ring sdma0`, duas vezes. Até o `amdgpu` perdeu interrupções do SDMA0 no início; depois funcionou.

### 2.3 Energia e relógios (lidos do SMU)

- `pp_dpm_sclk`: níveis 1000 / **43 (atual)** / 2000 MHz; `pp_od_clk_voltage`: SCLK 1000–2000 MHz, VDDC 700–1129 mV, atual 918 mV.
- `pp_dpm_mclk` 450 MHz, `fclk` 450 MHz, `socclk` 1254 MHz, todos com nível único.
- `amdgpu_pm_info`: `SMC Feature Mask: 0xffffffffffffffff`; VDDGFX 918 mV; **VDDNB variando de 831 a 1224 mV**; potência do SoC (com a CPU) entre 64 e 75 W.
- **Clock gating:** quase todo desligado. Ligados: SDMA MGCG, BIF MGCG, HDP MGCG e IH CG.
- **Leitura honesta:** sclk de 43–100 MHz com o vkcube rodando não fecha com o relato da comunidade (1500 MHz fixos sem governor). Os valores foram **lidos** (P0); o que significam é H.

### 2.4 Display

- **DCN 2.0.1:** inicializou com avisos de HPD (`dal_irq_service_dummy_ack`, `Failed to clear hpd(rx)`), o que confirma o registro antigo "HPD quebrado na placa até no Linux".
- **DP-1 conectado** com a lista completa de modos (2560x1080 primeiro); DP-2 desconectado. O EDID existe via AUX, mas **o script não salvou o binário** (pasta `edid/` vazia).

### 2.5 Plataforma

- **CPU:** "AMD BC-250", 8 núcleos e 16 threads. RAM vista pelo Linux 15.819.208 kB. MTRR: só 0–2 GB write-back.
- **IOMMU desligado** (sysfs vazio). Esta coleta não instalou o pacote `lm_sensors`; as temperaturas vieram do hwmon.

---

## 3. O que isso muda no nosso driver

### 3.1 Causa candidata das faltas de memória: a RADV não sabe que é APU (P1 por leitura, H no hardware)

- **Na RADV** (`src/amd/common/ac_gpu_info.c:516`):

  ```c
  info->has_dedicated_vram = !(device_info->ids_flags & AMDGPU_IDS_FLAGS_FUSION);
  ```

- **Na nossa winsys** (`mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_winsys.c`, `radv_wddm2_fill_gpu_info`), **`dev.ids_flags` nunca é preenchido**. Fica 0, então `has_dedicated_vram = true`: a nossa RADV trata a BC-250 como **placa dedicada**.
- **Efeito**, pelo `radv_physical_device_init_mem_types` (`radv_physical_device.c:442`):
  - **Com APU (Linux):** a RADV soma GTT e VRAM visível e declara 2/3 como "local". Os apps alocam texturas "locais" à vontade e o kernel põe o excedente em GTT.
  - **Sem APU (nós):** ~~o heap "local" é só a VRAM (~251 MB)~~. **CORRIGIDO em 01/10 ([doc 554](554_M1B1_RADV_NO_SEGMENTO_2_DORMENTE_E_CORRECAO_DO_HEAP.md) §1), errei aqui.** A mesma função da winsys fixa `vram_vis_size` em **16 GB** para a BC-250. O heap local da RADV é **16 GB fictícios** (P1, leitura; o log da corrida só diz "2 heaps"), e quem recusa é o KMD, que só tem ~245 MB no segmento 1. Os 1.124 `vkAllocateMemory -2` (doc 545) continuam compatíveis com essa leitura, mas pelo motivo inverso: a RADV promete demais, não de menos.
- **Correção candidata, uma variável:** a winsys preencher `ids_flags |= AMDGPU_IDS_FLAGS_FUSION`.
- **Ela só funciona junto com o KMD aceitando o segmento 2** para as alocações "locais" da RADV. Sem isso, o VidMm não tem onde pôr o excedente. É o passo 1 do M1b (doc 551 §2.3), que agora tem gabarito e mecanismo nomeados.

### 3.2 A trava dura: placa ou driver?

- **Desfecho L-B, P0:** o `amdgpu` aguentou 149 despejos completos da VRAM sem travar, a 79 °C.
- Nas nossas corridas, as travas vêm em 1 a 4 minutos com bem menos paginação, sempre num FILL/TRANSFER de paginação do SDMA.
- **Leitura (H forte):** a causa provável é algo que o `amdgpu` configura ou faz e nós não. Os candidatos que a coleta nomeia:
  1. **SMU:** o `amdgpu` gerencia relógios e tensões (VDDNB oscila); nós deixamos a placa no estado de fábrica;
  2. **SDMA1:** inicializado e usado pelo `amdgpu`; nós não sabemos em que estado ele fica;
  3. **Interrupções:** o `amdgpu` aposenta por interrupção; nós, por polling e leitura de fence;
  4. **Clock gating / HDP / MMHUB:** o W3 (diferença por leitura de `sdma_v5_0.c`, `gmc_v10_0.c`, `mmhub_v2_0.c`, `cyan_skillfish_ppt.c`) ganha prioridade.
- **Ressalva:** uma sessão de 8,5 min não prova estabilidade (Lei 1). O W1 (plataforma sozinha, no Windows com o BasicDisplay) continua valendo como segundo controle.

### 3.3 Firmware

As seis versões são idênticas às nossas, então firmware sai da lista de suspeitos. Fica a lacuna do **SDMA1**: não temos o binário e não o inicializamos. Saber em que estado ele fica no Windows é H.

### 3.4 Instrumento novo: o registro de motivo de reset da plataforma

- **Valor (P0):** `0x40080400`, igual nos dois boots, com os bits 19 (escrita de 0x6 em 0xCF9, reboot por software), 30 ("parity error") e 10 (sem nome).
- **Significado:** decodificado pelo kernel (P1). Como o valor se repete, o bit de paridade parece persistir até um corte total de energia; **não dá para ligá-lo a uma trava específica** (H).
- **CORRIGIDO em 01/10 pela Corrida 199 ([doc 555](555_CORRIDA199_256MB_SOBE_TRAVA_SEM_CALOR_SEM_ERRO_DE_FABRIC.md) §2.2):** os bits 30 e 10 **nao limpam** por escrita (medido duas vezes no Windows). Entao o `0x40080400` daqui NAO prova evento novo; so' o bit 19 entra e sai.
- **Proposta (Fase 1 do plano):**
  - ler esse registro **no Windows**, só leitura, a cada boot, pela ferramenta da CMOS;
  - o endereço vem do kernel Linux (`FCH_PM_BASE` + `S5_RESET_STATUS`) e precisa ser conferido na fonte antes do uso (Lei 7);
  - procedimento: um corte de energia total para zerar, depois uma trava, depois a leitura. Isso diz se a trava dura gera "parity error" ou "sync flood".

---

## 4. Pendências da Frente L

1. Salvar o **EDID binário** do DP-1 (`/sys/class/drm/card1-DP-1/edid`); o script de hoje não grava.
2. Instalar o pacote `lm_sensors`; o modelo de leitura de `junction` e `mem` veio vazio.
3. Uma sessão mais longa (30–60 min) com a mesma carga, para pesar a ausência de trava.
4. Repetir o boot com UMA de 2048 só com `dmesg | grep amdgpu`, para fechar a H1 do doc 549 (IP discovery × tamanho da UMA). Opcional: a decisão de usar 256 já não depende disso.

**Nada deste documento foi validado no driver Windows. NÃO VALIDADO EM HARDWARE.**
