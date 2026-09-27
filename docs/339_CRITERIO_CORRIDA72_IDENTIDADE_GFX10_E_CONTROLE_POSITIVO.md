# Critério da Corrida 72 — Identidade GFX10.1 e controle positivo (faixa DX.26)

> **Escrito em 22/09/2026, ANTES do ensaio** (Lei 9). **Estado:** NÃO VALIDADO EM HARDWARE.
> Base: Fase A das auditorias ([docs/338](338_RESULTADO_FASE_A_AUDITORIAS.md)).

## 0. O que entra nesta corrida

| Parte | O quê | Tipo |
|---|---|---|
| **1** | ICD do RADV com `ip_discovery_version = 10.1.3` → o GFX1013 deixa de ser tratado como GFX10.3 | **Única variável comportamental** |
| 2 | `bc250-gfx-selftest.exe` (Vulkan puro, ICD direto) rodado 3× antes do harness: `nodcc`, `nodcc,nonggc`, `nodcc,nongg` | Medição, processos isolados, nada vai à tela |
| 3 | Snapshot **somente leitura** no KMD dos 34 golden settings e do `RLC_CSIB_*` | Medição |

**Identidade dos artefatos:**

| Artefato | SHA-256 |
|---|---|
| Pacote | lab **3.233.1.0**, `DISPLAY_RENDER_LAB`, WDDM 2.7, P1 PERSISTENT SUBMISSION |
| SYS assinado | `7DE5C2D5FCA0200B276303AE1A8934608BB21FE59D01CAA8A7BA732B517DFCDD` (fonte `6428DC96...`) |
| UMD assinada | `A757ACEDFA8B4EED1C7090048704290988F9348F6BA566F5DC7BFD99B486E00E` (igual à 3.232.1.0) |
| INF / CAT | `3530DAC9...` / `521E2EB6...` |
| ICD (`vulkan_radeon.dll`) | `57D60ACB44BD2E7C7E838F5E3C2F44450966CF594104FD93D45FC04AF8AFCB1B` — copiado do `build\` do fork pelo executor |
| Delta do fork | patch `D5847192...` em `external/radv-wddm/` |

Verificação offline: build do fork, `build-contract`, build do SYS de laboratório, `build-umd`,
`build-gfx-selftest` — todos EXIT=0; auditoria do RADV (portão DX.26) e três auditorias do repo
PASS; `Lab package audit: PASS`; strings conferidas no SYS assinado e no ICD. **Três mutações
reprovando:** `hw_ip_compute` sem `ip_discovery_version`; escrita dentro do snapshot; runner sem a
variante `nongg`.

**Riscos declarados:**
- A mudança de identidade altera tudo o que o RADV programa e compila (estado 10.1, contornos de
  defeitos do 10.1 ligados). O caminho de **compute** provado em 12/09 rodou com a identidade errada;
  pode mudar de comportamento.
- O autoteste pode provocar TDR (por exemplo, na variante padrão). O TDR recupera; a variante
  seguinte roda em processo novo.

## 1. Perguntas

1. O RADV passa a tratar o chip como GFX10.1?
2. O instrumento de leitura enxerga valores conhecidos (controle positivo)?
3. Com a identidade certa, a rasterização gera e escreve fragmentos — no autoteste e no DWM?
4. O CSB está programado e os golden settings conferem?

## 2. Sinais a medir (assinaturas usadas nos desfechos)

- **ID-OK:** `selftest-*-stderr.txt` com `BC-250 IDENTIDADE: gfx_level=<GFX10>` e
  `ngg_fully_culled_bug=1`, **e** `tools/decode-gfx-ib-regs.py` sobre o `gfx-ib-dump` mostrando
  **0** registradores só-GFX10.3.
- **INSTR-OK:** no autoteste, `S1 FILL_BUFFER_GPU ... PASS` e `S2 CLEAR_TRANSFERENCIA_MAGENTA ... PASS`.
- **CB-OK:** `S3 CLEAR_RENDER_PASS_VERDE ... PASS`.
- **RASTER-OK:** `S4 DRAW_TELA_CHEIA_BRANCO ... PASS` e `S5 CONTADOR_DE_FRAGMENTOS=65536`.
- **DWM-PIXEL:** alguma `INSPECAO LINEAR_BUF GRADE: NonZero=n` com `n > 0` no `dwm.exe`.
- **Snapshot:** linhas `[GOLDEN_SNAPSHOT] ... RESUMO` e `[CSIB_SNAPSHOT]` no log do KMD.

## 3. Desfechos (Lei 10 — assinatura e implicação separadas)

### A — Identidade corrigida e gráfico funcionando
- **Assinatura:** ID-OK **e** INSTR-OK **e** CB-OK **e** RASTER-OK na variante `nodcc` (padrão).
- **Implicação:** a identidade errada era a causa (ou uma causa necessária) do "zero fragmento". Se
  também houver DWM-PIXEL, D1.6 passa a depender de atribuição (D1.8) e da ponte WDDM. Se não houver
  DWM-PIXEL, o defeito restante é específico da tradução D3D11 ou do Present — o autoteste prova que
  o silício rasteriza.

### B — Identidade corrigida, rasterização só funciona sem NGG
- **Assinatura:** ID-OK e INSTR-OK e CB-OK; RASTER-OK **só** em `nonggc` ou **só** em `nongg`.
- **Implicação:** o defeito está no NGG (culling, ou NGG inteiro) mesmo com a identidade certa.
  Próximo: desligar o que falhou no ICD por padrão para este chip, como correção declarada.

### C — Controle positivo prova o instrumento, e a rasterização continua zero em todas as variantes
- **Assinatura:** INSTR-OK e CB-OK; S4/S5 falham nas três variantes (contador 0 ou alvo verde).
- **Implicação:** o CB escreve e a leitura funciona, mas nada é rasterizado mesmo sem NGG. Próximo:
  CSB/`PA_SC_GENERIC_SCISSOR` (ver o `[CSIB_SNAPSHOT]`), depois topologia e goldens pelo snapshot.

### D — O CB não escreve
- **Assinatura:** INSTR-OK e **S3 falha** (clear de render pass não chega ao alvo).
- **Implicação:** o problema é anterior à rasterização: color buffer/topologia/goldens. Próximo:
  confrontar o `[GOLDEN_SNAPSHOT]` e a topologia antes de mexer em NGG.

### E — O instrumento é cego
- **Assinatura:** S1 ou S2 falha (fill pela GPU não aparece, ou clear de transferência não aparece),
  ou `MAP_FALHOU`.
- **Implicação:** todas as conclusões de "preto" das corridas 69–71 ficam suspensas. Próximo:
  winsys (map/coerência/fence) ou cópia/tiling, antes de qualquer outra coisa.

### F — Identidade não mudou
- **Assinatura:** `gfx_level` ainda igual a GFX10_3 no stderr, **ou** registradores só-10.3 no IB, **ou**
  hash do ICD em `binding-provenance.json` diferente de `57D60ACB...`.
- **Implicação:** o ensaio não aconteceu como desenhado; corrigir a implantação primeiro.

### G — Algo fora do mapa
- **Assinatura:** qualquer coisa que não caiba em A–F: autoteste sem saída, BugCheck, regressão dos
  controles (replay, faults GCVM, `Submetidas != Aposentadas`), TDR em todas as variantes antes de S1.
- **Implicação:** descrever antes de julgar.

**Desfecho parcial não é desfecho** (Lei 10.3). O snapshot (parte 3) não decide desfecho: é
insumo da próxima faixa.

**Controles a não regredir:** `check-gfx-ib-dump-cumulative.py` = 0, zero `[TDR_GCVM_FAULT]`,
`Submetidas == Aposentadas`, `RECOVERY_OK_ONLINE`.

## 4. Execução e análise

Boot frio, `tools\EXECUTAR_D3D11_NATIVO.cmd -Force`. Na análise, nesta ordem:

```bash
python tools/decode-gfx-ib-regs.py <evidencia>/gfx-ib-dump/<maior .bin>
python tools/check-gfx-ib-dump-cumulative.py <evidencia>/gfx-ib-dump
```

e os arquivos `selftest-*-stdout.txt`, `selftest-*-stderr.txt` e `selftest-resumo.txt`.
