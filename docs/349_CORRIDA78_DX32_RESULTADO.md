# Corrida 78 — DX.32 (engine de invalidação 0, cópia única, `COMPUTE_VMID`): resultado

> **23/09/2026, 08:13:52–08:17:12.** Candidato lab **3.239.1.0**, SYS
> `9324073FE93F7EC3E2E972A3B3FA890EB4E97CBB6BD4DD1499178D00A07A311A` (conferido na pasta e em
> `binding-provenance.json`), ICD `5988B38E...`.
> Pasta: `docs/evidence/2026-09-23_08-13-52-DESKTOP-HQAG6T4-lab-binding/`; log do KMD
> `display-log-this-bind-FINAL.txt` (30.772 linhas, `complete_at_capture=true`).
> Critério prévio: [docs/348](348_DX32_CRITERIO_CORRIDA78_ENGINE_PROPRIO_E_COPIA_UNICA.md).
> **Veredito:**
> - classe de TDR: **C3**;
> - V2: **não exercitada**, portanto E não ocorreu;
> - controles: sem F;
> - fora do mapa (G): harness com 12/60 quadros, e cópias `SET_ADDRESS` de primária zerada apagando o scanout.

## 1. Houve avanço?

**Diagnóstico sim; funcional, pouco.**
- **Eliminado um suspeito (P0):** o engine 0 funcionou e a classe de TDR persistiu. A disputa do engine 17 entre anel e
  CPU **não é causa suficiente**.
- **Harness: 12 de 60 quadros aceitos** (`PRESENT_OK` até o quadro 12, falha no 13 com `0x887A0005/0x887A0007`).
  Antes disso:
  - o máximo era 1/60, na Corrida 75;
  - a 76 teve 0/60;
  - a 77 também teve 0/60 (no arquivo de conformidade daquela pasta).

  Isso é contagem de Presents aceitos pelo runtime, **não prova de rasterização nem de tela**.
- **Visual:** pior que a 77 (desktop congelado lá; aqui, tela preta a maior parte do tempo).

## 2. Sucessos medidos

- **ENG0-ATIVO:**
  - 88 linhas `VMID_SWITCH_VM_FLUSH`, das quais 79 trazem `Eng=0` e nenhuma `Eng=17`. As outras 9 são linhas de dump PM4 que
    contêm a mesma tag;
  - no dump da primeira transição, o REQ está no dword `0x28A3` e o ACK no `0x28B5` (linhas 1939/1941);
  - o anel passou das transições: `SUBMIT SUCESSO` e fences aposentadas depois delas.
- **Não foi D:** nos dois TDRs o ME está em `EVENT_WRITE` (`0xC0004600`), não em `WAIT_REG_MEM`. O PFP está em
  `PFP_SYNC_ME`, e `IB1 Base` é o IB alvo, ou seja, o IB partiu.
- **Autoteste:**
  - `nodcc,nonggc` e `nodcc,nongg` 5/5, com contador de 65.536 fragmentos;
  - `nodcc` teve S1 PASS e S2 **NÃO MEDIDO** (`MAP_FALHOU res=-5`), com S3–S5 pulados. Pela regra do docs/348 §3.1 isso não
    é FAIL.
- **Controles:**
  - zero `PREAMBLE_CNTL`;
  - zero `GFXHUB_FAULT`;
  - zero BugCheck;
  - `TDR_RESTART PROVADO` nos 2 TDRs, com NOP e fence sobre poison.
- **Caminho do harness até o scanout (P0 amostral):**
  - `COMPOSITE_BLT OK RECT 1:1 1280x720` (5/5 amostras) no slot do Present;
  - `Present #13–#15` com `TILE_PROBE 256/256` = `FF2673E5`, o fundo azul do harness;
  - um `PRESENT_BLT` de escape `1280x720 ... SDMA HW BLT CONCLUIDO`, com 5/5 amostras, para o scanout em `0x0`;
  - o scanout aparece com **86/256** amostras não-nulas e hash constante `006685EB`. 1280×720 ocupa 33% de 2560×1080, e
    86/256 também é 33%.

  É o "quadrado com triângulo" que o Daniel viu, no canto superior esquerdo sobre fundo preto.

## 3. Falhas

