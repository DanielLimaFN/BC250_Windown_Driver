# Análise da corrida 2026-09-07 10:00 — `P7_D3D11_CAPS_PROBE` v4

**Candidato:** SYS `EAF66723…703BBC` · UMD `88844A1D…3FA50C0` (v4: allowlist por entry point,
`OpenAdapter10_2` aceitando `D3D11_0_7`, e regra de honestidade no `GetCaps`).
**Veredito:** **a segunda hipótese também caiu — e o resultado aponta um defeito NOSSO**, que
estava legível no header desde o começo.

---

## 1. A v4 fez o que foi projetada para fazer

```text
=== bc250umd.dll CARREGADA ===
OpenAdapter10_2: Interface=0x000B0011, Version=0x00060000
CAPS_PROBE: ARMADO
OpenAdapter10_2: Interface=0x000B0011 fora da allowlist deste entry point
                 -> E_NOTIMPL (o runtime desce a escada; alvo daqui e' 0x000B000B)
OpenAdapter10_2: Interface=0x000B000B, Version=0x00020009
UMDRIVERPRIVATE: blob OK -- v1, 104 bytes, Vendor=0x1002 Device=0x13FE,
                 NodeCount=2 (SDMA=0, Compute=1), GpuVaBits=48, PageSize=4096,
                 VramSegId=1 VramBytes=0xF570000, GateFlags=0x0000001B
OpenAdapter10_2: Interface=0x000B000B, Version=0x00020009 -> S_OK (BC250 Native)
=== bc250umd.dll CARREGADA ===                                  <-- e descarta
```

**58 ciclos** no `dwm.exe`. O `OpenAdapter10_2` aceitou `D3D11_0_7` e publicou a tabela de
**5 entradas** — a que tem `pfnGetSupportedVersions` e `pfnGetCaps`. O handshake
`UMDRIVERPRIVATE` seguiu perfeito: **142 publicações** no KMD, **zero** `NOT SUPPORTED`.

## 2. E o runtime continuou chamando nada

| Marcador real | Chamadas |
|---|---:|
| `GetSupportedVersions:` | **0** |
| `GetCaps:` | **0** |
| `INTERFACE ACORDADA` (CreateDevice) | **0** |
| `CalcPrivateDeviceSize:` | **0** |
| `CloseAdapter:` | **0** |
| `OpenAdapter10:` | **0** — não desceu mais, aceitamos no degrau de cima |

**A hipótese geométrica caiu.** Não era falta de `pfnGetCaps` na tabela de 3 entradas: com as
5 entradas publicadas e aceitas, o desfecho é idêntico.

## 3. Os três fatos que sobraram têm uma explicação só

1. Nem o **entry point** nem o **tamanho da tabela** mudam o desfecho.
2. O runtime chama **zero** funções — e, decisivamente, **nunca chama `pfnCloseAdapter`**.
3. Descarta a DLL e recomeça, 58 vezes.

**O fato 2 é a pista.** Um runtime que abriu um adapter e decide descartá-lo chamaria
`pfnCloseAdapter` — a não ser que esse ponteiro esteja **nulo do ponto de vista dele**.

E o header diz:

```c
union {
    D3D10DDI_ADAPTERFUNCS*     pAdapterFuncs;      // out: Driver function table
    D3D10_2DDI_ADAPTERFUNCS*   pAdapterFuncs_2;    // out: Driver function table
};
```

com `initializing-communication-with-the-direct3d-version-11-ddi.md` especificando que a
tabela é *"returned **in** the member … where **pAdapterFuncs_2 points to** a
`D3D10_2DDI_ADAPTERFUNCS` structure"*.

Isto é a convenção de `CREATEDEVICE.pDeviceFuncs`, que o header marca explicitamente
**`in/out`**: **o runtime aloca a struct e passa o ponteiro; o driver preenche os membros
dela.**

Nosso código fazia o oposto — **sobrescrevia o ponteiro** com o endereço da nossa tabela
estática. Se a convenção é essa, o runtime seguia lendo a struct **dele**, que permanece
zerada: uma tabela de ponteiros nulos. Isso explica **exatamente** o observado — nenhuma
função chamada, `CloseAdapter` inclusive (também nulo), e descarte imediato da DLL.

**Classificação:** `H` forte, ainda **não** provada. A v5 registra qual dos dois casos ocorre
e deixa a corrida decidir.

## 4. Correção v5, offline, em 07/09

Os dois casos passam a ser tratados **e registrados**:

```text
OpenAdapter10_2: runtime forneceu pAdapterFuncs_2=0x......
                 -> PREENCHENDO a struct dele (5 entradas)
```
ou
```text
OpenAdapter10_2: pAdapterFuncs_2 veio NULL
                 -> publicando ponteiro para a nossa tabela estatica
```

Mesmo tratamento no `OpenAdapter10` (3 entradas). Se sair a primeira linha, o defeito estava
ali desde sempre e as chamadas devem começar a aparecer na sequência. Se sair a segunda, a
suspeita está errada e o bloqueio é outro.

Pacote v5: SYS `525A8629…D676A2` · UMD `6249F69F…6C00C5` · CAT `ACC63CDE…EFD897`.

## 5. O resto da corrida

Display convergiu igual: 18 `IsSupportedVidPn`, 1 `CommitVidPn`, 1 `SetVidPnSourceAddress`,
**176 `Present`**, `device-after.json` com **Código 0**. Dois dumps do DWM.

**A9/A10 seguem honestos:** `BLT_TRIPOD` = **0**, `BLT_ENGINE_2LEGS` = 1,
`BYTE_PROOF PASS` = **0**, `BYTE_PROOF INCONCLUSIVO` = **3**. Nenhuma linha de sucesso falso
em cinco corridas consecutivas.

Tela preta, imagem de volta, watchdog reiniciou. **Sétima recuperação limpa.**

## 6. Autocrítica de custo

**Cinco corridas gastas no probe.** O saldo:

| Corrida | O que entregou |
|---|---|
| v1 (23:45) | `P1`: step-down é comandado pela recusa da UMD. Defeito meu: cap falso |
| v2 (08:05) | `P1`: step-down restaurado até `OpenAdapter10` |
| v3 (08:25) | Localizou `UMDRIVERPRIVATE` recusado — pelo **silêncio** das 3 entradas |
| item zero (09:40) | `P1`: handshake funciona. **Hipótese do `Type=0` refutada** |
| v4 (10:00) | **Hipótese da forma da tabela refutada.** Aponta defeito de convenção nosso |

Cada corrida eliminou uma hipótese, o que é progresso real. Mas se a v5 confirmar, o defeito
era uma **convenção de ponteiro legível no header desde o início**, e eu passei quatro
corridas testando o comportamento do runtime em vez de reler como a struct é preenchida.
O caminho curto era ler o contrato antes de instrumentar.

**Compute Vulkan, submissão Node 1, Área de Trabalho visível e jogos: NÃO VALIDADOS EM
HARDWARE.**
