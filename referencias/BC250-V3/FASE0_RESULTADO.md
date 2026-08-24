# Fase 0 — Purga de Mentiras: relatório de execução

**Data:** 2026-08-22
**Build do KMD gerado:** `1.0.0822.0725` (id 8220725)
**Driver em execução durante as medições:** `1.0.822.39` (pré-Fase 0 — o novo `.sys` está compilado e assinado, mas ainda não instalado)

---

## 1. O que a Fase 0 encontrou que não estava no plano

A Fase 0 era para ser mecânica: remover fallbacks e inverter asserções. Duas descobertas mudaram o escopo.

### 1.1 A tabela de registradores estava errada — e a minha análise anterior também

O item 0.8 pedia só para corrigir `A_CP_STAT`. Ao procurar o offset real em `gc_10_1_0_offset.h` (que já tínhamos em `referencias/BC250-windowsDriverTest/docs/upstream/generated/`), a fórmula de derivação apareceu:

```
BAR5_byte = (segment_base + mmREG) * 4
segment_base = 0x1260 (BASE_IDX 0)  |  0xA000 (BASE_IDX 1)
```

Confirmada contra os offsets que já funcionavam: `mmCP_ME_CNTL 0x0F56` → `(0x1260+0x0F56)*4 = 0x86D8`, exatamente o valor do nosso driver. Os registradores do CP ring estavam todos certos. **Tudo o mais estava errado**, incluindo os offsets que eu havia reportado como "confirmados no silício" na análise anterior — eu tinha copiado os valores do projeto de referência, que também estão errados.

| Registrador | Estava | Correto | Diferença observada |
|---|---|---|---|
| `GRBM_STATUS` | `0x03040` | **`0x08010`** | lia `0x00000000` constante → agora `0x00003028` |
| `CP_STAT` | `0x08684` | **`0x08680`** | lia fill `0xDEF4DEF4` → agora `0x00000000` |
| `SPI_PG_ENABLE_STATIC_WGP_MASK` | `0x05C3C` | **`0x0935C`** | lia `0` → agora `0x0000FFFF` |
| `CC_GC_SHADER_ARRAY_CONFIG` | `0x09C1C` | **`0x089BC`** | → `0xFFF80000` (= 24 CU stock) |
| `SCRATCH_REG0/1` | `0x032D4` | **`0x30100/4`** | `0x32D4` não aceita escrita; `0x30100` aceita |
| `GRBM_GFX_INDEX` | `0x034D0` | **`0x30800`** | idx 1, não idx 0 |
| `RLC_PG_ALWAYS_ON_WGP_MASK` | `0x03D64` | **`0x3B14C`** | lia fill `0xFFFFFFFF` → agora `0x00000003` |
| bloco `COMPUTE_*` | `0x080E0+` | **`0x0B800+`** | escrevia em endereços aleatórios |

O driver estava escrevendo a configuração de compute inteira em endereços que não são os registradores de compute.

### 1.2 `SPI_PG` não é SOS-locked — o registrador é outro

No offset correto `0x0935C`, `SPI_PG_ENABLE_STATIC_WGP_MASK` lê `0x0000FFFF` e **uma escrita de `0x1F` gruda**. Foi restaurado a `0x0000FFFF` imediatamente.

A conclusão que a comunidade inteira carrega — "WGPs fundidos / travados pelo Secure OS" — vem de ler e escrever `0x5C3C`, que é um registrador diferente. O projeto de referência testou exaustivamente todas as combinações de `GRBM_GFX_INDEX` contra `0x5C3C` e concluiu "SOS-lock definitivo". Estava sondando o endereço errado o tempo todo.

Isso **não** significa que 40 CUs estão destravadas. Significa que a hipótese precisa ser reaberta com os offsets certos, e que `CC_GC_SHADER_ARRAY_CONFIG = 0xFFF80000` bate exatamente com o "stock 24 CU" documentado pela referência — o que confirma que `0x089BC` é o registrador certo.

**Por segurança, a reconfiguração de WGP no bring-up ficou desligada por padrão** (`HwCuUnlockMode = 0` só observa). Nos offsets antigos essas escritas caíam em endereços inválidos e eram inofensivas; nos corretos elas realmente reconfiguram o shader array, e fazer isso sem DPM do SMU não está validado.

### 1.3 A SDMA0 não está validada — retratação

Eu havia afirmado, nas duas análises anteriores, que a SDMA0 estava "genuinamente comprovada no silício, 12,2 GB/s com fence de GPU". **Está errado.**

O teste antigo só verificava o código de retorno do escape e imprimia a largura de banda. O teste novo escreve um padrão conhecido na origem, **envenena o destino com `0x5C`**, copia, e compara byte a byte:

```
[MEDIDO]    Origem apos escrita   : 0xA5 0x01 0xEC 0xCB  (padrao 0xA5 0x01 0xEC 0xCB)  [OK]
[MEDIDO]    Destino depois        : 0x5C 0x5C 0x5C 0x5C  (origem 0xA5 0x01 0xEC 0xCB)
[MEDIDO]    Releituras ate' o conteudo aparecer: 50
```

O poison está intacto após 50 releituras em 100 ms.

> ⚠️ **Corrigido na segunda rodada (§7b.5).** Esta medição é do driver antigo,
> que não reportava `FenceObserved`. Com o KMD da Fase 0 a fence **é** escrita
> pela GPU e a cópia funciona parte das vezes. A retratação sobre os 12 GB/s
> continua válida.

A causa está no critério de conclusão em `wddm_hw.c`:

```c
if (*fencePtr == fenceValue || currentRptr == targetWptrBytes || ...) break;
```

É um **OU**. O ramo `RPTR == WPTR` basta para devolver `STATUS_SUCCESS`. Mas RPTR alcançar WPTR prova apenas que o motor consumiu o frame do anel — não que executou a cópia. O motor engole o `SDMA_OP_COPY`, processa o `SDMA_OP_FENCE`... e mesmo a fence não é escrita.

E os "12.232 MB/s"? Eram `tamanho ÷ duração`, onde a duração é latência de submissão. Com 4 KB em vez de 4 MB, o mesmo código reporta 325 MB/s. O número nunca mediu transferência.

Isso é coerente com `Hw_Fw_SDMA0` retornando `TEE_ERROR_ITEM_NOT_FOUND`: a SDMA0 também não tem microcódigo aceito.

---

## 2. Itens da Fase 0 — o que foi feito

| # | Item | Estado | Onde |
|---|---|---|---|
| 0.1 | Remover o loop `pC[k] = pA[k] + pB[k]` do KMD | ✅ | [wddm_hw.c](wddm_hw.c) `Bc250HwSubmitComputeDispatch` |
| 0.2 | `clBuildProgram` recusar fonte OpenCL C | ✅ | [opencl/bc250_opencl.c](opencl/bc250_opencl.c) |
| 0.3 | `clEnqueueNDRangeKernel` recusar sem executável válido | ✅ | idem |
| 0.4 | ADL parar de devolver 48 °C / 42 W / 1500 MHz fabricados | ✅ | [adl/atiadlxx.c](adl/atiadlxx.c) — 7 sítios |
| 0.5 | Fence real em vez de contador de CPU | ✅ | compute **e** SDMA |
| 0.6 | OpenCL reportar a VRAM realmente mapeada | ✅ | 256 MB, não 16 GB |
| 0.7 | Expor os códigos TEE crus do PSP | ✅ | `Hw_Fw_*_TeeResp`, `Hw_Fw_*_Seen` |
| 0.8 | Corrigir `A_CP_STAT` | ✅ **ampliado** | tabela inteira derivada por SOC15 |
| 0.9 | Renomear `Hw_SmuGfxOff` | ✅ | → `Hw_SmuGetGfxFreqRsp` |
| 0.10 | Inverter as asserções da suíte | ✅ | 11 testes, novo harness |

Extras que a medição obrigou:

- `SchemaVersion` nas estruturas de caps, para que um consumidor detecte um driver antigo em vez de ler zeros como "detectado"
- `HwCuCountSource` / `VramSizeSource`: todo número agora viaja com a sua procedência (DETECTADO ou ASSUMIDO)
- `HwFirmwareReady`: só TRUE se **todos** os microcódigos do CP forem aceitos
- Escritas de `SPI_PG` / `CC_GC_SHADER_ARRAY` no caminho de dispatch removidas
- Contadores `ComputeFailNoFence`, `HwSpiPgRaw`, `HwCcArrayRaw`, `HwGrbmStatusAtInit`, `HwCpStatAtInit`

### O fallback de compute, antes e depois

```c
/* ANTES — wddm_hw.c */
BcRegWrite(Dev, A_COMPUTE_DISPATCH_INITIATOR, COMPUTE_DISPATCH_EN_MASK);
...
for (k = 0; k < count; k++) {
    pC[k] = pA[k] + pB[k];          /* a CPU faz a conta */
}
Dispatch->FenceValue = ++Dev->ComputeFenceSequence;   /* "fence" = contador */
return STATUS_SUCCESS;                                 /* sempre */
```