- **2 TDRs de 240/240 ondas, os dois logo depois de troca de VMID:**

  | TDR | Troca | IB travado |
  |---|---|---|
  | 1 | VMID 3→4 (flush #7, `Eng=0`) | 648 DW em `0x2400E0000`, VMID 4 |
  | 2 | VMID 1→4 (flush #31, `Eng=0`) | 288 DW em `0x240030000` (a classe `DRAW_READBACK`), VMID 4 |

  O TDR 1 coincide com a queda do harness (08:14:50) e o TDR 2 com o `nodcc` S2 NÃO MEDIDO.
- **`COMPUTE_VMID` sem leitura útil:**
  - o valor foi `0xCFBB8BBC` nos dois TDRs, idêntico e com bits fora do campo `DATA[3:0]`;
  - `DATA=12` não é nem o VMID do IB travado (4) nem o do anterior (3 ou 1);
  - `GRBM_GFX_CNTL=0` significa seleção ME0/pipe0/queue0, a do gráfico, onde o registrador de VMID de **compute** não
    tem significado definido (H, coerente com o limite declarado no docs/348).
- **V2 não exercitada:** zero `REDUNDANT_SKIP` e zero `REDUNDANT_CANDIDATE_REJEITADO`. Das 20 `PRIMARY_COPY`, 19
  tinham origem zerada (`SrcNZ=0`), e o pulo exige origem não-nula. A única não-nula (#18, MPO3, 223/256) não vinha de
  cópia integral anterior da mesma origem.
- **Tela preta, com mecanismo medido:**
  - as 16 `PRIMARY_COPY` de rota `SET_ADDRESS` copiaram a primária em `0xAAC000` (hAlloc do GOP, **sempre zerada**) sobre o
    scanout;
  - em #5–#8 o destino tinha 86/256 (o quadro do harness) e ficou 0/256;
  - em #12, #17, #19 e #20 o destino tinha 256/256 e ficou 0/256;
  - o quadro do harness voltava a cada Present (o destino antes da cópia seguinte volta a `006685EB`) e era apagado de novo
    pela `SET_ADDRESS`.

  Na 77 o mesmo acontecia (8 `SET_ADDRESS` com origem zero), mas havia 21 MPO3 com origem **não-nula**, vindas do flip
  do DWM. Na 78, 3 das 4 MPO3 tinham origem zero. Por que os buffers de flip do DWM vieram zerados nesta corrida está
  **não determinado** (H: DWMs derrubados pelos TDRs antes de compor).
- `Present #25`: `BYTE_PROOF FAIL` (`FirstMismatch=0x9ED000`) e `RECUSADO status=0xC01E0200`, logo depois de uma troca de
  VMID. Não atribuído.
- Zero `BYTE_PROOF PASS` do Present, contra ≥1 exigido nos controles, e 1 `BYTE_PROOF INCONCLUSIVO`.

## 4. Veredito frente ao critério (docs/348)

- **Classe de TDR: C3.**
  - ENG0-ATIVO presente, e TDR de 240 ondas pós-troca presente;
  - `ObservedComputeVmid` incoerente com os dois VMIDs (valor com bits fora do campo, igual nos dois eventos);
  - pela implicação escrita antes: a leitura pela seleção GRBM atual não serve e não pode ser usada como discriminante.

  A, B e D não ocorreram.
- **Display:** E não ocorreu, porque não houve `REDUNDANT_SKIP` e a V2 nunca agiu. A tela preta não é da V2.
- **F:** não ocorreu. O `nodcc` teve S2 NÃO MEDIDO por device perdido, não FAIL. Não houve BugCheck, `PREAMBLE_CNTL` nem
  perda de CSIB.
- **G (fora do mapa):** 12/60 quadros do harness; e a rota `SET_ADDRESS` da DX.31 **apaga** o scanout com uma primária que
  ninguém desenha.

## 5. Recuperação e pendências

- O watchdog fez rollback. A primeira recuperação não fechou; a segunda terminou em `RECOVERY_OK_ONLINE` às 08:17:12,
  com BasicDisplay em Código 0 em 5 amostras. Isso é recuperação, não sucesso do nosso DWM.
- Nenhum BugCheck.
- A marca de boot frio precisa ser conferida antes de qualquer novo bind.
- **NÃO VALIDADO EM HARDWARE:**
  - eliminação da classe de TDR;
  - V2;
  - desktop estável;
  - 60 quadros;
  - VMID do dispatch no instante do travamento.

**Próximos passos, sem corrida, para decidir antes de outra faixa:**
1. **Rota `SET_ADDRESS`:** no modelo Blt o Present escreve direto no scanout, e a primária `0xAAC000` nunca é atualizada.
   Copiá-la ao scanout destrói o quadro válido.

   Candidata (H): não copiar em `SET_ADDRESS` quando a alocação for a primária do modelo Blt, cujo espelho já é o scanout.
   Uma variável comportamental.
2. **VMID do dispatch:** medir `COMPUTE_VMID` exige seleção de ME/pipe/queue por `GRBM_GFX_CNTL`, e isso é escrita.

   A alternativa só de leitura é o `CP_ME_*`/`VMID` do anel gráfico. Ler o upstream (`gfx_v10_0.c`, registros de
   `CP_*_VMID`) antes de propor.

   A linha C1/C2 continua aberta, e a direção por leitura continua sendo `CONTEXT_CONTROL`/estado CS na troca de contexto.
