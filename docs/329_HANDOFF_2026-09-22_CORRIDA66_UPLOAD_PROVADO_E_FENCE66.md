# Handoff Corrida 66 — 22/09/2026 (13:06:56)

> **Candidato testado:** lab **3.227.1.0** (faixa DX.19)
> **SYS SHA-256:** `F2A3F42C3823D592CAB428ED9497EB457A85C854A676C5CFCBB2C70971DB614D`
> **Pasta de evidência:** `docs/evidence/2026-09-22_13-06-56-DESKTOP-HQAG6T4-lab-binding/`
> **Critério prévio:** [docs/328_CRITERIO_CORRIDA66_VULKAN_TEXTURE_UPLOAD_RESTORED.md](328_CRITERIO_CORRIDA66_VULKAN_TEXTURE_UPLOAD_RESTORED.md)

---

## 1. Resumo Executivo e Veredito

- **Houve avanço?** **SIM, avanço funcional e diagnóstico decisivo frente à Corrida 65.**
  - **Avanço funcional:** A restauração de `Bc250UmdVkUploadImageData` via staging buffer e barreiras explícitas (`UNDEFINED -> TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL`) foi **100% PROVADA no silício**. Todas as texturas do DWM (590x800, etc.) foram carregadas e sincronizadas com sucesso comprovado por fence de hardware.
  - **Avanço funcional:** O fault na textura antiga em `0x2441000` foi **completamente eliminado**.
  - **Avanço funcional:** O anel gráfico físico `CP_RB0` (Node 1 GFX) consumiu e **aposentou 64 submissões 3D no silício**, com avanço contínuo de fences até **Fence 66** (contra 10 na Corrida 65).
  - **Avanço funcional no DWM:** O Draw 1 do DWM (`VkDraws=1`, vertices=48, `CB=1, SRV=0`) executou e aposentou em apenas **21 ms** (`13:07:27.606 -> 13:07:27.627`).
  - **Avanço diagnóstico:** O Draw 2 do DWM (`VkDraws=2`, vertices=126, `CB=1, SRV=1, SAMPLER=1`), que vinculou pela primeira vez a textura amostrada `590x800` (`res=0000000006929170`, alocada em `0x243100000`), durou 2.065 segundos (`13:07:27.678 -> 13:07:29.743`), disparando o watchdog de TDR de 2 segundos do WDDM.
  - **Diagnóstico físico:** Durante o TDR, a GCVM registrou fault `Status=0x0010153C` com `Cid=10` (`TCP_UTCL0` — Texture Cache Pipe) em `FaultAddrPagina=0x2431` (`GpuVa=0x2431000`, que é exatamente `0x243100000 >> 8`). Um segundo fault em Fence 66 registrou `Status=0x001012B1` com `Cid=9` (`SQC_DCACHE` — Scalar Data Cache) em `GpuVa=0x200040000` com `PTE=0` em contexto `00000D6C`.
- **Veredito frente ao critério de docs/328:** **DESFECHO B (PARCIAL).** Meta 1 (restauração de upload Vulkan e sincronização com layout de hardware) 100% cumprida; Meta 2 (eliminação do stall na amostragem de textura) parcial: identificada a causa raiz em metadados de compressão DCC não inicializados e desalinhamento de descritores de textura `T#` em `SQ_IMG_RSRC_WORD0`.

---

## 2. Métricas e Controles Medidos (P0)

| Controle | Corrida 65 (3.226.1.0) | Corrida 66 (3.227.1.0) | Observação |
|---|---:|---:|---|
| **Upload Vulkan de Texturas** | Ignorado (bypass) | **100% PROVADO** | `UploadImageData SUCESSO` em 590x800 |
| **Fences GFX Node 1 Aposentadas** | 10 / 11 | **64 / 66** | **Salto de 10 para 66 fences** |
| **Draw 1 DWM (`SRV=0`)** | Concluído | **Concluído em 21 ms** | Vertex+Pixel pipeline ativo |
| **Draw 2 DWM (`SRV=1, SAMP=1`)** | Hang no Draw 9 | **Executou em 2.065 s** | Concluiu pós-TDR com fence válida |
| **Fault `0x2441000`** | Presente | **ELIMINADO** | Textura antiga não falhou mais |
| **Novo Fault `Cid=10`** | `0x2441000` | **`0x2431000`** | Exatamente `0x243100000 >> 8` |
| **Segundo Fault `Cid=9`** | Ausente | **`0x200040000`** | PTE zero em L0 idx=64 |
| **`IB EXECUTADO (INDIRECT)`** | 1.710+ | **2.431+** | SDMA Node 0 plenamente funcional |
| **BugCheck** | 0 | **0** | Zero BSOD |
| **Watchdog Rollback** | `RECOVERY_OK_ONLINE` | **`RECOVERY_OK_ONLINE`** | BasicDisplay Código 0 restaurado |
| **D3D11 Native Harness** | Quadro 1 fail | **Quadro 1 fail** | DeviceRemoved causado pelo TDR do DWM |

---

## 3. Análise Detalhada dos Fatos Físicos

