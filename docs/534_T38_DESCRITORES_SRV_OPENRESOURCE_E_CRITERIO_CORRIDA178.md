# 534 — T3.8: Clamping de SRV, Extração de Geometria em OpenResource, Evicção para Segmento 2 (GART) e Critério da Corrida 178

> 29/09/2026, offline. Candidato **3.323.1.0** (inclui T3.8, T3.7, T3.6, T3.5, T3.4, T3.3, HDP.1, T3.1, EVT.1, X1, X2a, X3 e X4). **Nada desta faixa foi validado em hardware.**

---

## 1. Por quê: Diagnóstico e Causalidade da Corrida 177

Na **Corrida 177** (candidato T3.7, DriverVer `3.322.1.0`), o desktop permaneceu ativo e acelerado por hardware durante **226,5 segundos (quase 4 minutos)** ininterruptos.

### O que funcionou e avançou na Corrida 177
1. **Estabilidade e Aceleração Contínua Comprovadas:**
   - **746 quadros** comitados pelo DWM (`SetVidPnSourceAddress`).
   - **1.008 apresentações** no modelo Present Blt por SDMA no Node 0.
   - **1.211 submissões** de renderização no Node 1 (GFX) executadas e aposentadas (WPTR=22.772, RPTR=6.388).
   - **11.885 submissões** de SDMA no Node 0 executadas com sucesso.
   - **Zero BugChecks:** nenhum 0x10E, 0x119, 0x133 ou 0x116 ocorreu.
   - **Gerenciador de Tarefas ativo:** a foto enviada pelo Daniel comprovou a GPU 0 (AMD BC-250 Graphics Cyan Skillfish 2) com gráficos de uso 3D e Copy atualizando dinamicamente no Windows.

### A causa do Menu Iniciar como bloco preto e do congelamento temporário
Ao interagir com o menu Iniciar, duas anomalias ocorreram:
1. **Rejeição de Draw no Explorer por SRV além do subrecurso carregado:**
   - O Direct2D no `explorer.exe` compilou com sucesso o pipeline gráfico e vinculou um Shader Resource View (SRV) com textura 1024x1 (gradiente de shell do menu Iniciar).
   - Em `bc250_umd_descriptors.c`, uma verificação arbitrária (`MostDetailedMip != 0 || FirstArraySlice != 0`) rejeitou o descritor com `SRV_ALEM_DO_SUBRECURSO_CARREGADO`.
   - O `Draw` falhou (`Draw: RECUSADO -- backend Vulkan motivo=SRV_ALEM_DO_SUBRECURSO_CARREGADO`), fazendo o Direct2D abortar imediatamente toda a renderização e desmontar os estados do device context. Nada do conteúdo do menu foi desenhado.
2. **Amostragem DWM de textura fallback 1x1 transparente (0x00000000):**
   - Ao compor janelas e superfícies abertas via `OpenResource`, o DWM abriu recursos compartilhados cujas dimensões ficavam zeradas na UMD (`res->Width = 0`, `res->Height = 0`).
   - A UMD criava uma textura fallback 1x1 preenchida com `0x00000000` (preto transparente).
   - O DWM amostrava essa textura transparente sobre o fundo do desktop, resultando em um bloco preto opaco/vazio no lugar do menu e janelas.
3. **Thrashing de Paginação (66.326 transferências) por saturação do BAR 256 MB:**
   - A memória dedicada de VRAM local (Segmento 1) é limitada pelo BAR da CPU em 256 MB.
   - Com o Explorer criando 3.752 recursos e 20.314 SRVs, o consumo atingiu 231 MB (90% do limite).
   - Como todas as alocações não-primárias tinham `EvictionSegmentSet = 0u`, o VidMm não podia evictá-las para a abertura de memória do sistema / GART (Segmento 2, 2048 MB).
   - O VidMm entrou em um loop agressivo de ping-pong de paginação (66.326 transferências em poucos segundos), retendo a fila de submissão e causando a trava temporária da interface.

---

## 2. A mudança (Candidato T3.8)

