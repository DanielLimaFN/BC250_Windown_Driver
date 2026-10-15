# 662 — L1: Linux com `amdgpu.dc=0`, 2 × 600 s sem trava (desfecho A, com ressalva de memória)

**Data:** 08/10/2026, sessões às 16:17 e 16:36, coleta às 16:47.
**Evidência:** `referencias/bc250-l1/`, com `auto.log`, `RESULTADO.txt` e as pastas `bc250-l1-20261008-161708-L1_DC0-s1` e `bc250-l1-20261008-163606-L1_DC0-s2`.
**Sistema:** Fedora 44, kernel 6.19.10, Mesa 26.0.3 (radeonsi e RADV gfx1013), weston 15, UMA de 256 MB lida na CMOS.
**Natureza:** referência fora do nosso driver; não conta como corrida. Critério escrito antes no doc 661 §4.

## 1. Pré-condição (P0, as duas sessões)

| Item | s1 | s2 |
|---|---|---|
| `dc_lido` | 0 | 0 |
| Conectores DRM | nenhum (`card1` e `renderD128` sem `card1-*`): sem display, como previsto | idem |
| Weston | `Using GL renderer`, `radeonsi gfx1013` | idem |
| `CARGA_NA_GPU` | OK (fence do GFX 0x3D → 0x1E97E em 10 s) | OK |
| Despejos de VRAM / GTT | 197 / 65 | 200 / 66 |
| Fence do GFX parada entre amostras | 0 de 301 | 0 de 301 |
| Firmware | o mesmo do Windows (ME 0x63, PFP 0x94, CE 0x25, RLC 0xD, MEC 0x90) | idem |

A pré-condição foi cumprida nas duas sessões.

## 2. Resultado (P0)

| Medida | s1 | s2 |
|---|---|---|
| Duração | 615 s, terminou e desligou sozinha | 615 s, idem |
| Fences do GFX | 6.655.453 (~10.800/s) | 6.660.163 (~10.800/s) |
| Fences do SDMA0 / SDMA1 | 5,57 M / 253 k | 5,10 M / 265 k |
| dmesg | só 2× `Fence fallback timer expired on ring sdma0` na inicialização (igual ao doc 552); nenhum timeout de anel, reset de GPU, page fault ou MCE | idem |
| VRAM usada | mediana 22 MB, máximo 246 MB (1 de 301 amostras acima de 200 MB) | mediana 22, máximo 243 |
| GTT usada | mediana 71 MB, máximo 141 | mediana 72, máximo 137 |
| Temperatura de borda da GPU | mediana 94 °C, **máximo 103 °C** | mediana 94, **máximo 101** |
| Tctl da CPU | mediana 97 °C, **máximo 102,4 °C** | mediana 97,2, **máximo 102,1** |

**Desfecho A** do doc 661: as duas sessões terminaram sem trava e sem erro do `amdgpu`.

## 3. DCN durante o ensaio (P0, só leitura pelo `amdgpu_regs`)

- `OTG0_CONTROL=0x80011311` e `OTG0_STATUS=0x00020002` não mudaram do começo ao fim.
- `HUBP0` com a superfície em `0x4_60000000` (PA do offset 0 da UMA) e `AUX0_CONTROL=0x01040000`: os mesmos valores que a nossa sonda leu no Windows (doc 660).
- O contador de quadros do OTG0 andou o tempo todo: 1.507 → 38.501 na s1 e 1.503 → 38.447 na s2.
- **O `amdgpu` não reservou a superfície do GOP.** O `amdgpu_vram_mm` mostra `reserved:` vazio e 240 MB livres no início. Logo, durante 20 min, o OTG varreu memória que o Linux deu a outros objetos, sem travar.

**Achado lateral (P2):** o contador de quadros mede **60,15 e 60,07 Hz** contra o relógio (épocas de 1 s em 615 s, ±0,2%). A nossa sonda e o EDID sintetizado derivam **56,13 Hz** de `HTotal`/`VTotal` com um pixel clock assumido (173,6 MHz). Com 2784 × 1111 a 60 Hz, o pixel clock seria ~185,6 MHz.
- Não muda nada na trava.
- Indica que o nosso pixel clock derivado está errado, e isso pesa no período de VSync que declaramos ao WDDM.
- A conferir no Windows contando quadros da própria sonda contra o QPC antes de mudar qualquer coisa.

## 4. Veredito e implicação

**Herdar o DCN do GOP não basta para travar a placa** (P0, duas sessões). Valia também com o scanout lendo memória reutilizada. O DCN sai da frente como causa suficiente. A causa da trava dura está no que o nosso driver faz de diferente do `amdgpu` fora do DCN.

**Ressalvas que mandam na força da conclusão:**
1. **Pressão de memória baixa.** A VRAM ficou em ~22 MB na mediana: a carga é pequena (weston + vkcube + glmark2 800×600) e o despejo a cada 2 s esvazia a VRAM. As travas no Windows vêm com a VRAM quase cheia (~228 de 233 MB na 250) e em rajadas de FILL de tela cheia. O doc 552 (dc=1, KDE) teve a VRAM cheia e também não travou, mas **dc=0 com a VRAM cheia não foi medido**.
2. **Calor:** 101–103 °C de borda e ~102 °C de Tctl, mais quente que qualquer corrida do Windows (~80 °C, doc 555) e sem trava. A térmica perde mais força como causa (P0 de que essa temperatura, por 20 min, não trava com o `amdgpu`).
3. **Volume:** ~10.800 fences/s do GFX e ~8.000–9.000/s do SDMA0, contra ~40–80 IBs/s nossos. A placa aguenta duas ordens de grandeza mais submissões com o `amdgpu`.
4. 2 × 10 min não provam estabilidade (Lei 1).

## 5. Próximo (proposta, decisão do Daniel)

- **L1b (opcional, barata, fora do driver):** a mesma L1 com a VRAM cheia, para fechar a ressalva 1. Por exemplo, glmark2 em tela cheia 2560×1080 em várias instâncias, ou um cliente que aloque ~200 MB de textura. Se travar, a combinação "DCN herdado + VRAM cheia" volta à lista; se não travar, o DCN sai de vez.
- **No driver:** com o DCN fora, seguir o inventário de paridade do doc 593 §4 pelo que sobrou (VM de 4 níveis/P5, MEC fora do StartDevice, ordem de inicialização, estado de MGCG por bloco).

**NÃO VALIDADO EM HARDWARE:** a causa da trava dura do nosso driver; dc=0 com a VRAM cheia; o refresh real de 60 Hz no Windows.
