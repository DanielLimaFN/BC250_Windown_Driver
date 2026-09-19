# P5 — GPUVA/Present fail-closed após a corrida de 12:23

**Data:** 2026-09-04  
**Candidato-base:** commit `0498663`  
**Estado:** implementação e testes offline; **NÃO VALIDADO EM HARDWARE**.

## Evidência que motivou a correção

A corrida `2026-09-04_12-23-15` provou ACK físico de FlushTlb, VidPn comitado,
18 entradas de Present e 10 blits registrados como `SDMA=YES`. Ela mostrou PTEs
iniciando no PFN `0xC10`, mas não registrou uma resolução autoritativa completa
`GPUVA 0xAC0000 -> VRAM 0xC10000`. A tela exibiu cinza/ruído e não houve
confirmação visual do desktop.

## Correções deste candidato

1. `UpdatePageTable` volta a propagar `BUSY` e estados inválidos do journal.
2. `FlushTlb` não transforma falha de publicação do journal em sucesso.
3. O Present não consulta o cache paralelo preenchido antes do flush; resolve a
   GPUVA exclusivamente no snapshot canônico publicado pelo journal.
4. Cada blit grava poison novo na fence e só conclui com fence esperada e
   `RPTR == WPTR`.
5. O safety audit trava essas quatro propriedades contra regressão.
6. O primeiro blit de Slot 1 resolvido compara todos os bytes entre origem e
   scanout; divergência desativa o SDMA e falha o Present.
7. O buffer de `SetVidPnSourceAddress` não é mais fallback de apresentação:
   sem Slot 1 resolvido pelo snapshot, o driver falha fechado.
8. Como o transporte físico invalida globalmente a GFXHUB, o commit do journal
   usa escopo global somente após o ACK. Root `0` não tenta publicar snapshot.
   Isso elimina honestamente os estados antigos `BAD_ARGUMENT` e
   `RANGE_NOT_FLUSHED`, sem mascará-los como sucesso.

## Testes

- Offline: `driver\wdk\build-contract.bat` — precisa terminar `EXIT=0`.
- Hardware: `P5_DWM_BACKING_TO_GOP_SDMA_TRIPOD`.
- Arquivo da prova: `docs/evidence/<timestamp>-P5-DWM-BACKING-SDMA-TRIPOD/`.

O teste físico fecha somente com: ACK do FlushTlb; resolução explícita da GPUVA
do Slot 1 pelo snapshot comitado; Segmento 1; blit com `RPTR == WPTR`; fence GPU
substituindo poison; comparação byte a byte origem/destino; desktop visível por
10 minutos sem TDR. Teto: uma corrida após boot frio. Qualquer requisito ausente
é FAIL e exige abandono da corrida.

Depois de dez minutos com o desktop visível e o driver ainda em Código 0, a
aceitação é feita por `tools\confirm-p5-dwm-backing-tripod.ps1`; o script recusa
qualquer evidência sem ACK, Slot 1, tripé, comparação integral ou confirmação
visual e grava `p5-dwm-backing-tripod-result.json` na pasta da corrida.

**NÃO VALIDADO EM HARDWARE**.
