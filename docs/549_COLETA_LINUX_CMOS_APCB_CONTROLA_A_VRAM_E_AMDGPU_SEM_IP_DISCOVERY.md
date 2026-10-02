# 549 — Coleta Linux de 01/10: a CMOS APCB controla a VRAM, e o `amdgpu` falhou no IP discovery

> **Data:** 01/10/2026, boot Linux às 13:05:29, coletas às 13:06:54 e 13:07:59.
> **Material:** `referencias/linux/` (kit `bc250-linux-referencia.sh` 2.0, `bc250_memcfg.cpp`, `histo.txt`, duas pastas `bc250-linux-20261001-*`).
> **Sistema:** Fedora 44, kernel `6.19.10-300.fc44.x86_64`, Mesa 26.0.3, BIOS `P3.00 12/09/2021`.
> **Natureza:** coleta física no Linux. Não é corrida do driver Windows, e nada aqui valida o nosso driver.

---

## 1. Resumo

| # | Achado | Confiança |
|---|---|---|
| 1 | A CMOS APCB tinha `UMA_SIZE=0512 MB` enquanto o menu da BIOS dizia `UMA_SPECIFIED` / 8G. É exatamente o que o nosso driver leu nas Corridas 194–196. | **P0** |
| 2 | Depois de gravar `UMA_SIZE=2048` na CMOS, o firmware reservou **exatamente 2 GiB** no topo da RAM. | **P0** |
| 3 | O menu da BIOS **não** controla a UMA nesta placa; quem controla é o bloco APCB da CMOS. Isso fecha a H das §6–§8 do doc 548. | **P0** (dois estados medidos) |
| 4 | O `amdgpu` **não inicializou**: `get invalid ip discovery binary signature` → `Fatal error during GPU init` → probe `-22`. O desktop rodou em `llvmpipe`. | **P0** |
| 5 | Por causa do item 4, a coleta **não** serve como gabarito de aceleração: sem EDID, PowerPlay, firmware, sensores de GPU nem medição de trava sob carga. | **P0** |

---

## 2. A CMOS APCB

**Leitura antes (`--ler-vram`, P0):**
- assinatura `0x4C424124` (`$ABL`), checksum `0x03FA`;
- `UMA_SIZE` é um `WORD` em `0xAA`, em MB (`bc250_memcfg.cpp:45`): bytes `00 02` = **0x0200 = 512 MB**.

**Gravação (`--set-vram 2048`) e releitura (P0):**
- bytes em `0xAA` passaram a `00 08` = **0x0800 = 2048 MB**;
- checksum `0x03FA` → `0x0400` (+6, a mesma diferença do byte alterado: `0x08 − 0x02`);
- timings de memória intactos (ClockSpeed 1750, tCL 24 etc.).

**Efeito no boot seguinte (P0, `/proc/iomem`):**
- `100000000-3efffffff : System RAM`;
- `3f0000000-46fffffff : Reserved` → `0x80000000` = **2 GiB** no topo, coerente com a UMA;
- `MemTotal: 14.012.872 kB` (≈13,36 GiB), contra os ~15,2 GB que o Windows via com 512 MB.

**Consequência para o nosso driver:**
- com 512 MB a VRAM ficava em `[0x450000000, 0x470000000)`, que é a base que o driver mediu (`[M2_BASE] ... 0x450000000`);
- com 2048 MB ela passa a `[0x3F0000000, 0x470000000)`: **a base física muda para `0x3F0000000`**. É o caso que o M2 dinâmico existe para atender. A base lida da placa já é usada nos caminhos principais (`Caps->VramMcBase`, MMU global, dummy page); `BC250_GCVM_VRAM_MC_BASE_FALLBACK` só entra quando a leitura dá zero.
- O BAR continua 256 MiB (`BAR 0 [mem 0xc0000000-0xcfffffff 64bit pref]`, P0), logo a parte visível à CPU não cresce.

**Correção de registro:** as trocas de valor na BIOS feitas pelo Daniel nas Corridas 194–196 ("3 GB", "8G") **nunca chegaram à placa**. A leitura de 512 MiB do driver estava certa nas três. O "teste cego" do valor da BIOS, como foi montado, não testava nada; o valor real vem da CMOS.

---

