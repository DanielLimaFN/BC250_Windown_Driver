# Doc 760 — Corrida 298: WSI.2 recusada na importação da fence, GravityMark sem memória; WSI.2b + HEAP.1, pacote 3453 e critério da Corrida 299

> **Corrida 298:** 14/10/2026 17:37:54, lab **3.452.1.0** (SYS `6DF1EE05`, UMD `B1AAFF12`, RADV `86BF7F51`), [doc 759](759_GFXRST2_RESET_DO_GFX_QUE_VOLTA_E_WSI2_VKCUBE_SEM_ESPERA_DA_CPU_PACOTE_3452_E_CRITERIO_CORRIDA298.md).
> **Evidência:** `docs/evidence/2026-10-14_17-37-54-DESKTOP-HQAG6T4-lab-binding/` (logs `-FINAL` do vkcube 11056, vkcubepp 6440 e GravityMark 13280; `display-log-this-bind-FINAL.txt`).
> **Relato do Daniel:** "deu erro no vkcube `vk_wddm2_monitored_fence.c:90`"; "no GravityMark 1.89 também deu erro"; "os vkcube abriram, porém não sei se estão mais rápidos, parecem iguais".

## 1. O que a 298 mediu (P0)

| Item | Medida |
|---|---|
| Bind, 600 s, rollback | Código 0 até o fim; `RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em 5 amostras, sem reboot |
| TDR do GFX | **nenhum** (as linhas `TDR_*` do log são a reconstrução do anel pela D2 no boot) → GFXRST.2 **não exercitada** (desfecho B0) |
| BugCheck / page fault | nenhum |
| vkcube | `WSI2: RECUSADO (importacao da fence da UMD recusada pela RADV)` em todas as swapchains (3, inclusive após redimensionar); rodou pela WSI.1; 170 Presents S_OK em 11,4 s ≈ **15 quadros/s** |
| vkcubepp | mesma recusa; 3.013 Presents S_OK em 157 s ≈ **19 quadros/s** |
| GravityMark 1.89 (Vulkan, 1280×720) | mesma recusa da WSI.2; 282 Presents S_OK em 34,8 s ≈ 8 quadros/s; então `MakeResident` → **`0xC0000017` (STATUS_NO_MEMORY)** numa alocação de 19,6 MB, depois de 79 alocações somando **~5,3 GB** (uma de 1,15 GB); o app reportou `device lost` e fechou |

## 2. As duas causas

**A. O erro `vk_wddm2_monitored_fence.c:90` é a recusa da WSI.2.** A linha 90 é o `default` de `NTSTATUS_to_VkResult`: o kernel devolveu um NTSTATUS que a RADV não mapeia. No mesmo milissegundo o log da RADV diz `WSI2: RECUSADO (importacao da fence da UMD recusada pela RADV)`. A única chamada desse arquivo naquele ponto é `D3DKMTOpenSyncObjectFromNtHandle2`, que a RADV fazia com `Shared=1, NtSecuritySharing=1, NoGPUAccess=1`. As duas fences da UMD foram criadas e compartilhadas (`[WSI2] fence compartilhada #0/#1` com NT handle). O número do NTSTATUS só foi para o console do app, não para o log (defeito meu de instrumentação). Por isso o vkcube "parece igual": é a mesma WSI.1 da 297 (11–16 quadros/s na 297, 15–19 agora). O erro na tela não derrubou o app.

**B. GravityMark: a RADV prometia memória que o Windows não dá.** O heap GTT saía de `CommitLimit` dos segmentos de abertura: 12 GB do nosso segmento 2 mais o segmento implícito com `UINT64_MAX`, cuja soma estourava (`gtt=12884901887`). Numa máquina com 15,5 GB de RAM o VidMm recusou a residência aos ~5,3 GB. Qual limite exato do VidMm foi atingido (orçamento NON_LOCAL do processo ou commit do sistema) é **H**: a 298 não registrou o orçamento.

## 3. Correções (offline)

**WSI.2b** (`mesa-radv-wddm/src/vulkan/runtime/vk_wddm2_monitored_fence.c`):
- a importação tenta 4 combinações de flags: a original; sem `Shared`, com acesso da GPU; sem `Shared`, `NoGPUAccess`; `Shared` com acesso da GPU. Fica a primeira que abre com mapeamento de CPU;
- cada tentativa grava `[RADV_WDDM2] WSI2B OpenSyncObjectFromNtHandle2 tentativa N (...): status=0x...`;
- todo NTSTATUS desconhecido passa a ir também para o log (`[RADV_WDDM2] monitored_fence: NTSTATUS desconhecido 0x...`).

**HEAP.1** (`mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_winsys.c`):
- segmento de abertura com `CommitLimit = UINT64_MAX` não entra mais na soma;
- o GTT é limitado por `D3DKMTQueryVideoMemoryInfo(NON_LOCAL).Budget`, a mesma regra do `QueryVideoMemoryInfo` dos drivers do Windows; a linha `HEAP1 orcamento NON_LOCAL` grava Budget, CurrentUsage e AvailableForReservation.

HEAP.1 não garante que o GravityMark caiba: com o heap honesto, o app pode escolher menos memória (se respeita o heap) ou receber `VK_ERROR_OUT_OF_DEVICE_MEMORY` mais cedo.

**Pacote lab 3.453.1.0:** SYS `E04FF5AD` (mesma fonte da 298, recompilado pelo DriverVer), UMD `B1AAFF12` (igual), RADV `12D48A75` no build e em `C:\BC250_LIB` (anterior guardada como `vulkan_radeon.dll.inuse-86BF7F51` e `.pre-298fix-86BF7F51`), RADV assinada no pacote `9B4BE2EB`. `audit-bc250mcdm-lab-package.ps1` PASS. RADV `ninja` EXIT 0 (avisos C4005 já existentes no `vk_wddm2_dispatch_table.h`). **NÃO VALIDADO EM HARDWARE.**

## 4. Critério da Corrida 299 (escrito antes)

**Roteiro:** `tools\EXECUTAR_CORRIDA299_WSI2B_HEAP1_600S.cmd`, elevado; antes: fusível, desligar e ligar. vkcube ~30 s, vkcubepp ~30 s, GravityMark com as **mesmas opções** da 298, desktop até o fim.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A1** | `WSI2B ... tentativa N ... status=0x00000000` e `WSI2: ATIVO`; `[WSI2] Present #N ... hr=0x00000000`; cubo girando (operador); ritmo acima de ~19 quadros/s | a WSI.2 liga; mede-se o ganho de tirar as esperas da CPU |
| **A2** | as 4 tentativas com status de falha e `WSI2: RECUSADO` | o NTSTATUS diz o motivo; a fence precisa ser criada de outro jeito (p.ex. pela RADV e importada pela UMD) |
| **A3** | `WSI2: ATIVO`, mas cubo parado, preto ou rasgado | ordem das fences errada; os valores no log dizem qual |
| **H1** | linha `HEAP1` com Budget < 12 GB e GravityMark passa dos ~40 s | o heap honesto resolveu |
| **H2** | `HEAP1` aparece e o GravityMark ainda recebe `0xC0000017` | o app não respeita o heap ou o limite é outro; o Budget/CurrentUsage dizem qual |
| **B** | TDR do GFX → como no doc 759 (B1/B2) | GFXRST.2 |
| **E** | fora do mapa: descrever antes de julgar | — |

## 5. Tabela viva do DCN

Sem mudança (o DCN não foi alterado nem exercitado de forma nova). Prontos: D01–D05, D08, D10, D15, D32–D33. Abertos: D06/D07, D11, D12–D14, D18–D23, D25–D29, D31.