### A. Clamping de Subrecursos de SRV na UMD (`driver/umd/bc250_umd_descriptors.c`)
- Remove a rejeição artificial `SRV_ALEM_DO_SUBRECURSO_CARREGADO`.
- Em vez de falhar o descritor, a UMD grampeia (`clamping`) `MostDetailedMip` e `FirstArraySlice` para ficarem estritamente dentro dos limites reais do recurso alocado (`< resource->MipLevels` e `< resource->ArraySize`).
- **Efeito:** Direct2D e Explorer executam `Draw` com sucesso para shell brushes, ícones e gradientes sem abortar o contexto de desenho.

### B. Extração de Geometria em `OpenResource` (`driver/umd/bc250_d3d_umd.c`)
- Em `Bc250Umd10OpenResource`, desempacota os dados privados (`pPrivateDriverData` ou `pOpenAllocationInfo2`) via `Bc250AllocationAbiParse`.
- Adicionada a função auxiliar `Bc250D3dddiFormatToDxgiFormat` para traduzir formatos D3DDDI de volta para DXGI.
- Popula `res->Width`, `res->Height`, `res->RowPitch`, `res->Format`, `res->TotalByteSize` e `Token` reais da superfície compartilhada.

### C. Textura Fallback Opaque Tema Escuro (`driver/umd/bc250_umd_descriptors.c`)
- Atualiza o pixel de fallback de `0x00000000u` (preto transparente) para `0xFF202020u` (RGB 32,32,32, Alpha 255 — cinza neutro do tema escuro do Windows 10).
- Previne áreas de cliente transparentes/pretas vazias caso uma superfície compartilhada precise de amostragem antes de sua carga completa.

### D. Habilitação de Evicção para Segmento 2 (GART) no KMD (`driver/wdk/bc250_active_miniport.c`)
- Em `Bc250ActiveCreateAllocationLifecycle`, define `info->EvictionSegmentSet = 2u;` para todas as alocações não-primárias (`!Bc250AllocationIsPrimary(&request)`).
- A alocação primária (scanout) segue protegida com `EvictionSegmentSet = 0u`.
- **Efeito:** Quando a VRAM local (Segmento 1, 256 MB) sofre pressão de memória, o VidMm pode evictar suavemente buffers e texturas para o Segmento 2 (abertura GART de 2048 MB), eliminando o ciclo de thrashing de 66.326 transferências.

---

## 3. Provas Offline de Validação

- **Build de contratos (`driver/wdk/build-contract.bat`):** `EXIT 0` (105 DDIs, 0 falhas).
- **Compilação e suíte UMD (`driver/umd/build-umd.bat`):** `EXIT 0` (todos os testes de winsys, bridge, descriptors e tripod aprovados).
- **Pacote de produção (`tools/build-bc250mcdm-package.ps1`):** `EXIT 0` (DriverVer `3.323.0.0`).
- **Imagem de laboratório (`tools/build-bc250mcdm-lab-bringup.ps1`):** `EXIT 0`.
- **Pacote de laboratório assinado (`tools/package-bc250mcdm-lab.ps1`):** `EXIT 0` (DriverVer `3.323.1.0`).
- **Auditoria de segurança (`tools/audit-bc250mcdm-safety.py`):** `PASS`.
- **Auditoria do pacote de laboratório (`tools/audit-bc250mcdm-lab-package.ps1`):** `PASS`.
- **Validação de pré-voo (`tools/prepare-corrida178-t38.ps1`):** `PASS`.

---

## 4. Critério da Corrida 178

- **Objetivo:** Abrir o menu Iniciar do Windows e verificar que ele exibe seu conteúdo visual (fundo, ícones, elementos) sem ficar um quadrado preto sólido e sem provocar thrashing de paginação ou congelamento da máquina.
- **Métricas de Sucesso:**
  1. Menu Iniciar abre com elementos renderizados (Direct2D completando `Draw`).
  2. Ausência de thrashing de paginação sob carga do Explorer (evicção limpa para Segmento 2).
  3. DWM sustentado acelerado por hardware durante os 600 segundos.
  4. Janelas abertas exibem interiores e silhuetas visíveis.
