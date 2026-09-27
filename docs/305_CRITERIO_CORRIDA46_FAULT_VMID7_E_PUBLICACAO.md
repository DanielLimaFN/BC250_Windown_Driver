# 305 — Critério prévio da Corrida 46 — o fault de VMID 7 e a publicação de GPUVA

> **Data:** 20/09/2026, escrito **antes** do ensaio.
> **Candidato:** lab `3.206.1.0`, `DISPLAY_RENDER_LAB` (RenderNode=1, RenderOnlyLab=0),
> WDDM 2.7, schema 135, **P1 PERSISTENT SUBMISSION**.
> **Estado:** assinado e auditado; **NÃO INSTALADO; NÃO VALIDADO EM HARDWARE**.
> **Perfil idêntico ao 3.202.1.0 — zero diferenças de tunable.** A sonda de onda está
> **DESARMADA**, conforme o desfecho D do `docs/304`.

## 1. O que mudou, e por que nada está armado

Três faixas de **telemetria pura**, nenhuma tocando caminho de GPU:

- **Fault decodificado.** `[TDR_GCVM_FAULT] DECODIFICADO` nomeia `Vmid`, `WalkerError`,
  `MappingError`, `PermissionFaults`, `Cid`, `Rw`, `MoreFaults` e converte o endereço de página
  em GpuVa. Ausência de fault também é declarada, nunca silenciosa.
- **`PUBLICACAO_COMPOSICAO` isenta da amostragem.** Na Corrida 45 ela saiu **zero vezes** em 10
  `PUBLICACAO_FALHA`: a isenção do amostrador casa por **substring** e o nome escolhido não
  continha token isento.
- **Sonda de onda desarmada**, por ordem do critério anterior.

## 2. O achado que motiva esta corrida

Na Corrida 45, nos **cinco** TDRs, o log trazia `FaultStatus=0x0070153C`. Decodificado contra
`gc_10_1_0_sh_mask.h`:

| Campo | Valor |
|---|---|
| `VMID` | **7** — o mesmo VMID de todas as 142 submissões GFX |
| `WALKER_ERROR` | **6** |
| `MAPPING_ERROR` | **1** |
| `PERMISSION_FAULTS` | 3 |
| `CID` | 10 |
| `FaultAddr` (página) | `0x245B` e `0x2431` → GpuVa **`0x245B000`** e **`0x2431000`** |

E a onda lida no mesmo TDR trouxe `SQ_WAVE_STATUS FATAL_HALT=1` com
`SQ_WAVE_TRAPSTS XNACK_ERROR=1`. As duas medições, de registradores independentes, dizem a
mesma coisa: **o caminhador de tabela de páginas falhou sob VMID 7, a onda recebeu XNACK e
morreu, e o CP ficou esperando por quem nunca vai retirar.**

Esse dado estava no log **em hexadecimal cru** desde muito antes. Ninguém o leu porque a busca
do projeto era pela string `GFXHUB_FAULT`, que é outro sítio e sai **zero**. Não era lacuna de
conhecimento; era lacuna de **leitura**.

## 3. Hipótese falsificável

**H46:** o stall do IB GFX em `RPTR=24/WPTR=32` é consequência de uma **falha de tradução sob
VMID 7**, e não de estado do CP nem de pacote malformado.

**H46b (subordinada, e esta é a que aponta o conserto):** o endereço que falta traduzir está
entre os que a **publicação de GPUVA não conseguiu publicar**. A Corrida 45 teve 10
`PUBLICACAO_FALHA`, e o mecanismo já está fechado por aritmética: o slot inativo é cópia do
ativo e o lote entra por cima, então o teto útil é `Cap − Mapped`.

## 4. Sucesso da faixa — medição, não correção

Esta corrida **não tenta consertar nada**. Sucesso é obter as três medidas:

1. `[TDR_GCVM_FAULT] DECODIFICADO` aparece, com `Vmid` e `GpuVa` nomeados, em pelo menos um TDR;
2. `[ActiveFlushTlb] PUBLICACAO_COMPOSICAO Maps=N Unmaps=M Livres=L` aparece em **toda**
   `PUBLICACAO_FALHA` — se houver falha e não houver composição, a isenção do amostrador ainda
   está errada;
