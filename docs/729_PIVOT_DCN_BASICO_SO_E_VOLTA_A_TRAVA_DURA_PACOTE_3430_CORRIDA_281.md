# 729 — Decisão do Daniel: DCN reduzido ao básico comprovado, volta à investigação da trava dura

**Data:** 13/10/2026, offline (sem corrida). Depois da Corrida 280 (correção de `V_TOTAL_MIN`/`MAX`) repetir o mesmo sintoma de artefatos, o Daniel decidiu parar de perseguir a causa exata agora e **reduzir o DCN ao que já está provado**, voltando a atenção para a investigação histórica da trava dura (P0-P8 do projeto).

## 1. Decisão do Daniel (verbatim, resumida)

Manter apenas: **D01, D02, D03, D04, D05, D08, D10, D15, D33**. Tudo o mais fora do pacote por agora. D18 (troca de resolução de verdade) segue como objetivo — "deverá funcionar de alguma forma, sim" — só não é prioridade imediata. D29 (áudio por DP) fica para o fim, porque o monitor atual não permite testar (o Daniel tem um 4K que serviria, mas não é o monitor em uso agora). D20, D21, D25, D26, D27, D09 vão ser necessários, mas não neste momento. **Prioridade agora: voltar a testar a trava do display** (a investigação histórica do projeto, P0-P8).

## 2. O que isso significa em `DcnExt`

| Bit | Item | Decisão |
|---|---|---|
| `0x01` IRQ | D05 (flip completo pela interrupção) depende deste bit (o comentário do próprio código diz "VSYNC e flip pela interrupção") | **Mantido** |
| `0x02` CRC | D17 | Já estava fora desde a doc 714 |
| `0x04` CURSOR | D10 | **Mantido** |
| `0x08` EDID | D15 (leitura de HPD) vem junto desta chave; D13/D14 (EDID real) continuam tentando e falhando, sem efeito colateral (é leitura de AUX por software, sem a escalada de modo do D12) | **Mantido** (por causa do D15; D13/D14 continuam sem funcionar, como sempre) |
| `0x10` DPMS | D08 | **Mantido** |
| `0x20` TIMING | D04 (refresh real no VidPn/EDID sintetizado) | **Mantido** |
| `0x40` D06 | Só leitura, nunca escreve nada — diagnóstico inofensivo | **Mantido** (não foi pedido para sair, e não tem risco) |
| `0x80` D12 | Pad AUX em modo hardware (escreve) | **Fora** |
| `0x100` D19 | V_TOTAL (escreve) | **Fora** |

Resultado: `DcnExt = 125` (0x7D = 0x3D + 0x40 D06), **exatamente o mesmo valor já validado nas Corridas 273 e 274** — sem tela com artefatos registrada em nenhuma das duas. `DcnFlip=1` continua (D01-D05). `DebugTrace=1` continua **ligado de propósito**: é a ferramenta certa para a investigação da trava dura (validador de PTE, eventos `PAGING_PTE`, doc 704), não tem relação com a faixa DCN.

Nenhum código foi revertido — as funções de D06/D12/D19 continuam no repositório, só não armadas nesta corrida (a chave de registro simplesmente não liga esses bits). Se um dia for preciso voltar a investigar a causa dos artefatos, o código e a instrumentação (incluindo o diagnóstico de `V_TOTAL_CONTROL`/`MIN`/`MAX` da doc 728) continuam disponíveis.

## 3. Pacote: lab 3.430.1.0

`build-contract.bat` completo, lab bring-up, empacotamento com `-DcnFlip -DcnExt -DebugTrace` (sem `-ArmAuxHwMode`, sem `-ArmVTotalRefreshTest`). Manifesto confere: `DcnExt=125`, `DcnExtAuxHwModeArmed=false`, `DcnExtVTotalRefreshTestArmed=false`. As 4 auditorias obrigatórias: todas **PASS**.

## 4. Critério da próxima corrida (Lei 10) — não é mais sobre DCN

Esta corrida **não é um ensaio de DCN**: é a volta à pergunta histórica do projeto — a trava dura que motivou toda a investigação P1-P8 antes do desvio para o DCN (doc 709 em diante). O DCN básico (flip, cursor, DPMS, refresh real) fica ativo só para a tela continuar visível durante o ensaio; o que importa medir é a trava, não o DCN.

**A — Sem trava dura, com T3 completa.** 600 s (ou o tempo que for definido) sem travamento, com a carga T3 rodando. → Informação nova: o DCN básico não interfere na trava, e o foco pode continuar 100% na pilha de submissão/memória.

**B — Trava dura de novo, assinatura conhecida** (GPU ociosa, RPTR==WPTR, sem fault/TDR). → Confirma que a trava é anterior e independente de tudo que foi tentado no DCN; next passo é voltar ao roteiro de P1-P8 do `BASE_DE_AVANCO.md`.

**C — Tela com artefatos mesmo com o DCN básico.** → Surpreendente dado que 273/274 não tiveram esse problema com a mesma configuração; descreveria algo novo, não coberto pelas hipóteses já descartadas (monitor, `V_TOTAL_MIN/MAX`).

**D — Qualquer regressão do que já funcionava** (flip, cursor, DPMS, refresh real).

**E — Fora do mapa.**

## 5. Pendências

- **NÃO VALIDADO EM HARDWARE.**
- D18, D20, D21, D25, D26, D27, D09, D29 ficam **registrados como necessários**, fora de ordem de prioridade imediata — ver `BASE_DE_AVANCO.md`, seção P9, para o estado de cada um.
- A causa exata dos artefatos de 276-280 continua sem explicação P0 definitiva; fica arquivada nos docs 725-728 para quando a faixa DCN for retomada.
