# 598 — P2: WPTR do SDMA0 por doorbell, como o amdgpu; critério da Corrida 221

**Data:** 05/10/2026 · **Candidato:** lab **3.369.1.0** · **Base:** [doc 597](597_CORRIDA220_P7_VALIDA_GFX_PELA_GTT_TRAVA_IGUAL_AOS_137S.md) (P7 válida, trava igual aos ~137 s)
Trabalho offline. **NÃO VALIDADO EM HARDWARE.**

## 1. Por que a P2

**Hoje:** cada submissão do SDMA0 publica o WPTR com 4 escritas de registrador pela BAR: `MINOR_PTR=1`, `RB_WPTR`, `RB_WPTR_HI` e `MINOR_PTR=0`. Foram ~6.000 na 220.

**O amdgpu** (`sdma_v5_0_ring_set_wptr`, com `use_doorbell`) faz outra coisa:
- grava a shadow do WPTR na memória;
- faz uma escrita de 64 bits na BAR de doorbell (`WDOORBELL64`);
- não escreve em nenhum registrador.

A P2 troca só isso: uma diferença. O HDP por MMIO em cada submissão (P1b) fica como está, para a próxima corrida.

## 2. Proveniência (P1, Lei 7)

Fontes: linux v6.6 em `inc/linux_ref/`, com `PROVENANCE_P2_DOORBELL.txt`, e o catálogo docs/03.

| O quê | Valor | Fonte |
|---|---|---|
| `SDMA0_GFX_DOORBELL_OFFSET` | `0x04C2C` = (0x1260+0xAB)·4, campo `0x0FFFFFFC` | `gc_10_1_0_offset.h`/`sh_mask.h` |
| `BIF_SDMA0_DOORBELL_RANGE` | `0x03BC0` = (0xD20+0x1D0)·4, OFFSET `0xFFC`, SIZE `0x1F0000` | `nbio_2_3_offset.h`/`sh_mask.h`, `navi10_ip_offset.h` |
| Índice | `0x200` = `AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE0` (0x100) << 1 | `amdgpu_doorbell.h`, `sdma_v5_0_sw_init` |
| SIZE da faixa | 20 | `nv.c` |
| Byte na BAR de doorbell | `0x800` | `WDOORBELL64(0x200)` |

A base NBIO idx2 `0xD20` dá também `0x378C` e `0x385C`, que já usamos e provamos no silício. Isso confere a aritmética.

## 3. O que muda (`bc250_sdma_present.{c,h}`, `bc250_active_miniport.c`)

- **Publicador único `Bc250SdmaPresentPublishWptr`:**
  - com a doorbell ativa: grava a shadow e faz a doorbell de 64 bits;
  - sem ela: o MMIO antigo.
  - Os 11 sítios de submissão passam por ele. O init e o restart continuam zerando o WPTR por MMIO, como o `gfx_resume`.
- **Arme (`Bc250SdmaPresentP2Arm`), no StartDevice logo depois da D2, com o anel ocioso:**
  1. RMW de `SDMA0_GFX_DOORBELL.ENABLE`, `DOORBELL_OFFSET` e `BIF_SDMA0_DOORBELL_RANGE`, com readback;
  2. **prova isolada:** o poll da shadow do WPTR é desligado, para que só a doorbell possa entregar o WPTR; um NOP com fence sobre poison precisa retirar, e pelo menos uma doorbell precisa ter sido escrita;
  3. o poll é restaurado com a shadow igual ao WPTR.
- **Falha do arme:**
  - desarma e devolve os 3 registradores ao valor lido antes;
  - não tenta de novo neste bind, o que evita laço com o restart;
  - reconstrói o anel pelo restart e segue com o WPTR por MMIO, declarado no log;
  - se o anel não voltar provado, o StartDevice recusa.
- **Restart do TDR:** marca a doorbell inativa antes do soft reset, que a desliga. Depois de provar o anel, rearma, com a mesma prova.
- **Teardown:** desarma, devolve o BIF e desmapeia a página da doorbell antes de soltar o anel.
- **Sonda de travamento (`ProbeResponsive`):** com a doorbell ativa, lê o WPTR da shadow, como `sdma_v5_0_ring_get_wptr`.