3. `[TDR_GFX_WAVEPC] DESARMADO` aparece, confirmando que a sonda não escreveu seletor nenhum.

**Não** faz parte do sucesso: o stall sumir, haver desktop, haver pixel.

## 5. A correlação que decide o próximo conserto

Com (1) e (2) na mesma corrida, comparar:

- **`Maps > Livres`** → a publicação falha por **capacidade real**; reordenar unmaps não
  resolveria, e a discussão passa a ser o teto de 1024 contra a pressão do DWM.
- **`Unmaps` altos** → a **ordem** é o defeito; aplicar unmaps antes de maps derruba o pico sem
  mudar o estado final, porque o journal garante intervalos não sobrepostos.

E, cruzando com o fault:

- **GpuVa do fault dentro da faixa de um lote que falhou** → H46b **confirmada**, e a cadeia
  causal fecha: publicação falha → mapeamento não chega à tabela → walker falha → XNACK →
  `FATAL_HALT` → CP travado → TDR. O conserto passa a ser a publicação, não o CP.
- **GpuVa do fault fora de qualquer lote falho** → H46b **refutada**; o fault tem outra origem,
  e as duas dívidas voltam a ser independentes.

## 6. Controle a não regredir

- `[TDR_GFX_RESTART_BASE] Pass=1` e `[TDR_GFX_RESTART_PROOF] PASS`, 5/5 nas Corridas 44 e 45;
- submissões GFX na casa das centenas com `Submetidas == Aposentadas` ao fim;
- ausência de BugCheck — duas corridas seguidas sem nenhum.

## 7. Falha informativa — assinatura e implicação separadas

| # | Assinatura no log | Implicação |
|---|---|---|
| A | Nenhum `[TDR_GCVM_FAULT] DECODIFICADO`, só `SEM FAULT LATCHADO` | O fault não reincidiu nesta corrida. H46 fica **sem teste**, não refutada — na Corrida 45 o status estava latchado nos cinco TDRs, mas isso não garante repetição. |
| B | Fault decodificado com **`Vmid != 7`** | O fault existe mas não é do contexto das submissões GFX. H46 **refutada como formulada**, e o suspeito passa a ser o contexto que o `Vmid` nomear. |
| C | Fault com `Vmid=7` **e** composição publicada | **H46 testada de verdade.** A §5 decide o próximo conserto, e ele passa a ter número. |
| D | `PUBLICACAO_FALHA` sem `PUBLICACAO_COMPOSICAO` ao lado | A isenção do amostrador ainda está errada — defeito **meu**, de instrumentação, não do driver. |
| **E** | **Qualquer coisa fora de A–D** | **Descrever o observado ANTES de julgá-lo.** Inclui BugCheck, regressão do controle da §6, `WALKER_ERROR` com valor diferente de 6, `MoreFaults=1` indicando fault perdido, e **qualquer resultado melhor que o previsto** — as Corridas 44 e 45 caíram aqui e foi de lá que vieram os dois maiores achados. |

## 8. Desfecho parcial não é desfecho

Lei 10, regra 3. Fault decodificado **sem** a composição é desfecho D, não "C parcial". Os três
itens da §4 valem juntos ou não valem.

## 9. Falha de contrato

- rearmar a sonda de onda antes de explicar o `selector_stuck` da Corrida 45;
- declarar a cadeia causal fechada sem a correlação da §5;
- tratar ausência de fault latchado como refutação de H46;
- perder a cauda do KMD;
- tratar BasicDisplay ou reboot como recuperação do **nosso** adaptador;
- alterar este critério depois de ver o resultado.

## 10. Sequência física

Estado na preparação: BC-250 em BasicDisplay `Status OK`, fusível inexistente, sem reboot
pendente. **Boot frio continua obrigatório.**

