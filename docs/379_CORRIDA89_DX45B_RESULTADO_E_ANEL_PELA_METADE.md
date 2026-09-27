# Corrida 89 — DX.45b: não travou; o TDR restante é o anel gráfico programado com metade do tamanho

> 23/09/2026. Bind às 16:11:37, `RECOVERY_OK_ONLINE` às 16:13:46, sem reboot.
>
> **Candidato:** lab `3.250.1.0` (SYS `700C6D93…`, conferido); UMD e RADV DX.42
> inalteradas.
>
> **Critério:** [docs/378](378_DX45B_DETECTOR_DUAS_FASES_GUARDIAO_DURAVEL_CRITERIO_CORRIDA89.md).
>
> **Evidência:** `docs/evidence/2026-09-23_16-11-37-DESKTOP-HQAG6T4-lab-binding/`.
> - FINAL com `complete_at_capture=false`; a cauda foi resgatada e confere
>   (1 TDR, 1 preempção, zero detecção).
> - Coleta RADV pós-rollback presente.

## 1. Houve avanço?

**Sim, de diagnóstico; funcional, estável.**
- O PC não travou (a 88 travou).
- O harness fez **13/60** (a 87 fez 16/60), com os três autotestes RADV em
  5/5.
- A causa do TDR restante ficou identificada com alta confiança; veja a seção 5.

## 2. Sucessos

- **DX.43, quarta amostra:** 136/136 aquecimentos e **zero** TDR de VMID
  anterior. Somando 86–89, são 365 trocas sem essa classe.
- **Sem travamento e sem BugCheck;** rollback online na primeira tentativa.
- 587 submissões gráficas, zero `Cid=9`, 2 `BYTE_PROOF PASS` de tela cheia.
- A preempção falou: uma chamada, antes do TDR.

## 3. Falhas

- **Um TDR.** Snapshot:
  - `RPTR=5 WPTR=8220 CpMeCntl=0x00000000` (o CP **não** estava parado);
  - ME em `WAIT_REG_MEM`, PFP em `SET_CONTEXT_REG` de 216 DW,
    `GfxContextBusy=1`;
  - zero ondas, `Committed=1`.
- **Harness:** 13/60; o quadro 14 falhou com device removed (`0x887A0020`).
- **Detector DX.45b:** zero `[CP_RESET_DETECTED]`. O CP não parou nesta
  corrida, então não houve o que detectar.
- **Guardião:** o travamento não se repetiu, então a durabilidade **não foi
  exercitada**.

## 4. Veredito frente ao critério

- **Desfecho B: nenhuma detecção e nenhum travamento.** O snapshot do TDR tem
  `CpMeCntl=0`, o que exclui C.
- **Função: falha.** 13/60, TDR, device removed.

## 5. Achado: o anel gráfico está programado com metade do tamanho

**Código (P0):**
- o KMD aloca e escreve um anel gráfico de **64 KB = 16384 DW**
  (`bc250_active_miniport.c:823`, `RingDwords = 16384`);
- o contrato deriva `RB_BUFSZ = log2(RingBytes/8) - 1`
  (`bc250_gfx_ring_contract.c:103–108`), o que dá 12
  (`CntlPlanned=0x50000A0C`);
- o próprio cabeçalho declara essa fórmula como **classe H**, "corroborada"
  por um valor medido que o driver antigo tinha escrito, ou seja, circular.

**Upstream (P1):**
- Linux v6.6, `gfx_v10_0.c:6106` (e `:6149` para o RB1):
  `rb_bufsz = order_base_2(ring->ring_size / 8)`, **sem o "-1"**;
- o caminho do HQD (`:6407`) usa `order_base_2(queue_size/4) - 1`, que é a
  mesma coisa;
- a semântica é anel = 2^(RB_BUFSZ+1) DW. Com 12, o CP usa **8192 DW**.

**Evidência física (P0, 2 de 2):**

| Corrida | Primeiro submit com WPTR > 8192 | O que aconteceu |
|---|---|---|
| 87 | fence 314, WPTR 8196 | a 314 nunca terminou; no submit seguinte o RPTR era 5; preempção; TDR (CP parado) |
| 89 | fence 316, WPTR 8220 | a 316 nunca terminou; RPTR=5 no TDR; preempção; TDR |
| 84, 85, 86, 88 | nunca cruzou 8192 | TDRs de outras classes (84/85 VMID anterior, 86 ocioso) e travamento na 88, com WPTR 4656 |

**Mecanismo, com alta confiança:**
- ao passar de 8192 DW, o CP volta a ler a partir do **início** do buffer, que
  contém pacotes **antigos** do começo da corrida;
- o KMD escreveu os pacotes novos em 8192–8220, onde o CP nunca olha;
- o CP executa lixo antigo (transição de VMID velha, `WAIT_REG_MEM` de fence
  antiga, estado de contexto), trava e vem o TDR;
- **correção da minha leitura na docs/375:** o "RPTR 8188→5" da 87 não foi um
  reset; foi a **volta** do anel de 8192 DW;
- o `CP_ME_CNTL=0x15000000` da 87 fica como H: o CP pode ter parado sozinho ao
  executar lixo.

**Por que só aparece agora:** cada restart de TDR zera o anel (`RPTR=12
WPTR=12` na prova de restart). Enquanto havia 3–4 TDRs de VMID anterior por
corrida, o WPTR nunca chegava a 8192. A DX.43 removeu essa classe, as corridas
ficaram longas o bastante e a volta do anel apareceu.

**O teste congelava o erro:** `gfx_ring_contract_test.c:55–114` exige que
8192 bytes produzam (9, 7), a fórmula com "-1". Pelo upstream devem produzir
(10, 8). É a mesma classe de "o portão congelava a mentira", já vista várias
vezes neste projeto.

## 6. Recuperação e próximo passo

- **Recuperação:** `RECOVERY_OK_ONLINE` às 16:13:46, BasicDisplay em Código 0.
  O marcador de boot frio foi regravado. **Desligamento completo antes de outra
  corrida.**
- **NÃO VALIDADO EM HARDWARE:** a correção do anel, a causa do TDR ocioso da 86,
  a causa do travamento da 88, desktop interativo e 60/60.

**Proposta DX.46 (uma variável de comportamento):**
- `Bc250GfxRingDeriveBufsz` passa a `RB_BUFSZ = log2(RingBytes/8)` e
  `RB_BLKSZ = RB_BUFSZ - 2`, como o upstream. Para 64 KB: (13, 11),
  `CntlPlanned` `0x50000B0D`.
- O teste passa a exigir (10, 8) para 8192 bytes e (13, 11) para 65536. O
  portão proíbe o "-1" e exige o casamento `RingDwords == 2^(RB_BUFSZ+1)`, com
  mutação.
- **Critério da próxima corrida:**
  - WPTR passando de 8192 e de 16384 **sem** TDR no submit que cruza;
  - `GFX_TRIPE` com `RPTR_Reg` acompanhando o WPTR reduzido por 64 KB.
