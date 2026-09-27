# 207 — Handoff 2026-09-08 16:15 · Passagem de Bastão: Disputa de VMID 8 e Próximos Passos

> **Para:** Próximo Agente de IA / Engenheiro de Bring-up  
> **Data-base:** 2026-09-08 16:15  
> **Candidato Pronto:** Lab `3.94.4.0` (SYS `EAF0603F3C3CC68DA66E774347468FC13D2D69752ACC145AAB05B2298C08F39B`, UMD `35FBD78186D300D84F25DB76D1AF1C3025C7FECA3211CA422D35619B116AF214`, CAT `0C7E1E6F...`, INF `05EC304F...`)  
> **Estado Atual do Hardware:** Rollback limpo do watchdog executado; BasicDisplay ativo em Código 0; fusível de boot limpo (`CLEAN 0`).  
> **Handoffs Imediatos Anteriores:** `docs/205` (Faixa B7.1 / MapAllocation) e `docs/206` (Análise da corrida 15:43 e disputa de VMID 8).

---

## 1. Onde Estamos de Verdade (Fatos Medidos no Silício — P0)

Na última corrida de hardware (08/09 15:43, `docs/evidence/2026-09-08_15-43-55-DESKTOP-HQAG6T4-lab-binding/`):

1. **A UMD desatravancou inteira (Faixa B7.1 aprovada):**
   - O retorno `0x8000000A` (`E_PENDING`) de `pfnMapGpuVirtualAddressCb` e `pfnMakeResidentCb` agora é tratado como enfileiramento assíncrono padrão do WDDM 2.0+.
   - Buffers alocados (`VA=0x11000` comando, `VA=0x21000` destino), sincronizados pela cerca de paginação (`WaitPaging` OK).
   - Venenos gravados e lidos, cerca monitorada criada e `pfnSubmitCommandCb` retornou `S_OK`.

2. **Primeiro submit da história no Node 1 (Compute MEC) no KMD:**
   - DDI `DxgkDdiSubmitCommandVirtual` recebeu o pacote:
     `[McdmSubmitCommandVirtual] Node=1, FenceId=1, DmaSize=68`
     `[SubmitVirtual Node1] GATE ABERTO: GpuvaMappingsOperational=1; publicando IB de 68 bytes em VA=0x11000 no anel persistente MEC (FenceId=1)`
   - O WPTR do anel persistente MEC avançou fisicamente de **204 para 216 dwords** no silício (`P1LastWptrMmioReadback`), correspondendo exatamente aos 12 dwords de payload PM4 (`PACKET3_INDIRECT_BUFFER` + `RELEASE_MEM`).
   - O mecanismo de doorbell e o anel MEC funcionam (StartDevice realizou 17 submissões de 12 dwords aposentadas em 1 poll: `P1SubmitAccepted: 17`, `P1RetiredTotal: 17`).

3. **O Tripé do Node 1 falhou fechado:**
   - `P1LastRptrMmio` permaneceu em **204 dwords** (a GPU não consumiu os 12 dwords).
   - `WaitForFenceFromCpu` retornou `CurrentFence = UINT64_MAX` (`18446744073709551615`).
   - Canários em VRAM permaneceram com zeros (`Values=00000000...`).
   - Veredito da UMD: `NODE1_TRIPOD: FAIL-CLOSED poison=1 submit=1 fence=0 data=0 cleanup=1 -- NAO VALIDADO EM HARDWARE`.

4. **TDR / `DEVICE_REMOVED` refutado como causa:**
   - Nenhuma DDI de TDR (`ResetFromTimeout`, `RestartFromTimeout`, `QueryEngineStatus`) disparou no log (0 ocorrências).
   - `WaitForFenceFromCpu` retornou `hr = 0x00000000` (`S_OK`).

---

## 2. A Causa Raiz Identificada no Log: Disputa do VMID 8

