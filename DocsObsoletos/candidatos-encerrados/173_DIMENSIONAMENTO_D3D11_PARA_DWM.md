# 173 — Dimensionamento do D3D11 que o `dwmcore` exige

**Data:** 2026-09-06 · **Custo:** zero corridas físicas · **Método:** decodificação do log da
UMD da corrida 22:42 contra os headers do WDK 10.0.26100 (`um/d3d10umddi.h`) e o dump do DWM.
**Objetivo:** medir o tamanho da única obra que destrava WDDM acelerado por GPU — a barreira
que o dump de 06/09 provou ser obrigatória.

> **Classificação:** `P1` (derivado de fonte normativa e de log medido) para tudo que está em
> tabela; `H` onde explicitamente marcado. **Nada aqui é validado em hardware.**

---

## 0. ITEM ZERO — o handshake `UMDRIVERPRIVATE` (acrescentado em 07/09 após 3 corridas)

> Esta seção foi escrita **depois** do resto do documento e o corrige na ordem de execução.
> As três corridas do `P7_D3D11_CAPS_PROBE` (06/09 23:45, 07/09 08:05, 07/09 08:25) mediram
> que **nada** do que está dimensionado abaixo é alcançável antes disto.

**Medido (`P1`) na corrida 07/09 08:25:**

```text
[McdmQAI] Type=0, OutSize=2400, CapsStatus=0x00000000
[McdmQAI] Type=0 NOT SUPPORTED                      <- 80 vezes
```

`Type=0` é **`DXGKQAITYPE_UMDRIVERPRIVATE`** (confirmado em `shared/d3dkmddi.h`:
`DXGKQAITYPE_UMDRIVERPRIVATE = 0`), o canal de **private driver data** entre o nosso KMD e o
nosso UMD. Ele **não tem `case`** no switch de `Bc250McdmQueryAdapterInfo` e cai no
`default: return STATUS_NOT_SUPPORTED`.

Na mesma corrida, com as três entradas de `D3D10DDI_ADAPTERFUNCS` instrumentadas:

| Entrada | Chamadas |
|---|---:|
| `pfnCalcPrivateDeviceSize` | **0** |
| `pfnCloseAdapter` | **0** |
| `pfnCreateDevice` | **0** |
| `pfnGetSupportedVersions` | **0** |
| `pfnGetCaps` | **0** |

O runtime aceita o `S_OK` do `OpenAdapter10`, chama **nenhuma** função e descarta a DLL **sem
nem fechar o adapter**. Ele decide pelos dados que já tem — e o dado que falta é o blob.

Distribuição dos tipos de QAI naquela corrida: `Type=0` **160** chamadas (80 recusadas),
contra 1 ou 2 de cada um dos outros (`DRIVERCAPS`, `GPUMMUCAPS`, `QUERYSEGMENT4`,
`PAGETABLELEVELDESC`). **`UMDRIVERPRIVATE` é o tipo mais pedido e o único recusado em massa.**

### Consequência para a ordem de execução

```text
[0] handshake UMDRIVERPRIVATE (KMD responde + UMD consome)   <- ANTES de tudo
[1] pfnGetSupportedVersions / pfnGetCaps respondidos
[2] pfnCreateDevice devolvendo device
[3] as 105 entradas de D3D10_1DDI_DEVICEFUNCS
[4] compilador DXBC->SPIR-V->ACO e pipeline 3D (rota B)
```

### Por que isto é favorável à rota B

O blob é **nosso nos dois lados** — nós definimos o formato no KMD e o consumimos no UMD.
Não há ABI da AMD para imitar, que é precisamente a fragilidade que a Collabora aponta no
próprio port do RADV ("private driver data obtido por engenharia reversa", acoplamento
"inerentemente frágil"). Quando o winsys da branch `wddm2` for adaptado, ele passa a falar o
**nosso** blob versionado.

### Classificação honesta

`P1`: o runtime pede `UMDRIVERPRIVATE` 160 vezes e recusamos 80; nenhuma entrada do adapter
table é chamada.
`H`: que **essa** recusa seja a causa do descarte. É a hipótese mais forte, e coerente com
toda a cadeia — mas nada na documentação lida declara `UMDRIVERPRIVATE` **obrigatório** para
Full Graphics, e `mcdm-implementation-guidelines.md` chega a listá-lo como **opcional** (para
MCDM, que não é o nosso perfil). **Só uma corrida com o tipo implementado decide.**

