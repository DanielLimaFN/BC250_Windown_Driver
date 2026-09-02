# ROADMAP WDDM DISPLAY+RENDER — BC-250

> **Documento de handoff.** Escrito para ser implementado por outra IA, sem
> acesso à conversa que o originou. Tudo o que é necessário está aqui ou nos
> arquivos referenciados.
>
> **Criado em:** 2026-08-31 · **Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3 /
> Cyan Skillfish 2) · Windows 11 x64 build 26100

---

## 0. Objetivo

Um driver **WDDM display+render completo** para a BC-250, capaz de **rodar
jogos**. Não é um acelerador de computação; é uma placa de vídeo.

Isso implica, e nenhum destes itens é opcional:

- `Class=Display` no INF, com fonte de vídeo e filho reportados;
- registro por `DxgkInitialize` (display + render), nunca por
  `DxgkInitializeDisplayOnlyDriver`;
- `DxgkDdiPresent` real com flip e primário em VRAM;
- nó de render vivo, com submissão por `DxgkDdiSubmitCommandVirtual`;
- GPUVA dinâmica por processo;
- TDR e preempção provados — num jogo, travar a GPU é rotina;
- UMD Vulkan capaz de compilar SPIR-V, via ACO do Mesa.

**Herança a descartar.** O projeto nasceu mirando MCDM compute-only. Vários
contratos, audits e capacidades ainda codificam aquele alvo e **contradizem** o
alvo atual. Corrigir essas contradições é trabalho legítimo deste roadmap, não
relaxamento de rigor. Ver seção 4.

O documento irmão [`ROADMAP_VULKAN_100.md`](ROADMAP_VULKAN_100.md) contém o
histórico completo da investigação de 2026-08-31. **Leia-o antes de começar.**

---

## 1. Estado atual

```text
Binário instalado         FCA11D7BF729D041   (candidato headless, Código 43)
Árvore de trabalho        já corrigida para o alvo display+render, NÃO empacotada
Classe no INF             Display  {4d36e968-e325-11ce-bfc1-08002be10318}   ✅ correta
Registro no dxgkrnl       DxgkInitialize   ✅ provado em silício
ConfigManagerErrorCode    43
```

```text
====================== STATUS ======================
SDMA (H2D/D2H byte a byte)        PROVADO EM SILICIO
CP/PM4 + fence fisica em VRAM     PROVADO EM SILICIO
MEC2/MQD/anel persistente (P1)    PROVADO EM SILICIO
Multi-submissao 16/16 (P2)        PROVADO EM SILICIO
VidPn: IsSupported/Commit/Source  PROVADO EM SILICIO
PresentDisplayOnly (blit CPU)     PROVADO EM SILICIO
Registro por DxgkInitialize       PROVADO EM SILICIO
Dispositivo em Codigo 0           NAO
Submissao WDDM real               NUNCA ACONTECEU
DxgkDdiPresent real (flip)        NAO EXISTE (stub)
====================================================
Veredito: NAO VALIDADO EM HARDWARE
```

---

## 2. Leis inegociáveis

Além das quatro de [`CLAUDE.md`](CLAUDE.md), que valem integralmente:

- **L5** — Nenhum entry point devolve sucesso sem backend. Falta de suporte é
  `STATUS_*`/`VK_ERROR_*`, nunca sucesso silencioso.
- **L6** — Nenhuma capacidade anunciada sem lastro, **e nenhuma capacidade
  contraditória**. Declarar `ComputeOnly` e enumerar fonte de vídeo ao mesmo
  tempo é erro, mesmo que cada metade pareça defensável isolada.
- **L7** — Fence só é sinalizada por leitura de memória escrita pela GPU. Nunca
  por contador de CPU.
- **L8** — Aceitação é por operador conferido, não por token gerado nem por
  quadro apresentado.
- **L9** — Campo de telemetria em zero deve significar "não aconteceu neste
  boot". Zere o bloco no `DriverEntry`; use sentinela `0xFFFFFFFF` onde `0` for
  valor legítimo. Ver seção 6.

---

## 3. O diagnóstico que levou a este roadmap

Resumo do que foi **medido**, não deduzido.

