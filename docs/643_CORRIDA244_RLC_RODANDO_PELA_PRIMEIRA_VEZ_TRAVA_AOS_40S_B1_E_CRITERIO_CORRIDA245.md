# 643 — Corrida 244: RLC rodando o firmware pela primeira vez, trava aos ~40 s (desfecho B1); critério da Corrida 245

**07/10/2026.** Bind 22:10:32; UMD carregada às 22:10:41; trava cerca de 22:11:10; reinício pelo Daniel. Lab 3.388.1.0 (SYS assinado `DABCBD0C…`) + RADV `B4AE2E14`. Critério: [doc 642](642_RLC1_RLC_RODANDO_E_GFXRST1_RESET_DO_GFX_COMO_O_AMDGPU_E_CRITERIO_CORRIDA244.md).

**Evidência:**
- pasta `docs/evidence/2026-10-07_22-10-32-DESKTOP-HQAG6T4-lab-binding/`;
- log do KMD lido do cumulativo `C:\BC250_DISPLAY_LOG.txt` a partir do offset 536.715.389 (4.661.243 bytes).

**Lacuna:** a coleta automática pós-trava ainda não tinha gravado `display-log-this-bind-RECUPERADO.txt`, o RESET_REASON nem o teste de RAM quando este doc foi escrito.

## 1. Pré-condição: cumprida (P0)

```
[RLC1] antes do PSP: RLC_CNTL=0x00000000 RLC_GPM_STAT=0x00F40016 EXEC_ROM_CODE=1 (RLC na ROM: firmware nao carregado)
[RLC1] PSP tipo 8 (RLC_G, 25088 bytes): Status=0 PhysicalReady=1 Resposta=0x00000000 Fence=0x00000001/0x00000001
[RLC1] boot (depois do PSP): ATIVO -- RLC_CNTL=0x00000001 RLC_GPM_STAT=0x00B40016 (EXEC_ROM_CODE=0: firmware do RLC rodando) RLC_PG_CNTL=0x00000000 RLC_CGCG_CGLS_CTRL=0x00000000 RLC_SPM_MC_CNTL=0x0000000F escritas=5/5
[RLC_ESTADO] boot (fim do StartDevice): RLC_CNTL=0x00000001 RLC_ENABLE_F32=1 (firmware do RLC rodando)
```

- **É a primeira vez em regime que o RLC roda o firmware.**
- `RLC_GPM_STAT=0x00B40016` é igual ao do Linux (doc 01).
- O firmware deixava `RLC_PG_CNTL=0x8`; a RLC.1 o zerou, como o `rlc_resume`.
- O PFP direct load e o resto do bring-up passaram com o RLC rodando: StartDevice `0x0`. Não houve regressão D.
- A P3.3 está desligada: `rptr_por_doorbell=0 escritas_doorbell=0` em todas as linhas de regime.

## 2. Medido (P0)

- **Atividade:**
  - 2.202 IBs no SDMA0 (`IB EXECUTADO`);
  - 346 Presents;
  - IH 1:1 (última linha de regime: ISR 2.106, TRAPs 1.941, EOP 172:172, `outros=0`, zero overflow).
- **Falhas:** zero TDR (nenhum `TDR_GFX_QUIESCE`; a GFXRST.1 **não foi exercitada**), zero fault e zero timeout.
- **Fim do log:** atividade normal de desktop (Present #181, paginação op 12, IB de 8.544 bytes executado).
  - Do StartDevice ao último registro passaram ~35 s por QPC, o que dá ~40 s depois do bind (o mesmo cálculo do doc 641).
- **Boot seguinte:** fusível disparado e StartDevice recusado (`0xC0000182`). Placa em Código 43.

## 3. Veredito pelo critério

**Desfecho B1:** pré-condição OK e trava dura antes de 60 s.

| Corrida | P3.3 | MEC.1 | RLC rodando | Trava depois do bind |
|---|---|---|---|---|
| 241b | não | não | não | ~330 s |
| 242 / 242b | sim | não | não | ~11 / ~22 s |
| 243 | sim | sim | não | ~49 s |
| **244** | **não** | sim | **sim** | **~40 s** |

**Leitura:**
- Com a P3.3 desligada, a trava precoce **não sumiu**.
- Há duas leituras, e esta corrida não as separa:
  - (a) o RLC rodando também encurta a vida da máquina;
  - (b) a trava precoce desde a 242 tem outra causa comum, ou é acaso. A 236, sem P3.3 e sem RLC, travou aos ~45 s.
- A regra escrita antes (doc 642 §4, B1) manda repetir com `BC250_RLC1_RUNNING_LIKE_AMDGPU 0`.

## 4. Corrida 245 (escrito antes)

**Mudança:** só `BC250_RLC1_RUNNING_LIKE_AMDGPU` → **0**. O resto fica igual à 244: P3.3=0, MEC.1=1 e GFXRST.1=1 (este só age num TDR do GFX). Ou seja, é o 3.387 sem a P3.3 — a corrida de confirmação proposta antes.

| Artefato | Valor |
|---|---|
| Pacote | **3.389.1.0** |
| SYS lab | `AB3DB9F1…` |
| SYS assinado | `6623270E…` |
| INF | `794420E2…` |
| CAT | `1E770A13…` |
| UMD | `8782A5E5…` (a mesma) |
| RADV | `B4AE2E14` (a mesma) |
| Auditoria do pacote | PASS |
| Auditoria de segurança | PASS |
| Pacote 3.388 (244) guardado em | `driver\wdk\package\output_lab_3.388.1.0_244\` |
| Script | `tools\EXECUTAR_CORRIDA245_SEM_RLC1_MENU_600S.cmd` |

O roteiro é o mesmo: só o menu até o rollback.

**Pré-condição:**
- nenhuma linha `[RLC1] ... ATIVO`;
- `[RLC_ESTADO] boot` com `RLC_ENABLE_F32=0`;
- `rptr_por_doorbell=0`.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | Pré-condição OK e passa de **60 s** (trava depois disso ou 600 s) | O RLC rodando encurtou a vida da máquina na 244 (n=1 cada). Próximo: entender por quê antes de religar. Suspeitas a ler: o handshake RLC↔SMU (`RLC_PG_CNTL` bit 23 em 0, como o upstream com GFXOFF, mas sem o SMU fazendo a parte dele) e os goldens não aplicados (G.1) |
| B | Pré-condição OK e trava **antes de 60 s** | A trava precoce não é do RLC nem da P3.3. Ela vem de algo comum às 242–245 ou é dispersão (a 236 travou aos ~45 s). Próximo: o G.1, e a RLC.1 pode voltar |
| C | Pré-condição falha (RLC armado, P3.3 ativa) | Pacote errado; registrar |
| E | Fora do mapa | Descrever antes de julgar |

**Teto:** 1 corrida.

**NÃO VALIDADO EM HARDWARE:**
- a causa da trava dura;
- o efeito do RLC rodando na trava;
- a GFXRST.1, porque não houve TDR.
