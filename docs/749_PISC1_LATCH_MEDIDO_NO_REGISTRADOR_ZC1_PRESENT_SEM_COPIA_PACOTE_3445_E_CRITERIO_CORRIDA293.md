# Doc 749 — PISC.1 (latch medido no registrador) e ZC.1 (Present sem cópia), pacote 3.445.1.0 e critério da Corrida 293

> **Data:** 14/10/2026, offline. Base: Corrida 292c ([doc 748](748_CORRIDA292C_GART_DE_32MB_NO_AR_COMPOSICAO_NUMA_COPIA_SO_DESFECHO_A_E_O_QUE_SOBRA.md)).
> **Pedido do Daniel:** passos 1, 2 e 3 da checklist juntos.

## 1. Passo 1, PISC.1: o balde "latch 3+ quadros" era erro de contagem (P1 pelo código)

`Bc250DcnFlipPollLatch` calculava quantos quadros o latch levou subtraindo dois números de fontes diferentes:
- o quadro do OTG **lido no registrador** na hora do pedido de flip;
- o `g_Bc250DisplayState.LastOtgFrameCount`, que **só é atualizado no VSYNC**.

Hoje 99,8% dos latches são resolvidos pela interrupção do flip, que pode chegar antes do VSYNC daquele quadro. Nesse caso o valor guardado fica um quadro atrás, a diferença mascarada dá a volta (cerca de 2²⁴) e cai no balde "3+". Isso explica os 364 "3+" da 292c convivendo com só 1 latch acima de 50 ms.

**A correção:**
- o quadro do latch passa a ser lido no registrador `OTG0_STATUS_FRAME_COUNT`, o mesmo já lido em todo flip;
- `[PISC1_LATCH] CONTADOR_ATRASADO` conta os casos que o cálculo antigo jogava no "3+" e o novo não joga;
- `[PISC1_LATCH] TARDIO` registra os latches tardios de verdade (2 quadros ou mais, ou 33,4 ms ou mais), com quadro, tempo e endereço;
- o resumo `[DCN1_RESUMO]` ganhou `PISC1 contador_atrasado=… tardios_reais=…`.

## 2. Passos 2 e 3, ZC.1: render target apresentável sem cópia

### O que a 292c mediu (P0)

Os backbuffers do DWM são as próprias **primárias** que o MPO3 do DCN mostra (os DstVa da composição são as alocações PRIMARY). Mesmo assim, o render target delas tinha **memória própria da RADV**, no segmento 2. A recusa vinha da regra criada na Corrida 190 (`FRENTE_B NAO … render target fica com memoria propria`).

Por isso cada quadro passava por:
1. `vkCmdCopyImageToBuffer`: imagem para um buffer linear de 11 MB da RADV, também no segmento 2;
2. a CPU esperando o GFX terminar;
3. o escape do KMD copiando o buffer linear para a primária pela janela da GART, com 2.700 páginas de sistema.

São duas cópias de 11 MB e duas esperas por quadro.

### ZC.1

- **Onde o alias passa a valer:** em render target **apresentável** do próprio processo, que é:
  - a primária do swapchain (chave `BC250_UMD_ZC1_PRIMARY_RT_ALIAS`);
  - ou a superfície compartilhada que o DWM compõe (chave `BC250_UMD_ZC1_SHARED_RT_ALIAS`).
- **Como:** a `VkImage` LINEAR é criada sobre a própria alocação WDDM, pelo mesmo caminho da Frente B. A RADV ainda confere pitch, offset e tamanho.
  - A primária fica no segmento 1, a VRAM.
  - A compartilhada prefere a VRAM (APU.2c).
- **No Present:** o render pass é fechado, uma barreira torna as escritas visíveis e o flush espera a GPU, e então o quadro vai direto ao `pfnPresentCb`. Não há cópia nenhuma; aparece a linha `[ZC1_PRESENT]`.
- **O que continua como antes:**
  - render targets não apresentáveis (regra da 190);
  - texturas próprias (regras das 191/192);
  - superfícies abertas de outro processo (adoção).
- **Rotação de identidade:** `ZeroCopyTarget` passou a girar junto com a alocação em `RotateResourceIdentities`. Sem isso, um buffer sem alias seria apresentado como se tivesse.

**Risco declarado:** na Corrida 190, com o alias em todo render target do DWM, a tela ficou preta fora do que era redesenhado. O mecanismo nunca foi medido (H). Desde então entraram a rotação do vínculo de alias (DX.28 já girava a imagem; a Frente B gira o alias) e muitas correções do KMD. Se a 190 voltar, cada chave desliga a sua metade sozinha.

**O que sobra:** a espera da CPU pelo GFX antes do flip continua (flush com fence). Tirá-la exige que o flip espere a fence da RADV, que é o próximo passo.

## 3. Build e pacote

- `build-umd` EXIT=0 (teste `Frente B alias policy` PASS, com os casos novos: apresentável aceito, não apresentável recusado, contrapontos de depth e mips).
- `build-contract` EXIT=0; build de laboratório OK.
- Auditoria de segurança PASS, com 5 portões novos do ZC.1; auditoria do pacote PASS.
- Strings conferidas nos binários: `PISC1_LATCH` no SYS e `ZC1_PRESENT` na UMD.
- **Lab 3.445.1.0:**

| Arquivo | Hash |
|---|---|
| SYS | `64D0E959…` |
| UMD | `9F65C18A…` (nova) |
| INF | `964A2E3B…` |
| CAT | `50385CAA…` |
| RADV | `B4AE2E14` (igual) |

- **Roteiro:** `tools/EXECUTAR_CORRIDA293_ZC1_ZERO_COPIA_300S.cmd` (preflight `tools/prepare-corrida293-zc1-zero-copia.ps1`). Antes: desligar e ligar.

## 4. Critério da Corrida 293

| Desfecho | Assinatura no log | Implicação |
|---|---|---|
| **A** | `FRENTE_B SIM_ZC1_PRIMARIA` no dwm.exe; `[ZC1_PRESENT]` crescendo; composições do bridge (`COMPOSITE_BLT OK VIA_VA` com `SrcVa=…(janela)`) perto de zero no DWM; desktop inteiro visível; zero TDR; `PISC1 contador_atrasado` > 0 e `tardios_reais` pequeno | zero cópia validado; menu e piscadas avaliados pelo Daniel |
| **B** | desktop com regiões pretas fora do que é redesenhado (a 190 de volta) | desligar `BC250_UMD_ZC1_SHARED_RT_ALIAS` primeiro, depois a primária; medir o mecanismo |
| **C** | nenhum `SIM_ZC1` (a RADV recusou o layout ou o formato linear) | ler o motivo na linha `FRENTE_B NAO` |
| **D** | TDR ou fault de GFX ao desenhar em imagem linear | o GFX10 com alvo linear nesta RADV precisa de outra regra; voltar à cópia |
| **E** | algo fora do mapa | descrever antes de julgar |

Para as piscadas: se `tardios_reais` for quase zero e a tela ainda piscar, a causa não é o latch, e os próximos suspeitos são os VSYNC perdidos (D06) e os remapeamentos.

**NÃO VALIDADO EM HARDWARE.**