### 3.1 O driver era display-only, e isso escondia tudo

Até 2026-08-31 o driver registrava por `DxgkInitializeDisplayOnlyDriver`
(KMDOD). `KMDDOD_INITIALIZATION_DATA` **não possui nenhum campo** de `Submit*`,
`Render`, `CreateContext`, `CreateAllocation`, `Patch` ou `BuildPagingBuffer` —
conferido campo a campo em `km/dispmprt.h` do WDK 10.0.26100.

Consequência: as 38 DDIs de `bc250_mcdm_callbacks.c` nunca eram entregues ao
sistema. `DxgkDdiSubmitCommandVirtual` jamais foi chamado, e o rastreador de
fences era inalcançável por construção.

O único caminho para `DxgkInitialize` era um sentinel `DriverObject ==
0xDEADBEEF`, gancho de teste offline que nunca é verdade em produção.

**Corrigido e provado:** o tunable `RenderNode=1` faz o `DriverEntry` chamar
`DxgkInitialize` (`V5RegistrationMode = 1`, `DxgkInitializeStatus = 0`).

### 3.2 Bloqueios resolvidos depois disso

| Bloqueio | Sintoma medido | Correção |
|---|---|---|
| `DXGKQAITYPE_WDDMDEVICECAPS` não implementado | `V5QaiLastStatus = 0xC00000BB` | `case` implementado, devolve `DXGKDDI_WDDMv2_7` |
| Gate de `runtimeCaps` na consulta pré-`StartDevice` | `0xC00000A3` | Removido — o WDK especifica que essa consulta ocorre **antes** do `StartDevice`, quando runtime não existe |
| 7 DDIs implementadas mas **nulas** na tabela de render | rastro parava em 2 marcas | `SetPalette`, `GetScanLine`, `ControlEtwLogging`, `QueryInterface`, `NotifyAcpiEvent`, `NotifySurpriseRemoval`, `GetChildContainerId` wiradas → rastro foi para 10 marcas |
| 7 DDIs obrigatórias de render ausentes | auditoria estática: 59 de 187 campos preenchidos | `QueryCurrentFence` (real), `Escape`, `Acquire/ReleaseSwizzlingRange`, `Patch`, `Render`, `SubmitCommand` |

### 3.3 O bloqueio atual

```text
QAI(WDDMDEVICECAPS) OK → StartDevice OK → QueryInterface ×8 → aborta
Kernel-PnP evento 411: Status do Problema 0xC00000E5 (STATUS_INTERNAL_ERROR)
DXGKQAITYPE_DRIVERCAPS nunca é pedido
```

A contradição identificada, e **ainda não testada em hardware**:

```text
INF:     Class=Display,  fonte de video 1, filho 1
Driver:  MiscCaps.ComputeOnly = 1     <-- herança do alvo MCDM
```

Um adaptador `ComputeOnly` não apresenta nada. Em 2026-08-31 tentou-se resolver
pelo lado errado — headless, com `0/0` fontes — e o Código 43 permaneceu
idêntico. **A árvore de trabalho já foi corrigida para o lado certo:**
`ComputeOnly = 0`, fonte e filho de volta a `1/1`.

**Essa correção nunca foi empacotada nem testada.** É o próximo passo.

---

## 4. Fase G1 — Coerência display+render · **PRÓXIMO PASSO**

### 4.1 Já feito na árvore de trabalho (build limpo, não empacotado)

| Mudança | Arquivo |
|---|---|
| `MiscCaps.ComputeOnly` de `1u` para `0u` | `driver/wdk/bc250_mcdm_callbacks.c` |
| `StartDevice` volta a reportar 1 fonte e 1 filho | `driver/wdk/bc250_active_miniport.c` |
| Contrato passa a exigir `ComputeOnly == 0u` | `driver/tests/mcdm_callbacks_test.c` |
| `audit-bc250mcdm-safety.py` passa a **proibir** `ComputeOnly = 1u` | `tools/audit-bc250mcdm-safety.py` |

A propriedade de segurança que importava continua garantida no audit:
`SupportNonVGA` não pode ser `TRUE`.

### 4.2 O que falta fazer

