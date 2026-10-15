# 716 — Passagem de bastão (12/10/2026): DCN.1 a DCN.3, DIRQL.1, e o que fazer a seguir

**Para a próxima IA.** Leia isto inteiro antes de tocar em código ou propor corrida. Depois leia, nesta ordem:

| Ordem | Documento | O que traz |
|---|---|---|
| 1 | `CLAUDE.md` | Leis 1–10 e a regra das tabelas pós-corrida |
| 2 | [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md) | Seção P9 = Tabela viva do DCN |
| 3 | Docs 709 a 715 | A série do DCN, uma corrida por doc |

Passagem anterior: [doc 678](678_PASSAGEM_DE_BASTAO_2026-10-09_TRAVA_DURA_DEPOIS_DA_LISTA_C1_C4.md), sobre a trava dura.

**NÃO VALIDADO EM HARDWARE:** VSYNC por interrupção, EDID real, cursor monocromático por hardware, troca de resolução, e o fim da trava dura.

---

## 1. Quem é quem e como se trabalha

- **Daniel** é o dono do projeto e **executa toda corrida física**: desliga, liga, roda o `.cmd`, segue o roteiro na tela e relata o que viu.
- **A IA** compila, empacota, audita, escreve o critério antes da corrida e analisa depois.
- **A IA nunca:**
  - faz bind;
  - commita sem pedido explícito (nada desta sessão foi commitado: 31 arquivos modificados ou novos);
  - escreve em registrador fora do que a P9 aprovou.
- **Corrida é cara em tempo, mas o Daniel quer mais entregas por corrida.** Junte mudanças independentes, cada uma com sua assinatura no log e seu bit de chave, para que o resultado de uma não esconda o da outra.
- **Depois de toda corrida,** sem exceção:
  - as 5 seções obrigatórias: avanço, sucessos, falhas, veredito, recuperação;
  - a tabela-checklist da corrida;
  - a **Tabela viva do DCN** inteira (BASE, P9), atualizada.

  Isso vale até o DCN estar completo (decisão do Daniel, 12/10).
- **Fora do escopo, não será feito:** HDR, segundo monitor (a placa tem uma única saída DP) e hotplug (o HPD da placa é defeituoso até no Linux).
- Programas abertos durante a corrida são permitidos. Boot frio (desligar e ligar) entre binds é obrigatório. Arquivos `.bat`/`.cmd` têm de ser CRLF.
- Os docs 680–690 foram escritos por outra IA e **não são fonte confiável** (doc 691).

---

## 2. Estado em 12/10/2026, 21:30

**Placa e pacote:**
- BasicDisplay em Código 0.
- Fusível ausente (limpo).
- Marcador de boot frio armado: o próximo bind exige desligar e ligar.
- Último pacote: lab **3.423.1.0** (SYS `79C8E8A0`, INF `9F092F90`, CAT `EC38D49A`), cópia em `driver/wdk/package/output_lab_3.423.1.0_273`. O INF fonte está em 3.423.0.0: **suba o DriverVer antes do próximo pacote.**
- UMD `B73F9BDA` e RADV `B4AE2E14`, inalteradas desde a 248.

**Chaves do INF de laboratório:**

| Chave | Valor | Efeito |
|---|---|---|
| `DcnFlip` | 1 | Flip pelo HUBP0 |
| `DcnExt` | 0x3D | 1 IRQ, 4 cursor, 8 EDID real, 0x10 DPMS, 0x20 timing real; **0x02 CRC desligado** |
| `DebugTrace` | 1 | Modo debug |

**Corridas do DCN:**

| Corrida | Pacote | Resultado | Doc |
|---|---|---|---|
| 269 | 3.418.1.0 | Primeiro flip pelo HUBP0; 60 s; rollback limpo | 711 |
| 270 | 3.420.1.0 | Trava dura de sempre ~1 s depois do SearchApp | 712 |
| 271 | 3.421.1.0 | 174 s, `conta=FECHA`, 59,997 Hz; a T3 não rodou | 713 |
| 272 | 3.422.1.0 | DCN.2: timing, DPMS e cursor OK; **0x133 nossa** (autotravamento em DIRQL) | 714 |
| 273 | 3.423.1.0 | DIRQL.1 + DCN.3: **175,6 s sem trava com 24 processos da T3**; VSYNC por IRQ e AUX falham com diagnóstico | 715 |

