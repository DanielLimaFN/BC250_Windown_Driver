# Doc 757 — Corrida 296: trava dentro do StartDevice, VCN removida, e ICD.1 + WSI.1 (Vulkan como driver oficial) no pacote 3451 — critério da Corrida 297

> **Data:** 14/10/2026.
> **Corrida 296:** bind às 15:31:15, lab **3.450.1.0** (SYS `0F74A48F`), [doc 756](756_BLOCO1_ALIVIO_JOURNAL_GPUVA_E_VCN1_SONDA_BOOT_VCN2_PS5_PACOTE_3450_E_CRITERIO_CORRIDA296.md), evidência `docs/evidence/2026-10-14_15-31-15-DESKTOP-HQAG6T4-lab-binding/`.
> **Pacote novo:** lab **3.451.1.0** (SYS `B29BFF54`, UMD `4BA5929A`, RADV `0EE338DB`, assinada no pacote `87F5E9DE`).
> **Decisão do Daniel (14/10):** tirar o VCN e seguir com um app 3D real: um vkcube baixado da internet tem de funcionar "sem eu fazer nada", como num driver oficial da AMD.

## 1. Corrida 296 (P0)

| Medida | Valor |
|---|---|
| Log do KMD recuperado | 3.106 bytes; termina em `[DCN1_D2] HUBP0/OTG0 como o GOP deixou` |
| Linhas que deveriam vir logo depois | `[DCN2_TIMING]`, `[DCN_PROBE]`, `[VCN_PROBE]`: nenhuma |
| Rastro de paginação | 19 registros, último em ~4,17 s depois do AddDevice, zero paginação |
| Fim | Kernel-Power 41 com BugcheckCode 0 às 15:40:06: a máquina ficou travada ~8 min até o reset manual |
| Boot seguinte | fusível recusou o StartDevice (`State=2, Boots=2`), rollback online, BasicDisplay Código 0 em 5 amostras |
| WHEA/MCA | nada além das entradas que aparecem em todo boot (15:18 e 15:29 têm as mesmas) |
| Teste de RAM depois da trava | 8.473 MB × 2 passadas, zero erros |
| `RESET_REASON` | `0x40080400` (bits 30/10 fixos, bit 19 de todo boot; doc 625): não diz nada |

**Atribuição (P2, n=1):** a trava foi dentro do StartDevice, antes do desktop. Em 38+ corridas o mesmo caminho até o `DCN1_D2` passou. O único código novo nele é a VCN.1, que roda logo depois do `Bc250DisplayInit` com `VcnProbe=2` (o INF de laboratório pedia o boot completo): lê e escreve no bloco UVD, aponta a VCPU para um buffer e faz polling de até 1 s. As linhas que diriam se a máquina morreu na leitura, na escrita ou no boot da VCPU ficaram no anel de log em RAM e se perderam.

**Correção:** depois da corrida, às 15:54, o `bc250_vcn_probe.c` ganhou um bloqueio cujo comentário diz *"ACHADO DE SILICIO (Corrida 296, doc 757): leitura MMIO … causa TARGET ABORT … RESET_REASON bit 30 … reinício instantâneo"*. Isso não se sustenta: o doc 757 não existia, o bit 30 é fixo em todo boot, não houve reinício instantâneo (foram ~8 min travada) e o log não separa leitura de escrita. O bloqueio estava certo; a justificativa era H apresentada como P0.

### Relato da corrida 296

1. **Houve avanço?** Não. Regressão: a 295 subia o desktop; a 296 travou antes dele.
2. **Sucessos:** só a recuperação: fusível, rollback online, RAM sem erro.
3. **Falhas:** trava dura no StartDevice, sem BugCheck nem dump; nenhuma das três verificações do doc 756 rodou.
4. **Veredito:** falha.
5. **Recuperação e pendências:** BasicDisplay ativo, fusível disparado (rodar `inspect-boot-fuse.ps1 -Reset`). O Bloco 1 do journal (16/64 slots) continua NÃO VALIDADO EM HARDWARE e segue no pacote novo. O VCN sai.

