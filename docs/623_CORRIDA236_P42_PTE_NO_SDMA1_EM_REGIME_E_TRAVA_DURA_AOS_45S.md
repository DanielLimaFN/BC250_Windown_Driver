# 623 — Corrida 236: P4.2 em regime (PTE no SDMA1, 8.414 trocas sem falha) e trava dura aos ~45 s

**Corrida:** 06/10/2026, bind às 15:37:18. Candidato lab **3.384.1.0** (SYS `DA42466A…`), RADV `C6BEC54B…`, boot frio, CMOS em 256 MB (`vram-cmos/2026-10-06_15-36-51`).

**Evidência:** `docs/evidence/2026-10-06_15-37-18-DESKTOP-HQAG6T4-lab-binding/`
- `display-log-this-bind-RECUPERADO.txt`: 8.504.154 bytes, 68.965 linhas, SHA-256 `C1C6AB2B…`, offset 388.856.354 do log vivo;
- `paging-trace-this-bind-RECUPERADO.bin` e `paging-trace-decodificado.json`;
- `umd-logs-pos-travamento/`: dwm, Explorer, ShellExperienceHost, TextInputHost, OneDrive ×2, taskmgr;
- `reset-reason-pos-trava.txt`;
- `ramtest-pos-trava.txt` com **0 bytes**: o teste de RAM não rodou ou não terminou (lacuna).

**Critério:** doc 622 §5.

**Relato do Daniel:** "durante o teste travou e tive que reiniciar".

**Antes do bind:** a primeira tentativa foi recusada pelo preflight porque `EXECUTAR_CORRIDA236…cmd` estava em LF. Foi defeito meu, corrigido para CRLF sem tocar no pacote.

## 1. Medições (P0)

**Pré-condição OK:**
- StartDevice `status=0` (linha 425);
- `[P3B_MMHUB] resultado: LIGADA (34/34)`;
- `[P3_IH] ATIVO`, com prova por TRAP;
- `[CP_EOP] ARMADO`;
- firmware do SDMA1 pelo PSP (tipo 10, checksum `0x00000000` → `0x55B182E3`);
- `[P4_SDMA1] BringUp=OK`, com prova `WPTR=64 RPTR=64 RPTR_wb=64`, fence `0x5D1A0001` sobre poison em 1 poll e relógio de volta (`0x000C0100`);
- `[P4_SDMA1] ANEXADO ao envio do Node 0` (anel `0xE0000`, Go/Done `0xE1080`/`0xE1088`).

**Antes do bring-up, `SDMA1_F32_CNTL` leu `0x00000000` de novo,** como na 235.

**P4.2 em regime.** Últimas linhas `[P4_SDMA1] regime`, uma a cada 10 s:

| Linha | registradas | descartadas | rotas | inline | sem_espaco | token = Go = Done | RPTR1 = cursor1 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 (StartDevice) | 8 | 0 | 0 | 0 | 0 | 0 | 64 |
| 2 | 5.638 | 1.349 | 3.470 | 0 | 0 | 3.470 | 333.188 |
| 3 | 6.089 | 1.439 | 3.815 | 0 | 0 | 3.815 | 366.308 |
| 4 | 12.612 | 2.693 | 7.512 | 0 | 0 | 7.512 | 721.220 |
| 5 (última) | 14.014 | 2.985 | 8.414 | 0 | 0 | **8.414** | **807.812** |

- **8.414 faixas de PTE rodaram no SDMA1.** Em toda linha, Done = Go = token: o SDMA1 sempre terminou a faixa antes da linha seguinte, e o SDMA0 nunca ficou esperando um Done atrasado. O RPTR do SDMA1 sempre alcançou o cursor (anel drenado).
- **Zero `desalinhadas`, `inline` e `sem_espaco`:** o build produz faixas alinhadas e o anel do SDMA1 nunca encheu.
- **`descartadas` ≈ 21% das registradas** (2.985 de 14.014). São faixas que `Bc250PagingSplitTake` não achou inteiras dentro de um envio. Pelo desenho (doc 622 §2), elas rodam dentro do IB do SDMA0, como antes. Não é falha, mas a causa exata **não foi medida**: o VidMm pode dividir um paging buffer em vários envios, ou a faixa pode pertencer a um buffer nunca enviado (ver §4).
- `registradas` > `rotas` + `descartadas` porque o registro conta também as fusões de faixas contíguas e as ainda pendentes.

