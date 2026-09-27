# Corrida 119: decomposicao temporal de pfnPresentCb, triangulo visivel e recuperacao R1

> Bind: **25/09/2026 09:47:50**, lab **3.278.1.0**, SYS assinado SHA-256 `5EC12AF4AE22A7BFDDD6939F953CF095505F74B43B7F73B3A541369A538833BD`. Evidencia: `docs/evidence/2026-09-25_09-47-50-DESKTOP-HQAG6T4-lab-binding/`. Criterio previo: [doc 439](439_DX74_TRACE_TEMPORAL_PRESENT_CB_E_CRITERIO_CORRIDA119.md). Daniel confirmou: **o watchdog reverteu, o triangulo apareceu**.

## 1. Houve avanco?

**Sim, avanco diagnostico conclusivo e repeticao do sucesso visual/R1 da Corrida 118:**
1. A lacuna dos ~443 ms de `pfnPresentCb` foi decomposta com precisao de microsegundos via contadores sincronizados QPC (10 MHz):
   - **Pre-DDI (UMD -> KMD):** mediana **0,82 ms** (media 5,82 ms; 1,33% do tempo total). O runtime do Windows despacha a chamada para `DxgkDdiPresent` quase instantaneamente.
   - **Execucao interna do KMD (`Bc250DxgkDdiPresent`):** mediana **0,67 ms** (media 3,49 ms; 0,80% do tempo total).
   - **Despacho para o anel SDMA (`DxgkDdiSubmitCommandVirtual` Node 0):** mediana **0,27 ms** apos a saida de `DxgkDdiPresent`.
   - **Pos-DDI (retorno de `pfnPresentCb` a UMD):** mediana **437,57 ms** (media 429,28 ms; **97,88% do tempo total**).
2. O resultado prova que a retencao de ~437 ms reside inteiramente dentro de `pfnPresentCb` no `dxgkrnl`. No modelo de apresentacao windowed (`Blt` em superficie de redirecionamento), o subsistema grafico do Windows regula o avanco da aplicacao (frame latency regulation) com base na taxa de composicao do DWM.
3. Funcionalidade visual confirmada pelo Daniel (triangulo colorido visivel na janela) e **60/60** copias 1280x720 registraram centro `Src=Dst=FF7F4040`, canto `Src=Dst=FF2673E5` e `Match=5/5`.
4. A velocidade permaneceu estavel: harness ~0,94 FPS, DWM 2,48/s nos primeiros 120 s (3,11/s no ensaio todo), consistente com uma mudanca estritamente de telemetria.

## 2. Sucessos e metricas

- **Proveniencia `binding-provenance.json`:** pacote 3.278.1.0 `DISPLAY_RENDER_LAB`, SYS `5EC12AF4...`, RADV `FCAA11FE...`, PDB `703E67B6...`. Log KMD final completo de 24,96 MB em duas passagens.
- **Harness (PID 12232):** **60/60 `PRESENT_OK`**, taxa media **0,94 FPS** (60 quadros em ~64 s).
  - 60/60 marcadores `[DX74_PRESENT_UMD_CB]` com `hr=0x00000000`.
  - Duracao total UMD de `pfnPresentCb`: min 279,3 ms, p50 **442,5 ms**, p90 467,7 ms, max 545,0 ms.
- **KMD Display DDI:**
  - 60/60 marcadores `[DX74_PRESENT_KMD_TEMPO]` com `Blt=1`, `hContext=0000062100000621`, `Status=0x00000000`.
  - Duracao interna da DDI: min 0,65 ms, p50 **0,67 ms**, p90 12,66 ms, max 48,19 ms.
  - Submissao SDMA: 60/60 eventos estruturados `BC250_DIAG ... event=KMD_SDMA_SUBMIT ctx=0000062100000621 node=0` aceitos no anel (dxg_fence 998 a 1057) com intervalo de despacho p50 de ~0,27 ms apos a DDI.
- **Fronteira de Pixel:** 60/60 copias `DX73_COMPOSITE_CENTER` conferidas com `Src=Dst=FF7F4040`, `CornerSrc=CornerDst=FF2673E5` e `Match=5/5`.
- **DWM (PID 1224):** 533 Presents em 171,23 s = **3,11/s** medio (297 nos primeiros 120 s = **2,48/s**). 4.885 chamadas `FlushCommands` com `SubmitUs` mediana de **1,75 ms**.
- **Guardiao:** `ROLLBACK_STARTED` as 09:50:52; final `RECOVERY_OK_ONLINE` as 09:51:03, `boot_ticks=armed_boot_ticks=639259371865000000`; cinco amostras consecutivas de BasicDisplay Codigo 0 confirmadas no mesmo boot. `MEMORY.DMP` continua historico (24/09 13:56:30).

## 3. Falhas e limites

- Nenhum erro visual, artefato, TDR, BugCheck, travamento ou device-lost observado durante a janela de 180 s.
- A velocidade da aplicacao continua amarrada em ~0,94 FPS porque o DWM compoe a ~2,5-3,1 FPS e o dxgkrnl bloqueia a aplicacao em `pfnPresentCb` ate que o quadro seja consumido pelo compositor.
- Estabilidade longa de 600 s, 60 FPS e jogos seguem **NAO VALIDADOS EM HARDWARE**.

## 4. Veredito

**Desfecho A do criterio 439:** Sucesso integral do rastreamento temporal com sincronizacao QPC perfeita (60/60 correlacionados), triangulo visivel, 60/60 PRESENT_OK, fidelidade de copia intacta e reversao online sem reboot para BasicDisplay Codigo 0.

## 5. Recuperacao e proximo passo

O sistema encontra-se seguro com `BasicDisplay` ativo em Codigo 0. O sentinela `C:\BC250_REQUIRE_CLEAN_BOOT.json` foi atualizado para os ticks do boot da Corrida 119, exigindo boot frio antes do proximo ensaio.

**Proximo passo arquitetural (DX.75):**
Com a comprovacao de que 97,88% do tempo do `pfnPresentCb` e' regulacao de latencia do dxgkrnl pelo ciclo do DWM:
1. Analisar as flags de `pfnPresentCb` / `DXGI_PRESENT` (`DXGI_PRESENT_DO_NOT_WAIT`, swapchain buffer count).
2. Investigar por que o ciclo do DWM consome ~320-400 ms (reduzir a quantidade de 4.885 `FlushCommands` intermediarios no DWM por quadro ou habilitar dirty rects).

Provas SHA-256:
- KMD: `display-log-this-bind-FINAL.txt` (24.961.902 bytes) `D4090505D3592E69193A9D65016FD31AB9030533CC4ECAF5B68E957B883E02A2`
- DWM: `bc250_umd_log_1224-FINAL.txt` (11.902.966 bytes) `04FA221E6A4113AB95F69C6C08FEE789555EB18E10DB2B6BEDAADB475AEC24AC`
- Harness: `bc250_umd_log_12232-FINAL.txt` (417.621 bytes) `98A4372E8EFB715E1762B328E1A5A8888E1E014016AB6DBA2E9FB77A7C467356`
- Guardiao: `safe-bind-final.json` `739A6A1A6618C8A6305E7022AFCA369BBFA0A0D2C9517EA87B50C5E07C69D457`