```c
/* DEPOIS */
fencePtr = (volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->ComputeFenceVramOffset);
*fencePtr = ~fenceExpected;         /* poison; a CPU nunca escreve o valor bom */
...
BcRegWrite(Dev, A_COMPUTE_DISPATCH_INITIATOR, COMPUTE_DISPATCH_EN_MASK);
...
Dispatch->FenceObserved   = *fencePtr;
Dispatch->ExecutionProven = (Dispatch->FenceObserved == fenceExpected) ? 1u : 0u;
Dispatch->GrbmChanged     = (Dispatch->GrbmStatusAfter != grbmBefore) ? 1u : 0u;

if (!Dispatch->ExecutionProven) {
    Dev->ComputeFailNoFence++;
    return STATUS_DEVICE_NOT_READY;   /* falha honesta */
}
```

---

## 3. A nova suíte de testes

11 executáveis + 1 verificação estática. Todo teste declara o critério antes de medir e deriva o veredito das provas da regra BC-250.

| Arquivo | Cobre |
|---|---|
| [tests/test_verdict.h](tests/test_verdict.h) | harness: `BcCriterio` / `BcMedido` / `BcProva` / `BcVeredito`, detector de fill, write-stick |
| [tests/check_no_fallback.py](tests/check_no_fallback.py) | **T8** — falha o build se houver aritmética de CPU em função de dispatch, offset proibido, telemetria literal ou texto triunfalista. Ignora comentários |
| [tests/test_regmap.c](tests/test_regmap.c) | **T1** scratch round-trip, **T3** write-stick, **T4** detector de fill + regressão dos offsets antigos |
| [tests/test_cp_execution.c](tests/test_cp_execution.c) | **T2** — host grava A, PM4 grava B, host lê. Prova irrefutável de execução do CP |
| [tests/test_fw_status.c](tests/test_fw_status.c) | códigos TEE crus por `fw_type`; distingue chave ausente de valor zero |
| [tests/test_sdma.c](tests/test_sdma.c) | **T5** poison + comparação byte a byte + RPTR/fence separados |
| [tests/test_telemetry.c](tests/test_telemetry.c) | **T6** — exige variância entre amostras; constante não é telemetria |
| [tests/test_opencl.c](tests/test_opencl.c) | verifica que a camada falha honestamente; carrega a DLL instalada dinamicamente |
| [tests/run_suite.cmd](tests/run_suite.cmd) | matriz consolidada, veredito de 3 estados, exit code ≠ 0 |

Estados: `PROVADO` / `NÃO VALIDADO EM HARDWARE` / `BLOQUEADO` (pré-requisito ausente — não conta como falha do teste).

---

## 4. Resultado da suíte no hardware

Rodada com o driver **antigo** ainda carregado. Os `BLOQUEADO` são precisamente o mecanismo de version skew funcionando: os testes recusam interpretar campos que o driver em execução não preenche.

```
  test_regmap ............ PROVADO
  test_hw_info ........... BLOQUEADO   (driver antigo, SchemaVersion=0)
  test_fw_status ......... BLOQUEADO   (driver antigo não publica códigos TEE)
  test_telemetry ......... PROVADO
  test_vram_rw ........... PROVADO
  test_sdma .............. NAO_VALIDADO
  test_pm4_ring .......... NAO_VALIDADO
  test_cp_execution ...... NAO_VALIDADO
  test_compute ........... BLOQUEADO
  test_power_profile ..... PROVADO
  test_opencl ............ PROVADO

VEREDITO GERAL: NAO VALIDADO EM HARDWARE
```

Antes da Fase 0 esses mesmos 11 caminhos imprimiam sete variações de "100% OPERACIONAL".

### T2 — a resposta decisiva

```
[CRITERIO]  O CP deve consumir SET_UCONFIG_REG e gravar 0xBBBB2222 no scratch.
[MEDIDO]    WPTR   antes=0x00000000  depois=0x00000005
[MEDIDO]    RPTR   antes=0x00000000  depois=0x00000000
[MEDIDO]    GRBM   antes=0x00003028  durante=0x00003028
[MEDIDO]    SCRATCH antes=0xAAAA1111  depois=0xAAAA1111  (esperado 0xBBBB2222)
[PROVA  OK]   Prova 2: comando aceito por um ring fisico valido
[PROVA  OK]   Prova 3: WPTR avancou apos a submissao
[PROVA FALTA] Prova 4: RPTR alcancou o WPTR
[PROVA FALTA] Prova 6: a GPU escreveu o valor esperado no scratch
[PROVA FALTA] Prova 7: GRBM_STATUS mudou durante a carga
```

**O CP não executa.** Custou 8 dwords para provar o que meses de "sucesso" nunca testaram.

