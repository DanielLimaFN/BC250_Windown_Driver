# Análise da corrida 2026-09-07 09:40 — ITEM ZERO (`UMDRIVERPRIVATE`)

**Candidato:** SYS `6E5C3A79…6E1F09` · UMD `99C7084C…5876956` · hashes do DriverStore
conferidos contra o build **e** por inspeção de strings antes do bind.
**Veredito:** **o handshake funcionou de ponta a ponta, e a hipótese caiu.** Resultado
negativo limpo, e do tipo que só uma corrida podia dar.

---

## 1. O handshake funcionou

**Lado KMD — 112 publicações, zero `NOT SUPPORTED`:**

```text
[McdmQAI] UMDRIVERPRIVATE: 104 bytes publicados (OutSize=2400, InSize=0), NodeCount=2, GateFlags=0x0000001B
[McdmQAI] UMDRIVERPRIVATE: 104 bytes publicados (OutSize=104,  InSize=0), NodeCount=2, GateFlags=0x0000001B
```

O `OutSize=104` na segunda chamada é significativo: **o runtime aprendeu o nosso tamanho e
voltou a pedir com ele**. É o padrão de duas passagens, e prova que ele *consumiu* a
resposta em vez de descartá-la.

**Lado UMD — 32 validações:**

```text
UMDRIVERPRIVATE: blob OK -- v1, 104 bytes, Vendor=0x1002 Device=0x13FE,
                 NodeCount=2 (SDMA=0, Compute=1), GpuVaBits=48, PageSize=4096,
                 VramSegId=1 VramBytes=0xF570000, GateFlags=0x0000001B
```

`GateFlags=0x1B` = `NO_3D_PIPELINE | NO_NODE1_SUBMIT | NO_SHADER_COMPILER | CAPS_PROBE_ONLY`.
O blob declara corretamente o que não existe.

E não sobrou nenhuma outra recusa no kernel nesta corrida: o único `NOT_SUPPORTED` é o do
`PHYSICALADAPTERCAPS`, intencional (adaptador físico único).

## 2. E mesmo assim: zero chamadas no adapter table

| Marcador real | Chamadas |
|---|---:|
| `D3D10 CreateDevice: INTERFACE ACORDADA` | **0** |
| `CalcPrivateDeviceSize:` | **0** |
| `CloseAdapter:` | **0** |
| `GetCaps:` | **0** |
| `GetSupportedVersions:` | **0** |

Sequência, com o handshake no meio e o descarte no fim:

```text
=== bc250umd.dll CARREGADA ===
OpenAdapter10_2: 0x000B0011 fora da allowlist -> E_NOTIMPL
OpenAdapter10_2: 0x000B000B fora da allowlist -> E_NOTIMPL
OpenAdapter10:   0x000A0002
UMDRIVERPRIVATE: blob OK -- v1, 104 bytes, ...
UMDRIVERPRIVATE: GateFlags declara CAPS_PROBE_ONLY -- ...
OpenAdapter10:   0x000A0002 -> S_OK
=== bc250umd.dll CARREGADA ===          <-- descarta e recomeça
```

> ⚠️ **Erro de medição que eu cometi e corrigi na hora.** Meu primeiro `grep` por
> `"CreateDevice"` retornou 32 e eu reportei "`CreateDevice` chamado 32 vezes, pela primeira
> vez". Estava **errado**: o padrão casou com a minha própria mensagem de log
> (`"...CreateDevice falhara fechado por decisao"`). Medido pelo marcador correto
> (`INTERFACE ACORDADA`), o número é **zero**. Registro aqui porque a conclusão oposta teria
> mandado o projeto para o lado errado.

## 3. A hipótese caiu — e o critério estava escrito antes

A `BASE_DE_AVANCO.md` dizia, antes desta corrida: *"Se (a) e (b) saírem e (c) não, a hipótese
de que a recusa do `Type=0` era a causa cai, e o bloqueio é outro."*

**(a) saiu. (b) saiu. (c) não saiu.** Portanto: **a recusa do `Type=0` NÃO era a causa do
descarte do adapter.** A hipótese mais forte que existia está refutada por medição.

