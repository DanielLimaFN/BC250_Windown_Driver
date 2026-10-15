# 718 — D06 (leitura do roteamento de IRQ) e D12 (pad AUX em modo hardware), preparados offline para a Corrida 274

**Data:** 12/10/2026, offline (sem corrida). Continuação do [doc 716](716_PASSAGEM_DE_BASTAO_2026-10-12_DCN1_A_DCN3_DIRQL1_E_O_QUE_FAZER_A_SEGUIR.md) §5.1/§5.2/§5.3 e do [doc 717](717_FLIP_COMPLETO_PELA_INTERRUPCAO_D05_OFFLINE_SEM_PACOTE_NOVO.md) (D05, feito antes nesta mesma sessão).

**Pedido do Daniel:** continuar empilhando mudança até a Corrida 274 estar fechada. Nada aqui foi empacotado nem rodado na placa.

---

## 1. D06 — leitura do roteamento das interrupções do OTG (SÓ LEITURA)

**Problema:** na 273, VSTARTUP/VUPDATE/VLINE0 nunca chegaram ao IH (0/0/0 entregas), enquanto o flip do HUBP0 chega 1:1. O doc 716 propôs ler `OTG0_INTERRUPT_DEST` e `DCHUB_INTERRUPT_DEST2` para comparar o bit de destino do flip (que sabemos chegar) contra os das três fontes do OTG (que não chegam), como indício do roteamento — sem decidir a semântica do bit por conta própria (ela é **H**, não documentada no header).

**Proveniência dos registradores e campos** (Lei 7): `dcn_2_0_0_offset.h` e `dcn_2_0_0_sh_mask.h` (o `dcn_2_0_3_offset.h` não tem o bloco `dce_dc_dmu_ihc_dispdec`, erro que o doc 710 já tinha cometido uma vez com outro arquivo e o doc 716 §4.4 avisou para não repetir):
- `OTG0_INTERRUPT_DEST` (mm 0x154): `OTG0_IHC_OTG_VERTICAL_INTERRUPT0_DEST` bit 9, `OTG0_IHC_VSTARTUP_INTERRUPT_DEST` bit 16, `OTG0_IHC_V_UPDATE_NO_LOCK_INTERRUPT_DEST` bit 19.
- `DCHUB_INTERRUPT_DEST2` (mm 0x14F): `DCHUB_INTERRUPT_DEST2__HUBP0_IHC_FLIP_INTERRUPT_DEST` bit 0.

**Implementação:**
- Função pura testável, `Bc250DcnIrqDestDecode` (`driver/core/bc250_dcn_ext_contract.h/.c`): decodifica os quatro bits dos dois valores lidos. Não acessa registrador.
- Runtime `Bc250Dcn2ReadIrqDest()` (`driver/wdk/bc250_display_ddi.c`): lê os dois registradores, decodifica, publica `[DCN2_IRQDEST]` com os valores crus e decodificados, e o aviso explícito de que a semântica do bit continua H. **Nenhuma escrita.**
- Chamada só com a chave nova `BC250_DCN2_KEY_IRQ_DEST` (bit `0x40` de `DcnExt`) ligada, no mesmo bloco de arme das outras entregas da DCN.2/3.

## 2. D12 — pad DDC1/AUX0 em modo hardware/AUX (ESCREVE)

**Problema:** na 273, o AUX0 respondia `AUX_SW_NON_AUX_MODE` (0x801) — o pad físico está em modo DDC/I2C, não AUX. O doc 716 propôs programar o pad para modo AUX, como `dal_ddc_open(GPIO_MODE_HARDWARE, MODE_AUX)`.

