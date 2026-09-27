# 463 — Análise da Corrida 130 (DX.83): Captura ETW Pós-Reset, Rastro DX.56, Isolamento de EDID (UID0 vs UID1) e Bloqueio de Paging Fence

> **RETIFICAÇÃO (26/09/2026, [doc 467](467_DX85_DEADLOCK_DA_DX83_NA_PAGINACAO_E_CRITERIO_CORRIDA132.md)):** a tela preta das Corridas 130 e 131 foi causada pela própria instrumentação DX.83 — linhas `BC250_HANG` do wrapper de `BuildPagingBuffer` fazendo I/O de arquivo na thread de paginação do VidMm (último `ENTER op=12` sem `LEAVE`, `BUILD_ENTER` seguinte nunca registrado). Ficam refutados: "KMD paging vivo" (o batimento é só do writer), "UID1 impedia CommitVidPn" (a 130 comitou com UID1) e "gargalo na RADV" (sintoma da paging fence parada). O texto abaixo é preservado como histórico.

> **Data do Ensaio:** 2026-09-26 00:48:23  
> **Candidato:** DX.83 / Pacote Lab `3.288.1.0` (SYS `8FD29E62…`, UMD `85A78934…`, INF `8F8A69E9…`, CAT `E31E0A88…`, RADV `FCAA11FE…`)  
> **Pastas de Evidência:**
> - ETW: [`docs/evidence/2026-09-26_00-48-17-DX83-HANG-ETW`](../evidence/2026-09-26_00-48-17-DX83-HANG-ETW)
> - Binder: [`docs/evidence/2026-09-26_00-48-23-DESKTOP-HQAG6T4-lab-binding`](../evidence/2026-09-26_00-48-23-DESKTOP-HQAG6T4-lab-binding)  
> **Relato do Daniel:** *"vamos lá apenas tela preta nao chegou a dar imagem em nenhum momento, depois de 10m eu reiniciei, ja rodei o powershell -NoProfile -ExecutionPolicy Bypass -File tools\bc250-hang-etw.ps1 -Recover"*

---

## 1. Veredito Geral

- **Houve avanço?**
  - **Funcional:** **Não.** O ensaio apresentou tela preta imediata sem apresentação de quadros do DWM (`Presents = 0`) e travamento completo do subsistema gráfico por volta de 5 segundos após o bind. A máquina exigiu reinício forçado pelo botão após 10 minutos.
  - **Diagnóstico:** **SIM, avanço diagnóstico decisivo.** A instrumentação implantada na DX.83 funcionou com 100% de sucesso pós-hard-reset:
    1. A sessão ETW circular sobreviveu intacta, gravando 268 MiB de escalonador (`kernel-scheduler.etl`) e 134 MiB de subsistema gráfico (`graphics-dxg-dwm.etl`, 118.027 eventos sem perda de buffers).
    2. O rastro durável DX.56 (`paging-trace-after-reset.bin`, 39,6 MiB) refutou a hipótese de que o kernel paging engine estaria preso em laço de execução: todas as 5.261 operações de paginação foram processadas até `active_count = 0` no tick 24 (~2,4 s de ensaio).
    3. A coleta de EDID provou por que o monitor não recebeu sinal: colisão de topologia entre **UID0** e **UID1**.

---

## 2. Descobertas Críticas da Mineração de Evidências

### Descoberta 1: Monitor Desconectado da Apresentação (UID0 vs UID1 e EDID Ausente)
Na fase `PreBind` (sob `BasicDisplay`):
- O Windows reconhecia o monitor físico LG Ultrawide 2560x1080 conectado ao target **`UID0`**:
  `DISPLAY\GSM5B98\5&41A81D0&15&UID0` (128 bytes válidos de EDID, checksum OK).
No momento do bind (`edid-Bound.json`):
- O driver WDDM expõe em `Bc250DxgkDdiQueryChildRelations` hardcoded `ChildUid = 1` (`UID1`) e `VidPnTargetId = 1`.
- A DDI `Bc250DxgkDdiQueryDeviceDescriptor` verificou `Bc250DisplayGetSyntheticEdidEnabled()` que estava **desativado (`0`)**, retornando `STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA`.
- O Windows, portanto, associou o display a um **`DISPLAY\DEFAULT_MONITOR\5&41A81D0&15&UID1` ("Monitor Genérico não PnP") com zero bytes de EDID**, desconectando o display físico do scanout ativo e impedindo o DWM de produzir composições de apresentação (`MODECHANGE_BLANK_SKIP`).

