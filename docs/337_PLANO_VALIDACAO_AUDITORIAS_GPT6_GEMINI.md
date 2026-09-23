# Plano de validação das auditorias externas (GPT 6 e Gemini) — pós-Corrida 71

> **22/09/2026.** Plano apenas; nada foi implementado. Regra: nenhuma afirmação das auditorias
> entra na BASE como fato antes de passar por este plano. Toda afirmação é tratada como **H** até
> ser confirmada por leitura local (P1) ou medição (P0).

---

## 0. Onde as duas análises concordam e onde divergem

| Tema | GPT 6 | Gemini | Situação |
|---|---|---|---|
| Instrumento de leitura sem controle positivo | Sim, prioridade 1 | Sim | **Consenso** — obrigatório antes de qualquer conclusão sobre pixel |
| NGG / culling NGG | A/B `nonggc` × `nongg` | A/B, com mecanismo "viewport negativo inverte a área" | **Consenso no ensaio**; o mecanismo do Gemini conflita com a Corrida 70 (viewport positivo, também preto) |
| `vkMapMemory -5` no harness | Consequência provável do TDR; falta a linha `Lock2 failed` | Consequência do TDR | **Consenso**; falta a prova |
| Causa nº 1 | **Topologia fabricada no winsys** (1 SA/SE contra 2 medidos) | **Clear State Buffer não carregado** → `PA_SC_GENERIC_SCISSOR` zerado | **Divergência** — as duas são verificáveis offline |
| Golden settings | Snapshot primeiro; nunca aplicar os 34 juntos | Lista como omissão | Consenso em não aplicar às cegas |
| Coerência CB → cópia | Não prioriza | Hipótese de barreira sem flush do CB | Verificável offline pelo despejo de IB |
| Primeiro uso `UNDEFINED` com pass `LOAD` | Defeito real | — | Verificável por leitura |
| `DepthCompareOp` não propagado | Aponta | — | Verificável por leitura |

**Afirmações do Gemini que precisam de checagem antes de valer (possíveis erros):**
- "O RADV nunca programa `PA_SC_GENERIC_SCISSOR_TL/BR`" — conferir no fork e no despejo de IB.
- "`gfx_v10_0_setup_rb` programa `PA_SC_RASTER_CONFIG`" — no GFX10 isso pode não existir; conferir no upstream.
- Offsets citados (`RLC_CSIB_ADDR_LO 0x13288`, `PA_SC_GENERIC_SCISSOR_TL 0x28240`, `TILE_STEERING 0x835C`) — **Lei 7**: só valem conferidos em `inc/linux_ref/`.
- O experimento proposto escreve **canário pela CPU** no buffer linear e troca o `loadOp` do alvo do
  **DWM** para magenta. O buffer linear é o que vai ao scanout: canário de CPU ali violaria a Lei 4
  se chegasse à tela. Substituir por `vkCmdFillBuffer` (GPU) e fazer os controles fora do DWM.

**Afirmações do GPT 6 que precisam de checagem:**
- A captura Fedora de `docs/10` (SE 2, SH/SE 2, CU/SH 10, 24 CUs ativos) — conferir a fonte e a
  data.
- "`max_render_backends=8` habilita culling NGG e alimenta `pa_sc_tile_steering_override`" —
  conferir o fluxo em `ac_gpu_info.c` do fork.

---

## 1. Fase A — validação OFFLINE (sem corrida)

Cada item tem método, resultado que confirma, resultado que refuta, e o que muda no plano.

