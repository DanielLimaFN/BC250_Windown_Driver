# Corrida 120: coalescencia Clear+Draw, eliminacao de drenos vazios e reducao de latencia (DX.75)

> Bind: **25/09/2026 10:25:35**, lab **3.279.1.0**, SYS assinado SHA-256 `7BDCA67C06FB3270A9F5D83E83F4EE21AE306128AA7D2756D2B74C1639F6DB40`, UMD `9F02C24FA3821ADF70A2C7E072DF2AD1DB6210C16CA366701FC2A02E27581C87`. Evidencia: `docs/evidence/2026-09-25_10-25-35-DESKTOP-HQAG6T4-lab-binding/`. Criterio previo: [doc 441](441_DX75_COALESCENCIA_CLEAR_DRAW_E_CRITERIO_CORRIDA120.md). Daniel confirmou execucao com reversao limpa pelo guardiao.

## 1. Houve avanco?

**Sim, avanco funcional e de desempenho direto:**
1. **Eliminacao completa do dreno sincrono com `DrawsPendentes=0`:** O harness passou de 60 drenos vazios (gastando ~70 ms por quadro na Corrida 119) para **exatamente zero** na Corrida 120 (`DrawsPendentes=0` = 0).
2. **Coalescencia real de Clear + Draw:** O `ClearRenderTargetView` com `loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR` e o `Bc250UmdExecuteVkDraw` passaram a ser submetidos conjuntamente dentro do mesmo render pass e no mesmo command buffer Vulkan (`DrawsNoLote=1`).
3. **Queda de 50% nas submissoes síncronas do harness:** O total de chamadas `FlushCommands` do harness caiu de 243 (Corrida 119) para **123** (Corrida 120), estabilizando em exatamente 2 flushes por quadro (1 para Clear+Draw coalescido e 1 para destile linear do Present).
4. **Ganho expressivo de taxa de quadros e reducao de tempo de parede:**
   - A duracao total dos 60 quadros caiu de **~64 s** (Corrida 119, 0,94 FPS) para **~44 s** (Corrida 120, média de **1,36 FPS**, com picos de janela de **1,7 FPS**).
   - O tempo de parede de renderizacao por quadro caiu de **~1100 ms** para **~480-650 ms** (~500 ms de reducao de parede por quadro).
5. **Fidelidade de pixel 100% preservada:** Todas as inspecoes do buffer linear confirmaram rasterizacao correta: `Center(640,360)=0xFF7F4040`, `Px0..3=[0xFF2673E5 ...]`, `NonZeroSamples=5/5` e grade inteira preenchida (`NonZero=28800/28800`).
6. **Recuperacao R1 online intacta:** O guardiao realizou o rollback online para `BasicDisplay` Codigo 0 em 5/5 amostras consecutivas no mesmo boot sem reiniciar a maquina.

---

## 2. Sucessos e metricas

- **Proveniencia `binding-provenance.json`:** pacote 3.279.1.0 `DISPLAY_RENDER_LAB`, SYS `7BDCA67C...`, UMD `9F02C24F...`, INF `9C4E2AEE...`, CAT `900C2F56...`. Log KMD final com 25,34 MB.
- **Harness D3D11 (PID 5228):** **60/60 `PRESENT_OK`** aceitos pelo runtime.
  - FPS medio: **1,34 FPS** (min 0,5 no boot inicial a quente, max 1,7 FPS na janela de execucao estavel).
  - Flushes totais em 60 quadros: **123** (contra 243 na Corrida 119).
  - Teto de inspecao de readback: atingido no quadro 3 (`[DRAW_READBACK] TETO de 3 leituras atingido neste device; draws seguintes NAO sao lidos`), eliminando 57 flushes desnecessarios.
  - Inspecao de pixels no linear buffer: 60/60 quadros com centro `0xFF7F4040`, cantos `0xFF2673E5` e grade `NonZero=28800/28800`.
