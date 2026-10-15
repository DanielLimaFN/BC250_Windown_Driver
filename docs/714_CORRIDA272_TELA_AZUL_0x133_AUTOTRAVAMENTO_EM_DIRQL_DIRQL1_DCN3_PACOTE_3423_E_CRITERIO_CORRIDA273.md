# 714 — Corrida 272: tela azul 0x133 por autotravamento em DIRQL; DIRQL.1 + DCN.3 (pacote 3.423) e critério da Corrida 273

**Data:** 12/10/2026. **NÃO VALIDADO EM HARDWARE** para nada da DIRQL.1 e da DCN.3.

## 1. Corrida 272 (20:20, lab 3.422.1.0, 180 s)

Evidência: `docs/evidence/2026-10-12_20-20-30-DESKTOP-HQAG6T4-lab-binding/`.
- Inclui o dump `MEMORY-0x133.DMP`, copiado pelo Daniel.
- O log do bind é o trecho do log cumulativo a partir do offset 1029045561.

**Linha do tempo (P0):**
- Bind às 20:20:41; a T3 começa às 20:21:02.
- 21 processos da T3 completos (código 0).
- O log do KMD acaba ~16 ms depois de o 21º processo terminar (20:23:06,6), no meio da paginação.
- Às ~20:25:06 vem a **BugCheck 0x133** (DPC_WATCHDOG_VIOLATION, Arg1=1 tempo acumulado, Arg2=0x1E00 = 120 s), e a máquina reinicia sozinha às 20:25:30.
- Depois do reboot, o fusível recusou o StartDevice. O rollback online fechou limpo (`RECOVERY_OK_ONLINE`, BasicDisplay Código 0).

### 1.1 O dump (P0)

Pilha da CPU 0, a referência do watchdog (641 de 641 capturas iguais):

```
VidSchiWorkerThread -> VidSchiExecuteMmIoFlip -> DpSynchronizeExecution -> KeSynchronizeExecution
 -> VidSchiExecuteMmIoFlipMultiPlaneOverlayAtISR3 -> Bc250DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3+0xbb
 -> Bc250DisplayLog -> KeAcquireSpinLockRaiseToDpc (anel do log)
 -> [interrupcao] KiCallInterruptServiceRoutine -> KxWaitForSpinLockAndAcquire   (para sempre)
```

- O símbolo `Bc250DxgkDdiUpdateMonitorLinkInfo+0x16c` na pilha é dobra de COMDAT: é o `Bc250LogRingAppend`.
- **CPU 8:** VidMm em `FlushPagingBufferInternal -> VidSchIsMonitoredFenceSignaled`, esperando lock.
- **CPU 10:** `DxgkSignalSynchronizationObjectFromGpu2`, esperando lock.
- As demais CPUs estão ociosas.

**Mecanismo (P0 pela pilha, P1 pelo contrato do Windows):**
1. O dxgmms2 chama o nosso flip MPO3 em DIRQL, dentro do `KeSynchronizeExecution`, com o spinlock de interrupção do dispositivo preso.
2. A primeira linha da DDI ([bc250_display_ddi.c](../driver/wdk/bc250_display_ddi.c), `=== SetVidPnSourceAddressWithMultiPlaneOverlay3 ...`) loga.
3. O log usa `KeAcquireSpinLock`, que **baixa o IRQL para DISPATCH** (só é permitido em <= DISPATCH).
4. Com o IRQL baixo, a interrupção do IH chegou na mesma CPU. O ISR girou no spinlock de interrupção que a própria CPU segurava.

**Por que agora:**
- O defeito existe desde que o IH foi armado (P3, Corrida 232), com o flip por essa rota.
- A interrupção de flip do HUBP0 (DCN.2) dispara justamente em volta do flip, na janela do log.
- A copia pelo SDMA0 em DIRQL (o caminho antigo do MPO3, antes da DCN.1) tem a mesma família de risco, com o `RingLock`.

**Relação com a trava dura de sempre (P2, inferência):** provavelmente outra coisa.
- Neste dump o Windows congelou e fotografou todas as CPUs, inclusive a travada.
- Nas corridas com KD (247–250), nem o Ctrl+Break respondia na trava de sempre.
- Não dá para descartar que algumas travas desde a 232 tenham sido este defeito: o Daniel aperta o reset em 10–15 s, e o 0x133 só sai aos 120 s.
- Por isso, a partir da 273, o roteiro manda **esperar 3 minutos antes do reset**.

### 1.2 DCN.2 na 272

