# 480 — Corrida 140 (DX.89b, repetição da 139): mesma trava, no mesmo ponto, com o GPUVA crescendo sem parar

> **Corrida:** 27/09/2026. Desligamento às 17:00:58, boot às 17:01:57, bind às 17:04:13, trava antes das 17:06, reset (Kernel-Power 41 às 17:06:20). **Candidato:** DX.89b / 3.295.1.0, o mesmo da 139.
> **Evidência:** `docs/evidence/2026-09-27_17-04-06-DESKTOP-HQAG6T4-lab-binding`. A cauda do KMD sai do offset 3.210.214.141 de `C:\BC250_DISPLAY_LOG.txt`, e o rastro do offset 83.131.408, cortado no reinício do `writer_tick`. O ETW desta corrida está em `2026-09-27_17-04-01-DX83-HANG-ETW`, com estado RUNNING e sem análise. Os logs da UMD desta corrida **ainda não foram coletados**: saem quando o watchdog vencer, ~17:14.
> **Relato do Daniel:** "rodei, deu tela preta, aí reiniciei".

## 1. Relato obrigatório
**1. Houve avanço?** Sim, só de diagnóstico: a trava da 139 **se repetiu**. Funcionalmente não houve avanço.

**2. Sucessos (P0):** até a trava, 6.373 FLUSH_TLB entraram e 6.373 saíram, houve 1 CommitVidPn, 3 `BYTE_PROOF PASS` e zero TDR, fault, `InsideIb`, `Blt Timeout` ou `DEVICE_REMOVED`.

**3. Falhas:**
- **Trava dura ~27 s depois da primeira paginação.** O log do KMD cobre 27,3 s pelo QPC, e o rastro para no tick 261; na 139 foram ~29 s e o tick 268.
- O último estágio do rastro é o commit do journal (44), com **M=1 e N=772**; na 139 foi M=65 e N=779.
- O gravador morreu no mesmo tick da última paginação. BCR1 mostra só um FLUSH_TLB em andamento (CPU 3) e nenhum acesso a registrador.
- 14 `Journal NO_SPACE (num=64) -> transacao parcial ABORTADA`.
- **No reboot o fusível recusou o StartDevice** (`0xC0000182`), e o dispositivo ficou em **Código 43 com bc250mcdm**. O rollback ainda não tinha ocorrido às 17:09.

**4. Veredito frente ao critério do doc 479: B.**
- Assinatura: trava em menos de 2 min, `NO_SPACE` presente e N no mesmo patamar que eu já havia chamado de "perto do teto" na 139.
- Ressalva: o teto de 1.024 **não** foi atingido (o máximo foi 836), então esgotar o teto não é o mecanismo direto.

**5. Recuperação e pendências:**
- Aguardar o watchdog, ou rodar `tools\BC250_RECOVERY.cmd` elevado.
- Conferir o fusível e fazer boot frio antes de qualquer bind.
- **NÃO VALIDADOS EM HARDWARE:** estabilidade com a textura compartilhada e a causa da trava.

## 2. O dado que separa as corridas (P0, rastro DX.56)
N é o número de faixas vivas no estágio 44, por tick de ~100 ms.

| Corrida | Pacote | Duração | N típico | N máximo | Desfecho |
|---|---|---|---|---|---|
| 138 | DX.89 (compartilhada recusada) | 600 s | 230–470, sobe e desce | 649 | sem trava |
| 139 | DX.89b (compartilhada criada) | ~29 s | 290 → 774, só sobe | 792 | trava |
| 140 | DX.89b (compartilhada criada) | ~27 s | 290 → 772, só sobe | 836 | trava |

- Na 138, N oscilou por 10 min e nunca passou de 649.
- Com a DX.89b, N cresce **de forma quase monotônica, ~18 faixas/s**, e a máquina trava duas vezes seguidas **no mesmo tempo e no mesmo patamar**.
- Duas repetições com tempo e N praticamente iguais apontam para gatilho **determinístico** ligado à carga nova, não para a trava intermitente antiga.

## 3. Hipóteses (H) e o que falta ler
1. **Crescimento legítimo:** o Explorer vivo mapeia mais, e algum custo ou limite que cresce com N trava a máquina perto de 800. Exemplos: commit do journal O(N) sob lock, páginas de tabela de páginas, janela de 256 MB do segmento local. Contra: a trava é dura e a última paginação estava progredindo.
2. **Vazamento:** faixas que deveriam sair não saem. Um mecanismo possível: um `NO_SPACE` que aborta uma transação de **desmapeamento** grava a PTE no hardware, mas não remove a faixa do journal.
3. **Uso da textura compartilhada** (abrir, sincronizar, compor) que leva a GPU ou o barramento a travar, com N apenas acompanhando o tempo.

