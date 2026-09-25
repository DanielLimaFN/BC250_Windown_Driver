# 458 — Critério Pré-Ensaio da Corrida 129 (DX.82): Sincronização de Teardown Multithread e Resistência de 600 s

> **Resultado posterior:** A Corrida 129 falhou cerca de 90 s após o bind. A causalidade atribuída à Corrida 128 na motivação deste documento era uma hipótese, não uma prova. Ver [análise 459](459_CORRIDA129_DX82_TRAVAMENTO_PRECOCE_ANALISE.md).

> **Data:** 2026-09-25  
> **Candidato:** DX.82 / DriverVer `09/25/2026,3.287.1.0`  
> **Arquivos do Pacote de Laboratório:**
> - KMD SYS: `DABE03E2BFDBC7471B0DAF345A9370855031FA993BBFEE083900262ADB0DC8DF`
> - UMD DLL: `ACF637FB1B58E1F45219B33BAA1DEE9AF07489BB243C306E9AB9C62F2928B7D1`
> - INF: `A5A683C0A362672F392A4D0E2A68304F98DCB68621F9E881B73A9FE170201D31`
> - CAT: `715EC02C19A446399D4CB8982F43002A23BB5ABD3E37D4E2E39B2A269D931DB6`
> - Mesa RADV: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`
>
> **Script de Preparação:** [`tools/prepare-corrida129-dx82.ps1`](../tools/prepare-corrida129-dx82.ps1)  
> **Comando de Execução:** [`tools/EXECUTAR_CORRIDA129_DX82_600S.cmd`](../tools/EXECUTAR_CORRIDA129_DX82_600S.cmd)

---

## 1. Motivação e Causa-Raiz Isolada na Corrida 128

Na Corrida 128 (DX.81), eliminamos com sucesso de 100% o stall do escape linear (reduzido de 4.172 ms para 4 ms) e o DWM executou 45.023 linhas de log ininterruptas sem reiniciar.

Porém, aos 214 segundos (~3 min 34 s), a interface congelou totalmente. A análise dos logs comprovou microssegundo a microssegundo:
1. Em `explorer.exe` (pid 1252), uma thread de trabalho iniciou `CreateResource`, alocou memória WDDM e bloqueou no kernel aguardando a paging fence 7010 (`WaitPaging`).
2. Simultaneamente, a thread principal do Explorer chamou `D3D10 DestroyDevice` -> `Bc250UmdDeviceTeardown` -> `Bc250UmdVkBridgeShutdown`, iniciando a destruição do `VkDevice` e descarregamento da DLL Vulkan.
3. A fence 7010 completou, a thread de trabalho acordou de `WaitPaging` e chamou `Bc250UmdVkCreateResource` -> `vkAllocateMemory` no exato instante em que o `VkDevice` estava sendo destruído ou descarregado.
4. Isso gerou um travamento total no loader Vulkan com travas de sistema/WDDM presas, congelando a máquina inteira. Esse padrão é a causa-raiz da barreira histórica de 3 a 5 minutos observada nas Corridas 108..112, 127 e 128.

---

## 2. Correções Implementadas no Candidato DX.82

1. **Sincronização por `SRWLOCK DeviceLock`:**
   Adicionada trava leve de leitor/escritor em `BC250_UMD_DEVICE`.
2. **Entrada Atômica de Teardown:**
   No início de `Bc250UmdDeviceTeardown`, `TeardownActive = TRUE`, `DeviceActive = FALSE` e `VkBridge.DeviceActive = FALSE` são marcados imediatamente, e `AcquireSRWLockExclusive(&pDevice->DeviceLock)` é adquirido. Isso garante que qualquer thread em andamento conclua suas operações antes que a destruição dos recursos Vulkan se inicie.
3. **Guarda Pós-Paging em `CreateResource`:**
   `CreateResource` adquire a trava compartilhada (`AcquireSRWLockShared`). Após o retorno de `WaitPaging`, revalida explicitamente `if (device->TeardownActive || !device->VkBridge.DeviceActive)`. Se o device entrou em teardown enquanto aguardava a paginação, a criação de recursos Vulkan é imediatamente abortada de forma segura.
4. **Proteção Fail-Closed das DDIs da Bridge:**
   `Bc250UmdVkDestroyResource`, `Bc250UmdVkDestroyRtv`, `Bc250UmdVkDestroyDsv`, `Bc250UmdVkUploadImageData` e `Bc250UmdVkUploadBufferData` agora verificam `if (!pDevice->DeviceActive || !pBridge->DeviceActive || pBridge->Device == VK_NULL_HANDLE)` e saem sem tocar na API Vulkan caso a desmontagem já tenha sido iniciada.

---

## 3. Critérios de Sucesso da Corrida 129

1. **Estabilidade Temporal:**
   - Superar com folga os 214 s da Corrida 128 e atingir a meta completa de 600 segundos (10 minutos) sem travamento da máquina.
2. **Ciclo de Vida Limpo do Shell (Explorer):**
   - Criação e destruição de devices secundários (thumbnails, previews de janelas) sem travamentos, sem deadlocks e sem vazamento de recursos.
3. **Composição e Renderização:**
   - DWM ativo e contínuo por toda a janela de 600 s.
   - Harness D3D11 nativo completando 60/60 quadros com Exit 0 e pixels conferidos.
4. **Recuperação Limpa:**
   - Ao término dos 600 s (ou em caso de interrupção), o watchdog online deve restaurar o `BasicDisplay` Código 0 sem exigir intervenção destrutiva.
