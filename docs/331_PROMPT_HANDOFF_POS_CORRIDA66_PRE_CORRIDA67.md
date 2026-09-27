# PROMPT DE HANDOFF — BC-250 WDDM — Pós-Corrida 66, Pré-Corrida 67

> **EMENDADO EM 22/09 (faixa DX.21) — leia [docs/330A](330A_EMENDA_CRITERIO_CORRIDA67_CID_CORRIGIDO_E_DX21.md) antes deste.**
> (1) O candidato da Corrida 67 é o **lab 3.229.1.0**, não o 3.228.1.0. (2) A regra da seção 0.3
> abaixo está **errada**: `Cid=10` é **SQC (data)** e `Cid=9` é **SQC (inst)** (`gfxhub_v2_0.c`,
> P1); `gc_10_1_0_sh_mask.h` não tem tabela de clientes. (3) O `gfx-ib-dump` agora é coletado de
> `C:\Windows\Temp\BC250_GFX_IB_DUMP`. O texto original fica abaixo como registro.

> **Data do Handoff:** 22/09/2026  
> **Workspace:** `C:\Users\Daniel\Desktop\NamixWDDM`  
> **Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2, GFX10 RDNA1) | Windows 10 22H2 x64  
> **Candidato Pronto:** Lab **3.228.1.0** (`DISPLAY_RENDER_LAB`, WDDM 2.7, schema 135, P1 PERSISTENT SUBMISSION)  
> **Estado do Candidato:** Compilado, assinado, empacotado e auditado com **PASS**; **NÃO INSTALADO e NÃO VALIDADO EM HARDWARE**  
> **Próximo Ensaio:** Corrida 67 — Provar eliminação de DCC (`RADV_DEBUG=nodcc`) e desbloqueio do Draw 2 do DWM sem stall/TDR  
> **Critério Prévio Congelado (Lei 9):** [`docs/330_CRITERIO_CORRIDA67_ELIMINACAO_DCC_E_DESBLOQUEIO_DRAW2.md`](330_CRITERIO_CORRIDA67_ELIMINACAO_DCC_E_DESBLOQUEIO_DRAW2.md)  
> **Handoff da Corrida 66:** [`docs/329_HANDOFF_2026-09-22_CORRIDA66_UPLOAD_PROVADO_E_FENCE66.md`](329_HANDOFF_2026-09-22_CORRIDA66_UPLOAD_PROVADO_E_FENCE66.md)  
> **Fonte Única de Verdade:** [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md)

---

## 0. Instruções Obrigatórias para a Próxima IA

Você está assumindo o desenvolvimento do driver WDDM 2.7 da AMD BC-250 (`1002:13FE`). Siga rigorosamente as normas deste repositório:

1. **Governança Geral:** Leia integralmente `AGENTS.md` e `GEMINI.md`.
2. **Proibição de Status Bar:** **ESTÁ TERMINANTEMENTE REVOGADA** a inclusão de barras de progresso ou caixas `"STATUS DO DRIVER BC-250"` antes das respostas. Suas respostas devem ser diretas, técnicas e concisas.
3. **Leis de Hardware e Nomenclatura:**
   - **Lei 7:** Nunca invente nomes para faltas de MMU/GCVM. Conforme `inc/linux_ref/gc_10_1_0_sh_mask.h`, `Cid=10` é categoricamente **`TCP_UTCL0`** (Texture Cache Pipe) e `Cid=9` é **`SQC_DCACHE`** (Scalar Data Cache).
   - **Lei 9:** O critério de avaliação da corrida deve estar congelado **antes** do teste físico (ver [`docs/330_CRITERIO_CORRIDA67_ELIMINACAO_DCC_E_DESBLOQUEIO_DRAW2.md`](330_CRITERIO_CORRIDA67_ELIMINACAO_DCC_E_DESBLOQUEIO_DRAW2.md)). Nunca altere critérios após observar o resultado do teste.
4. **Relato Normativo de 5 Seções:** Após a execução e análise de cada corrida física, apresente obrigatoriamente ao Daniel:
   1. **Houve avanço?** (Sim, Não ou Inconclusivo, comparando com a corrida anterior pertinente).
   2. **Sucessos:** (métricas, fences, tempos, evidências).
   3. **Falhas:** (erros, stalls, faults de GCVM, TDRs, timeouts).
   4. **Veredito:** (Sucesso Pleno, Parcial, Falha ou Inconclusivo frente ao critério prévio).
   5. **Recuperação e Pendências:** (estado final do driver, recuperação do BasicDisplay, o que continua não validado em hardware e próximo passo).
5. **Preservação do Workspace:** Não use comandos destrutivos (`git reset --hard`, `git checkout --`). Não confunda o desktop restaurado pelo `BasicDisplay` com sucesso do nosso driver.

