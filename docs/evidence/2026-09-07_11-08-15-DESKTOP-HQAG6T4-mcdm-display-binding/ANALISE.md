# Análise da corrida 2026-09-07 11:08 — regressão da toolchain VS2026 + WDK 28000

**Candidato:** SYS `62B4730D…4C2B2E` · UMD `55C6A71D…0714CB5` · CAT `2FBCBEB7…98E3E2`
**Toolchain:** **VS2026 + SDK/WDK 10.0.28000.0** (antes: VS2022 + WDK 10.0.26100.0)
**Mudança de código:** **nenhuma** desde a corrida 10:11. Só compilador e headers.
**Objetivo:** regressão pura — reproduzir os números de 10:11.
**Veredito:** **PASSOU.** Nenhum contador voltou a zero, o tripé da SDMA armou, e a cadeia de
negociação D3D reproduziu degrau por degrau.

---

## 1. Verificação offline feita ANTES da corrida

Nada disso custou corrida, e tudo passou:

| Verificação | Resultado |
|---|---|
| `/Oy-` no `CFLAGS` do SYS (gate A5) | **presente** |
| CRLF em todos os `.bat` de build (gate A8) | **0 arquivos com LF solto** |
| Contrato DDI pinado | `DXGKDDI_INTERFACE_VERSION=0xC004`, `NTDDI_VERSION=0x0A00000C` — **não foi bumpado** |
| `build-contract.bat` | **EXIT=0** |
| `build-umd.bat` | **EXIT=0**, todos os testes de contrato |
| `audit-bc250mcdm-safety` / `ddi-matrix` / `ddi-noop` | **PASS** |
| Strings da v5 na DLL empacotada | todas presentes |

### A checagem que mais importava: tamanho das structs de fronteira

A v5 **copia** uma struct para dentro do buffer que o runtime aloca
(`*pOpenData->pAdapterFuncs_2 = tabela`). Se o header novo tivesse crescido, isso corromperia
a heap do `dwm.exe`. Comparação entre os dois SDKs:

```text
D3D10_2DDI_ADAPTERFUNCS ... 5 entradas em 26100  ·  5 em 28000
D3D10DDI_ADAPTERFUNCS ..... 3                     ·  3
DXGK_DRIVERCAPS ........... 41 membros            ·  41
```

**Idênticas.** O risco não existia, e foi descartado por leitura antes de armar.

## 2. A cadeia de negociação D3D, reproduzida

```text
OpenAdapter10_2(0x000B000B) -> PREENCHENDO a struct dele   ...  95
  -> GetSupportedVersions                                  ... 285 linhas (95 chamadas)
  -> GetCaps: Type=130 (D3D11DDICAPS_3DPIPELINESUPPORT)    ...  63   <-- PARA AQUI
  -> CloseAdapter                                          ...  95
  -> CalcPrivateDeviceSize / CreateDevice                  ...   0
```

O `GetCaps` continua pedindo **um único tipo**, 63 vezes: `3DPIPELINESUPPORT`. Nem
`THREADING`, nem `SHADER`, nem `OPTIONS`.

**Consequência epistêmica:** a conclusão de arquitetura — *"o último portão é um pipeline 3D
real, e não há atalho por caps"* — passa a estar reproduzida **com toolchain diferente**. Não
é artefato de compilador nem coincidência de uma corrida.

## 3. Comparação lado a lado

| Medida | 10:11 (WDK 26100) | **11:08 (WDK 28000)** |
|---|---:|---:|
| `PREENCHENDO a struct dele` | 90 | **95** |
| `GetSupportedVersions` (chamadas) | 90 | **95** |
| `GetCaps Type=130` | 63 | **63** |
| `CloseAdapter` | 95 | **95** |
| `CreateDevice` | 0 | **0** |
| `UMDRIVERPRIVATE` publicado (KMD) | 179 | **186** |
| `IsSupportedVidPn` | 18 | **18** |
| `CommitVidPn` / `SetVidPnSourceAddress` | 1 / 1 | **1 / 1** |
| `Present` | 175 | **175** |
| `BLT_ENGINE_2LEGS` / `BLT_TRIPOD` | 1 / 0 | **1 / 0** |
| `BYTE_PROOF INCONCLUSIVO` / `PASS` | 3 / 0 | **3 / 0** |

> ⚠️ **Nota de medição:** em 10:11 eu reportei "90" para `GetSupportedVersions` contando um
> filtro que pegava uma linha por chamada; aqui o `grep` por `GetSupportedVersions:` retorna
> **285**, porque cada chamada emite três linhas (sondagem de contagem + `declarado[0]` +
> `declarado[1]`). O número de **chamadas** é 95 nas duas. Registro para ninguém comparar
> 90 com 285 e concluir mudança de comportamento onde só houve mudança de filtro.

## 4. O tripé da SDMA — o item mais sensível a codegen

```text
[SdmaPresent] TRIPÉ NO SILÍCIO COMPROVADO (Item 63)! WPTR=68 RPTR=52
              Fence=00001001 Dados=4/4 OK em 1 polls!
```

Armou **em 1 poll**, com `Dados=4/4 OK`. Era o maior risco da troca de compilador — ordem de
acesso a registrador e timing, que nenhum teste offline cobre — e reproduziu idêntico ao
histórico.

## 5. A9/A10 seguem honestos, sétima corrida consecutiva

`BLT_TRIPOD` = **0** · `BLT_ENGINE_2LEGS` = 1 · `BYTE_PROOF PASS` = **0** ·
`BYTE_PROOF INCONCLUSIVO` = **3**. Nenhuma linha de sucesso falso saiu de nenhuma corrida
desde que os gates entraram.

## 6. Veredito

**A toolchain VS2026 + SDK/WDK 10.0.28000.0 está VALIDADA NO SILÍCIO** para este driver, com
os três gates comprados a custo de corrida preservados: `/Oy-`, CRLF nos `.bat`, e o contrato
DDI pinado em `0xC004` (não bumpado para além do que o Windows 10 22H2 suporta).

Tela preta, imagem de volta, watchdog reiniciou — comportamento correto e esperado: o driver
recusa honestamente o cap de pipeline 3D, o DWM não obtém device, e a recuperação automática
funciona. **Nona recuperação limpa consecutiva.**

**Compute Vulkan, submissão Node 1, Área de Trabalho visível e jogos: NÃO VALIDADOS EM
HARDWARE.**