### Checklist da 296

| Item | Estado |
|---|---|
| StartDevice até o desktop | ❌ trava depois do `DCN1_D2` |
| Bloco 1: journal 16/64 slots e cura do `JournalTaintAll` | ⏸️ não rodou |
| Bloco 1: zero `NO_SPACE` | ⏸️ não medido |
| VCN.1: estado inicial do UVD | ❌ linha perdida |
| VCN.1: boot da VCPU | ❌ não medido; principal suspeita |
| Fusível anti-boot-loop | ✅ |
| Rollback online, Código 0 | ✅ |
| RAM depois da trava | ✅ |

## 2. VCN removido

- `bc250_vcn_probe.c/.h`, `vcn_probe_contract_test.c` e `firmware/ps5_vcn.bin` foram para `DocsObsoletos/candidatos-encerrados/vcn1_corrida296/` (nada apagado).
- Saíram a chamada no StartDevice e o teardown (`bc250_active_miniport.c`), o build e a suíte (`build-contract.bat`), a chave `VcnProbe` e o arquivo do INF, e o parâmetro e o hash do empacotador.
- O EXECUTAR da 297 apaga o resíduo `System32\drivers\ps5_vcn.bin` que a 296 copiou.
- Motivo além da trava: decodificação de vídeo não está no caminho de jogos (Lei do escopo), e o firmware veio dos patches do Linux do PS5, sem proveniência AMD.

## 3. O que um vkcube baixado precisava e não tinha (achados offline, P0 por leitura da máquina e do código)

1. **Não há runtime do Vulkan no Windows:** `System32\vulkan-1.dll` não existe. Nenhum app Vulkan abre. Os drivers da AMD instalam esse runtime (Khronos).
2. **O ICD registrado está quebrado:** `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` aponta para `driver\umd\output\bc250_icd.json`, que aponta para `bc250_vulkan.dll`, que não existe (de 31/08).
3. **A RADV apresentava pelo caminho de setembro:** `DeviceWaitIdle` a cada quadro, cópia por escape direto para o scanout do GOP (por fora do DWM), janela fixa em 2560×1080. Com o flip pelo DCN (DCN.1) a tela mostra a primária do DWM, então o cubo não apareceria.
4. A UMD liga `RADV_DEBUG=nodcc` na RADV dela; um app Vulkan carregaria a RADV com DCC.

## 4. O que o pacote 3.451.1.0 muda

### ICD.1 — o Vulkan é achado pelo adaptador, como num driver da AMD

- INF: `HKR,,VulkanDriverName,0x00010000,%13%\bc250_vk_icd.json` na chave do adaptador; `vulkan_radeon.dll`, `z-1.dll` e `bc250_vk_icd.json` vão para o DriverStore (DIRID 13). O JSON aponta para `.\vulkan_radeon.dll` (relativo a ele).
- O loader da Khronos lê esse valor por `D3DKMTQueryAdapterInfo` e chama `vk_icdEnumerateAdapterPhysicalDevices` com o LUID da placa (já existe na RADV, `vk_instance.c`).
- Os dois empacotadores (laboratório e produção) copiam e assinam os três arquivos; o manifesto e a auditoria do pacote conferem os hashes.
- `tools/instalar-vulkan-runtime.ps1`: instala o runtime da Khronos em silêncio (`VulkanRT.exe /S` do SDK que já está na máquina) se faltar, e remove a entrada quebrada do ICD (backup em `C:\BC250_VK_ICD_ANTIGO.reg`). Só 64 bits: não há RADV de 32 bits.

### WSI.1 — a janela de um app Vulkan passa pela UMD D3D11 e pelo DWM

Mesmo desenho da WSI DXGI da Mesa (que usa D3D12), com D3D11 porque a nossa UMD é D3D11:

