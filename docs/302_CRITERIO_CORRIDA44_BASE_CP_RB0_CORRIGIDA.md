# 302 — Critério prévio da Corrida 44 — base do CP_RB0 codificada por fonte única

> **Data da faixa offline:** 20/09/2026.  
> **Candidato:** lab `3.202.1.0`, `DISPLAY_RENDER_LAB`, WDDM 2.7, schema 135.  
> **Pacote:** `driver/wdk/package/output_lab/`.  
> **Estado:** assinado e auditado; **NÃO INSTALADO; NÃO VALIDADO EM HARDWARE**.

> ⚠️ **EMENDADO EM 20/09/2026, ANTES DA CORRIDA 44,** por
> [`docs/302A_EMENDA_CRITERIO_CORRIDA44.md`](302A_EMENDA_CRITERIO_CORRIDA44.md).
> As seções "Sucesso obrigatório" e "Falha informativa" **abaixo estão superadas**: elas
> discordavam do §11 do `docs/303`, não tinham o desfecho `E — algo fora do mapa` exigido pela
> Lei 10, e punham no sucesso obrigatório itens que dependem do stall inicial — dívida que esta
> faixa não toca. Este documento fica intacto como registro do que foi escrito primeiro; o
> critério **vigente** é o 302A.

## 1. Mudança isolada

A Corrida 43 mediu que a prova pós-reset ficou em `RPTR=0/WPTR=12`. A inspeção encontrou que
o restart escrevia o endereço bruto do ring em `CP_RB0_BASE`, embora o registrador exija
`GPUVA >> 8`. O caminho ONLINE já estava correto.

O candidato 3.202.1.0 centraliza a codificação em `Bc250GfxRingEncodeBaseLo/Hi` e usa os mesmos
helpers no ONLINE e no restart. Para o endereço físico medido `0xF40FF70000`, o valor esperado
nos registradores é `BASE_HI=0`, `BASE_LO=0xF40FF700`.

Nenhuma mudança foi feita na receita PM4 do IB que causa o stall inicial. A Corrida 44 mede
somente se a correção permite ao CP executar a prova privada após o reset.

## 2. Identidade congelada

- DriverVer: `09/20/2026,3.202.1.0`
- SYS fonte: `1007DACB3B70C59DA3A14B7C420FD57C925769F1B4EF1C7EDCB9BF35F1040894`
- SYS assinado: `33E8FF29868D643D409D6256BD8120E061883B9B4BE8D3A74652FBB4D3ABCCFF`
- PDB privado: `EF6EFD68063F2344498ACD9753D6C2E7B6118E70446C5FAC2D94BD2DDF82575F`
- UMD assinada: `934DBD84E04F10024F6B7CAB28FBE014CAA41D35E103600F160BBF395AB5236A`
- INF: `B2FE0DE42C7E365FFF32DEC11ABA540AEEBB7C5B4CB0E21F7DA41AF13C66C998`
- CAT: `70FB1638E35B2E6FC1EB53937D7C509229FFC41D2B43B0AB383644CEC4BC7A8F`

`driver/wdk/build-contract.bat`, auditoria de segurança e auditoria do pacote: PASS. A nova
aritmética e os gates são prova offline, não prova do restart no hardware.

## 3. Etapa obrigatória antes de outro bind

Concluída: o dump atribuiu o Arg2 a `Bc250McdmRestartFromTimeout` e o Arg3 à recusa
`STATUS_DEVICE_NOT_READY`, sem AV/IRQL/verifier/corrupção. O PDB arquivado estava corrompido;
a atribuição foi recuperada por um par reconstruído cuja `.text` e `.pdata` são idênticas ao
SYS instalado. O binder agora copia **SYS + PDB** e recalcula os hashes dos dois destinos,
falhando fechado se qualquer cópia divergir do manifesto.

Ainda é obrigatório executar o roteiro de recuperação e resetar o fusível antes do novo
bind. Na última inspeção do handoff o estado era `TRIPPED`, `Boots=4`, e a BC-250 estava em
erro pós-StartDevice.

## 4. Hipótese falsificável

**H44:** a falha `RPTR=0` da prova pós-reset foi causada pela codificação 256 vezes errada de
`CP_RB0_BASE`. Com `BASE=RingGpuVa>>8`, o pacote de 12 dwords será consumido e a fence privada
substituirá o poison.

### Sucesso obrigatório

Todos precisam ocorrer:

1. `[TDR_FENCE_RECONCILE] PASS` e `NotificadasComoConcluidas=0`;
2. `[TDR_GFX_RESTART_PROOF] PASS` com `RPTR=WPTR=12`, poison substituído e fence esperada;
3. `[TDR_GFX_RESTART] CP_RB0 rearmado e comprovado por execucao fisica`;
4. callback de restart retorna sucesso;
5. uma submissão GFX posterior avança e aposenta fence real;
6. nenhum segundo TDR equivalente e nenhum BugCheck.

### Falha informativa

Se o RPTR continuar zero, H44 está refutada ou há outra dependência pós-reset. Preservar
readbacks de BASE/CNTL/ACTIVE/ME_CNTL e o dump; não ampliar o reset por tentativa. Se RPTR
avançar parcialmente, a base foi corrigida mas outro estado ainda bloqueia o pacote.

### Falha de contrato

- declarar restart sem a prova executável;
- notificar fence abortada como concluída;
- aceitar trabalho novo após prova falha;
- perder a cauda integral do KMD;
- tratar BasicDisplay após reboot como sucesso do driver BC-250.
