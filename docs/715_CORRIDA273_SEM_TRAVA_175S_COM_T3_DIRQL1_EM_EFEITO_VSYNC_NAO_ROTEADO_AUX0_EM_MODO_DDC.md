# 715 — Corrida 273: 175 s sem trava com a T3, DIRQL.1 em efeito; interrupção do OTG não roteada ao IH; AUX0 fora do modo AUX

**Data:** 12/10/2026, 21:07–21:11. **Pacote:** lab 3.423.1.0 (SYS `79C8E8A0`).

**Evidência:**
- `docs/evidence/2026-10-12_21-07-58-DESKTOP-HQAG6T4-lab-binding/`;
- log do KMD a partir do offset 1038303676 do log cumulativo;
- `C:\BC250_PROCESSO_NOVO.log`, sessão `20261012-210837-8452`.

## 1. Resultado (P0 salvo indicado)

- **Estabilidade:**
  - **175,6 s armado, sem trava, sem BugCheck e sem TDR de execução.** As linhas `TDR_*` são só a reconstrução do anel no boot.
  - T3: 24 processos novos completos. O 25º saiu com código 2 (`ADAPTADOR_NAO_E_BC250`) porque o rollback já tinha devolvido o BasicDisplay.
  - Rollback `RECOVERY_OK_ONLINE`; nenhum evento 41/1001/6008 depois das 21:05.
- **DIRQL.1 em efeito:**
  - **2.310 linhas** registradas pelo log sem trava em DIRQL, 0 descartadas;
  - 0 cópias recusadas em DIRQL (nenhum flip recusado).
- **DCN.1:**
  - 2.136 flips, todos latchados, `conta=FECHA`;
  - 2.132 MPO3 sem cópia;
  - HUBP0 de volta ao GOP com readback OK.
- **Timing real:** 59,999 Hz aplicado; OTG 60,002 Hz em 10.535 quadros.
- **DPMS:** apagar e acender OK; o operador confirmou o monitor voltando.
- **Cursor:**
  - 138 formas de cor por hardware e 14.390 movimentos;
  - **9 formas monocromáticas recusadas por inversão** (XOR), que ficaram no software do Windows;
  - zero monocromáticas sem inversão e zero mascaradas.
  - Relato do operador sobre o cursor em I: pendente.
- **VSYNC pela interrupção: não chega.**
  - VSTARTUP, VUPDATE e VLINE0: **0 entregas ao IH** cada.
  - No fim, as três estavam com `INT_STATUS=1` no OTG (`GLOBAL_SYNC=0x0010D10D`, `VINT0_CTL=0x00011100`). As interrupções **nascem no OTG e não são roteadas ao IH**.
  - A de flip do HUBP0 chega 1:1 (2.136).
  - O VSYNC seguiu pela consulta de 4 ms, com 1.122 de 10.535 perdidos (10,6%, igual à 272).
- **AUX: o canal certo é o AUX0, e ele não está em modo AUX.**
  - **AUX0:** a transação terminou (`DONE=1`) com `SW_STATUS=0x801`. O bit 0x800 é **`AUX_SW_NON_AUX_MODE`** (`dcn_2_0_3_sh_mask.h`): o pad está em modo DDC/I2C, não AUX.
  - **AUX1:** `RX_TIMEOUT`, ou seja, nada ligado nele.
  - O AUX voltou como o GOP deixou.
- **CRC/FMT:** `FMT0_CONTROL=0x00001300`, `FMT0_BIT_DEPTH_CONTROL=0x00000921`. A decodificação dos campos de dithering fica para o próximo passo (offline).

## 2. Veredito frente ao critério do doc 714

| Pergunta | Desfecho |
|---|---|
| 1. DIRQL.1 | **Sucesso:** sem 0x133 e com o caminho novo exercitado (2.310 linhas) |
| 2. VSYNC | **Alternativa:** as três em 0 com `INT_STATUS=1`; a interrupção nasce no OTG e não chega ao IH (roteamento, fora do mapa) |
| 3. AUX | **Falha com diagnóstico:** o AUX0 está no conector e em modo não-AUX (`NON_AUX_MODE`) |
| 4. Cursor | Inversão recusada 9/9 sem cursor duplicado no log; operador pendente |
| 5. FMT | Registrado |
| 6. Trava | Não houve |

## 3. Leitura (H, não P0)

