# 536 — T4.0: Ponte de Dados OpenResource, Descritores SRV e Critério da Corrida 180

> 29/09/2026, offline. Candidato **3.325.1.0** (inclui T4.0, T3.9, T3.8, T3.7, T3.6, T3.5, T3.4, T3.3, HDP.1, T3.1, EVT.1, X1, X2a, X3 e X4). **Nada desta faixa foi validado em hardware.**

---

## 1. Por quê: Diagnóstico e Evidência Física da Corrida 179

Na **Corrida 179** (candidato T3.9, DriverVer `3.324.1.0`), o Daniel executou o ensaio de 600 segundos com repetidas aberturas e fechamentos do menu Iniciar e reportou:
> *"boa, nao travou agora e nem deu tela azul, abri varias bezes o menu, as vezes aparecia os quadrados no menu e as vezes nao, mesmo com o quadrado nada de conseguir ver nada no menu"*

A fotografia arquivada (`media_1790724679361.jpg`) comprovou:
- O menu Iniciar abriu com sucesso sobre o desktop ultrawide acelerado.
- Uma grade regular de 12 blocos (6 superiores, 6 inferiores) foi perfeitamente composta pelo DWM.
- Todos os 12 blocos foram desenhados como retângulos sólidos de cor cinza escuro (`0xFF202020`).
- **Zero BugChecks** (a reversão `EvictionSegmentSet = 0u` extinguiu em definitivo o BugCheck `0x10E (0x34)`).
- **Zero travamentos ou telas azuis** durante os 600 segundos completos do teste.
- **Métricas do hardware:** 2.699 quadros comitados por `SetVidPnSourceAddress`, 50.973 submissões SDMA no Node 0 e 3.064 flushes de hardware executados sem qualquer erro.

---

## 2. Causa Raiz dos Blocos Cinza Sólidos

A investigação no código da UMD revelou dois gargalos complementares que impediam a chegada dos pixels das aplicações do shell moderno ao compositor:

1. **Ausência de Handles na Abertura de Recursos Compartilhados (`Bc250Umd10OpenResource`):**
   - O DDI `OpenResource` recebia do runtime WDDM as estruturas `D3DDDI_OPENALLOCATIONINFO2` contendo `hAllocation` (o handle kernel da alocação) e `GpuVirtualAddress` (o endereço virtual mapeado na GPU).
   - O código anterior apenas inspecionava o buffer de dados privados para ler a geometria, deixando `res->Allocation.KernelAllocation = 0` e `res->Allocation.GpuVirtualAddress = 0`.
   - Consequentemente, para a UMD e o winsys, o recurso compartilhado não possuía identidade física nem endereço de acesso.

2. **Substituição Agressiva por Textura Fallback 1x1 (`Bc250UmdBuildVkDescriptorSet`):**
   - Em `driver/umd/bc250_umd_descriptors.c:132-157`, sempre que um recurso não possuía imagem Vulkan própria (`!resource->VkImg`), o código sobrescrevia incondicionalmente suas dimensões reais para `Width = 1u; Height = 1u;` e realizava o upload de um único pixel `0xFF202020`.
   - Ao executar os draw calls da composição do shell, o DWM aplicava esse descritor 1x1 em cada quad dos blocos, renderizando o cinza sólido fotografado pelo Daniel.

---

## 3. Arquitetura da Solução: Candidato T4.0

### A. Extração Integral de Alocação em `OpenResource` (`driver/umd/bc250_d3d_umd.c`)
- Popula `res->Allocation.KernelAllocation` e `res->Allocation.GpuVirtualAddress` diretamente a partir de `pOpenAllocationInfo2[0]`.
- Caso o runtime não tenha mapeado GPUVA no processo do DWM, o driver executa o mapeamento e torna o recurso residente via `Bc250WinsysMapAllocation` e `Bc250WinsysMakeResident`.

