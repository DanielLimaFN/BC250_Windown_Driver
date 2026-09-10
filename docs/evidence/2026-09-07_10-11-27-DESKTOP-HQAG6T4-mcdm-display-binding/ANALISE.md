# Análise da corrida 2026-09-07 10:11 — `P7_D3D11_CAPS_PROBE` v5 · **MISSÃO CUMPRIDA**

**Candidato:** SYS `525A8629…D676A2` · UMD `6249F69F…6C00C5` · hashes do DriverStore e strings
da v5 conferidos antes do bind.
**Veredito:** **a hipótese estava certa, o defeito era nosso, e o probe respondeu as três
perguntas que motivaram sua existência.** A tela seguiu preta — e agora sabemos exatamente
por quê, com o último portão nomeado e medido.

---

## 1. A convenção era o defeito — confirmado

```text
OpenAdapter10_2: runtime forneceu pAdapterFuncs_2=0000000002F724B8
                 -> PREENCHENDO a struct dele (5 entradas)
```

**O ponteiro vem NÃO-NULO.** O runtime aloca a `D3D10_2DDI_ADAPTERFUNCS` e passa o endereço;
o driver preenche os membros. Nosso código **sobrescrevia o ponteiro** com o endereço da
nossa tabela estática, e o runtime seguia lendo a struct dele — zerada. Tabela de ponteiros
nulos.

Isso explicava, retrospectivamente, **todas** as anomalias das quatro corridas anteriores:
nenhuma função chamada, `pfnCloseAdapter` nunca chamado (também nulo), e a DLL descartada e
recarregada em laço.

**A prova de que a correção pegou:** `pfnCloseAdapter` foi chamado **95 vezes** nesta corrida.
Nas quatro anteriores: **zero**.

## 2. Pela primeira vez, o runtime conversou

| Entrada | Corridas v1–v4 | **v5** |
|---|---:|---:|
| `pfnGetSupportedVersions` | 0 | **90** |
| `pfnGetCaps` | 0 | **63** |
| `pfnCloseAdapter` | 0 | **95** |
| `pfnCalcPrivateDeviceSize` | 0 | 0 |
| `pfnCreateDevice` | 0 | 0 |

```text
GetSupportedVersions: sondagem de contagem -> 2
GetSupportedVersions: declarado[0]=0x000A000100040000   (Interface=0x000A0001, Version=0x00040000)
GetSupportedVersions: declarado[1]=0x000A000200010000   (Interface=0x000A0002, Version=0x00010000)
```

Declaramos **D3D10_0 e D3D10_1** — honestamente, pelo mecanismo desenhado para isso — depois
de abrir em `0x000B000B`. **O runtime aceitou e prosseguiu.** Isso valida a decisão da v4:
abrir não é prometer.

## 3. A resposta que o probe existia para obter

```text
GetCaps: Type=130 (D3D11DDICAPS_3DPIPELINESUPPORT) DataSize=4 pInfo=NULL
         -> E_NOTIMPL (zerar seria declarar capacidade inexistente)
```

**63 chamadas, e exatamente UM tipo de caps.** Não `THREADING`, não `SHADER`, não
`D3D11_OPTIONS` — **só o nível do pipeline 3D**, 4 bytes.

As três perguntas do dimensionamento (doc 173, seção 6), respondidas:

| Pergunta | Resposta medida |
|---|---|
| O `dwmcore` aceita feature level 10_1? | **Sim** — aceitou a declaração de D3D10_0/D3D10_1 em `GetSupportedVersions` e prosseguiu para os caps |
| Quais caps ele valida antes do device? | **Exatamente um: `D3D11DDICAPS_3DPIPELINESUPPORT`, 4 bytes** |
| Qual header é o minor 17 (`0x000B0011`)? | **Irrelevante** — `0x000B000B` funciona; a pergunta deixou de bloquear |

## 4. O último portão, e por que não há truque

A cadeia agora é completa e medida:

```text
OpenAdapter10_2(0x000B000B) -> preenche a tabela de 5 entradas -> S_OK
  -> GetSupportedVersions  -> declaramos D3D10_0 + D3D10_1          [OK]
  -> GetCaps(3DPIPELINESUPPORT, 4 bytes) -> RECUSAMOS               [PARA AQUI]
  -> CloseAdapter                                                    [fecha limpo]
```

O runtime para porque nós recusamos — e a recusa é **correta**. `D3D11DDI_3DPIPELINELEVEL`
não tem valor para "nenhum": `0` significa nível `10_0`. Responder qualquer coisa ali é
declarar um pipeline 3D que não existe.

**Não existe truque de cap que passe deste ponto.** Se declarássemos `10_1`, o runtime
chamaria `pfnCalcPrivateDeviceSize`, `pfnCreateDevice` e em seguida as **105 entradas** de
`D3D10_1DDI_DEVICEFUNCS` — que não existem. Trocaríamos uma recusa honesta por um crash com
cap falso.

**Conclusão de arquitetura, agora medida e não inferida:** a única coisa entre este driver e
um device D3D é um **pipeline 3D real**. O caminho é o da rota B (DXBC → SPIR-V → RADV/ACO),
e a ordem do doc 173 fica confirmada em cada degrau.

## 5. O resto da corrida

Display igual: 18 `IsSupportedVidPn`, 1 `CommitVidPn`, **175 `Present`**, `device-after.json`
com **Código 0**, e **179** publicações do blob `UMDRIVERPRIVATE` no KMD.

**A9/A10 honestos pela sexta corrida seguida:** `BLT_TRIPOD` = **0**,
`BLT_ENGINE_2LEGS` = 1, `BYTE_PROOF PASS` = **0**, `BYTE_PROOF INCONCLUSIVO` = **3**.

Dois dumps do DWM. Tela preta, imagem de volta, watchdog reiniciou. **Oitava recuperação
limpa consecutiva.**

## 6. Saldo do `P7_D3D11_CAPS_PROBE` — encerrado

**Seis corridas.** O probe cumpriu o que prometeu, mas caro:

| Corrida | Resultado |
|---|---|
| v1 · 06/09 23:45 | `P1`: step-down é comandado pela recusa da UMD · **defeito meu:** cap falso |
| v2 · 07/09 08:05 | `P1`: step-down restaurado até `OpenAdapter10` |
| v3 · 07/09 08:25 | Localizou `UMDRIVERPRIVATE` recusado — pelo **silêncio** das 3 entradas |
| item zero · 09:40 | `P1`: handshake funciona · **hipótese do `Type=0` REFUTADA** |
| v4 · 10:00 | 5 entradas publicadas e aceitas · **hipótese da tabela REFUTADA** |
| **v5 · 10:11** | **Convenção confirmada. `GetSupportedVersions` + `GetCaps` chamados. Último portão nomeado: `3DPIPELINESUPPORT`.** |

**Autocrítica, e é a mesma das duas análises anteriores, agora confirmada:** o defeito da v5
era uma **convenção de ponteiro legível no header desde a primeira corrida**. Quatro corridas
foram gastas testando o comportamento do runtime — step-down, blob, forma da tabela — quando
a pergunta certa era "quem aloca esta struct?". O caminho curto era ler o contrato antes de
instrumentar, e eu inverti a ordem.

O que salvou o método foi a disciplina de **instrumentar para provar ausência**: foi o
silêncio das entradas, e depois a ausência específica de `pfnCloseAdapter`, que apontou a
convenção. Sem esses logs, a suspeita não teria surgido.

**Compute Vulkan, submissão Node 1, Área de Trabalho visível e jogos: NÃO VALIDADOS EM
HARDWARE.**