### OpenCL, depois da correção

```
[MEDIDO]    Memoria global     : 256 MB          (era 16 GB fixos)
[MEDIDO]    Compilador disp.   : nao             (era sim, sem compilador existir)
[MEDIDO]    clBuildProgram retornou -11 (CL_BUILD_PROGRAM_FAILURE)
            Build log: BC-250: nao existe compilador OpenCL C -> ISA GFX10 neste driver.
                       NAO VALIDADO EM HARDWARE.
[MEDIDO]    clEnqueueNDRangeKernel retornou -2
[VEREDITO]  PROVADO   (6 de 6 — a camada falha honestamente)
```

---

## 5. Estado do silício, medido

| Item | Valor | Fonte |
|---|---|---|
| `GRBM_STATUS` | `0x00003028` (GUI_ACTIVE=0) | `0x08010` |
| `CP_STAT` | `0x00000000` | `0x08680` |
| `CP_ME_CNTL` | `0x00000000` (não halted) | `0x086D8` |
| `CP_MEC_CNTL` | `0x50000000` (**MEC halted**) | `0x08234` |
| `SPI_PG_WGP_MASK` | `0x0000FFFF`, **gravável** | `0x0935C` |
| `CC_GC_SHADER_ARRAY` | `0xFFF80000` (24 CU stock) | `0x089BC` |
| `PSP C2PMSG_64` | `0x80020000` (TOS pronto + ring KM) | `0x58200` |
| `PSP C2PMSG_35` | `0xFFFFFFFF` (bootloader concluiu) | `0x5818C` |
| Firmware do CP | **nenhum aceito** | `TEE_ERROR_ITEM_NOT_FOUND` |
| VRAM mapeada | 256 MB | BAR0 |
| Bloco GC | alcançável, scratch R/W | `0x30100` |

---

## 6. O que muda no plano

A Fase 3 (firmware) fica mais importante e a Fase 1 ganha um item novo:

1. **Reabrir a hipótese do unlock de WGP** com `0x0935C` / `0x089BC`. Não como prioridade — como item a re-verificar, porque a conclusão anterior foi obtida no endereço errado. Nunca no caminho de submissão; só em bring-up deliberado e com DPM antes.
2. **O `SDMA_OP_COPY` precisa de auditoria de formato.** O motor consome o frame e não executa. Duas hipóteses: encabeçalho do pacote errado para SDMA5, ou ausência de microcódigo. A referência do Linux está em `referencias/BC250-windowsDriverTest/docs/upstream/sdma_v5_0.c`.
3. **Nada muda em Fase 2 (DPM) e Fase 4 (MQD).** Continuam sendo o caminho.

---

## 7. Para validar as mudanças do KMD

O `.sys` novo está compilado, assinado e empacotado em `package/`, mas o driver em execução ainda é o antigo. Instalar exige o ciclo de desinstalação e reinício:

```bash
cd BC250-V3/package && ./install-wddm.bat
```

Depois do reinício, rodar:

```bash
cd BC250-V3/tests && ./run_suite.cmd
```

Esperado após a instalação: `test_hw_info`, `test_fw_status` e `test_compute` saem de `BLOQUEADO` e passam a reportar evidência real. `test_cp_execution` deve continuar `NÃO VALIDADO` — o CP não vai executar até a Fase 3 resolver o firmware.

---

## 7b. Segunda rodada — o que a instalação do KMD revelou (2026-08-22, build 0725)

O driver da Fase 0 foi instalado e a suíte re-executada. Três achados novos, um
deles um bug meu.

### 7b.1 A contagem de CU estava inflada — bug introduzido por mim

A suíte reportou **`Compute Units: 40 [DETECTADO]` / 2560 shaders**. Está errado.

Eu derivei a contagem de `SPI_PG_ENABLE_STATIC_WGP_MASK`, que lido em broadcast
devolve `0x0000FFFF`. Contar bits 0..4 disso dá 5 WGP/SA → 40 CU. Mas `0xFFFF`
não é uma máscara de 5 bits por shader array; o registrador não significa isso
quando lido em broadcast.

A fonte autoritativa é `CC_GC_SHADER_ARRAY_CONFIG`, campo `INACTIVE_WGPS` em
[31:16]:

```
CC = 0xFFF80000  ->  INACTIVE_WGPS = 0xFFF8 = 1111111111111000
                     bits 0,1,2 limpos  ->  3 WGP/SA ativos
                     4 SA  ->  12 WGP  ->  24 CU  ->  1536 SP
```

