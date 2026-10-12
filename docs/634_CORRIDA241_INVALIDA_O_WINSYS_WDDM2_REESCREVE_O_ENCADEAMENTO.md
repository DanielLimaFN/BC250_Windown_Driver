# 634 — Corrida 241: inválida; o winsys `wddm2` reescreve o encadeamento GFX

**07/10/2026, 10:04:59–10:15:14.** Lab 3.385.1.0 (SYS `40916F50…`), RADV `DFD8D120…` (hash conferido em `binding-provenance.json` e no disco). Evidência: `docs/evidence/2026-10-07_10-04-59-DESKTOP-HQAG6T4-lab-binding/`. Critério: [doc 633](633_ENCADEAMENTO_GFX_DA_RADV_COMO_O_UPSTREAM_E_CRITERIO_CORRIDA241.md).

## 1. Pré-condição de validade: falhou (P0)

O preâmbulo do menu (`StartMenuExperienceHost` pid 7148, submit 3, VA `0x2407D4000`, 184 dw, `layout=CHAIN`) termina com `INDIRECT_BUFFER` controle **`0x075040D0`**: bit 22, VMID 7, sem VALID. É o mesmo padrão da 240.

Causa, por leitura de fonte: `mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_cs.c:1484-1490`, no fallback de encadeamento do submit. Depois de `radv_winsys_cs_chain`, o código faz `&= ~S_3F3_VALID(1)` e `|= (1u << 22) | (7u << 24)` para GFX. A reversão do doc 633 no header foi sobrescrita ali. Defeito meu: procurei os emissores só em `radv_winsys_cs.h`. O comentário local ("bit 23 é RESERVADO, estritamente 0") não tem fonte nem doc que o sustente.

Os despejos dos AppContainers não entram na coleta comum, só na pós-trava. Foram copiados para `gfx-ib-dump-appcontainer/` (Search 55, menu 17, CBS 5 arquivos da janela da corrida).

## 2. O que foi observado

- 600 s, rollback online limpo (`RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em 5 amostras); zero `TDR_GFX_QUIESCE`, `GFXHUB_FAULT`, `IB_TRIPOD TIMEOUT` e BugCheck.
- O roteiro não foi o da 239/240. Menu e Pesquisa só tiveram atividade entre 10:07 e 10:08: um device do menu, 4 Presents e o preâmbulo executado sem TDR. Depois, só o Explorer até o rollback.
- Na 239/240 o TDR veio aos ~7–9 min de ciclos de menu. Esta corrida não chegou a essa janela, então a ausência de TDR não informa sobre o encadeamento.

## 3. Veredito

**Inválida** (pré-condição do doc 633 §2) e roteiro incompleto. Não conta no teto de 2 corridas do par. É mais uma corrida de uso leve sem trava dura (como a 238), sem atribuição.

## 4. Próximo (241b)

1. Tirar o override do ramo GFX em `radv_wddm2_cs.c`, deixando o controle de `radv_winsys_cs_chain` (upstream `CHAIN | VALID | PRE_ENA`). O ramo de compute fica como está.
2. Procurar qualquer outro emissor de `0x3F3` no winsys.
3. Recompilar, e no preflight conferir o controle por despejo de um teste local antes do bind, se possível.
4. Na coleta comum, incluir os despejos dos AppContainers.
5. Roteiro: abrir e fechar o menu a cada ~30 s **até o rollback**.

**NÃO VALIDADO EM HARDWARE:** o encadeamento upstream no GFX.