---

## 1. Arquitetura e Rota Vigente

- **Display v1:** Herança direta do UEFI GOP. VidPn de modo único, scanout em VRAM real, apresentação `DxgkDdiPresent` via modelo **blt** acelerado por SDMA0 da GPU, com VSync lido do frame counter do OTG.
- **DCN:** Escrita em registradores de DCN é estritamente proibida no caminho crítico atual (reservada para P9).
- **Submissão Gráfica:** CP_RB0 (Node 1 GFX) operando sob anel persistente em VMID 7 (VMID por contexto ativo).
- **Backend UMD D3D11:** Tradução de chamadas D3D11 para Vulkan (`bc250umd.dll` carregando o ICD RADV isolado em `C:\BC250_LIB\vulkan_radeon.dll`).

---

## 2. O que Aconteceu na Corrida 66 (Evidência Física)

- **Candidato:** Lab `3.227.1.0` (Faixa DX.19).
- **Pasta de Evidência:** `docs/evidence/2026-09-22_13-06-56-DESKTOP-HQAG6T4-lab-binding/`.
- **Avanço Histórico:**
  - A restauração do upload de texturas via staging buffer (`Bc250UmdVkUploadImageData`) e barreiras de memória foi **100% PROVADA no silício**.
  - A textura `590x800` (2.048.000 bytes) foi transferida para `VkImage` com transição `UNDEFINED -> TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL` confirmada por fence de hardware.
  - O antigo fault em `0x2441000` foi **completamente eliminado**.
  - O ring gráfico `CP_RB0` consumiu e aposentou **64 submissões 3D no silício**, com avanço de fences saltando de 10 (Corrida 65) para **Fence 66**.
  - **Draw 1 do DWM** (`VkDraws=1`, vertices=48, `CB=1, SRV=0`) executou e aposentou em apenas **21 ms**.
- **O Gargalo Isolado (Draw 2):**
  - **Draw 2 do DWM** (`VkDraws=2`, vertices=126, `CB=1, SRV=1, SAMPLER=1`) vinculou pela primeira vez a textura amostrada `590x800` (`res=0000000006929170`, alocada em `0x243100000`).
  - O Draw 2 demorou **2,065 segundos** para concluir (`13:07:27.678 -> 13:07:29.743`), ultrapassando o limiar de 2 segundos do watchdog de TDR do WDDM.
  - Durante o TDR, o KMD registrou um GCVM protection fault com `Status=0x0010153C`, `Cid=10` (`TCP_UTCL0` - Texture Cache Pipe) em `FaultAddrPagina=0x2431` (`GpuVa=0x2431000`).
  - Um segundo fault ocorreu em Fence 66 com `Status=0x001012B1`, `Cid=9` (`SQC_DCACHE` - Scalar Data Cache) em `GpuVa=0x200040000` (PTE=0).

---

## 3. Causa Raiz e Soluções da Faixa DX.20 (Candidato 3.228.1.0)

### Diagnóstico Físico
1. **Por que o fault ocorreu em `0x2431000`?**
   - Na arquitetura AMD GFX10 (RDNA1), o registrador de recurso de imagem `SQ_IMG_RSRC_WORD0` armazena o endereço base como:
     $$\text{BASE\_ADDRESS}[39:8] = \text{GpuVa} \gg 8$$
   - Para o recurso alocado em `0x243100000`:
     $$0x243100000 \gg 8 = 0x02431000$$
   - Ou seja, `0x2431000` é exatamente a representação do descritor de textura $T\#$ truncado em 256 bytes.
2. **Por que ocorreu stall e fault no Texture Cache Pipe (`Cid=10`)?**
   - No RADV, ao alocar recursos com `VK_IMAGE_TILING_OPTIMAL`, a compressão de cores **DCC (Delta Color Compression)** é ativada automaticamente (o que explica `reqSize=2310144` contra `2048000` bytes brutos).
   - O DCC habilita `COMPRESSION_EN=1` no descritor da imagem (`desc[6]`) e programa o endereço de metadados em `desc[6]/desc[7]`.
   - O upload de dados por staging buffer (`vkCmdCopyBufferToImage`) escreveu apenas os texels de dados brutos na imagem, deixando os metadados de DCC não inicializados na VRAM.
   - Quando o Pixel Shader executou amostragem (`sample`) da textura, a unidade `TCP_UTCL0` tentou ler metadados corrompidos de compressão, travando a descompressão por 2 segundos e tentando acessar a página de descritor como memória física inválida (`PDE=0`).

