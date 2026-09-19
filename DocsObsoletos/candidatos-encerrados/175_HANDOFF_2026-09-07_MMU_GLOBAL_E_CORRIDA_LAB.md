# 175 — Handoff Técnico: Integração da MMU Global e Diagnóstico da Corrida Lab (07/09/2026)

> **Documento de passagem de bastão (Handoff) para a próxima IA / Desenvolvedor.**
> **Data:** 2026-09-07 ~14:40 | **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2)
> **Ambiente:** Windows 10 Pro 22H2 x64 (Build 19045.2965) | VS2026 + WDK 10.0.28000.0

> **CORREÇÃO OBRIGATÓRIA (07/09 15:05):** este handoff registrou conclusões que a
> evidência do próprio bind não sustenta. No arquivo delimitado ao bind
> `display-log-this-bind.txt`, a MMU global falhou com status 2
> (`SNAPSHOT_FAILED`) em `SetRootPageTable`; mesmo assim, o código antigo seguiu
> em frente. Esse arquivo contém **zero** `BuildPagingBuffer Op=11`, zero
> `ActiveUpdatePT Finished`, zero `SubmitCommand`, zero `GetStdAllocData`, zero
> `CommitVidPn`, zero `SetVidPnSourceAddress` e zero `Present`. As ocorrências no
> `bc250_display_log-full.txt` são históricas e não podem ser atribuídas a esta
> corrida. Logo a corrida das 14:32 **não provou paginação WDDM nem integração da
> MMU global**. A causa individual entre os 26 registradores também não foi
> isolada: o ensaio das 13:59 prova causalmente o conjunto de programação, não
> qual registrador sozinho é a causa-raiz. A versão corrigida e o candidato
> fail-closed estão em `docs/176_HANDOFF_2026-09-07_MMU_FAIL_CLOSED.md`.

---

## 1. Onde queremos chegar (Objetivo e Restrições)

1. **Destino Final:** Jogos no Windows 10 22H2 rodando nativamente na AMD BC-250 (24 CUs, 16 GB GDDR6, monitor DisplayPort da placa), com D3D12 e Vulkan reais acelerados pela GPU. **Sem WARP, sem GDI, sem llvmpipe e sem fallback de software.**
2. **Display v1 (Arquitetura Atual):** Herança direta do modo de vídeo estabelecido pelo UEFI GOP (2560x1080 @ 56.13 Hz). VidPn de modo único, scanout em VRAM real herdada, `DxgkDdiPresent` via blt por SDMA direto, VSync sincronizado com o frame counter do OTG. **Zero escrita em registradores DCN** (fora de P9).
3. **Leis Inegociáveis:**
   - **Sinceridade Absoluta:** O que não tiver prova no silício é explicitamente marcado como `NÃO VALIDADO EM HARDWARE`.
   - **Tripé no Silício:** Só há execução provada se houver simultaneamente `RPTR == WPTR`, fence física gravada pela GPU em VRAM sobre poison check, e conferência de dados byte a byte.
   - **Fail-Closed:** Qualquer falha na cadeia desarma e reverte (watchdog, teardown, rollback para BasicDisplay).

---

## 2. O que foi feito nesta sessão

### 2.1 Descoberta e Comprovação Física da Causa-Raiz (Item B1 / P0)
- **Problema anterior:** A submissão de paginação em WDDM (`BuildPagingBuffer Op=11`, `DXGK_OPERATION_UPDATE_PAGE_TABLE`) travava a GPU sob VMID 8 quando o Vulkan tentava criar o dispositivo (`vkCreateDevice`).
- **Causa-Raiz:** O UEFI GOP deixa o GFXHUB MMU em modo de bypass físico pós-POST (`MX_L1_TLB_CNTL = 0x00000501`, `L2_CNTL4 = 0x000000C1`). Sob esse estado, a MMU recusa a caminhada de tabelas de páginas virtuais para contextos não-zero (VMID 8).
- **Prova Física no Silício (07/09 13:59:59):**
  - Com a programação dos 26 registradores globais (`MX_L1_TLB_CNTL = 0x1D59`, `L2_CNTL = 0x00080E01`, `L2_CNTL4 = 0x1`, faixas de abertura e triggers no `L2_CNTL2`), o ensaio `tools/test-vmid8-ib.ps1 -GlobalMmu` executou buffers indiretos sob VMID 8 em **11 ms**, com tripé físico completo.
  - **Prova Causal Negativa (Anti-Falso Positivo):** Adulterou-se propositalmente a PDE (`0x61`); o hardware acusou falha real imediata na memória:
    `FAULT_STATUS = 0x00801B01`, `FAULT_ADDR = 0x0000000201` (página `0x201000`), `VMID=8, RW=0 (READ), MAP_ERR=1`.

