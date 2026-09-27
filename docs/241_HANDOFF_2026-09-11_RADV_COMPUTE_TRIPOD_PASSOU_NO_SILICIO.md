# docs/241 — Handoff: RADV WDDM Compute Tripod PASSOU NO SILÍCIO (15/15 Verificações)

**Data:** 2026-09-11 09:18:11 - 09:21:40  
**Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2, 24 CUs, 16 GB GDDR6)  
**Host:** Windows 10 22H2 x64 (`DESKTOP-HQAG6T4`)  
**Pasta de Evidência:** [`docs/evidence/2026-09-11_09-18-11-DESKTOP-HQAG6T4-lab-binding/`](../evidence/2026-09-11_09-18-11-DESKTOP-HQAG6T4-lab-binding/)  
**Harness:** `driver/tests/output_radv_gate/bc250-radv-compute-gate.exe`  
**ICD:** Mesa RADV (`mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll`, 16.615.936 bytes)  
**Veredito:** **`RADV_WDDM_COMPUTE_TRIPOD PASSOU NO SILICIO`** (`hardware_validated: true`)  

---

## 1. Resumo Executivo: Marco 1 da ROTA 1 Concluído no Silício

O ensaio físico do tripé de compute Vulkan via Mesa RADV sob WDDM 2.0+ foi **executado com sucesso pleno no silício físico da AMD BC-250**.

Todos os **15 critérios de verificação** passaram com status `True`. O compilador ACO gerou o binário de shader GFX1013, o Mesa RADV alocou as páginas WDDM e gravou o command buffer, o KMD (`bc250mcdm.sys`) despachou o pacote para a fila MEC/MQD no Nó 1, a GPU física executou as CUs e substituiu o valor de veneno físico na VRAM por byte proof, e o fence foi aposentado com drenagem de anel e telemetria completa.

---

## 2. Dados de Medição Física (P0)

### 2.1 Conexão e Identificação
- **ICD Carregado:** `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll` (Build MSVC x64)
- **Adaptador Detectado:** `AMD BC-250 Graphics` (vendor=0x1002, device=0x13FE, GFX1013)
- **Queue Family:** `0`, `flags=0x0000000E` (Compute / Transfer)

### 2.2 Prova Física de Bytes (Byte Proof)
- **Endereço do Buffer:** Memória mapeada pelo WDDM (`HOST_VISIBLE | HOST_COHERENT`) com GPUVA alocada e mapeada pelo Mesa RADV no Nó 1.
- **Veneno Inicial:** `0x43DAFFFE` (precedido por validação de zeramento de buffer no harness)
- **Valor Observado Pós-Execução:** `0xBC250C01`
- **Valor Esperado:** `0xBC250C01`
- **Veredito do Harness:** `BC250 RADV COMPUTE BYTE PROOF: PASS` (Exit Code 0)

### 2.3 Telemetria do Silício (KMD Node 1 / MEC)
| Métrica | Antes do Ensaio | Depois do Ensaio | Delta |
| :--- | :--- | :--- | :--- |
| **P1SubmitCalls** | 17 | **18** | **+1** |
| **P1SubmitAccepted** | 17 | **18** | **+1** |
| **P1RetiredTotal** | 17 | **18** | **+1** |
| **P1LastFenceObserved** | 17 | **18** | **+1** (Escrita via `RELEASE_MEM` da GPU) |
| **P1LastWptrPublished** | 0 | **12** | Anel avançou 12 DW (4 DW IB + 8 DW RELEASE_MEM) |
| **P1LastRptrReportMem** | 0 | **12** | **RPTR == WPTR == 12 (Anel drenado)** |
| **V2TrackerPushedTotal** | 0 | **1** | **+1** |
| **V2NotifiedTotal** | 0 | **1** | **+1** |
| **V2NotifyBatches** | 0 | **1** | **+1** |
| **V2FencesInFlight** | 0 | **0** | Quiescido |
| **GpuvaGateOperational** | 1 | **1** | Operacional |
| **GpuvaGateBlockMask** | 0 | **2048** | Bloqueio pós-ensaio esperado (teto de 1 submit) |

### 2.4 Tabela de Verificações do Runner (15/15 TRUE)
1. `process_completed`: **True**
2. `loaded_icd_identity`: **True**
3. `process_exit_zero`: **True**
4. `byte_proof`: **True**
5. `submit_accepted`: **True**
6. `retired`: **True**
7. `ring_drained`: **True**
8. `ring_moved`: **True**
9. `physical_fence_replaced_poison`: **True**
10. `gpuva_gate_operational`: **True**
11. `v2_pushed`: **True**
12. `v2_notified`: **True**
13. `v2_drained`: **True**
14. `no_submit_reject`: **True**
15. `no_tracker_fault`: **True**

---

## 3. Explicação da "Tela Preta" Durante a Corrida

Durante os 180 segundos em que o driver de laboratório esteve vinculado, o usuário observou tela preta. Esse comportamento é **completamente esperado e compreendido na arquitetura**:

1. O driver de laboratório `DISPLAY_RENDER_LAB` assume o adaptador de vídeo e herda o frame buffer scanout do UEFI GOP.
2. O compositor de janelas do Windows (`dwm.exe`) tenta inicializar a interface Direct3D 11 (`OpenAdapter10_2`).
3. Como a UMD nativa D3D11 (`bc250umd64.dll`) declara honestamente `E_NOTIMPL` (`BC250_D3D10_11_NATIVE_READY == 0`), o DWM não consegue desenhar a área de trabalho do Windows.
4. Enquanto a tela fica preta para o DWM, o subsistema WDDM e o hardware estão **100% ativos em Código 0**:
   - VSYNC gerado a 56 Hz no silício (`InitialFrameCount=4443`, interrupções registradas).
   - O harness `bc250-radv-compute-gate.exe` rodou em segundo plano por 5,75 segundos, executou o shader Vulkan e concluiu com êxito.
5. Ao expirar a janela de segurança de 180 segundos do watchdog sem confirmação visual do operador, o script de segurança executou o rollback automático:
   - Dispositivo desabilitado de forma quiescida.
   - `bc250mcdm` desinstalado com fusível de desligamento limpo (`CLEAN (0)`).
   - `BasicDisplay` reinstalado em Código 0.
   - O sinal de vídeo do monitor foi restaurado automaticamente.

---

## 4. O Que Isso Destrava para Jogos

Com o Marco 1 (Compute Vulkan RADV + WDDM KMD) comprovado no silício físico:
- **Temos o compilador de shader funcional (Valve ACO) gerando ISA válida para a GPU BC-250 (GFX1013).**
- **Temos a fila MEC e o anel de computação despachando e executando instruções no hardware real.**
- **Temos a tradução GPUVA e alocação WDDM 2.0+ operando sem falhas de MMU/GFXHUB.**

O próximo passo no Roadmap (Marco 2) é a infraestrutura de **Graphics Queue e Swapchain de Apresentação**:
1. Habilitar a fila gráfica (`GFX_QUEUE`) ou usar o modelo de renderização offscreen Vulkan com cópia (SDMA blt) direta para o frame buffer scanout do UEFI GOP.
2. Com isso, jogos Vulkan e jogos DirectX (via DXVK e VKD3D-Proton) apresentarão seus quadros diretamente na tela, contornando a dependência do compositor D3D11 fechado do Windows.
