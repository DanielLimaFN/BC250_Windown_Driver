# Handoff 13/09/2026 — corrida 12:50, watchdog absoluto e blt retangular

## Veredito

A corrida física do candidato 3.169.1.0 trouxe avanço real. O escape atravessou
o dxgkrnl e o KMD executou 60 cópias de composição no SDMA, com 60
`COMPOSITE_BLT OK`, zero TDR e zero BugCheck. O bloqueio seguinte ficou
isolado: `DrawIndexed` pede uma cópia 1:1 de uma superfície 1280x1040 com
pitch 5120 para um destino 2560x1080 com pitch 10240. O código aceitava apenas
linhas inteiras com pitch igual e recusava essa geometria.

A tela continuou preta e as três amostras do Present continham somente zero.
Assim, a corrida prova o transporte UMD -> escape -> KMD -> SDMA, mas não prova
pixels úteis nem rasterização 3D.

Separadamente, o watchdog antigo não iniciou o rollback no prazo. O processo
do guardião entrou em consultas/coleta potencialmente ilimitadas antes de
publicar `ROLLBACK_STARTED` e chamar a recuperação. O reboot também reiniciava
o cronômetro relativo. A recuperação manual restaurou BasicDisplay em Código
0; logo, a falha foi do mecanismo de acionamento, não do script de recuperação.

## Evidência física da corrida 12:50

- 60 entradas UMD `BltViaKmd ENTER`;
- 60 opcodes de composição recebidos pelo KMD;
- 60 `COMPOSITE_BLT OK` por SDMA;
- 60 substituições de Draw por cópia 1:1, sem CPU;
- aproximadamente 9.149 IBs Node 0;
- 2.656 tripés completos e 6.493 provas de duas pernas;
- zero timeout, TDR e BugCheck;
- três amostras do Present com conteúdo zerado;
- recusa repetida da geometria 1280x1040, pitch 5120 -> 10240.

Evidência: `docs/evidence/2026-09-13_12-50-08-DESKTOP-HQAG6T4-lab-binding/`.

## Correção do watchdog — offline

O safe-bind agora grava `armed_at_utc_ticks` e `deadline_utc_ticks` e arma duas
tarefas SYSTEM: o observador e um executor independente do prazo absoluto. A
recuperação pós-prazo é serializada por mutex global; mutex abandonado conta
como propriedade adquirida. Depois do prazo, nenhuma consulta WMI, coleta de
eventos ou arquivamento antecede a chamada da recuperação. Consultas CIM que
restam antes do prazo têm timeout de dois segundos.

A confirmação visual recusa desarmar o rollback quando restam menos de 70
segundos. O teste offline do safe-bind passou, e a auditoria exige prazo
absoluto, tarefa independente, mutex e ausência de WMI no caminho crítico.

**Correção NÃO VALIDADA EM HARDWARE.**

## C3.18 — cópia retangular 1:1

O contrato UMD/KMD agora transporta `SourcePitch`, `DestPitch`, `DestX` e
`DestY`. Não há scaling, blend ou rasterização. O caso contíguo continua no
`COPY_LINEAR` já validado; pitches diferentes usam um único pacote SDMA 5.0
`COPY_LINEAR_SUB_WIND`, evitando milhares de pacotes por scanline.

As posições dos campos e o subopcode 4 foram transcritos do
`navi10_sdma_pkt_open.h` upstream. A interpretação de pitch, slice e retângulo
como contagens menos um é **HIPÓTESE (H)**: o cabeçalho gerado nomeia os
campos, mas não documenta essa semântica. Só a próxima corrida pode promovê-la.

A prova de dados é não destrutiva. Antes do SDMA, o KMD lê cinco pixels reais
distribuídos da origem; depois da fence, lê as coordenadas correspondentes no
destino usando o pitch próprio de cada superfície. Divergência falha fechada;
zero contra zero é rotulado inconclusivo; ao menos uma amostra não zero e todas
iguais produzem `SAMPLED_PASS`.

## Validação offline

- build-contract completo: PASS;
- build e testes da UMD: PASS;
- caso exato 1280x1040, 5120 -> 10240 congelado no teste de pacote;
- pitch desalinhado: recusado;
- auditoria de segurança: PASS;
- mutação do subopcode 4 para 5: FAIL esperado; restauração: PASS;
- teste offline do safe-bind: PASS;
- auditoria do pacote lab: PASS.

Zero instalação e zero execução física das correções.

## Candidato produzido — não instalado

- `DriverVer`: `09/13/2026,3.170.1.0`;
- perfil: `DISPLAY_RENDER_LAB`, WDDM 2.7;
- SYS: `516E977821DDA848197EEB0E98DCC653650319435319E7C8F0DA7D423D9160B1`;
- UMD: `31DF97DD1271A0DB25EBCDEA9EDB830B121711F31E0E0A1A458138E3B535B41A`;
- INF: `63A4D3E38815104349B5544CFABF0BAB65B1AD366238FDB44F0AC0391F0E0B62`;
- CAT: `8D1FFF19F7B8CBC483FF5F8100D729C613DB556614569917B7E05A1EA90E9605`.

**NÃO VALIDADO EM HARDWARE.**

## Critério da próxima corrida

Sucesso mínimo informativo:

1. `BLT_RECT_ENGINE_2LEGS` sem timeout/TDR;
2. `COMPOSITE_DATA_PROOF SAMPLED_PASS` ou `INCONCLUSIVO_ZERO` honesto;
3. `COMPOSITE_BLT OK RECT 1:1` para a geometria 1280x1040;
4. desktop visível antes do prazo absoluto; somente então confirmar.

Se a tela ficar preta, não confirmar. O executor de prazo deve publicar
`ROLLBACK_STARTED` e restaurar BasicDisplay no deadline absoluto.