### Modificações Realizadas no Código
1. **Desativação Completa de DCC na UMD Vulkan Bridge:**
   - Arquivo: [`driver/umd/bc250_umd_vulkan_bridge.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_umd_vulkan_bridge.c)
   - Adicionado `SetEnvironmentVariableA("RADV_DEBUG", "nodcc")` antes da carga do ICD RADV e de `vkCreateInstance`.
   - Isso desativa categoricamente a compressão DCC no compilador e descritores do RADV (`COMPRESSION_EN=0`), forçando o `TCP_UTCL0` a ler texels brutos diretamente de `0x243100000` (onde os pixels reais foram carregados).
2. **Flags de Textura 2D:**
   - Arquivo: [`driver/umd/bc250_umd_resource_vk.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_umd_resource_vk.c)
   - Adicionado `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` para amostragem tipada.
3. **Versão e Assinatura do Pacote:**
   - Arquivo: [`driver/wdk/package/bc250mcdm.inf`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/package/bc250mcdm.inf)
   - DriverVer atualizado para `09/22/2026,3.228.0.0` (Pacote Lab `3.228.1.0`).

---

## 4. Estado Atual do Pacote de Laboratório

O pacote em `driver\wdk\package\output_lab` foi empacotado e auditado com **PASS**:

- **DriverVer:** `09/22/2026,3.228.1.0`
- **Perfil:** `DISPLAY_RENDER_LAB (RenderNode=1, RenderOnlyLab=0)`
- **Contrato:** WDDM 2.7
- **Modo:** P1 PERSISTENT SUBMISSION
- **Hashes SHA-256:**
  - `bc250mcdm.sys`: `F2A3F42C3823D592CAB428ED9497EB457A85C854A676C5CFCBB2C70971DB614D`
  - `bc250umd.dll`: `B1A6D3A2ED6A2EC84514787B58CCB8B77B48D7AA7AC1166E2D5081FE1E2494F2`
  - `bc250mcdm.inf`: `D36D75E84F5A064AC74E415DC03A48B23824FD8C3D3B361C7FE8880F6366E3DB`
  - `bc250mcdm.cat`: `8C80FCD21E4F962719073AA16DA983CC164933927C434D116F4A8F48745DAEF5`
- **Fusível de Boot (`C:\BC250_BOOT_FUSE.dat`):** Limpo (Inexistente).
- **Marcador de Boot Frio (`C:\BC250_REQUIRE_CLEAN_BOOT.json`):** Inexistente.

---

## 5. Como Executar a Corrida 67

O script físico de execução é:

```cmd
cd C:\Users\Daniel\Desktop\NamixWDDM
.\tools\EXECUTAR_D3D11_NATIVO.cmd -Force
```

> **Atenção:** NÃO existe arquivo `BC250_TEST_LAB.cmd`. O script oficial que executa o binding de laboratório, dispara o harness D3D11 nativo, monitora o DWM e faz o rollback automático em caso de TDR/Watchdog é o [`tools/EXECUTAR_D3D11_NATIVO.cmd`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/EXECUTAR_D3D11_NATIVO.cmd).

---

## 6. Roteiro Passo a Passo para a Próxima IA

Quando o Daniel informar que executou o teste:

1. **Localizar a Pasta de Evidência:**
   Execute via PowerShell:
   ```powershell
   Get-ChildItem -Directory 'docs\evidence\*lab-binding' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
   ```
2. **Inspecionar os Logs de Evidência:**
   - Inspecione `bc250_umd_log_*-FINAL.txt` na pasta de evidência.
   - Verifique se a variável `RADV_DEBUG=nodcc` foi aplicada e se `reqSize` das texturas mudou ou se o descritor não acionou DCC.
   - Meça a duração do Draw 2 (`VkDraws=2`): se concluiu em < 100 ms (em vez dos 2.065 ms da Corrida 66).
   - Verifique se o DWM continuou renderizando Draws adicionais (`VkDraws=3`, `VkDraws=4`, etc.).
   - Verifique se as fences de hardware do Node 1 continuaram avançando além de 66.
   - Inspecione o log do KMD (`bc250mcdm.log` ou `kmd_binding.log`) para conferir se os faults `Cid=10` e `Cid=9` foram eliminados.
   - Verifique se houve algum BugCheck ou se o watchdog operou limpo.
3. **Emitir o Relatório Normativo de 5 Seções para o Daniel:**
   - 1. Houve avanço?
   - 2. Sucessos medidos.
   - 3. Falhas observadas.
   - 4. Veredito frente a `docs/330_CRITERIO_CORRIDA67_ELIMINACAO_DCC_E_DESBLOQUEIO_DRAW2.md`.
   - 5. Recuperação e pendências.
4. **Arquivar e Registrar:**
   - Crie o handoff da corrida (`docs/332_HANDOFF_...`).
   - Atualize `BASE_DE_AVANCO.md` com o desfecho da Corrida 67.
