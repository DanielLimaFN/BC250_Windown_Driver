# Passagem de Bastão: Corrida 13/09/2026 16:28:27 (Candidato 3.172.0.0)

> **Data/Hora da Corrida:** 2026-09-13 16:28:27  
> **Pasta de Evidências:** `docs/evidence/2026-09-13_16-28-27-DESKTOP-HQAG6T4-lab-binding/`  
> **Status:** Sem tela azul (BugCheck 0x10E eliminado). Rollback online limpo pelo Watchdog (RECOVERY_OK_ONLINE) após 180s. BasicDisplay restaurado em Código 0.

---

## 1. O que foi Conquistado nesta Sessão

1. **BugCheck 0x10E Totalmente Eliminado**:
   - A causa raiz (`FlagsWddm2.CpuVisible = 1u` indiscriminado em `bc250_active_miniport.c`) foi corrigida.
   - Recursos de render/composição agora recebem `CpuVisible = 0u`, retirando-os do `VIDMM_RECYCLE_HEAP_MGR` do `dxgmms2.sys`.
   - **Resultado no silício:** Zero BugChecks, zero falhas de kernel, zero reboots durante toda a corrida de 180 segundos.
2. **Carga Massiva no Hardware com Estabilidade Absoluta**:
   - **3.757 submissões indiretas de paginação no Node 0 (SDMA)** sob VMID 8, todas com `TRIPE_PROVADO` (fences conferidas, canários 5/5, divergências=0).
   - **5.165 operações de Escape / Composição (`COMPOSITE_BLT`)** executadas pelo DWM no hardware via SDMA.
   - **1.740 cópias retangulares (`BLT_RECT_ENGINE_2LEGS`)** concluídas com sucesso.
   - **Zero timeouts, zero TDR, zero falhas de GPU.**
3. **Rollback Seguro Validado**:
   - O watchdog expirou aos 180s de observação, executou o rollback limpo sem reboot (`RECOVERY_OK_ONLINE`) e restaurou `BasicDisplay` em Código 0 em 5 amostras consecutivas.

---

## 2. Diagnóstico da Tela Preta Atual

- A composição do DWM via SDMA está operando intensamente: o DWM está gerando milhares de quadros compostos em seu backbuffer offscreen (`VA=0xF0000 -> Phys=0x1600000`).
- Porém, esses quadros compostos ainda **não estão sendo transferidos para o scanout primário do monitor** (endereço do UEFI GOP em `0xAAC000` / `0x0`).
- A cadeia de apresentação precisa garantir que o backbuffer composto pelo DWM seja transferido (via Blt de scanout ou flip de endereço) para a superfície primária conectada ao display do GOP.

---

## 3. Próximo Trabalho para a Próxima IA

1. **Investigar o caminho de Present do DWM para o Scanout**:
   - Na UMD ([`driver/umd/bc250_d3d_umd.c`](../driver/umd/bc250_d3d_umd.c)): verificar [`Bc250DxgiPresent`](../driver/umd/bc250_d3d_umd.c#L3247) e assegurar que o backbuffer composto seja enviado ao `pfnPresentCb` / escape do KMD para cópia final no scanout GOP.
   - No KMD ([`driver/wdk/bc250_display_ddi.c`](../driver/wdk/bc250_display_ddi.c)): verificar a conexão entre o buffer recebido em `DxgkDdiPresent` e o `ScanoutGpuAddress` (`g_Bc250SdmaPresent.ScanoutGpuAddress`).
2. **Manter as Leis e Contratos**:
   - Preservar o fail-closed e a honestidade de prova (Lei 4).
   - Rodar sempre `driver/build-contract.bat`, `driver/umd/build-umd.bat` e `tools/audit-bc250mcdm-safety.py` antes de novos pacotes.