- **KMD Display DDI:**
  - 489 eventos de Present registrados no KMD (`[Present #489]` concluido com sucesso).
  - Transicoes de plano `SetVidPnSourceAddressWithMultiPlaneOverlay3` fluidas e estaveis em VRAM.
- **DWM (PID 11912):**
  - 385 Presents aceitos em 127 s de sessao ativa (**3,03 Presents/s**).
- **Guardiao:**
  - Janela de observacao de 180 s completada (`10:25:41` a `10:28:37`).
  - `RECOVERY_OK_ONLINE` as `10:28:44`, `recovery_exit_code=0`, 5 amostras consecutivas de `BasicDisplay` Codigo 0 sem reboot no mesmo boot (`boot_ticks=639259394235000000`).

---

## 3. Falhas e limites

- Nenhum BugCheck, zero TDR, zero device-lost, zero erros de compilacao de shader ou quebra de render pass observados na janela de 180 s.
- A taxa de apresentacao do harness (~1,3-1,7 FPS) ainda e' limitada pela cadencia de liberacao de buffers pelo DWM (~3,03 Presents/s com retencao de ~334 ms no `pfnPresentCb`).
- Os 2 flushes restantes por quadro (o submit do draw e o submit do destile linear do Present) ainda operam com esperas sincronas (`vkWaitForFences`), custando ~60 ms cada um.
- Estabilidade longa de 600 s, 60 FPS e jogos seguem **NAO VALIDADOS EM HARDWARE**.

---

## 4. Veredito

**Desfecho A do criterio 441:** Sucesso pleno de coalescencia Clear+Draw, eliminacao total do dreno vazio `DrawsPendentes=0`, reducao do teto de readback de diagnostico, reducao de ~50% no numero de flushes, reducao de ~500 ms no tempo de quadro (elevando a taxa de 0,94 para ate 1,7 FPS), preservacao da fidelidade de pixels e reversao online R1 sem reboot.

---

## 5. Recuperacao e proximo passo (DX.76)

O sistema encontra-se seguro com `BasicDisplay` ativo em Codigo 0. O sentinela `C:\BC250_REQUIRE_CLEAN_BOOT.json` foi atualizado para os ticks do boot da Corrida 120, exigindo boot frio antes do proximo ensaio.

**Proximo passo arquitetural (DX.76):**
Para avancar rumo a taxas superiores no desktop e no harness:
1. **Coalescer o Destile do Present com o Command Buffer do Draw:** O destile (`Bc250UmdPresentVulkanDetileAndScanout`) atualmente grava em um novo command buffer e faz um segundo `FlushCommands` síncrono por quadro (~60 ms). Se o destile for encadeado no mesmo command buffer apos o render pass, economiza-se mais um flush por quadro.
2. **Avaliar cadencia do DWM e Swapchain Latency:** Reduzir a retencao de ~334 ms em `pfnPresentCb` atraves do ajuste de politicas de buffer no DWM / DXGI.

Provas SHA-256:
- KMD: `display-log-this-bind-FINAL.txt` (25.341.506 bytes) `D7358AD6375706B981D8C3293939B4775313212D942FEDB531EEA52591BDE218`
- Harness UMD: `bc250_umd_log_5228-FINAL.txt` (341.532 bytes) `BB0D5080977F00BACF6552E5958477B42832F2EB0B058511E0839D7C124D26E6`
- DWM UMD: `bc250_umd_log_11912-FINAL.txt` (9.191.217 bytes) `D1B535B53DFD1CBD4208038C3FEA6034F1E2E3E1A756178CBFB1D498D5AEFFB9`
- Guardiao: `safe-bind-final.json` `0C5389B4E807EF3E2E978205F5FA4EEC304F3230156037790DC03C33E5B6A64E`
- Harness Stdout: `conformance-stdout.tmp` `3886DB5E0E8B62AD42747ECA9BF0EA4D3F4A0E8BBC08C48C92DF95DAB4584508`