### B. Ponte de Blt GPU Bidirecional (`Bc250UmdBridgeGpuBlt`)
- Generaliza o escape de composição do KMD (`BC250_ESCAPE_OP_COMPOSITE_BLT`) para permitir transferências diretas GPU-to-GPU no Node 0 entre quaisquer faixas de GPUVA do processo.
- Permite a transferência instantânea de dados da alocação compartilhada para imagens Vulkan com dreno estrito do lote (`Bc250UmdVkDrainPending`).

### C. Estratégia em Três Camadas para Descritores de Textura (`driver/umd/bc250_umd_descriptors.c`)
Preserva rigorosamente a largura, altura, pitch e formato reais da superfície aberta:
- **Tier 1 (Acesso Direto CPU):** Tenta ler a memória da alocação via `Bc250WinsysReadAllocation` (`pfnLockCb` com `Flags.ReadOnly = 1u`). Havendo dados válidos, efetua o upload para a imagem Vulkan através de `Bc250UmdVkUploadImageData`.
- **Tier 2 (Transferência Direta SDMA GPU-to-GPU):** Se o lock CPU não estiver disponível (alocações só-GPU criadas com `CpuVisible = 0`), transfere os pixels da `GpuVirtualAddress` de origem para o buffer intermediário de staging via `Bc250UmdBridgeGpuBlt`.
- **Tier 3 (Fallback Geométrico Seguro):** Se nenhuma leitura for possível, preenche uma textura nas **dimensões reais da superfície** com `0xFF202020`, mantendo a estabilidade sem distorção 1x1.

---

## 4. Provas Offline de Validação

- **Build de contratos (`driver/wdk/build-contract.bat`):** `EXIT 0` (105 DDIs, 0 falhas).
- **Build da UMD (`driver/umd/build-umd.bat`):** `EXIT 0` (todos os testes de contratos de descritores e Node 1 aprovados).
- **Pacote de produção (`tools/build-bc250mcdm-package.ps1`):** `EXIT 0` (DriverVer `3.325.0.0`).
- **Imagem de laboratório (`tools/build-bc250mcdm-lab-bringup.ps1`):** `EXIT 0`.
- **Pacote de laboratório assinado (`tools/package-bc250mcdm-lab.ps1`):** `EXIT 0` (DriverVer `3.325.1.0`).
- **Auditoria de segurança (`tools/audit-bc250mcdm-safety.py`):** `PASS`.
- **Auditoria do pacote de laboratório (`tools/audit-bc250mcdm-lab-package.ps1`):** `PASS`.
- **Validação de pré-voo (`tools/prepare-corrida180-t40.ps1`):** `PASS`.

### Hashes do Candidato T4.0 (DriverVer `09/29/2026,3.325.1.0`)
- `bc250mcdm.sys`: `F082BA0BC02E1736D22E032C6A175F38F92ECC81F6C6E133C2537E4265C6323C`
- `bc250mcdm.inf`: `289B6B9E69593879C6AA0F8F91104E58D109A692FE674D65FD13EE58AE994CC9`
- `bc250mcdm.cat`: `279A3F4E5A323872D1FCCE65FE4015C5E616DE59FAC56544E886A5DA3E3DF659`
- `bc250umd.dll`: `79C07B3DEEAE03980B0E799D71F3723D19DA27F1612C47448F804161901F25B5`
- `vulkan_radeon.dll`: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`

---

## 5. Critério da Corrida 180

- **Objetivo:** Abrir o menu Iniciar do Windows 10 na BC-250 e verificar se os ícones, textos e conteúdos reais dos blocos são renderizados no lugar dos retângulos cinza sólidos da Corrida 179.
- **Métricas de Sucesso:**
  1. Desktop ativo e estável por até 600 segundos.
  2. Elementos gráficos visíveis (ícones/textos) nos blocos do menu Iniciar.
  3. Zero BugChecks, travamentos ou anomalias de paginação.
  4. Recuperação limpa e segura em Código 0 pelo BasicDisplay após o término do ensaio.
