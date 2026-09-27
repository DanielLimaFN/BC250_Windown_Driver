# Passagem de bastão — WDDM acelerado na BC-250 (Candidato 3.112.1.0)

> **Data:** 2026-09-09 14:20 UTC-3  
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Pacote:** Candidato `3.112.1.0` (Lab Bring-up com RenderNode=1 e Contrato WDDM 2.7)  
> **Status:** Compilado, auditado, assinado e **NÃO INSTALADO** nesta sessão.  
> **Regra 10:** A execução física é prerrogativa e decisão exclusiva do operador humano (Daniel).

---

## 1. Diagnóstico do Stall RCIU (`MEC2_WAIT_ON_RCIU_READ`) e Solução Completa

Na corrida física do candidato 3.111.1.0 (`docs/evidence/2026-09-09_13-24-58-DESKTOP-HQAG6T4-lab-binding`), a instrumentação comprovou no silício:
- `SPI_PG_ENABLE_STATIC_WGP_MASK = 0x1F` e `RLC_PG_ALWAYS_ON_WGP_MASK = 0x1F` foram configurados com sucesso em todos os 4 Shader Arrays.
- O CP consumiu a doorbell física (avançando `WPTR` de 204 para 216).
- Contudo, `RPTR` estagnou em 204, `CP_HQD_IB_CONTROL` permaneceu `0x0` (`PROCESSING_IB = 0`), e `CP_CPC_STALLED_STAT1 = 0x00040000` (`MEC2_WAIT_ON_RCIU_READ`).

### Análise de Código Upstream Linux (`amdgpu` GFX10 / Cyan Skillfish 2) e Mesa/RADV
A investigação nos patches de suporte da BC-250 (`referencias/bc250-gfx1013-fix`) e na arquitetura GFX10 revelou três bloqueadores interdependentes:

1. **Master Power-Gating do RLC (`RLC_PG_CNTL = 0x0003B10Cu`)**:
   - `RLC_PG_CNTL` é o controlador mestre de power-gating do RLC. Quando habilitado pelo VBIOS do console, ele força transições de repouso no SMU/RCIU. Conforme catalogado no patch `0002-gfx1013-compute-gfxoff-guard.patch`, desligar o power-gating mestre (`RLC_PG_CNTL = 0`) é indispensável na APU BC-250 para evitar que o microcódigo do MEC congele na malha RCIU.
2. **Coerência de Memória com Invalidação de VMID Dupla (VMID 0 e VMID 9)**:
   - `Bc250SubmissionApplyCoherencyLocked` invalidava exclusivamente o `VMID 0` antes do kick de doorbell. O anel MEC opera sob Host PA / VMID 0, mas o Buffer de Comandos (IB) e as alocações de payload são traduzidas sob o `VMID 9`. Ao submeter no Nó 1, tanto `VMID 0` quanto `VMID 9` devem ser invalidadas no L2/TLB (`0x00F80201u`) e aguardar ACK (`0x00000201u`) antes do disparo.
3. **Identificador de Tipo de Shader no Pacote PM4 (`SHADER_TYPE_COMPUTE`)**:
   - O pacote PM4 Type 3 `WRITE_DATA` emitido pela UMD gerava cabeçalho `0xC0073700` com bit 1 (`SHADER_TYPE`) zerado (gráficos). Em anéis de computação assíncrona do MEC, pacotes com tipo gráficos não são executados pelas microengines de computação e bloqueiam o processador de comandos. Marcando o bit 1 (`BC250_PM4_SHADER_TYPE_COMPUTE`), o cabeçalho se torna `0xC0073702`, compatível com o MEC2.

---

## 2. O que foi Implementado no Candidato 3.112.0.0 / 3.112.1.0

### A. Desativação do Master RLC Power Gating (`bc250_active_miniport.c`)
- Em `Bc250ActivePrepareComputeVmid`, adicionada a escrita atômica `0u` em `BC250_HQD_MMIO_RLC_PG_CNTL` (`0x0003B10Cu`) antes do loop de WGP static dispatch.
- Readback de confirmação adicionado ao log do miniport:
  `[SubmitVirtual Node1] HQD_VMID: Target=0x... Readback=0x... WGP_PG: SPI=0x1F RLC=0x1F RLC_PG_CNTL=0x00000000`
