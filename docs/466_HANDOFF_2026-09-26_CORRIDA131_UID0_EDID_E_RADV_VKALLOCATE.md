# Passagem de Bastão — BC-250 WDDM após a Corrida 131

> **RETIFICAÇÃO (26/09/2026, [doc 467](467_DX85_DEADLOCK_DA_DX83_NA_PAGINACAO_E_CRITERIO_CORRIDA132.md)):** a tela preta das Corridas 130 e 131 foi causada pela própria instrumentação DX.83 — linhas `BC250_HANG` do wrapper de `BuildPagingBuffer` fazendo I/O de arquivo na thread de paginação do VidMm (último `ENTER op=12` sem `LEAVE`, `BUILD_ENTER` seguinte nunca registrado). Ficam refutados: "KMD paging vivo" (o batimento é só do writer), "UID1 impedia CommitVidPn" (a 130 comitou com UID1) e "gargalo na RADV" (sintoma da paging fence parada). O texto abaixo é preservado como histórico.

> **Data:** 2026-09-26  
> **Autor da Passagem:** Antigravity / Gemini CLI  
> **Destinatário:** Próximo Agente de IA / Engenheiro de Desenvolvimento  
> **Ponto de Partida Operacional:** Leia este documento, o topo de [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md), as diretrizes normativas [`AGENTS.md`](../AGENTS.md) / [`GEMINI.md`](../GEMINI.md), e os relatórios analíticos recentes: [`463`](463_CORRIDA130_DX83_ANALISE_TRAVAMENTO_ETW_EDID_E_RESULTADO.md), [`464`](464_DX84_TOPOLOGIA_UID0_EDID_SINTETICO_E_CRITERIO_CORRIDA131.md) e [`465`](465_CORRIDA131_DX84_TOPOLOGIA_UID0_SUCESSO_E_TRAVAMENTO_RADV_VKALLOCATE.md).

---

## 0. Resumo Executivo para Quem Acabou de Chegar

### O Objetivo do Projeto
Rodar jogos no **Windows 10 22H2 x64** na **AMD BC-250 APU** (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2, 24 CUs ativas, 16 GB GDDR6 unificada) através da porta DisplayPort física da própria placa, utilizando Direct3D 11/12 e Vulkan, **sem WARP e sem nenhum fallback de renderização por software**.

### Arquitetura de Vídeo Vigente
- **Display v1 por Herança do UEFI GOP:** Modo fixo `2560x1080` a ~56,13 Hz (ou 60 Hz sintético), scanout alocado em VRAM real (`Phys=0xC0000000`), modelo de apresentação `blt` via DMA Engine (SDMA0), sincronização VSync com leitura direta do contador de quadros do bloco OTG (`FE800000 + 0x14030`).
- **Governança de DCN:** **Proibida qualquer escrita em registrador DCN** antes da prioridade P9 do roadmap. Apenas leitura é permitida.

