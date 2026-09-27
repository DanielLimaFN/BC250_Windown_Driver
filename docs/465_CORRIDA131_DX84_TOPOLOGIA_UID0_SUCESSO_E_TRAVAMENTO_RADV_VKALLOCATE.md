# 465 - Corrida 131 (DX.84): Sucesso na Topologia UID0 e CommitVidPn, e Análise do Travamento em vkAllocateMemory (RADV)

> **RETIFICAÇÃO (26/09/2026, [doc 467](467_DX85_DEADLOCK_DA_DX83_NA_PAGINACAO_E_CRITERIO_CORRIDA132.md)):** a tela preta das Corridas 130 e 131 foi causada pela própria instrumentação DX.83 — linhas `BC250_HANG` do wrapper de `BuildPagingBuffer` fazendo I/O de arquivo na thread de paginação do VidMm (último `ENTER op=12` sem `LEAVE`, `BUILD_ENTER` seguinte nunca registrado). Ficam refutados: "KMD paging vivo" (o batimento é só do writer), "UID1 impedia CommitVidPn" (a 130 comitou com UID1) e "gargalo na RADV" (sintoma da paging fence parada). O texto abaixo é preservado como histórico.

> **Data:** 2026-09-26  
> **Candidato:** DX.84 / Pacote Lab `3.289.1.0`  
> **Pasta de Evidência:** [`docs/evidence/2026-09-26_08-19-24-DESKTOP-HQAG6T4-lab-binding`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-09-26_08-19-24-DESKTOP-HQAG6T4-lab-binding)  
> **Rastro ETW:** [`docs/evidence/2026-09-26_08-19-18-DX83-HANG-ETW`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-09-26_08-19-18-DX83-HANG-ETW)  
> **Resultado:** PARCIAL (Avanço significativo na topologia, comit de VidPn e desacoplamento do kernel; bloqueio aberto na RADV).

---

## 1. Houve Avanço?

**SIM.** Houve avanço funcional e arquitetural crítico em relação à Corrida 130:

1. **Topologia Alinhada e EDID Sintético Funcionaram Perfeitamente:**
   - Na Corrida 130, o monitor físico em `UID0` foi detectado como desconectado e rebaixado a "Monitor Genérico não PnP" de zero bytes na saída fantasma `UID1`, descartando primárias e impedindo qualquer `CommitVidPn`.
   - Na Corrida 131, com `ChildUid = 0` e `EnableSyntheticEdid = 1`, o Windows PnP registrou com sucesso `DISPLAY\BCX13FE\5&41A81D0&15&UID0` como "Monitor Genérico PnP" contendo os 128 bytes de EDID sintetizados (resolução nativa `2560x1080@60Hz`, pixel clock `173.61 MHz`, cabeçalho e checksum válidos).