1. **Empacotar e testar.** Ver seção 6 para a ordem obrigatória.
2. **Revisar `tools/audit-mcdm-ddi-matrix.py`.** Ele tem hoje dois perfis, MCDM e
   WDDM. Com o alvo definido, o perfil MCDM só faz sentido para a imagem
   readonly de produção — decida se ela continua existindo. A lista `PROHIBITED`
   proíbe DDIs de gráficos que este driver **precisa**.
3. **Revisar o INF inteiro** procurando outras heranças de compute-only.

### 4.3 Critério de saída

```text
ConfigManagerErrorCode = 0
V5RegistrationMode     = 1
V5QaiCalls             > 1        <- DRIVERCAPS finalmente pedido
```

`V5QaiCalls` saindo de `1` é o sinal perseguido por cinco boots.

Se o Código 43 persistir, **não corrija por hipótese**. Leia `V5LastDdi`,
`V5PrevDdi`, `V5Prev2Ddi` e o evento Kernel-PnP 411. Se o rastro não explicar, o
próximo instrumento é captura ETW do provedor `Microsoft-Windows-DxgKrnl` — o
evento 411 dá o NTSTATUS mas não o motivo, e `DEVPKEY_Device_ProblemStatus` veio
`0` e não ajuda.

---

## 5. Fases seguintes

### G2 — Retirada de fence (código pronto, nunca exercitado)

Implementado com contrato offline aprovado; nunca executou porque nada chegou a
`DxgkDdiSubmitCommandVirtual`.

| Peça | Arquivo |
|---|---|
| FIFO de 64 entradas correlacionando fence do anel ↔ `SubmissionFenceId` | `bc250_submission_engine.{h,c}` |
| `DxgkCbNotifyInterrupt` com `DXGK_INTERRUPT_DMA_COMPLETED` em DIRQL | `bc250_active_miniport.c` |
| Varredura por KTIMER de 1 ms | idem |
| Contrapressão: FIFO cheio → `STATUS_GRAPHICS_ALLOCATION_BUSY` | idem |
| 12 campos `V2*` de telemetria | `bc250_probe_telemetry.{h,c}` |

**Tripé de aceitação:** `RPTR == WPTR` · fence física lida da VRAM ·
`V2NotifiedTotal == 16` com `V2TrackerOverflow == 0`, sem TDR, em 16 submissões.

Pendências: anel e slot de fence são **hardcoded** (`0xF400000000`,
`0xF400002200`) e precisam ser por contexto; a interrupção real do MEC (anel IH)
nunca foi validada — hoje é varredura por timer.

### G3 — GPUVA dinâmica por processo

Pré-requisito de multiprocesso e de qualquer jogo. Faltam
`DxgkDdiUpdatePageTable`, `UpdatePageDirectory`, `MovePageDirectory`,
`DescribePageTable`. O GCVM já tem PTE `0x10` provado em silício — use-o.

### G4 — `DxgkDdiPresent` real

Hoje é stub honesto devolvendo `STATUS_NOT_SUPPORTED`
(`bc250_display_ddi.c`). Precisa de flip, primário em VRAM e integração com o
VidPn já provado. **Sem isto não há jogo**, por mais que o render funcione.

`DxgkDdiPresentDisplayOnly` existe e funciona (blit por CPU), mas é exclusivo do
caminho display-only e não serve aqui.

### G5 — TDR e preempção provados

`TdrReady = 1` é declarado sem prova física. Num jogo, travar a GPU é rotina, não
exceção. Prove reset de engine após shader travado, com recuperação.

### G6 — RADV com gfx1013 (parcialmente feito)

Árvore em `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm`,
branch **`bc250-wddm-diag`**, fork `lfrb/mesa` wddm2. `meson`/`ninja`
instalados. `build_radv.bat` aponta para VS 2022 Enterprise; a máquina tem VS 18
Community — adapte.

Já feito e **verificado sem instalar em System32**:

