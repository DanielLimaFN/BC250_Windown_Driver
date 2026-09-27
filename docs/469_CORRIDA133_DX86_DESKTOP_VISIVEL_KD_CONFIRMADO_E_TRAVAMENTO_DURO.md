# 469 — Corrida 133 (DX.86): desktop visível, lentidão da 132 explicada pelo KD, e travamento duro aos ~5m51s

> **Corrida:** 26/09/2026, bind 15:10:29, trava ~15:16:20, reset ~18:51 (Daniel: "visível por um tempo, depois tela preta, tive que reiniciar").
> **Candidato:** DX.86 / lab 3.291.1.0 (SYS `C1BE8BBF…`, UMD `3A973193…`), boot com `NODEBUG` (KD desligado).
> **Evidência:** `docs/evidence/2026-09-26_15-10-29-DESKTOP-HQAG6T4-lab-binding` (sem arquivos `-FINAL`: a janela não terminou), cauda do KMD a partir do offset 2.758.362.509 de `C:\BC250_DISPLAY_LOG.txt`, rastro DX.56 a partir do offset 40.281.648, ETW `docs/evidence/2026-09-26_15-10-23-DX83-HANG-ETW` (não fechado; manifesto ainda `RUNNING`).

## 1. Relato obrigatório

**1. Houve avanço?** Sim, funcional, contra a 132: desktop visível, paginação rápida, DWM fluido e harness completo. O travamento duro é a classe antiga das 115/128/129, não uma regressão nova.

**2. Sucessos (P0):**

| Métrica | 132 (KD ligado) | 133 (KD desligado, sem DbgPrint) |
|---|---|---|
| `FLUSH_TLB` | 10.615; 536 com ≥ 400 ms | **93.049; p50 1,38 ms, p90 1,95 ms, 1 com ≥ 400 ms** |
| VidMm em `PagingPreparation` (ETW, 90 s) | 96%, p90 522 ms | **32%, p90 8 ms** |
| `pfnPresentCb` do DWM | 88–116 s por chamada | **p50 2,8 ms, p90 15,4 ms** (máx 6,6 s) |
| Presents do DWM | 35 em 10 min | **953 em 5m42s (~2,8/s)**, todos `hr=0` |
| Presents no KMD | 223 | **1.409**; 26 `DESKTOP REAL DETECTADO`, 5 `BYTE_PROOF PASS` |
| Harness D3D11 | 24 quadros e `DEVICE_REMOVED` | **60/60 `PRESENT_OK`, 2,1 FPS** (126: 1,7) |
| Writer do rastro DX.56 | ~0,66 registro/s | **~9,2 registros/s** |

Também: wrapper de paginação balanceado, zero TDR_QUIESCE, zero IB_TRIPOD TIMEOUT, zero GFXHUB_FAULT, zero `WaitPaging TIMEOUT`. O Daniel viu o desktop.

**3. Falhas:**
- **Trava dura da máquina às ~15:16:20** (~5m51s de bind). Os logs da UMD (DWM e Explorer) param no mesmo milissegundo. O writer do rastro morre no mesmo instante. Sem BugCheck, sem dump, e a máquina ficou assim ~3,5 h até o reset.
- O Explorer caiu 4 vezes (`0xC0000602` fail-fast em `KERNELBASE+0x12d862`), sempre logo depois de o runtime recusar com `E_INVALIDARG` uma textura **compartilhada** 1024x832 BGRA (`Misc=0x2`), e sempre acompanhado de LiveKernelEvent 193/804. O `SearchApp` caiu 10 vezes. **Não é novo:** a 126 teve 436 dessas recusas, 2 quedas do Explorer e 9 live dumps. O KMD aceita a alocação (`KmdStatus=0` na 126); quem recusa é o runtime.
- 233 `NO_SPACE`, 1 `PUBLICACAO_FALHA`.