## 3. O `amdgpu` não subiu

**Medido (dmesg do boot, P0):**
```
amdgpu 0000:01:00.0: amdgpu: initializing kernel modesetting (CYAN_SKILLFISH 0x1002:0x13FE 0x1022:0x0000 0x00).
amdgpu 0000:01:00.0: amdgpu: get invalid ip discovery binary signature
amdgpu 0000:01:00.0: amdgpu: Fatal error during GPU init
amdgpu 0000:01:00.0: probe with driver amdgpu failed with error -22
```
- `/proc/cmdline` **sem** `nomodeset`; parâmetros `discovery=-1` (auto), `ip_block_mask=0xFFFFFFFF`;
- o vídeo ficou no `simpledrm` (framebuffer do GOP, 2560x1080);
- OpenGL e Vulkan = `llvmpipe`; o `vkcube` rodou 120 s na CPU (CPU chegou a 89,1 °C) e não mede nada da GPU.

**Hipóteses (H, não testadas):**
1. A tabela de IP discovery fica no topo da VRAM. Com a UMA mudada de 512 para 2048 MB, o firmware pode não tê-la reescrito onde o `amdgpu` procura. **Não sabemos se o mesmo kernel sobe com 512 MB**, porque só este boot foi capturado.
2. Regressão do kernel 6.19 para a Cyan Skillfish.

**Teste que separa as duas:** voltar `UMA_SIZE=512` na CMOS, desligar da tomada, e repetir só `dmesg | grep amdgpu`.
- Se subir com 512 e falhar com 2048, a causa é o tamanho (H1). Isso também seria um aviso para o nosso driver: o firmware da placa pode não suportar bem UMA maior.
- Se falhar com 512 também, é o kernel (H2). Nesse caso, usar o Fedora 43 Live que o próprio `README.md` do kit recomenda.

**O que ficou vazio por causa disso:** as pastas `edid/`, `firmware/` e `powerplay/` e os sensores de GPU. O DisplayPort aparece como `connected` com o modo `2560x1080`, mas via `simpledrm`, sem o AUX lido.

---

## 4. Impacto na Corrida 197 (adendo ao doc 548 §9)

A CMOS **ficou em 2048 MB**. Se o Windows subir assim, a 197 roda num cenário de VRAM diferente das 194–196:

| Grandeza | Corridas 194–196 | Esperado na 197 |
|---|---|---|
| `VramTotalMiB` | 512 | **2048** |
| `CpuVisibleMiB` | 256 | 256 |
| Base física da VRAM | `0x450000000` | **`0x3F0000000`** (`[M2_BASE] ... DIFERENTE -- usando a lida`) |
| Segmento 3 | 240 MiB | **≈1776 MiB** (2048 − 256 − 16 de guarda), `[0x10000000, +0x6F000000)` |
| RAM no Windows | 15,227 GB | ≈13,3–13,7 GB |

**Isso mistura duas variáveis numa corrida só** (a abertura no `Lock2` e a mudança de base e tamanho).

**Decisão do Daniel (01/10): rodar com 2048; a CMOS não volta para 512.** O critério com as duas variáveis, os valores esperados e a bissecção de contingência (2048 com o segmento 3 não declarado) estão no [doc 548 §9.1](548_M2_SEGMENTO3_VRAM_ALEM_DO_BAR_DINAMICO_E_CRITERIO_CORRIDA194.md).

Também foi pedido o mesmo controle de VRAM no Windows. Está feito em `tools/vram-cmos/` ([doc 550](550_CONTROLE_DA_VRAM_PELA_CMOS_NO_WINDOWS.md)), com teste offline sobre os dumps desta coleta.

A leitura do valor na abertura do relato continua: eu informo o `VramTotalMiB` lido.

---

## 5. Pendências

- ~~Decidir o UMA da 197 (§4).~~ Decidido: 2048.
- Rodar `tools\vram-cmos\bc250-vram.ps1 -Ler` (teste V1 do doc 550) antes da 197.
- Repetir o boot Linux para separar H1 de H2 (§3). Só faz sentido para usar o Linux como gabarito (Frente L); não bloqueia a 197.
- Nada deste documento foi validado no driver Windows. **NÃO VALIDADO EM HARDWARE.**