### Descoberta 2: Bloqueio em `WaitPaging` (Paging Fence 7004) no Explorer
A telemetria do `BC250_HANG` na UMD registrou a cronologia exata do travamento:
- `dwm.exe` (PID 1212) inicializou a bridge Vulkan, alocou com sucesso 4 buffers de backbuffer/swapchain e iniciou a criação do 5º buffer às 00:48:45.280.
- Simultaneamente, às 00:48:45.328 (QPC 2377914992), `Explorer.EXE` (PID 6444) criou um recurso em seu dispositivo secundário e chamou `WaitPaging`:
  ```text
  00:48:45.327 [pid=6444 Explorer.EXE] [WDDM_WINSYS] MakeResident LEAVE: hr=0x8000000A PagingFence=7004
  00:48:45.327 [pid=6444 Explorer.EXE] [WDDM_WINSYS] WaitPaging ENTER: TargetFence=7004 CurrentFence=7002 SyncObj=0x8000af00
  00:48:45.328 [pid=6444 Explorer.EXE] BC250_HANG v=1 seq=5 span=3 tid=6640 qpc=2377914992 op=WAIT_PAGING phase=ENTER owner=0000000015580530 a=0x1B5C b=0x8000AF008000AAC0 hr=0x00000000
  ```
- O Explorer bloqueou no kernel (`pfnWaitForSynchronizationObjectFromCpuCb`) aguardando a fence 7004 (`0x1B5C`). Essa fence nunca foi sinalizada no objeto de sincronização `0x8000af00`.
- O DWM, que chamava `vkAllocateMemory` no RADV (o qual internamente invoca alocações D3DKMT), congelou nas travas globais de device do DxgKrnl.
- Minutos depois (00:50:51), o harness `bc250-d3d11-conformance.exe` foi iniciado e congelou em `vkCreateInstance`.

### Descoberta 3: Estado do Kernel Paging Engine (DX.56)
- O leitor binário `paging-trace-after-reset.bin` comprovou que o kernel executou **5.261 operações** de paginação.
- No tick 20 (QPC ~2377418779), `DXGK_OPERATION_FLUSH_TLB` (Operação 12) executou o estágio 44 (`FLUSH_JOURNAL_WAIT`).
- No tick 21 (100 ms depois), a operação completou, seguida de atualizações de tabela de páginas que levaram o estado final para a sequência **5.261, estágio 6 (`AFTER_DISPATCH`), status `0x00000000` e `active_count = 0`**.
- O writer durável continuou registrando batimento a cada 100 ms até o tick 8.701 (870 segundos = 14,5 minutos).
- **Conclusão:** O kernel KMD **não** travou em laço infinito, não deu BugCheck e não causou deadlock no hardware SDMA. O travamento foi de sincronização de fence entre o VidMm e o runtime do dispositivo secundário.

---

## 3. Métricas e Provas Físicas

| Item | Corrida 129 (DX.82) | Corrida 130 (DX.83) | Análise / Evolução |
|---|---|---|---|
| **Tempo até falha** | ~90 s | ~5 s (sintoma aos 5s, 10 min de espera) | Falha de sincronização precoce |
| **Presents KMD** | 0 | 0 | Scanout preservado em branco |
| **Paging Submissions SDMA** | 20 (fences 4114..4117) | 23 (fences 4114..4120) | 23x `TRIPE_PROVADO` (fences e canários 100% OK) |
| **Operações DX.56** | Parou com `active_count=1` | 5.261 operações, `active_count=0` | Refutou hang do executor de paginação |
| **ETW Trace** | Inexistente | 268 MB Scheduler + 134 MB Graphics | 118.027 eventos capturados sem perdas |
| **Monitor EDID** | Não medido | Capturado: PreBind (UID0 OK), Bound (UID1 vazio) | Identificou incompatibilidade de topologia |
| **Rollback pós-reset** | BasicDisplay Código 0 (boot 2) | BasicDisplay Código 0 (boot 2) | Recuperação automática R1 confirmada |

---

## 4. Próximos Passos Obrigatórios

1. **Alinhamento de Topologia do Monitor (P0):**
   - Atualizar `Bc250DxgkDdiQueryChildRelations` e a topologia VidPn para utilizar `ChildUid = 0` (compatível com o target `UID0` do GOP e com o registro PnP de `DISPLAY\GSM5B98`).
   - Habilitar `Bc250DisplayGetSyntheticEdidEnabled()` como fallback para que, mesmo se o Windows não encontrar cache, o KMD entregue os 128 bytes válidos de EDID na DDI `QueryDeviceDescriptor`.
2. **Sincronização de Paging Fence em Dispositivos Múltiplos (P0):**
   - Investigar por que a paging fence 7004 do dispositivo de Explorer ficou pendente sem sinalização enquanto a do DWM avançava.
