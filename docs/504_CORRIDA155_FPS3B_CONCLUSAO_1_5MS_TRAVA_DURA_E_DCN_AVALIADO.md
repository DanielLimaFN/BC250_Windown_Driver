# 504 — Corrida 155: a conclusão da paginação caiu para 1,5 ms (FPS.3b validada na latência), trava dura aos ~77 s; o DCN avaliado como próximo passo

> Corrida de 28/09/2026. Bind às 11:53:07, candidato 3.306.1.0 (DX.94 + DX.95 + FPS.3b), sem KD, harness atrasado 150 s. Trava às ~11:54:24, reboot manual às 11:55:35 (Kernel-Power 41), sem dump.
>
> Evidência:
> - pasta `docs/evidence/2026-09-28_11-53-07-DESKTOP-HQAG6T4-lab-binding/`;
> - cauda recuperada, onde a corrida vai até a linha ~178.475;
> - logs da UMD do `%TEMP%` do usuário em `umd-logs-pos-travamento/`.
>
> Análise: `python tools/analyze-fps3-retire.py`.

## 1. Relato (cinco pontos)

**1. Houve avanço?** Sim, no desempenho da paginação (P0). Na estabilidade, não: terceira trava dura seguida.

**2. Sucessos (P0):**
- `[FPS3B] aposentadoria do SDMA: timer de alta resolucao ATIVO`.
- **SDMA submit → conclusão:**

  | Medida | 154 | 155 |
  |---|---|---|
  | p10 | 1,41 ms | 1,17 ms |
  | **p50** | 5,22 ms | **1,50 ms** |
  | p90 | 13,45 ms | **1,52 ms** |
  | p99 | 21,1 ms | 1,85 ms |

  Histograma: 72 em 0 ms, **2.280 em 1 ms**, 18 em 2 ms, e 2 em 15 ms. A massa de 3 a 15 ms sumiu. **A previsão do doc 503 (p50 ≤ ~1,5 ms, concentrado em 0–2 ms) bateu.**
- **DX.95 continua limpa:** 128 `DX95 Node`, uma única troca do VMID8 (a inicial), zero IB em voo, zero parada do SDMA, zero TDR, zero `GFX_FAULT_SENTINELA`.
- **GFX medido pela primeira vez** (548 conclusões): submit → conclusão p10 0,12, **p50 3,77**, p90 15,15, p99 23,4 ms, com a massa espalhada de 2 a 23 ms. **H:** é o mesmo tique no `RetirementTimer` (KTIMER periódico de 1 ms). A distribuição não é tão limpa quanto a do SDMA antes da FPS.3b, porque há conclusões vistas no submit seguinte.

**3. Falhas (P0):**
- **Trava dura** aos ~77 s, antes do harness, sem dump.
- **No instante da trava**, pelo último registro de cada instrumento:
  - GPU ociosa (`GRBM_STATUS 0xA0003028`, fence 0x223 = última submetida, anel drenado);
  - paginação sem nada em curso (`active_count=0`);
  - migalha mais recente da máquina: `SDMA_REG_READ` **concluída**.

  A entrada `ANTES` do CPU04 tem sequência 34.123.550 contra 569.170 desta sessão, então é de um boot antigo.
- O KMD, o DWM e o Explorer param juntos (11:54:24.45).
- **Nenhum evento WHEA** no log do sistema em toda a história da máquina.
- **FPS do harness NÃO MEDIDO** (ele não chegou a abrir).
- **SDMA conclusão → próximo submit:** p50 6,5 ms, p90 15,9 ms. Esse é o lado do VidMm e dos nossos FLUSH_TLB, que não mudou.

**4. Veredito contra o doc 503:** **E, fora do mapa.** A assinatura de latência do desfecho A apareceu inteira. Mas A exige o harness 60/60 com ms por quadro, e ele não rodou, porque a corrida terminou numa trava que nenhum desfecho previa. Não chamo de "A parcial" (Lei 10, regra 3). O que se afirma: **a FPS.3b tirou o tique da conclusão do SDMA** (P0).

**5. Recuperação e pendências:**
- O fusível vai recusar o StartDevice no próximo boot.
- Recuperação: `tools\BC250_RECOVERY.cmd`, `inspect-boot-fuse.ps1 -Reset` e boot frio.
- **NÃO VALIDADO EM HARDWARE:**
  - o FPS do harness com a FPS.3b;
  - a causa da trava.

## 2. Correção de registro (doc 501)

Eu escrevi que, na 153, "o DWM reiniciou" por causa do TDR. **Não se sustenta.** Na 155, sem TDR nenhum, o DWM também reinicia ~10 s depois do primeiro carregamento (1268 às 11:53:16 → 11128 às 11:53:26), e na 153 foi igual (1284 → 2944, 16 s). É a troca normal do bind. O que continua valendo da 153: depois do TDR vieram 1.907 Presents e 35.167 IBs, então o desktop continuou apresentando.

## 3. A trava dura: o que se sabe

| Corrida | Pacote | Resultado | Trava (depois do bind) | Tempo até o reboot manual |
|---|---|---|---|---|
| 149 | M1 | 600 s ok, com programas abertos | — | — |
| 150 | DX.93 | 600 s ok | — | — |
| 151 | FPS.1 | 0x10E aos 49 s (causa: VMID8, doc 501) | — | dump |
| 152 | SDMA.4M | trava | ~183 s | ~73 s |
| 153 | DX.94 | 600 s ok (TDR recuperado aos ~1 s) | — | — |
| 154 | DX.95 | trava | ~129 s | ~44 s |
| 155 | FPS.3b | trava | ~77 s | ~71 s |