| Correção | Onde | Resultado |
|---|---|---|
| `chip_external_rev` de `1` → `0x82` | `radv_wddm2_winsys.c:~312` | device passou a reportar `RADV GFX1013` em vez de `RADV NAVI10` |
| Removido o engolidor de NTSTATUS da submissão | `radv_wddm2_cs.c:~758` | `llama-bench` passou a falhar honestamente em vez de reportar 115.504 t/s |

Falta: WARP enumerado como BC-250 (`deviceID 0x008c`), orçamento de memória fixo
em 16384 MiB, `uma: 0` numa APU unificada, subgroup 64 vs 32, e **WSI Win32**,
que é obrigatório para jogos e nunca foi provado.

Teste sem instalar:

```bash
powershell -ExecutionPolicy Bypass -File tools\test-v2-wddm-submission.ps1 -VulkanDriverFiles tools\radv-build-icd.json
```

### G7 — Gates de aceitação

Para compute: `test-backend-ops` do llama.cpp, operador a operador, GPU contra
CPU. Para gráficos: Vulkan CTS. **Não** aceite "o jogo abriu" como prova.

Binários llama.cpp em
`C:\Users\Daniel\Desktop\bc250-win\bc250-win\tools_ai\llama-cpp`, modelos em
`...\tools_ai\models`. Há uma árvore `Vulkan-Samples` na raiz do repositório.

### G8 — ICD próprio: bancada, não produto

`driver/umd/bc250_vulkan.dll` está **fail-closed**: recusa `vkCreateInstance` com
`VK_ERROR_INCOMPATIBLE_DRIVER` salvo com `BC250_ICD_UNSAFE=1`. Antes derrubava o
loader com `0xC0000005` e executava palavras SPIR-V como se fossem ISA GFX10.

51 de 95 entry points; faltam descritores (0/8), pipeline layout, barreiras,
semáforos, query pool. **Não invista aqui** — o compilador vem do ACO via G6.
Auditoria: `python tools\audit-vulkan-icd-entrypoints.py`.

---

## 6. Armadilhas descobertas em 2026-08-31 — leia antes de gastar um ensaio

Cada uma custou pelo menos um ciclo de bind ou boot.

**O empacotador não compila.** `package-bc250mcdm-lab.ps1` apenas copia o `.sys`.
Pular o build assina o binário do candidato anterior. Ordem obrigatória:

```text
driver\wdk\build-contract.bat
tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission
tools\audit-bc250mcdm-lab-package.ps1
```

Confira `SourceSysSha256` do manifesto contra o hash do build. Há guard-rail no
bind, mas confira mesmo assim.

**O `RenderNode` só é lido de forma confiável no boot.** É lido em `DriverEntry`,
e durante o bind o `pnputil` está reescrevendo a chave do serviço — a leitura sai
instável e o driver cai no default, voltando a display-only. Se
`V5RenderNodeTunablePresent = 0`, a corrida não vale. **Sempre reinicie.**

**Estado de engine não cai com desbind.** Só o boot frio limpa. Vincular sobre
uma fila ativa (`HqdActiveBefore != 0`) faz o motor não subir e tudo sair zerado.
Há guard-rail no bind.

**Telemetria persistente sobrevive ao boot.** Corrigido para os blocos P1/P2
(`Bc250ProbeResetSubmissionEvidence`) e V5, mas **não** para o resto. Campos
`Dmm*` podem ser resíduo. Antes de usar um campo como evidência, verifique se ele
é zerado no `DriverEntry`.

**`0` é ambíguo para NTSTATUS.** `STATUS_SUCCESS` também vale `0`. Use sentinela
`0xFFFFFFFF` para "não gravado". Isso quase levou a ler um Código 43 como
sucesso.

**PowerShell 5.1 mata executáveis nativos.** `$ErrorActionPreference = 'Stop'`
mais `*>&1` transforma a primeira linha de stderr em `NativeCommandError`. O
`llama-bench` morria em 0 s e o delta zerado era lido como resultado. Use
`Start-Process` com redirecionamento em arquivo.

**Nunca conclua a partir de ausência de dado.** Três vezes neste dia uma hipótese
plausível trocou um Código 43 por outro. Todas as vezes em que se instrumentou
antes de corrigir, a correção saiu de primeira e foi de uma linha.
**Instrumente primeiro.**

---

