# Corrida 121: unificacao Draw+Destile no command buffer, reducao para 1 flush por quadro e resultado (DX.76)

> Bind: **25/09/2026 11:23:15**, lab **3.280.1.0**, SYS assinado SHA-256 `7BDCA67C06FB3270A9F5D83E83F4EE21AE306128AA7D2756D2B74C1639F6DB40`, UMD `9A7F60DA1101E0A0D0F589711E1CE6E457A3660C866E959E0859F47B8E114C8F`. Evidencia: `docs/evidence/2026-09-25_11-23-15-DESKTOP-HQAG6T4-lab-binding/`. Criterio previo: [doc 443](443_DX76_UNIFICACAO_DRAW_DESTILE_E_CRITERIO_CORRIDA121.md). Daniel confirmou execucao com comportamento visual estavel identico aos testes anteriores.

## 1. Houve avanco?

**Sim, avanco funcional direto e reducao historica de flushes:**
1. **Unificacao real de Draw e Destile no mesmo Command Buffer:** O draw rotineiro passou a ser gravado sem submissao sincrona intermediaria (`FlushAgora=0`). O Present anexa diretamente a barreira e o `vkCmdCopyImageToBuffer` ao command buffer aberto, submetendo Clear + Draw + Destile em **uma unica chamada `FlushCommands` por quadro** (`DrawsNoLote=1`).
2. **Queda de 46% nas submissoes sincronas do harness:** O total de chamadas `FlushCommands` em 60 quadros caiu de **123** (Corrida 120) para **66** na Corrida 121 (exatamente 1 flush por quadro nos quadros 4 a 60, mais as leituras de diagnostico dos 3 primeiros quadros). Comparado a Corrida 119 (243 flushes), a reducao acumulada e' de **73%**!
3. **Eliminacao dos stalls de `vkDeviceWaitIdle`:** A remocao das chamadas redundantes de `vkDeviceWaitIdle` no Present permitiu fluidez total no pipeline de submissoes.
4. **Volume de apresentacao do DWM e KMD expandido:**
   - DWM apresentou **561 quadros** ao longo da janela (3,12 Presents/s sustentados), contra 385 na Corrida 120.
   - O KMD processou **660 Presents**, alcancando deteccao continua de desktop real na superficie (`*** DESKTOP REAL DETECTADO NA SUPERFICIE *** NonZero=256/256`) com roteamento automatico ao scanout (`Blt 11059200 bytes (src 0xAAC000 -> scanout 0x0): SDMA=YES`).
5. **Fidelidade de pixel 100% mantida:** 60/60 inspecoes confirmaram `Center(640,360)=0xFF7F4040`, `Px0..3=[0xFF2673E5 ...]`, `NonZeroSamples=5/5` e preenchimento total da grade (`NonZero=28800/28800`).
6. **Recuperacao R1 online comprovada:** O guardiao reverteu online para `BasicDisplay` Codigo 0 em 5/5 amostras consecutivas sem reboot.

---

## 2. Sucessos e metricas

- **Proveniencia `binding-provenance.json`:** pacote 3.280.1.0 `DISPLAY_RENDER_LAB`, SYS `7BDCA67C...`, UMD `9A7F60DA...`, INF `EA6DA5B9...`, CAT `75C7D1CD...`. Log KMD final com 26,70 MB.
- **Harness D3D11 (PID 4016):** **60/60 `PRESENT_OK`** aceitos pelo runtime.
  - Total de `FlushCommands`: **66** (contra 123 na Corrida 120 e 243 na Corrida 119).
  - Comportamento de lote comprovado: nos quadros 4 a 60, `FlushAgora=0` no Draw e `DrawsNoLote=1` no flush do Present.
  - Taxa do harness: 1,2 FPS medio, com picos de **1,7 FPS** nas janelas estaveis.
  - Inspecao de pixels no linear buffer: 60/60 quadros com centro `0xFF7F4040`, cantos `0xFF2673E5` e grade `NonZero=28800/28800`.
- **KMD Display DDI:**
  - **660 Presents** completados no KMD (`[Present #660]`).
  - Deteccao de desktop real na superficie em VRAM ativa (`Hash=0x00E51474 (Phys=0xAAC000)`) roteada com sucesso ao scanout 2560x1080 via SDMA Blt.
- **DWM (PID 1204):**
  - **561 Presents** concluidos com sucesso em ~180 s de sessao ativa (**3,12 Presents/s** sustentados).
- **Guardiao:**
  - Janela de observacao de 180 s completada (`11:23:21` a `11:26:17`).
  - `RECOVERY_OK_ONLINE` as `11:26:31`, `recovery_exit_code=0`, 5 amostras consecutivas de `BasicDisplay` Codigo 0 sem reboot no mesmo boot (`boot_ticks=639259429005000000`).

---

## 3. Falhas e limites

- Zero BugChecks, zero TDRs, zero device-lost, zero travamentos durante os 180 s.
- A taxa de apresentacao do harness segue regulada pelo compositor DWM (~3,12 Presents/s com retencao em `pfnPresentCb`).
- Estabilidade longa de 600 s, 60 FPS e jogos seguem **NAO VALIDADOS EM HARDWARE**.

---

## 4. Veredito

**Desfecho A do criterio 443:** Sucesso integral da unificacao Draw+Destile. Total de flushes no harness caiu para 66, confirmando exatamente 1 submissao por quadro. KMD atingiu 660 presents com deteccao e roteamento de desktop real ao scanout. Fidelidade de pixel preservada e reversao limpa R1 sem reboot.

---

## 5. Recuperacao e proximo passo (DX.77)

O sistema encontra-se seguro com `BasicDisplay` ativo em Codigo 0. O sentinela `C:\BC250_REQUIRE_CLEAN_BOOT.json` foi atualizado para os ticks do boot da Corrida 121, exigindo boot frio antes do proximo ensaio.

**Proximo passo arquitetural (DX.77):**
1. **Otimizacao do Roteamento de Scanout e Dirty Rects:** Com o desktop real sendo roteado via SDMA Blt a 3,12 Hz, avancar na reducao de transferencias desnecessarias no DWM atraves de Dirty Rects (`pMoves`/`pDirtyRects`).
2. **Elevacao do Frame Latency Regulation:** Avaliar politicas de apresentacao assincrona no callback do harness para permitir renderizacao desacoplada da taxa do compositor.

Provas SHA-256:
- KMD: `display-log-this-bind-FINAL.txt` (26.699.791 bytes) `03B4B1EC183D200B86ED5E419B29F43CD7DD0ACCD1DB2B3FEB372A854E90703E`
- Harness UMD: `bc250_umd_log_4016-FINAL.txt` (314.929 bytes) `5CD222A80B3BD2EBF7C83FAD1BF716B70D18C4B8DD2D8F000D75A92489095479`
- DWM UMD: `bc250_umd_log_1204-FINAL.txt` (12.392.134 bytes) `6636198E1662CC06B0B1F21E035E722605B22A5A69546925001787865AC5FC47`
- Guardiao: `safe-bind-final.json` `C825C8B17FA5B5775A6719288DDAB6C467526501B90E54B9E929287D3B04F4FA`
- Harness Stdout: `conformance-stdout.tmp` `ED028538F3C86F212FAAE793DC58DADA028DE928095A5C9D1F47D0A10657A163`
