# DX.45 — detector de reset silencioso do CP; critério da Corrida 88

> 23/09/2026. Faixa **offline**, sem bind. Resultado anterior:
> [Corrida 87](375_CORRIDA87_DX44_RESULTADO.md). Teto: **uma** corrida, depois de
> desligamento completo.

## 1. O que já se sabe antes da corrida

**P0 (Corrida 87):**
- entre os submits das fences 314 e 315, o RPTR do CP_RB0 caiu de 8188 para 5;
- o snapshot do TDR leu `CP_ME_CNTL=0x15000000` (CE, PFP e ME parados), que é
  o valor de power-on desta placa e o mesmo estado que um
  `GRBM_SOFT_RESET` do CP produz;
- a fence 314 nunca terminou;
- nenhuma linha nossa no intervalo.

**Escritores conhecidos (P0, leitura de código):**
- `CP_ME_CNTL` é escrito só no bring-up (`bc250_pfp_direct_load.c`,
  `bc250_gfx_ring_transaction.c`);
- `GRBM_SOFT_RESET` é escrito só no quiesce do TDR (logado como
  `[TDR_GFX_QUIESCE]`), no reset do SDMA0 (logado como
  `[SdmaPresent] SDMA0 Reset`, com máscara só do bit 23) e no bring-up do PFP;
- nenhum desses logs aparece entre os submits 314 e 315.

**Upstream (P1):**
- Linux v6.6 `nv.c` zera `cg_flags` e `pg_flags` para GC 10.1.3/10.1.4;
- o SMU do Cyan Skillfish (`cyan_skillfish_ppt.c`) não tem mensagem de GFXOFF.

**Histórico (P0):** `RLC_PG_CNTL=0x00000000` em 361.524 leituras das corridas
anteriores, ou seja, `GFX_POWER_GATING_ENABLE=0` (`gc_10_1_0_sh_mask.h:33515`).
**O power gating do RLC está desligado.**

**Hipóteses para o reset:**
- **H1**, power gating ou GFXOFF: **enfraquecida** pelos três fatos acima.
- **H2:** outro agente (firmware do CP, RLC, SMU ou PSP) resetou o CP.
- **H3:** um caminho nosso não logado. Os escritores foram auditados acima; um
  caminho indireto não foi excluído.

## 2. A mudança (somente leitura; nenhuma decisão muda)

Em `bc250_active_miniport.c`:
- **`Bc250ActiveGfxDx45Check`:**
  - lê `CP_ME_CNTL` (0x86D8) no **fim do poll**, fora do spinlock do anel, o
    que dá a janela temporal;
  - lê também **logo após cada submit**;
  - quando o CP está parado (máscara `0x15000000`) com o motor online, emite
    **uma** linha `[CP_RESET_DETECTED]` por transição, com teto de 16.
- **O que a linha traz:**
  - onde foi detectado (`POLL` ou `SUBMIT`);
  - os bits de halt e o RPTR anterior e atual;
  - WPTR, fence observada, submetidas e aposentadas;
  - `CP_STAT` (0x8680), `GRBM_STATUS` (0x8010), `RLC_CNTL` (0x3B000),
    `RLC_PG_CNTL` (0x3B10C) e `RLC_GPM_STAT` (0x3B9B8).
  - Todos são da `inc/linux_ref/gc_10_1_0_offset.h` (base 0 = 0x1260 DW, base
    1 = 0xA000 DW) e já estavam na allowlist de leitura.
- **Rearme:** quando o CP volta a rodar (por exemplo, depois do restart do
  TDR), o detector se rearma.
- **Sem falso positivo no nosso reset:** o quiesce do TDR marca
  `s_Bc250Dx45OwnQuiesce` antes do nosso próprio `GRBM_SOFT_RESET`, e o
  detector não se acusa nessa janela. É uma marca de telemetria; o fluxo não
  muda.
- **Amostragem:** `CP_RESET_DETECTED` fica isento da amostragem global.
- **Mantidas:** DX.43 (aquecimento de VMID) e DX.44 (preempção falante).

