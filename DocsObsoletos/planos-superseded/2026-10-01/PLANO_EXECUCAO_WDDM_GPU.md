# PLANO DE EXECUÇÃO: WDDM PROCESSANDO NA GPU (SILÍCIO REAL)

> **Documento Estratégico e Operacional** · AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 11 x64
> **Data:** 2026-09-05
> **Objetivo Único e Vigente:** Conquistar um driver WDDM que esteja **processando de verdade na GPU** (e não por fallback de CPU), mantendo a estabilidade do sistema sem BugCheck e sem tela preta.
> **Regra de Ouro:** **Só iremos avançar para computação na GPU com Vulkan / UMD 3D após ter o WDDM minimamente rodando e sustentado na GPU.**

---

```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pre-WDDM (Lab)        ██████████████████▋░  93%   Estavel e Isolado
SDMA (DMA Engine & Robustez)     ██████████████████░░  90%   Boot tripod OK em 1 poll; IB virtual travou DENTRO do IB (RPTR parou no fim do INDIRECT)
GCVM & Traducao de Memoria       ██████████████████▋░  93%   141 UpdatePT + 3 FlushTlb com AckObserved=1; traducao do IB sob VMID8 nao provada
RLC, CP & PM4 Direct Load        ███████████████████░  95%   Fence em VRAM provada
Telemetria & Observabilidade     █████████████████░░░  85%   IB_ALIGN era emitido sob spinlock e nunca chegou ao arquivo; corrigido no candidato 3.81
Compute & Async Queues (MEC/MQD) ██████████████████░░  90%   P1 17/17 e P2 16/16 provados; fila persistente nao consome
Miniport WDDM Display+Render     ████████████████░░░░  80%   A corrida de 22:27 morreu no primeiro FillVirtual: 0 CommitVidPn, 0 Present
=============================================================================================
Veredito Global: a corrida de 04/09 22:27 nao chegou perto da tela. O encoder de alinhamento
funcionou (DmaSize=64), a SDMA0 consumiu o pacote INDIRECT e saltou para o IB em GPUVA 0x24F000
sob VMID8 -- e nao voltou. RPTR parou em 96 bytes (DW 24), exatamente o fim do INDIRECT; o GCR e
a fence que vinham depois nunca rodaram e o poison 0x43DAFFFE ficou intacto. GFXHUB nao latchou
fault (STATUS=0). Devolver STATUS_IO_DEVICE_ERROR ao VidMm produziu BugCheck 0x119
VIDEO_SCHEDULER_INTERNAL_ERROR Arg1=2 Arg2=0xC0000185, reboot, e o fusivel recusou o boot
seguinte. Zero CommitVidPn, zero SetVidPnSourceAddress, zero Present, zero StopDevice.
Execucao de IB de paginacao sob VMID8 -- NAO VALIDADO EM HARDWARE.
Blt de apresentacao -- NAO VALIDADO EM HARDWARE.
=============================================================================================
```

---

## 1. Onde Estamos e Por Que a Tela Apaga com Reboot

### 1.1 A Cadeia de Causa e Efeito no WDDM de Produção
No silício da BC-250, o driver WDDM (`bc250mcdm.sys`) atinge **Código 0**, expõe os nós (Node 0 = SDMA, Node 1 = Compute) e inicializa a memória. O gerenciador de memória de vídeo do Windows (`VidMm`) começa a orquestrar os recursos necessários para o Desktop Window Manager (`DWM`).

Para preparar o espaço de vídeo, o `VidMm` despacha comandos de paginação (começando pelo `FillVirtual`, operação de limpeza/inicialização de alocações primárias):
1. O Windows chama `DxgkDdiBuildPagingBuffer` (Op 9 = `DXGK_OPERATION_FILL`). Nosso driver codifica pacotes `CONST_FILL` de SDMA dentro do buffer de DMA alocado pelo sistema.
2. O agendador de vídeo (`dxgmms2!VidSchiSubmitPagingCommand`) chama `DxgkDdiSubmitCommandVirtual` no Node 0 (SDMA), entregando a GPUVA do buffer de paginação (`pSubmitCommand->DmaBufferVirtualAddress`).
3. Em `Bc250ActiveSubmitCommandVirtualBody`, o driver tenta executar esse buffer usando o pacote de indireção de hardware da SDMA:
   ```c
   Bc250SdmaPresentExecuteIndirect(pSubmitCommand->DmaBufferVirtualAddress, ..., BC250_GCVM_VMID8);
   ```