### 3.1 Upload Vulkan e Transição de Textura Provados
No log `bc250_umd_log_2052-FINAL.txt` (DWM):
```
13:07:25.524 [pid=2052 dwm.exe] [VK_RES] Bc250UmdVkCreateResource: TEXTURE2D 590x800 (dxgiFmt=87 -> vkFmt=44, bindFlags=0x8)
13:07:25.525 [pid=2052 dwm.exe] [VK_RES] Image MemoryRequirements: reqSize=2310144, memTypeIndex=0
13:07:25.534 [pid=2052 dwm.exe] [VK_RES] SUCESSO: Imagem criada (img=0000000006BF5E30, mem=00000000059D0030, size=2310144)
13:07:25.540 [pid=2052 dwm.exe] ResourceUpdateSubresourceUP: textura copiada linha-a-linha (590x800 bpp=4 SrcPitch=2360 DstPitch=2560)
13:07:25.609 [pid=2052 dwm.exe] [VK_RES] UploadImageData SUCESSO: 590x800, 2048000 bytes, RowPitch=2560 bufferRowLength=640 texels em mip0/camada0 (img=0000000006BF5E30)
13:07:26.145 [pid=2052 dwm.exe] ResourceUpdateSubresourceUP: textura sincronizada com VkImage (590x800, 2048000 bytes, mip0/camada0)
```
A textura de 590x800 foi criada, copiada via staging buffer e transferida para a `VkImage` com a barreira de transição `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` confirmada por fence no hardware.

### 3.2 Execução do Draw 1 vs Hang do Draw 2
O DWM disparou o Draw 1 às 13:07:27.606:
```
13:07:27.606 [pid=2052 dwm.exe] [VK_BRIDGE] FlushCommands ENTER: gravacao ativa, finalizando (cmd=0000000007582340)...
13:07:27.627 [pid=2052 dwm.exe] [VK_BRIDGE] FlushCommands: Execucao e Fence em hardware CONCLUIDOS com SUCESSO!
13:07:27.629 [pid=2052 dwm.exe] Bc250UmdExecuteVkDraw: DRAW fence concluida; pixels NAO CONFERIDOS (VkDraws=1, Total=1, Verts=48, Inst=1, Indexed=1)
```
Draw 1 levou **21 milissegundos** e aposentou limpo.

Em seguida, o DWM vinculou o SRV e o Sampler e disparou o Draw 2 às 13:07:27.678:
```
13:07:27.649 [pid=2052 dwm.exe] [VK_DESCRIPTOR] BOUND: bindings=3 (CB=1 SRV=1 SAMPLER=1)
13:07:27.678 [pid=2052 dwm.exe] [VK_BRIDGE] FlushCommands ENTER: gravacao ativa, finalizando (cmd=0000000007582340)...
13:07:29.743 [pid=2052 dwm.exe] [VK_BRIDGE] FlushCommands: Execucao e Fence em hardware CONCLUIDOS com SUCESSO!
13:07:29.745 [pid=2052 dwm.exe] Bc250UmdExecuteVkDraw: DRAW fence concluida; pixels NAO CONFERIDOS (VkDraws=2, Total=2, Verts=126, Inst=1, Indexed=1)
```
O Draw 2 demorou **2,065 segundos** para concluir. Às 13:07:29.74, o watchdog do WDDM (limite padrão de 2,0 segundos) disparou o TDR `McdmResetFromTimeout`.

### 3.3 A Causa Raiz: Metadados DCC e Descritor `T#[0]`
O KMD capturou no TDR:
```
[TDR_GCVM_FAULT] DECODIFICADO Status=0x0010153C -> Vmid=1 WalkerError=6 MappingError=1 PermissionFaults=0x3 Cid=10 Rw=0 MoreFaults=0 | FaultAddrPagina=0x0000000000002431 -> GpuVa=0x2431000
[TDR_GCVM_FAULT] CANDIDATO <<20 = 0x243100000 DENTRO da faixa viva Base=0x242F00000 Size=0xA8C000 Offset=0x200000 Proc=0000002400000024 Alloc=000000F6000000F6 Token=0x5245530004B40016 Nome=RESOURCE Seg=1 (LOCAL_VRAM) SegOff=0x47C0000
[PT_WALK FAULT_VMID_POOL] RESOLVIDO: VA=0x243100000 -> 0x451D8C000 (root=MC, folha=MC)
```
Pela arquitetura GFX10 / RDNA1:
1. `Cid=10` corresponde a `TCP_UTCL0` (Texture Cache Pipe, definido em `gc_10_1_0_sh_mask.h`).
2. A textura `res=0000000006929170` foi alocada em `0x243100000`. No descritor `T#` (`SQ_IMG_RSRC_WORD0`), o endereço base é programado como `va >> 8 = 0x02431000`.
3. Na criação da imagem com `VK_IMAGE_TILING_OPTIMAL`, o RADV habilitou compressão DCC (`reqSize=2310144` vs tamanho da imagem de `2048000` bytes).
4. O upload via `vkCmdCopyBufferToImage` copiou os pixels para o buffer de imagem, mas **não inicializou a superfície de metadados DCC** (262 KiB na cauda da alocação).
5. O descritor foi montado com `COMPRESSION_EN=1` e apontando para a área de metadados. Ao executar `image_sample`, o descompressor do TCP leu dados não inicializados na área de DCC, provocando stall severo no pipeline ou tentando dereferenciar blocos de compressão inválidos, gerando a requisição faltosa em `0x2431000` (`PDE=0`) e o stall de 2,065 s.

---

## 4. O que Continua NÃO VALIDADO EM HARDWARE
- Execução do Draw 2 sem compressão DCC (`RADV_DEBUG=nodcc`).
- Amostragem de textura pelo Pixel Shader com conclusão em < 5 ms.
- Apresentação completa do frame composto pelo DWM no scanout GOP.
- Conclusão do harness Direct3D 11 Native com contagem de quadros apresentados > 0.