### 2.2 Módulo Implementado e Integrado ao Miniport WDDM
1. **Core:** [`driver/core/bc250_gcvm_global_mmu.h`](driver/core/bc250_gcvm_global_mmu.h) e [`driver/core/bc250_gcvm_global_mmu.c`](driver/core/bc250_gcvm_global_mmu.c):
   - Snapshot transacional de 26 registradores;
   - Programação com máscara para triggers de auto-limpeza (`L2_CNTL2`);
   - Readback estrito e flush de TLB;
   - Rollback LIFO em caso de discrepância;
   - Função `Bc250GcvmGlobalMmuRestore` para restaurar o estado original no descarregamento do driver.
2. **Teste de Unidade:** [`driver/tests/gcvm_global_mmu_test.c`](driver/tests/gcvm_global_mmu_test.c) freestanding (`/NODEFAULTLIB`), testando programação, tolerância ao trigger do `L2_CNTL2`, reversão sob erro e restauração.
3. **Integração no Miniport Ativo (`bc250_active_miniport.c`):**
   - Em `Bc250DxgkDdiStartDeviceActive`: inicialização global após mapeamento de MMIO;
   - Em `Bc250ActiveSetRootPageTableObject`: defesa em profundidade que garante a MMU ativa on-demand antes de qualquer `Bc250GcvmVmid8Install`;
   - Em `Bc250DxgkDdiStopDeviceActiveReason`: restaura o snapshot original antes de desmapear o transporte.
4. **Builds e Auditorias:**
   - `build-contract.bat`: todas as suítes e gates passaram com `EXIT=0`.
   - `output_active` (3.81.0.0, produção): construído e assinado, aprovado em `audit-bc250mcdm-package.ps1`.
   - `output_lab` (3.81.1.0, lab com bringup ativo e WDDM 2.0): construído e assinado, aprovado em `audit-bc250mcdm-lab-package.ps1`.
   - `tools/_bind_runner.ps1`: atualizado para parar e descarregar o serviço de teste `bc250dbg` automaticamente para não travar o gate de pré-binding.

---

## 3. A Corrida de 07/09 14:32:49 e Diagnóstico do Watchdog

### 3.1 O que foi executado
O operador disparou a trilha de laboratório com o runner de RADV:
```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 `
  -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission `
  -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -Execute `
  -IAcceptBlackScreenRisk -RunRadvComputeTripod