**A trava:**
- A 273 é a primeira corrida com a T3 a fechar a janela inteira desde a 263. Antes: 263 ~22 processos, 265 ~41, 266 ~7, 267/268 ~29, 270 travou antes da T3, 272 0x133 nosso.
- Com n=1 e a dispersão conhecida (8–600 s), isso **não prova** que a trava de sempre acabou.
- Hipótese que a 272 abriu: desde a P3 (IH armado, Corrida 232), o flip MPO3 em DIRQL tomava spinlocks com `KeAcquireSpinLock` (log e o `RingLock` da cópia do SDMA0). Com a interrupção do IH entrando na mesma CPU, ou na outra CPU que segurava o lock, a máquina fica num deadlock com a GPU ociosa, como a assinatura de sempre.
- Contra: nas corridas com KD (247–250) o Ctrl+Break não respondia. No dump da 272 o congelamento das CPUs funcionou.
- **Teste:** repetir a 273 e medir a taxa de trava com a T3. A regra de esperar 3 min antes do reset continua.
- **Mineração posterior (P0 sobre a evidência, 12/10): a hipótese não explica a trava de sempre.**
  - Contei, em 53 binds de 06/10 a 12/10, o flip MPO3 e o IH armado (`[P3_IH] ATIVO`).
  - As corridas 229–231 tiveram MPO3 (4.347 e 1.322 flips) com o **IH desligado**, e a 231 travou aos ~325 s. Antes delas, a série 199–228 também travou sem IH.
  - Sem interrupção do dispositivo não existe o autotravamento da 272. Logo, a trava de sempre existe sem ele.
  - A DIRQL.1 corrige um defeito real (a 0x133 da 272), mas **não é candidata a causa da trava dura**. Os 175 s da 273 são compatíveis com a dispersão.

**VSYNC:** no amdgpu, o caminho do OTG ao IH passa pelo roteamento de interrupções do DMU, que não está no `dcn_2_0_3_offset.h` baixado. Próximo passo offline: achar o registrador de destino e o que o GOP deixou, sem escrita nova até ter proveniência.

**AUX:**
- No amdgpu, antes de usar o AUX, `dal_ddc_set_config(..., DDC_CONFIG_TYPE_MODE_AUX)` (`hw_ddc.c`) põe o pad do DDC1 em modo AUX (`DC_GPIO_DDC1_MASK.AUX_PAD1_MODE`).
- Próximo passo offline: baixar `hw_ddc.c` e os offsets do GPIO, e propor a troca de modo só durante a leitura, com volta ao estado do GOP.

## 4. Mineração offline depois da corrida (12/10)

- **CRC (D17), P1 sobre P0:**
  - `FMT0_BIT_DEPTH_CONTROL=0x921`: truncar a 10 bpc e dithering espacial ligado para 8 bpc, sem temporal.
  - `FMT0_CONTROL=0x1300`: `SPATIAL_DITHER_FRAME_COUNTER_MAX=3`, `BIT_SWAP=1`.
  - O padrão do dither espacial gira com o contador de quadros, então o CRC muda a cada quadro mesmo com a tela parada. Isso explica a 272.
  - Saída: comparar o CRC na mesma fase do contador (a cada 4 quadros), sem mexer no FMT.
- **VSYNC (D06), roteamento:**
  - O `dcn_2_0_3_offset.h` não traz o bloco `dce_dc_dmu_ihc_dispdec`. O do DCN 2.0 traz, arquivado agora em `inc/linux_ref/dcn/dcn_2_0_0_offset.h`/`_sh_mask.h`.
  - `OTG0_INTERRUPT_DEST` (mm 0x154, BASE_IDX 2): VSTARTUP no bit 16, V_UPDATE_NO_LOCK no 19, VERTICAL_INTERRUPT0 no 9.
  - `DCHUB_INTERRUPT_DEST2` (mm 0x14F): flip do HUBP0 no bit 0.
  - Hipótese (H): os bits do OTG0 apontam para o microcontrolador do display, e o do flip para o host. Isso explicaria o flip chegar e o VSYNC não.
  - Discriminante barato: só **ler** os dois registradores na próxima corrida. Os bytes 0xD850 e 0xD83C ficam abaixo da janela atual, que começa em 0xE000, então a janela precisa crescer.
- **AUX (D12):**
  - `dce_aux.c acquire()` chama `dal_ddc_open(..., GPIO_MODE_HARDWARE, GPIO_DDC_CONFIG_TYPE_MODE_AUX)`, que faz duas coisas:
    - `hw_gpio.c`: `MASK` = 0, o pino passa ao hardware;
    - `hw_ddc.c set_config`: `AUX_PAD1_MODE` = 1.
  - Registrador: `DC_GPIO_DDC1_MASK` (mm 0x28D0, byte 0x17640, também fora da janela atual).
  - No DCN 2.x, `phy_aux_cntl` e `dc_gpio_aux_ctrl_5` são 0 (`hw_factory_dcn20.c`).
  - Arquivados em `inc/linux_ref/dcn/`: `hw_ddc.c`, `hw_gpio.c`.
- **VSYNC perdidos (D07):** 472 dos 1.122 caíram na janela do DPMS (9–21 s) e 34 no primeiro meio segundo. Fora disso ficam 616 em ~9.600 quadros (6,4%), com 1–13% por janela de 4 s.

**NÃO VALIDADO EM HARDWARE:** VSYNC por interrupção, EDID real, cursor monocromático/mascarado por hardware e o fim da trava.