2. **CommitVidPn e SetVidPnSourceAddress Foram Executados:**
   - O subsistema gráfico do Windows aceitou o modo e executou `CommitVidPn` (Call #1, `AffectedSource=0, Started=1`) seguido de `SetVidPnSourceAddress(SourceId=0, Addr=0xAAC000)`.
   - O OTG VSync foi vinculado e sincronizado em `TargetId=0` (`OTG_Frame=7725`).

3. **Prevenção de Deadlock em `WaitPaging` Provada:**
   - O `Explorer.EXE` (PID 5788) atingiu o limite de espera em `WaitPaging` e desarmou com segurança via timeout de 1000 ms (`WaitPaging TIMEOUT (1000ms) - continuando sem travar`), evitando o congelamento global das travas do kernel `dxgkrnl.sys`.

4. **KMD Permaneceu Totalmente Vivo e Operacional:**
   - O decodificador do rastro físico durável (`C:\BC250_PAGING_TRACE.bin`) comprovou que a fila de paginação encerrou com `active_count = 0` e manteve 632 batimentos cardíacos (`heartbeat_after_last_paging`), registrando ~64 segundos de atividade saudável no kernel até o reset efetuado pelo operador.
   - O motor SDMA indireto executou 100% das submissões (#3 a #19) com tripé comprovado no silício (`TRIPE_PROVADO`, `Canaries=5`, `Divergentes=0`).

---

## 2. Diagnóstico da Falha: Bloqueio em `vkAllocateMemory` (RADV)

Apesar do sucesso no subsistema de display e no KMD, o desktop permaneceu em tela preta porque **nenhum quadro chegou a ser apresentado pelo DWM (`DxgkDdiPresent = 0`)**.

A mineração dos logs de UMD (`BC250_UMD_LOG_1212.txt` do `dwm.exe` e `BC250_UMD_LOG_5788.txt` do `Explorer.EXE`) isolou com exatidão o ponto do congelamento:

1. **`dwm.exe` (PID 1212):**
   - Criou os buffers D3D11 1 e 2 com sucesso através da ponte Vulkan (`[VK_RES] SUCESSO: Buffer criado`).
   - Às `08:19:50.178`, iniciou a criação do terceiro buffer (`reqSize=4096, memTypeIndex=2`) chamando `vkAllocateMemory`.
   - **Nunca retornou dessa chamada.**

2. **`Explorer.EXE` (PID 5788):**
   - Às `08:19:52.250`, após escapar do `WaitPaging` pelo timeout de 1000 ms, chamou `Bc250UmdVkCreateResource` (`BUFFER size=4096`) e congelou exatamente na mesma rotina de `vkAllocateMemory`.

3. **Causa Raiz no Backend RADV (`radv_wddm2_bo.c`):**
   - Ao alocar memória no Vulkan, o driver `vulkan_radeon.dll` executa internamente:
     ```c
     radv_wddm2_bo_create_internal(...) ->
         CreateAllocation2(&create);
         MapGpuVirtualAddress(&map);
         MakeResident(&make_resident);
         WaitForSynchronizationObjectFromCpu(&wait);
     ```
   - No `radv_wddm2_bo.c` (linhas 694–717):
     - A chamada `WaitForSynchronizationObjectFromCpu` é realizada de forma **síncrona pura**, sem passar `hAsyncEvent`.
     - Não há verificação se `*ws->paging_fence_cpu_va >= paging_fence_value` antes de entrar na espera do kernel.
     - Não há timeout de proteção (ao contrário do que implementamos na UMD D3D11).
     - Quando a fence de paginação interna da fila do Vulkan atrasa ou não progride, a thread do DWM e do Explorer entra em espera infinita em modo usuário/kernel, paralisando a inicialização do pipeline gráfico antes que o primeiro `Present` possa ocorrer.

---

## 3. Registro Criptográfico e Evidências

| Item | Valor / Caminho |
|---|---|
| **bc250mcdm.sys** | `2A2433C6B20541D772946673230B297B5C0C946485A2CEED8536C7200EC46B2F` |
| **bc250umd.dll** | `39E6E62AD3C1FB6484CA1631B0F8C212C755088CB6DD3C64C2DF03B52AAB6C35` |
| **vulkan_radeon.dll** | `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C` |
| **EDID Sintetizado (Bound)** | `97FAF3D07E7E87872444E0CB4D81289368776544ABB9E888153268B5E3172C09` (128 bytes) |
| **Monitor Reconhecido** | `DISPLAY\BCX13FE\5&41A81D0&15&UID0` ("Monitor Genérico PnP") |
| **CommitVidPn** | AffectedSource=0, Started=1, Primary=0xAAC000, VSync OTG=7725 |
| **SDMA Indirect** | Submissões #3 a #19 concluídas (`TRIPE_PROVADO`, `Canaries=5`, 0 divergências) |
| **Paging Trace KMD** | 72.547 registros válidos, `latest_sequence = 4485`, `active_count = 0`, 632 heartbeats |

---

## 4. Próximos Passos (Plano para DX.85 / Corrida 132)

1. **Correção em `radv_wddm2_bo.c` (mesa-radv-wddm):**
   - Adicionar checagem prévia de fence na CPU: `if (ws->paging_fence_cpu_va && *ws->paging_fence_cpu_va >= paging_fence_value) return VK_SUCCESS;`.
   - Adicionar espera assíncrona limitada (`hAsyncEvent` com timeout de 1000 ms) em `WaitForSynchronizationObjectFromCpu` dentro de `radv_wddm2_bo_create_internal`, espelhando a proteção implementada na UMD D3D11.
   - Redirecionar logs de diagnóstico da RADV para arquivo (para não depender de `stderr`, que é descartado em processos de sistema como DWM e Explorer).

2. **Reconstruir `vulkan_radeon.dll`, empacotar e auditar o candidato DX.85.**