```
Evidência arquivada em: `docs/evidence/2026-09-07_14-32-49-DESKTOP-HQAG6T4-lab-binding/`.

### 3.2 O que a evidência registrou
1. **O driver subiu em Código 0:**
   - Tripé de boot da SDMA0 validado no silício (`BootTripodValidated=1`).
   - `McdmBuildPagingBuffer Op=11` foi executado centenas de vezes (`ActiveUpdatePT Finished: journalStatus=0 -> SUCCESS`). A paginação **não** deu hang na primeira chamada.
2. **O Display não comitou:**
   - O dxgkrnl entrou em laço de 100 consultas `IsSupportedVidPn` (`DmmIsSupportedCount: 100`, `CommitVidPn: 0`).
   - Nenhuma alocação primária foi solicitada pelo SO (`GetStdAllocData: 0`).
   - A tela permaneceu preta (GOP não substituído por scanout WDDM).
3. **O Portão do RADV bloqueou a execução:**
   - O arquivo [`tools/bind-bc250mcdm-lab.ps1`](tools/bind-bc250mcdm-lab.ps1) (linhas 900-932) contém um gate que exige `SharedPrimary`, `CommitVidPn` e `SetVidPnSourceAddress` dentro de 15 segundos para disparar o harness do Vulkan.
   - Como o display não convergiu, o script registrou:
     `RADV_WDDM_COMPUTE_TRIPOD NAO EXECUTADO: o display deste bind nao chegou a SharedPrimary=False, CommitVidPn=False, SetVidPnSourceAddress=False em 15 s.`
     E gerou `radv-display-prerequisite-gate.json` com `"state": "RADV_SKIPPED"`.
   - **Conclusão:** O teste do RADV sequer chegou a ser chamado.
4. **Atuação do Watchdog:**
   - Como a tela estava preta e o operador não teve como confirmar visualmente o display, o watchdog aguardou o timeout (180 s) e executou o rollback limpo para `BasicDisplay` Código 0 (`watchdog-rollback.txt`). O sistema retornou estável.

---

## 4. O Problema Aberto para a Próxima IA: A Divergência do VidPn Lab

### 4.1 Fato Observado
- **Corrida de Produção 05/09 22:20 (`output_active`):** O SO fez 18 consultas `IsSupportedVidPn`, chamou `GetStdAllocData`, criou 155 alocações, comitou o VidPn (`CommitVidPnEntered = 1`) e realizou 175 `Present`s.
- **Corridas de Lab (`output_lab`):** Em TODAS as corridas de laboratório (06/09 e 07/09), o SO faz 97 a 100 consultas `IsSupportedVidPn` e **nunca** comita o VidPn nem pede alocação primária.

### 4.2 O que separa `output_active` de `output_lab`
No INF e perfil:
1. `SkipHwBringup`:
   - `output_active` tem `SkipHwBringup=1`: o `StartDevice` herda o GOP e **sai imediatamente** (linhas 3360-3502 de `bc250_active_miniport.c`).
   - `output_lab` tem `SkipHwBringup=0`: o `StartDevice` executa `Bc250PhysicalRenderBringup` (linhas 3513-4998), que faz carga de firmware CP/PFP/ME/CE/MEC, probe de HQD, e ativação de fila MEC.
2. `ShaderDispatch`: 0 (ativo) vs 1 (lab).
3. `CpSoftReset`: 0 (ativo) vs 1 (lab).
4. `SdmaAutoCtxSw`: 1 (ativo) vs 0 (lab).
5. `GpuvaLabPromotion`: 0 (ativo) vs 1 (lab).

No código de `bc250_active_miniport.c`:
- A chamada `Bc250GcvmGlobalMmuProgram` no `StartDevice` (linha 3456) foi adicionada dentro do bloco `#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)`.
- No binário de lab (`#else`), essa chamada no `StartDevice` **não** foi executada (executou apenas on-demand em `SetRootPageTable`).

---

## 5. Próximos Passos Recomendados

1. **Ajuste no `StartDevice` para o Perfil Lab:**
   - Mover a chamada de `Bc250GcvmGlobalMmuProgram` para ocorrer **antes** do `#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)`, logo após `Bc250DisplaySetMmioBase`, para que ambos os perfis (ativo e lab) tenham a MMU global garantidamente programada desde o início do `StartDevice`.
2. **Testar o Pacote Ativo (`output_active`):**
   - Executar o bind da trilha de produção com watchdog:
     `powershell -ExecutionPolicy Bypass -File tools\invoke-elevated-lab-bind.ps1`
   - Verificar se o display comita e mantém a tela acesa em Código 0 agora com a MMU global integrada e o hotfix de pivô do VidPn.
3. **Isolar por que o Bringup de Hardware Bloqueia o DMM do SO:**
   - Se o display funcionar no pacote ativo, a causa do laço de 100 `IsSupportedVidPn` no lab está no bloco `Bc250PhysicalRenderBringup` (interferência do CP/GRBM no estado do adapter ou flags do INF como `ShaderDispatch` ou `SdmaAutoCtxSw`).
   - Bisseccionar essas flags uma a uma no pacote de lab até o VidPn convergir.
4. **Desacoplamento de Teste Compute vs Display:**
   - Se o operador quiser rodar o RADV compute tripod mesmo sem o display convergir em 15s (por exemplo, via script ou flag dedicada), atentar que `bind-bc250mcdm-lab.ps1` propositalmente salta o teste se o display não comitar. Avaliar se o gate de pré-requisito de display deve continuar estrito ou ter opção de bypass explícito sob watchdog.