Bate exatamente com o stock documentado. **Corrigido**: a contagem agora vem de
`CC`, com `SPI_PG` usado apenas como conferência — e a divergência entre as duas
fontes é registrada em `Hw_WgpSourcesDisagree` em vez de resolvida escolhendo a
maior.

Também introduzi as constantes de topologia (`2 SE × 2 SA`), que antes estavam
implícitas num `* 4` sem explicação.

### 7b.2 Eu apaguei a evidência que queria ler

O teste de SDMA passou a reportar `0 de 20` cópias **e** `0 de 20` fences, com
todos os campos de evidência zerados. Investigando com uma ferramenta de
tamanho de struct:

```
sizeof novo=88  sizeof antigo=48
NOVO   : ret=0xC00000A3 (STATUS_DEVICE_NOT_READY)  hdrStatus=0  fenceWritten=0
ANTIGO : ret=0xC0000023 (STATUS_BUFFER_TOO_SMALL)  hdrStatus=0
```

O `0xC00000A3` é o código que **eu** fiz o driver retornar quando a fence não é
escrita. E o `dxgkrnl` **não copia o buffer privado de volta ao modo usuário
quando o escape retorna NTSTATUS de falha**. Ou seja: minha "falha honesta"
apagava `FenceObserved`, `RptrAfter` e `FenceWritten` — e o `0 de 20` era
artefato do transporte, não medição do silício.

**Corrigido**: o escape é o *transporte* de um diagnóstico e retorna
`STATUS_SUCCESS` por ter entregue a medição. O status real da engine vai em
`Header.Status` e o veredito físico no payload (`FenceWritten`,
`ExecutionProven`). Isso não é retornar sucesso sem execução — nenhum campo
afirma que a cópia ocorreu, e quem consome é obrigado a olhar `FenceWritten`.
A mesma correção foi aplicada ao dispatch de compute.

### 7b.3 A suíte aplicou um overclock sozinha

`test_power_profile` é interativo. Dentro de `run_suite.cmd` ele imprimiu um
menu, e a resposta aplicou **1850 MHz @ 900 mV** no meio da bateria — via
`ForceGfxFreq` sem tabelas DPM, exatamente o caminho que a referência marca como
`DANGER`. O clock ficou alterado para os testes seguintes (o `test_opencl`
reportou 1850 MHz).

**Corrigido**: `run_suite.cmd` agora invoca esse utilitário com `--readonly`, um
modo novo que só lê telemetria. Uma bateria automatizada não pode mudar o estado
do silício. O clock foi restaurado para 1500 MHz com `--unforce`.

### 7b.4 O que o SDMA realmente faz

Com o driver novo e a evidência chegando ao modo usuário, o quadro é mais
específico do que "a SDMA não funciona":

- `RPTR` alcança o `WPTR` — o motor consome o frame do anel;
- a **GPU escreve a fence** em `0xF40FD03000` (esperada `0x08`, observada `0x08`)
  — o motor executa o pacote `SDMA_OP_FENCE`;
- em algumas execuções o destino recebe o conteúdo correto, em outras o poison
  `0x5C` fica intacto ou só parte dos bytes chega.

O pacote `SDMA_OP_COPY` está byte a byte igual ao `sdma_v5_0_vm_copy_pte` do
Linux (header, `count-1`, src lo/hi, dst lo/hi). Os caminhos de coerência do
host estão corretos nos dois casos (barreira + HDP flush na escrita, HDP
invalidate na leitura).

O teste agora roda **20 tentativas** e reporta a taxa, em vez de tirar conclusão
de uma amostra. Um escape de diagnóstico `SDMA_FILL` foi adicionado: o FILL faz a
GPU escrever um padrão gerado por ela mesma, sem depender de ler dado que o host
pôs na VRAM. Se o FILL funcionar e o COPY não, o problema está na leitura da
origem; se nenhum dos dois funcionar, o motor só processa a FENCE. **Esse
diagnóstico ainda não rodou** — precisa do KMD reinstalado.

### 7b.5 Correção ao §1.3 deste documento

O §1.3 afirma que "a fence não é escrita". Isso foi medido com o driver antigo,
que não reportava `FenceObserved`. Com o driver da Fase 0, **a fence é escrita
pela GPU**. A retratação sobre os "12 GB/s" continua válida: aquele número era
tamanho ÷ latência de submissão. O que muda é o diagnóstico — o motor está vivo
e executa pacotes; especificamente o `COPY` é que não move dados de forma
confiável.

---

## 8. Declaração exigida pela regra

Nenhuma funcionalidade de renderização, compute ou cópia da BC-250 apresenta o conjunto de provas exigido.

**NÃO VALIDADO EM HARDWARE**
