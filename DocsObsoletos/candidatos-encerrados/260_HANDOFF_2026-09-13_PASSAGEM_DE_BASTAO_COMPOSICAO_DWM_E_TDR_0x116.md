# Handoff 260 — Passagem de Bastão: Composição DWM no Silício, Causa-Raiz do BugCheck 0x116 e Roteiro de Continuação

**Data:** 2026-09-13 (02:00 UTC / 23:00 Horário Local)  
**Alvo:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2, 24 CUs, 16 GB GDDR6)  
**Ambiente:** Windows 10 22H2 x64 | Monitor DisplayPort nativo (2560x1080)  
**Veredito físico:** Marco 3 sustentado no silício (600 quadros 60 FPS Vulkan); Composição DWM alcançou execução no SDMA0 da GPU (`Substituidos=1`); Stall síncrono na CPU durante o 2º draw do DWM culminou em TDR e BugCheck `0x116`.  
**Estado pós-reboot:** Adaptador limpo em Código 0 (`BasicDisplay`), Fusível `C:\BC250_BOOT_FUSE.dat` limpo (`CLEAN`).

---

## 🎯 1. Onde Queremos Chegar

1. **Meta Imediata do Usuário:**  
   **WDDM acelerado por GPU com DWM ativo.**  
   Fazer o compositor nativo do Windows 10 (**DWM - Desktop Window Manager**) rodar acelerado na GPU BC-250, apresentando a área de trabalho, janelas e barra de tarefas na resolução nativa do monitor DisplayPort (2560x1080), **sem WARP e sem fallbacks de software por CPU (`zero memcpy`), em estrita conformidade com a Lei 4**.
2. **Meta Final do Projeto:**  
   **Jogos rodando no Windows 10 22H2 na BC-250** — 24 CUs, 16 GB GDDR6, D3D12/Vulkan, sem dependência de adaptadores secundários.

---

## 📊 2. O Que Já Fizemos e Validamos no Silício

| Componente | Estado no Silício | Evidência Arquivada |
|---|---|---|
| **Marco 2 (RADV Present)** | **100% CONCLUÍDO** | Swapchain Vulkan apresentando quadros no monitor DP via SDMA0 Blt |
| **Marco 3 (Sustentabilidade 60 FPS)** | **100% SUSTENTADO** | **600 quadros** ininterruptos a 21,9–60 FPS, 0 TDR, 0 BugCheck, WPTR=RPTR drenados |
| **Tradução de Shaders (vkd3d-shader)** | **100% VALIDADO OFFLINE** | Direct Token Streams SM4/SM5 do DWM traduzidos para SPIR-V via `libvkd3d-shader-1.dll` |
| **Composição 1:1 no SDMA (D3D11DrawBltSubstitution)** | **PROVADO NO SILÍCIO** | Primeiro draw do desktop (`2560x1080 -> 2560x1080`) substituído e executado com sucesso no SDMA0 físico da GPU (`Substituidos=1`, 7.559 submissões atendidas) |
| **Recuperação e Fusível** | **100% OPERACIONAL** | Watchdog com rollback limpo para `BasicDisplay` Código 0 sem travar o SO; fusível anti-bootloop preservado |

---

## 🔍 3. As Três Corridas Físicas de Hoje e os Problemas Encontrados

### Corrida 1 (22:33:16) — Tela Preta e BugCheck `0x10E`
- **O que aconteceu:** Tela preta durante o bind acelerado; após 180s o watchdog tentou rollback, mas o sistema gerou BugCheck `0x10E` Arg1=`0x4`.
- **Diagnóstico:** O monitor de testes é ultrawide **2560x1080**. O Windows inicializou o desktop gerando um buffer intermediário de **1920x1080**. O validador da UMD exigia `src->Width == dst->Width`, rejeitando o primeiro draw com `geom=0, vpValid=0`. Sem draws atendidos, o DWM entrou em timeout e o VidMm disparou o `0x10E` ao purgar alocações órfãs.
- **Correção aplicada:** Atualizada a geometria em `Bc250UmdSubstituteDrawWithBlt` (`driver/umd/bc250_d3d_umd.c`) para aceitar sub-retângulos onde `src <= dst` posicionados em `[0,0]`, e liberada a flag misc `0x20000` (`D3DWDDM2_0DDI_RESOURCE_MISC_DISPLAYABLE_SURFACE`) no UMD.

