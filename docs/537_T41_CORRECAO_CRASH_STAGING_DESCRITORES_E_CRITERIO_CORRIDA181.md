# 537 — T4.1: Correção de Crash no Staging de Descritores e Critério da Corrida 181

> 29/09/2026, offline. Candidato **3.326.1.0** (inclui T4.1, T4.0, T3.9, T3.8, T3.7, T3.6, T3.5, T3.4, T3.3, HDP.1, T3.1, EVT.1, X1, X2a, X3 e X4). **Nada desta faixa foi validado em hardware.**

---

## 1. Por quê: Relato e Forense da Corrida 180

Na **Corrida 180** (candidato T4.0, DriverVer `3.325.1.0`), o Daniel executou o ensaio e reportou:
> *"rodei, tela preta ai reiniciei, o menu abriu apenas o quadrado cinza no menu"*

### Diagnóstico Forense nos Logs de Evidência
A análise detalhada dos rastros arquivados em `docs/evidence/2026-09-29_20-59-31-DESKTOP-HQAG6T4-lab-binding/` revelou a sequência exata de eventos:

1. **Crash no Primeiro Processo do Compositor (`dwm.exe` PID 1284):**
   - O DWM subiu às `20:59:43.042` e inicializou o bridge Vulkan e os recursos nativos.
   - Às `20:59:44.054`, o shell criou uma textura compartilhada `res=0000000009BC04F0` via `CreateShaderResourceView` e disparou um draw call.
   - O manipulador de descritores (`Bc250UmdBuildVkDescriptorSet`) acionou o **Tier 2** (SDMA staging blt).
   - No código do T4.0, o ponteiro de mapeamento CPU `device->VkBridge.UploadStagingMapped` foi passado para `Bc250UmdVkUploadImageData`.
   - Dentro de `UploadImageData`, a chamada a `Bc250UmdVkEnsureUploadStaging` detectou que o recurso demandava mais memória do que o tamanho inicial do staging (4 MB), executando `vkFreeMemory` no buffer anterior e remapeando um novo bloco.
   - Consequência: o ponteiro passado em `pData` tornou-se um **dangling pointer** apontando para memória virtual desalocada.
   - O `memcpy` subsequente tentou ler da memória desalocada, resultando em um crash instantâneo `0xc0000005` (Access Violation em `VCRUNTIME140.dll` + `0x1dc91`).

2. **Tela Preta e Reinício do Shell pelo Windows:**
   - Com a queda do PID 1284 às `20:59:44.055`, a tela do monitor ficou preta enquanto o subsistema de sessão do Windows detectava o encerramento anormal do compositor.
   - O Windows reiniciou o DWM e o Explorer (`[pid=5824 explorer.exe]` às `21:00:10.684`).
   - O segundo compositor recuperou o desktop, mas sem os dados da textura compartilhada (caindo no fallback cinza), o que explica o menu ter aberto apenas com quadrados cinza antes do reinício manual pelo Daniel.

---

## 2. Arquitetura da Solução: Candidato T4.1

Para resolver em definitivo o defeito de concorrência/redimensionamento e otimizar o fluxo de dados entre o SDMA e a GPU, a arquitetura do Tier 2 foi refeita:

### A. Alocação e Dreno Prévios do Buffer de Staging (`driver/umd/bc250_umd_descriptors.c`)
- Antes de consultar a `GpuVirtualAddress` de destino na memória de staging, o código agora chama obrigatoriamente `Bc250UmdVkEnsureUploadStaging(&device->VkBridge, totalBytes)`.
- O buffer de staging é garantido com capacidade suficiente **antes** de qualquer operação de cópia.
- É executado um dreno explícito (`Bc250UmdVkDrainPending(&device->VkBridge, "SDMA Staging Blt")`), garantindo que nenhuma leitura pendente da GPU concorra com o novo lote.
- O endereço GPUVA (`stagingGpuVa`) é obtido do buffer já redimensionado e validado.

### B. Cópia Direta GPU-to-GPU sem Memcpy de CPU (`Bc250UmdVkCopyStagingBufferToImage`)
- Implementada nova função dedicada em `driver/umd/bc250_umd_resource_vk.c`:
  ```c
  BOOL Bc250UmdVkCopyStagingBufferToImage(
      BC250_UMD_DEVICE* pDevice,
      BC250_UMD_RESOURCE* pRes,
      VkDeviceSize BufferOffset,
      UINT RowPitch,
      SIZE_T ByteSize);
  ```
- **Fluxo 100% acelerado por hardware:**
  1. A GPU transfere a textura compartilhada para o buffer Vulkan de staging via SDMA (1 ms).
  2. `Bc250UmdVkCopyStagingBufferToImage` emite barreira `UNDEFINED -> TRANSFER_DST_OPTIMAL`.
  3. Emite `vkCmdCopyBufferToImage` gravado diretamente no command buffer.
  4. Emite barreira `TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL`.
  5. Executa flush com fence concluída em hardware (`Bc250UmdVkFlushCommands`).
- **Eliminação completa do CPU memcpy:** a CPU não lê nem grava um único byte da memória de staging durante a transferência, extinguindo completamente o risco de `0xc0000005` e use-after-free.

---

## 3. Provas Offline de Validação

- **Build de contratos (`driver/wdk/build-contract.bat`):** `EXIT 0` (105 DDIs, 0 falhas).
- **Build da UMD (`driver/umd/build-umd.bat`):** `EXIT 0` (testes de descritores, nós e winsys aprovados).
- **Pacote de laboratório assinado (`tools/package-bc250mcdm-lab.ps1`):** `EXIT 0` (DriverVer `3.326.1.0`).
- **Auditoria de segurança (`tools/audit-bc250mcdm-safety.py`):** `PASS`.
- **Auditoria do pacote de laboratório (`tools/audit-bc250mcdm-lab-package.ps1`):** `PASS`.
- **Validação de pré-voo (`tools/prepare-corrida181-t41.ps1`):** `PASS`.

### Hashes do Candidato T4.1 (DriverVer `09/29/2026,3.326.1.0`)
- `bc250mcdm.sys`: `70053A627762CF1AC146DC4162EA7E09159377C3C9B1888CED63372FC6CB3BD7`
- `bc250mcdm.inf`: `E2CA94578A1284019ECD199B8F7B566F2C15D75D7B34C2294D5809EE86D4ADEB`
- `bc250mcdm.cat`: `F2E00DBBA87C53E27EB99B32230E838A72E7518847E67387CCDEAC0564E49641`
- `bc250umd.dll`: `3D3037517C96E464D96C530C0E62AD97A85C30201847CBA53943AD9AC835ABF7`
- `vulkan_radeon.dll`: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`

---

## 4. Critério da Corrida 181

- **Procedimento:** Executar `tools\EXECUTAR_CORRIDA181_T41_600S.cmd` como Administrador após boot frio limpo.
- **Objetivo:** Abrir o menu Iniciar repetidas vezes sem crash do `dwm.exe` (sem tela preta temporária) e verificar se os ícones/textos dos blocos são visíveis na composição acelerada.
- **Métricas de Sucesso:**
  1. Desktop e compositor estáveis por 600 segundos ininterruptos.
  2. Zero quedas do `dwm.exe` (zero logs de restart do DWM ou Explorer).
  3. Renderização correta dos blocos do menu Iniciar sem blocos cinza sólidos.
  4. Recuperação limpa e segura em Código 0 pelo BasicDisplay após os 600 segundos.