**Resto do driver:**
- 4.419 `IB EXECUTADO` no SDMA0;
- 210 Presents e 213 `PRIMARY_COPY` (MPO3, SDMA, fence + RPTR);
- 88 `[PT_GPU_SPLIT]`;
- última linha `[P3_IH] regime`: `ISR=4563 traps_sdma=4253 dpc_por_irq=4237 eop_gfx=313 eop_emitidos=313 dpc_gfx_por_irq=313`, `outros=0`, zero overflow.
- **Zero:**
  - `IB_TRIPOD TIMEOUT`, `TDR_QUIESCE`, `GFXHUB_FAULT`;
  - `Blt Timeout`, `DEVICE_REMOVED`, `INSIDE_IB`;
  - `[ActiveOpenAlloc] RECUSADO` (eram 86 na 235 e 80 na 234; aqui o uso foi curto);
  - recusas fora das 16 linhas de `DIRTY_RECTS_RECUSADOS` (bissecção DX50 desligada, já conhecida).

**Desktop:**
- a UMD carregou no dwm, Explorer, TextInputHost e ShellExperienceHost às 15:37:28;
- o Daniel abriu o menu (ShellExperienceHost até 15:37:50) e o Gerenciador de Tarefas (taskmgr às 15:37:59);
- o OneDrive subiu duas vezes (15:37:48 e 15:38:05);
- nenhum log de SearchApp: a Pesquisa não chegou a carregar a UMD antes da trava.

**Trava dura, mesma assinatura das anteriores:**
- o log do KMD cobre 41,5 s pela QPC (`4303490653` → `4718399229`, 10 MHz) e termina em atividade normal: `PAGING_DDI` fechados, um FILL por cópia de 81.920 bytes montado e um envio do SDMA0 de 416 bytes `accepted=1`, sem a linha de execução (a drenagem parou ali; doc 582: o fim do log é a última drenagem, não o último evento);
- no rastro em RAM, todo `SUBMIT_SDMA` tem o seu `RETIRE_SDMA` (o último, fence 4404, está retirado): GPU ociosa no último tique;
- última linha da UMD: dwm às **15:38:10.314**, no meio de um `FlushCommands`;
- **a trava veio ~42 s depois do desktop e ~45 s depois do StartDevice.** É a mais curta da série (as anteriores ficaram entre ~77 e ~584 s);
- o rastro de paginação mostra rajadas logo antes: na janela de 1 s anterior, 5 despejos, 10 FILL e 344 `FlushTlb`.

**Pós-trava:**
- boot às 15:39:10;
- `RESET_REASON=0x40080400`, igual ao de sempre (bit 19 = reset por `0xCF9`; bit 30 = paridade);
- placa em **Código 43** (`CM_PROB_FAILED_POST_START`, StartDevice `0xC0000182` pelo fusível no boot seguinte), com o serviço `bc250mcdm` ainda ligado;
- o rollback do watchdog não rodou: o diário parou em `OBSERVING`.

## 2. Veredito frente ao critério

| Desfecho | Ocorreu? | Por quê |
|---|---|---|
| A | **não** (Lei 10, regra 3) | Todos os sinais de regime estão presentes: `rotas` acompanha `registradas`, `inline`/`sem_espaco` em 0, Done = Go, zero timeout/TDR/fault, desktop desenhado. **Falta o teardown** (`[P4_SDMA1] DESANEXADO`/`DESLIGADO`) porque a máquina travou |
| B | não | 8.414 rotas |
| C | não | Done nunca atrás de Go; zero timeout e TDR |
| D | não | zero fault, zero TDR gráfico, sem BugCheck. O Código 43 é o do fusível depois da trava, como na 234 e na 235, não regressão da P4.2 |
| E | não | a trava dura estava prevista (doc 622 §5) |

**Veredito: parcial.** A P4.2 funciona como mecanismo (P0), mas o desfecho A não fecha sem o teardown, e a trava dura continua.

