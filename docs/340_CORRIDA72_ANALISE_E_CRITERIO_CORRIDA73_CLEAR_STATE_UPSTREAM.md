# Corrida 72: análise, faixa DX.27 e critério da Corrida 73 (clear state upstream)

> **22/09/2026.** Parte 1: análise da Corrida 72 (21:19:06, lab 3.233.1.0 com ICD `57D60ACB`), evidência em
> `docs/evidence/2026-09-22_21-19-06-DESKTOP-HQAG6T4-lab-binding/`, critério em [docs/339](339_CRITERIO_CORRIDA72_IDENTIDADE_GFX10_E_CONTROLE_POSITIVO.md).
> Parte 2: faixa DX.27, offline. Parte 3: critério da Corrida 73, **escrito antes do ensaio** (Leis 9 e 10).
> **Estado da DX.27: NÃO VALIDADO EM HARDWARE.**

---

## Parte 1 — Corrida 72

**Veredito frente ao docs/339: G, fora do mapa.**
- A a D exigem o S2, que não foi medido.
- F está refutado: a identidade mudou.
- A assinatura literal de E ("S1 falha") aparece, mas só por causa de um defeito do autoteste (1.3). A implicação de E, "instrumento cego", é contrariada pelos números brutos.

### 1.1 O que foi provado (P0)

- **Identidade GFX10.1:**
  - o stderr dos 4 processos mostra `gfx_level=12 (GFX10)` e `ngg_fully_culled_bug=1`;
  - os 5 IBs têm zero registradores só-10.3 (eram 3 na 71);
  - o hash do ICD em `binding-provenance.json` confere.
- **Caminho sem draw:** o IB de fill (`DMA_DATA` + `RELEASE_MEM` + `WAIT_REG_MEM`) retirou nas duas variantes em que rodou.
- **Snapshot somente leitura:**
  - `RLC_CSIB_ADDR_LO/HI/LENGTH` = 0/0/0, ou seja, o CSB não está programado;
  - 30 de 34 golden settings diferem (clock gating em valores de reset, `GRBM_GFX_INDEX`=0 em vez de broadcast);
  - `GB_ADDR_CONFIG` confere.
- **Controles:**
  - IB sem cumulativo;
  - zero fault GCVM latchado;
  - 4 TDRs recuperados (`TDR_RESTART PROVADO` nos 4);
  - zero BugCheck;
  - BasicDisplay em Código 0 depois da recuperação.

### 1.2 O que falhou (P0)

Foram só 7 submissões gráficas:

| FenceId | Origem | Conteúdo | Desfecho |
|---|---|---|---|
| 1 | DWM | 1 draw | TDR |
| 2 | autoteste `nonggc`, S1 | fill | retirou |
| 3 | autoteste `nonggc`, S2 | clear por draw + cópia | TDR |
| 4 | autoteste `nongg`, S1 | fill | retirou |
| 5 | autoteste `nongg`, S2 | clear por draw + cópia | TDR |
| 6 | DWM | 1 draw | TDR |
| 7 | DWM | 1 draw | indeterminado no log |

A variante `nodcc` do autoteste teve o device removido pelo TDR do DWM (FenceId 1) enquanto ainda criava recursos: `MapGpuVirtualAddress` devolveu `0xC00002B6`.

Assinatura, igual nos 4 TDRs:
- o ME está em `WAIT_REG_MEM`, que o RADV emite logo após o `RELEASE_MEM` que segue o `DRAW_INDEX_AUTO`, e o PFP está em `PFP_SYNC_ME`;
- `EopDoneBusy=1`, `GfxContextBusy=1`, SPI e PA ocupados no SE0;
- **ondas = 0**.

O draw entra no pipeline e o EOP nunca volta, com NGG, sem NGG culling e sem NGG. Nas Corridas 68 a 71, com a identidade 10.3, os draws retiravam sem pixel, e o TDR ocasional vinha com 240 ondas presas.

### 1.3 Defeito meu no autoteste

O S1 preenchia com `POISON` e esperava `POISON`. O `Inspect` classificava "veneno" antes de "igual", então o S1 saía FAIL com 65536 de 65536 dwords iguais a 0xA5A5A5A5 e zero zeros. Isso não fecha o controle positivo, porque não dá para excluir memória residual com esse padrão exato. Corrigido na DX.27 (2.3).

