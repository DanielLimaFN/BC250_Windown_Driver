# 464 - Candidato DX.84: Topologia UID0 Alinhada ao GOP, Ativação de EDID Sintético e Espera Bounded em Paging (Critério da Corrida 131)

> **Data:** 2026-09-26  
> **Candidato:** DX.84 / Pacote Lab `3.289.1.0`  
> **Objetivo:** Resolver as causas-raiz identificadas na Corrida 130 ([doc 463](463_CORRIDA130_DX83_ANALISE_TRAVAMENTO_ETW_EDID_E_RESULTADO.md)): a tela preta imediata provocada por desconexão de monitor (`UID0` vs `UID1`) e o congelamento aos ~5 s decorrente de bloqueio indefinido em `WaitPaging` (Paging Fence 7004 no Explorer).

---

## 1. Contexto e Diagnóstico da Corrida 130

Na Corrida 130 (DX.83), foram mineradas duas falhas críticas independentes:

1. **Desconexão do Monitor Físico e Criação de Display Fantasma:**
   - Sob o `BasicDisplay` (UEFI GOP), o Windows reconhece o monitor LG Ultrawide 2560x1080 na saída `UID0` (`DISPLAY\GSM5B98\5&41A81D0&15&UID0`) com 128 bytes de EDID válidos.
   - O KMD declarava `ChildRelations[0].ChildUid = 1` e `EnableSyntheticEdid = 0`.
   - Ao carregar o driver, o Windows detectava a saída `UID0` como desconectada e tentava consultar o EDID da nova saída `UID1`. Com o EDID sintético desligado, a DDI retornava `STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA`.
   - O Windows rebaixava a saída para um "Monitor Genérico não PnP" (`DISPLAY\DEFAULT_MONITOR\...\UID1`) com zero bytes de EDID, descartava as primárias do DWM (`MODECHANGE_BLANK_SKIP`) e mantinha a tela preta com `Presents = 0`.

2. **Bloqueio Indefinido no Kernel via `WaitPaging` no Explorer:**
   - `Explorer.EXE` (PID 6444) criou um recurso em seu dispositivo secundário e recebeu `E_PENDING` com `PagingFence = 7004`.
   - A UMD chamou `pfnWaitForSynchronizationObjectFromCpuCb` com `hAsyncEvent = NULL`.
   - Sem evento associado, o `dxgkrnl.sys` realizou uma espera síncrona sem timeout em modo kernel, retendo travas globais de dispositivo e congelando o subsistema gráfico quando o DWM tentou alocar memória (`vkAllocateMemory`).

---

## 2. Modificações Implementadas no Candidato DX.84

### A. Alinhamento de Topologia com UEFI GOP (`UID0`)
- **[`driver/wdk/bc250_display_ddi.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_display_ddi.c):**
  - `ChildRelations[0].ChildUid = 0;` (alinhado com o target do GOP e com o registro PnP de `DISPLAY\GSM5B98\...\UID0`).
  - `data.CrtcVsync.VidPnTargetId = 0;` e `data.DisplayOnlyVsync.VidPnTargetId = 0;` em `Bc250DxgkNotifyInterruptVsync`.
  - `newPath->VidPnTargetId = 0;` nas rotinas de topologia VidPn (`RecommendFunctionalVidPn`, `EnumVidPnCofuncModality`, `RecommendVidPnTopology`).
  - `StopDeviceAndReleasePostDisplayOwnership` já entregava `DisplayInfo->TargetId = 0`, eliminando a divergência anterior.
  - Testes unitários [`driver/tests/active_miniport_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/active_miniport_test.c) e [`driver/tests/display_vidpn_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/display_vidpn_test.c) atualizados para validar `ChildUid == 0`.

### B. Ativação de EDID Sintético de 128 Bytes
- **[`driver/wdk/package/bc250mcdm.inf`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/package/bc250mcdm.inf):**
  - `HKR,Parameters,EnableSyntheticEdid,%REG_DWORD%,1` habilitado.
- **[`driver/wdk/bc250_active_miniport.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_active_miniport.c):**
  - `Bc250ActiveReadTunable(L"EnableSyntheticEdid", 1u)` configurado com fallback padrão 1 (ativo).
