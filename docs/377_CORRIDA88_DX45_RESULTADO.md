# Corrida 88 — DX.45: travamento duro do PC, sem TDR, sem BugCheck, sem rollback

> **Data:** 23/09/2026. Bind às 15:43:42. O PC travou cerca de 40 s depois
> (último `Vsync #2220`) e o Daniel reiniciou à mão; boot novo às 15:51:24.
>
> **Candidato:** lab `3.249.1.0` (SYS `7F94EECB…`), UMD inalterada, RADV
> DX.42.
>
> **Critério:** [docs/376](376_DX45_DETECTOR_RESET_CP_E_CRITERIO_CORRIDA88.md).
>
> **Evidência:** `docs/evidence/2026-09-23_15-43-42-DESKTOP-HQAG6T4-lab-binding/`.
> - A pasta ficou **truncada**: sem FINAL, sem logs da UMD, sem coleta RADV,
>   saída do harness vazia.
> - A cauda do log do KMD foi resgatada do log vivo em
>   `bc250_display_log-tail-recovered.txt` (1.646.171 bytes, SHA-256
>   `51877B93…`).
>   - As linhas 1–12821 são da corrida.
>   - A linha 12822 já é o boot seguinte:
>     `[FUSE] STARTDEVICE RECUSADO: Fusivel disparado`.

## 1. Houve avanço?

**Não. Regressão de estabilidade:** um travamento duro que exigiu reinício
manual. As 86 e 87 terminaram com rollback online.

## 2. Sucessos (limitados ao que foi medido)

- **Autotestes RADV:** 3/3 com 5/5, antes do harness.
- **Log do KMD até o travamento (bind + cauda):**
  - 168 submissões gráficas, 42 trocas com aquecimento DX.43 e 872 submissões
    SDMA;
  - 117 Present, 1 `BYTE_PROOF PASS`;
  - **zero** TDR, zero `Cid=9`, zero `[CP_RESET_DETECTED]`, zero
    `[PREEMPT_COMMAND]`.
- **Proteções:** o fusível anti-boot-loop funcionou. No boot seguinte, o
  StartDevice foi recusado (`State=2, Boots=2`), o dispositivo ficou em Código
  43 e não houve loop de boot.

## 3. Falhas

- **Travamento duro do sistema** com o nosso driver ativo:
  - sem TDR, sem BugCheck (o `MEMORY.DMP` é das 09:42) e sem live dump;
  - o log termina em linhas normais (submit GFX 169 aceito, SDMA provado,
    `[McdmQAI] Type=29 … CapsStatus=0xC00000A3`);
  - essa linha QAI aparece uma vez por corrida também nas 86/87, logo não é
    anômala.
- **Sem rollback:** o watchdog ficou em `OBSERVING` do boot anterior e não
  retomou depois do reinício manual. O `bc250mcdm` 3.249.1.0 **continua
  vinculado**, em Código 43, e o marcador de boot frio **não existe**.
- **Harness:** a saída ficou vazia. O número de quadros é **NÃO MEDIDO**.

## 4. Veredito frente ao critério

- **Inconclusivo:** o log truncou no travamento, e desfecho parcial não é
  desfecho.
  - A assinatura de B (nenhuma detecção) aparece, mas com o log cortado não dá
    para afirmar B.
  - Também não há snapshot de TDR para distinguir C.
- **Função: falha.**

**Leitura do travamento:**
- **P0:** entre a 87 e a 88, a única mudança de comportamento do KMD são as
  leituras MMIO do detector DX.45:
  - `CP_ME_CNTL` a cada poll e a cada submit;
  - `CP_STAT`, `GRBM_STATUS`, `RLC_CNTL`, `RLC_PG_CNTL` e `RLC_GPM_STAT`
    **só quando detecta o CP parado**.
- **Falha de desenho minha (P0 no código):** em
  `Bc250ActiveGfxDx45LogCpReset` as cinco leituras extras vêm **antes** do
  `Bc250DisplayLog`. Se o reset silencioso da 87 se repetiu e uma dessas
  leituras não completou, o PC trava **e a linha que o provaria nunca é
  escrita**. É exatamente o que um travamento sem linha mostraria.
- **H, não provado:** uma leitura de registrador RLC/GRBM com o bloco em reset
  ficou sem conclusão no PCIe e travou a CPU.
- **Hipóteses concorrentes:**
  - o travamento é independente da DX.45 (a GPU derrubou o barramento por
    conta própria);
  - `CP_ME_CNTL`, lido a cada poll, também pode travar num estado ruim.
    Porém o poll já lia `CP_RB0_RPTR` (mesmo domínio) em todas as corridas, e
    na 87 o snapshot leu `CP_ME_CNTL` com o CP parado sem travar.

## 5. Recuperação e pendências

**Recuperação, para o Daniel, em terminal elevado, antes de qualquer outra
coisa:**
1. `tools\BC250_RECOVERY.cmd`: restaura o BasicDisplay e remove o
   `bc250mcdm`.
2. `powershell -ExecutionPolicy Bypass -File tools\inspect-boot-fuse.ps1 -Reset`:
   o fusível está `TRIPPED`, e o binder recusa sem esse reset.
3. Desligamento completo.

**NÃO VALIDADO EM HARDWARE:**
- causa do travamento;
- se o reset silencioso do CP se repetiu;
- harness da 88;
- tudo o que as 86/87 deixaram aberto.

**Próximo passo proposto (DX.45b, correção da instrumentação):**
- **Duas fases no detector:** registrar `[CP_RESET_DETECTED]` **imediatamente**
  com `CP_ME_CNTL` e os RPTR já em mãos. Depois, ler cada registrador extra
  com uma linha "vou ler X" antes da leitura. Um novo travamento passa a
  apontar o registrador.
- **Alternativa mais conservadora:** tirar as leituras RLC do detector e deixar
  só `CP_ME_CNTL` mais a linha imediata.
- **Pedido ao Daniel:** se possível, ativar o dump completo com NMI em
  travamento duro. É mudança de configuração do sistema, então fica com você.
