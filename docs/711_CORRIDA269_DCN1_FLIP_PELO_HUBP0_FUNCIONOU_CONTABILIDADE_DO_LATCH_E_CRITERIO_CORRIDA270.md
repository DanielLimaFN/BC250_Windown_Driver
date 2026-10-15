# 711 — Corrida 269: o flip pelo HUBP0 funcionou; contabilidade do latch corrigida; critério da Corrida 270

**Data:** 12/10/2026. **Corrida 269:** bind às 16:27:41, lab **3.418.1.0** (SYS `B624B7ED`), boot frio às 16:21.
**Evidência:** `docs/evidence/2026-10-12_16-27-29-DESKTOP-HQAG6T4-lab-binding/` (`display-log-this-bind-FINAL.txt`).
**Critério:** [doc 710](710_DCN1_FLIP_PELO_HUBP0_D0_A_D4_PACOTE_3418_E_CRITERIO_CORRIDA269.md).

## 1. Corrida 269: o que o log mostra

- **D2 (P0):** o estado do GOP lido bate com a sonda das 249/250:
  - `SURFACE_CONFIG=0x8`, `VIEWPORT=0x04380A00` (2560×1080), `PITCH=0x9FF`;
  - endereço `0x4:60000000`;
  - `OTG_CONTROL=0x80011311`, `MASTER_UPDATE_LOCK=0`.
- **Arme (P0):** `chave DcnFlip=1 (present=1)` e `ARMADO`, com zero recusas.
- **D3 (P0):**
  - **881 flips**: 3 por `SetVidPnSourceAddress` e 878 por MPO3;
  - **readback OK em todos**, com zero falhas e zero recusas;
  - os endereços giram nas três primárias do DWM (`0xACC000`, `0x30CC000`, `0x51DC000`), no domínio previsto pelo doc 710 (endereço do DCN = GOP + VramOff − scanout);
  - **878 Presents de flip sem nenhuma escrita no scanout do GOP.**
- **D4 (P0):** 866 latches vistos pelo próprio HUBP (`EARLIEST_INUSE` igual ao pedido e `FLIP_PENDING=0`).
  - Nas amostras do log, o latch veio de 8 a 27 ms depois do pedido; o máximo foi 49 ms.
- **Teardown (P0):** o HUBP0 voltou ao `0x4:60000000` na passagem de posse e no teardown, com readback OK.
- **Estabilidade na janela de 60 s:**
  - zero TDR, fault do GFXHUB, timeout de IB e BugCheck;
  - 1.309 `DXGI Present` com hr=0 na UMD;
  - nenhum crash do DWM.
- **Rollback:** `RECOVERY_OK_ONLINE`, BasicDisplay em Código 0 em 5 amostras.
- **Operador (Daniel):** "a tela atualizou normal"; janelas funcionaram; **menu não testado**.

**Veredito: desfecho A** (flip validado no registrador e no latch, imagem confirmada pelo operador, 60 s sem trava, rollback limpo). Sobre a trava, 60 s não dizem nada.

## 2. Lacunas da 269

### 2.1 Quinze flips sem destino (881 contra 866) — defeito meu, corrigido

Em `Bc250DcnFlipPollLatch` o latch era decidido pelo **segmento**: se o segmento exibido já era o pedido, a função saía sem contar nada. Isso perdia dois casos:

1. **Flip repetido para o endereço já exibido.** O log mostra isso de fato: os flips #15 e #16 foram os dois para `0xACC000`, assim como #2 e #3. O flip repetido nunca era contado como latchado.
2. **Flip trocado por um mais novo antes do latch.** Ele não era contado em lugar nenhum.

Havia ainda um terceiro balde não contado: os flips pendentes no teardown.

**Correção (desde o 3.419; vai no 3.420.1.0):**
- Os flips são numerados. O DPC de VSYNC resolve pela **sequência**, com um CAS em `ResolvedSeq`.
- Quando o latch do flip N é visto e o último flip resolvido era M, os flips entre M e N contam como **substituídos** (`Bc250DcnFlipSupersededBetween`, no contrato e no teste).
- No teardown, os pendentes são `flips − ResolvedSeq`, e a linha imprime `conta=FECHA` ou `conta=DIVERGE` para `latchados + substituídos + pendentes == flips`.