```powershell
& 'C:\BC250_RECOVERY.cmd'
powershell -ExecutionPolicy Bypass -File .\tools\inspect-boot-fuse.ps1 -Reset
shutdown /s /t 0
```

Depois do boot frio:

```powershell
.\tools\EXECUTAR_D3D11_NATIVO.cmd -CaptureDwmDump
```


---

## 11. Emenda pós-Corrida 46 — o discriminador de granularidade (Corrida 47)

> **Escrita em 20/09, ANTES da Corrida 47.** A Corrida 46 **cumpriu** os três itens da §4
> (desfecho C) e respondeu a §5: `Unmaps=0` em 11 de 11 → a publicação falha por **capacidade**,
> não por ordem. O que ela abriu é um garfo novo, e é ele que a Corrida 47 fecha.

**Candidato:** lab `3.207.1.0`, perfil **idêntico ao 3.202** (zero diferenças de tunable), sonda
de onda **desarmada**. Só telemetria nova; nenhum caminho de GPU tocado.

### 11.1 O garfo

`gc_10_1_0_sh_mask.h` declara `LOGICAL_PAGE_ADDR` e **não** declara o tamanho da página. Com o
valor medido `0x2431`, as duas conversões defensáveis caem em lugares opostos:

| Conversão | Endereço | O que significaria |
|---|---|---|
| `<< 12` | `0x2431000` (36 MB) | Região **vazia** — nenhuma alocação. Ponteiro perdido |
| `<< 20` | `0x243100000` (9,05 GB) | **Entre** `0x242F00000` e `0x243340000`, alocações reais da winsys. Acesso além do fim de um buffer |

**Não escolhi nenhuma.** Nesta investigação duas hipóteses minhas com aritmética convincente já
morreram por leitura de código: a de que o endereço era a VA deslocada 8 bits (era a codificação
**correta** de `CB_COLOR0_BASE`), e a de que o IB apontava para 36 MB (eram ponteiros de 9,2 GB
corretamente codificados). Escolher por intuição aqui repetiria o erro pela terceira vez.

### 11.2 Quem escolhe é a medição

Cada candidato é procurado no snapshot publicado de GPUVA por `Bc250GpuvaSpaceDescribeAddress`
— leitor **puro**, sem lock, sem exigir dono (no TDR não se sabe de quem é o endereço). Só faixa
`MAPPED` conta: aceitar `TEARDOWN` seria um falso positivo capaz de **inverter** o veredito.

Quando não acha, a linha diz **quão longe** ficou da faixa viva mais próxima — é essa distância
que separa estouro de buffer de ponteiro perdido.

### 11.3 Desfechos

| # | Assinatura | Implicação |
|---|---|---|
| A | `CANDIDATO <<20 ... DENTRO da faixa viva Base=... Alloc=...` | Granularidade é 1 MB, e o fault é **acesso dentro de uma alocação conhecida** que não traduz. O conserto vira residência/PTE daquela alocação, que passa a ter nome |
| B | `CANDIDATO <<12 ... DENTRO da faixa viva` | Granularidade é 4 KB e existe alocação em 36 MB que os logs não mostravam — a ausência anterior era da **amostragem**, não do espaço |
| C | Ambos `FORA`, mas `<<20` com `dist` pequena do fim de uma faixa | **Estouro de buffer**: o shader lê logo depois do fim de uma alocação real. O número diz quantos bytes |
| D | Ambos `FORA` e ambos longe de tudo | O endereço não pertence a nada que o driver conhece. Suspeito passa a ser descritor com lixo, e o alvo vira a emissão de descritores na UMD |
| **E** | **Fora de A–D** | **Descrever antes de julgar.** Inclui `SEM VEREDITO` (snapshot indisponível no TDR), `LiveScan=0`, e qualquer resultado melhor que o previsto |

### 11.4 Não faz parte do sucesso

O stall sumir, haver desktop, haver pixel. Esta corrida **mede**, e os quatro desfechos A–D
nomeiam consertos diferentes — é isso que ela compra.

---

## 12. Emenda pós-Corrida 47 — a caminhada no ponto do fault (Corrida 48)