- Incluída leitura e registro de `RLC_PG_CNTL` na rotina de amostragem `Bc250ActiveCaptureMecTelemetry` (`[_CPF]`).

### B. Invalidação Coerente de Ambos os VMIDs (0 e 9) (`bc250_submission_engine.c`)
- Em `Bc250SubmissionApplyCoherencyLocked`:
  - `reqValue = BC250_HQD_GCVM_INVALIDATE_REQ_VMID0 | (1u << Engine->ComputeRing.Vmid)` (`0x00F80201u`).
  - `ackMask = BC250_HQD_GCVM_INVALIDATE_VMID0 | (1u << Engine->ComputeRing.Vmid)` (`0x00000201u`).
  - Garantia de que tanto o anel persistente (Host PA) quanto o IB/dados (VMID 9) estejam completamente sincronizados no cache L2 e na TLB do GFXHUB antes do avanço do cursor e do toque da doorbell.

### C. Cabeçalho PM4 com `SHADER_TYPE_COMPUTE` (`bc250_d3d_umd.c`)
- Em `Bc250UmdBuildNode1ProofCommand`:
  - `cmd->pCpuVa[initialCursor] |= BC250_PM4_SHADER_TYPE_COMPUTE;`
  - Pacote `WRITE_DATA` emitido passa a ser `0xC0073702`.
  - Atualizado `bc250_d3d_umd_test.c` com asserção estrita do cabeçalho `0xC0073702`.

### D. Ampliação da Allowlist MMIO e Testes de Contrato
- Adicionado `BC250_HQD_MMIO_RLC_PG_CNTL` nas allowlists de leitura e escrita de `bc250_mmio_topology_transport.c`.
- Atualizado e validado `driver/tests/mmio_topology_transport_test.c` (ReadCount = 146, WriteCount = 54).
- Suíte contratual (`driver\wdk\build-contract.bat`) e suíte UMD (`driver\umd\build-umd.bat`) passaram com `EXIT=0`.
- Auditorias semânticas de segurança (`audit-bc250mcdm-safety.py` e `audit-umd-node1-submission.py`) passaram com `PASS`.
- **Salvaguarda do Harvest de 24 CUs**: `CC_GC_SHADER_ARRAY_CONFIG` (`0x089BC`) **permanece estritamente somente leitura**, garantindo que os fusíveis de fábrica da placa nunca sejam tocados.

---

## 3. Identidade Criptográfica do Pacote 3.112.1.0

Localização: `driver/wdk/package/output_lab`  
Manifesto: `driver/wdk/package/output_lab.manifest.json`

| Componente | SHA-256 |
| :--- | :--- |
| **SYS de laboratório** (`bc250mcdm.sys`) | `1489F7AE873A6EE6675AB0B6705A2B170C07C1535AAB3052FD153B0BF39ED339` |
| **UMD de laboratório** (`bc250umd.dll`) | `AC7760BAFF0017FE3751FED6EAE48679ED443946ACE265C2B4C17E9EBDE9F7E0` |
| **INF de laboratório** (`bc250mcdm.inf`) | `97D506E4824B6B9B7E4D952A0BB4F5411302800444EB0E0F40D4C9A4C72D209A` |
| **Catálogo assinado** (`bc250mcdm.cat`) | `30FAD349EF15EF96A6596679E82C57194B954C5E6CA47409C72AF5F68FF985A5` |
| **DriverVer** | `09/09/2026,3.112.1.0` |
| **Perfil** | `DISPLAY_RENDER_LAB` (`RenderNode=1`, `RenderOnlyLab=0`) |
| **WddmContractVersion** | `27` (`WDDM 2.7`) |

---

## 4. Instruções para o Próximo Ensaio Físico

> [!IMPORTANT]
> O fusível de boot (`tools/inspect-boot-fuse.ps1`) está em estado **CLEAN (0)**.
> Como houve desligamento limpo sem TDR pendente, não é obrigatório boot frio, mas para máxima fidelidade do estado das microengines da GPU, recomenda-se iniciar o teste com o sistema em repouso.

Execução do ensaio de laboratório elevado:
```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod
```