Nenhuma leitura ou escrita nova em registrador.

**Verificação:**
- teste do contrato com 5 casos mais uma simulação de 10 flips;
- portão novo dentro de `audit_dcn1_flip_pelo_hubp`, provado por 3 mutações (voltar ao critério por segmento, tirar o CAS, tirar a conferência da conta).

### 2.2 `Upload LOCK falhou: hr=0x80070057` no Explorer — não é do DCN

Na 269 foram duas falhas reais no Explorer, que aparecem duplicadas no agregado.

Minha primeira leitura de que eram novas estava **errada**. Ela comparava só o agregado `-FINAL`, que falta em várias pastas. Contando todos os logs da UMD, **toda corrida desde 05/10 tem essas falhas**: 6 a 2.463 por corrida, e 22 na 268.

É a dívida antiga da UMD já registrada no doc 548 §5: `Lock` de alocação sem acesso da CPU. Fica fora da DCN.1; uma mudança por corrida.

### 2.3 Sem prova byte a byte da imagem

A P1 (silêncio da BAR) proíbe a CPU de ler a VRAM. Hoje a prova é que o HUBP leu o endereço (`EARLIEST_INUSE`), não o conteúdo exibido. O CRC do OTG daria essa prova, mas exige escrita em registrador fora da P9 aberta. **Fica declarado, não corrigido.**

## 3. Corrida 270: o que muda (pedido do Daniel: mais coisas e 180 s)

A 270 não é uma repetição da 269. Muda o driver (só medição), o roteiro, o tempo e a carga.

**Medição nova no driver** (só contadores e a leitura do contador de quadros do OTG, que já era lida; nenhuma escrita nova em registrador):

1. **Contabilidade do latch** (§2.1): cada flip termina latchado, substituído ou pendente, e o teardown confere `conta=FECHA`.
2. **`[DCN1_RESUMO]`** a cada 1.024 latches e no teardown. A linha traz:
   - **rotas:** `SET_ADDRESS`, MPO1, MPO3 e flips imediatos;
   - **primária fora do segmento 1** (cópia do SDMA0, sem flip): era um caminho mudo, agora contado e nomeado (`FORA_DO_SEG1`);
   - **latch em quadros do OTG** (0, 1, 2, 3+) e **em tempo** (<16,7 ms, <33,4 ms, <50 ms, mais). Esperado para flip VSYNC: 1–2 quadros. Quadro 0 só em flip repetido para o mesmo endereço;
   - **VSYNC contra o OTG:** quadros do OTG desde o arme, refresh medido em Hz (a 251b mediu 60,5 Hz e o EDID diz 56,13), VSYNC do OTG vistos pelo DPC (os perdidos são quadros sem tique), e VSYNC notificados ao dxgkrnl.

**Roteiro novo (180 s):**
1. menu ×3 (obrigatório);
2. arrastar uma janela e Alt+Tab ×3;
3. Edge com vídeo em janela (~15 s) e em tela cheia (~10 s);
4. **T3** a partir de ~1 min.

O vídeo e a tela cheia exercitam outras rotas de flip, possivelmente imediatos e primárias de outro segmento. A T3 mede pela primeira vez o flip pelo HUBP0 sob a carga que trava.

**Watchdog:** 180 s, dentro do piso normal de 120 s; o lançador não usa mais a exceção de watchdog curto.

## 4. Pacote 3.420.1.0 (Corrida 270)

O 3.419.1.0 (só a contabilidade) foi empacotado e nunca instalado; foi substituído por este.

