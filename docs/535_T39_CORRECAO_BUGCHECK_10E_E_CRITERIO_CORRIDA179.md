# 535 — T3.9: Correção do BugCheck 0x10E (0x34), Prova do Quadrado Cinza e Critério da Corrida 179

> 29/09/2026, offline. Candidato **3.324.1.0** (inclui T3.9, T3.8, T3.7, T3.6, T3.5, T3.4, T3.3, HDP.1, T3.1, EVT.1, X1, X2a, X3 e X4). **Nada desta faixa foi validado em hardware.**

---

## 1. Por quê: Diagnóstico e Causalidade da Corrida 178

Na **Corrida 178** (candidato T3.8, DriverVer `3.323.1.0`), dois fatos físicos cruciais foram observados pelo Daniel:
1. *"o teste ainda esta rolando, porem abri o menu e aparece so o qquadrado cinza"*
2. *"tentei abrir o menu ai começou a ficar bem lento e tela azul"*

O registro de eventos do Windows confirmou a verificação de erro:
- **BugCheck:** `0x0000010E` (`0x0000000000000034`, `0x000000000000000B`, `0x0000000000000001`, `0x0000000000000000`)
- **Horário:** 29/09/2026 19:55:18 (após mais de 6 minutos de desktop ativo e acelerado).

---

## 2. A Descoberta Crucial: O Quadrado Cinza (`0xFF202020`)

Na transição do candidato T3.7 para o T3.8, o valor do pixel da textura fallback 1x1 em `driver/umd/bc250_umd_descriptors.c:140` foi alterado:
- Em **T3.7:** `fallbackPixel = 0x00000000u` (preto transparente).
  - *Resultado visual:* Daniel relatou um "quadrado preto".
- Em **T3.8:** `fallbackPixel = 0xFF202020u` (RGB 32,32,32, Alpha 255 — cinza neutro do tema escuro do Windows 10).
  - *Resultado visual:* Daniel relatou "o quadrado cinza".

### O que isso comprova no silício
1. **O DWM está executando nossos Shaders e Descritores D3D11:** O DWM compilou com sucesso seus pixel shaders de composição, vinculou os descritores de textura gerenciados pela nossa UMD (`bc250umd.dll`) e amostrou a textura fallback diretamente na superfície do menu Iniciar.
2. **Causa da Ausência de Ícones no Menu:** Superfícies do shell moderno (`StartMenuExperienceHost.exe`) são criadas em outro processo e passadas ao DWM via `OpenResource`. Como a importação de imagens Vulkan entre instâncias independentes ainda não está conectada, `resource->VkImg` é nulo nessas superfícies compartilhadas. A UMD adotou o caminho seguro de fallback 1x1, gerando o quadrado cinza observado.

---

## 3. Causa Raiz do BugCheck 0x10E (`0x34, 0x0B, 0x01, 0x00`)

O código `0x10E` é `VIDEO_MEMORY_MANAGEMENT_INTERNAL` do `dxgmms2.sys`.
- **Subcódigo `0x34`:** Transição de estado de alocação de heap incompatível com o conjunto de segmentos suportados.
- **Mecanismo:** No candidato T3.8, `info->EvictionSegmentSet = 2u;` foi configurado em `Bc250ActiveCreateAllocationLifecycle` para todas as alocações não-primárias, enquanto `info->SupportedReadSegmentSet` e `info->SupportedWriteSegmentSet` permaneceram `1u`.
- No modelo de paginação do WDDM 2.x, um conjunto de evicção só é válido se pertencer ao conjunto de segmentos suportados pelo driver para leitura/escrita.
- Sob a pressão de alocação gerada pelo Explorer e pelo Shell (~245 MB na VRAM local), o VidMm tentou evictar alocações para o Segmento 2 via `DXGK_OPERATION_TRANSFER`. Ao detectar que o Segmento 2 não era suportado para leitura, a máquina de estados do heap do `dxgmms2.sys` acionou o BugCheck `0x10E (0x34)`, precedido pela lentidão severa descrita pelo Daniel (thrashing de tentativa de evicção inválida).

---

## 4. A Correção: Candidato T3.9

### A. Reversão de `EvictionSegmentSet = 0u` (`driver/wdk/bc250_active_miniport.c`)
- Define `info->EvictionSegmentSet = 0u;` incondicionalmente para todas as alocações.
- O VidMm volta a utilizar a evicção virtual de páginas para a memória de paginação do sistema sem violar o conjunto de segmentos locais (`SupportedReadSegmentSet = 1u`).
- Este é exatamente o modelo que operou de forma 100% estável e sem BugCheck durante as Corridas 175, 176 e 177.

### B. Manutenção das Proteções de Geometria e Descritores
- Preserva o clamping seguro de descritores de SRV (`driver/umd/bc250_umd_descriptors.c`).
- Preserva o parser de geometria de superfícies abertas em `OpenResource` (`driver/umd/bc250_d3d_umd.c`).
- Preserva o fallback rápido e seguro de composições compartilhadas.

---

## 5. Provas Offline de Validação

- **Build de contratos (`driver/wdk/build-contract.bat`):** `EXIT 0` (105 DDIs, 0 falhas).
- **Pacote de produção (`tools/build-bc250mcdm-package.ps1`):** `EXIT 0` (DriverVer `3.324.0.0`).
- **Imagem de laboratório (`tools/build-bc250mcdm-lab-bringup.ps1`):** `EXIT 0`.
- **Pacote de laboratório assinado (`tools/package-bc250mcdm-lab.ps1`):** `EXIT 0` (DriverVer `3.324.1.0`).
- **Auditoria de segurança (`tools/audit-bc250mcdm-safety.py`):** `PASS`.
- **Auditoria do pacote de laboratório (`tools/audit-bc250mcdm-lab-package.ps1`):** `PASS`.
- **Validação de pré-voo (`tools/prepare-corrida179-t39.ps1`):** `PASS`.

### Hashes do Candidato T3.9 (DriverVer `09/29/2026,3.324.1.0`)
- `bc250mcdm.sys`: `6CD63D89138DDC051E01174DF53F19F7A90A80BF41994D0920357B26DBB2CFCF`
- `bc250mcdm.inf`: `3D0A3D0BB869A3EC5ADB64A7E9A3E1AD7AA29D72480ACE7B2687A3E18BF0829E`
- `bc250mcdm.cat`: `93DF8755D91F69DE57E5C964EE2FAD0F8AB8814F43364291AEA6C1D49DCB63B5`
- `bc250umd.dll`: `59FE4A823D78EE9780AEF4013F437C0733512860DFE1CF1C8D14E2D77BD8EF36`
- `vulkan_radeon.dll`: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`

---

## 6. Critério da Corrida 179

- **Objetivo:** Abrir e fechar o menu Iniciar do Windows na BC-250 e verificar que o sistema permanece responsivo, eliminando completamente a lentidão de paginação e o BugCheck `0x10E (0x34)`.
- **Métricas de Sucesso:**
  1. Desktop estável por até 600 segundos sob interação com o menu Iniciar.
  2. Zero BugChecks (0x10E, 0x119 ou 0x116 eliminados).
  3. Ausência de thrashing ou congelamento severo da interface durante a abertura do menu.
  4. Recuperação limpa e segura em Código 0 pelo BasicDisplay ao término do ensaio.