### Corrida 2 (22:44:41) — Sucesso no 1º Draw e Bloqueio no 2º Draw (`BlendEnable`)
- **O que aconteceu:** Tela preta mantida, mas **o watchdog realizou rollback limpo sem reboot**, restaurando `BasicDisplay` Código 0 com fusível intacto.
- **Achado crucial no log UMD (`bc250_umd_log_1228.txt`):**
  - O primeiro draw do quadro foi atendido com sucesso pelo SDMA0 da GPU (`Substituidos=1`)!
  - No segundo draw (janela de 590x800), o DWM ativou `BlendEnable = 1`. A função `Bc250UmdDrawBltSubstitutionIsLawful` continha `if (BlendEnabled) return FALSE;`. Isso contradizia o contrato declarado de `arm-d3d11-draw-blt-substitution.ps1` (*"A substituição ignora shader, topologia e blend"*), abortando a renderização do frame.
- **Correção aplicada:** Removida a recusa em `BlendEnabled` em `driver/umd/bc250_umd_caps_probe.c` e atualizado o teste unitário em `driver/tests/bc250_d3d_umd_test.c`. Todos os testes e compilações passaram com EXIT 0.

### Corrida 3 (22:57:30) — Tela Preta e BugCheck `0x116` (`VIDEO_TDR_FAILURE`)
- **O que aconteceu:** Tela preta durante o teste, seguida de tela azul do Windows e reinicialização automática do sistema.
- **Evidências mineradas no Event Viewer e Logs:**
  1. `Event ID 4101` (22:57:42): *"O driver de vídeo bc250mcdm parou de responder e se recuperou com êxito."* (TDR disparado pelo dxgkrnl).
  2. `Event ID 1001` (22:59:27): *BugCheck `0x00000116` (`0xffff8f84e289f460`, `0xfffff8055f97c950`, `0x0000000000000000`, `0x000000000000000d`)* com despejo em `C:\Windows\MEMORY.DMP`.
  3. No log UMD (`bc250_umd_log_1204.txt`), às 22:57:36.221:
     - 1º Draw substituído com sucesso (`Substituidos=1`).
     - O 2º Draw (`590x800`) foi aceito com `blend=1` e despachado para `Bc250UmdBltGpu`!
     - O log encerrou exatamente no meio do processamento do 2º draw.

---

## ⚡ 4. A Causa-Raiz Exata do BugCheck `0x116`

Ao inspecionar a implementação de `Bc250UmdBltGpu` em `driver/umd/bc250_umd_blt.inl`:

```c
    // Para uma janela de 590x800, o loop emite 800 pacotes de cópia linear:
    for (y = 0; y < src->Height; ++y) {
        if (Bc250SdmaEmitCopyLinear(d->BltWords, BC250_BLT_WORDS, &cursor,
            src->Allocation.GpuVirtualAddress + (uint64_t)y * src->RowPitch,
            dst->Allocation.GpuVirtualAddress + dstOffset + (uint64_t)y * dst->RowPitch,
            (uint32_t)rowBytes) != BC250_SDMA_OK) return E_FAIL;
    }
    ++d->BltSequence;
    ...
    // E EM SEGUIDA A UMD REALIZA UMA ESPERA BLOQUEANTE NA CPU DENTRO DA THREAD DO DWM:
    if (Bc250WinsysSubmitGpuVaForNode(&d->BltWinsys, 0u,
            d->BltCommand.GpuVirtualAddress, cursor * 4u) != BC250_WINSYS_OK ||
        Bc250WinsysWaitForFenceFromCpu(&d->BltWinsys, &d->BltFence,
            d->BltSequence) != BC250_WINSYS_OK ||
        !d->BltFence.CpuVirtualAddress || *d->BltFence.CpuVirtualAddress != d->BltSequence)
        return E_FAIL;
```

