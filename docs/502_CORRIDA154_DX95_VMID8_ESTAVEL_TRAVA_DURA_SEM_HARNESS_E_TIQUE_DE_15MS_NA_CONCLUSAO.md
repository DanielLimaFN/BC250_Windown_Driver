# 502 — Corrida 154: VMID8 estável com a DX.95, trava dura aos ~130 s sem o harness, e o tique de 15,6 ms na conclusão da paginação

> Corrida de 28/09/2026. Bind às 11:25:27, candidato 3.305.1.0 (DX.94 + DX.95 + telemetria FPS.3a), sem KD, harness atrasado 150 s.
>
> Evidência:
> - pasta `docs/evidence/2026-09-28_11-25-27-DESKTOP-HQAG6T4-lab-binding/`;
> - cauda recuperada `bc250_display_log-tail-recovered.txt`; a corrida vai até a linha ~271.000, e o restante é o boot seguinte;
> - logs da UMD do `%TEMP%` do usuário em `umd-logs-pos-travamento/` (Explorer 6076, taskmgr 11712).
>
> O log do `dwm.exe` (em `C:\Windows\Temp`) não foi copiado.

## 1. Relato (cinco pontos)

**1. Houve avanço?** De diagnóstico, sim:
- a DX.95 fez o que devia no VMID8;
- a causa do tempo perdido por IB de paginação foi medida.

Funcional, não: a corrida terminou em trava dura.

**2. Sucessos (P0), nos ~130 s até a trava:**
- **DX.95:**
  - 230 linhas `DX95 Node` (a raiz só foi registrada);
  - **um único** `VMID8 CONTEXT SWITCH` (a instalação inicial da paginação, NULL → 0x04) e um único `GCVM_INVENTARIO_2 VMID08`, para a raiz de paginação `0x450A8C000`;
  - **zero** `CONTEXT SWITCH COM IN_FLIGHT`, zero `IB_TRIPOD TIMEOUT`, zero `Blt Timeout`, zero TDR, zero `GFX_FAULT_SENTINELA`.

  Na 153 foram 760 trocas do VMID8.
- **FPS.3a (medição nova):** para 4.835 IBs de paginação:

  | Medida | p10 | p50 | p90 | p99 |
  |---|---|---|---|---|
  | submit → conclusão | 1,4 ms | **5,2 ms** | 13,5 ms | 21 ms |
  | conclusão → próximo submit (tempo do VidMm) | 0,16 ms | **5,0 ms** | 14,8 ms | 25 ms |

  - 78% das conclusões vieram pelo DPC do timer (`FromDpc=1`).
  - O histograma de submit → conclusão é **praticamente plano de 0 a 15 ms e despenca depois de 16 ms**. Essa é a assinatura de uma checagem feita a cada tique de 15,6 ms do relógio do Windows.
  - **A hipótese do tique está CONFIRMADA (P0):** a conclusão de cada IB espera, em média, meio tique.

**3. Falhas (P0):**
- **Trava dura** às ~11:27:36, com reboot por Kernel-Power 41 às 11:28:20 e **sem dump**.
- Pelo relato do Daniel: o menu Iniciar não abria e o Gerenciador de Tarefas mostrava ~50% no motor "Cópia" (o nosso Node 0, SDMA/paginação).
- O log do KMD termina em qpc 3926360203, 87 ms antes da última linha do Explorer (`WAIT_PAGING ENTER` da fence 8204). A máquina parou de uma vez.
- **O harness não chegou a rodar.** É a primeira trava dura desta série **sem o harness**; as 139, 140, 143 e 152 foram com ele.
- **No instante da trava**, pelo último registro de cada instrumento:
  - batimento da GPU: ocioso (`GRBM_STATUS 0xA0003028`, fence 0x23E = última submetida, anel gráfico drenado);
  - rastro de paginação: `active_count=2`;
  - migalhas: só transitórias (`PAGING_DDI` op 12).

  É o mesmo quadro das 143 e 152. A nossa instrumentação não nomeia o ponto.

**4. Veredito contra o doc 501:** **E, fora do mapa.** As assinaturas do VMID8 do desfecho A apareceram todas. Mas A também exige o harness 60/60, e ele não rodou, e a corrida terminou numa trava dura que nenhum desfecho previa. Pela Lei 10, regra 3, "A parcial" não é desfecho.

O que se pode afirmar: **a DX.95 eliminou as trocas do VMID8 e, nos 130 s medidos, não houve parada do SDMA.** Que isso resolva as paradas **não está provado**: 130 s é pouco, e a 153 teve a parada aos ~1 s, a 151 aos 49 s.

**5. Recuperação e pendências:**
- Depois do reboot o fusível recusou o StartDevice. O dispositivo ficou em `CM_PROB_FAILED_POST_START`, com o driver 3.305.1.0 preso.
- Antes de qualquer bind: `tools\BC250_RECOVERY.cmd` elevado, `inspect-boot-fuse.ps1 -Reset` e boot frio.
- **NÃO VALIDADO EM HARDWARE:**
  - a DX.95 em 600 s;
  - a causa da trava dura;
  - o FPS com a conclusão sem o tique.

## 2. O que isso muda nas prioridades

1. **FPS, com causa medida.** Cada IB de paginação espera, em média, meio tique de 15,6 ms para ser dado como concluído, e o harness faz ~19 deles em série por quadro. **FPS.3b:** notificar a conclusão sem depender do tique. Duas vias:
   - **(a)** timer de alta resolução (`ExAllocateTimer` com `EX_TIMER_HIGH_RESOLUTION`), mudança pequena e local;
   - **(b)** interrupção de fim de IB do SDMA (TRAP → anel de interrupção), que é o correto a longo prazo e exige proveniência de registradores.

   A (a) primeiro. **Previsão (H):** submit → conclusão com p50 < 1 ms; o tempo de paginação por quadro cai ~40%; o FPS do harness sobe, sem chegar a 60.
2. **Trava dura.** Agora aparece sem o harness, com o Explorer e o motor de cópia ocupado. Ela segue sem nome. Com o KD ligado ela não aconteceu (0 de 2), o que aponta para temporização. Continua como item aberto.

## 3. Observações que ficaram fora
- `Journal NO_SPACE`: 233.
- `PUBLICACAO_FALHA`: 3.
- 18 `DIRTY_RECTS_RECUSADOS` (a bissecção DX está desligada).
- Os horários do batimento da GPU têm granularidade de ~15 ms. A leitura de 35 ms no último registro não é sinal (a 152 teve p99 igual).
