# Passagem de bastão — WDDM acelerado na BC-250 (Candidato 3.113.1.0)

> **Data:** 2026-09-09 15:10 UTC-3  
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Pacote:** Candidato `3.113.1.0` (Lab Bring-up com RenderNode=1 e Contrato WDDM 2.7)  
> **Status:** Compilado, auditado, assinado e **NÃO INSTALADO** nesta sessão.  
> **Regra 10:** A execução física é prerrogativa e decisão exclusiva do operador humano (Daniel).

---

## 1. Diagnóstico da Estagnação do Command Packet Fetcher (`RPTR = 204`) e Solução

Na corrida física do candidato 3.112.1.0 (`docs/evidence/2026-09-09_14-28-15-DESKTOP-HQAG6T4-lab-binding`), a telemetria comprovou no silício:
- O bloqueio de RCIU foi **100% extinto**: `CP_CPC_STALLED_STAT1 = 0x00000000` e `CP_CPF_STALLED_STAT1 = 0x00000000`. Desligar o power-gating mestre (`RLC_PG_CNTL = 0`) e manter estáticos os WGPs liberou a malha interna do microcódigo do MEC.
- O SDMA0 continuou perfeito: executou 10 IBs indiretos sob VMID 8 com zero faults (`TRIPE_PROVADO #1..#10`).
- A UMD criou o dispositivo nativo D3D10 (`Interface = 0x000A0002`) e o buffer de comandos do Nó 1.
- A tradução de GPUVA do IB foi confirmada pelo caminhamento de páginas do KMD (`VA=0x11000 -> PA=0x451FCC000` sob VMID 9).
- A doorbell física foi consumida e o cursor de escrita avançou (`P1LastWptrMmioReadback = 216`).

Contudo, `P1LastRptrMmio` estagnou em **204** e `CP_CPF_BUSY_STAT = 0x68460000` permaneceu ocupado:
- Decodificação dos bits de `CP_CPF_BUSY_STAT`:
  - Bit 30: `HQD_PQ_BUSY` (1)
  - Bit 29: `HQD_WPTR_POLL_BUSY` (1)
  - Bit 27: `HQD_ROQ_PQ_BUSY` (1)
  - Bit 22: `HQD_CONSUMED_RPTR_BUSY` (1)
  - Bit 18: `HQD_PQ_FETCHER_BUSY` (1)
  - Bit 17: `HQD_MESSAGE_BUSY` (1)
  - **Bit 19 (`HQD_IB_FETCHER_BUSY`) = 0**: O CP sequer chegou a buscar o Buffer Indireto (`PROCESSING_IB = 0`).

### Causa-Raiz Identificada: Falta do Dreno HDP na CPU (`0x04A80`)
1. As escritas da CPU no anel persistente em VRAM (`Engine->ComputeRingVirtual[204..215]`) passam pela abertura PCI e pela FIFO do **Host Data Path (HDP)** da GPU.
2. O processador de comandos (CP Command Packet Fetcher) lê a memória do anel em VRAM através da malha GFXHUB.
3. `Bc250SubmissionApplyCoherencyLocked` invalidava unicamente o cache de leitura do HDP (`0x03FC4`) e o cache L2/TLB, mas **não acionava o flush das filas de escrita da CPU no HDP** (`0x04A80`).
4. Sem o dreno de escrita no HDP, o fetcher do CP buscava dados obsoletos ou parcialmente gravados na VRAM física no índice 204, mantendo `HQD_PQ_FETCHER_BUSY` travado.
5. Nas rotinas provadas no silício para SDMA (`Bc250SdmaHdp`) e compute de teste (`Bc250ComputeHdpFlush`), o flush exige uma sequência atômica de 5 passos envolvendo `HDP_MISC` (`0x03FCC`), `HDP_FLUSH` (`0x04A80`) e `HDP_READ_CACHE_INVALIDATE` (`0x03FC4`).

---

## 2. O que foi Implementado no Candidato 3.113.0.0 / 3.113.1.0