---

## 0b. RESPOSTA MEDIDA (07/09 10:11) — as três perguntas do item 6 estão fechadas

A corrida `P7_D3D11_CAPS_PROBE` v5 fez o runtime conversar pela primeira vez. O defeito que
bloqueava tudo era **nosso**: `pAdapterFuncs_2` vem **não-NULO** — o runtime aloca a
`D3D10_2DDI_ADAPTERFUNCS` e o driver **preenche os membros dela**; nós sobrescrevíamos o
ponteiro, e o runtime seguia lendo a struct dele, zerada.

Prova de que a correção pegou: `pfnCloseAdapter` foi chamado **95 vezes** (zero nas quatro
corridas anteriores).

| Pergunta (item 6) | Resposta medida |
|---|---|
| O `dwmcore` aceita feature level 10_1? | **Sim.** Abrimos em `0x000B000B` e declaramos D3D10_0/D3D10_1 em `pfnGetSupportedVersions` (90 chamadas); o runtime aceitou e prosseguiu |
| Quais caps ele valida antes do device? | **Exatamente um:** `D3D11DDICAPS_3DPIPELINESUPPORT`, `DataSize=4`, `pInfo=NULL` — 63 chamadas. Nem `THREADING`, nem `SHADER`, nem `OPTIONS` |
| Qual header é o minor 17? | **Deixou de importar** — `0x000B000B` funciona |

### A cadeia completa, e o último portão

```text
OpenAdapter10_2(0x000B000B) -> preenche a tabela de 5 entradas -> S_OK
  -> GetSupportedVersions  -> declaramos D3D10_0 + D3D10_1          [OK]
  -> GetCaps(3DPIPELINESUPPORT, 4 bytes) -> RECUSAMOS               [PARA AQUI]
  -> CloseAdapter                                                    [fecha limpo]
```

**Não existe truque de cap que passe deste ponto.** `D3D11DDI_3DPIPELINELEVEL` não tem valor
para "nenhum" — `0` significa nível `10_0`. Declarar qualquer coisa ali é afirmar um pipeline
3D inexistente; e se declarássemos, o runtime chamaria `pfnCreateDevice` e em seguida as
**105 entradas** que não existem, trocando uma recusa honesta por um crash com cap falso.

**Portanto, agora medido e não inferido:** a única coisa entre este driver e um device D3D é
um **pipeline 3D real**. A ordem de execução das seções seguintes está confirmada degrau por
degrau. Evidência: `docs/evidence/2026-09-07_10-11-27-.../ANALISE.md`.

---

## 1. O que o `dwm.exe` pediu, decodificado

A `bc250umd.dll` registrou as três tentativas do runtime. Cruzando os valores de
`D3D10DDIARG_OPENADAPTER.Interface` com os `#define` do header:

| Ordem | Entry point | `Interface` | Nome no WDK | `Version` |
|---:|---|---|---|---|
| 1 | `OpenAdapter10_2` | `0x000B0011` | **sem nome neste header** — major 11, minor **17**, logo acima de `D3DWDDM1_3` (16) e abaixo de `D3DWDDM2_0` (0x000B0020) | `0x00060000` |
| 2 | `OpenAdapter10_2` | `0x000B000B` | **`D3D11_0_7_DDI_INTERFACE_VERSION`** | `0x00020009` |
| 3 | `OpenAdapter10` | `0x000A0002` | **`D3D10_1_DDI_INTERFACE_VERSION`** | `0x00010000` |

Contagem no log: **1024** chamadas a `OpenAdapter10_2`, **512** a `OpenAdapter10`, **2** a
`OpenAdapter12`, e **770** respostas `E_NOTIMPL`.

**Leitura:** o runtime desce a escada. Ele tenta o DDI mais novo, cai para o D3D11.0 da era
Windows 7, e por fim aceita conversar em **D3D10.1**. Isso define o **piso**: o alvo mais
barato que ainda satisfaz o pedido é `D3D10_1_DDI_INTERFACE_VERSION`.