Tabela viva do DCN: ver a BASE, seção P9. Resumo:
- ✅ provados: flip, latch, refresh real, interrupção de flip, DPMS, cursor de cor, HPD (leitura), volta ao GOP, código em DIRQL sem trava.
- ❌ com diagnóstico: VSYNC por interrupção e AUX/EDID.
- ⏸️ desligado de propósito: CRC (causa conhecida).
- ⬜ todo o resto: troca de resolução e refresh, escala, rotação, formatos, gamma, MPO, DirectFlip, VRR, link, áudio, clocks.

---

## 3. A trava dura de sempre: onde está

- Continua **sem causa**.
- Assinatura: a máquina congela com a GPU ociosa, quase sempre nos primeiros ~1 s de vida de um processo novo que usa a GPU, com paginação em voo ou recém-terminada (docs 705–708).
- Dispersão de 8 a 600 s com o mesmo pacote.
- **Saíram das causas suficientes:** a lista longa do doc 678 (4c, D2, P1b, P3, P4, P5, C1–C4, TMR...), a cópia de apresentação pelo SDMA0 (DCN.1 a tirou, n=1) e, agora, o autotravamento em DIRQL.
  - Mineração de 12/10: as corridas 199–231 travaram com o IH desligado, e sem interrupção do dispositivo esse deadlock não existe.
  - **A DIRQL.1 corrige um defeito real (a 0x133 da 272), mas não é a trava.** Não deixe ninguém concluir o contrário pela 273.
- **Regra de roteiro nova, mantenha:** se travar, o Daniel espera **3 minutos** antes do reset.
  - Uma trava em DIRQL vira 0x133 com dump aos 120 s, e o dump nomeia a pilha.
  - Se nada acontecer em 3 min, é congelamento total, a outra classe.
  - Nas corridas com KD (247–250), o Ctrl+Break não respondia na trava de sempre.
- **Medida obrigatória:** repetir a configuração da 273 para ter taxa de trava com n>1 antes de atribuir qualquer melhora.

---

## 4. Erros que cometi (não repita)

1. **Logar ou tomar spinlock comum em código que roda em DIRQL.**
   - O `dxgmms2` chama o nosso `SetVidPnSourceAddressWithMultiPlaneOverlay3` em DIRQL, dentro do `KeSynchronizeExecution`.
   - `KeAcquireSpinLock` baixa o IRQL para DISPATCH, e a nossa interrupção entra na mesma CPU (0x133 da 272).
   - `AtDpcLevel` também não serve: dá ordem invertida entre CPUs.
   - Regra: acima de DISPATCH só use o log sem trava (`Bc250LogHirqlAppend`). Nada de `RingLock` do SDMA0.
   - O portão `audit_dirql1_sem_lock_acima_de_dispatch` cobre isso. Vale para MPO3, `SetVidPnSourceAddress`, callbacks de `DxgkCbSynchronizeExecution`, ISR e possivelmente `SetPointerPosition`.
2. **Chamar de causa da trava algo que só ficou plausível.** Propus que o deadlock em DIRQL explicaria travas desde a 232; a mineração de 30 segundos (contar `[P3_IH] ATIVO` por corrida) refutava. **Minere antes de enunciar.**
3. **Citar proveniência inexistente.** No doc 710 citei `dcn_2_0_1_offset.h`, que não existe. O certo é `dcn_2_0_3_offset.h`, que o `dcn201_resource.c` inclui. Os valores estavam certos e a fonte errada. Sempre confira o `#include` do arquivo `*_resource.c` do ASIC.
4. **Header incompleto não é ausência de registrador.** O `dcn_2_0_3_offset.h` não traz o bloco `dce_dc_dmu_ihc_dispdec`, que tem o roteamento de interrupções. O do DCN 2.0 traz, arquivado em `inc/linux_ref/dcn/dcn_2_0_0_*`. Usar um irmão é P1 com H de que o bloco é igual: diga isso, e **leia antes de escrever**.
5. **Contabilidade que parece perda de dado.** Na 269, 15 "flips sem destino" eram flip repetido e substituído não contados. Na 270, "13 cópias de 11 MB" eram uma linha de log enganosa. Antes de chamar algo de falha, confira se o contador mede o que você pensa.
6. **VSYNC "perdidos" inclui a janela do DPMS.** Com o monitor apagado o DPC de VSYNC sai cedo. 42% dos perdidos da 273 eram isso. Separe por janela antes de comparar corridas.
7. **Ferramentas:**
   - Heredoc do Bash corrompe apóstrofo e barra (use Write/Edit).
   - `sed -i` do Git Bash grava LF (quebra `.bat`/`.cmd`).
   - O empacotador **não compila**: rode `tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup` antes de empacotar e confira strings novas no SYS.
   - `/O2` troca laço de cópia por `memset`/`memcpy`, ausentes no link offline (use ponteiro volatile).
   - O `MEMORY.DMP` só abre com o Daniel copiando como administrador para a pasta da evidência; aí o `cdb` abre normal.
   - Com `-c "!analyze -v; !running -ti; !dpcwatchdog"` a 0x133 se explica em minutos.