### A. Transação Atômica Completa de Flush e Invalidação HDP (`bc250_submission_engine.c`)
Substituída a escrita isolada em `0x03FC4` pela transação exata de 5 passos provada no silício:
```c
uint32_t hdpMisc = 0u;
uint32_t hdpDummy = 0u;

if (!Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_HDP_MISC, &hdpMisc) ||
    !Bc250SubmissionWrite32(Engine, Context,
        BC250_HQD_MMIO_HDP_FLUSH, 0u) ||
    !Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_HDP_FLUSH, &hdpDummy) ||
    !Bc250SubmissionWrite32(Engine, Context,
        BC250_HQD_MMIO_HDP_READ_CACHE_INVALIDATE, 1u) ||
    !Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_HDP_READ_CACHE_INVALIDATE, &hdpDummy) ||
    !Bc250SubmissionWrite32(Engine, Context,
        BC250_HQD_MMIO_HDP_MISC, hdpMisc | 1u) ||
    !Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_HDP_MISC, &hdpDummy) ||
    !Bc250SubmissionWrite32(Engine, Context,
        BC250_HQD_MMIO_HDP_MISC, hdpMisc & ~1u) ||
    !Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_HDP_MISC, &hdpDummy)) {
    return STATUS_DEVICE_HARDWARE_ERROR;
}
Engine->CoherHdpApplied = 1u;
```
- A escrita de `0u` em `0x04A80` e seu readback imediato garantem que todas as escritas pendentes do host no barramento PCIe sejam completamente comitadas na VRAM física antes que a HQD seja notificada.

### B. Barreira Explícita de Memória (`KeMemoryBarrier`)
- Adicionada barreira de memória de armazenamento no processador (`KeMemoryBarrier()`) em `Bc250SubmissionEngineSubmit` imediatamente após o commit das 12 dwords no anel persistente e antes de invocar `Bc250SubmissionPublishWptr`.

### C. Definições MMIO no Contrato de MQD (`bc250_compute_mqd_contract.h`)
- Definidos formalmente `BC250_HQD_MMIO_HDP_MISC` (`0x00003FCCu`) e `BC250_HQD_MMIO_HDP_FLUSH` (`0x00004A80u`), ambos já permitidos pelas allowlists estritas de MMIO do miniport (`bc250_mmio_topology_transport.c`).

### D. Validação Contratual e Auditorias
- `build-contract.bat`: **EXIT=0** (todos os 45 contratos e testes unitários offline aprovados).
- `build-umd.bat`: **EXIT=0** (todos os harnesses D3D, Vulkan e Nó 1 aprovados).
- `audit-bc250mcdm-safety.py`: **PASS**.
- `audit-umd-node1-submission.py`: **PASS**.
- **Salvaguarda do Harvest de 24 CUs**: `CC_GC_SHADER_ARRAY_CONFIG` (`0x089BC`) **permanece estritamente somente leitura**, garantindo que os fusíveis de fábrica da placa nunca sejam tocados.

---

## 3. Identidade Criptográfica do Pacote 3.113.1.0

Localização: `driver/wdk/package/output_lab`  
Manifesto: `driver/wdk/package/output_lab.manifest.json`

| Componente | SHA-256 |
| :--- | :--- |
| **SYS de laboratório** (`bc250mcdm.sys`) | `AD3B92E60E0253426C11EC3473C9DB501F3C9AA4DE25C1F53E24D3B493D5C84C` |
| **UMD de laboratório** (`bc250umd.dll`) | `6261CE3D2DBD3244AF0B882DEDABC94AC001CBDC52F146F0F2216706C8750682` |
| **INF de laboratório** (`bc250mcdm.inf`) | `11F2FC7DC6B51FE55B3B38F93BA77B53E9A3561DD93F5E34E0924C7939D489AF` |
| **Catálogo assinado** (`bc250mcdm.cat`) | `9DBCA34875179183D85B62EAD17BDC01E52C2E47874C0EE7382EB0558D430B16` |
| **DriverVer** | `09/09/2026,3.113.1.0` |
| **Perfil** | `DISPLAY_RENDER_LAB` (`RenderNode=1`, `RenderOnlyLab=0`) |
| **WddmContractVersion** | `27` (`WDDM 2.7`) |

---

## 4. Instruções para o Próximo Ensaio Físico

> [!IMPORTANT]
> O fusível de boot (`tools/inspect-boot-fuse.ps1`) está em estado **CLEAN (0)**.
> O watchdog anterior realizou rollback limpo e o sistema está pronto para execução direta.

Execução do ensaio de laboratório elevado:
```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod
```
