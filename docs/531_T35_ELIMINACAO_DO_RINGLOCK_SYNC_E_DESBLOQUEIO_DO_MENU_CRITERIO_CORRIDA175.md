# 531 — T3.5: Eliminação do RingLock no Polling Síncrono do SDMA e Proteção contra I/O de Disco sob Locks; Critério da Corrida 175

> 29/09/2026, offline. Candidato **3.320.1.0** (inclui T3.5, T3.4, T3.3, HDP.1, T3.1, EVT.1, X1, X2a, X3 e X4). **Nada desta faixa foi validado em hardware.**

---

## 1. Por quê

A análise detalhada da Corrida 174 (doc 530) e a auditoria cruzada do código de submissão e apresentação revelaram:

1. **Avanço comprovado do T3.4 na Corrida 174:**
   - 18.743 chamadas de `FLUSH_TLB` executadas com sucesso imediato (~6 µs por chamada), sem nenhum travamento na CPU10.
   - O processo do Menu Iniciar (`StartMenuExperienceHost.exe`) foi instanciado com sucesso, carregou a UMD e o backend Vulkan, compilou seus pipelines sem erros, executou 261 draw calls e apresentou 9 quadros válidos (`hr=0x00000000`) com pixels comprovados na grade.

2. **Identificação da causa do congelamento aos 81,4 s:**
   - Ao abrir o Menu Iniciar, o Windows criou uma superfície de paginação de 64 KB e invocou `[DX91_PAGING_MOVE] op=FILL`.
   - Concomitantemente, o DWM chamou `DxgkDdiPresent` / `Blt` (`Bc250SdmaPresentBltCore`).
   - Na implementação anterior de `driver/wdk/bc250_sdma_present.c`, o spinlock `RingLock` era retido **durante todo o laço de polling** (até 10.000 iterações = 50 ms a `DISPATCH_LEVEL`) nas funções síncronas.
   - Ao mesmo tempo, a rotina de paginação/submissão na CPU13 tentava submeter o DMA buffer e precisava de `RingLock` para registrar ou aposentar pacotes (`Bc250ActiveSdmaRetireAndNotify`), gerando contenção massiva de barramento e travamento mútuo entre a thread do DWM e a thread do VidMm.
   - Adicionalmente, `Bc250DisplayLog` executava operações síncronas de I/O em disco (`ZwCreateFile`/`ZwFlushBuffersFile`), o que é estritamente proibido quando APCs estão desabilitadas ou quando pushlocks do kernel estão mantidos.

---

## 2. A mudança (Faixa T3.5)

1. **Redução Estrita do Escopo de `RingLock` em `bc250_sdma_present.c`:**
   - O spinlock `RingLock` agora é liberado **imediatamente após a escrita do ponteiro WPTR MMIO** nas 5 funções síncronas:
     - `Bc250SdmaPresentBltCore`
     - `Bc250SdmaPresentBltRectCore`
     - `Bc250SdmaPresentBltScanoutSub`
     - `Bc250SdmaPresentSyncNop`
     - `Bc250SdmaPresentIndirect`
   - O laço de polling de conclusão (`for (poll = 1u; poll <= 10000u; ++poll)`) agora roda **completamente fora do `RingLock`**, alinhando-se exatamente à arquitetura do driver Linux upstream (`drivers/gpu/drm/amd/amdgpu/sdma_v5_0.c`). Isso permite que submissões assíncronas do VidMm continuem fluindo em paralelo sem qualquer espera no anel.

2. **Proteção contra I/O de Disco sob Locks em `bc250_display_ddi.c`:**
   - Em `Bc250DisplayLog`, adicionada verificação de `KeAreApcsDisabled()`. Se as APCs do kernel estiverem desabilitadas (como dentro de regiões críticas de pushlocks ou spinlocks), o log é desviado automaticamente para o ring buffer em RAM, bloqueando chamadas a `ZwCreateFile` / `ZwFlushBuffersFile`.

3. **Pushlocks Compartilhados em `Bc250ActiveResolveDisplayGpuVa`:**
   - Convertida a aquisição de `g_Bc250AdapterDirectoryLock` e `active->ObjectLock` de Exclusivo para Compartilhado (`ExAcquirePushLockSharedEx`), eliminando a serialização desnecessária entre threads de render e paginação durante a resolução de GPUVA.

4. **Migalhas Granulares na Submissão (`bc250_active_miniport.c`):**
   - Adicionadas as etapas `BC250_SUBMIT_STAGE_DEST_WALK (0x15)`, `BC250_SUBMIT_STAGE_PATH_SELECT (0x1A)` e `BC250_SUBMIT_STAGE_PRE_RETIRE (0x1B)` nas migalhas persistentes da VRAM, permitindo discriminação cirúrgica no caso de qualquer interrupção durante a paginação.

---

## 3. Provas offline

- Suíte de contratos completa (`driver/wdk/build-contract.bat`): **EXIT 0** (105 DDIs, 0 falhas).
- Compilação do pacote de produção (`build-bc250mcdm-package.ps1`): **EXIT 0** (DriverVer `3.320.0.0`).
- Compilação de laboratório (`build-bc250mcdm-lab-bringup.ps1`): **EXIT 0**.
- Empacotamento de laboratório (`package-bc250mcdm-lab.ps1`): **EXIT 0** (DriverVer `3.320.1.0`).
- Auditoria de segurança (`audit-bc250mcdm-safety.py`): **PASS**.
- Auditoria de empacotamento (`audit-bc250mcdm-lab-package.ps1`): **PASS**.
- Script de preflight (`prepare-corrida175-t35.ps1`): **EXIT 0**.

---

## 4. Candidato

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `C9500445737D25C11A2F1E0072940D7E91C1E0E7C10435097A5B33D525264050` |
| `bc250umd.dll` (idêntica à 3.310) | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `B8F46688ED52AC3DD8C723BB0049D36A556CB9DB82999B1796EB32A148445F75` |
| `bc250mcdm.cat` | `B737D5B35A9DE28D270F0EB117D3C55F7A832B18845149C0D9E9B4F9E017DB20` |

- DriverVer: `09/29/2026,3.320.1.0` (fonte `3.320.0.0`).
- Runner: `tools/EXECUTAR_CORRIDA175_T35_600S.cmd`.

---

## 5. Critério da Corrida 175 (escrito antes; Lei 10)

**Roteiro:** O mesmo da Corrida 174 (atividade mínima, sem harness). Assim que o desktop estabilizar (~20 s), clicar para abrir e fechar o menu Iniciar repetidas vezes (para exercitar alocações dinâmicas e o paralelismo entre apresentação do DWM e submissão assíncrona do VidMm).

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | **600 s sem trava** com menu Iniciar operando e abrindo/fechando repetidamente, Presents contínuos e sem congelamento | **Sucesso completo do paralelismo SDMA/VidMm:** o desacoplamento de `RingLock` e a proteção de pushlocks eliminaram o gargalo de concorrência. Avança imediatamente para os testes com aplicações 3D / jogos. |
| **B** | Trava em ponto novo de submissão identificado pelas novas migalhas (0x15, 0x1A, 0x1B) | Identificar se a contenção remanescente decorre de fence de hardware ou ciclo de DMA buffer. |
| **C** | Regressão de sincronização no Present (deslocamento de frame ou artefato visual) | O polling sem `RingLock` revelou dependência não sincronizada entre passes consecutivos do DWM. Ajustar barreira de memória. |
| **E** | Fora do mapa (BugCheck com dump de kernel) | Analisar arquivo de dump `.dmp` gerado para obter callstack exata. |