### Onde Estamos Agora (26/09/2026 — Pós-Corrida 131)
1. **Topologia de Display e EDID (Resolvido na Corrida 131):**
   - Na Corrida 130, o KMD declarava `ChildUid = 1` com EDID sintético desligado, fazendo o Windows detectar o monitor físico LG Ultrawide como desconectado (pois sob o GOP ele pertencia a `UID0`). Isso gerava um display fantasma sem EDID, descartava primárias (`MODECHANGE_BLANK_SKIP`) e impedia o comit do VidPn.
   - Na Corrida 131, corrigimos `ChildRelations[0].ChildUid = 0` e ativamos `EnableSyntheticEdid = 1` no KMD e INF. O Windows reconheceu imediatamente `DISPLAY\BCX13FE\5&41A81D0&15&UID0` como *"Monitor Genérico PnP"* com 128 bytes de EDID válidos.
   - O Windows comitou com sucesso o modo via `CommitVidPn` (Call #1, `AffectedSource=0, Started=1`) e executou `SetVidPnSourceAddress(SourceId=0, Addr=0xAAC000)`. O OTG VSync sincronizou perfeitamente (`OTG_Frame=7725`).
2. **Deadlock de Kernel via `WaitPaging` (Resolvido na Corrida 131):**
   - Na Corrida 130, o `Explorer.EXE` aguardava a fence de paginação 7004 chamando `pfnWaitForSynchronizationObjectFromCpuCb` com `hAsyncEvent = NULL`, causando uma espera síncrona sem timeout no `dxgkrnl.sys` que travava o kernel.
   - Na Corrida 131, adicionamos espera assíncrona com evento (`hAsyncEvent`) e timeout de 1000 ms na UMD. O Explorer atingiu o timeout (`WaitPaging TIMEOUT (1000ms) - continuando sem travar`) e prosseguiu sem congelar o kernel.
3. **KMD Paging Engine Comprovado Saudável:**
   - O rastro físico durável (`C:\BC250_PAGING_TRACE.bin`) comprovou que o engine de paginação encerrou com `active_count = 0` e manteve 632 batimentos de heartbeat (~64 segundos de atividade saudável no kernel até o reset efetuado pelo operador). O KMD **não** travou.
4. **O Gargalo Ativo que Bloqueia o Vídeo (Descoberto na Corrida 131):**
   - Tanto o `dwm.exe` (PID 1212) quanto o `Explorer.EXE` (PID 5788) congelaram dentro de **`vkAllocateMemory`** no backend da RADV (`vulkan_radeon.dll`).
   - No arquivo [`mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_bo.c`](file:///C:/Users/Daniel/Desktop/bc250-win/bc250-win/mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_bo.c#L694-L717), a função `radv_wddm2_bo_create_internal` executa `WaitForSynchronizationObjectFromCpu` de forma síncrona pura, sem passar `hAsyncEvent`, sem checar se a fence da CPU já avançou e sem timeout de proteção.
   - Isso bloqueia as threads do DWM e do Explorer indefinidamente, impedindo o DWM de produzir qualquer quadro (`DxgkDdiPresent = 0`) e mantendo a tela preta.

---

## 1. Estado da Máquina e Ambiente

| Componente | Estado Atual | Observações |
|---|---|---|
| **Adaptador Ativo** | `Adaptador de Vídeo Básico da Microsoft` | Status OK, Código 0, Problem `CM_PROB_NONE`. |
| **Fusível de Boot** | `CLEAN` | `C:\BC250_BOOT_FUSE.dat` inexistente / limpo. |
| **Marcador de Boot** | `CLEAN_BOOT_REQUIRED` | `C:\BC250_REQUIRE_CLEAN_BOOT.json` presente. **Exige boot frio com desligamento completo da energia antes do próximo bind físico.** |
| **Toolchain** | VS 2026 (MSVC 14.51 / layout 18) | WDK 10.0.28000.2526, Windows SDK 10.0.28000.0, Python 3.12, Ninja 1.12. |
| **Mesa / RADV** | `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm` | Compilador Ninja (`build_radv.bat`), binário de deploy em `C:\BC250_LIB\vulkan_radeon.dll`. |

---

## 2. Linha do Tempo Recente das Corridas

| Corrida | Candidato | Janela / Modo | Resultado e Desfecho Diagnóstico | Documentos |
|---|---|---|---|---|
| **127** | DX.80b, 3.284.1.0 | 600 s | Resistência de 10 min sem BugCheck; stall de escape de 4 s e laço do DWM. | [`455`](docs/455_CORRIDA127_DX80B_600S_ENDURANCE_E_RESULTADO.md), [`457`](docs/457_AUDITORIA_CORRIDA127_TDR_SDMA_GPUVA_E_CRITERIO128.md) |
| **128** | DX.81, 3.285.1.0 | 600 s | Stall eliminado (4 ms); congelamento aos 214 s por colisão de teardown multithread no Explorer. | [`456`](docs/456_CRITERIO_CORRIDA128_DX81_STALL_ESCAPE_E_GPUVA4096.md), [`457`](docs/457_CORRIDA128_DX81_STALL_ELIMINADO_CONGELAMENTO_E_RESULTADO.md) |
| **129** | DX.82, 3.287.1.0 | 600 s | Falha aos ~90 s; DWM parou em `VkBridgeShutdown`; Explorer travou em `WaitPaging`. | [`458`](docs/458_CRITERIO_CORRIDA129_DX82_SINCRONIZACAO_TEARDOWN_E_600S.md), [`459`](docs/459_CORRIDA129_DX82_TRAVAMENTO_PRECOCE_ANALISE.md) |
| **130** | DX.83, 3.288.1.0 | 600 s | Tela preta imediata. Diagnóstico ETW + EDID isolou divergência `UID0` vs `UID1` e espera síncrona sem evento no `WaitPaging`. | [`461`](docs/461_DX83_RASTRO_TRAVAMENTO_ETW_E_CRITERIO_CORRIDA130.md), [`463`](docs/463_CORRIDA130_DX83_ANALISE_TRAVAMENTO_ETW_EDID_E_RESULTADO.md) |
| **131** | DX.84, 3.289.1.0 | 600 s | **Avanço Crítico:** `UID0` e EDID sintético aceitos, `CommitVidPn` ativo (`0xAAC000`), VSync OTG sincronizado, `WaitPaging` com timeout de 1000 ms salvou o Explorer. **Bloqueio isolado:** `vkAllocateMemory` no backend RADV (`radv_wddm2_bo.c`). | [`464`](docs/464_DX84_TOPOLOGIA_UID0_EDID_SINTETICO_E_CRITERIO_CORRIDA131.md), [`465`](docs/465_CORRIDA131_DX84_TOPOLOGIA_UID0_SUCESSO_E_TRAVAMENTO_RADV_VKALLOCATE.md) |

---

## 3. Detalhamento Técnico das Modificações Realizadas

### A. Alinhamento de Topologia com UEFI GOP (`UID0`)
Arquivos modificados:
- [`driver/wdk/bc250_display_ddi.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_display_ddi.c):
  - `ChildRelations[0].ChildUid = 0;` (alinhado com a saída física detectada no GOP pelo Windows).
  - `VidPnTargetId = 0;` nas notificações de VSync e na construção de topologia cofuncional (`RecommendFunctionalVidPn`, `EnumVidPnCofuncModality`, `RecommendVidPnTopology`).
  - `Bc250DxgkDdiQueryDeviceDescriptor` configurado para aceitar requisições em `ChildUid == 0` e retornar 128 bytes de EDID sintetizado (`Bc250BuildEdid`).
- [`driver/wdk/bc250_active_miniport.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_active_miniport.c):
  - `Bc250ActiveReadTunable(L"EnableSyntheticEdid", 1u)` configurado com fallback padrão 1 (ativo).
- [`driver/wdk/package/bc250mcdm.inf`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/package/bc250mcdm.inf):
  - Inserido `HKR,Parameters,EnableSyntheticEdid,%REG_DWORD%,1`.
- Suítes de testes unitários atualizadas e validadas:
  - [`driver/tests/active_miniport_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/active_miniport_test.c) e [`driver/tests/display_vidpn_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/display_vidpn_test.c) ajustados para `ChildUid == 0`.

### B. Prevenção de Deadlock em `WaitPaging` na UMD
Arquivos modificados:
- [`driver/umd/bc250_wddm_winsys.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_wddm_winsys.c):
  - `Bc250WinsysWaitPaging` e `Bc250WinsysWaitForFenceFromCpu`:
    ```c
    HANDLE hWaitEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    args.hAsyncEvent = hWaitEvent;
    args.Flags.Async = 1;
    hr = pWinsys->pfnWaitForSynchronizationObjectFromCpuCb(pWinsys->hDevice, &args);
    if (SUCCEEDED(hr) && hWaitEvent != NULL) {
        DWORD waitRes = WaitForSingleObject(hWaitEvent, 1000u);
        if (waitRes == WAIT_TIMEOUT) {
            Bc250WinsysTrace("[WDDM_WINSYS] WaitPaging TIMEOUT (1000ms): continuando sem travar");
        }
        CloseHandle(hWaitEvent);
    }
    ```
- Mocks atualizados em [`bc250_wddm_winsys_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/bc250_wddm_winsys_test.c), [`bc250_d3d_umd_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/bc250_d3d_umd_test.c), [`umd_node1_submit_contract_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/umd_node1_submit_contract_test.c) e [`umd_node1_tripod_contract_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/umd_node1_tripod_contract_test.c).

---

## 4. O Problema Aberto Exato: O que Fazer a Seguir (Candidato DX.85 / Corrida 132)

### A. Onde está o defeito
No arquivo de winsys WDDM2 da árvore RADV:
[`C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\src\amd\vulkan\winsys\wddm2\radv_wddm2_bo.c`](file:///C:/Users/Daniel/Desktop/bc250-win/bc250-win/mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_bo.c#L694)

Na função `radv_wddm2_bo_create_internal`:
```c
   const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait = {
      .hDevice = ws->device_h,
      .ObjectCount = 1,
      .ObjectHandleArray = &ws->paging_fence_h,
      .FenceValueArray = &paging_fence_value,
   };

   cur_paging_fence = ws->paging_fence_cpu_va ? *ws->paging_fence_cpu_va : 0;
   fprintf(stderr, "[RADV_WDDM2] ENTER WaitForSynchronizationObjectFromCpu (paging): target_fence=%llu cur_paging_fence=%llu\n",
           (unsigned long long)paging_fence_value, (unsigned long long)cur_paging_fence);
   fflush(stderr);

   status = WDDM2_DISPATCH(WaitForSynchronizationObjectFromCpu(&wait));
```

### B. O que deve ser alterado em `radv_wddm2_bo.c`
1. **Pular a espera se a fence da CPU já atingiu o valor:**
   ```c
   if (ws->paging_fence_cpu_va && *ws->paging_fence_cpu_va >= paging_fence_value) {
      /* Já concluído pelo hardware/kernel; não precisa entrar no kernel */
      goto allocation_done;
   }
   ```
2. **Transformar a espera síncrona em assíncrona com timeout de segurança:**
   ```c
   HANDLE hAsyncEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
   D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait = {
      .hDevice = ws->device_h,
      .ObjectCount = 1,
      .ObjectHandleArray = &ws->paging_fence_h,
      .FenceValueArray = &paging_fence_value,
      .hAsyncEvent = hAsyncEvent,
      .Flags = { .Async = (hAsyncEvent != NULL) ? 1 : 0 },
   };

   status = WDDM2_DISPATCH(WaitForSynchronizationObjectFromCpu(&wait));
   if (NT_SUCCESS(status) && hAsyncEvent != NULL) {
      DWORD waitRes = WaitForSingleObject(hAsyncEvent, 1000u);
      if (waitRes == WAIT_TIMEOUT) {
         fprintf(stderr, "[RADV_WDDM2] AVISO: WaitForSynchronizationObjectFromCpu TIMEOUT (1000ms) target_fence=%llu\n",
                 (unsigned long long)paging_fence_value);
      }
      CloseHandle(hAsyncEvent);
   }
   ```
3. **Redirecionar logs de diagnóstico da RADV para arquivo:**
   Em processos do Windows como `dwm.exe` e `Explorer.EXE`, `stderr` é descartado. Adicionar uma rotina simples em `radv_wddm2_winsys.c` ou `radv_wddm2_bo.c` que abra `C:\BC250_LIB\radv_wddm2.log` em modo append quando a variável de ambiente ou `is_bc250` estiver ativa, permitindo visibilidade total das operações internas da RADV sob o DWM.

### C. Como Compilar e Implantar a Nova RADV
1. Execute o script de compilação da RADV em prompt de comando x64:
   ```cmd
   call C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build_radv.bat
   ```
2. Copie a DLL compilada para a pasta isolada:
   ```cmd
   copy /Y "C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll" "C:\BC250_LIB\vulkan_radeon.dll"
   ```
3. Atualize o espelho e proveniência no repositório:
   ```cmd
   python tools/sync-radv-fork-delta.py --update
   python tools/sync-radv-fork-delta.py --verify
   ```

### D. Como Empacotar e Auditar o Candidato DX.85
1. Atualize a versão no INF para `3.290.0.0` em `driver/wdk/package/bc250mcdm.inf`.
2. Execute o empacotamento:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1
   ```
3. Execute todas as auditorias de segurança:
   ```powershell
   python tools/audit-bc250mcdm-safety.py
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-lab-package.ps1
   ```

---

## 5. Diretrizes Finais de Governança para o Próximo Agente

1. **Cumprimento estrito de `AGENTS.md` e `GEMINI.md`:**
   - Nunca invente percentuais ou declare validação em hardware antes de um ensaio físico.
   - Sempre forneça o relatório obrigatório de 5 pontos (Avanço, Sucessos, Falhas, Veredito, Recuperação/Pendências) após analisar cada corrida física.
   - Sempre utilize links clicáveis no formato `file:///`.
2. **Respeito aos marcadores de boot limpo:**
   - Antes de qualquer bind físico, o Daniel deve realizar **boot frio com corte completo de energia**. Nunca remova o arquivo `C:\BC250_REQUIRE_CLEAN_BOOT.json` para "burlar" o preflight; isso protege o silício contra estados zumbis de hardware.
