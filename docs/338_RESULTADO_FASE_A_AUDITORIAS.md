# Resultado da Fase A (offline) — validação das auditorias GPT 6 e Gemini

> **22/09/2026.** Execução da Fase A do [docs/337](337_PLANO_VALIDACAO_AUDITORIAS_GPT6_GEMINI.md).
> Nenhuma corrida, nenhuma mudança de driver. Ferramenta nova: `tools/decode-gfx-ib-regs.py`
> (decodifica os registradores escritos num despejo de IB usando `gfx10.json`/`gfx103.json` do
> próprio Mesa do fork). Evidência usada: `docs/evidence/2026-09-22_16-53-51-...` (Corrida 71).

---

## 0. Achado principal — NÃO estava em nenhuma das duas auditorias

**O RADV trata o BC-250 como GFX10.3 (RDNA2), não como GFX10.1.**

- **P1 (leitura):** o winsys `wddm2` monta `drm_amdgpu_info_hw_ip` com `10.1`, mas **não preenche
  `ip_discovery_version`** (`radv_wddm2_winsys.c:401-414`). Sem esse campo, `ac_fill_hw_ip_info`
  aplica a "correção de versão do kernel" (`ac_gpu_info.c:475-485`): família NV que não seja
  NAVI10/12/14 recebe `ver_minor = 3`. O GFX1013 (`external_rev 0x82`) cai aí →
  `gfx_level = GFX10_3` (`ac_gpu_info.c:725`).
- **P0 (hardware):** os IBs executados na Corrida 71 escrevem três registradores que **só existem no
  GFX10.3**: `DB_VRS_OVERRIDE_CNTL`, `PA_CL_VRS_CNTL` e `SX_PS_DOWNCONVERT_CONTROL=0xFF`.
- **P1 (consequência):** o Mesa liga contornos de defeitos de hardware do GFX10.1 só com
  `gfx_level == GFX10` — `has_ngg_fully_culled_bug` (`ac_gpu_info.c:386`),
  `has_vgt_flush_ngg_legacy_bug`, `has_zero_index_buffer_bug`, `has_prim_restart_sync_bug`,
  `has_two_planes_iterate256_bug`, `has_htile_stencil_mipmap_bug`. **Nenhum é aplicado.** O
  compilador ACO também gera para GFX10.3.
- **Corroboração externa (P1):** o patch da comunidade Linux para o BC-250
  (`referencias/bc250-gfx1013-fix/patches/mesa/0001`, "mirrors upstream !43465") corrige
  exatamente isto: adiciona GFX1013 à lista NAVI10/12/14 para ficar em `ver_minor = 1`. O mesmo
  repositório mostra RADV renderizando jogos neste silício sob o amdgpu.
- **H (efeito):** explica, num só defeito, "vértices certos e zero fragmento" (NGG/estado de
  rasterização programados para outra geração) **e** o TDR recorrente de "240 ondas presas"
  (defeito de NGG totalmente descartado do GFX10.1 sem contorno). Não está provado que é a causa.

## 1. Tabela da Fase A

