# Suite de testes — driver AMD BC-250 (BC250-V2)

Testes de integração que rodam contra o **driver instalado em silício real**.
Não há testes unitários aqui: o driver é um miniport de kernel e praticamente
tudo que importa só é observável através do dxgkrnl, do registro e das APIs de
display do Windows.

## Executando

```bash
powershell -ExecutionPolicy Bypass -File run-all.ps1
```

```bash
powershell -ExecutionPolicy Bypass -File run-all.ps1 -Phases 1,2 -Quick
```

```bash
powershell -ExecutionPolicy Bypass -File run-all.ps1 -Json resultado.json
```

Cada teste também roda sozinho:

```bash
powershell -ExecutionPolicy Bypass -File Test-Fase1-LinhaDeBase.ps1
```

Não precisa de elevação. O único item que melhora com privilégio de
administrador é a medição de CPU do `dwm.exe` na Fase 2 — sem elevação ela é
reportada como não mensurável em vez de mostrar zero, que seria enganoso.

## Numeração das fases

A antiga Fase 3 (múltiplas resoluções) foi movida para o fim depois de não
funcionar em silício real. As demais subiram:

| Fase atual | Conteúdo | Era |
|---|---|---|
| 1 | Linha de base: KMDOD, GOP, VSync, Present | 1 |
| 2 | Performance 2D: blit vetorizado, dirty rects | 2 |
| 3 | Motor de cópia por hardware SDMA0 | 4 |
| 4 | Compute / GPGPU: anéis MEC, caps, dispatch | 5 |
| 5 | Múltiplas resoluções — **bloqueado**, ver `docs/FASE3_DIAGNOSTICO.md` | 3 |

Comentários no código-fonte do driver podem ainda usar a numeração antiga.

## Estados de verificação

| Estado | Significado | Derruba a suite? |
|---|---|---|
| `PASS` | passou | — |
| `FAIL` | falhou | **sim** |
| `XFAIL` | falha **esperada e documentada** (bloqueio conhecido) | não |
| `XPASS` | um `XFAIL` passou | **sim**, de propósito |
| `SKIP` | pré-requisito ausente | não |
| `INFO` | medição ou observação, sem julgamento | não |
| `BENCH` | número de benchmark | não |

`XPASS` derruba a suite deliberadamente. Se um bloqueio conhecido parar de
falhar, alguém precisa olhar e atualizar a expectativa em vez de deixar o teste
mentindo em silêncio.

Código de saída: `0` sem `FAIL` nem `XPASS`; `1` caso contrário; `2` para
argumento inválido.

## Pré-requisitos por fase

As Fases 3 e 4 dependem de bring-up de hardware, que a instalação padrão
**desliga**. Sem isso elas reportam `SKIP` com a instrução exata, em vez de
falhar:

```bash
powershell -ExecutionPolicy Bypass -File ..\package\install-wddm.ps1 -EnableHardwareBringup -EnableDangerousEscapes
```

- `SkipHwBringup=0` — necessário para SDMA0 e para os anéis de compute.
- `EnableDangerousEscapes=1` — necessário para todo escape além de
  `QUERY_INFO`, incluindo o benchmark do SDMA0 e `QUERY_COMPUTE_CAPS`.

## O que cada fase mede

### Fase 1 — Linha de base
PnP em `CM_PROB_NONE`, `DodInitStatus`/`Start_Status` zerados, herança do
framebuffer GOP, os três mapeamentos (BAR0, scanout, BAR5), coerência de
`pitch >= largura*4`, e o adaptador GDI sendo de fato a BC-250.

VSync é medido por `D3DKMTWaitForVerticalBlankEvent`, **não** pelo contador do
registro: `VsyncCount` só é gravado dentro de `IsSupportedVidPn`, então o valor
no registro fica estagnado e mediria nada. A API mede o motor de ponta a ponta.

Benchmarks: taxa de VSync (Hz), intervalo médio e jitter, apresentações/s.

### Fase 2 — Performance 2D
Compara frame cheio contra retângulo sujo de 1/16 da área, com uma cópia de
memória do host no mesmo tamanho de frame como cota superior do lado da CPU.

Benchmarks: apresentações/s nos dois regimes, vazão estimada para o BAR0,
fração da cópia do host, ganho do dirty rect, CPU do DWM.

O teste também inspeciona `wddm_driver.c` e reporta a largura de vetor
realmente implementada. **O blit usa SSE2 de 128 bits** (`_mm_stream_si128`),
não AVX2 de 256 bits (`_mm256_stream_si256`) como o item 2.1 do roadmap
descreve. A parte não-temporal do requisito está cumprida; a largura de
registrador não. O teste reporta isso como `INFO` em vez de fingir que a
otimização de 256 bits existe.

