# 642 — RLC.1 (RLC rodando) e GFXRST.1 (reset do GFX) como o amdgpu; critério da Corrida 244

**Escrito antes do ensaio (Lei 10), 07/10/2026, offline.** Continua o [doc 641](641_CORRIDA243_MEC1_ARMADA_E_TRAVA_DURA_AOS_49S_DESFECHO_B.md) §4 e o [doc 630](630_MINERACAO_DOS_TDR_DO_GFX_SQ_ERROS_DEPOIS_DO_RESET_E_RLC_SEM_SAFE_MODE.md) §6.2.

**Decisão do Daniel (07/10):** "estamos muito devagar". O RLC e o reset do GFX entram juntos nesta corrida, e a P3.3 sai na mesma corrida. São três mudanças de uma vez, **contra a regra de uma variável por corrida**. A divergência está declarada; cada uma tem chave própria para bissecção.

## 1. O que a leitura achou (P0/P1)

- **O RLC nunca rodou o firmware em regime.**
  - Desde 28/08 a carga do `cyan_skillfish2_rlc.bin` pelo PSP estava desligada (commit `55efed3`, "desacoplamento do gate de RLC em disco", porque o arquivo não estava instalado).
  - O carregador `Bc250ActiveLoadRlcFirmware` ficou no código sem chamador.
  - Mesmo quando rodava (23–27/08, doc 01: `EXEC_ROM_CODE` 1→0, `RLC_GPM_STAT=0x00B40016` igual ao Linux), ele **devolvia `RLC_CNTL` a 0 no fim**.
  - Nas Corridas 240, 241b e 243, `RLC_CNTL=0` em regime (P0), e o `pfp_direct_load` liga e desliga o F32 só durante a carga do CP.
  - O arquivo hoje está em `System32\drivers` com o hash esperado (`20ACEFDB…`), que é o mesmo firmware que o Linux da placa carregou (RLC `0xd`, doc 552).
- **amdgpu no Cyan Skillfish 2 (P1, `referencias/linux_upstream_2026-09-29/gfx_v10_0.c`):**
  - O firmware é carregado pelo PSP, sem autoload.
  - `gfx_v10_0_rlc_resume` faz: `rlc_stop`, `RLC_CGCG_CGLS_CTRL=0`, `RLC_PG_CNTL=0`, `init_csb`, `RLC_SPM_VMID=0xF` e `rlc_start` (handshake com o SMU ligado e `ENABLE_F32=1`).
  - Isso acontece antes do `cp_resume`.
  - Para a GC 10.1.3, `cg_flags = pg_flags = 0` (`nv.c:828`).
- **O reset do amdgpu é `gfx_v10_0_soft_reset`:**
  - A máscara sai do `GRBM_STATUS`/`GRBM_STATUS2`: CP+GFX com o pipeline ocupado, CP com o CP ocupado, RLC com o RLC ocupado; nada ocupado significa sem reset.
  - Depois vêm `rlc_stop`, halt de ME/PFP/CE e de MEC1/MEC2, e um pulso de 50 µs.
  - **Não há safe mode e não há CPF/CPG.**
  - O nosso reset era diferente: safe mode, que nunca confirmou (0 de 41); CP+GFX+CPF+CPG sempre; e o MEC sem halt.
- **Achado lateral, fora desta corrida:**
  - O snapshot DX.26 da 243 mostra **30 de 34 golden settings do Cyan Skillfish diferentes do upstream**, porque nunca foram aplicados. Exemplos: `GL2C_ADDR_MATCH_MASK`, `GL2_PIPE_STEER_*`, `CH_PIPE_STEER`, `TA_CNTL_AUX`, `UTCL1_CTRL`.
  - O amdgpu os aplica no `hw_init`, antes do RLC.
  - A tabela local confere com `golden_settings_gc_10_0_cyan_skillfish`.
  - É o próximo item de paridade (G.1). Não entrou aqui.

## 2. Mudanças

| Chave | Valor | O que faz |
|---|---|---|
| `BC250_P33_IH_RPTR_BY_DOORBELL` | **0** | RPTR do IH por MMIO, como até a 241b (doc 641) |
| `BC250_MEC1_DOORBELL_RANGE_LIKE_AMDGPU` | 1 | igual à 243 |
| `BC250_RLC1_RUNNING_LIKE_AMDGPU` | **1** | No trecho do PSP, depois do SDMA1 e antes do PFP direct load: carga do RLC_G pelo PSP (tipo 8, 25.088 bytes) e `Bc250ActiveRlc1Resume` (sequência do `rlc_resume`). A prova é `ENABLE_F32=1`, `EXEC_ROM_CODE=0`, CGCG=0 e PG=0 lidos. Sem prova: o RLC para e CG/PG/SPM voltam. Desarme no StopDevice (depois do CP gráfico) e no teardown da GART |
| `BC250_GFXRST1_LIKE_AMDGPU` | **1** | O quiesce do TDR do GFX chama `Bc250ActiveGfxRst1SoftReset` (`gfx_v10_0_soft_reset`). No restart, `Bc250ActiveGfxRst1PostReset` refaz o `rlc_resume` e devolve o `CP_MEC_CNTL` antes de rearmar o CP_RB0 |