Leitura offline antes de qualquer corrida:
- logs da UMD desta corrida (depois do watchdog);
- composição das faixas vivas por processo/segmento no fim da 139/140;
- o ETW desta corrida;
- o caminho do `NO_SPACE` em `Bc250DxgkDdiUpdatePageTable` (`bc250_active_miniport.c:15291`).

## 4. Logs da UMD (copiados depois do rollback) e comparação com a 136
Logs em `umd-logs-pos-travamento` (7 arquivos, com SHA-256). O ETW desta corrida está **vazio**: início igual ao fim e zero eventos nos dois `.etl`, porque os buffers ficam em RAM e se perdem na trava dura. ETW não serve para esta classe de trava.

**Todos os processos param no mesmo instante, 17:04:42,5–42,6.**
- **Harness de conformidade (pid 4772):** carrega às 17:04:36,2. Faz 2 Presents `hr=0`, com `PRESENT_CB` de 942 ms e 437 ms. A última linha é `FlushCommands ENTER` do 3º draw, uma submissão gráfica. **Na 139 é o mesmo padrão:** o harness carrega às 12:52:15,3 e a trava vem às 12:52:20,9. Nas duas corridas a trava cai **~6 s depois do início do harness**.
- **Explorer (pid 4100):** nos primeiros 27 s fez **252 alocações e zero liberações**, com 4 Presents e 4 Draws. Na 136, a janela de 600 s em que o Explorer ficou vivo o tempo todo, os primeiros 27 s tiveram 166 alocações, 216 liberações, 76 Presents e 42 Draws. Das 249 criações da 140, **238 são texturas 1024x1 BGRA com dado inicial e só SRV**: são gradientes do Direct2D, recurso legítimo, sem falha em laço.
- **N da 136:** ficou entre 243 e 360 (máx. 592) por 600 s, **com o Explorer vivo e o harness concorrente**. Logo, "Explorer vivo" e "harness + Explorer" **não bastam** para N crescer nem para travar. A diferença é a textura compartilhada aceita (DX.89b): com ela, o Explorer entra numa fase nova, que desenha a interface com D2D e retém recursos.
- **Descartados por leitura:**
  - a imagem Vulkan pede mais bytes que a alocação WDDM (3.670.016 contra 3.407.872), mas tem `vkAllocateMemory` própria, então não há estouro;
  - a VA do Explorer não para no mesmo limite nas duas corridas (0x20087A000 contra 0x200D5E000);
  - a soma das alocações não mede o conjunto vivo, então não prova pressão de VRAM.

**Hipóteses restantes (H):**
1. **Carga/escala:** a fase nova do Explorer (N crescente, recursos retidos) leva a um limite ou caminho não exercitado, e a trava viria mesmo sem o harness.
2. **Concorrência:** a trava exige a submissão gráfica do harness **sobre** esse estado; sem o harness, a máquina sobrevive.

## 5. Critério da Corrida 141 (escrito antes)
**Runner:** `tools\EXECUTAR_CORRIDA141_DX89B_SEM_HARNESS_600S.cmd`. **Pacote:** o mesmo 3.295.1.0. **Única mudança:** o harness sai, via `BC250_SKIP_D3D11_HARNESS=1` no `EXECUTAR_D3D11_NATIVO.cmd`; o padrão continua com o harness. Boot frio antes. Usar o desktop como nas 139/140.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem trava; N do rastro sobe e depois estabiliza ou desce | Hipótese 2: a trava exige a submissão gráfica concorrente. Próximo: ler o caminho de submissão Node 1 com esse estado (offline). |
| **B** | Trava sem o harness, com N crescendo como nas 139/140 | Hipótese 1: carga/escala. Próximo: instrumentar a composição das faixas e o limite alcançado. |
| **C** | 600 s sem trava, mas N crescendo sem parar até o fim | Crescimento sozinho não trava; o harness é o gatilho sobre um estado grande. Tratar como A e registrar o N final. |
| **D** | O Explorer volta a cair ou a textura compartilhada falha | A variável mudou e o ensaio não compara; descrever. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |
