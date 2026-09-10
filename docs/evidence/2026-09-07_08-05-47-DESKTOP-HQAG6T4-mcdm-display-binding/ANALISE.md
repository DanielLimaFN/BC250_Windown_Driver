# Análise da corrida 2026-09-07 08:05 — `P7_D3D11_CAPS_PROBE` v2

**Candidato:** SYS `F138BA3A…C902FD` · UMD `15233E8D…733AA9` (v2 com allowlist de interface)
**Probe:** armado · **Hashes do DriverStore conferidos antes do bind** contra o build.
**Veredito:** a correção da v2 **funcionou e está provada no silício**. O probe avançou dois
degraus além da v1 e parou num ponto novo, agora nomeado.

---

## 1. O step-down voltou a funcionar — provado

Sequência exata, em ordem, no `dwm.exe`:

```text
=== bc250umd.dll CARREGADA ===
OpenAdapter10_2: Interface=0x000B0011, Version=0x00060000
CAPS_PROBE: ARMADO
OpenAdapter10_2: Interface=0x000B0011 fora da allowlist do probe
                 -> E_NOTIMPL (deixa o runtime descer a escada ate 0x000A0002)
OpenAdapter10_2: Interface=0x000B000B, Version=0x00020009
OpenAdapter10_2: Interface=0x000B000B fora da allowlist do probe -> E_NOTIMPL
OpenAdapter10:   Interface=0x000A0002, Version=0x00010000
OpenAdapter10:   Interface=0x000A0002, Version=0x00010000 -> S_OK      <-- desceu a escada
=== bc250umd.dll CARREGADA ===                                          <-- e recomeçou
```

Contagens: **66** recusas de `0x000B0011`, **66** de `0x000B000B`, e **22** aceites de
`0x000A0002` via `OpenAdapter10`. Na v1, `OpenAdapter10` **nunca** havia sido chamado.

**Isto fecha a pergunta de mecânica:** a negociação de DDI é uma escada descendente, e o
degrau é escolhido pela **recusa** da UMD. A hipótese `H` da análise anterior passa a `P1`.

## 2. O ponto novo onde ele para — e o ponto cego que ele expôs

Depois do `S_OK` do `OpenAdapter10`, a próxima linha é **uma nova carga da DLL**. Nenhum
`GetSupportedVersions`, nenhum `GetCaps`, nenhum `CreateDevice`. As perguntas 1 e 3 do
dimensionamento (doc 173) **continuam sem resposta**.

E aqui está o defeito de instrumentação: **2 das 3 entradas de `D3D10DDI_ADAPTERFUNCS` eram
mudas.** Só `pfnCreateDevice` registrava; `pfnCalcPrivateDeviceSize` e `pfnCloseAdapter`
não. Com isso é **impossível distinguir**:

- o runtime não chamou nada depois do `S_OK`; de
- o runtime chamou `CalcPrivateDeviceSize` e/ou `CloseAdapter` e nós não registramos.

Duas hipóteses vivas, nenhuma medida (`H`):

**(a) O caminho de 3 entradas é beco sem saída para o D3D11.** `OpenAdapter10` publica
`D3D10DDI_ADAPTERFUNCS` — 3 ponteiros, **sem** `pfnGetSupportedVersions` e **sem**
`pfnGetCaps`. Se o runtime D3D11 precisa interrogar caps antes do device, ele não tem por
onde, e abandona. Nesse caso o alvo correto seria aceitar `0x000A0002` também em
`OpenAdapter10_2` (tabela de 5 entradas) — mas o runtime **nunca** pediu `0x000A0002` por
aquele entry point nesta corrida.

**(b) Ele chamou `CalcPrivateDeviceSize` e desistiu ali.** Plausível e indistinguível hoje.

## 3. Correção v3, aplicada offline em 07/09

`pfnCalcPrivateDeviceSize` e `pfnCloseAdapter` passaram a registrar. As três entradas do
adapter table agora falam. A próxima corrida distingue (a) de (b) sem custo adicional:

```text
CalcPrivateDeviceSize: Interface=0x........, Version=0x........ -> N bytes
CloseAdapter: adapter=0x........, Interface=0x........ -> S_OK
```

Se `CloseAdapter` aparecer **logo após** o `S_OK` e **antes** de nova carga da DLL, o runtime
desistiu de propósito e fechou o adapter — e a desistência está entre
`CalcPrivateDeviceSize` e `CreateDevice`. Se não aparecer, a DLL está sendo descarregada sem
fechar o adapter, o que é outra história.

## 4. A9 e A10 se provaram no silício, na mesma corrida

As correções de honestidade da instrumentação apareceram no log real e substituíram os
rótulos falsos:

```text
[SdmaPresent] BLT_ENGINE_2LEGS #1 (RPTR==WPTR + fence sobre poison;
              PERNA DE DADOS NAO CONFERIDA): WPTR=380 RPTR=380 Fence=00001004
              Poison=43DAFFFE Bytes=11059200

[Present #1] BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO): Source=0xAB0000 Dest=0x0
             Bytes=11059200 copiados fielmente, mas PostScanout NonZero=0/256
             Hash=0x00000000. Zero contra zero NAO prova caminho de pixel. BlankStreak=1
```

| Medida | Corrida 22:42 (antes) | **Esta corrida (A9/A10)** |
|---|---|---|
| `BLT_TRIPOD` | 1 (afirmava 3 pernas tendo 2) | **0** |
| `BLT_ENGINE_2LEGS` | não existia | **1** (rótulo honesto) |
| `BYTE_PROOF PASS` com frame preto | 3 | **0** |
| `BYTE_PROOF INCONCLUSIVO` | não existia | **2**, com `BlankStreak` |

**Nenhuma linha de sucesso falso saiu desta corrida.** Era exatamente o objetivo do A9/A10:
uma corrida que continua com a tela preta agora **diz** que continua com a tela preta.

## 5. O display convergiu de novo

18 `IsSupportedVidPn`, 1 `CommitVidPn`, 1 `SetVidPnSourceAddress`, 2 `SharedPrimary`,
**138 `Present`**. `NonZero=0/256` em 50 de 50 amostras — a primária segue em branco, pela
mesma causa já estabelecida no doc 169 e no dump: o DWM não obtém device D3D11. Dois dumps
do `dwm.exe` capturados.

## 6. Estado da máquina

Confirmação visual recusada (o adaptador não estava em `bc250mcdm` Código 0 no momento da
digitação), watchdog expirou, rollback e reboot automático. **Quarta recuperação limpa
consecutiva.**

## 7. Custo e saldo

Uma corrida gasta. Ela **não** respondeu as perguntas 1 e 3 do dimensionamento — isso é
falha do probe, não do plano. O que ela entregou:

1. **`P1`:** o step-down existe e é comandado pela recusa da UMD (era `H`).
2. **`P1`:** o runtime para depois de aceitar `0x000A0002` via `OpenAdapter10`, sem pedir
   caps nem device.
3. **`P1`:** A9 e A10 funcionam no silício; a instrumentação parou de mentir.
4. O ponto cego das 2 entradas mudas, agora fechado na v3.

**Compute Vulkan, submissão Node 1, Área de Trabalho visível e jogos: NÃO VALIDADOS EM
HARDWARE.**