| Item | Valor |
|---|---|
| SYS assinado | `87DB985383E17DBA44E706C78A74739F689C1B2439D0BE604A1230620810C14A` |
| SYS fonte | `028DF95C9F86E4A5A6543FA01B100F4C152925B116A3A921056081F7630C49B8` |
| INF | `1371B0169F668408DF3A3E4670489CA0DBDFA5EC91468B08E1027FE8D3452573` (DcnFlip=1, DebugTrace=1) |
| CAT | `6C1453F4DCF94D8DDCB0F78D04B8C383BE236FF607EB4F7B9BD1435FC3DD8622` |
| UMD / RADV | `B73F9BDA` / `B4AE2E14`, iguais |
| Cópia | `driver/wdk/package/output_lab_3.420.1.0_270` |
| Lançador | `tools/EXECUTAR_CORRIDA270_DCN1_AMPLIADA_180S.cmd` (preflight `tools/prepare-corrida270-dcn1-ampliada.ps1`) |

**Verificação:**
- teste do contrato: contabilidade, quadros com volta do contador de 24 bits, baldes com os números da 269 e refresh de 60,5 Hz;
- portões novos em `audit_dcn1_flip_pelo_hubp`, provados por 5 mutações (3 da contabilidade, 2 do resumo e do segmento);
- build de laboratório, empacotador e auditoria do pacote: PASS.

**NÃO VALIDADO EM HARDWARE.**

**Antes:** desligar e ligar. O rollback da 269 foi online e o fusível está limpo; não precisa do `BC250_RECOVERY`.

## 5. Critério da Corrida 270 (escrito antes)

**Pré-condição:**
- `[DCN1_FLIP] ARMADO`;
- `[DCN1_RESUMO]` e `[DCN1_TEARDOWN] ... conta=` no teardown — ou, se travar, o último `[DCN1_RESUMO] PERIODICO` e os flips no rastro;
- a T3 iniciada (log em `C:\BC250_PROCESSO_NOVO.log`).

Os desfechos são combináveis: as perguntas são independentes. Para cada pergunta, a resposta de um lado não vale como resposta do outro (Lei 10).

**P1 — rotas novas na tela** (menu, Alt+Tab, vídeo, tela cheia):
- *Assinatura do sucesso:* o operador vê cada passo normal, e o resumo mostra as rotas usadas, com `fora_do_seg1` e `imediatos` contados (zero ou não).
- *Implicação do sucesso:* o flip cobre o uso real do DWM.
- *Assinatura da falha:* um passo mostra preto, lixo, congelamento ou tremor.
- *Implicação da falha:* cruzar a hora com as rotas e endereços daquele instante.

**P2 — a conta fecha:**
- *Assinatura do sucesso:* `conta=FECHA` nas linhas de teardown.
- *Implicação do sucesso:* os 15 flips da 269 eram contabilidade, não flip perdido.
- *Assinatura da falha:* `conta=DIVERGE`.
- *Implicação da falha:* defeito da contabilidade ou corrida entre o Program e o DPC; corrigir antes de qualquer outra coisa.

**P3 — VSYNC:**
- *Assinatura do sucesso:* quase todo latch em 1–2 quadros, `perdidos` perto de 0, e VSYNC notificados próximos dos quadros do OTG.
- *Implicação do sucesso:* o D4 por poll é fiel ao hardware.
- *Assinatura da falha:* muitos latches em 3+ quadros, ou muitos VSYNC perdidos.
- *Implicação da falha:* o poll de 4 ms não acompanha o OTG, e a D5 (VSYNC pela interrupção do DCN no IH) sobe de prioridade.
- *Medição colateral:* o refresh medido confirma ou não os 60,5 Hz contra os 56,13 do EDID (defeito do VidPn, doc 666).

**P4 — sob a T3:**
- **(a)** chega aos 180 s com o flip vivo: a trava não veio nesta janela. Com n=1 e a mediana em 100–200 s, isso **não** diz que as cópias de 11 MB causavam a trava.
- **(b)** trava: comparar com a assinatura de sempre (GPU ociosa, paginação, processo novo nos primeiros 0,9 s). Se a assinatura for igual, as cópias de apresentação no SDMA0 saem das causas suficientes. Se for diferente, minerar.

**E — algo fora do mapa:** descrever o observado antes de julgar.