1. No `vkCreateSwapchainKHR`, a WSI Win32 da RADV cria um device D3D11 na BC-250 (adaptador pelo LUID) e uma swapchain DXGI na janela, no modelo blt (`DXGI_SWAP_EFFECT_DISCARD`, 1 buffer, B8G8R8A8), o mesmo do teste 3D (T3) que já passa pelo DWM. A swapchain é da superfície; o VkSwapchain recriado no redimensionamento usa `ResizeBuffers`.
2. Para cada imagem do VkSwapchain cria uma textura D3D11 compartilhada (render target). A UMD já desenha nessas texturas pela própria alocação (ZC.1). Uma interface privada nova da UMD (`Bc250UmdWsiBeginCapture/EndCapture/MarkWritten`, `driver/umd/bc250_umd_wsi.h`, espelho na Mesa) devolve a GPUVA, o pitch e o tamanho da textura.
3. A imagem que o app desenha é uma VkImage comum. O blit da WSI (na fila do app) copia para uma VkImage LINEAR sobre a textura, importada pela GPUVA (o mesmo `IMPORT_GPU_VA` da Frente B). O pitch da RADV tem de casar com o da textura, senão falha fechado com motivo no log.
4. No `vkQueuePresentKHR`: espera a fence da imagem, marca a textura como escrita na UMD, `CopyResource` para o back buffer, `Flush` (a UMD espera a GPU) e `Present`. Devolve `VK_SUBOPTIMAL_KHR` quando a janela muda de tamanho.
5. Nenhuma cópia passa pela CPU. Não existe fallback: qualquer passo que falhe devolve erro Vulkan com motivo (`WSI1: …` no log da RADV).

Também: modos de apresentação FIFO e IMMEDIATE; de 2 a 8 imagens; tamanho real da janela. O caminho antigo (`radv_bc250_present_scanout`) saiu da WSI; a função continua exportada, mas nada a chama.

### RADV sem DCC na BC-250

`radv_is_dcc_disabled` devolve verdadeiro para `CHIP_GFX1013`. A UMD já configurava a RADV dela assim (DX.20), e as duas RADV do processo passam a criar imagens com o mesmo layout.

## 5. Arquivos e hashes

| Arquivo | SHA-256 |
|---|---|
| `bc250mcdm.sys` (lab, assinado) | `B29BFF54BCBB505E2D83874E8665C5E6BE64A64BF2014597900CA860EBBE5EC1` |
| `bc250umd.dll` (assinada) | `4BA5929A83F5CB0A28373032996DD3C17ABBCD5ACD72A20336781DBD611F16FE` (fonte `AABAC3BB`) |
| `vulkan_radeon.dll` (build e `C:\BC250_LIB`) | `0EE338DB082722024FA3BCAF3547414076B54B72BFA55A5BE24059BFF8F3F30E` |
| `vulkan_radeon.dll` (assinada, DriverStore) | `87F5E9DECCB7AE754E3A2995A4A57C9C538FBBBFFD3454EA88EDDD19F9574C43` |
| `z-1.dll` (assinada) | `1AFBD67C6806FBB2E754222146962FA13F78B1C0A1BBDFAFBEC73CD1CD4AEB34` |
| `bc250_vk_icd.json` | `9D01FB48C88DD820A79432B3E974C27A7282FF05E990BE38B75E574231D8E214` |
| `bc250mcdm.inf` | `D9F4FDEABFD25E78AAB19A16A30F43602C5BA418F00E52A1E5BAC3E8DCB4C397` |
| `bc250mcdm.cat` | `86FE5B5F492C2084B2B453608B028E32812B63918443182E46F8703D589ACFF6` |

A RADV anterior (`B4AE2E14`) está salva em `C:\BC250_LIB\vulkan_radeon.dll.pre-wsi1-B4AE2E14`. As mudanças da RADV estão na árvore `mesa-radv-wddm` (fora deste repositório, sem commit): `wsi_common_win32.cpp`, `wsi_common.h`, `bc250_umd_wsi.h` (novo), `radv_wddm2_wsi.c`, `radv_physical_device.c`. Todos os `.obj` de `src/vulkan` e `src/amd/vulkan` foram apagados antes do build (198 passos), por causa da recompilação parcial do ninja.