| ID | Afirmação | Resultado | Classe |
|---|---|---|---|
| A1 | Winsys fabrica topologia (GPT) | **Confirmada:** SE=2, **SA/SE=1**, RB=8, máscara 0xFF, 6 WGPs por SE, `GB_ADDR_CONFIG=0x44` fixos no código | P1 |
| A2 | Silício tem 2 SA/SE (GPT, captura Fedora) | **Parcial:** `docs/10` registra `SE 2, SH per SE 2, CU per SH 10, active_cu_number 24`, mas o diretório bruto citado **não está no repositório** — a captura não é reverificável aqui | P2 |
| A3 | Mesa usa os valores do winsys sem reconstruir (GPT) | **Não conferido em detalhe** (secundário diante do §0) | — |
| A4 | Estado efetivo do draw | **Feito.** `PA_SC_GENERIC_SCISSOR_TL/BR` **não são escritos** pelo IB (dependem do `CLEAR_STATE`); screen/window/vport scissor = 2560x1080; `CB_TARGET_MASK=0xF`, `CB_SHADER_MASK=0xF`; `CB_COLOR0_INFO` = 8888 com troca BGRA; `ATTRIB2` = 2560x1080; `DB_DEPTH_CONTROL` com Z desligado (ALWAYS); `SPI_SHADER_COL_FORMAT=4`; 3 registradores só-GFX10.3 escritos | P0 |
| A5 | CSB nunca carregado no KMD (Gemini) | **Confirmada a ausência:** nenhum código do KMD escreve `RLC_CSIB_ADDR_LO/HI/LENGTH` (offsets 0x4CA2–0x4CA4, **BASE_IDX 1** → bytes `0x3B288/0x3B28C/0x3B290`). **Correção:** o `0x13288` do Gemini ignora o `BASE_IDX` e está errado; eu o havia registrado como "bate" nesta mesma tabela. Se o `CLEAR_STATE` produz scissor genérico zerado **não está provado** — o valor efetivo não é lido | P1 (ausência) / H (efeito) |
| A6 | Init do GFX10 no upstream que não fazemos | **Lista concreta** (upstream `gfx_v10_0.c`, sha256 `3B2013DC...`): golden registers, cleaner shader, GRBM CAM remapping, `constants_init` (`GRBM_CNTL.READ_TIMEOUT`, `setup_rb`, tile steering, `DB_RING_CONTROL`, `SH_MEM_CONFIG/BASES` por VMID, VMIDs de compute/GDS), `init_csb`. **Nada disso aparece no KMD** (golden só do SDMA) | P1 |
| A7 | `setup_rb` programa `PA_SC_RASTER_CONFIG` no GFX10 (Gemini) | **Refutada:** o `gfx_v10_0.c` upstream não tem nenhuma referência a `RASTER_CONFIG` | P1 |
| A8 | Flush do CB antes da cópia (Gemini) | **Não conferido** nesta passada | — |
| A9 | Primeiro uso `UNDEFINED` com pass `LOAD` (GPT) | **Confirmada:** imagem nasce `UNDEFINED`; o alvo do DWM não recebe clear nem transição antes do 1º draw, que abre o pass `LOAD` (`initialLayout=COLOR_ATTACHMENT_OPTIMAL`). Uso inválido; efeito no silício H | P1 |
| A10 | Depth test sem attachment (GPT) | **Não se aplica ao draw medido:** `Z_ENABLE=0` no IB | P0 |
| A11 | `-5` é consequência do TDR (ambos) | **Confirmada:** `Lock2 failed: status=0xC00002B6` (`STATUS_DEVICE_REMOVED`) após o TDR | P0 |
| A12 | PFP parado em `0x42` (Gemini) | **Corrigida:** `0x58 ACQUIRE_MEM` em 4 de 6 TDRs das corridas 68–71; `0x42 PFP_SYNC_ME` só na 71; um caso com PFP fora de pacote | P0 |
| A13 | Viewport negativo inverte área no culling NGG (Gemini) | **Refutado como mecanismo:** a Corrida 70 (altura positiva) também saiu preta | P0 |
| A14 | Offsets citados | `PA_SC_GENERIC_SCISSOR_*` (0x28240/0x28244) conferem; `RLC_CSIB_*` do Gemini **não** conferem — faltou o `BASE_IDX 1` (o certo é 0x3B288/0x3B28C/0x3B290) | P1 |

## 2. O que muda no plano (Fase B/C do docs/337)

A ordem "corrigir primeiro o que é **fato fabricado**" passa a ter três itens, e o de maior
alcance é o nível de GFX:

1. **Identidade do chip:** o winsys passar a entregar `ip_discovery_version = 10.1.3` (ou o fork
   adotar o mesmo ajuste do patch da comunidade) → `gfx_level = GFX10`. **Uma variável.** Muda a
   geração que o RADV programa, liga os contornos de defeitos do GFX10.1 e faz o ACO gerar
   GFX10.1.
2. **Topologia** (SA/SE, máscaras de CU/RB) medida em vez de fixa — **depois** de 1, porque o valor
   certo depende de leitura no hardware (Fase B).
3. CSB/scissor genérico, primeiro uso `UNDEFINED`, goldens por subconjunto — como no docs/337.

O controle positivo do instrumento (Fase B1) continua obrigatório e pode ir na mesma corrida do
item 1, por ser só medição.

**Proposta para a próxima corrida:** candidato com (a) item 1 como única variável comportamental,
(b) autoteste Vulkan de controle positivo no harness e (c) snapshot somente leitura de
`RLC_CSIB_*`, `GB_ADDR_CONFIG` e dos 34 goldens. Critério escrito antes.
