# 636 — Corrida 241b: encadeamento GFX upstream, menu sem TDR, trava dura aos ~5,5 min

**07/10/2026, bind 11:33:08, trava ~11:38:38, boot 11:39:41.** Lab 3.385.1.0 (SYS `40916F50…`) + RADV `B4AE2E14…` (as duas cópias). Evidência: `docs/evidence/2026-10-07_11-33-08-DESKTOP-HQAG6T4-lab-binding/`. Critério: [doc 635](635_241B_ENCADEAMENTO_NO_WDDM2_E_P33_RPTR_DO_IH_POR_DOORBELL_CRITERIOS_241B_E_242.md) Parte 1.

## 1. Pré-condição: cumprida (P0)

- Os despejos do menu (`gfx-ib-dump-pos-trava/`, `StartMenuExperienceHost` pid 6688) têm 5 preâmbulos de 184 dw em VA `0x2407D4000`, um por device novo. Todos terminam em `INDIRECT_BUFFER` com controle `0x009040D0` ou `0x009040F0`: CHAIN e VALID, sem bit 22 e sem VMID 7.
- Na 240 e na 241 o controle era `0x075040xx`.
- Dos 393 despejos, 32 estão zerados: os últimos segundos se perdem na trava, como já registrado.

## 2. Medido (P0)

- **TDR do GFX: zero** (`TDR_GFX_QUIESCE`, `TDR_QUIESCE`, `KMD_TDR_WAVE` = 0), em ~5 min de uso do menu.
  - O menu criou **5 devices** (10 contextos, 95 submissões). O preâmbulo de cada device novo rodou sem TDR.
  - Na 239 e na 240 o TDR vinha justamente na 1ª submissão de um device novo do menu, depois da troca de VMID.
- **Sem fault nem erro do SQ:** zero `GFXHUB_FAULT`, zero `IB_TRIPOD TIMEOUT`, zero `[P3_IH] OUTRO`.
- **Trava dura por volta de 11:38:38 (~5 min 30 s depois do bind).**
  - Última linha do DWM: 11:38:38.598, um Present e um Flush OK.
  - No mesmo instante subia um processo novo da Pesquisa (`SearchApp` pid 11904, carregado 11:38:38.556, último log 32 ms depois, no meio da criação do device Vulkan).
- **Fim do log do KMD:**
  - rajada de `PAGING_DDI operation=12`;
  - composição 2560×1080 OK com dados amostrados 5/5;
  - Present #907 por flip;
  - um IB de 36.768 bytes submetido sem a linha de execução depois.
  - O fim do log é a última drenagem, não o último evento (doc 582).
- **Rastro de paginação:** o último tique mostra a GPU ociosa, com os contadores de paginação zerados nos últimos segundos. É a assinatura de sempre.
- **Plataforma depois da trava:**
  - `RESET_REASON=0x40080400`: bits 30/10 fixos e bit 19 do reset pelo botão, como em todas as travas (doc 625);
  - eventos 41/6008 (desligamento sujo);
  - RAM 8.740 MB × 2 passadas sem erro;
  - coleta automática OK;
  - rollback pelo guardião no boot seguinte (BasicDisplay Código 0).
- **RLC:** `RLC_ENABLE_F32=0` no boot, como na 240.

## 3. Veredito pelo critério

**Desfecho B:** pré-condição cumprida, sem TDR do GFX, e trava dura com a assinatura de sempre.

- **Avanço funcional:** o TDR do menu sumiu com o encadeamento upstream, com o mesmo roteiro e mais devices novos do que na 240 (n=1, indício, não prova).
- **O que isso separa:** TDR e trava dura são problemas distintos. A trava veio sem TDR antes dela.
- **Achado:** a mudança local da RADV (bit 22, VMID 7, sem VALID) era a causa provável do TDR do menu, e ela não tinha fonte.

## 4. Próximo

1. **Corrida 242:** P3.3 (RPTR do IH por doorbell), KMD 3.386.1.0 + RADV `B4AE2E14`, mesmo roteiro. Critério no doc 635 Parte 2. Também serve de repetição do "menu sem TDR".
   - **Pacote pronto:** 3.386.1.0, SYS assinado `382C007B…` (fonte `825AC196…`), INF `FE3B9265…`, CAT `82CD4A89…`, UMD `8782A5E5…` (a mesma), auditoria do pacote PASS, strings da P3.3 no SYS assinado.
   - Script: `tools\EXECUTAR_CORRIDA242_P33_IH_RPTR_DOORBELL_MENU_600S.cmd`.
   - O pacote 3.385.1.0 da 241b ficou guardado em `driver\wdk\package\output_lab_3.385.1.0_241b\`.
2. Depois, **RLC rodando** como no amdgpu (o `pfp_direct_load` não deixar o `RLC_CNTL` em 0).

**NÃO VALIDADO EM HARDWARE:** causa da trava dura; "menu sem TDR" ainda precisa de repetição.
