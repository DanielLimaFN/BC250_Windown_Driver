# Passagem de bastão — WDDM acelerado na BC-250 (Candidato 3.111.1.0)

> **Data:** 2026-09-09 13:20 UTC-3  
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Pacote:** Candidato `3.111.1.0` (Lab Bring-up com RenderNode=1 e Contrato WDDM 2.7)  
> **Status:** Compilado, auditado, assinado e **NÃO INSTALADO** nesta sessão.  
> **Regra 10:** A execução física é prerrogativa e decisão exclusiva do operador humano (Daniel).

---

## 1. Diagnóstico do Stall RCIU (`MEC2_WAIT_ON_RCIU_READ`) e Solução de Silício

Na corrida física dos candidatos 3.109.1.0 e 3.110.1.0, o 1º submit Node 1 avançou `WPTR` de 204 para 216 e o TDR capturou:
`CP_CPC_STALLED_STAT1 = 0x00040000` (`MEC2_WAIT_ON_RCIU_READ`).

### Análise da Correção Cyan Skillfish 2 no Linux (`referencias/bc250-gfx1013-fix`)
1. No silício da APU BC-250 (`1002:13FE`, GC 10.1.3), `RLC_PG_ALWAYS_ON_WGP_MASK` (`0x3B14C`) inicia por padrão de hardware/VBIOS em `0x00000003` (apenas WGPs 0 e 1 mantidos ligados; WGPs 2, 3 e 4 ficam em power-gating).
2. Quando o MEC recebe uma submissão de usuário ou tenta despachar ondas/ler contadores através da malha RCIU, a falta de confirmação dos WGPs adormecidos causa o travamento permanente do CP (`MEC2_WAIT_ON_RCIU_READ`).
3. Para mantê-los despertos e ativos, o driver precisa programar `0x1F` (5 WGPs ativos por Shader Array) em:
   - `SPI_PG_ENABLE_STATIC_WGP_MASK` (`0x0935C`): habilita despacho estático para os WGPs.
   - `RLC_PG_ALWAYS_ON_WGP_MASK` (`0x3B14C`): mantém os WGPs energizados e impede gating durante despachos.
4. Essa escrita precisa ser feita em todos os 4 Shader Arrays (SE 0..1, SH 0..1) via comutação em `GRBM_GFX_INDEX` (`0x00030800`), e finalizada com escrita em modo broadcast (`0xE0000000`).
5. **Salvaguarda do Harvest de 24 CUs**: `CC_GC_SHADER_ARRAY_CONFIG` (`0x089BC`) **permanece estritamente somente leitura**, garantindo que a máscara física de fusíveis original da placa nunca seja alterada.

---

## 2. O que foi Implementado no Candidato 3.111.0.0 / 3.111.1.0

### A. Programação de WGP Keep-Alive no Nó 1 (`bc250_active_miniport.c`)
- Em `Bc250ActivePrepareComputeVmid`, adicionada a programação atômica sob spinlock:
  - Loop por `SE 0..1` e `SH 0..1` via `GRBM_GFX_INDEX`.
  - Escrita de `0x1F` em `BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK` e `BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK`.
  - Restauração de `GRBM_GFX_INDEX` para broadcast (`0xE0000000`).
  - Escrita de broadcast `0x1F` e readback com log explícito:
    `[SubmitVirtual Node1] HQD_VMID: Target=0x00000900 Readback=0x00000900 WGP_PG: SPI=0x0000001F RLC=0x0000001F`

### B. Ampliação da Allowlist MMIO (`bc250_mmio_topology_transport.c` e contratos)
- Adicionado `BC250_HQD_MMIO_SMUIO_GFX_MISC_CNTL` (`0x0005A314u`) em `driver/core/bc250_compute_mqd_contract.h`.
- Adicionado `BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK` e `BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK` na allowlist de escrita de topologia MMIO.
- Atualizado e validado `driver/tests/mmio_topology_transport_test.c` (todas as 35+ suítes com `EXIT=0`).

### C. Telemetria Síncrona Estendida (`Bc250ActiveCaptureMecTelemetry`)
- Amostragem e exibição no log de `SPI_PG_STATIC_WGP_MASK`, `RLC_PG_ALWAYS_ON_WGP_MASK` e `SMUIO_GFX_MISC_CNTL` na linha `[_CPF]`.

### D. UMD WRITE_DATA sem Wait Síncrono
- Verificado em `driver/umd/bc250_d3d_umd.c` que `Bc250UmdBuildNode1ProofCommand` utiliza `writeControl = BC250_PM4_WD_ENGINE_ME | BC250_PM4_WD_DST_MEM` (`0x00000500`), sem o bit síncrono `WR_CONFIRM` (`0x00100500`), confiando no `RELEASE_MEM` subsequente com fence EOP para sincronização de memória.

---

## 3. Identidade Criptográfica do Pacote 3.111.1.0

Localização: `driver/wdk/package/output_lab`  
Manifesto: `driver/wdk/package/output_lab.manifest.json`

| Componente | SHA-256 |
| :--- | :--- |
| **SYS de laboratório** (`bc250mcdm.sys`) | `FBE6C1E2EDA08A4E8B11AFEFA8A82604C9158A52375DB905D4406D97E5B74252` |
| **UMD de laboratório** (`bc250umd.dll`) | `902B6B8AACF61540A81AC3E5360EAEE56A8D23613F4F05ADCDFD0CF48BD905B3` |
| **INF de laboratório** (`bc250mcdm.inf`) | `FCF905EBEEAF77423D16EEB145781031B9AB939D0CB93F8FAC64E8C4EE05ECB7` |
| **Catálogo assinado** (`bc250mcdm.cat`) | `74C7610A07B4101A819B8C6E59FEF3E1D114CC786A0B4CE32514A16AF783BF55` |
| **DriverVer** | `09/09/2026,3.111.1.0` |
| **Perfil** | `DISPLAY_RENDER_LAB` (`RenderNode=1`, `RenderOnlyLab=0`) |
| **WddmContractVersion** | `27` (`WDDM 2.7`) |

---

## 4. Instruções para o Próximo Ensaio Físico

> [!IMPORTANT]
> O rollback anterior gravou `C:\BC250_REQUIRE_CLEAN_BOOT.json`. Por segurança do silício e das engines de hardware, o binder exige **reiniciar a máquina** (boot limpo com `BasicDisplay`) antes de realizar o novo bind.

Após reiniciar a máquina:
```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod
```