- **Transporte:**
  - `RLC_PG_CNTL` e `RLC_SPM_MC_CNTL` entraram na lista de leitura.
  - `RLC_CGCG_CGLS_CTRL`, `RLC_PG_CNTL` e `RLC_SPM_MC_CNTL` entraram na de escrita.
  - O teste do transporte que congelava "PG nunca é escrito" foi atualizado; as máscaras de WGP continuam só de leitura.
- **Auditoria:**
  - O portão novo `audit_rlc1_gfxrst1_like_amdgpu` dá PASS.
  - 6 mutações reprovam:
    1. prova sem `EXEC_ROM`;
    2. safe mode no reset novo;
    3. CPF no reset novo;
    4. desarme fora do StopDevice;
    5. post-reset removido;
    6. `PG_CNTL` escrito fora.
  - O portão I.SDMA0 deixou de exigir o CGCG só de leitura.
- **Diferenças declaradas com o amdgpu:**
  - O CSIB continua com o escritor único DX.27, mais tarde no bring-up. O amdgpu o escreve antes do `rlc_start`.
  - No pós-reset não há recarga pelo PSP: depois da GART do MMHUB o PSP não pode ser usado, doc 612. O amdgpu recarrega no resume de um reset completo.
  - O MEC só volta ao `CP_MEC_CNTL` de antes; a fila HQD segue o caminho de restart que já existia.

| Artefato | Valor |
|---|---|
| Pacote | **3.388.1.0** |
| SYS lab (fonte) | `AB8476E1…` |
| SYS assinado | `DABCBD0C…` |
| INF | `20C437DA…` |
| CAT | `11D4DA10…` |
| UMD | `8782A5E5…` (a mesma) |
| RADV | `B4AE2E14` (a mesma) |
| Firmware do RLC | `20ACEFDB…` (no pacote e em `System32\drivers`) |
| Auditoria do pacote | PASS |
| Pacote 3.387 (243) guardado em | `driver\wdk\package\output_lab_3.387.1.0_243\` |
| Script | `tools\EXECUTAR_CORRIDA244_RLC1_GFXRST1_MENU_600S.cmd` |

O roteiro é o mesmo da 239–243: só o menu, abrindo e fechando até o rollback.

## 3. Pré-condição de validade

O log mostra:
1. `[RLC1] antes do PSP: ... EXEC_ROM_CODE=1`;
2. `[RLC1] PSP tipo 8 (RLC_G ...) Status=0 PhysicalReady=1`;
3. `[RLC1] boot (depois do PSP): ATIVO` com `EXEC_ROM_CODE=0`;
4. `[RLC_ESTADO] boot` com `RLC_ENABLE_F32=1`;
5. nenhuma linha `[P3_3_IH_DOORBELL] ATIVO`.

Sem 1 a 4, a corrida responde só à pergunta da P3.3 (desfecho C).

## 4. Desfechos

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | Pré-condição OK, desktop e menu, **600 s sem trava dura** e rollback limpo | Sem a P3.3 e com o RLC rodando, sem trava. Não separa RLC de acaso (a 222 e a 226 também passaram 600 s). Exige repetição; depois, bissecção pela chave do RLC |
| B | Pré-condição OK e trava dura com a assinatura de sempre **depois de 60 s** | A trava precoce era da P3.3 (confirmado). O RLC rodando não é causa suficiente da trava de sempre. Próximo: G.1 (golden settings) |
| B1 | Pré-condição OK e trava dura **antes de 60 s** | Com a P3.3 desligada, a trava precoce não sumiu: a P3.3 não era a única via, ou o RLC rodando encurta a vida da máquina. Repetir com `BC250_RLC1_RUNNING_LIKE_AMDGPU 0` |
| C | `[RLC1] ... RECUSADO`, `NAO TENTADO` ou PSP com `Status != 0` | O RLC não rodou; a corrida responde só à P3.3 (pela regra de B/B1). Registrar o motivo |
| D | Regressão: Código 43 no StartDevice, PFP direct load ou CP sem prova, desktop que não sobe, BugCheck | O RLC rodando quebra o bring-up. A chave volta a 0 e a corrida se repete sem ela |
| T (soma aos outros) | Houve TDR do GFX. Aparecem `[GFXRST1] #n ... mascara=`, `[RLC1] pos-reset do GFX: ATIVO`, `[GFXRST1] pos-reset: CP_MEC_CNTL devolvido` e `TDR_GFX_RESTART_PROOF PASS`; contam-se `[P3_IH] ... outros=` depois do reset | Com `outros` = 0 depois do reset (a 239 teve 992), o reset do amdgpu acaba com os erros do SQ. Com `outros` > 0, o estado perdido não é só RLC/CP: provavelmente os goldens (G.1). **Sem TDR, a GFXRST.1 fica NÃO VALIDADA EM HARDWARE** |
| E | Fora do mapa | Descrever antes de julgar |

**Teto:** 2 corridas. Uma repetição só se der A ou B1.

**NÃO VALIDADO EM HARDWARE:** tudo deste doc.
