# Prompt de handoff — continuação do driver BC-250 após a Corrida 75

> Cole este texto inteiro na IA que vai continuar. Escrito em 22/09/2026, depois da Corrida 75.
> Commit do repositório principal: `b125b1a`.

---

Você vai continuar o desenvolvimento de um driver WDDM para a **AMD BC-250** (`1002:13FE`, GC 10.1.3 "Cyan Skillfish",
RDNA1 com 24 CUs e 16 GB de GDDR6), em **Windows 10 Pro 22H2** (build 19045). O objetivo final é rodar jogos com
D3D11/D3D12/Vulkan na tela ligada à própria placa, **sem WARP e sem fallback de software**.

## 0. Leia antes de qualquer coisa
1. `C:\Users\Daniel\Desktop\NamixWDDM\CLAUDE.md`: leis inegociáveis e o formato obrigatório do relato de corrida.
2. `BASE_DE_AVANCO.md`: as entradas do topo, de 22/09, são o estado atual. Leia pelo menos até a "Faixa DX.26".
3. `docs/339` a `docs/342`: critérios e análises das Corridas 72 a 75, na ordem.

## 1. Regras de trabalho (do dono do projeto, Daniel)
- **O Daniel executa as corridas físicas; você compila, analisa e prepara.** Nunca instale driver nem faça bind.
- **Boot frio entre binds.** O marcador `C:\BC250_REQUIRE_CLEAN_BOOT.json` fica armado depois de cada corrida.
- **Uma variável comportamental por corrida.** Medição somente leitura pode acompanhar. Se o Daniel pedir duas, avise e
  desenhe o critério para separar as assinaturas.
- **O critério é escrito ANTES da corrida** (Leis 9 e 10), num doc numerado (o próximo livre é o `docs/344`):
  - desfechos A, B, C… com **assinatura e implicação em campos separados**;
  - sempre um desfecho "G/E — fora do mapa";
  - **desfecho parcial não é desfecho**.
- **Depois de cada corrida, o relato tem as 5 seções obrigatórias:**
  1. Houve avanço?
  2. Sucessos
  3. Falhas
  4. Veredito frente ao critério
  5. Recuperação e pendências

  Informe data/hora, candidato e pasta de evidência. Atualize a `BASE_DE_AVANCO.md`.
- **Classifique afirmações:** P0 (medido no silício), P1 (upstream/fonte), P2 (parcial), H (hipótese). Nunca chame
  hipótese de causa. Se você errou antes, corrija em uma frase.
- **Registradores só com procedência** (`inc/linux_ref/`, `docs/03`). Nada de escrita em DCN.
- **Todo portão novo de auditoria precisa ser verificado por mutação:** introduza o defeito, veja reprovar, restaure. Isso
  já pegou furos em portões meus várias vezes.
- **Não faça commit** a menos que o Daniel peça.
- **Pronomes:** use o gênero neutro quando o gênero de alguém não foi informado.

## 2. Onde as coisas estão
| O quê | Onde |
|---|---|
| Repositório principal (KMD, UMD, ferramentas, docs) | `C:\Users\Daniel\Desktop\NamixWDDM` |
| KMD (miniport WDDM) | `driver/wdk/` (principal: `bc250_active_miniport.c`); contratos puros em `driver/core/` |
| UMD D3D10/11 → Vulkan | `driver/umd/` (`bc250_d3d_umd.c`, `bc250_umd_resource_vk.c`, `bc250_umd_vulkan_bridge.c`) |
| Fork do Mesa/RADV (ICD Vulkan) | `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm` — **as mudanças NÃO estão commitadas lá**; o delta está espelhado em `external/radv-wddm/bc250-radv-wddm.patch` (sha em `PROVENANCE.md`) |
| Referências upstream (amdgpu) | `inc/linux_ref/` (offsets, masks, `nvd.h`, `clearstate_gfx10.h`) e `bc250-win/BC250-V4/referencias/BC250-windowsDriverTest/docs/upstream/` (`gfx_v10_0.c`, `amdgpu_gfx.c`…) |
| Evidências das corridas | `docs/evidence/<data>-DESKTOP-HQAG6T4-lab-binding/` (Corrida 75 = `2026-09-22_22-34-53-…`) |

### Comandos
- **Suíte de contrato + testes offline:** `driver\wdk\build-contract.bat`
- **UMD e testes dela:** `driver\umd\build-umd.bat`
- **SYS de laboratório** (obrigatório antes de empacotar; o empacotador NÃO compila, só copia):
  `powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup`