### Fase 3 — SDMA0
Status do anel, `RB_ENABLE` em `SDMA0_RB_CNTL`, autenticação do microcódigo
pelo PSP, e `rptr == wptr` — que é a prova de que o motor **consumiu** o que
foi submetido, não apenas aceitou.

Benchmark de banda em blocos de 64 KB, 256 KB, 1 MB e 4 MB, comparado com a
meta de 10 GB/s do item 4.2 do roadmap (registrada como `INFO`, não como
contrato do teste).

### Fase 4 — Compute / GPGPU
Cadeia de microcódigo (`RLC`, `CE`, `PFP`, `ME`, `MEC`, `MEC2`), `GRBM_STATUS`
com `GUI_ACTIVE` baixo em repouso, detecção de MMIO morto (`0xFFFFFFFF` nos
dois registradores), e os caps de compute via escape `QUERY_COMPUTE_CAPS`
(0xBD06): 24 CUs, 12 WGPs, 1536 stream processors, GFX10.1, wave32.

Verifica **coerência interna** dos caps: `CUs × SIMD × wave` tem que dar o
total de shaders. Um cap que se contradiz é pior que um cap ausente.

`COMPUTE_DISPATCH` (0xBD07) é deliberadamente **não** exercitado: exige um
shader válido residente em VRAM e um endereço GPU correto, e disparar isso às
cegas em silício real trava a GPU. Validação de dispatch pertence a um teste
dedicado, com shader conhecido.

FP32 de pico é rotulado como **teórico** (`lanes × 2 × clock`), não medido.

Também compara os `#define BC250_ESCAPE*` de `test_phase5_compute.c` contra
`wddm.h`. **Esse check já apontou um defeito real, hoje corrigido.** O que estava
errado:

| Constante | No teste em C | No driver |
|---|---|---|
| `BC250_ESCAPE_CMD_SUBMIT_PM4` | `0xBD01` | `0x01` |
| `BC250_ESCAPE_CMD_READ_REG` | `0xBD02` | `0x02` |
| `BC250_ESCAPE_CMD_WRITE_REG` | `0xBD03` | `0x03` |
| `BC250_ESCAPE_CMD_QUERY_INFO` | `0xBD04` | `0x04` |

O último era o perigoso: `0xBD04` no driver é `BC250_ESCAPE_SDMA_COPY`, então o
passo "[1] Consultando Telemetria" do `test_phase5_compute.exe` caía no caminho
de cópia DMA e submetia um DMA com endereços de GPU arbitrários.

A Fase 4 **não executa** `test_phase5_compute.exe` enquanto os IDs divergirem.
Detectar o defeito é seguro; disparar não é.

### Fase 5 — Múltiplas resoluções (bloqueada)
Separa deliberadamente os dois lados:

- **Lado do driver** — assertivas normais, e todas passam: 12 modos de origem,
  mode set de monitor coerente, conjunto de destino restrito ao timing nativo,
  EDID sintético aceito pelo Windows (`MONITOR\BCX13FE`, 12 modos no WMI,
  timing reconstruindo 60,0000 Hz exatos), `CommitVidPn` aceitando.

- **Lado do SO** — assertivas marcadas `-Expected FAIL`, ou seja `XFAIL`: a
  lista de modos do GDI fica vazia e todo `ChangeDisplaySettings` responde
  `BADMODE`, **inclusive na resolução nativa em uso**.

`CDS_TEST` apenas valida o modo; nunca aplica, nunca muda a tela.

## Notas de implementação

**Structs de display são lidos em buffers crus com offsets explícitos.**
Marshaling declarativo de `DEVMODE`/`DISPLAY_DEVICE` deu tamanho errado em
teste (o `CharSet` default do struct é ANSI, não Unicode) e o sintoma foi
enumeração retornando zero modos — um falso negativo convincente. Buffer cru
elimina essa classe de erro.

**`Present_Count` é quantizado em 256.** O driver só grava a chave quando
`(count & 0xFF) == 0`. Com o Present limitado pelo VSync a ~60 Hz, um passo
leva ~4,3 s de carga contínua. Por isso `Measure-Bc250PresentRate` insiste em
blocos de carga até o contador andar, com teto de tempo — medir numa janela de
1,5 s dava delta 0 e produzia falha espúria. As taxas derivadas desse contador
são **limites inferiores** e estão rotuladas como tal.

