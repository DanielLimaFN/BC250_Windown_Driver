# Conclusão das Fases 0, 1 e 2 — Implementação do UMD Direct3D Fail-Closed e Saneamento do Contrato WDDM

**Data:** 2026-09-02  
**Status de Hardware:** `NAO VALIDADO EM HARDWARE` (Camada de software e contratos 100% aprovados offline)  
**Documento de Referência:** `docs/161_PLANO_MESTRE_POS_GATE_1_5.md` e `docs/162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md`

---

## 1. Contexto e Motivação

Após a reprovação definitiva do Gate 1.5 Render-Only no Candidato 3.80.0.0 (Doc 162), a causa-raiz da recusa imediata do `dxgkrnl` pós-`StartDevice` com Código 43 foi identificada:
1. **Contrato de Classe Display (`Class=Display`)**: O Windows exige que todo adaptador registrado sob a classe Display declare seus binários de User-Mode Driver (UMD) no registro através da seção `.HW` / `.SoftwareSettings` (`InstalledDisplayDrivers`, `UserModeDriverName`, `UserModeDriverNameWow`).
2. **Ausência Histórica do UMD**: O pacote `bc250mcdm` não possuía declaração de UMD nem exportava o binário UMD D3D correspondente (`bc250umd.dll`), fazendo com que o subsistema gráfico do Windows finalizasse e desmontasse o adaptador logo após `StartDevice`.

---

## 2. Entregas Realizadas

### Fase 0: Higiene do Kernel e Estancamento de Handle Leak
- **Correção em `bc250_probe_telemetry.c`**: O `ZwClose(key)` foi movido para fora de condicionais redundantes, eliminando risco de leak de handles de registro durante o boot e transações PnP.
- **Auditoria de Enums**: 100% dos membros de `BC250_PROBE_FIELD` foram mapeados com nomes únicos de telemetria.

### Fase 1: Diagnóstico e Ajustes Estruturais de INF
- **Diff com Drivers WDDM Reais**: Comparado o INF de referência (`rdvgwddmdx11.inf`).
- **Seção `.HW` e Segurança**: Inserida a chave de descritores de segurança `[BC250Mcdm_Device.NT.HW]`.
- **Seção `SoftwareDeviceSettings`**: Mapeadas as chaves de registro do UMD:
  - `InstalledDisplayDrivers = "bc250umd"`
  - `UserModeDriverName = "bc250umd.dll"`
  - `UserModeDriverNameWow = "bc250umd.dll"`
- **Destino do Binário UMD**: `bc250umd.dll` direcionada para `%SystemRoot%\System32` (`DIRID 11`).

### Fase 2: Implementação do User-Mode Driver D3D (Plano A)
- **Header**: [`driver/umd/bc250_d3d_umd.h`](../driver/umd/bc250_d3d_umd.h)
- **Implementação**: [`driver/umd/bc250_d3d_umd.c`](../driver/umd/bc250_d3d_umd.c)
  - `OpenAdapter10_2`, `OpenAdapter10`, `OpenAdapter11` e `OpenAdapter12`.
  - Tabelas `D3D10_2DDI_ADAPTERFUNCS` e `D3D12DDI_ADAPTERFUNCS`.
  - `pfnGetSupportedVersions` retorna `D3D10_0_DDI_SUPPORTED`, `D3D10_1_DDI_SUPPORTED`, `D3D11_0_DDI_SUPPORTED` e `D3D11_1_DDI_SUPPORTED`.
  - `pfnGetCaps` limpa/preenche capabilities com zero (sem suporte a aceleração 3D sintética).
  - `pfnCreateDevice` retorna **`E_NOTIMPL`** (*fail-closed*), impedindo crashes de processo em chamadas a ponteiros não implementados e forçando fallback gracioso de software em apps D3D.
- **Exportações**: [`driver/umd/bc250umd.def`](../driver/umd/bc250umd.def).
- **Harness de Testes**: [`driver/tests/bc250_d3d_umd_test.c`](../driver/tests/bc250_d3d_umd_test.c).

---

## 3. Matriz de Resultados dos Testes Offline

```text
======================================================================
1. Suite do Kernel (driver/wdk/build-contract.bat):
   - 37 testes unitarios e contratos de DDI executados e APROVADOS (0 falhas)
   - DDI Matrix: PASS (Structural)
   - Safety Audit: PASS

2. Suite UMD (driver/umd/build-umd.bat):
   - UMD Runtime Contract: 21/21 invariantes validadas
   - D3D UMD Contract (bc250_d3d_umd_test.exe): PASS
   - Compilacao limpa /W4 /WX para bc250umd.dll, bc250_runtime.dll e bc250_vulkan.dll

3. Empacotador e Assinador Authenticode (tools/build-bc250mcdm-package.ps1):
   - bc250umd.dll: Assinado com SHA-256 e validado
   - bc250mcdm.sys: Assinado com SHA-256 e validado
   - bc250mcdm.cat: Gerado via Inf2Cat e assinado
   - Manifesto de build gerado em output_active
======================================================================
```

---

## 4. Próxima Etapa no Roadmap

Transição para a **Fase 3 (Vulkan ICD Integration & Harness Compute)**:
1. Conectar a descoberta do `bc250_vulkan.dll` pelo Vulkan Loader via `VulkanDriverName` ou manifesto JSON (`bc250_icd.json`).
2. Validar a submissão de kernels de inferência LLM através do runtime compute.
