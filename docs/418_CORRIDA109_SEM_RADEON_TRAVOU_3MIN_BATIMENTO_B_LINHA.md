# Corrida 109 (DX.65, sem Radeon Software, 600 s): travamento aos 3 min 16 s; desfecho B′

> **Corrida 109:** 24/09/2026, bind às 18:21:29.
> **Candidato:** lab `3.269.1.0`, SYS `7EF9D54D…`, UMD `B6AB6788…`.
> **Evidência:** `docs/evidence/2026-09-24_18-21-29-DESKTOP-HQAG6T4-lab-binding/`.
> - `display-log-this-bind-RECUPERADO.txt`: 20.883.770 bytes, sha256 `0d9790ff…23f5`. O boot seguinte começa na linha 152.447.
> - `paging-trace-this-bind.bin`: 1.039.968 bytes = 1.884 registros v2, sha256 `cd04e871…7fe6`. Decodificação em `paging-trace-decode-last4.json`.
> - `umd-logs-pos-travamento/`: 7 logs.
>
> **Critério:** [doc 417 §5](417_CORRIDA108_TRAVAMENTO_6MIN_RADEON_E_DX65_CRITERIO_CORRIDA109.md).
> **Relato do Daniel:** "mexia o mouse de tempos em tempos; por volta dos 3 minutos fui mexer e travou".

## 1. Resultado

**Controle OK:**
- nenhum log da UMD do processo `Radeonsoftware.exe`: processos com log são dwm, Explorer, harness, msedge, msedgewebview2, Claude, dxgiadaptercache;
- os registros do rastro são v2, com batimento.

**Travamento duro às 18:24:45,0** (boot 18:19:49,5 + 295,499 s), **3 min 16 s após o bind**. O guardião só recuperou no boot novo:
- 18:31:31: `ROLLBACK_STARTED`;
- depois `RECOVERY_OK_ONLINE`, com `boot_ticks` diferente do armado, portanto **não é R1**;
- entre o boot novo (~18:27) e o prazo, o dispositivo ficou em **Código 43 com o bc250mcdm**, porque o fusível recusou o StartDevice.

**Batimento (DX.65, P0):**
- Ao longo da corrida houve 517 registros com a sequência parada: o escritor estava vivo em períodos sem paginação. **O batimento funciona.**
- Nos 8 registros finais, a sequência avança ~450–800 a cada 100 ms, até o último (seq. 711.890, `FLUSH_TLB` estágio 5, `active_count=1`, tick 1868).
- **Zero** registro depois da última paginação: a paginação estava ativa e o escritor (PASSIVE_LEVEL) parou **no mesmo período de 100 ms**.

**Desfecho: B′.** A máquina parou de uma vez, junto com a paginação, e o rastro de paginação **não localiza**. A correlação com a Radeon Software **cai como condição necessária** (P0 com o controle válido): travou sem ela, e mais cedo (3 min 16 s contra 6 min).

**Outros sinais:**
- zero TDR, zero timeout, zero GFXHUB_FAULT, zero `CP_RESET`;
- zero `BLT_DESTINO RECUSADO`, zero fallback, zero `status=9`, zero `BYTE_PROOF FAIL`, zero `COMPOSITE_BLT RECUSADO`;
- harness 60/60, 0,9 FPS médio;
- últimas linhas da UMD às 18:24:44,294: DWM em `Upload ENTER` (pfnLockCb, sem LEAVE), Explorer em `CheckMultisampleQualityLevels`.
  - Isso fica ~0,7 s antes do último registro durável. É a mesma distância medida na 101 (0,66 s) e na 108 (0,62 s).
  - H: é a janela do cache de escrita do arquivo da UMD perdida no travamento. Não localiza.

## 2. O que muda no quadro

**Tempos até travar com o candidato atual:** 108 ~6,0 min, 109 ~3,3 min. Com os anteriores: 100 ~6,0, 101 ~6,6, 102 ~8,5.

**O mouse (relato do Daniel):**
- `PointerCaps=0`: não há cursor por hardware, e o DWM desenha o cursor.
- Mexer o mouse gera uma rajada de composição, com GFX (Node 1) e SDMA (paginação e composição) ao mesmo tempo. O cursor não passa por DDI nossa.
- Travar "ao mexer o mouse" é, portanto, travar numa rajada de trabalho de GPU. **H.** É uma amostra, sem hora exata do movimento.

**A assinatura aponta para um congelamento de todos os núcleos (H de alta prioridade):**
- sem BugCheck (os watchdogs de DPC e de clock não dispararam);
- a thread PASSIVE parou dentro de 100 ms;
- Kernel-Power 41.

Um mecanismo clássico que produz exatamente isso é **uma leitura MMIO à GPU que nunca completa**. O núcleo fica preso na transação PCIe e os outros bloqueiam em seguida. O nosso KMD faz leituras MMIO no caminho quente:
- readback de WPTR (0xC150) a cada submissão GFX;
- leitura de SQ a cada submissão e em cada troca de VMID (`GFX_WAVE_BOUNDARY` antes e depois, 649 vezes);
- polling de RPTR e fence do SDMA.

**Nada disso está provado.** É o candidato que casa com a assinatura inteira.

## 3. Próximo passo recomendado: migalhas que sobrevivem ao reset

É o mesmo item recomendado no doc 403 e ainda não feito.

**A ideia:** cada sítio de acesso MMIO do caminho quente escreve, **antes** do acesso, um identificador de sítio e uma sequência numa página reservada da **VRAM**, pelo BAR. A escrita é postada e nunca bloqueia. No StartDevice seguinte, **mesmo recusado pelo fusível**, o KMD lê a página e registra a última migalha.
- **Se a VRAM sobreviver ao reinício:** a última migalha nomeia o sítio em que a CPU parou (se for uma leitura MMIO, a leitura exata).
- **Se não sobreviver:** o magic ausente diz isso, e a abordagem é descartada com uma corrida.

**Depende de como a máquina é reiniciada:**
- botão de reset ou reinício a quente: a VRAM costuma ser preservada;
- corte de energia: não.

Antes de implementar, isso é uma pergunta ao Daniel.

**Alternativa sem código (bissecção por remoção):** desligar a leitura de SQ por submissão, que é diagnóstico e não funcional. Isso muda comportamento e só responde "sim/não" para um dos candidatos.

**Recomendação:** as migalhas primeiro. Respondem qual sítio, em vez de testar um por vez, e custam uma corrida.

## 4. Estado

- BasicDisplay em Código 0 depois da recovery no boot novo.
- `CLEAN_BOOT_REQUIRED` ativo.
- A Radeon Software continua desligada (tarefa `StartCN`, serviço AMD External Events). Recomendo mantê-la assim até o fim desta investigação, para não reintroduzir uma variável. Os comandos para desfazer estão no doc 417.

**NÃO VALIDADO EM HARDWARE:** causa do travamento, pixels do destino, 600 s estáveis, velocidade, jogo.