- **[`driver/wdk/bc250_display_ddi.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_display_ddi.c):**
  - `g_Bc250DisplaySyntheticEdidEnabled` inicializado com padrão 1.
  - `Bc250DxgkDdiQueryDeviceDescriptor` aceita requisições para `ChildUid == 0` e `ChildUid == 1`, retornando o bloco de 128 bytes sintetizado pelo `Bc250BuildEdid` (resolução nativa 2560x1080@60Hz, checksum correto e temporizações padrão).

### C. Prevenção de Deadlock em `WaitPaging` com Async Event
- **[`driver/umd/bc250_wddm_winsys.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_wddm_winsys.c):**
  - Em `Bc250WinsysWaitPaging` e `Bc250WinsysWaitForFenceFromCpu`, a chamada a `pfnWaitForSynchronizationObjectFromCpuCb` agora cria e passa um `hAsyncEvent` (`CreateEventW`).
  - A espera do usuário é realizada através de `WaitForSingleObject(hWaitEvent, 1000u)`.
  - Se a fence atrasar além de 1000 ms, o driver registra o evento de timeout na telemetria (`WAIT_PAGING TIMEOUT`), fecha o handle e prossegue sem bloquear indefinidamente as travas de kernel do `dxgkrnl.sys`.
  - Mocks dos testes unitários ([`bc250_wddm_winsys_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/bc250_wddm_winsys_test.c), [`bc250_d3d_umd_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/bc250_d3d_umd_test.c), [`umd_node1_submit_contract_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/umd_node1_submit_contract_test.c), [`umd_node1_tripod_contract_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/umd_node1_tripod_contract_test.c)) atualizados para sinalizar `hAsyncEvent` quando presente.

---

## 3. Identidade Criptográfica do Pacote DX.84 (`3.289.1.0`)

| Arquivo | Caminho | SHA-256 |
|---|---|---|
| **bc250mcdm.sys** | `driver/wdk/package/output_lab/bc250mcdm.sys` | `2A2433C6B20541D772946673230B297B5C0C946485A2CEED8536C7200EC46B2F` |
| **bc250umd.dll** | `driver/wdk/package/output_lab/bc250umd.dll` | `39E6E62AD3C1FB6484CA1631B0F8C212C755088CB6DD3C64C2DF03B52AAB6C35` |
| **bc250mcdm.inf** | `driver/wdk/package/output_lab/bc250mcdm.inf` | `DFA90365D19500CE25DEE24B95F533BB73D347ECE07D260B4E4E905013A2EC09` |
| **bc250mcdm.cat** | `driver/wdk/package/output_lab/bc250mcdm.cat` | `F6949D47ABCA71DA9B8596124D0E77A03A958E0B462EEF2093ED24306598ED30` |
| **vulkan_radeon.dll** | `mesa-radv-wddm/build/.../vulkan_radeon.dll` | `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C` |

- **Manifesto:** [`driver/wdk/package/output_lab.manifest.json`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/package/output_lab.manifest.json)
- **Preflight de Validação:** [`tools/prepare-corrida131-dx84.ps1`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/prepare-corrida131-dx84.ps1) (PASS)
- **Comando de Execução:** [`tools/EXECUTAR_CORRIDA131_DX84_600S.cmd`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/EXECUTAR_CORRIDA131_DX84_600S.cmd)

---

## 4. Critério de Aceitação para a Corrida 131

1. **Reconhecimento do Monitor:** O Windows deve manter o monitor ativo como `UID0`, registrando os 128 bytes válidos de EDID em `edid-Bound.json`.
2. **Sinal de Imagem Ativo:** O monitor não pode permanecer em tela preta/sem sinal; o DWM deve receber caminhos de apresentação VidPn e submeter quadros de scanout.
3. **Ausência de Deadlock:** O Explorer e o DWM não podem travar em `WaitPaging`; se ocorrer latência de fence no dispositivo secundário, a espera deve expirar com segurança sem congelar a sessão.
4. **Resistência na Janela de 600 s:** O ensaio deve progredir sem BugCheck e sem necessidade de reset manual.
5. **Recuperação Limpa:** Se o watchdog de 600 s expirar, o rollback para BasicDisplay Código 0 deve operar com sucesso sem reboot.