> **Escrita em 20/09, ANTES da Corrida 48.** A Corrida 47 cumpriu a §11 no **desfecho A**, 5 de 5.

**Candidato:** lab `3.208.1.0`, perfil **idêntico ao 3.202**, sonda de onda desarmada. Só
telemetria; nenhum caminho de GPU tocado.

### 12.1 O que a Corrida 47 fechou

A granularidade é **1 MB**. Em 5 de 5, `<<20` caiu **DENTRO** de faixa viva e `<<12` caiu num
vazio de gigabytes. Duas alocações nomeadas, do mesmo processo `0x2B`:

| Alloc | Base | Size | Offset do fault |
|---|---|---|---|
| `0xF3` | `0x242F00000` | `0xA8C000` (= 2560×1080×4) | `0x200000` |
| `0xDF` | `0x245000000` | `0xB80000` | `0xB00000` |

**Retirada:** o item DX.3 dizia que o fault era independente da publicação porque "36 MB está
fora de 6–16 MB". Essa comparação usava a interpretação `<<12`, agora refutada. A conclusão
**cai**; a independência não está estabelecida.

### 12.2 A pergunta desta corrida

O snapshot declara a faixa **`MAPPED`** e o caminhador do hardware **falha** nela. Software e
hardware discordam, e o log de 47 não sabia dizer de que lado: `[ActiveUpdatePT]` é classe
**amostrada**, então ausência de linha de PTE não prova ausência de PTE. Foi a **terceira** vez
que a amostragem barrou uma conclusão nesta investigação.

Agora a resposta sai do silício: no ponto do fault, caminha-se a tabela do **VMID 7** — o VMID
que o próprio fault nomeia — no endereço exato que falhou.

**H48:** a faixa está publicada no snapshot mas **não tem PTE** na tabela do VMID 7.

### 12.3 Sucesso da faixa

1. `[TDR_GCVM_FAULT] WALK <<20 = 0x... ` aparece com veredito — `RESOLVEU` ou `NAO RESOLVE`;
2. quando não resolve, as linhas `[PT_WALK FAULT_VMID7]` acima dizem **em que nível** parou;
3. o discriminador da §11 continua saindo (controle de que nada regrediu).

### 12.4 Desfechos

| # | Assinatura | Implicação |
|---|---|---|
| A | `WALK <<20 ... NAO RESOLVE`, com `[PT_WALK FAULT_VMID7] FALHA L1` ou `L2` | **H48 confirmada**: a PTE não existe. O defeito é a escrita da tabela, não o hardware. O nível que falhou diz se falta PDE ou PTE |
| B | `WALK <<20 ... RESOLVEU para PA=0x...` | **H48 refutada**: a PTE existe e o walker do hardware ainda falha. Suspeito passa a ser TLB obsoleto, permissão, ou a própria raiz instalada no VMID 7 |
| C | `raiz do VMID 7 AUSENTE` | O contexto de usuário não tinha raiz no instante do TDR. Isso por si só explicaria o `WALKER_ERROR`, e o alvo vira a instalação da raiz |
| D | `runtime caps INDISPONIVEIS` | Sem `VramMcBase` não se caminha. Defeito de ordem interna, não do alvo |
| **E** | **Fora de A–D** | **Descrever antes de julgar.** Inclui fault que não reincide, BugCheck, regressão do controle, e qualquer resultado melhor que o previsto |

### 12.5 Risco e escopo

A caminhada é **somente leitura** e só é legal ali porque `DXGKDDI_RESETFROMTIMEOUT` é
`_IRQL_requires_(PASSIVE_LEVEL)` (`d3dkmddi.h`) — a leitura de VRAM fora da janela do
framebuffer usa `MmMapIoSpace`, que exige IRQL ≤ APC_LEVEL. Em DISPATCH_LEVEL seria defeito; o
projeto já pagou `0x0A`, `0x133` e `0x139` por erros dessa família.

Não faz parte do sucesso: o stall sumir, haver desktop, haver pixel.
