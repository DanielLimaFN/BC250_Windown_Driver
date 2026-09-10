# Análise da corrida 2026-09-07 08:25 — `P7_D3D11_CAPS_PROBE` v3

**Candidato:** SYS `F244576E…80EFCC` · UMD `FE227F68…9B75554` (v3: as três entradas do adapter
table instrumentadas) · DriverStore conferido contra o build antes do bind.
**Veredito:** **o probe achou o bloqueio — e ele não está na UMD.** Está num `case` ausente no
KMD.

---

## 1. O silêncio que localizou o problema

A v3 instrumentou `pfnCalcPrivateDeviceSize` e `pfnCloseAdapter`, que antes eram mudas. O
resultado:

| Entrada do adapter table | Chamadas |
|---|---:|
| `pfnCalcPrivateDeviceSize` | **0** |
| `pfnCloseAdapter` | **0** |
| `pfnCreateDevice` | **0** |
| `pfnGetSupportedVersions` | **0** |
| `pfnGetCaps` | **0** |

Sequência real, com o step-down funcionando:

```text
=== bc250umd.dll CARREGADA ===
OpenAdapter10_2: 0x000B0011 fora da allowlist -> E_NOTIMPL
OpenAdapter10_2: 0x000B000B fora da allowlist -> E_NOTIMPL
OpenAdapter10:   0x000A0002 -> S_OK
=== bc250umd.dll CARREGADA ===          <-- descarrega SEM nem fechar o adapter
```

O runtime aceita o `S_OK`, chama **nada**, e larga a DLL **sem chamar `CloseAdapter`**. Isso
**elimina** a hipótese (b) da análise anterior (“chamou `CalcPrivateDeviceSize` e desistiu
ali”): ele não desistiu no meio, ele desistiu **antes de começar**. Um runtime que
descarta um adapter sem invocar uma única função dele decidiu pelos dados que já tinha.

## 2. O que ele já tinha: o log do KMD, na mesma corrida

```text
[McdmQAI] Type=0, OutSize=2400, CapsStatus=0x00000000
[McdmQAI] Type=0 NOT SUPPORTED
```

**80 vezes.**

`Type=0` é **`DXGKQAITYPE_UMDRIVERPRIVATE`** — confirmado no header do WDK
(`shared/d3dkmddi.h`: `DXGKQAITYPE_UMDRIVERPRIVATE = 0`). É o canal de **private driver
data** entre o nosso KMD e o nosso UMD.

E ele **não tem `case`** no switch de `Bc250McdmQueryAdapterInfo`: cai no `default:` e
devolve `STATUS_NOT_SUPPORTED` (`driver/wdk/bc250_mcdm_callbacks.c:1985`).

Distribuição completa dos tipos de QAI nesta corrida:

```text
Type=0  (UMDRIVERPRIVATE)     160 chamadas, 80 respondidas NOT SUPPORTED
Type=1  (DRIVERCAPS)            1
Type=10 (GPUMMUCAPS)            1
Type=11 (QUERYSEGMENT4)         2
Type=13 (PAGETABLELEVELDESC)    2
Type=14, 15, 16                 1 cada
```

**`UMDRIVERPRIVATE` é, com folga, o tipo mais pedido — e o único recusado em massa.**

## 3. Por que isso explica tudo o que estava sem explicação

| Observação que não fechava | Explicação |
|---|---|
| Zero funções do adapter table chamadas | O runtime nunca chega a usá-las: falta o private data |
| DLL carregada e descartada em laço (44, 66, 80 ciclos) | Cada tentativa refaz o caminho e bate na mesma recusa |
| Aceitar (v1) ou recusar (v2) interface não mudou o desfecho | O bloqueio é **anterior**, e está no kernel |
| `DXGI_ERROR_UNSUPPORTED` no `CreateD3D11Device` do `dwmcore` | Consequência final da mesma cadeia |

## 4. Classificação honesta

`P1` (medido): o runtime pede `UMDRIVERPRIVATE` com 2400 bytes, 160 vezes, e o nosso KMD
recusa 80 delas com `STATUS_NOT_SUPPORTED`; e nenhuma entrada do adapter table da UMD é
chamada.

`H` (hipótese, **não** provada): que essa recusa seja **a** causa do descarte do adapter.
É a hipótese mais forte de longe, e é coerente com toda a cadeia — mas nada na documentação
que li declara `UMDRIVERPRIVATE` **obrigatório** para Full Graphics.
`mcdm-implementation-guidelines.md` inclusive o lista como **opcional**, embora para MCDM,
que não é o nosso perfil. Só uma corrida com o tipo implementado decide.

## 5. Autocrítica de método

Gastei **três corridas** no probe mexendo na UMD, quando o `Type=0 NOT SUPPORTED` estava no
log do KMD **desde a primeira**. As duas primeiras corridas entregaram a mecânica do
step-down (`P1`, útil), mas o caminho curto era ler o log do kernel com atenção aos tipos de
QAI antes de instrumentar modo usuário.

O que finalmente localizou o problema não foi um log novo — foi o **silêncio** dos três
novos logs. Instrumentar para provar ausência valeu a corrida.

## 6. Estado da máquina

Confirmação visual recusada — o operador digitou ` DISPLAY_OK` com espaço à esquerda, e o
confirmador exige a palavra exata. Watchdog expirou, rollback, reboot. **Quinta recuperação
limpa consecutiva.** Depois do reboot: BasicDisplay, Código 0, fusível `CLEAN`.

## 7. O que muda no plano

O **item zero** do dimensionamento passa a ser o handshake `UMDRIVERPRIVATE`, antes das 105
entradas de `D3D10_1DDI_DEVICEFUNCS`. E ele é favorável à **rota B**: o blob é **nosso** nos
dois lados, então não há ABI da AMD para imitar — que é exatamente a fragilidade que a
Collabora aponta no próprio port de RADV.

**Compute Vulkan, submissão Node 1, Área de Trabalho visível e jogos: NÃO VALIDADOS EM
HARDWARE.**
