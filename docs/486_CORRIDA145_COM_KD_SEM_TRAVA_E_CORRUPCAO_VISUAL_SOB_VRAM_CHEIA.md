# 486 — Corrida 145 (DX.90 com WinDbg/KDNET): sem trava, e corrupção visual em blocos com a VRAM quase cheia

> **Corrida:** 27/09/2026. Bind às 22:12:13 após boot frio, com KDNET ativo (`DEBUG DEBUGPORT=NET`). Harness às 22:15:13. Rollback online (`RECOVERY_OK_ONLINE`). **Candidato:** DX.90 / 3.296.1.0, harness atrasado 150 s.
> **Evidência:** `docs/evidence/2026-09-27_22-12-13-DESKTOP-HQAG6T4-lab-binding` (logs `-FINAL`); foto do monitor enviada pelo Daniel.
> **Relato do Daniel:** "não travou; quando abriu aquele quadrado azul com o triângulo, ficou dando uns bugs, e ficou preto; quando fechou, voltou ao normal".

## 1. Relato obrigatório
**1. Houve avanço?** Inconclusivo para a trava. Houve um achado visual novo.

**2. Sucessos (P0, exceto o visual):**
- 600 s sem trava; paginação 103.764/103.764; 1.981 Presents no KMD, 5 `BYTE_PROOF PASS`.
- Harness 60/60, exit 0.
- Zero TDR, fault ou `DEVICE_REMOVED`.
- **Condição de validade cumprida:** o harness começou com N≈880.

**3. Falhas e observações:**
- **Corrupção visual, observada pelo Daniel (P2):**
  - com o harness aberto, apareceram blocos de lixo azul e branco em áreas da tela fora da janela dele, que depois ficaram pretas;
  - ao fechar o harness, a tela voltou ao normal;
  - a foto mostra o Gerenciador de Tarefas com **216/256 MB de memória dedicada da GPU em uso**, e "Memória da GPU 0,2/7,9 GB".
- 26.961 `PUBLICACAO_FALHA`, com o journal no teto; 366 `NO_SPACE`.

**4. Veredito frente ao doc 485 §5: D.** Sem trava em 600 s com o KD ligado. Não se pode separar "não reproduziu" de "o KD mudou o tempo". O visual é **E, fora do mapa**.

**5. Recuperação e pendências:**
- BasicDisplay Código 0 no mesmo boot; `CLEAN_BOOT_REQUIRED` armado.
- **NÃO VALIDADOS EM HARDWARE:** a causa da trava e a causa da corrupção.

## 2. Leitura da corrupção (H)
- Blocos de lixo que somem ao fechar o harness, com a VRAM local quase cheia (216 de ~245 MB utilizáveis), parecem **conteúdo de superfícies de outras janelas perdido** quando o VidMm despeja ou move alocações para abrir espaço para o harness. Ao fechar o harness, o DWM redesenha e o conteúdo volta.
- **Pela leitura de código:**
  - `VIRTUAL_TRANSFER` tem implementação (`Bc250McdmBuildTransferVirtual`), e as cópias registradas terminaram "Completo" (log amostrado);
  - o codificador de PTE trata páginas de sistema (segmentos 0 e 2) com endereço físico e SYSTEM|SNOOPED;
  - `MAP_APERTURE_SEGMENT` responde sucesso sem ação. No modelo GpuMmu isso é esperado, mas não está provado.
- **Ligação possível com a trava (H):** o momento em que o harness chega é o momento de pressão de VRAM, com despejo e movimentação. Isso explicaria por que o gatilho é "harness + Explorer grande". É a pista mais concreta até aqui. Liga a trava à frente de memória que o Daniel levantou: usar os 512 MB e declarar a memória do sistema.

## 3. Próximo passo proposto
1. **Offline:** auditar o caminho de despejo e restauração (`VIRTUAL_TRANSFER` com origem ou destino em memória do sistema, `VIRTUAL_FILL`, `DISCARD_CONTENT`, e o que acontece com a textura compartilhada e com a primária sob despejo).
2. **Instrumentar sem amostragem** as operações 8/9/2 com origem, destino, tamanho e resultado, porque a classe `[TransferVirtual]` hoje é amostrada.
3. **Critério de uma corrida sem KD depois disso:** reproduzir a corrupção de propósito e ver se as cópias daquele instante tocaram as superfícies corrompidas.
