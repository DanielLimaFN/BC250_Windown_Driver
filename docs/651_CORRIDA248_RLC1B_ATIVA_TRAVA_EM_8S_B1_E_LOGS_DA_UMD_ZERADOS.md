# 651 — Corrida 248: RLC.1b ativa, trava em ~8 s (B1), logs da UMD zerados

**08/10/2026.** Bind às 09:07:46, trava em segundos, boot seguinte às 09:08:44. Pacote 3.392.1.0 (SYS assinado `5EB4F626…`, UMD `B73F9BDA…`) + RADV `B4AE2E14`, com KD.
Evidência: `docs/evidence/2026-10-08_09-07-46-DESKTOP-HQAG6T4-lab-binding/`. O log do KMD foi recuperado à mão do cumulativo pelo offset do baseline (`display-log-this-bind-RECUPERADO.txt`, 31.866 linhas até o boot seguinte; SHA-256 em `SHA256-recuperados.txt`); a coleta automática ainda não tinha rodado. Critério: [doc 650](650_RLC1B_HANDSHAKE_SMU_DESLIGADO_E_LOGMIN1_LOG_MINIMO_DA_UMD_CRITERIO_CORRIDA248.md).

## 1. Medido (P0)

| Item | Medido |
|---|---|
| G.1 | `29 aplicados, 4 ja' iguais, 1 divergentes/restaurados -> ARMADO` |
| RLC.1 + RLC.1b | `ATIVO`, `EXEC_ROM_CODE=0`, `RLC_PG_CNTL=0x00800000 (handshake SMU DESLIGADO (RLC.1b))`, `escritas=6/6` |
| P3.3 | `rptr_por_doorbell=0` |
| Duração | **~8,3 s** do primeiro registro com QPC ao último; nenhum salto acima de 1 s (sem pausa do KD) |
| Atividade | 1.985 IBs, 186 Presents, 76 submissões do GFX, composição `SAMPLED_PASS` |
| Erros | zero TDR, zero fault, zero timeout |
| Última linha | paginação op 12, Present #55 (flip) e submissão SDMA normais: a assinatura de sempre |
| Boot seguinte | fusível disparado, Código 43 |

**Logs da UMD perdidos:** as seis cópias da pasta de evidência (feitas ~5 s depois do bind) têm o tamanho certo e **só zeros**. O tamanho foi ao disco e os dados ficaram no cache quando a máquina morreu. Por isso a pré-condição 4 (LOGMIN ativo) **não pode ser conferida** nesta corrida. Os originais em `C:\Windows\Temp` não são legíveis desta sessão.

## 2. Veredito

**Desfecho B1** (pré-condições 1–3 OK, a 4 não verificável): trava em ~8 s, a mais rápida já medida (antes: P3.3 em 11–49 s, 244 em ~40 s, 246 em ~33 s). Pela regra do doc 650, as duas mudanças precisam ser separadas antes de seguir.

**Leitura (H):** das duas, só a RLC.1b mexe no hardware; a LOGMIN.1 só tira escrita de arquivo e de `OutputDebugStringA`. A suspeita principal é a RLC.1b, mas com n=1 e a dispersão conhecida isso não está provado.

## 3. Próximo

- **248b:** o mesmo pacote com a RLC.1b desligada (`BC250_RLC1B_SMU_HANDSHAKE_OFF 0`) e a LOGMIN.1 ligada. Passar de 60 s aponta a RLC.1b; travar cedo de novo aponta a LOGMIN.1 ou dispersão.
- **Coleta:** o binder precisa forçar as cópias da UMD ao disco (`FlushFileBuffers`/`[IO.File]` com write-through) logo depois de copiar, senão toda trava rápida zera os snapshots.

**NÃO VALIDADO EM HARDWARE:** efeito da LOGMIN.1; causa da trava dura; GFXRST.1.

## 4. Corrida 248b pronta (critério, escrito antes)

Pacote **3.393.1.0** = 3.392.1.0 com `BC250_RLC1B_SMU_HANDSHAKE_OFF 0` (bit 23 a 0, como a 247); LOGMIN.1, G.1 e RLC.1 iguais. SYS lab `EA598B24…`, SYS assinado `B71F505C…`, INF `D9CBAA23…`, CAT `BCC48B8B…`, UMD `B73F9BDA…` (a mesma da 248). Auditorias de pacote e de segurança PASS. Pacote 3.392 guardado em `driver\wdk\package\output_lab_3.392.1.0_248\`.

Binder: as cópias de log da UMD agora fazem `Flush($true)` (os snapshots zerados da 248).

Lançadores: `tools\EXECUTAR_CORRIDA248B_SEM_RLC1B_LOGMIN_COM_KD_MENU_600S.cmd` (com WinDbg) e `..._MENU_600S.cmd`.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | `[RLC1] ATIVO` com `handshake SMU ligado`, `escritas=5/5`, e mais de 60 s reais | A trava em 8 s veio da RLC.1b: ela sai (com a `pp_feature_mask` padrão do amdgpu, 0xfff7bfff, o GFXOFF está na máscara e o `rlc_start` deixa o handshake ligado; P1) |
| B | Mesma pré-condição e trava em menos de 60 s | A RLC.1b não é a causa da 248; LOGMIN.1 ou dispersão. Repetir a 247 (3.391) ou tirar a LOGMIN.1 |
| C | `[RLC1] RECUSADO` | Ensaio não aconteceu |
| E | Fora do mapa | Descrever antes de julgar |