**Proveniência** (Lei 7), lida linha a linha em `inc/linux_ref/dcn/hw_gpio.c` e `hw_ddc.c` (já arquivados no repo):
- `dal_hw_gpio_config_mode` caso `GPIO_MODE_HARDWARE`: `REG_UPDATE(MASK_reg, MASK, 0)` — chamado uma vez para o pino CLK e uma vez para o pino DATA, cada um mapeado para o seu próprio bit do **mesmo** registrador `DC_GPIO_DDC1_MASK` (`DC_GPIO_DDC1CLK_MASK` bit 0, `DC_GPIO_DDC1DATA_MASK` bit 8).
- `hw_ddc.c` `set_config` caso `GPIO_DDC_CONFIG_TYPE_MODE_AUX`: `REG_SET(gpio.MASK_reg, regval, AUX_PAD1_MODE, 1)` — bit 16, só se ainda não estiver ligado.
- Os blocos `dc_gpio_aux_ctrl_5`/`phy_aux_cntl` que a mesma função toca no caminho I2C **não existem no DCN 2.x** (guardados por ponteiro nulo no upstream) e não têm equivalente catalogado aqui — **Q, não inventado**.

Como as três escritas caem no mesmo registrador de 32 bits, a implementação as combina numa única leitura-modificação-escrita (Lei 6), em vez de replicar as três chamadas separadas do upstream.

**Implementação:**
- Função pura `Bc250DcnAuxPadHardwareModeValue(Read)` (`driver/core/bc250_dcn_ext_contract.h/.c`): `(Read & ~mask) | (valor & mask)` com `mask` = CLK_MASK|DATA_MASK|AUX_PAD1_MODE e `valor` = só AUX_PAD1_MODE. Testada com registrador zerado, todo-1, e dois casos medidos de combinação de bits vizinhos (`AUX1_POL`).
- `Bc250Dcn2AuxPadAcquire`/`Bc250Dcn2AuxPadRelease` (`driver/wdk/bc250_display_ddi.c`): a adoção lê, escreve e confere o readback; se não confirmar, **devolve o pad ao valor salvo por conta própria** (falha fechada) e conta em `AuxPadReadbackFail`. A liberação sempre escreve o valor salvo (Lei 5), chamada nos dois pontos de saída de `Bc250Dcn2ReadRealEdid` (a recusa por DPCD sem resposta e o fim normal da função) — **não** há caminho que adote o pad e não o devolva.
- A adoção acontece **antes** da primeira transação AUX (antes até da leitura do HPD), e só dura até o fim da leitura de EDID — nunca atravessa para fora dessa função.
- Chave nova `BC250_DCN2_KEY_AUX_HW_MODE` (bit `0x80` de `DcnExt`). **Esta é a parte que escreve registrador e precisa do OK do Daniel para ser armada** no pacote da 274 — o código já está pronto e testado, mas a decisão de ligar o bit continua do dono do projeto.
- `[DCN2_AUXPAD]` loga a adoção (valores antes/depois) e a falha de readback, quando houver.

## 3. Janela de MMIO ampliada

D06 precisa de bytes abaixo da janela antiga (`0xD83C`/`0xD850` < `0xE000`) e D12 de bytes acima (`0x17640` > `0x15400`). A janela mapeada por `MmMapIoSpace` (`BC250_DCN_FLIP_WINDOW_BASE`/`BYTES`, `driver/core/bc250_dcn_flip_contract.h`) passou de `[0xE000, 0x15400)` para `[0xD800, 0x17800)` — um único mapeamento maior (40.960 bytes) em vez de duas janelas pequenas, mais simples de manter e ainda bem menor que a página inteira de registradores. Todos os offsets absolutos usados (DCN.1, DCN.2, DCN.3 e agora D06/D12) continuam sendo constantes; só a base do mapeamento mudou, e a aritmética de `Bc250DcnRead`/`Write` (`Window + (AbsOffset - WINDOW_BASE)`) se ajusta sozinha.

## 4. Testes, portões e mutação

- **Testes de contrato** (`driver/tests/dcn_ext_contract_test.c`):
  - D06: decodificação com registrador zerado, com os quatro bits isolados, e o caso que importa — flip em 0 com os outros três em 1 (e vice-versa) — mais `Out == NULL` sem crash.
  - D12: registrador zerado, todo-1 (prova que o resto é preservado), e dois casos com bits vizinhos ligados (idempotência e o bit `AUX1_POL`).