- **Pacote:** antes, suba o `DriverVer` em `driver/wdk/package/bc250mcdm.inf` (fonte X.Y.0.0 → candidato X.Y.1.0; o último foi
  **3.236.1.0**). Depois:
  - `powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission`
  - auditoria do pacote: `tools\audit-bc250mcdm-lab-package.ps1`, que tem de dar PASS. Se falhar por IOException, rode o empacotador de novo.
- **Auditorias:** `python tools\audit-bc250mcdm-safety.py`, `tools\audit-mcdm-ddi-matrix.py`, `tools\audit-ddi-noop-callbacks.py`,
  `tools\audit-radv-wddm-safety.py --mesa-root <fork>`
- **Fork:** `build_radv.bat` na raiz do fork (ninja). Depois, `python tools\sync-radv-fork-delta.py --mesa-root <fork> --update` e
  atualize `external/radv-wddm/PROVENANCE.md`. O executor copia o ICD do `build\` do fork sozinho, então **recompilar o fork
  põe variável na corrida**.
- **Autoteste Vulkan** (controle positivo, roda antes do harness): `tools\build-gfx-selftest.bat`
- **Análise de IB:** `tools\decode-gfx-ib-regs.py [--estado] <ib.bin>` e `tools\check-gfx-ib-dump-cumulative.py <pasta>`
- **Corrida (quem roda é o Daniel):** `tools\EXECUTAR_D3D11_NATIVO.cmd -Force`

### Armadilhas conhecidas
- **Heredoc no Bash corrompe `\n` e apóstrofos:** escreva arquivos com o editor/Write, nunca via heredoc com barra invertida.
- **Scripts `.bat` precisam de CRLF** (há portão para isso).
- **O log do KMD é amostrado:** a ausência de uma linha não prova ausência do evento, então confira a classe de amostragem.
  O log vivo é cumulativo (`C:\BC250_DISPLAY_LOG.txt`); a pasta de evidência tem `display-log-this-bind-FINAL.txt`.
- **O dwm.exe não grava no perfil do usuário.** Os logs da UMD vão para `C:\Windows\Temp` e o executor os copia
  (`bc250_umd_log_<pid>-FINAL.txt`).
- **O latch de fault GCVM guarda o PRIMEIRO fault desde o último TDR.** Use a sentinela `[GFX_FAULT_SENTINELA]` para datar.

## 3. Estado atual — o que já funciona (P0, Corridas 73–75)
- **Identidade GFX10.1 correta no RADV:** o winsys declara `ip_discovery_version=10.1.3`; zero registradores só-10.3 nos IBs.
- **Clear State Buffer como no upstream:** CSB de 949 dw (`clearstate_gfx10.h`), `RLC_CSIB` programado com readback e o mesmo
  conteúdo no preâmbulo de boot do anel. **Foi isso que destravou a rasterização** (Corrida 73).
- **Autoteste Vulkan 5/5 nas 3 variantes de NGG:** fill, clear de transferência, clear de render pass, draw de tela cheia e
  contador de fragmentos = 65536/65536.
- **DWM renderiza o desktop pela GPU.** O Present copia por SDMA para o scanout do GOP, com `BYTE_PROOF PASS`. A rotação de
  identidades (DX.28) passou a levar a imagem Vulkan junto, e os "quadrados" sumiram.
- **Harness D3D11 (`driver/tests/d3d11_native_conformance_test.cpp`):** clear azul + triângulo com cor interpolada por vértice,
  **quadro 1 apresentado e visível no monitor**. É Present Blt do próprio app copiado pelo KMD ao centro do scanout
  (DstXY=640,180), **não** composição pelo DWM.
- **Estabilidade de base:** zero BugCheck, recuperação de TDR provada, rollback limpo.

## 4. O problema aberto nº 1 — TDR de "240 ondas presas"
Ele derruba o DWM (imagem estática, mouse travado) e o harness (`DEVICE_REMOVED` no quadro 2).

### Fatos (P0), Corridas 73, 74 e 75
- **8 de 8 travamentos** foram num IB logo depois de um IB de **outro VMID** (outro processo), contra uma taxa de base de
  18–26% dos IBs.
- **Todos em IB de dispatch** (280 ou 288 DW, um único `DISPATCH_DIRECT`). Pelo tamanho, é cópia imagem→buffer por compute
  (1280×720 → 160×90 grupos; 2560×1080 → 320×135). **Nenhum draw travou.** O mesmo tipo de IB retira normalmente quando o
  anterior é do mesmo VMID.
- **Assinatura:** `SQ_DEBUG_STS_GLOBAL` com WaveLevel 240/240, ME em `EVENT_WRITE`, PFP em `PFP_SYNC_ME`/`ACQUIRE_MEM`.
- **Fault latchado de busca de instrução (CID 9) sob o VMID do IB ANTERIOR, na página do shader do processo atual:**
  - harness (VMID 4) com shader em 0x2003C0B00 → fault VMID 1 em 0x2003C0000, onde o VMID 1 mapeia a PRIMARY do DWM sem
    bit de execução;
  - DWM (VMID 3) com shader em 0x2016000xx → fault VMID 1/2 em 0x201600000 com WalkerError=6.
- A raiz de cada VMID é instalada **por MMIO da CPU no instante do submit**, fora da ordem do anel. O pool reatribui VMIDs
  o tempo todo.

### Hipóteses já refutadas
- **"O PFP avança para o IB novo antes do anterior terminar"**, refutada na Corrida 75. `WAIT_REG_MEM` no PFP sobre a fence do
  anel + `PFP_SYNC_ME` na troca de VMID (DX.29) ficou ativo 77 vezes e não impediu nenhum dos 3 TDRs.
- O CSB, a identidade e o `PREAMBLE_CNTL` do fork não são a causa: todos corrigidos, e o padrão continua.

### Candidatos (H), por ordem de custo
1. **De onde vem o VMID das ondas de compute na fila GRÁFICA.** O fato forte é que o dispatch executa com o VMID do job
   anterior. Pesquise no upstream (`gfx_v10_0.c`, RADV/`ac_*`) e no PM4 se o dispatch na fila gráfica depende de algo além
   do campo VMID do `INDIRECT_BUFFER`. O RADV sob o amdgpu roda isso normalmente, então compare a sequência por job que o
   amdgpu emite (`amdgpu_ib_schedule`, `amdgpu_vm_flush`, `emit_vm_flush`, `emit_mem_sync`, `emit_cntxcntl`/switch buffer)
   com o que o nosso `Bc250ActiveGfxEngineSubmit` emite hoje: sincronia DX.29 + IB + `RELEASE_MEM`.
2. **Flush de VM dentro do anel na troca** (escrever `GCVM_CONTEXTn_PAGE_TABLE_BASE` e invalidar pelo anel, como
   `gmc_v10_0_emit_flush_gpu_tlb`), em vez de MMIO no submit.
3. **Diagnóstico barato, a considerar:** os dispatches que travam parecem ser as cópias imagem→buffer do próprio instrumento
   (`DRAW_READBACK`/`INSPECAO` na UMD) e/ou da ponte de Present. Confirme por log; isso é H. Desligar o readback por draw
   distingue "dispatch em geral" de "nosso instrumento", mas remove medição. Se fizer, declare.
4. Ideia de contorno para testar a hipótese, **não de solução**: evitar trocar de VMID na fila gráfica, por exemplo com um
   VMID fixo por processo ou serialização.

## 5. Outras pendências (não validadas em hardware)
- **Composição real:** a janela do app deveria ser composta pelo DWM, não copiada pelo KMD direto ao centro do scanout.
- **Desktop interativo:** mouse, e 60 quadros do harness.
- **Topologia fabricada no winsys do fork:** 1 SA por SE, contra 2 na captura Linux, que não pode ser reverificada.
- **30 de 34 golden settings diferentes** (só leitura, nunca aplicados). `PA_SC_TILE_STEERING_OVERRIDE` vai 0 no CSB
  (divergência declarada).
- **Trocas de resolução** vistas pelo operador no início da corrida: não atribuídas.
- **Barreiras da UMD** com `oldLayout` fixo em `COLOR_ATTACHMENT_OPTIMAL` (`bc250_d3d_umd.c`): suspeita antiga, não testada.

## 6. Primeiro passo sugerido
Sem gastar corrida:
1. leia a evidência da 75 (TDRs e sentinela no `display-log-this-bind-FINAL.txt`, e os logs da UMD);
2. investigue o candidato 1 por leitura de fonte upstream;
3. só então proponha ao Daniel a próxima variável, com critério escrito antes, no `docs/344`.