**O que fica (P0):**
- **A troca entre os dois SDMA funciona na placa.** Foram 8.414 faixas de PTE executadas pelo SDMA1 sob a VMID 8, com `POLL_REGMEM` de memória (RETRY 0xFFF) e FENCE UC, sem uma espera atrasada nem um fault. O risco 1 do doc 622 (a semântica de RETRY 0xFFF) não se manifestou. Ele também não foi provado em geral: Done nunca atrasou, então a espera nunca foi posta à prova.
- **O SDMA1 lê pelo contexto 8 do GFXHUB sem fault** (risco 2 do doc 622).
- **Dividir as PTEs entre os dois SDMA não tira a trava.** A P4.2 sai das causas suficientes.

**Sobre a trava ter vindo mais cedo (~45 s):**
- uma amostra só, dentro de uma série que já varia de ~77 a ~584 s, inclusive por mudanças que depois se mostraram neutras (233 → 234 foi de ~584 para ~136 s);
- não há sinal de que a P4.2 a tenha provocado: zero espera pendente, anel do SDMA1 drenado e GPU ociosa no último tique;
- fica registrada como **H fraca**: o ritmo de trabalho por segundo foi maior nesta corrida (o uso começou logo, com menu, taskmgr e OneDrive em 30 s).

## 3. Tabela do desktop mínimo funcional (pedida pelo Daniel)

| Item | Estado | Corrida 236 |
|---|---|---|
| Driver sobe (Código 0, StartDevice 0) | ✅ | sim |
| Desktop desenhado pela GPU | ✅ | sim, até a trava |
| Menu Iniciar abre | 🟡 | abriu (ShellExperienceHost ativo) |
| Pesquisa | ❌ | não chegou a carregar antes da trava |
| Gerenciador de Tarefas | 🟡 | processo carregou a UMD; tela não confirmada |
| 600 s sem trava | ❌ | ~45 s |
| TDR recupera sem perder o display | 🟡 | não exercitado (zero TDR) |
| Rollback/teardown limpo | ❌ | não rodou (trava) |

## 4. Próximo

**A paridade de hardware com o `amdgpu` está quase esgotada como hipótese de causa da trava.** Saíram:
- CG.1, 4c, D2/GART, P1, P7, P2, P1b, P3.0–P3.2, P6, EOP do CP, P4.1 e P4.2.

**Sobram, do inventário do doc 593 §4:**
- **P3.3:** RPTR do IH por doorbell. É barata, mas improvável: o IH nunca deu overflow;
- **P5:** VM de 4 níveis e 48 bits. É cara, e é a maior diferença estrutural que resta;
- diferenças de **política** que não são registradores, ainda não inventariadas com a mesma régua. Exemplos: o tamanho dos FILL e despejos de tela cheia que o VidMm pede de uma vez, contra os pedaços do TTM; a ordem das invalidações.

**Pendências de código achadas nesta análise** (sem correção feita, porque o pedido foi só analisar):
1. **Mensagem velha:** a linha `[P4_SDMA1] resultado: ATIVO … nenhuma submissao vai para ele nesta etapa` ainda é o texto da P4.1. Com a rota armada, ela é falsa.
2. **Descartadas:** ~21% das faixas são descartadas no envio. Vale um contador do motivo: faixa parcial, buffer nunca enviado, ou outro. Elas rodam corretamente no SDMA0, então não há perda de dados, só de rota.
3. **Teste de RAM pós-trava:** o arquivo saiu com 0 bytes. Conferir `tools\rodar-teste-de-ram.ps1` e a tarefa de coleta automática.

**Recuperação, antes de qualquer novo bind:**
1. `tools\BC250_RECOVERY.cmd` elevado;
2. `powershell -ExecutionPolicy Bypass -File tools\inspect-boot-fuse.ps1 -Reset`;
3. boot frio.

**NÃO VALIDADO EM HARDWARE:**
- teardown do SDMA1, do EOP do CP, do IH e da GART do MMHUB em regime (nenhuma corrida desde a 233 chegou ao fim);
- a espera RETRY 0xFFF sob Done realmente atrasado;
- a causa da trava dura.