---

## 5. O que está pronto para a próxima corrida (274)

Proposta já apresentada ao Daniel, sem OK ainda para a escrita no pad: repetir a 273, que mede a dispersão da trava, e acrescentar três coisas.

1. **D06 (só leitura):**
   - Ler `OTG0_INTERRUPT_DEST` (mm 0x154, byte 0xD850) e `DCHUB_INTERRUPT_DEST2` (mm 0x14F, byte 0xD83C).
   - Hipótese: os bits do OTG0 (VSTARTUP 16, V_UPDATE_NO_LOCK 19, VERTICAL_INTERRUPT0 9) apontam para o microcontrolador, e o bit 0 do flip do HUBP0, que chega, aponta para o host.
   - Se confirmar, a corrida seguinte escreve 0 nesses bits, com retorno ao GOP: precisa de OK do Daniel e ampliação da P9.
   - Atenção: a semântica 0 = host é **H**. Confirme pelo valor lido, comparando com o bit do flip.
2. **D12 (escrita, precisa de OK):**
   - `DC_GPIO_DDC1_MASK` (mm 0x28D0, byte 0x17640): `DDC1CLK_MASK` e `DDC1DATA_MASK` em 0, `AUX_PAD1_MODE` em 1.
   - Exatamente o `dal_ddc_open(GPIO_MODE_HARDWARE, MODE_AUX)` do `dce_aux.c acquire()` (`hw_gpio.c`, `hw_ddc.c`).
   - Só durante a leitura do EDID, com volta ao valor do GOP.
   - A 273 mostrou o AUX0 respondendo `AUX_SW_NON_AUX_MODE` (0x801) e o AUX1 em timeout.
3. **Janela do DCN:** hoje vai de 0xE000 a 0x15400. D06 fica abaixo (0xD83C) e D12 acima (0x17640).
   - Ampliar o mapeamento, ou mapear duas janelas pequenas extras.
   - **Atualize o portão de offsets e de escritores da auditoria** junto.
4. **D17 (opcional, só leitura):** religar o CRC e comparar só na mesma fase do contador do dither.
   - O FMT0 tem dither espacial ligado com `FRAME_COUNTER_MAX=3`; o padrão gira a cada quadro.
   - Valores lidos na 273: `FMT0_BIT_DEPTH_CONTROL=0x921`, `FMT0_CONTROL=0x1300`.

**Processo:**
- Mudança de código → teste de contrato → portão de auditoria → **mutação provando que o portão pega** → build de laboratório → pacote → auditoria do pacote → lançador CRLF → preflight → doc com critério (Lei 10) → BASE e as quatro regras.
- Os geradores desta sessão estão no scratchpad (`mk273.py`, `gov273.py`, `mut273.py`) e servem de modelo.

---

## 6. Oportunidades que vi e não explorei

1. **Completar o flip pela interrupção.** A interrupção de flip do HUBP0 chega 1:1 (D05), mas hoje só conta.
   - Ela pode marcar o latch no ISR/DPC na hora, em vez de esperar o tique de 4 ms, e reduzir o `latch em 2+ quadros`.
   - Não depende de D06. Barato, e mede a latência do flip.
2. **VSYNC perdidos fora do DPMS (6,4%).**
   - Antes da DCN.2 era 4,2%: o resumo, o cursor (14.390 movimentos) e o log em DIRQL ficam no mesmo DPC.
   - Mova o resumo periódico para fora do DPC de VSYNC antes de culpar o hardware.
3. **Cursor em I.**
   - O Windows manda o I como monocromático **com inversão**, que o modo ARGB não reproduz.
   - O modo `MONO`/`COLOR_1BIT_AND` do DCN existe, mas o upstream diz "todo: clarify" e não há layout de bits. É Q: não invente.
   - Se um dia houver documentação, é o caminho. Até lá, software, que é correto.