---

## Parte 2 — Faixa DX.27 (offline)

### 2.1 Item 1: comparação dos IBs da 71 (identidade 10.3) com os da 72 (10.1)

Comparei, com `decode-gfx-ib-regs.py --estado`, o primeiro IB do DWM de cada corrida:

- **Sumiram só os registradores do 10.3:**
  - `DB_VRS_OVERRIDE_CNTL`, `SX_PS_DOWNCONVERT*`, `SX_BLEND_OPT_*`, `PA_CL_VRS_CNTL`;
  - `VGT_DRAW_PAYLOAD_CNTL`, `VGT_REUSE_OFF`, 0x3098C.
- **Mudaram valores ligados à geração:**
  - máscaras `CU_EN` (`SPI_SHADER_PGM_RSRC3/4_*`);
  - `MEM_ORDERED` em `RSRC1_GS`;
  - `PA_CL_NGG_CNTL` (0x78 → 0);
  - `GE_CNTL`;
  - `DB_SHADER_CONTROL` e `DB_RENDER_OVERRIDE2`.
  - O resto é conteúdo diferente dos dois draws (alvo, viewport, scissor).
- **Nenhum registrador passou a depender do `CLEAR_STATE`.** Com as duas identidades, `PA_SC_GENERIC_SCISSOR_TL/BR` e `PA_SC_TILE_STEERING_OVERRIDE` **não são escritos pelo IB**. O Mesa só os escreve no gfx11 (`ac_cmdbuf.c:495-499`); no gfx10 os deixa para o `CLEAR_STATE`, com o comentário "CLEAR_STATE also sets this" (`ac_gpu_info.h:453`).

**Achado novo, P1 com leitura de fonte:** o preâmbulo de boot do KMD e o `radv_emit_graphics` do fork emitem `PREAMBLE_CNTL` com payload **0 e 1**. O upstream usa `BEGIN_CLEAR_STATE = (2<<28)` e `END_CLEAR_STATE = (3<<28)` (`nvd.h:395-396`). Além disso, o nosso boot não tinha **nenhum** `SET_CONTEXT_REG` entre as marcas.

Resumo: o `CLEAR_STATE` executado em todo IB nunca teve dados. O CSIB está vazio, e o preâmbulo nem marcava clear state.

### 2.2 Item 3: clear state como no upstream (única variável comportamental)

**Procedência:**
- `inc/linux_ref/clearstate_gfx10.h`, `clearstate_defs.h` e `nvd.h`, baixados do `torvalds/linux` master sem edição, com SHA-256 em `inc/linux_ref/PROVENANCE_CLEARSTATE.txt`;
- o upstream usa `gfx10_cs_data` para todo o GFX10 (`gfx_v10_0.c:4388`).

**Montador** `Bc250Pm4BuildGfx10ClearState` (`driver/core/bc250_pm4_packet_builder.c`), que reproduz `gfx_v10_0_get_csb_buffer`:
1. `PREAMBLE_CNTL(BEGIN_CLEAR_STATE)`;
2. `CONTEXT_CONTROL`;
3. um `SET_CONTEXT_REG` por extent, com 921 registradores em 8 extents;
4. `PA_SC_TILE_STEERING_OVERRIDE`;
5. `PREAMBLE_CNTL(END_CLEAR_STATE)`;
6. `CLEAR_STATE`.

São 949 dwords.

**KMD** (`Bc250ActiveGfxEngineOnline`), na ordem do upstream (`rlc_resume` → `cp_gfx_start`):
1. o CSB é montado numa página de VRAM logo depois do bloco auxiliar do anel;
2. o escritor único `Bc250ActiveProgramClearStateBuffer` grava `RLC_CSIB_ADDR_HI/LO/LENGTH`, com LENGTH em dwords (949). Faz snapshot, escreve, confere por readback e, se não conferir, restaura o snapshot e falha fechado;
3. o mesmo conteúdo é emitido no CP_RB0, seguido do `RELEASE_MEM`. É o `gfx_v10_0_cp_gfx_start` **sem** o `SET_BASE(CE_PARTITION_BASE)`, que parou o ME na Corrida 31.

Falha em qualquer passo deixa o motor gráfico fora do ar.