## 4. O que isso valeu

1. **Eliminou a hipótese dominante.** Sem esta corrida, o trabalho seguiria sobre premissa
   errada.
2. **O handshake é infraestrutura que a rota B exige de qualquer forma** — o winsys do RADV
   precisa exatamente deste canal para receber os fatos do adaptador. Não é desperdício: é o
   item [0] da ordem de execução, agora **pronto e provado no silício**.
3. **Estreitou o suspeito para um só, e ele é geométrico.**

## 5. O suspeito que restou

```text
OpenAdapter10   -> D3D10DDI_ADAPTERFUNCS   = 3 entradas, SEM pfnGetCaps
OpenAdapter10_2 -> D3D10_2DDI_ADAPTERFUNCS = 5 entradas, COM pfnGetCaps
```

O runtime **só oferece `0x000A0002` pelo `OpenAdapter10`** — a tabela **sem** `pfnGetCaps`.
Sem `pfnGetCaps` ele não tem por onde perguntar o feature level, e descarta sem chamar nada.
E ele **nunca** pede `0x000A0002` pelo `OpenAdapter10_2`.

Logo o único caminho medido para o `pfnGetCaps` existir é **aceitar um DDI de D3D11 no
`OpenAdapter10_2`** e então declarar a verdade em `pfnGetSupportedVersions`.

## 6. O resto da corrida

Display convergiu igual: 18 `IsSupportedVidPn`, 1 `CommitVidPn`, 1 `SetVidPnSourceAddress`,
**175 `Present`**. `device-after.json` com `bc250mcdm` em **Código 0**.

**A9/A10 continuam honestos no silício:** `BLT_TRIPOD` = **0**, `BLT_ENGINE_2LEGS` = 1,
`BYTE_PROOF PASS` = **0**, `BYTE_PROOF INCONCLUSIVO` = **3**, com 63/63 amostras zeradas.
Nenhuma linha de sucesso falso.

Os ~40 s de tela preta seguidos do retorno da imagem batem com o padrão conhecido: o DWM cai
(2 dumps novos), sem DWM não há `Present`, o BasicDisplay reassume, e o watchdog reinicia.
**Sexta recuperação limpa consecutiva.**

## 7. Correção aplicada — v4, offline, em 07/09

**Allowlist por entry point:** `OpenAdapter10_2` passa a aceitar `0x000B000B`
(`D3D11_0_7_DDI_INTERFACE_VERSION`), publicando a tabela de **5 entradas**; `OpenAdapter10`
segue aceitando só `0x000A0002`. O minor 17 continua **fora** — não temos nome nem header
contemporâneo para ele, e declarar suporte ao que não se sabe nomear é chute.

**Por que isto não é o erro da v1:** na v1 eu devolvia `S_OK` para um DDI e **não havia como
qualificar a resposta** — era cap falso puro. Aqui aceitamos a **abertura** em `0x000B000B` e
imediatamente declaramos, pelo mecanismo que a Microsoft desenhou para isso
(`pfnGetSupportedVersions`), que as versões suportadas são D3D10_0 e D3D10_1. **Abrir não é
prometer.** E `pfnCreateDevice` segue fail-closed.

**Regra nova de honestidade no `pfnGetCaps`:** zerar o buffer é resposta verdadeira para
conjuntos de flags (`THREADING`, `SHADER`, `OPTIONS` — tudo zero significa "nada
suportado"), mas é **mentira** para `D3D11DDICAPS_3DPIPELINESUPPORT`, cujo payload é um
`D3D11DDI_3DPIPELINELEVEL` onde **`0` == `3DPIPELINELEVEL_10_0`**. Zerar ali declararia um
pipeline 3D nível 10_0 que este driver não tem. Para esse tipo o probe passa a devolver
**falha**, e `Bc250UmdCapsZeroIsTruthful` codifica a regra, travada por teste.

Pacote v4: SYS `EAF66723…703BBC` · UMD `88844A1D…3FA50C0` · CAT `2C3CA8E2…1D5A31`.

**Compute Vulkan, submissão Node 1, Área de Trabalho visível e jogos: NÃO VALIDADOS EM
HARDWARE.**