4. **D19, troca de refresh no mesmo modo** (só `V_TOTAL`/`V_TOTAL_MIN/MAX`).
   - É o degrau mais barato antes da troca de resolução (D18) e a base do VRR (D27).
   - Exige o Daniel abrir escrita de timing; proponha com snapshot e volta do V_TOTAL.
5. **D18, troca de resolução:** o item mais caro (OTG, DCCG/clock de pixel, HUBP viewport/pitch, DPP scaler, link). Antes dele:
   - dominar a escala no DPP (D20): uma fonte menor que 2560×1080 em tela cheia **sem mexer no link**, que é o que o Windows mais pede em jogos;
   - ter o EDID real (D14) para listar os modos.
6. **Trava dura, com a coleta que já temos:**
   - Correlacionar o fim de cada trava com o último evento DCN (flip, latch, cursor). Com o log em DIRQL sem trava, as linhas do MPO3 agora saem.
   - Se a trava sempre cai fora do flip, o DCN sai de vez da investigação.
7. **Auditoria:** o portão `audit_dirql1` cobre MPO3 e ISR. Vale estender a todas as DDIs que o WDK documenta como podendo rodar em DIRQL:
   - callbacks de `DxgkCbSynchronizeExecution`;
   - `DxgkDdiInterruptRoutine`;
   - `DxgkDdiSetPointerPosition` (confira a IRQL no WDK).
8. **Higiene:** são 31 arquivos não commitados.
   - Peça ao Daniel para commitar a série 269–273, com a mensagem apontando os docs 710–716, antes de começar mudança grande.
   - O histórico do git protege contra a perda que o file-history já salvou antes.

---

## 7. Onde fica cada coisa

| O quê | Onde |
|---|---|
| Contrato puro do DCN (testável offline) | `driver/core/bc250_dcn_flip_contract.h`, `driver/core/bc250_dcn_ext_contract.h/.c`, teste `driver/tests/dcn_ext_contract_test.c` |
| Runtime do DCN (DCN.1/2/3, DIRQL.1) | `driver/wdk/bc250_display_ddi.c` (busque `s_Bc250Dcn`, `s_Bc250Dcn2`, `Bc250LogHirql`) |
| ISR do IH (cliente DCE) | `driver/wdk/bc250_sdma_present.c`, `Bc250DisplayDcnIsr(src, dw4)` |
| Caps do cursor | `driver/wdk/bc250_mcdm_callbacks.c` |
| Leitura das chaves | `driver/wdk/bc250_active_miniport.c` (`DcnFlip`, `DcnExt`); reserva de VRAM do cursor antes da SOMBRA |
| Portões | `tools/audit-bc250mcdm-safety.py`: `audit_dcn1_*`, `audit_dcn2_sobre_o_gop`, `audit_dirql1_sem_lock_acima_de_dispatch` |
| Upstream de referência | `inc/linux_ref/dcn/` (`dcn_2_0_3_*`, `dcn_2_0_0_*`, `irq_service_dcn201.c`, `dce_aux.c`, `hw_ddc.c`, `hw_gpio.c`, `dcn10_link_encoder.c`, `dcn20_hubp.c`/`dpp.c`, `dcn10_optc.c`...) |
| Registradores catalogados | `docs/03_CATALOGO_DE_REGISTRADORES.md`, seção "DCN.1–DCN.3 e candidatos" |
| Lançadores | `tools/EXECUTAR_CORRIDA27x_*.cmd`, preflights `tools/prepare-corrida27x-*.ps1`, `tools/DESLIGAR_MONITOR.cmd` |
| Evidência | `docs/evidence/2026-10-12_*-lab-binding/` (272 com `MEMORY-0x133.DMP`) |

**Como achar o log de uma corrida:**
1. Leia o `display-log-baseline.json` da pasta: ele dá o `start_offset` dentro de `C:\BC250_DISPLAY_LOG.txt`, que é cumulativo.
2. Leia a partir dali com `FileShare ReadWrite`.
3. Os resumos úteis são `[DCN1_RESUMO]`, `[DCN2_RESUMO]` e `[DCN1_TEARDOWN] ... conta=`.
4. A T3 grava em `C:\BC250_PROCESSO_NOVO.log`.

Bom trabalho. O DCN está a meio caminho e bem instrumentado. A trava dura continua sendo o problema principal, e o DCN não é a causa dela (L1, 251b, 263b, 268, 270 e agora a mineração do IH).