**Divergência declarada (H):** o CSB leva `PA_SC_TILE_STEERING_OVERRIDE = 0`. O upstream do 10.1 calcula o valor pelo `gc_info` da IP discovery, que não temos medido deste chip. Zero é o valor que o upstream usa no 10.3 e deixa o override desligado.

**Medição somente leitura acrescentada:** `[TDR_GFX_RESTART_CSIB]` mostra se o soft-reset do CP preserva o CSIB.

**Fora desta variável, de propósito:** o `PREAMBLE_CNTL` 0/1 do fork do RADV continua. O ICD é o mesmo `57D60ACB`. Com o CSIB programado, o `CLEAR_STATE` de cada IB passa a ter de onde ler, e o fork é o próximo candidato se a 73 não fechar.

### 2.3 Item 2: correção do S1 do autoteste

- A CPU grava a sentinela 0x5E5E5E5E pelo map.
- A GPU preenche com 0x3C3C3C3C, valor diferente do veneno e da sentinela.
- Só 0x3C3C3C3C em 65536 de 65536 dwords conta como PASS, e o `Inspect` classifica o esperado antes do veneno.

### 2.4 Verificação

- **Builds:**
  - `build-contract` EXIT=0, com o teste novo do CSB: 25 asserções contra números do upstream;
  - SYS de laboratório e autoteste compilados;
  - UMD inalterada.
- **Auditorias PASS:** segurança do MCDM (portão DX.26 ajustado, portão DX.27 novo), matriz de DDI, callbacks no-op e RADV.
- **Seis mutações reprovando na auditoria:**
  1. BEGIN volta a 0;
  2. escritor sem restauração;
  3. S1 esperando o veneno;
  4. CSIB programado depois do anel;
  5. CSIB fora da allowlist de escrita;
  6. tabela upstream editada.
- **Uma mutação reprovando no teste:** BEGIN=0 faz o `pm4_packet_builder_test` sair com código 1.
- **Pacote lab 3.234.1.0** assinado, com `Lab package audit: PASS`, NÃO INSTALADO:

| Artefato | SHA-256 |
|---|---|
| SYS assinado | `AF13FD894CB451A61D675125B9537D0A42CF6B2ADBC6F3F53EA648379E3E13F8` (fonte `62F1E206...`) |
| UMD | `A757ACED...` (igual à 3.232/3.233) |
| INF / CAT | `7CFF430B...` / `43983AF7...` |
| ICD | `57D60ACB...` (igual à Corrida 72) |
| Autoteste | `007AA396...` |

Strings conferidas no SYS assinado: `[CSIB_PROGRAM]`, `CSB montado`, `TDR_GFX_RESTART_CSIB`, `DX.27: CSB upstream`.

**Riscos declarados:**
- O boot agora escreve 921 registradores de contexto antes do primeiro submit. Se o CP não drenar, o motor gráfico fica fora do ar e a corrida não mede draw (desfecho D).
- Escrever o CSIB com o RLC rodando é o que o upstream faz no caminho de carga por PSP (`gfx_v10_0_rlc_resume`, P1), mas nunca foi feito nesta placa.

---

## Parte 3 — Critério da Corrida 73

### 3.1 Perguntas

1. O CSB é aceito pelo silício: readback, dreno do boot e persistência?
2. Com o CSB programado, o draw retira, ou seja, acaba o TDR de "ondas=0"?
3. Se retira, há fragmentos no alvo (S4 e S5)?
4. O instrumento de leitura passa no controle positivo (S1 e S2)?

### 3.2 Sinais

- **CSB-OK:** `[GfxEngine] CSB montado ... Dwords=949`, **e** `[CSIB_PROGRAM] ... -> PASS`, **e** `BOOT_PREAMBLE: ... DrenoOk=1 ... CSB upstream de 949 dw`, **e** `[CSIB_SNAPSHOT] ... LENGTH=0x000003B5 ... PROGRAMADO`.
- **ID-OK:** como no docs/339: `gfx_level=12` e zero registradores só-10.3.
- **INSTR-OK:** `S1 FILL_BUFFER_GPU (...) ... igual=65536 ... PASS` **e** `S2 CLEAR_TRANSFERENCIA_MAGENTA ... PASS`.
- **RETIRA:** nenhum TDR cuja linha `IB1 Base=` aponte para IB com draw, nas variantes que chegaram a submeter.
- **RASTER-OK:** `S4 ... PASS` e `S5 CONTADOR_DE_FRAGMENTOS=65536`.
- **HANG-72:** TDR com `ME_HEADER_DUMP ... WAIT_REG_MEM` e `WaveLevelSA0=0 WaveLevelSA1=0` logo após um IB com draw.
- **Variante não medida:** uma variante que termina com `0xC00002B6` ou `ExecutionState=2` **antes de submeter o próprio S2** foi derrubada por TDR de outro processo. Ela não conta como falha, e o desfecho usa as variantes que mediram. Se nenhuma medir, o desfecho é G.