No arquivo [`bc250_display_log-full.txt`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-09-08_15-43-55-DESKTOP-HQAG6T4-lab-binding/bc250_display_log-full.txt#L11867-L11893):

- **Linha 11867/11872:** `SetRootPageTable` instala a tabela do harness (`0x45FFB0000`) no registrador de hardware `PT_BASE` do VMID 8 (onde `VA=0x11000` existe).
- **Linha 11875/11877:** Node 1 emite o IB apontando para `VA=0x11000` sob `VMID=8` no anel MEC.
- **Linha 11884..11892:** Apenas 15 linhas depois, a thread concorrente de display (`Present #3`) chama o SDMA (Node 0), detecta troca de contexto e executa:
  `[SubmitVirtual Node0 SDMA] VMID8 CONTEXT SWITCH: Ctx FFFFE68999D486B0 -> FFFFE68999D48500 (Root=0x450A8C000, PT_BASE=0x450A8C000)`
- **Consequência:** O hardware do GFXHUB possui apenas um registrador `PT_BASE` para o VMID 8. Quando a engine MEC foi consumir o pacote sob VMID 8, o registrador já continha `0x450A8C000` (tabela de paginação/DWM), onde `VA=0x11000` **não existe**. Isso causou GPU page fault / paralisou o processamento do MEC, travando o `RPTR` em 204.

---

## 3. O Que Já Foi Feito Nesta Sessão (Offline)

1. **Instrumentação da Disputa do VMID 8:**
   - Em `driver/wdk/bc250_active_miniport.c`:
     Adicionada a telemetria `[SubmitVirtual Node1] VMID8_DISPUTE_PROBE`:
     - `targetPtPhys`: raiz esperada pelo contexto do harness.
     - `instCtx` / `instRoot`: contexto e raiz anotados em software.
     - `hwPtBase`: leitura direta em tempo real de `BC250_GCVM_VMID8_PT_BASE_LO` e `HI`.
     - `MatchHw`: flag de igualdade.
     - Log explícito de publicação: `[SubmitVirtual Node%u] PUBLICADO NO ANEL MEC: FenceId=...`.
2. **Build e Testes Offline:**
   - `build-contract.bat`: **EXIT=0** (0 falhas).
   - `audit-bc250mcdm-safety.py`: **PASS** (17 portões íntegros).
   - `build-bc250mcdm-lab-bringup.ps1`: **EXIT=0** (binário SYS `EAF0603F...`).
   - `package-bc250mcdm-lab.ps1`: Pacote de laboratório construído e assinado com versão `3.94.4.0`.
   - `audit-bc250mcdm-lab-package.ps1`: **PASS**.
3. **Documentação Sincronizada:**
   - `BASE_DE_AVANCO.md`, `AGENTS.md`, `CLAUDE.md`, `GEMINI.md` atualizados com a barra de status canônica.
   - Handoff `docs/206` arquivado.

---

## 4. Onde a Próxima IA Deve Continuar (Bifurcação de Decisão)

A disputa do registrador de hardware `VMID 8` entre o Display/Present (SDMA) e o Compute (MEC) precisa ser resolvida. Três opções estão disponíveis:

### Opção 1: Isolamento de VMID por Hardware (Recomendada)
- O GFXHUB possui 16 VMIDs (VMID 0 a 15).
- Atualmente, tanto SDMA quanto Compute estão amarrados a `VMID 8`:
  - SDMA (Node 0) usa VMID 8.
  - Compute (Node 1) usa `context->SubmissionEngine.ComputeRing.Vmid = 8u;`.
- **Solução Arquitetural:** Alocar um VMID exclusivo para o Node 1 (por exemplo, **VMID 9**):
  - Definir os registradores de hardware de VMID 9 no GFXHUB (`0x0000A224` para CNTL, `0x0000A3F4` para PT_BASE_LO, etc., seguindo os offsets catalogados em `gc_10_1_0_sh_mask.h` / `docs/03`).
  - No `SetRootPageTable`, instalar a raiz do processo de compute no VMID 9 (ou manter VMID 8 para o SDMA e VMID 9 para o Compute).
  - Dessa forma, uma submissão de Present no SDMA **nunca** sobrescreverá a raiz de tradução do Compute MEC.

### Opção 2: Ensaio Diagnóstico Limpo com o Pacote Atual (3.94.4.0)
- Se a próxima IA ou o usuário preferirem validar a telemetria recém-criada antes de mexer na alocação de VMID:
  - O pacote `3.94.4.0` já está pronto em `driver/wdk/package/output_lab/`.
  - Comando para bind (após boot frio):
    ```powershell
    powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod -WatchdogTimeoutSeconds 180
    ```
  - O log capturará `[SubmitVirtual Node1] VMID8_DISPUTE_PROBE` com a medição exata do descompasso no registrador físico.

### Opção 3: Serialização com Lock / Reinstalação de Raiz Pré-Kick
- Sincronizar o lock de submissão do SDMA e MEC para garantir que `SetRootPageTable` / context switch no VMID 8 não aconteça enquanto o MEC estiver com trabalho pendente no anel.
- *Ressalva:* Como o MEC executa de forma assíncrona na GPU, segurar a CPU não impede a GPU de sofrer fault se o próximo Present chegar antes de o MEC terminar de buscar o IB. A Opção 1 (VMID dedicado) é infinitamente mais robusta.

---

## 5. Arquivos Críticos Modificados nesta Sessão

- [`driver/wdk/bc250_active_miniport.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_active_miniport.c#L7075-L7135): `VMID8_DISPUTE_PROBE` adicionado no Node 1.
- [`driver/wdk/package/output_lab/`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/package/output_lab/): Pacote `3.94.4.0` construído, assinado e catalogado.
- [`BASE_DE_AVANCO.md`](file:///c:/Users/Daniel/Desktop/NamixWDDM/BASE_DE_AVANCO.md): Síntese atualizada com a corrida de 15:43 e a descoberta da colisão de VMID 8.
- [`docs/206_HANDOFF_2026-09-08_15-43_PRIMEIRO_SUBMIT_NODE1_E_DISPUTA_VMID8.md`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/206_HANDOFF_2026-09-08_15-43_PRIMEIRO_SUBMIT_NODE1_E_DISPUTA_VMID8.md): Registro detalhado dos logs e números da corrida.