| Item | Resultado (P0, salvo indicado) |
|---|---|
| Timing real | ✅ OTG0 2784×1111, **59,999 Hz**; VidPn aceitou e o desktop subiu |
| DPMS | ✅ `D3 -> OTG apagado`, `D0 -> OTG aceso`; o monitor voltou (operador) |
| Cursor por hardware | ✅ 33 formas de cor e 1.868 movimentos, readback OK; o operador viu o cursor acompanhar |
| Flip do HUBP0 pela interrupção | ✅ 799 interrupções (0x4F) para ~800 flips |
| VSYNC pela VSTARTUP | ❌ 0 interrupções em ~8.580 quadros |
| CRC do OTG | ❌ 7.669 mudanças em 7.674 amostras, no máximo 4 quadros iguais com a tela parada |
| EDID real | ❌ AUX0 sem resposta (resposta 5, 3/3 falhas, 0 bytes); EDID sintetizado mantido |
| 180 s | ❌ BugCheck 0x133 aos ~145 s (seção 1.1) |

Detalhes:
- **VSTARTUP:**
  - Nenhuma entrada DCE desconhecida chegou ao IH (`outros=0`).
  - Antes do arme, o `OTG_GLOBAL_SYNC_STATUS` lia 0x00104104, com `VSTARTUP_EVENT_OCCURRED=1`. Ou seja, o evento existe no OTG mas não vira interrupção.
- **VSYNC perdidos:** 905 de 8.580 (10,5%), contra 4,2% na 271; ~300 deles caem na janela do DPMS.
- **CRC, hipótese (H):** dithering temporal deixado pelo GOP no FMT0. A 273 lê o FMT0, só leitura.

## 2. O que entra na 273 (escolha do Daniel)

| Mudança | O que faz | Escreve |
|---|---|---|
| **DIRQL.1** (correção) | (a) Acima de DISPATCH o log não toma lock: cada linha vai a um slot próprio, reservado por CAS, e é copiada ao anel pelo próximo log em <= DISPATCH (contadores `linhas`/`descartadas`). (b) A cópia pelo SDMA0 em DIRQL com o IH armado falha fechada (só acontece se a DCN.1 recusar um flip). | nada |
| CRC desligado | `DcnExt=0x3D` (bit 0x02 fora). Não serve a nenhuma função do Windows. | — |
| **DCN.3 VSYNC** | Arma juntas as três fontes de quadro do `irq_service_dcn201`: VSTARTUP (0x3C), VUPDATE sem lock (0x57) e VLINE0 (0x1E, ext_id 6, linha = início do blank). O ISR passa o `ext_id` (dw4 da entrada). Um VSYNC por quadro do OTG, qualquer que seja a fonte (`mesmo_quadro` conta as juntadas). O resumo lê o `INT_STATUS` de cada uma. | `OTG_GLOBAL_SYNC_STATUS` (+VUPDATE), `OTG_VERTICAL_INTERRUPT0_POSITION/CONTROL` |
| **DCN.3 AUX** | Foto completa do AUX antes e depois da falha. Inicialização como `dcn10_aux_initialize` (`AUX_HPD_SEL` = HPD com sinal, `AUX_LS_READ_EN` 0, janela de recepção 1/4). Tenta AUX0 e depois AUX1. CONTROL e DPHY_RX_CONTROL0 voltam como o GOP deixou. Também lê `OTG_VSTARTUP_PARAM` e o FMT0, só leitura. | `AUX_CONTROL`, `AUX_DPHY_RX_CONTROL0` (só durante a leitura) |
| **DCN.3 cursor** | Monocromático (AND e depois XOR, cada um Width × Height; o XOR fica em `pPixels + Height * Pitch`, como no VBoxMPWddm) e cor mascarada, convertidos para o ARGB já provado. Forma com **inversão** (XOR com a tela) é recusada (`NEEDS_XOR`) e fica no cursor por software do Windows, sem aproximar. A forma anterior é escondida. Caps: Monochrome, Color e MaskedColor. | os mesmos do cursor |

**Proveniência:**
- `irq_service_dcn201.c` e `irqsrcs_dcn_1_0.h` (fontes 0x57 e 0x1E/6, campos de enable e clear).
- `dcn_2_0_3_offset.h` e `dcn_2_0_3_sh_mask.h` (offsets e máscaras, conferidos pela auditoria).
- `dcn10_link_encoder.c` (v6.12, baixado agora para `inc/linux_ref/dcn/`), `dcn10_aux_initialize`.
- WDK, `DXGKARG_SETPOINTERSHAPE` e `DXGK_POINTERFLAGS`.
- VirtualBox `VBoxMPWddm.cpp` (offset do XOR).
- O modo de cursor com XOR do DCN (`COLOR_1BIT_AND`/`MONO`) não tem layout no upstream (`todo: clarify`), por isso fica de fora (Q).

**Ficou fora:** hotplug (decisão do Daniel: o DP desta placa tem uma falha conhecida no Linux) e o flip direto (exige a UMD D3D11.1).

### Verificação offline

- `dcn_ext_contract_test` cobre:
  - classificação das fontes;
  - posição do VLINE0;
  - init do AUX;
  - mono sem e com inversão, inclusive a leitura do XOR na linha certa;
  - mascarada opaca, transparente, com XOR e com máscara inválida.