**Logs novos:**
- `[P2_DOORBELL] APLICADO ... readback=confere`
- `[P2_DOORBELL] ATIVA em STARTDEVICE ... (Kicks=N)`
- `[P2_DOORBELL] NAO ATIVA ...`
- `[P2_DOORBELL] DESARMADA (teardown) ... Kicks=N`

## 4. Provas offline

- `build-contract`: EXIT=0;
- build de laboratório: "TODOS OS TESTES APROVADOS (0 falhas)";
- auditoria de segurança: PASS;
- auditoria do pacote: PASS;
- as strings `[P2_DOORBELL] ATIVA`/`APLICADO` estão no SYS assinado.
- **Portão novo `audit_p2_sdma0_doorbell`, com 13 mutações reprovadas:**
  - chave em 0;
  - índice 0x100;
  - offset do BIF errado;
  - sítio de volta ao MMIO;
  - doorbell sem shadow;
  - prova sem isolar o poll;
  - prova aceita sem doorbell;
  - laço com o restart;
  - restart sem rearme;
  - restart com a doorbell marcada;
  - teardown sem desarme;
  - sonda lendo o registrador;
  - StartDevice seguindo sem anel.

| Arquivo | SHA-256 |
|---|---|
| SYS | `03FB3C8C507FB850CC8744B369D3D7CE0BC0878E269D15A92DA8D0DEA9FAEAFE` |
| INF | `46695E64F0B6FD04278EF53C15AFE62D3D1D137D88C44A7AF6F0FC4C4B6A4E8F` |
| CAT | `6A12BAA7B8C50AA3BC8CC66FCB433E2F0DBF2C1D110202075C16359C1D4DB0F7` |
| UMD / RADV | inalteradas |

**Scripts:** `tools/prepare-corrida221-p2-doorbell-sdma0.ps1` e `tools/EXECUTAR_CORRIDA221_P2_DOORBELL_SDMA0_600S.cmd`. O dry-run passou em hashes e manifesto e parou na leitura da CMOS, como esperado.

## 5. Critério da Corrida 221 (Lei 10)

**Pré-condição de validade:**
- `[P2_DOORBELL] APLICADO ... readback=confere`;
- `[P2_DOORBELL] ATIVA em STARTDEVICE`;
- a base da 220 se manteve: D2 ATIVO, `[P7_GFX_GTT] ATIVO`, `DrenoOk=1`, P1.

Desfechos:

- **A — 600 s sem trava dura**, com a doorbell ativa até o fim (`DESARMADA (teardown) ... Kicks` na casa dos milhares).
  - *Implicação:* a publicação do WPTR por MMIO era a causa, ou parte dela. Próximo: repetir para confirmar.
- **B — trava dura com a mesma assinatura** (GPU ociosa, último evento um retire), com a doorbell ativa.
  - *Implicação:* a P2 do SDMA0 sai das causas suficientes. Próximo: P1b (HDP por MMIO em cada submissão), depois o WPTR do GFX por doorbell e a P3.
- **C — a doorbell não arma:** `NAO ATIVA`, com prova=0 ou readback=0.
  - *Implicação:* o roteamento do BIF nesta placa difere do navi10, ou falta `RCC_DOORBELL_APER_EN`/`selfring`. O log traz os valores antes e depois dos 3 registradores. A corrida segue por MMIO e não testa a P2.
- **D — a doorbell arma, mas o SDMA0 para depois:** `IB_TRIPOD TIMEOUT` com RPTR parado ou TDR do SDMA0 logo no começo.
  - *Implicação:* falta algo da publicação em regime (por exemplo, a ordem shadow→doorbell ou o `MINOR_PTR`). O TDR rearma; o log diz quantos kicks antes.
- **E — fora do mapa:** descrever antes de julgar.

## 6. Pendências

- **Daniel:** recuperação, reset do fusível, leitura da CMOS (`tools\vram-cmos\bc250-vram.ps1 -Ler`), boot frio e `tools\EXECUTAR_CORRIDA221_P2_DOORBELL_SDMA0_600S.cmd`. A coleta e o teste de RAM rodam sozinhos depois de uma trava.
- **NÃO VALIDADO EM HARDWARE:**
  - a P2;
  - a desmontagem da P7;
  - 600 s sem trava com 256 MB.