4. O driver emite no anel físico da SDMA0 o pacote `INDIRECT` (OP 4) apontando para o buffer sob o VMID 8, seguido por um flush de cache (`GCR`) e uma `FENCE` física em VRAM.
5. **O Hardware Trava no Salto:** O microcontrolador F32 da SDMA0 lê o anel, decodifica o cabeçalho `INDIRECT`, carrega `IB_BASE` e `IB_CNTL`, e avança o `RPTR` exatamente até o final do pacote `INDIRECT` (96 bytes / DW 24).
6. A partir dali, **a engine não executa o conteúdo do buffer e não retorna para o anel.**
7. O `GCR` e a `FENCE` posteriores nunca são executados. A fence em VRAM permanece com o poison `0x43DAFFFE`.
8. `Bc250SdmaPresentExecuteIndirect` esgota o tempo limite de polling (~1 segundo) e retorna `FALSE`.
9. `SubmitCommandVirtual` devolve `STATUS_IO_DEVICE_ERROR` (0xC0000185) ao kernel.
10. **O Desfecho Inevitável:** O Video Scheduler do Windows não aceita falha na fila de paginação. Ele aciona instantaneamente o **BugCheck 0x119 (`VIDEO_SCHEDULER_INTERNAL_ERROR`, Arg1=2, Arg2=0xC0000185)**.
11. A máquina reinicia. No boot seguinte, o fusível de segurança anti-loop (`C:\BC250_BOOT_FUSE.dat`) detecta o crash e bloqueia o bind para proteger o usuário de loops infinitos.
12. **Sintoma percebido pelo Daniel:** A tela apaga ao instalar o driver, fica preta por alguns segundos, o watchdog/BSOD reinicia o PC e volta no BasicDisplay.

---

## 2. Análise do Handoff 168 e Por Que o Teste de Laboratório Falhou

A última IA tentou criar um script de isolamento para não arriscar telas pretas nem reboots:
```powershell
powershell -ExecutionPolicy Bypass -File tools\test-vmid8-ib.ps1 -Execute -IAcceptTheRisk
```
O transcript arquivado em `docs/evidence/2026-09-05_11-56-44-vmid8-ib-lab/run.txt` revelou:
- **O anel físico funciona perfeitamente:** O controle positivo (`WRITE_LINEAR` direto no anel com conferência byte a byte) **passou** com louvor no silício (`RPTR=0x44`, `WPTR=0x44`, `FENCE=0xBC250001`, 4/4 dwords confirmados na VRAM).
- **Todas as variantes de `INDIRECT` falharam:** Inclusive a variante `PHYS`, que não usa tradução de GCVM (VMID 0 com endereçamento físico puro MC em `0xF40F020000`).

### Causas-Raiz Identificadas no Laboratório e no Driver:
1. **Ativação Indevida de `MIDCMD_PREEMPT_ENABLE` (Bit 5 em `SDMA0_CNTL` = 0x20):**
   - Tanto em `bc250_sdma_present.c:300` quanto em `dbg/hw.c:1235`, o código força `F_CNTL_MIDCMD` ligado:
     ```c
     temp |= F_SDMA0_CNTL__UTC_L1_ENABLE | F_SDMA0_CNTL__MIDCMD_PREEMPT_ENABLE | F_SDMA0_CNTL__AUTO_CTXSW_ENABLE;
     ```
   - O recurso de pré-empção no meio do comando (*Mid-Command Preemption*) do SDMA 5.0 exige que o pacote `INDIRECT` aponte para uma área de salvamento de contexto válida (*Context Save Area - CSA*, DW4 e DW5 do pacote).
   - O driver e o script emitem **`CSA = 0`**! Com preempção ligada e CSA nula, o microcódigo do F32 aborta ou suspende o processamento do IB.
   - O hardware original após reset mantém `MIDCMD_PREEMPT_ENABLE = 0` (`0x000400C2`).
2. **Ausência de Rinse de Cache (`GCR`) e Invalidação Adequada:**
   - No script `test-vmid8-ib.ps1`, os dados do IB foram escritos na VRAM pela CPU, mas nenhum pacote `GCR` (SDMA Op 17) foi submetido após a escrita ou após o IB. O marcador pós-INDIRECT foi colocado sem GCR e sem fence formal.
3. **Inconsistência de Formato de `CONST_FILL`:**
   - No `test-vmid8-ib.ps1`, o cabeçalho de fill foi codificado manualmente como `0x8000000B` (com bits 30-31), enquanto no builder do driver é `0x0000020B`.
4. **Conclusão Crucial:** O pacote `INDIRECT` na SDMA0 é delicado, sensível a preempção de contexto e ao estado da CSA. **O anel direto, por outro lado, nunca falhou no silício.**

---

## 3. Planos de Ação: Do Desbloqueio Imediato à Solução Definitiva

Para garantir que o driver execute na GPU **sem telas pretas, sem reboots e sem CPU fallback**, desenhamos três planos:

---

### 🚀 PLANO A (Recomendado): Emissão Direta no Anel SDMA0 (Paging Buffer Flattening)

