# Análise da corrida 2026-09-06 23:45 — `P7_D3D11_CAPS_PROBE` v1

**Candidato:** pacote ativo com A9/A10 e o probe v1 · **SYS** `F0EC608C…D25128F` ·
**UMD** `2086D316…3CD19D5`
**Probe:** armado em `HKLM\SOFTWARE\BC250\Umd\D3D11CapsProbe = 1`
**Veredito:** o probe **falhou no seu propósito declarado** — não obteve caps nem interface
acordada. Mas produziu um achado decisivo, e ele é um **defeito meu**.

---

## 1. O que o probe deveria produzir, e não produziu

O roteiro previa quatro linhas em ordem. Saiu só a primeira:

| Linha esperada | Ocorrências no log |
|---|---:|
| `CAPS_PROBE: ARMADO` | **66** (dwm.exe) + 2 Explorer + 1 dxgiadaptercache + 1 AMDRSSrcExt |
| `GetSupportedVersions: declarado[i]=` | **0** |
| `GetCaps: Type=NNN (...)` | **0** |
| `D3D10 CreateDevice: INTERFACE ACORDADA=` | **0** |

O conjunto **completo** de linhas distintas que o `dwm.exe` produziu, nas 66 iterações:

```text
66 [dwm.exe] === bc250umd.dll CARREGADA ===
66 [dwm.exe] CAPS_PROBE: ARMADO (HKLM\SOFTWARE\BC250\Umd\D3D11CapsProbe)
66 [dwm.exe] OpenAdapter10_2: Interface=0x000B0011, Version=0x00060000
66 [dwm.exe] OpenAdapter10_2: Interface=0x000B0011, Version=0x00060000 -> S_OK (BC250 Native)
```

Nada mais. O runtime chamou `OpenAdapter10_2`, recebeu **`S_OK`**, e **não chamou mais
nenhuma DDI** — descarregou a DLL e repetiu o ciclo 66 vezes.

## 2. O defeito, nomeado

**`OpenAdapter10_2` da v1 aceitava qualquer `Interface`.** O runtime pediu `0x000B0011`
(minor 17, o DDI mais novo da escada) e nós respondemos `S_OK` — declarando suporte a um
DDI que não implementamos.

Duas consequências, ambas medidas:

**(a) O step-down morreu.** Na corrida 22:42, com `E_NOTIMPL` em tudo, o runtime desceu a
escada: `0x000B0011` → `0x000B000B` → `0x000A0002`, e chegou a pedir D3D10_1 via
`OpenAdapter10`. Nesta corrida **`OpenAdapter10` nunca foi chamado**: por termos aceito o
degrau mais alto, o runtime nunca desceu. O probe foi construído para observar os caps do
degrau de baixo e se fechou fora dele.

**(b) Era declaração de capacidade falsa.** Devolver `S_OK` para um DDI que não
implementamos é exatamente o que a **Lei 4** proíbe. Eu escrevi o probe com o cuidado de
manter `pfnCreateDevice` fail-closed e deixei passar a mentira um nível acima, no
`OpenAdapter`. O hardware pegou em **uma** corrida.

## 3. O que a corrida ensinou, e que a leitura não daria

`P1` (medido, não inferido):

1. **A negociação de DDI do runtime é uma escada descendente, e o degrau é escolhido pela
   recusa da UMD.** Aceitar interrompe a descida; não existe re-negociação para baixo dentro
   do processo depois de um `S_OK`.
2. **O runtime não chama `pfnGetSupportedVersions` como sondagem inicial.** A ordem real é:
   `OpenAdapter*` primeiro, com a versão que ele quer; só depois, se aceito, ele avança. A
   suposição de que `GetSupportedVersions` seria o primeiro contato estava errada.
3. O ciclo de **carga/descarga da DLL 66 vezes** dentro do mesmo `dwm.exe` mostra que o
   runtime reinicia a tentativa do zero, sem manter estado do adapter recusado.

`H` (não medido): que o `dwmcore` aceite feature level 10_1 continua **sem resposta** — a
pergunta 1 do doc 173 segue aberta, e a pergunta 3 (qual header é o minor 17) também.

## 4. Correção aplicada, offline, em 07/09

`Bc250UmdProbeAcceptsInterface` — **allowlist de um item** em
`driver/umd/bc250_umd_caps_probe.c`. O probe passa a aceitar somente
`0x000A0002` (`D3D10_1_DDI_INTERFACE_VERSION`), que é a única versão cujo alvo está
dimensionado (105 entradas de `D3D10_1DDI_DEVICEFUNCS`, doc 173), e recusa o resto com
`E_NOTIMPL` — **porque é a recusa que permite o runtime descer a escada**.

Log novo na recusa, para a próxima corrida mostrar a descida acontecendo:

```text
OpenAdapter10_2: Interface=0x000B0011 fora da allowlist do probe
                 -> E_NOTIMPL (deixa o runtime descer a escada ate 0x000A0002)
```

Travado por `TestProbeInterfaceAllowlist` em `bc250_d3d_umd_test`: aceita `0x000A0002`,
**recusa** `0x000B0011` e `0x000B000B` — os dois degraus que a v1 aceitou por engano — e
recusa `0`, `0xFFFFFFFF` e o vizinho `0x000A0003`. `build-umd.bat` em `EXIT=0`.

## 5. Estado da máquina e do display

O `dwm.exe` caiu de novo — dois dumps capturados (`dwm-crash-01-dwm.exe.8436.dmp`,
`dwm-crash-02-dwm.exe.10864.dmp`). A confirmação visual não foi dada (o prompt recebeu o
texto do comando de bind em vez de `DISPLAY_OK`), então o watchdog seguiu armado, expirou,
fez rollback e reiniciou a máquina. Depois do reboot: **BasicDisplay, Código 0**, fusível
`CLEAN (0)`, host sem resíduo. A recuperação automática funcionou pela terceira vez seguida.

## 6. Custo e conclusão

Uma corrida gasta. Ela **não** respondeu as três perguntas do dimensionamento, e é honesto
dizer isso: o probe v1 estava errado. O que ela entregou foi a mecânica da negociação — que
nenhuma leitura de header teria dado — e a correção que torna a próxima corrida capaz de
responder o que esta não respondeu.

**Compute Vulkan, submissão Node 1, Área de Trabalho visível e jogos: NÃO VALIDADOS EM
HARDWARE.**