- Portões novos:
  - `audit_dirql1_sem_lock_acima_de_dispatch`;
  - `audit_dcn2_sobre_o_gop` ampliado: ISR com as 4 fontes sem log e sem lock, desarme do VLINE0/VUPDATE, AUX liberado em toda saída, caps só com o cursor pronto e offsets novos.
- **10 mutações, todas pegas:**
  - log em DIRQL volta ao lock;
  - cópia do SDMA0 em DIRQL volta;
  - ISR loga;
  - desarme sem a posição do VLINE0;
  - AUX sem restaurar o RX0;
  - inversão aproximada por branco;
  - XOR lido na linha errada;
  - caps de mono fora do cursor pronto;
  - ISR do IH sem `ext_id`;
  - forma recusada sem esconder a anterior.
- Builds de contrato e de laboratório limpas; auditoria de segurança PASS; auditoria do pacote PASS.

## 3. Pacote 3.423.1.0 (Corrida 273)

| Item | Valor |
|---|---|
| SYS assinado | `79C8E8A0285C27964809EAAACB4E897DA8BE7DFEF5182922B7C10BAC825E7739` |
| SYS fonte | `ED1C44220CBFB98BB5D324377C1C7F233C9204E791C5FD8B329EE8BB13633488` |
| INF | `9F092F9014DD4B67FF864EDAEF917874B45028407F58332CA0E07E553E2DA273` (DcnFlip=1, DcnExt=61, DebugTrace=1) |
| CAT | `EC38D49A6FA7BE5AD7E0F112BDEA03CE33EF317AF1080F41C2B3E72EF194659B` |
| UMD / RADV | `B73F9BDA` / `B4AE2E14`, iguais |
| Cópia | `driver/wdk/package/output_lab_3.423.1.0_273` |
| Lançador | `tools/EXECUTAR_CORRIDA273_DIRQL1_DCN3_180S.cmd` (preflight `tools/prepare-corrida273-dirql1-dcn3.ps1`) |

**Antes:** desligar e ligar. O fusível está limpo (o arquivo não existe) e o marcador de boot frio está armado.

## 4. Critério da Corrida 273 (escrito antes)

**Pré-condição:**
- `[DCN2] chave DcnExt=0x3D`;
- `[DCN2_IRQ] ARMADO` com as três fontes;
- `[DCN1_FLIP] ARMADO`.

As perguntas são independentes; assinatura e implicação ficam separadas (Lei 10).

**1. DIRQL.1**
- *Sucesso — assinatura:* nenhuma BugCheck 0x133 com `Bc250DisplayLog`/`KeAcquireSpinLockRaiseToDpc` na pilha; o `[DCN2_RESUMO]` mostra `log em DIRQL linhas>0` (o caminho novo rodou) e `descartadas` baixo.
- *Sucesso — implicação:* o autotravamento sai.
- *Falha — assinatura:* outro 0x133.
- *Falha — implicação:* ler a pilha nova; o defeito tem outra entrada em DIRQL.

**2. VSYNC**
- *Sucesso — assinatura:* pelo menos uma das fontes (`vstartup`, `vupdate`, `vline0`) com contagem ≈ quadros do OTG; `vsync_por_irq` subindo; `perdidos` perto de 0.
- *Sucesso — implicação:* VSYNC pelo hardware; a fonte que chegou passa a ser a única.
- *Alternativa — assinatura:* as três em 0, com `INT_STATUS` = 1 no resumo.
- *Alternativa — implicação:* a interrupção do OTG nasce no OTG e não chega ao IH (roteamento no DMU, fora do mapa); o flip, que chega, segue sozinho.

**3. AUX**
- *Sucesso — assinatura:* `DPCD ... resposta=0 bytes=16` em algum canal; EDID com checksum OK.
- *Sucesso — implicação:* nome real do monitor, se o DTD casar.
- *Falha — assinatura:* os dois canais sem resposta.
- *Falha — implicação:* a foto `[DCN3_AUX]` (HPD_DISCON, RX_TIMEOUT, NO_DET...) diz em que fase falha.

**4. Cursor**
- *Assinatura:* `mono`/`mascaradas` > 0 ou `com_xor` > 0.
- *Operador:* o I de texto aparece (por hardware ou por software), sem cursor duplicado.
- *Falha — assinatura:* cursor duplicado ou sumido.
- *Falha — implicação:* reverter as caps de mono e mascarada.

**5. CRC e FMT**
- *Assinatura:* a linha `[DCN3_FMT]` registra o dithering.
- *Implicação:* responde a hipótese do CRC sem religá-lo.

**6. Trava**
- *Assinatura:* se travar e o operador esperar 3 minutos, o desfecho é (a) 0x133 com dump ou (b) nada.
- *Implicação:*
  - (a) a pilha nomeia a trava;
  - (b) a trava de sempre é um congelamento total, outra classe, e essa diferença já é informação.

**E — algo fora do mapa:** descrever o observado antes de julgar.
