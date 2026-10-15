# 650 — RLC.1b (handshake RLC↔SMU desligado) e LOGMIN.1 (log mínimo da UMD); critério da Corrida 248

**Escrito antes do ensaio (Lei 10), 08/10/2026, offline.** Continua o [doc 649](649_CORRIDA247_RLC_RODANDO_SOBRE_A_G1_108S_REAIS_DESFECHO_A_COM_KD.md). **Duas mudanças juntas, por decisão do Daniel** (como na 244): se o desfecho mudar, a atribuição exige separar depois.

## 1. Mudanças

### RLC.1b
- **Upstream (P1):** `gfx_v10_0_rlc_start` chama `gfx_v10_0_rlc_smu_handshake_cntl(adev, enable)`, com `enable` = GFXOFF na `pp_feature_mask`. Com `enable=false`, o bit 23 de `RLC_PG_CNTL` vai a 1 (handshake desligado).
- **Nós:** não falamos com o SMU e não fazemos GFXOFF, então o ramo que vale é `enable=false`. Até a 247 o bit ficava em 0 (handshake ligado), com o firmware do RLC rodando e esperando um SMU que ninguém do nosso lado conduz (H do doc 644 §3).
- **Código:** `BC250_RLC1B_SMU_HANDSHAKE_OFF 1` em `bc250_active_miniport.c`. No `Bc250ActiveRlc1Resume`, depois do `PG_CNTL=0` e antes de ligar o F32, RMW do bit 23 a 1 (Lei 6). A prova passa a exigir o bit 23 lido em 1 e 6/6 escritas; sem prova, o RLC para e tudo volta (Lei 5). Vale também no `rlc_resume` do pós-reset do GFXRST.1.

### LOGMIN.1
- **Medido na 247 (P0):** o `dwm.exe` gravou ~23 MB em ~2 min (`BC250_UMD_LOG_1292.txt` + `-RADV.txt`, que recebe as mesmas linhas pelo stderr redirecionado), mais ~8 MB dos outros processos. A massa são linhas de estado por draw (`VB Slot`, `PsSetShaderResources`, `PsSetSamplers`, `DRAW_VERTS`...). Cada linha também vai ao `OutputDebugStringA`, que com o KD ligado atravessa o KDNET.
- **Código:** `BC250_UMD_TRACE_MINIMAL 1` em `bc250_wddm_winsys.c`, no sink único `Bc250WinsysTraceRaw`, antes de qualquer saída. As primeiras 400 linhas de cada processo saem inteiras; depois, só as linhas com marcador de falha ou ciclo de vida (`RECUS`, `FALH`, `TIMEOUT`, `LOST`, `hr=0x8`, `Present`, `CreateDevice`...), mais um `[LOGMIN] N linhas rotineiras suprimidas` a cada 20.000. `BC250_TRACE_FULL=1` restaura o log integral; o `build-umd.bat` usa isso nas suítes offline.
- **Não muda:** o log do KMD, o rastro de paginação e as linhas da RADV que não passam pelo nosso sink.

| Artefato | Valor |
|---|---|
| Pacote | **3.392.1.0** |
| SYS lab | `018D19F0…` |
| SYS assinado | `5EB4F626…` |
| INF | `6CE3F837…` |
| CAT | `EC12639E…` |
| UMD | `B73F9BDA…` (nova, LOGMIN.1) |
| RADV | `B4AE2E14` (a mesma) |
| Auditorias | pacote PASS; segurança PASS com o portão novo `audit_rlc1b_and_logmin` (4 mutações reprovam) |
| Pacote 3.391 (247) guardado em | `driver\wdk\package\output_lab_3.391.1.0_247\` |
| Lançadores | `tools\EXECUTAR_CORRIDA248_RLC1B_LOGMIN_COM_KD_MENU_600S.cmd` (com WinDbg) e `..._MENU_600S.cmd` (sem) |

Roteiro igual: só o menu Iniciar até o rollback. Pausas do KD são descontadas como no doc 648 §4.

## 2. Pré-condição de validade

1. `[G1_GOLDEN] RESUMO ... ARMADO`.
2. `[RLC1] boot (depois do PSP): ATIVO` com `EXEC_ROM_CODE=0`, `(handshake SMU DESLIGADO (RLC.1b))` e `escritas=6/6`.
3. `rptr_por_doorbell=0`.
4. LOGMIN ativo: linha `[LOGMIN]` em algum log de UMD, ou o log do `dwm.exe` abaixo de **2 MB por minuto** de corrida (247: ~10 MB/min).

## 3. Desfechos (tempo = execução real, descontadas as pausas do KD)

Referência: 245 ≈ 289 s, 246b ≈ 228 s, 247 ≈ 108 s; maior tempo histórico com trava, 584 s.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | Pré-condição OK e **600 s sem trava** (rollback do watchdog) | Primeira vez com o menu e a base atual. Uma das duas é candidata; repetir e depois separar (248b só RLC.1b, 248c só LOGMIN.1) |
| B | Pré-condição OK e trava entre **60 s e 584 s** | Nenhuma das duas é causa suficiente; ficam por paridade/higiene. Próximo: paginação em pedaços |
| B1 | Pré-condição OK e trava em **menos de 60 s** | Possível regressão; separar as duas antes de seguir |
| C | `[RLC1] RECUSADO` (bit 23 não fica) ou LOGMIN inativo | O ensaio não aconteceu como desenhado; registrar o motivo |
| D | Regressão no boot: StartDevice, PFP direct load ou desktop | Desligar e investigar |
| T (soma) | TDR do GFX | Ler `[GFXRST1]` e `[RLC1] pos-reset` (doc 642 §4) |
| E | Fora do mapa | Descrever antes de julgar |

**Teto:** 1 corrida.

**NÃO VALIDADO EM HARDWARE:** tudo deste doc.