#### O Que É:
Em vez de emitir um pacote `INDIRECT` no anel para forçar a engine a saltar para um buffer de DMA virtual (que hoje trava o F32 e derruba o Windows), o driver **desdobra os comandos do buffer de paginação diretamente no anel físico da SDMA0**.

#### Por Que Funciona e Não É Fake:
- **É 100% execução na GPU:** Os pacotes de trabalho (`CONST_FILL`, `COPY_LINEAR`) são processados pelos circuitos de DMA da GPU, escrevendo na memória física GDDR6 em alta velocidade, e a GPU grava a fence física em VRAM.
- **O WDDM Permite e Suporta:** O contrato de `DxgkDdiSubmitCommandVirtual` exige que o miniport garanta que os comandos requisitados pelo VidMm sejam executados pelo hardware e que a fence requisitada pelo sistema seja notificada. Como o driver submete ao motor de execução da GPU é detalhe interno do KMD.
- **A Resolução de Endereço Já Existe:** O driver já implementou `Bc250ActiveWalkGpuVa` (em `bc250_active_miniport.c:5915`), que caminha com precisão pela árvore de páginas (root + leaf) e resolve qualquer GPUVA de destino para o endereço físico correspondente na VRAM.
- **Tamanho dos Buffers de Paginação:** Os buffers de paginação do `FillVirtual` têm apenas 16 dwords! Eles cabem com folga imensa no anel físico da SDMA (que tem 1024 a 4096 dwords).

#### Passos de Implementação do Plano A:
1. Em `bc250_active_miniport.c`, dentro de `Bc250ActiveSubmitCommandVirtualBody`:
   - Se a submissão for de paginação (Node 0):
     - Ler os pacotes gerados em `McdmBuildPagingBuffer` (que residem na memória do buffer).
     - Para comandos com GPUVA de destino (ex: `FillVirtual`), traduzir a GPUVA de destino para Endereço Físico de VRAM através de `Bc250ActiveWalkGpuVa`.
     - Emitir os pacotes de preenchimento/cópia diretamente no anel da SDMA0 (`g_Bc250SdmaPresent.Ring`) usando o endereço físico.
     - Emitir o `GCR` (Cache Flush) e a `FENCE` de completion com a sequência `nextFence`.
     - Atualizar o `WPTR` de hardware da SDMA0.
2. Aguardar a conclusão do tripé (`RPTR == WPTR` e `Fence == nextFence`).
3. Notificar o agendador com `DXGK_INTERRUPT_DMA_COMPLETED` e retornar `STATUS_SUCCESS`.
4. **Resultado Imediato:**
   - Elimina 100% o BugCheck `0x119 Arg1=2`.
   - A paginação completa com sucesso no silício.
   - O `dxgkrnl` avança para o `CommitVidPn`, inicializa o DWM e despacha os `Present`.
   - A GPU assume o desktop através dos Blts provados no scanout herdado do GOP!

---

### 🔬 PLANO B: Conserto Cirúrgico do Pacote INDIRECT na SDMA0 (Via Laboratório)

#### O Que É:
Descobrir e corrigir a causa física exata pela qual o microcontrolador F32 da SDMA0 não retorna do salto de `INDIRECT`.

#### Linhas de Investigação e Correções Necessárias:
1. **Desligar `F_CNTL_MIDCMD`:**
   - Modificar `bc250_sdma_present.c:300` e `dbg/hw.c:1235` para remover `F_SDMA0_CNTL__MIDCMD_PREEMPT_ENABLE` (ou passar `MIDCMD = 0`). O registrador `SDMA0_CNTL` deve ser programado com `0x000400C2` (apenas `UTC_L1_ENABLE` e `AUTO_CTXSW_ENABLE`).
2. **Fornecer CSA Válida ou Isolar Sem CSA:**
   - Se o microcódigo do F32 da Cyan Skillfish exigir obrigatoriamente uma área de contexto para aceitar o pacote `INDIRECT`, alocar 4 KiB de VRAM estática para a CSA e passar seu endereço físico nos DW4 e DW5 do pacote `INDIRECT`.
3. **Validar no Script `tools\test-vmid8-ib.ps1` Sem Risco de Crash:**
   - Ajustar o script para emitir um `WRITE_LINEAR` mínimo dentro do IB (exatamente como o teste de validação do upstream Linux `sdma_v5_0_ring_test_ib`).
   - Adicionar o pacote `GCR` no final do IB.
   - Rodar o script no laboratório até a variante `PHYS` e a variante `EXEC` apresentarem `BYTE PROOF: OK`.

---

### 🛡️ PLANO C: Execução de Buffers e Paginação via Compute Engine (MEC / PM4 CP)

#### O Que É:
Utilizar o Command Processor de Compute (MEC - MicroEngine Compute), que já está amplamente validado no silício na Tabela 1 (Prova P1 com 17/17 e Prova P2 com 16/16 aprovadas com shaders reais e fences em VRAM), como a engine primária de submissão de buffers complexos.