**4. Veredito frente ao critério do doc 468: desfecho A.** Os quatro sinais foram atendidos: platô de 510 ms ausente (1 chamada ≥ 400 ms), `PagingPreparation` abaixo de 50%, Present do DWM com p90 < 2 s, desktop visto. A lentidão da 132 era o KD combinado com `DbgPrintEx` em toda linha de log. **A estabilidade de 600 s falhou** pela trava dura, que estava fora do escopo desse critério.

**5. Recuperação e pendências:** rollback após o reset, `RECOVERY_OK_ONLINE`, BasicDisplay Código 0. O fusível disparou no boot pós-reset, como esperado. A captura ETW ficou `RUNNING` e exige `-Recover` elevado antes do próximo bind. 600 s, 60 FPS, jogos e a causa da trava dura: **NÃO VALIDADOS EM HARDWARE**.

## 2. O que se sabe sobre a trava dura
- **Ponto não localizado.** No KMD, as linhas emitidas durante paginação passam pelo anel em RAM e se perdem na trava; o arquivo termina em atividade normal (flushes de ~1,3 ms e IB com tripé provado). A migalha em VRAM (DX.66) não sobreviveu ao reset ("ANTERIOR AUSENTE"). O último snapshot do rastro pegou a thread no estágio 44 (M=33, N=271), mas o log mostra flushes completando ~250 ms depois; ela **não** estava presa ali.
- O ETL de kernel (não fechado) foi lido até 356,162 s. O fim por CPU varia entre 355,4 s e 356,2 s por causa do flush de buffer, o que não identifica a CPU presa. As pilhas estão sem símbolos, porque o ETL circular perdeu os eventos de carga de módulos.
- Travado ~3,5 h sem 0x133: compatível com uma CPU presa em acesso MMIO que a GPU não responde (**H**), mais do que com um laço em DISPATCH.
- No instante da trava, o DWM criava uma textura 2560x1080 (~11 MB) logo depois de um `VIRTUAL_FILL` de 2.704 páginas. Porém na 128 o DWM fazia Present/Flush na trava, então isso **não é** gatilho consistente.
- `CollectDbgInfo` (chamada nos live dumps) não faz MMIO; não sustenta ligação com os LiveKernelEvent.
- Observação: o **AnyDesk** estava rodando (captura de tela). Não se sabe se estava ativo nas corridas anteriores.

## 3. Próximo passo: Corrida 134 com break-in (sem rebuild)
Com o `DbgPrint` fora do driver, o KD deixa de ser um custo por linha. Um WinDbg conectado é a única fonte de pilhas numa máquina travada (plano 460).

- **Pacote:** o mesmo 3.291.1.0. **Runner:** `tools\EXECUTAR_CORRIDA134_DX86_KD_BREAKIN_600S.cmd`, que passa `BC250_ALLOW_KERNEL_DEBUG=1` ao preflight.
- **Pré-requisitos (Daniel):**
  1. `tools\bc250-hang-etw.ps1 -Recover` elevado (captura da 133 pendente).
  2. `bcdedit /debug on` com o KDNET do host.
  3. Boot frio.
  4. WinDbg conectado e testado (Break/Go) **antes** do bind.
- **Teto:** uma corrida.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Trava ocorre, break-in funciona e `!running -it` mostra uma CPU num quadro nosso ou do dxgkrnl | A pilha nomeia o ponto. Corrigir ali. |
| **B** | Trava ocorre e o break-in **não** responde | Trava no nível de barramento/CPU (reforça MMIO sem resposta). Próximo: marcar cada acesso MMIO em memória que sobreviva ao reset, ou reduzir os acessos MMIO fora do caminho essencial. |
| **C** | Sem trava em 600 s | KD mudou o tempo ou a trava é rara. Registrar duração e repetir só com critério novo. |
| **D** | `FLUSH_TLB` volta a ter o platô de ~510 ms com KD ligado | Ainda há `DbgPrint` ou o custo é do próprio KD; conferir o binário. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |
