# 631 — Instrumentação do RLC, do IH e do IB do menu; critério da Corrida 240

**07/10/2026, offline.** Continua o [doc 630](630_MINERACAO_DOS_TDR_DO_GFX_SQ_ERROS_DEPOIS_DO_RESET_E_RLC_SEM_SAFE_MODE.md) §6, item 1.

## 1. Achado ao preparar (P1 por código + sondas P0 do bind)

`bc250_pfp_direct_load.c:1419`: o rollback do carregamento direto do PFP **sempre** grava `RLC_CNTL` de volta ao valor de antes, mesmo com o carregamento OK e o anel gráfico de pé (o `CP_ME_CNTL`, ao contrário, fica no valor aplicado). As sondas do bind da 239 (`probe-registry-at-bind.json`): `RlcCntlBefore=0`, `PfpDirectRlcCntlBefore=0` → `PfpDirectRlcCntlApplied=1`, `RlcCntlAfter=0`, `RlcCntlRestore=0`.

**Leitura:** o firmware do RLC fica **parado** em regime (`RLC_ENABLE_F32=0`). Isso explica as 0 de 41 confirmações de safe mode (não há quem responda). O amdgpu roda o RLC (`rlc_start` no `hw_init`), então é uma **diferença de paridade fora do inventário do doc 593**. A confirmação no silício é o primeiro objetivo da 240 (linha `[RLC_ESTADO] boot`).

## 2. Candidato

| Peça | Mudança | Hash |
|---|---|---|
| KMD lab **3.385.1.0** | `[RLC_ESTADO]` (RLC_CNTL, RLC_STAT, RLC_SAFE_MODE, IB2) no fim do StartDevice e no TDR do GFX; `[P3_IH] OUTRO #n` com os 8 dwords crus das 8 primeiras entradas do IH de origem desconhecida (fora do limite de 10 s); descartes da P4.2 por motivo; mensagem do `[P4_SDMA1]` corrigida | SYS `40916F50…`, INF `4DAF1601…`, CAT `D042C2C2…` |
| UMD | igual | `8782A5E5…` |
| RADV | despejo de IB em AppContainer vai para a Temp do pacote; anel dos últimos 256 IBs por processo (antes: os 128 primeiros) | `7B82C4AE…` (cópia da anterior: `vulkan_radeon.dll.pre-iib-C6BEC54B`) |
| Coletor pós-trava | copia os IBs/fluxos da RADV (`C:\Windows\Temp` e Temp dos pacotes) para `gfx-ib-dump-pos-trava\` | — |

Nenhuma mudança de comportamento da GPU. Offsets novos com proveniência em `gc_10_1_0_offset.h` (comentário em `bc250_gfx_ring_transaction.h`). Suíte offline EXIT=0, auditoria de segurança PASS, auditoria da RADV PASS, auditoria do pacote PASS, strings novas conferidas no SYS assinado e na DLL.

**Custo declarado:** o anel de IBs faz o DWM e o menu gravarem um arquivo por submissão a corrida inteira (antes paravam em 128). Na 239 o DWM fez ~4 submissões/s.

## 3. Roteiro

Igual ao da 239 (só o menu Iniciar, abrir/fechar a cada ~30 s). `tools\EXECUTAR_CORRIDA240_INSTRUMENTACAO_MENU_600S.cmd`, elevado, depois de **desligar** com Claude, Edge, Configurações, Gerenciador e Microsoft 365 Copilot fechados.

## 4. Desfechos (escritos antes)

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | TDR do GFX com: `[RLC_ESTADO] TDR`, IB do menu em `gfx-ib-dump-pos-trava\` com a VA da última submissão antes do TDR, e `[P3_IH] OUTRO` com os dwords do erro do SQ | Instrumentação completa: decodificar o IB e o erro offline; desenhar a correção do 1º IB depois da troca de VMID e o reset do GFX |
| B | `[RLC_ESTADO] boot` com `RLC_ENABLE_F32=0` (qualquer que seja o resto) | Confirma o RLC parado em regime: entra no inventário de paridade como item próprio (RLC rodando como o amdgpu) |
| C | `[RLC_ESTADO] boot` com `RLC_ENABLE_F32=1` | O RLC roda e o safe mode falha por outro motivo; a leitura do doc 631 §1 está errada e o próximo suspeito é o protocolo |
| D | Sem TDR do GFX nos 600 s | Sem dados do IB; vale só o B/C. Repetir com o mesmo pacote |
| E | Fora do mapa | Descrever antes de julgar |

A e B/C não se excluem: a linha do boot sai em toda corrida.

**Teto:** 2 corridas com este pacote.

**NÃO VALIDADO EM HARDWARE:** tudo deste doc.