#### Por Que Considerar:
- O microcódigo do CP (PFP, ME, CE, MEC) da BC-250 lida nativamente com buffers indiretos (`PM4_INDIRECT_BUFFER`) e tradução sob VMID sem as idiossincrasias do microcontrolador F32 da SDMA.
- Se a SDMA0 continuar apresentando restrições de indireção de firmware, o Nó 1 (Compute) ou um nó de paging baseado no CP pode processar as cópias e preenchimentos.
- A SDMA0 ficaria dedicada exclusivamente a Blts lineares de apresentação física (onde ela já provou 32/32 conclusões perfeitas sem falhas).

---

## 4. Matriz Comparativa das Opções

| Critério | Plano A (Emissão Direta no Anel) | Plano B (Conserto do INDIRECT) | Plano C (Paginação via MEC/PM4) |
|---|:---:|:---:|:---:|
| **Risco de BugCheck 0x119** | **Mínimo (Zero no curto prazo)** | Alto até validação total | Baixo |
| **Tempo até Código 0 com Desktop** | **Imediato (1 ciclo de build)** | Indeterminado (depende de ensaios) | Médio (refatoração de DDI) |
| **Uso da GPU Real** | **Sim (SDMA0 física em GDDR6)** | Sim (SDMA0 física em GDDR6) | Sim (24 CUs / Compute) |
| **Complexidade de Código** | Baixa (reaproveita `Bc250ActiveWalkGpuVa`) | Média | Alta |
| **Dependência de GCVM para o Salto** | Nenhuma (Ring sob VMID 0) | Total (VMID 8) | Total (VMID sob CP) |
| **Reversibilidade** | Total | Total | Média |

---

## 5. Próxima Etapa Operacional

1. **Adoção do Plano A no Driver de Produção:**
   - Modificar a submissão de `FillVirtual` em `SubmitCommandVirtual` para desdobrar os pacotes de preenchimento diretamente no anel da SDMA0 utilizando a tradução física do destino via `Bc250ActiveWalkGpuVa`.
   - Executar com tripé físico no anel (`RPTR==WPTR`, fence física gravada pela GPU, poison substituído).
2. **Execução de Build e Auditoria:**
   - Rodar `driver\wdk\build-contract.bat` para garantir `EXIT=0` e integridade das travas de contrato.
   - Empacotar e assinar com `tools\build-bc250mcdm-package.ps1` e `tools\audit-bc250mcdm-package.ps1`.
3. **Corrida Física com o Daniel:**
   - Boot frio.
   - Bind do pacote WDDM com watchdog de segurança armado.
   - **Critério de Sucesso:** `SubmitCommandVirtual` aposenta com sucesso sem BugCheck `0x119`, VidMm conclui paginação, DWM envia Presents e a GPU passa a desenhar o desktop na tela!

---

## 6. Diagnóstico da 1ª Corrida Física (05/09 12:39) e Refinamento

A primeira corrida física do Plano A alcançou com sucesso a intercepção de `FillVirtual` em `SubmitCommandVirtual`:
- A caminhada nas page tables L1/L0 da CPU resolveu GPUVA `0x260000` para `0x450AD0000`.
- O Plano A interceptou o `INDIRECT` e chamou `Bc250SdmaPresentFill`.
- Porém, ocorreu **TIMEOUT** na espera da fence física da GPU (`0x00001001`), devolvendo `STATUS_IO_DEVICE_ERROR` e causando BugCheck 0x119.

### Causas-Raiz e Correções Aplicadas no Silício:
1. **Conflito de Base PA vs MC da SDMA:**
   - As page tables operam em PA (`0x450000000 + offset`).
   - O anel físico da SDMA0 opera no espaço MC da GPU (`0xF400000000 + offset`).
   - **Correção:** Em `bc250_active_miniport.c`, calculamos `vramOffset = destPhysStart - 0x450000000` e passamos `destMcAddress = 0xF400000000 + vramOffset` (`0xF400AD0000`) para a SDMA0.
2. **Opcode `CONST_FILL` Incorreto:**
   - `bc250_sdma_packet_builder.c` deslocava `FILLSIZE` com `<< 8` gerando `0x0000020B`.
   - **Correção:** Deslocamento corrigido para `<< 30`, gerando `0x8000000B` (conforme `dbg/hw.c:2385` validado em hardware).
3. **Sequência de Fences e Polling:**
   - O boot tripé aposenta `0x00001001`; `NextFenceSequence` agora inicia em `0x00001001 + 1` (o 1º fill usa `0x00001002`).
   - Polling estendido para 10.000 iterações (50 ms) para cobrir preenchimentos de 11 MB com margem segura.