| ID | Afirmação | Método (leitura/comando) | Confirma se… | Refuta se… |
|---|---|---|---|---|
| **A1** | Winsys fabrica topologia (SE=2, SA/SE=1, RB=8, mask 0xFF, WGPs, `GB_ADDR_CONFIG=0x44`) | Ler `radv_wddm2_winsys.c` (~l.359) e o patch espelhado (~l.1620) | Valores estão fixos no código | Valores vêm do KMD/registrador |
| **A2** | Silício tem 2 SA/SE (captura Linux) | Ler `docs/10_AUDITORIA_INIT_GFX_CYAN_SKILLFISH.md` (~l.226) e a evidência que ela cita | Captura existe, é deste modelo, com proveniência | Não existe, é de outro chip ou é inferência |
| **A3** | O Mesa usa esses valores sem reconstruir | Ler `ac_gpu_info.c` (~l.1127) e `radv_physical_device.c` (~l.2576) | `max_sa_per_se`, `enabled_rb_mask`, `max_render_backends`, `pa_sc_tile_steering_override` vêm direto do winsys | Há recomputação a partir de registrador |
| **A4** | **Estado de contexto efetivo do draw** (novo — nenhuma auditoria usou) | Decodificar os 31–34 despejos de IB da Corrida 71: todo `SET_CONTEXT_REG`/`SET_SH_REG` antes do draw 5 | Listar `PA_SC_GENERIC_SCISSOR_TL/BR`, `PA_SC_SCREEN_SCISSOR_*`, `PA_SC_WINDOW_SCISSOR_*`, `PA_SC_VPORT_SCISSOR_0_*`, `PA_SC_CLIPRECT_RULE`, `PA_SC_TILE_STEERING_OVERRIDE`, `CB_COLOR0_BASE/INFO/ATTRIB/VIEW`, `CB_TARGET_MASK`, `CB_SHADER_MASK`, `CB_COLOR_CONTROL`, `SPI_SHADER_COL_FORMAT`, `DB_DEPTH_CONTROL`, `DB_RENDER_CONTROL`, `PA_CL_CLIP_CNTL`, `PA_SU_SC_MODE_CNTL`, `PA_CL_VPORT_*` | Mostra quais registradores o IB **não** escreve (dependem de CLEAR_STATE/CSB) e se `CB_COLOR0_BASE` = VA da imagem `>>8` |
| **A5** | CSB/`RLC_CSIB_*` nunca programados no KMD | Busca em `driver/wdk` e `driver/core` por `CSIB`, `CLEAR_STATE`, `csb`; offsets em `inc/linux_ref/gc_10_1_0_offset.h` | KMD não escreve `RLC_CSIB_ADDR_LO/HI/LENGTH` e o IB depende de `CLEAR_STATE` para `GENERIC_SCISSOR` (via A4) | KMD programa, ou RADV escreve `GENERIC_SCISSOR` explicitamente no IB |
| **A6** | O que o amdgpu faz na init do GFX10 e nós não | Baixar e arquivar em `inc/linux_ref/` o `gfx_v10_0.c` upstream (com sha256, como o `gfxhub_v2_0.c`); listar `init_golden_registers`, `constants_init`, `setup_rb`, `init_csb`, `init_pa_sc_tile_steering_override`, `SH_MEM_*` e comparar com o KMD | Lista concreta de registradores/sequências ausentes | Todas já feitas pelo KMD ou pelo RADV no IB |
| **A7** | `setup_rb` programa `PA_SC_RASTER_CONFIG` no GFX10 (Gemini) | Mesmo arquivo de A6 | Existe no caminho GFX10 | Só lê máscaras de RB (hipótese do GPT) |
| **A8** | Barreira `COLOR_ATTACHMENT_WRITE → TRANSFER_READ` faz flush do CB antes da cópia | Despejo de IB da submissão de leitura: eventos depois do draw (`CACHE_FLUSH_AND_INV`, `FLUSH_AND_INV_CB_*`) e `ACQUIRE_MEM` com `CB_ACTION` antes da cópia | Flush presente | Ausente → Gemini M3 vira candidato |
| **A9** | Primeiro uso do alvo: layout `UNDEFINED` com pass `LOAD` | Ler `Bc250UmdExecuteVkDraw` e o rastreio de `VkLayout`; no log da 71, o que vem antes do 1º draw no alvo (clear?) | Existe caminho em que `LOAD` roda sobre `UNDEFINED` | Todo alvo passa antes por clear/transição |
| **A10** | `DepthCompareOp` não propagado; depth test sem attachment | Ler montagem da chave em `bc250_d3d_umd.c` e o render pass (tem attachment de profundidade?) | `DepthTestEnable` pode ir a TRUE com `LESS` padrão | Só com attachment presente |
| **A11** | `-5` é consequência do TDR | `conformance-stderr.tmp` da Corrida 71: procurar `Lock2 failed: status=` e alinhar o horário com o TDR do KMD | `status` é de device hung/lost e posterior ao TDR | Falha antes do TDR ou com outro status |
| **A12** | Pacote em que o PFP para no TDR (0x42 `PFP_SYNC_ME` × 0x58 `ACQUIRE_MEM`) | `TDR_GFX_PACKET` nas corridas 68–71 | Qual opcode é, corrida a corrida | — (só corrige o registro) |
| **A13** | Mecanismo do Gemini: viewport negativo inverte a área no culling NGG | Comparar Corrida 70 (altura positiva) × 71 (negativa): ambas pretas | — | Preto nas duas refuta **esse mecanismo** (não o NGG em geral) |
| **A14** | Lei 7 — offsets citados pelas auditorias | Conferir cada um em `inc/linux_ref/` | Offset bate | Não bate → não usar |

**Saída da Fase A:** tabela com cada afirmação marcada **P1 confirmada**, **refutada** ou **só
medível em hardware**, registrada em `docs/338_...` e na BASE. As refutadas saem do plano.