**Offline:** `build-contract.bat` EXIT 0 (sem a VCN), `build-umd.bat` EXIT 0, RADV compilada, `build-bc250mcdm-lab-bringup.ps1` OK, pacote de laboratório com auditoria PASS, `audit-bc250mcdm-safety.py`, `audit-mcdm-ddi-matrix.py` e `audit-ddi-noop-callbacks.py` PASS. Nada disso roda a WSI.1 nem o loader com a placa: **NÃO VALIDADO EM HARDWARE.**

## 6. Critério da Corrida 297 (escrito antes)

**Roteiro:** `tools\EXECUTAR_CORRIDA297_VKCUBE_REAL_600S.cmd`, elevado, depois de desligar e ligar. Antes: `inspect-boot-fuse.ps1 -Reset`. Watchdog de 600 s. A janela minimizada "BC-250 VULKAN" espera o bind, roda `vulkaninfo --summary` sem variáveis `VK_*` e escreve `VULKAN_VERIFICADO`. Depois o Daniel abre o vkcube baixado, deixa girar, arrasta, redimensiona, minimiza e fecha; depois outros apps Vulkan.

**Pré-condição (sem ela a corrida não mede a WSI.1):** desktop sobe como na 295 (a VCN fora devolve o StartDevice ao normal) e `vulkan-apos-bind.log` tem `VulkanDriverName do adaptador = …bc250_vk_icd.json` com o manifesto existente.

| Desfecho | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| **A** | `VULKAN_VERIFICADO` com a BC-250 no `vulkaninfo`; no log da RADV do vkcube, `WSI1: device D3D11 da BC-250` e `WSI1: imagem … pronta` (uma por imagem); no log da UMD do vkcube, `[WSI1] captura … alias=1`; o operador vê o cubo girando e acompanhando o redimensionamento; zero TDR/fault | WSI.1 e ICD.1 funcionam; próximo: velocidade (tirar a espera da CPU por quadro) e jogos reais |
| **B** | Vulkan listado, mas o vkcube não abre ou fecha com `WSI1: … RECUSADA`, `sem alias de GPU na UMD`, `layout linear diverge` ou `importacao da GPUVA … recusada` | O problema é a ponte com a UMD (captura, pitch ou importação); a linha nomeia o passo |
| **C** | `VULKAN_SEM_DISPOSITIVO`, ou `VulkanDriverName … AUSENTE`, ou o vkcube diz que não há GPU Vulkan | O registro do ICD (INF/DriverStore/loader) não fechou; nada da WSI.1 foi exercitado |
| **D** | Linhas `WSI1` de sucesso, mas janela preta ou parada, `Present do D3D11 falhou/perdeu o device`, TDR ou trava | O caminho da GPU (blit, cópia ou apresentação) está errado; a WSI.1 conecta, mas não entrega pixel |
| **E** | Fora do mapa | Descrever o observado antes de julgar |

Desfecho parcial não conta: se o cubo aparece mas não há `[WSI1] captura`, não é A.

**Riscos declarados:**
- A CPU espera a GPU duas vezes por quadro (fence do blit e o `Flush` da UMD). O FPS vai ficar abaixo do que a placa faz; aqui o objetivo é correção, não velocidade.
- A swapchain DXGI é do modelo blt (o que o T3 usa e já passa).
- As queries de evento da UMD são sempre "prontas" (dívida antiga), por isso a sincronização não as usa.
- Só apps de 64 bits.

## 7. Tabela viva do DCN

Sem mudança: a 296 não passou da leitura do D2. Estado igual ao do doc 755 (prontos: D01–D05, D08, D10, D15, D32–D33; abertos: D06/D07, D11, D12–D14, D18–D23, D25–D29, D31).