**Fatos (P0):**
- Nas quatro travas instrumentadas (143, 152, 154, 155), a GPU estava ociosa, com o MMIO respondendo e nada nosso pendurado.
- Com o KD ligado houve zero travas em duas corridas (doc 484).
- O atalho de dump pelo teclado não funcionou na 145.

**O ponto que muda o próximo passo (P1, comportamento do Windows):**
- O DPC watchdog força **BugCheck 0x133 com dump** quando um processador passa, no acumulado, mais de 120 s em DISPATCH_LEVEL ou acima. A checagem roda no tique do relógio, acima do nível dos dispositivos.
- Nas três travas o reboot veio **antes de 120 s** (44–73 s), então o watchdog nunca teve chance de agir.
- Isso separa duas hipóteses sem mudar uma linha de código:

  | Hipótese | Assinatura | Implicação |
  |---|---|---|
  | **H1** — um processador preso com interrupções altas (por exemplo, um callback nosso sob `DxgkCbSynchronizeExecution` em DIRQL, ou um spinlock) | depois de 2–3 min aparece 0x133 com dump, e a pilha nomeia o culpado | o defeito é de software e tem pilha |
  | **H2** — travamento abaixo do sistema operacional (barramento/fabric, ou um acesso da GPU nos últimos <100 ms, que o batimento não vê) | nada acontece nem depois de 5 min | a investigação sai do código de CPU; nenhuma instrumentação nossa de CPU vai nomear o ponto |

**Proposta:** na próxima trava, **esperar pelo menos 5 minutos antes de desligar**, com o dump completo já configurado (o 0x10E da 151 gerou dump, então o caminho existe). Custo: nenhum código, só paciência.

## 4. Seguir para o DCN agora? Avaliação

**O que o DCN resolveria:**
- modo de vídeo e link DisplayPort próprios;
- mudar resolução e taxa de atualização;
- scanout de qualquer alocação, sem depender do framebuffer herdado do GOP;
- é parte do produto final (P9 da governança).

**O que ele não resolve, pelos dados que temos:**
1. **Nenhuma falha medida aponta para o display.**
   - O único acesso nosso ao DCN é a leitura do contador de quadros do OTG (BAR5 `0x14030`), para o VSync. Ela está coberta pelas migalhas (`OTG_READ`) e **não** estava pendente em nenhuma trava.
   - O scanout funciona: `BYTE_PROOF PASS` e desktop visível.
2. **A trava continuaria igual.** Programar o DCN não mexe na paginação, no SDMA, no VidMm nem no Explorer, que é onde as travas acontecem. E uma trava com o display congelado teria a mesma cara com ou sem DCN próprio.
3. **Traria uma classe de risco nova sobre uma base ainda instável.**
   - A escrita em DCN é proibida fora da P9 por governança.
   - Um erro de programação de DCN produz exatamente tela preta ou travada, que hoje não conseguimos separar das outras causas.
   - Trabalhar DCN agora misturaria duas fontes de trava no mesmo sintoma.
4. **Não é o caminho mais curto para jogos.** O que falta para jogos (estabilidade, FPS, rasterização D3D11 completa, Vulkan) não passa pelo DCN, e o GOP já entrega 2560×1080.

**Recomendação:** **não** seguir para o DCN agora. Fica para depois que:
- a trava tiver nome (ou for descartada como de hardware);
- houver 600 s estáveis em sequência.

O melhor passo agora custa zero código: a mesma corrida (3.306.1.0), e **na trava esperar 5 minutos** antes de desligar. Se vier 0x133, o dump dá a pilha e a trava ganha nome. Se não vier, fica a H2, e a investigação muda de direção.

## 5. Próximo passo de desempenho (depois da estabilidade)
- **FPS.3c:** o mesmo timer de alta resolução no `RetirementTimer` (GFX), que agora tem medição (p50 3,8 ms, cauda até 23 ms).
- **FPS.2:** o lote do journal numa reconstrução só (os FLUSH_TLB são ~30% do ciclo da paginação).

## 6. Adendo — Corrida 156 (mesmo pacote 3.306.1.0, bind às 12:55:39)

Evidência:
- pasta `docs/evidence/2026-09-28_12-55-39-DESKTOP-HQAG6T4-lab-binding/`, com a cauda recuperada (a corrida vai até a linha ~322.448);
- logs da UMD em `umd-logs-pos-travamento/`.

**P0:**
- **FPS.3b reproduzida:** `[FPS3B] ... ATIVO`; SDMA submit → conclusão p50 1,50, p90 1,53, p99 1,62 ms (n=4.397); histograma 42 / 4.333 / 19 em 0 / 1 / 2 ms.
- DX.95 limpa: uma troca do VMID8, zero IB em voo, zero parada do SDMA, zero TDR.
- GFX: p50 3,68, p90 16,6 ms (n=1.058).
- **Trava dura ~1 s depois de o harness abrir:**
  - o harness carregou às 12:58:39,8 e fez `vkCreateInstance`, `vkCreateDevice` e `vkCreateCommandPool`; a última linha é às 12:58:40,203;
  - o log do KMD termina por volta de 12:58:39,98 (alinhado pelo qpc do Explorer; o log do KMD sai com ~0,2 s de atraso), e a última criação registrada é o `CreateProcess` do harness;
  - o reboot manual veio às 13:00:01, ~81 s depois: **de novo antes dos 120 s do DPC watchdog**, porque o critério do §3 ainda não tinha sido passado ao Daniel;
  - sem dump.
- O FPS do harness não foi medido.

**Leitura:** duas das quatro travas recentes (152, 156) vieram no primeiro segundo de um processo novo criando seu device Vulkan. As outras duas (154, 155) vieram com o desktop em uso, sem harness. Isso é **correlação, não causa** (H).

O próximo ensaio continua sendo o do §3: esperar 5 min na trava.