### 3.3 Desfechos (assinatura separada da implicação)

**A — Clear state resolve o pipeline e há fragmentos**
- Assinatura: CSB-OK, ID-OK, INSTR-OK, RETIRA e RASTER-OK em pelo menos uma variante medida.
- Implicação: o CSB vazio era a causa (ou uma causa necessária) do travamento e do "zero fragmento". O próximo passo é o DWM: `INSPECAO LINEAR_BUF GRADE` com NonZero>0, atribuição e Present.

**B — O draw passa a retirar, mas sem fragmentos**
- Assinatura: CSB-OK, INSTR-OK e RETIRA; S4 ou S5 falham (contador 0 ou alvo verde) em todas as variantes medidas.
- Implicação: o CSB destravou o pipeline, mas algo ainda descarta tudo antes do CB. Próximo: topologia fabricada (SA/SE=1 contra 2 no silício, docs/338 A1/A2), steering, e depois goldens por subconjunto.

**C — CSB aplicado e o travamento de ondas=0 continua**
- Assinatura: CSB-OK e HANG-72 no primeiro IB com draw de toda variante medida.
- Implicação: o clear state era necessário e não suficiente, ou não era a causa. Próximo, por ordem de custo:
  1. o `PREAMBLE_CNTL` 0/1 do fork em cada IB;
  2. topologia e `PA_SC_TILE_STEERING_OVERRIDE` com valor medido;
  3. `GRBM_GFX_INDEX`, que o snapshot leu em 0 e não em broadcast;
  4. goldens.

**D — O CSB não foi aceito**
- Assinatura: `[CSIB_PROGRAM] ... FALHOU`, **ou** `CSB FALHOU`, **ou** `BOOT_PREAMBLE ... DrenoOk=0`. Nesse caso o motor gráfico não fica online.
- Implicação: a mudança falhou no bring-up. Decodificar RPTR, PFP e ME do boot antes de qualquer outra coisa. Nada sobre draw é concluível.

**E — O instrumento continua sem prova**
- Assinatura: CSB-OK, mas S1 falha com a lógica nova (sentinela 0x5E5E5E5E sobrevivendo, ou zeros) em toda variante medida, ou `MAP_FALHOU`.
- Implicação: map, coerência ou fence do winsys vêm antes de tudo, e as conclusões de "preto" seguem suspensas.

**G — Algo fora do mapa**
- Assinatura: qualquer coisa que não caiba em A–E, por exemplo:
  - BugCheck;
  - regressão do compute ou das faults GCVM;
  - IB cumulativo;
  - `Submetidas != Aposentadas`;
  - nenhuma variante medida;
  - TDR sem draw;
  - pixel na tela antes do rollback.
- Implicação: descrever antes de julgar.

Desfecho parcial não é desfecho (Lei 10.3). Os controles a não regredir são os do docs/339 e mais "CSIB lido como PROGRAMADO depois de todo TDR" (`[TDR_GFX_RESTART_CSIB]`); este último é informativo e não decide desfecho.

### 3.4 Execução e análise

Boot frio (o marcador `C:\BC250_REQUIRE_CLEAN_BOOT.json` está armado) e depois `tools\EXECUTAR_D3D11_NATIVO.cmd -Force`.

Na análise, nesta ordem:
1. as linhas `[GfxEngine] CSB`, `[CSIB_PROGRAM]`, `BOOT_PREAMBLE` e `[CSIB_SNAPSHOT]`;
2. `selftest-*`;
3. os TDRs (`TDR_GFX_PACKET`, `TDR_GFX_WAVES`, `TDR_GFX_RESTART_CSIB`);
4. `decode-gfx-ib-regs.py` e `check-gfx-ib-dump-cumulative.py`;
5. a grade do DWM.