> ⚠️ O minor 17 (`0x000B0011`) não tem `#define` no WDK 26100. É um número da era Win10 que
> saiu do header atual. Não inventamos nome para ele; se for necessário atender exatamente
> esse valor, o número precisa ser confirmado num header de WDK contemporâneo ao 19041.

---

## 2. Tamanho das tabelas, contado no header

| Tabela | Entradas | Quando é a exigida |
|---|---:|---|
| `D3D10DDI_ADAPTERFUNCS` | **3** | `OpenAdapter10` |
| `D3D10_2DDI_ADAPTERFUNCS` | **5** | `OpenAdapter10_2` |
| `D3D10DDI_DEVICEFUNCS` | 103 | device D3D10.0 |
| **`D3D10_1DDI_DEVICEFUNCS`** | **105** | **piso viável (item 3 da tabela acima)** |
| `D3D11DDI_DEVICEFUNCS` | 152 | D3D11.0 completo |
| `D3D11_1DDI_DEVICEFUNCS` | 157 | D3D11.1 |

A tabela de adapter é minúscula e é onde tudo começa:

```c
typedef struct D3D10_2DDI_ADAPTERFUNCS {
    PFND3D10DDI_CALCPRIVATEDEVICESIZE   pfnCalcPrivateDeviceSize;
    PFND3D10DDI_CREATEDEVICE            pfnCreateDevice;
    PFND3D10DDI_CLOSEADAPTER            pfnCloseAdapter;
    PFND3D10_2DDI_GETSUPPORTEDVERSIONS  pfnGetSupportedVersions;
    PFND3D10_2DDI_GETCAPS               pfnGetCaps;
} D3D10_2DDI_ADAPTERFUNCS;
```

### Composição das 105 entradas do piso (D3D10.1)

| Subsistema | Entradas |
|---|---:|
| Recurso e view (buffers, texturas, map/unmap, copy, clear) | **39** |
| Estado fixo (blend, depth, rasterizer, sampler, viewport, IA, RT) | **26** |
| Shader (criação, bind, SRV) | **17** |
| Query / predicate / counter | 8 |
| Ciclo de vida e misc | 10 |
| Desenho (`Draw*`) | 5 |
| **Total** | **105** |

---

## 3. Onde está o custo de verdade — não é a contagem

As 105 entradas são a **superfície**. O custo está concentrado em três obras que a contagem
esconde:

**3.1 Compilador de shader (DXBC → ISA do GFX10).** As entradas
`pfnCreateVertexShader`, `pfnCreatePixelShader`, `pfnCreateGeometryShader` e
`pfnCalcPrivateShaderSize` recebem **bytecode DXBC** e precisam devolver código executável
para o silício. Isso é um compilador, não um stub. É a maior peça isolada do item.

**3.2 Pipeline 3D real.** As 26 entradas de estado fixo e as 5 de desenho exigem programar
rasterizador, blend, depth/stencil, render targets e viewport no GFX10 — hoje o projeto tem
compute (MEC/HQD) e SDMA provados, mas **nenhum caminho gráfico**.

**3.3 Recursos e views sobre a nossa GPUVA.** As 39 entradas de recurso precisam alocar,
mapear e versionar superfícies em cima de `BC250_ALLOCATION_PRIVATE_V1` e do journal GPUVA —
que existe, mas cuja submissão real ainda depende do **B1** (IB sob VMID8 não executa).

---

## 4. O piso de feature level, e uma restrição achada no header

`D3D11DDI_3DPIPELINELEVEL` (o que o `pfnGetCaps` declara em `3DPIPELINESUPPORT`):

```text
D3D11DDI_3DPIPELINELEVEL_10_0   = 0
D3D11DDI_3DPIPELINELEVEL_10_1   = 1
D3D11DDI_3DPIPELINELEVEL_11_0   = 2
D3D11_1DDI_3DPIPELINELEVEL_11_1 = 3   (exige MINOR_HEADER_VERSION >= 3)
D3D11_1DDI_3DPIPELINELEVEL_9_1  = 4   (idem)
D3D11_1DDI_3DPIPELINELEVEL_9_2  = 5   (idem)
D3D11_1DDI_3DPIPELINELEVEL_9_3  = 6   (idem)
```