## 3. Verificação offline

- `build-contract` EXIT=0 e build lab EXIT=0. Os únicos avisos são de
  ferramentas user-mode antigas em `tools/`, fora do driver.
- Portão novo `audit_dx45_cp_reset_detector`, com
  `tools/test-dx45-cp-reset-detector-mutations.py` dando **8 de 8 mutações
  reprovadas**: fora do poll, fora do submit, detector escrevendo registrador,
  sem `RLC_PG_CNTL`, acusando o próprio quiesce, marca do quiesce removida,
  máscara trocada, linha amostrada.
- DX.43 7/7, DX.44 6/6 e DX.31 PASS.
- Auditorias de segurança, matriz DDI, callbacks e RADV: PASS.

## 4. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.249.1.0`, `GfxWaveProbe=1`. SYS `7F94EECB5097781E7E020D49959053EA8A5EA31FF8FAACD2E20F6F335EB5D4F7`, INF `C519EF38…C136`, CAT `D84B2D35…C7D9`. Auditoria do pacote PASS; strings DX.43, DX.44 e DX.45 no SYS empacotado. |
| UMD / RADV | inalteradas: `45BB28C3…`, DX.42 `FCAA11FE…` |
| Pré-voo | `tools/prepare-corrida88-dx45.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA88_DX45.cmd`, elevado, depois de **desligamento completo** (marcador armado no boot das 15:16:34) |

## 5. Critério, escrito antes

**Pergunta 1 — o reset silencioso reaparece, e em que estado está o RLC?**

| | Assinatura | Implicação |
|---|---|---|
| **A1** | `[CP_RESET_DETECTED]` fora do quiesce **e** (`RLC_PG_CNTL` bit 0 = 1 **ou** `RLC_GPM_STAT` com `GFX_POWER_STATUS`=0 ou `GFX3D_BLOCKS_CHANGING_POWER_STATE`=1) | Power gating/GFXOFF em ação (H1 volta). Próximo: desligá-lo como o Linux (sem PG) ou restaurar o CP depois do power-up. |
| **A2** | `[CP_RESET_DETECTED]` fora do quiesce **e** `RLC_PG_CNTL`=0 **e** `GFX_POWER_STATUS`=1 | Não é power gating (H1 refutada). Algum agente resetou o CP com o bloco ligado (H2/H3). Próximo: datar contra os logs do SDMA/MEC/PSP no mesmo intervalo e ler `GRBM_SOFT_RESET` no detector. |
| **B** | Nenhum `[CP_RESET_DETECTED]`, e todo snapshot de TDR com `CpMeCntl=0x00000000` | O reset não se repetiu nesta corrida (houve 1 em 4 corridas). Registrar a frequência; o detector segue para as próximas. |
| **C** | Nenhum `[CP_RESET_DETECTED]`, mas algum snapshot de TDR com `CpMeCntl≠0` antes do nosso quiesce | O **detector falhou**: o reset aconteceu e ele não viu. Defeito da instrumentação. |
| **D** | `[CP_RESET_DETECTED]` em rajada logo no bind, antes de qualquer submit gráfico, ou durante o nosso quiesce | Falso positivo. Os dados de reset desta corrida não valem. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Pergunta 2 — terceira amostra da DX.43:** contar trocas `Emitido=1` e TDRs
com ondas no VMID anterior. Zero mantém o desfecho A das 86/87.

**Pergunta 3 — preempção:** contar `[PREEMPT_COMMAND]` e a posição em relação
a cada TDR (antes/depois do travamento).

**Controles:** restart provado, sem BugCheck, coleta pós-rollback com JSON,
identidade de preâmbulo presente.

**Função:** harness (base: 16/60 na 87), desktop, pixels e device-lost.
Qualquer TDR ou menos de 60/60 é falha funcional.

Desfecho parcial não é desfecho. Exemplo: A2 sem a leitura de
`RLC_GPM_STAT` (`[0]`) não é A2.
