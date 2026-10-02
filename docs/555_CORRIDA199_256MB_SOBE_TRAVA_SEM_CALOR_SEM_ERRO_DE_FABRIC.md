# 555 — Corrida 199: 256 MB sobe; a trava dura vem sem calor e sem erro de fabric no FCH

**Corrida:** 01/10/2026. Bind às 21:44:51 após boot frio. A máquina travou ao abrir a Pesquisa do menu Iniciar. Reset pelo botão; boot às 21:47:52.
**Candidato:** lab **3.346.1.0**, que é o 3.344.1.0 mais a instrumentação I.PLAT (doc 553), com a M1b.1 dormente (doc 554). CMOS em **256 MB** aplicados.
**Evidência:** `docs/evidence/2026-10-01_21-44-51-DESKTOP-HQAG6T4-lab-binding/`
- `display-log-this-bind-RECUPERADO.txt` (7.870.420 bytes, sha256 `d27f50c6…`);
- `paging-trace-this-bind-RECUPERADO.bin` (sha256 `4faa947f…`) e `plataforma-bps1-resumo.txt`;
- `reset-reason-pre-bind.txt` e `reset-reason-pos-trava.txt`;
- hashes em `SHA256-recuperados.txt`.

O watchdog não rodou (a máquina travou), então os arquivos `-FINAL` não existem. Os logs da UMD do fim da corrida dependem de cópia elevada de `C:\Windows\Temp`.

---

## 1. Relato obrigatório

**1. Houve avanço?** Sim, de diagnóstico. Funcionalmente não: travou como a 198.
- **Funcional:** o driver subiu com 256 MB e o desktop apareceu. Mais de 2 minutos com ~1.130 Presents, sem TDR nem fault.
- **Diagnóstico:** pela primeira vez, uma trava dura foi medida com temperatura, estado do SDMA1 e motivo de reset do hardware. Três suspeitos caíram.

**2. Sucessos (P0, salvo quando indicado):**
- **256 MB lidos da placa como previsto (doc 551):**
  - `[M2_BASE] … =0x460000000`;
  - `VramTotalMiB=256 CpuVisibleMiB=256`;
  - segmento 3 com 0 MiB (`sem_vram_alem_da_janela_de_cpu`);
  - zero `[M2_SEGMENTO3_ALOC]` e zero `[M2_LOCK_ABERTURA]`.
- 15 `CommitVidPn`, 1.132 `[Present #`, 5 `BYTE_PROOF PASS`, 720 `COMPOSITE_BLT OK`, 1.534 `IB EXECUTADO`.
- Zero `TDR_QUIESCE`, `GFXHUB_FAULT`, `DEVICE_REMOVED`, `IB_TRIPOD TIMEOUT`, `StopDevice` e `Lock2 failed`.
- **Sem falta de memória nos primeiros ~30 s:** zero `-2`, `OUTOFMEMORY` e `Lock2` nos logs da UMD do DWM, do Explorer e de outros três processos (snapshot de 21:45).
- **A instrumentação I.PLAT funcionou na primeira corrida:**
  - 138 amostras de Tctl, todas plausíveis e com o KMD concordando com o decodificador;
  - o motivo de reset foi lido antes e depois da trava.

**3. Falhas:**
- **Trava dura ao abrir a Pesquisa**, ~140 s após o início do rastro. Tela congelada, sem BugCheck.
  - Os dois escritores pararam juntos: a thread do rastro (último tick 1.396, registro completo) e o log do KMD.
  - Não há dump, e não havia KD.
- **32 Presents recusados** a partir do #698, todos `BLT_DESTINO RECUSADO: GEOMETRIA_DA_ORIGEM_DESCONHECIDA` seguido de `RECUSADO: copia GPU nao concluida; status=0xC01E0200`.
  - São recusas honestas: o scanout não foi tocado.
  - É uma classe **nova contra a 198**, que teve zero. Ligação com 256 MB ou com a trava: NÃO DETERMINADA.
- 10 `Journal NO_SPACE` no `ActiveUpdatePT`, contra 1 na 198.

**4. Veredito frente ao doc 553 §3.3: E (fora do mapa).**

| Desfecho | Ocorreu? | Por quê |
|---|---|---|
| A | Não | Travou |
| B (calor) | Não | Tctl de 86,25 °C no início, caindo até ~80,6 °C no fim. Mínimo 75,9 °C, máximo 86,25 °C. Nada perto de 95 °C, nada subindo |
| C (erro de fabric/memória) | Não | Exige um bit de erro **novo**. Antes do bind o registro estava em `0x40000400` (o bit 30 "parity error" e o bit 10 **não limpam** por escrita, medido duas vezes). Depois da trava: `0x40080400`. O **único bit novo é o 19** ("software wrote 0x6 to 0xCF9"). Nenhum sync flood (27/31), watchdog (25) ou thermtrip (0/9) |
| D (sonda travou) | Não | O último BPS1 é um DEPOIS completo (tick 1.390); a parada foi no tick 1.397 |

**O observado:** a trava dura vem com temperatura normal, SDMA1 desligado e sem erro de hardware que o FCH registre. O único rastro no FCH é o próprio reset.

**5. Recuperação e pendências:**
- **Estado ao final:** BC-250 em **Código 43** (`CM_PROB_FAILED_POST_START`), com o 3.346.1.0 instalado. O fusível recusou o StartDevice no boot depois da trava, como projetado. Sem rollback, porque o watchdog morreu na trava.
- **Antes de qualquer bind:** `tools\BC250_RECOVERY.cmd` elevado, `tools\inspect-boot-fuse.ps1 -Reset` e boot frio.
- **NÃO VALIDADO EM HARDWARE:**
  - 256 MB sob carga longa;
  - a origem do bit 19 (ver §2.2);
  - a ligação entre as recusas de geometria e a VRAM pequena.

## 2. Leitura

### 2.1 Três suspeitos da trava caem (P0)
1. **Calor:** fora. A placa estava mais fria no fim do que no início, e a 79 °C o Linux não travou (doc 552).
2. **SDMA1:** fora como agente ativo.
   - Estado idêntico em 138 amostras: `STATUS=0x46DEE557` (IDLE, RB_EMPTY); `F32_CNTL=1` (HALT); `GFX_RB_CNTL=0x80840000` (RB_ENABLE=0). Decodificado por `inc/linux_ref/gc_10_1_0_sh_mask.h` (P1).
   - Ninguém o ligou.
   - Ainda pode importar o fato de o Linux o **usar** e nós não; isso é outra pergunta.
3. **Erro de fabric/memória sinalizado:** o FCH não registrou nada novo.
   - Em AMD, um erro não corrigido de memória costuma acabar em sync flood (bit 27) antes do reset.
   - Não aconteceu. Isso pesa contra "paridade da GDDR6" como mecanismo da trava (H).

### 2.2 Correção do doc 552 sobre o "parity error"
O doc 552 tratou o `0x40080400` lido pelo Linux às 14:52 como evento **novo** entre os boots, porque o Linux limpa o registrador. **Isso não se sustenta.** Os bits 30 e 10 não limpam por escrita, então podem ser antigos.

Nas três leituras de hoje (21:40, 21:44 e 21:52), o bit 19 é o único que entra e sai.

A origem do bit 19 é **H**:
- é compatível com o botão de reset sendo traduzido pelo firmware em escrita `0x6` na `0xCF9`;
- e também com o reinício por software, que faz o mesmo.

Teste barato para separar as duas: apertar o reset com o Windows **saudável** e ler.

### 2.3 O GRBM_STATUS "RSMU pendente" é o normal desta placa (P0)
O `GRBM_STATUS` estável na 199 é `0xA0003028`: GUI_ACTIVE, CP_BUSY, CB/DB_CLEAN e **RSMU_RQ_PENDING** (bit 5). A 198, **sem** a sonda nova, tem o mesmo valor em 1.904 de 1.944 batimentos. O bit 5 não foi introduzido pela escrita no seletor SMN.

### 2.4 A última operação difere entre as travas
- **199:** o fim do log é a preparação do 2º FILL de 11.059.200 bytes (uma superfície 2560×1080) no SDMA. O 1º, idêntico, concluiu (#1.534, tripé de duas pernas).
- **198:** terminou em submissões gráficas no Node 1; o último FILL estava 121 linhas antes e era de 256 KB.

Na 199, 16 de 17 FILLs de 11 MB concluíram. **O FILL grande não é um elemento comum** e fica como H. O que é comum às duas é a parada simultânea de toda a CPU, sem BugCheck: o mesmo quadro das travas desde a 157.

## 3. Próximo passo (justificado)
Sobram como diferenças nomeadas contra o Linux, que não trava: **SMU** (relógios e tensões) e **fences por interrupção**, mais clock gating, HDP e MMHUB (doc 552 §3.2). Nenhuma delas tem instrumento ainda.

Proposta, em ordem e sem gastar corrida antes de ler:
1. **Leitura offline (W3):**
   - o que o `amdgpu` faz com a SMU na Cyan Skillfish (`cyan_skillfish_ppt.c`) no init e em runtime;
   - o que nós deixamos no estado do POST.
2. **Instrumento barato a seguir:**
   - o estado do SDMA0 (RPTR/WPTR/STATUS) no batimento de 100 ms, que hoje só cobre o anel gráfico;
   - e a frequência atual de relógio da GPU, se houver registrador com procedência.

   Isso diria se o SDMA0 estava no meio de um FILL quando tudo parou.
3. **Teste de 1 minuto para o Daniel**, quando for conveniente, sem bind: apertar o botão de reset com o Windows normal e rodar `bc250-vram.ps1 -Ler`, para fechar a origem do bit 19.

A M1b continua dormente. A memória não foi o problema nos primeiros 30 s, e a trava veio antes de a carga crescer.