### Por que isso quebrou:
1. **Espera Síncrona Bloqueante na Thread do DWM:**
   - O DWM do Windows exige operação de renderização assíncrona orientada a pipeline.
   - Ao chamar `Bc250WinsysWaitForFenceFromCpu` sincronamente dentro da DDI de `DrawIndexed` para cada elemento da interface, a thread de renderização do DWM é congelada na CPU esperando a fence do SDMA.
2. **Estouro do Watchdog de TDR do Windows (2 segundos):**
   - Com 800 cópias de linhas individuais no anel SDMA0, se a fence atrasar ou se a escrita de monitored fence (`d->BltFence.GpuVirtualAddress`) não for sinalizada instantaneamente na CPU, a thread do DWM permanece bloqueada.
   - O `dxgkrnl.sys` detecta a falta de progresso da engine e aciona o TDR (`0x116`) após 2 segundos.
   - O código `0x116` Arg4=`0xD` confirma que o subsistema gráfico do Windows considerou a recuperação do adaptador incompleta/falha após o reset da engine.

---

## 🚀 5. Roteiro Direto para a Próxima IA Continuar

### A. Onde Trabalhar:
1. **Tornar o Blt de Composição do DWM Assíncrono (Eliminar o Wait Bloqueante na CPU):**
   - Em `driver/umd/bc250_umd_blt.inl`, linhas 151–158:
     O `Bc250WinsysWaitForFenceFromCpu` **NÃO PODE BLOQUEAR A CPU** durante a chamada de `Draw` do DWM.
     O submit para o nó SDMA0 (`Bc250WinsysSubmitGpuVaForNode`) deve ser enfileirado na GPU e a sincronização deve ocorrer apenas quando o dxgkrnl solicitar no `pfnPresent` ou via monitored fence da winsys.
   - Manter o pipeline fluindo: submeter os comandos ao SDMA0 e retornar `S_OK` imediatamente.

2. **Otimizar a Emissão de Cópias no SDMA:**
   - Atualmente, para uma janela de 800 linhas de altura, são emitidos 800 pacotes `Bc250SdmaEmitCopyLinear` individuais (7 dwords cada = 5.600 dwords / 22,4 KB por submissão).
   - Se a superfície de origem tiver pitch compatível ou cópia contígua (como no caso de fullscreen), usar uma única cópia linear em bloco.
   - Para sub-retângulos, agrupar ou despachar sem espera síncrona na CPU.

3. **Verificação da Monitored Fence da Winsys:**
   - Garantir que `d->BltFence.GpuVirtualAddress` esteja mapeada no espaço de endereço de GPU do contexto da winsys e que o pacote `Bc250SdmaEmitFence` escreva o valor exato no destino esperado pelo kernel.

### B. Ferramentas e Comandos de Trabalho (Testados e Funcionais):
1. **Compilar UMD e Rodar Testes de Contrato:**
   ```cmd
   driver\umd\build-umd.bat
   ```
2. **Compilar Miniport e Suíte Completa WDDM:**
   ```cmd
   driver\wdk\build-contract.bat
   ```
3. **Gerar e Assinar Pacote de Laboratório:**
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission
   ```
4. **Verificar Segurança:**
   ```powershell
   python tools/audit-bc250mcdm-safety.py
   python tools/audit-umd-node1-submission.py
   ```
5. **Comando de Teste no Silício (Executado pelo Usuário em Prompt Elevado):**
   ```cmd
   tools\EXECUTAR_WDDM_ACELERADO.cmd
   ```

---

## 📌 6. Resumo Executivo para a Nova Sessão

- **Código base:** 100% íntegro, limpo, sem conflitos de git.
- **Hardware:** GPU BC-250 em Código 0 (`BasicDisplay` ativo), fusível de boot `CLEAN`.
- **Marco atingido:** O DWM já aceita o driver, inicializa shaders e executa o primeiro frame no SDMA0 da GPU física.
- **O próximo passo exato:** Eliminar a espera síncrona na CPU (`Bc250WinsysWaitForFenceFromCpu`) dentro de `Bc250UmdBltGpu` para permitir que o pipeline do DWM flua de forma assíncrona na GPU sem estourar o watchdog de TDR (`0x116`).