- **Portões novos** (`tools/audit-bc250mcdm-safety.py`):
  - `audit_dcn06_irq_dest_readonly`: confere os mm/máscaras contra `dcn_2_0_0_offset.h`/`sh_mask.h`, que a decodificação é pura, que a leitura **não escreve** registrador, que só roda com a chave ligada, e que a janela cobre os dois offsets.
  - `audit_dcn12_aux_pad_hw_mode`: confere o mm/máscaras contra `dcn_2_0_3_offset.h`/`sh_mask.h`, que o valor é puro (Lei 6), que a adoção usa a função pura e reverte por conta própria no readback falho, que a liberação escreve exatamente o valor salvo, que a adoção vem antes de qualquer transação AUX, que **todo** retorno da função devolve o pad, que só roda com a chave ligada, e que a janela cobre o offset.
  - As duas novas entradas (`Bc250Dcn2AuxPadAcquire`/`Release`) foram somadas à lista de escritores autorizados de DCN que `audit_dcn1_flip_pelo_hubp` já mantinha.
- **Mutação provada (Lei 10):** duas, uma por portão novo.
  - Removida a guarda da chave em `Bc250Dcn2ReadIrqDest`: a auditoria **reprovou** com "DCN.06: a leitura só roda com a chave BC250_DCN2_KEY_IRQ_DEST ligada".
  - Removida a liberação do pad no retorno antecipado de `Bc250Dcn2ReadRealEdid`: a auditoria **reprovou** com "DCN.12: toda saída de Bc250Dcn2ReadRealEdid tem de devolver o pad ao GOP".
  - As duas mutações foram revertidas e a auditoria voltou a `PASS` nas duas vezes.
- **Compilação:** `dcn_ext_contract_test.exe` compilado e executado offline (mesmo `CFLAGS` do `build-contract.bat`): **OK**. `bc250_display_ddi.c` compilado isoladamente com `/W4 /WX`, toolchain VS2026 + WDK 10.0.28000.0, `BC250_ENABLE_DXGK_REGISTRATION` definido: **zero warnings**, antes e depois de cada mutação/reversão. `python tools/audit-bc250mcdm-safety.py`: **PASS**.
- **Não executado:** `build-contract.bat` completo (as ~70 suites + o `.sys`), o empacotador de laboratório, qualquer corrida física.

## 5. Estado e o que falta para a 274

- **NÃO VALIDADO EM HARDWARE.**
- **D05** (doc 717) e **D06** (este doc) não precisam de autorização adicional — são leitura ou reaproveitam infraestrutura de escrita já aprovada (o flip do HUBP0).
- **D12 escreve um registrador novo e precisa do OK explícito do Daniel** antes de ser armado no pacote — o código está pronto, mas a decisão de ligar o bit `0x80` continua do dono do projeto, como o doc 716 já havia marcado.
- Valor de `DcnExt` proposto para a 274, se os três entrarem juntos: `0xFD` = `0x3D` (da 273: IRQ+CURSOR+EDID+DPMS+TIMING, CRC fora) `| 0x40` (D06) `| 0x80` (D12). D17 (CRC por fase do dither) fica fora dessa conta — é opcional e não bloqueia a 274.
- **Falta, nesta ordem, antes do bind:** (1) o Daniel decidir o valor final de `DcnExt` e autorizar D12; (2) `build-contract.bat` completo; (3) empacotador de laboratório; (4) auditoria do pacote; (5) lançador CRLF e preflight; (6) doc com o critério da corrida (Lei 10, os quatro desfechos incluindo "E — fora do mapa").
- Critério já esboçado para quando a 274 rodar: `[DCN2_IRQDEST]` aparece com os quatro bits decodificados (D06 fecha por medição, não por hipótese, se o bit do flip divergir dos outros três); `[DCN2_AUXPAD]` mostra a adoção OK e o `[DCN2_EDID]` deixa de mostrar `AUX_SW_NON_AUX_MODE` (D12 confirmado se o DPCD/EDID responderem pela primeira vez).
