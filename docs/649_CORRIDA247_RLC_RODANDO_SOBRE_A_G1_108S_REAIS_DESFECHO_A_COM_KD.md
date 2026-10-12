# 649 — Corrida 247: RLC rodando sobre a G.1, ~108 s reais com o KD, desfecho A

**08/10/2026.** Bind às 08:18:10, trava por volta de 08:20:25, reinício pelo Daniel (boot 08:21:46, eventos 41/6008).
Pacote 3.391.1.0 (SYS assinado `58EEB2FB…`) + RADV `B4AE2E14`, **com KDNET** (lançador `EXECUTAR_CORRIDA247_RLC1_SOBRE_G1_COM_KD_MENU_600S.cmd`; opções de boot em `docs/evidence/vram-cmos/2026-10-08_08-17-35-corrida247kd-SystemStartOptions.txt`).
Evidência: `docs/evidence/2026-10-08_08-18-10-DESKTOP-HQAG6T4-lab-binding/` (log do KMD recuperado, 95.435 linhas até o boot seguinte). Critério: [doc 648](648_RLC1_SOBRE_A_G1_REPETICAO_DA_244_E_CRITERIO_CORRIDA247.md) §3 e §4.

## 1. Pré-condição (P0)

| Item | Medido |
|---|---|
| G.1 | `RESUMO: 34 registradores, 29 aplicados, 4 ja' iguais, 1 divergentes/restaurados -> ARMADO` |
| RLC.1 | PSP tipo 8 `Status=0`; `ATIVO`, `RLC_GPM_STAT=0x00B40016`, `EXEC_ROM_CODE=0`, escritas 5/5 |
| RLC no fim do StartDevice | `RLC_ENABLE_F32=1 (firmware do RLC rodando)` |
| P3.3 | `rptr_por_doorbell=0` |
| Pausas | 3 saltos de QPC (ver §2); pela §4 do doc 648 são descontados |

## 2. Tempo (P0, QPC a 10 MHz)

- Do primeiro registro com QPC ao último: **133,7 s**.
- Saltos sem nenhuma linha: **17,3 s** em t=68 s, **5,7 s** em t=115 s, **2,3 s** em t=127 s. Total ~25 s.
- **Tempo de execução real: ~108 s.**
- A trava veio **~4 s depois da última retomada** (na 246b, ~11 s).
- Nenhum log de UMD do WebView2 nesta corrida (só menu, Pesquisa, ShellExperienceHost, DWM e Explorer); a origem dos breaks não está no log.

## 3. Atividade (P0)

- 5.292 IBs executados, 958 Presents, composição com `SAMPLED_PASS` nos blts.
- IH 1:1 até o fim: ISR 6.080, `traps_sdma` 5.189, EOP 892, `outros=0`, zero overflow.
- Zero TDR, zero `GFXRST1`, zero fault, zero timeout.
- Última linha: paginação op 12 e submissão SDMA normais, a assinatura de sempre.
- Boot seguinte: fusível disparado (`State=2, Boots=2`), StartDevice recusado: Código 43.

## 4. Veredito

**Desfecho A** pela regra escrita antes: pré-condição OK e ~108 s reais, acima de 60 s. Com a 244 (~40 s) e a 247 (~108 s), o RLC rodando não mostra trava precoce consistente: a 244 fica como dispersão e a **RLC.1 fica ligada** por paridade.

**Ressalvas (Lei 1):**
- 108 s é o menor tempo "dos minutos"; está dentro da faixa histórica (45–584 s), mas abaixo de 245 (289 s) e 246b (228 s). Com n=2 não se separa efeito pequeno de dispersão.
- Pela segunda vez com o KD, a trava veio poucos segundos depois de uma pausa do depurador (4 s aqui, 11 s na 246b). As corridas sem KD também travam, então a pausa não é causa necessária; se ela antecipa a trava é **H**, não excluída.

**NÃO VALIDADO EM HARDWARE:** causa da trava dura; GFXRST.1 (nenhum TDR desde a 244); safe mode do RLC com o firmware rodando.