**Consequência prática:** os níveis **9_x — o caminho "10level9", que seria o mais barato —
só existem a partir do DDI D3D11_1** (`0x000B000F`). Na interface `D3D11_0_7` que o DWM pediu
em segundo lugar, o enum oferece apenas 10_0, 10_1 e 11_0. Ou seja: **não há como declarar
9_1 no caminho de piso**; declarar 9_1 exigiria atender a interface `0x000B0011` (minor 17),
que é mais nova, não mais barata.

Portanto o piso real de capacidade é **feature level 10_1** — shader model 4.1. O silício
GC 10.1.3 dá conta; o que falta é nosso backend.

---

## 5. O caminho que reaproveita o investimento em RADV

`pfnCreateVertexShader` e companhia recebem **DXBC**. O RADV compila **SPIR-V** (via ACO) e
já tem pipeline gráfico completo para GFX10. Existe precedente estabelecido de tradução em
modo usuário: DXVK e VKD3D-Proton convertem DXBC/DXIL para SPIR-V.

Duas rotas, e a segunda é a que serve ao objetivo declarado do projeto:

| Rota | O que exige | Avaliação |
|---|---|---|
| **A — DXBC → ISA nativo** | Compilador próprio de DXBC para RDNA, mais pipeline gráfico próprio | Maior obra do projeto; nada reaproveitável |
| **B — DXBC → SPIR-V → RADV/ACO** | Tradutor DXBC→SPIR-V no UMD + winsys da branch `wddm2` adaptado ao nosso *private data* | Reaproveita compilador e pipeline gráfico já existentes e testados no GFX10; alinhado com o item 146 |

**Recomendação (`H`, decisão de arquitetura ainda não tomada):** rota **B**. Ela une as duas
frentes do objetivo — o D3D11 que o DWM exige e o Vulkan que os jogos exigem — em um único
backend, em vez de construir dois.

---

## 6. O que este dimensionamento NÃO responde

Honestamente, três coisas continuam abertas e **nenhuma delas se resolve por leitura**:

1. **O `dwmcore` aceita um device de feature level 10_1?** Ele *pediu* o DDI D3D10.1, o que é
   evidência, **não prova**. `H`.
2. **Quais valores de caps ele valida?** `D3D11DDICAPS_THREADING`, `D3D11DDICAPS_SHADER` e
   `D3D11DDICAPS_3DPIPELINESUPPORT` são consultados antes do device; quais combinações o DWM
   recusa é desconhecido.
3. **Qual header corresponde ao minor 17** (`0x000B0011`) na plataforma 19041.

### O experimento barato que responde os três

Implementar `OpenAdapter10` / `OpenAdapter10_2` **apenas até os caps** — devolver tabela de
adapter válida, responder `pfnGetSupportedVersions` e `pfnGetCaps`, e **falhar fechado em
`pfnCreateDevice`**. Isso não é fallback de software nem cap falso: é instrumentação honesta
que faz o runtime revelar, em **uma** corrida, qual interface ele fecha, quais caps ele
valida e o que pede em seguida. Custa uma corrida e não promete render nenhum.

Nome sugerido do teste: **`P7_D3D11_CAPS_PROBE`**.

---

## 7. Resumo do dimensionamento

```text
Item ZERO ............... handshake UMDRIVERPRIVATE (Type=0) -- bloqueia todo o resto
Piso de interface ....... D3D10_1_DDI_INTERFACE_VERSION (0x000A0002)
Tabela de adapter ....... 3 funcoes (OpenAdapter10) ou 5 (OpenAdapter10_2)
Tabela de device ........ 105 entradas (D3D10_1DDI_DEVICEFUNCS)
Feature level de piso ... 10_1 (9_x indisponivel nesta interface)
Obras escondidas ........ compilador DXBC->ISA, pipeline 3D, recursos sobre GPUVA
Rota recomendada ........ DXBC -> SPIR-V -> RADV/ACO (rota B), a confirmar
Proximo passo ........... implementar UMDRIVERPRIVATE no KMD + consumo no UMD
Probe .................. 3 corridas gastas; localizou o item zero
```

**Prazo:** não é dado. Com o compilador de shader dentro do escopo, qualquer estimativa em
semanas seria palpite — e a Lei do escopo aceita demorar, não aceita fingir previsão.

**NÃO VALIDADO EM HARDWARE.**