---

## 2. Fase B — candidato de medição (uma corrida, sem mudar comportamento do DWM)

Só o que a Fase A deixou como "medível em hardware". Tudo **somente leitura** ou em processo de
teste separado; o DWM continua exatamente como na 71.

### B1. Autoteste gráfico em Vulkan puro (controle positivo), fora do DWM
Executado uma vez no processo do harness (armado por chave de registro), com SPIR-V embutido, sem
passar pela tradução D3D11. Sequência, com o buffer de leitura **reenvenenado pela GPU** antes de
cada cópia:
1. `vkCmdFillBuffer(buf, 0xA5A5A5A5)` → mapear → contar.
2. `vkCmdClearColorImage(alvo, magenta)` → cópia → mapear.
3. Render pass com `loadOp=CLEAR` verde → cópia → mapear.
4. Triângulo de tela cheia gerado por `VertexIndex` (sem VB/IB/CB), PS com cor constante branca.
5. Variante do PS que incrementa um contador atômico em storage buffer por fragmento.
6. Repetir 4–5 com `RADV_DEBUG=nodcc,nonggc` e `nodcc,nongg` (instâncias separadas; a opção é lida no
   `vkCreateInstance`).

Log por passo: palavras iguais ao veneno, pixels da cor esperada, zeros, outros, primeiro offset
divergente, contador de fragmentos, status do map. **Falha de medição é estado próprio, nunca vira
zero.**

### B2. Snapshot somente leitura no KMD, no bind
- `GB_ADDR_CONFIG`.
- Os 34 goldens: valor atual, esperado mascarado, igual/diferente, duas leituras (estabilidade).
- Topologia: registradores de configuração de SA e de RB desabilitados, por SE/SA — **exige
  escrever o seletor `GRBM_GFX_INDEX`**; declarar como risco e restaurar broadcast com readback
  (precedente: sonda de ondas).
- `RLC_CSIB_ADDR_LO/HI/LENGTH` atuais (confirma A5 no silício).

### B3. Exportar a topologia medida ao RADV só como log
O RADV registra lado a lado `KMD_MEASURED` × `RADV_EFFECTIVE` (SE, SA/SE, máscaras, RB, steering,
`GB_ADDR_CONFIG`). **Não corrigir ainda** — a correção é a Fase C.

### B4. Captura integral de stderr do RADV no harness
Para a linha `Lock2 failed` e as do winsys.

Critério da corrida escrito antes (Lei 10), com desfecho E.

---

## 3. Fase C — correções condicionais (uma variável por corrida)

Árvore de decisão a partir de B1:

| Resultado do autoteste | Implicação | Correção candidata (uma por corrida) |
|---|---|---|
| Fill não aparece | Map/coerência quebrados | Winsys: `Lock2`, tipo de memória, fence do buffer |
| Fill aparece, clear de transferência não | Cópia/tiling/layout da imagem | `GB_ADDR_CONFIG` e topologia do winsys (A1–A3, B2) |
| Clear de transferência aparece, clear de render pass não | CB não escreve | Topologia/RB/steering; depois subconjunto CB dos goldens com snapshot/rollback |
| Clear de render pass aparece, contador = 0 | Nenhum fragmento | `GENERIC_SCISSOR`/CSB (se A4/A5 confirmarem); NGG pelo A/B |
| Contador > 0, alvo sem a cor | Fragmentos executam, CB não grava | `CB_TARGET_MASK`/`CB_SHADER_MASK`/formato/blend (via A4) |
| Autoteste passa inteiro, DWM preto | Pipeline funciona | Tradução D3D11 do DWM (A9, A10) ou Present |
| TDR antes do controle | Não classificável | Repetir só com `nongg`; nunca contar como "pixel zero" |

Ordem se houver mais de um candidato: (1) corrigir o que for **fato fabricado** (topologia) antes
de qualquer ajuste empírico; (2) CSB/`GENERIC_SCISSOR`; (3) NGG; (4) primeiro uso `UNDEFINED`;
(5) goldens **por subconjunto**, nunca os 34 juntos.

---

## 4. Regras que valem em todo o plano

- Sem WARP e sem CPU produzindo pixel; o autoteste não apresenta nada na tela.
- Sem escrita em DCN. Escrita em GC só com transação completa (snapshot → aplicação → readback →
  rollback) e offset com procedência.
- Uma variável comportamental por corrida; instrumentação somente leitura pode acompanhar.
- "Fence retirou" não é "pixel escrito"; "grade zero" não é "superfície preta" sem controle positivo.
- Cada fase termina com registro na BASE; a Fase B com critério escrito antes.