**`-File` do PowerShell não passa arrays.** `-Phases 1,2,3` chega como o token
`"123"`. Por isso `run-all.ps1` recebe `-Phases` como string e faz o parse.

## Arquivos

```
run-all.ps1                    orquestrador, sumário consolidado, saída JSON
lib/Bc250TestLib.ps1           P/Invoke, telemetria, escapes, asserts, carga
Test-Fase1-LinhaDeBase.ps1
Test-Fase2-Blit2D.ps1
Test-Fase3-Sdma0.ps1
Test-Fase4-Compute.ps1
Test-Fase5-Resolucao.ps1
```

---

## Fase 4 (parte 2) — ICD Vulkan

`Test-Fase4-Vulkan.ps1` **não** chama a DLL do ICD diretamente. Carrega o
`vulkan-1.dll` do sistema e percorre a API como qualquer aplicativo faria:
`vkCreateInstance` → `vkEnumeratePhysicalDevices` → `vkGetPhysicalDeviceProperties`
→ `MemoryProperties` → `QueueFamilyProperties` → `vkCreateDevice` →
`vkGetDeviceQueue` → destroy. Se passa por aqui, passa para o `vulkaninfo`, para
o `llama.cpp` e para qualquer outro programa.

Também valida integridade de empacotamento, porque as duas falhas que
derrubaram o ICD antes eram exatamente aí:

- **DLL precisa ter header `MZ`.** Um `echo ... ^> ...` mal escapado no
  `compile_umd.bat` gerou um `amdbc250vulkan.dll` de 27 bytes contendo o texto
  `[cc] bc250_vulkan_icd.c -`.
- **Manifesto precisa ser JSON válido, sem BOM, e `library_path` não pode usar
  a forma `./`.** `"./amdbc250vulkan.dll"` é recusado pelo `LoadLibrary` com
  erro 87; `".\amdbc250vulkan.dll"` exige a barra escapada como `\` no JSON, e
  escapar errado invalida o arquivo e o loader descarta o driver inteiro. Por
  isso `umd\install-vulkan-icd.ps1` gera o manifesto com `ConvertTo-Json` e
  caminho absoluto.

Registro do ICD:

```bash
powershell -ExecutionPolicy Bypass -File umd\install-vulkan-icd.ps1
```

A execução de shader do usuário é `XFAIL` deliberado: não existe compilador
SPIR-V → ISA GFX10 no projeto, e `vkCreateComputePipelines` retorna
`VK_ERROR_INVALID_SHADER_NV` em vez de substituir um kernel que não computa
nada. Se esse XFAIL virar XPASS, alguém implementou o compilador.

## Aviso: o benchmark de SDMA0 travou a máquina

Em 20/08/2026 15:51, com `SkipHwBringup=0` e `EnableDangerousEscapes=1`, o
benchmark de banda do SDMA0 **travou o sistema** — `Kernel-Power 41`, sem
bugcheck e sem dump, exigindo desligamento na força. Por isso ele agora exige
opt-in explícito:

```bash
powershell -ExecutionPolicy Bypass -File Test-Fase3-Sdma0.ps1 -RunSdmaBenchmark
```

O `run-all.ps1` **não** o executa. A causa provável está medida na própria
suite: ver a seção seguinte.

## Estado real do SDMA0 (medido, contraria o roadmap)

O roadmap marca a Fase 3 (SDMA0) como CONCLUÍDA. A telemetria diz outra coisa:

| Medida | Valor | Leitura |
|---|---|---|
| `Hw_Sdma_Status` | 0 | anel programado |
| `Hw_Sdma_RbCntl` | `0x80841015` | `RB_ENABLE` ligado |
| `SdmaInitialized` | 1 | driver considera pronto |
| `Hw_Fw_Sdma0` | `0xC0000483` | **`STATUS_DEVICE_HARDWARE_ERROR`** |
| `SdmaWptr` / `SdmaRptr` | 55 / 0 | **o motor não consumiu nada** |
| `SdmaCopyCount` | 0 | nenhuma cópia concluída |

`0xC0000483` vem de `wddm_hw.c:355` e `:408`: o fence do PSP completa, mas o
`response buffer` volta não-zero — ou seja **o PSP rejeita as imagens de
microcódigo**. O mesmo status aparece para CE, ME, MEC, PFP, RLC e SDMA0; só
MEC2 volta 0.

O encadeamento explica o travamento: o driver programa os registradores do anel
e marca `SdmaInitialized=1` mesmo com o microcódigo rejeitado; submeter DMA a um
motor sem microcódigo deixa o `rptr` parado, e a espera pela cerca trava a
máquina.