## 7. Instrumentação disponível

Registro: `HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe`

| Grupo | Campos |
|---|---|
| Registro no dxgkrnl | `V5RegistrationMode`, `V5RenderNodeRequested`, `V5RenderNodeTunablePresent` |
| StartDevice | `V5StartDeviceStatus`, `V5StartDeviceSources`, `V5StartDeviceChildren` |
| QueryAdapterInfo | `V5QaiCalls`, `V5QaiLastType`, `V5QaiLastStatus`, `V5QaiLastUnsupportedType`, `V5QaiUnsupportedCount` |
| QueryInterface | `V5QiCalls`, `V5QiLastGuidD1`, `V5QiPrevGuidD1`, `V5QiPrev2GuidD1`, `V5QiLastVersion`, `V5QiLastSize` |
| Rastro de DDI | `V5LastDdi`, `V5PrevDdi`, `V5Prev2Ddi`, `V5DdiMarks` |
| Retirada de fence | `V2NotifiedTotal`, `V2TrackerPushedTotal`, `V2FencesInFlight`, `V2TrackerOverflow`, `V2TrackerOutOfOrder`, `V2PushFailures`, `V2PollFailures`, `V2Backpressure`, `V2RetirementDpcRuns`, `V2NotifyBatches`, `V2LastNotifiedFenceId`, `V2InterruptsSeen` |

Ids do rastro em `driver/wdk/bc250_probe_telemetry.h` (`BC250_V5_DDI_*`).
`Bc250ProbeMarkDdi()` está na entrada de 38 DDIs; o retrato vai ao registro na
primeira linha do `StopDevice`, antes de qualquer teardown.

GUIDs de interface já observados, todos recusados: `0xAF03F190`
(`GUID_DEVINTERFACE_MIRACAST_DISPLAY`), `0xE922004D`
(`GUID_DEVINTERFACE_WDDM3_ON_VB`), `0x6BA41A33` (não consta no WDK). A janela
guarda 3 de 8 — se precisar dos outros cinco, amplie a janela.

---

## 8. Ferramentas

```bash
python tools\audit-vulkan-icd-entrypoints.py
```

```bash
python tools\audit-mcdm-ddi-matrix.py
```

```bash
powershell -ExecutionPolicy Bypass -File tools\test-p2-multi-submission.ps1
```

```bash
powershell -ExecutionPolicy Bypass -File tools\test-v2-wddm-submission.ps1
```

```bash
powershell -ExecutionPolicy Bypass -File tools\arm-bc250mcdm-reboot-watchdog.ps1 -Execute -IAcceptPersistentRebootGuard -TimeoutSeconds 300
```

Bind (console elevado, e **reinicie depois**):

```bash
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode
```

Recuperação: `C:\BC250_RECOVERY.cmd` restaura o BasicDisplay.
Rollback do ambiente Mesa: `tools\clean-mesa-overrides.ps1 -Restore`.

---

## 9. Ordem de execução

```text
G1 (coerencia) ──> G2 (fence) ──> G3 (GPUVA) ──> G4 (Present real) ──> G5 (TDR)
                                      │                                    │
                                      └──> G6 (RADV gfx1013 + WSI) ────────┴──> G7 (gates) ──> jogos

G8 (ICD proprio) roda em paralelo, sem bloquear nada
```

G1 é o único passo executável hoje. Todo o resto depende do dispositivo chegar a
`ConfigManagerErrorCode = 0`.

---

## 10. Advertência final

Nada neste projeto está validado em hardware para aceleração. A P1 e a P2 provam
o anel de submissão **interno**, chamado de dentro do próprio motor
(`bc250_submission_engine.c`) — elas contornam `DxgkDdiSubmitCommandVirtual` e
**não** provam o caminho WDDM.

Enquanto `V2TrackerPushedTotal` for `0`, nenhuma submissão de sistema operacional
chegou ao silício. Qualquer número de throughput medido antes disso é ficção —
foi assim que `llama-bench` reportou 115.504 t/s, cerca de 60 vezes acima do pico
de compute físico do chip, com zero submissões reais.

**Veredito: NÃO VALIDADO EM HARDWARE.**
