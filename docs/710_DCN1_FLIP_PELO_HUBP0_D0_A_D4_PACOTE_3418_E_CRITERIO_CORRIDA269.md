# 710 — DCN.1: flip pelo HUBP0 (D0 a D4), pacote 3.418.1.0 e critério da Corrida 269

**Data:** 12/10/2026, offline.

**Decisão do Daniel (D0):** abrir a P9 e fazer D0–D4 numa corrida só. Base: [doc 709](709_REVISAO_DOS_ACHADOS_DO_DRIVER_AMD_WINDOWS_CHECKLIST_E_A_QUESTAO_DO_DCN.md) §3–§4.

**Expectativa declarada antes:** pela evidência (L1, 251b, 263b, 268), o DCN herdado do GOP **não** é a causa da trava. Esta corrida mede duas coisas:
1. se o flip pelo HUBP0 funciona;
2. se tirar as cópias de apresentação do SDMA0 muda a trava.

---

## 1. O que entrou

### D0 — governança

A escrita em registrador de DCN fica permitida na P9 **só** nos registradores do flip do HUBP0 (`DCSURF_FLIP_CONTROL` e o endereço primário), com:
- a chave `DcnFlip` (padrão 0 no INF);
- a volta ao endereço do GOP no teardown.

Timing, clock, PHY, formato, pitch e viewport continuam intocados.

### D1 — proveniência (Lei 7, P1)

Offsets e máscaras de `dcn_2_0_1_offset.h` / `dcn_2_0_1_sh_mask.h` do Linux; o `dcn201_resource.c` do amdgpu inclui esses dois para a Cyan Skillfish. Todos com BASE_IDX 2, byte = (0x34C0 + mm) × 4.

Os offsets que a sonda DCN já usava (OTG0, HUBP0) batem com o upstream.

**Correção aos docs 707/708 da raiz** (o par de engenharia reversa na raiz do repositório, não os docs 707/708 de `docs/`):
- o bloco é o **DCN 2.0.1**, não o 2.1;
- não existe `OTG_GLOBAL_SYNC_LOCK`; a trava do DCN 2.0.1 é o `OTG_MASTER_UPDATE_LOCK`.

Fonte única: `driver/core/bc250_dcn_flip_contract.h`.

### D2 — leitura do pipeline do GOP (só leitura)

Linha `[DCN1_D2]` no StartDevice com:
- `SURFACE_CONFIG`, `VIEWPORT`, `PITCH`, endereço, `SURFACE_CONTROL`, `FLIP_CONTROL`, `EARLIEST_INUSE`;
- `OTG_CONTROL`, `MASTER_UPDATE_LOCK` e o quadro.

`Bc250DcnFlipEvaluateGop` decide se o flip é seguro. Ele recusa, cada caso por um bit:
- OTG desligado;
- viewport ou pitch diferentes do modo;
- DCC;
- lock de superfície;
- master lock;
- endereço do GOP zero ou desalinhado;
- bloco morto (0xFFFFFFFF).

**Novos na leitura** (nunca lidos antes): `SURFACE_CONTROL`, `FLIP_CONTROL`, `EARLIEST_INUSE` e `MASTER_UPDATE_LOCK`. O risco é baixo (bloco DCN, não clock gating), mas está declarado.

### D3 — flip

`Bc250DisplaySwitchPrimary` é agora a rota única de `SetVidPnSourceAddress`, MPO1 e MPO3. Com o flip armado, a sequência é a do `hubp2_program_surface_flip_and_addr`:
1. `FLIP_CONTROL` (só `SURFACE_FLIP_TYPE` muda; Lei 6);
2. endereço **alto**;
3. endereço **baixo**, cuja escrita dispara o latch;
4. readback dos dois.

Recusado ou com readback divergente, cai na cópia do SDMA0 de antes. No readback divergente o HUBP0 volta ao GOP e o flip é desarmado (falha fechada).

**Endereço:** endereço do GOP − offset do scanout + offset de VRAM da primária. P0: o endereço que chega ao MPO3 é o mesmo `VramOff` do Present (268: `0xABC000`, `0x21DC000`, `0x3B0C000`, a cadeia de três buffers do DWM). A falha é fechada:
- fora da VRAM mapeada;
- sem alinhamento de 256 bytes;
- acima de 48 bits.

**Present:** o Present de flip em tela cheia com o flip armado deixa de copiar para o scanout (`DCN1_SEM_COPIA`).

### D4 — VSYNC depois do latch

O DPC de VSYNC lê `FLIP_CONTROL` e `EARLIEST_INUSE`. O flip só conta como exibido quando `SURFACE_FLIP_PENDING=0` e `EARLIEST_INUSE` é igual ao pedido (`hubp2_is_flip_pending`). A partir daí o `CRTC_VSYNC` reporta a primária nova (`[DCN1_LATCH]`, com a latência).

**Limite declarado:** o VSYNC continua vindo do contador de quadros do OTG0 por poll de 4 ms. **A interrupção do DCN pelo IH não está nesta etapa.**

### Lei 5 — teardown

O `HUBP0` volta ao endereço e ao `FLIP_CONTROL` do GOP, com readback e um resumo (`[DCN1_TEARDOWN]`), em dois pontos:
- `Bc250DisplayTeardown`;
- `StopDeviceAndReleasePostDisplayOwnership`.

## 2. Verificações

- `bc250_dcn_flip_contract.c` e o teste `dcn_flip_contract_test`, com os valores medidos na placa:
  - o GOP medido é elegível;
  - 8 recusas isoladas;
  - o endereço `0xABC000` vira `0x460ABC000`;
  - falha fechada fora da VRAM, sem alinhamento ou com GOP zero;
  - Lei 6 no `FLIP_CONTROL`;
  - pendência como o `hubp2`.
- `build-contract.bat`: EXIT 0. Build de laboratório (`-Execute`): EXIT 0. As marcas `DCN1_FLIP`, `DCN1_LATCH`, `DCN1_TEARDOWN`, `DCN1_D2` e `DCN1_SEM_COPIA` estão no SYS.
- `audit-bc250mcdm-safety.py`: PASS.
- **Portão novo `audit_dcn1_flip_pelo_hubp`**, provado por 6 mutações:
  1. arma sem checar o GOP;
  2. escreve o baixo antes do alto;
  3. teardown sem voltar ao GOP;
  4. offset errado;
  5. INF ligado por padrão;
  6. VSYNC sem o latch.

  A mutação 6 passou na 1ª versão do portão e ele foi endurecido.
- **Portões DX.31/33 atualizados:** a rota passou a ser `Bc250DisplaySwitchPrimary`, que tem de cair na cópia rastreável. A exigência de `ModeChangeOnly=FALSE` no MPO continua.
- Empacotador: EXIT 0. `audit-bc250mcdm-lab-package.ps1`: PASS. O preflight da 269 confere hashes e manifesto (testado até a CMOS).

### Pacote 3.418.1.0

| Item | Valor |
|---|---|
| DriverVer | `10/12/2026,3.418.1.0` (fonte 3.418.0.0) |
| SYS assinado | `B624B7ED4827098FC83C3B3A9F18B1D7EF2B285BDBC0E622510A93B7694C081A` |
| SYS fonte (lab) | `B4F32193E9A2DE97ED5AEDA1FEE3AEEA5D68207BBBA448C912C6FADE9D7B3E7B` |
| INF | `B3F68264AA787813D19B61FF7F94CC5A72CBC5FED497250DD33023A819D90B56` (`DcnFlip=1`, `DebugTrace=1`, `GfxWaveProbe=0`) |
| CAT | `850AEF6D03F61D6D1BE82290E82AFA17950C16D29AEC832843FACD2251CF96F4` |
| UMD, RADV, T3 | iguais à 268 |
| Cópia | `driver/wdk/package/output_lab_3.418.1.0_269` |

**Nota:** o 3.417.1.0 foi empacotado antes do caso de borda abaixo, nunca instalado e substituído. **Caso de borda corrigido no 3.418:** se um flip for recusado com o HUBP0 já numa primária do DWM, o HUBP0 volta ao GOP e o flip é desarmado antes da cópia de fallback (`FLIP_RECUSADO_VOLTA_A_COPIA`). Sem isso a cópia iria para um scanout que ninguém exibe.

## 3. Critério da Corrida 269 (escrito antes)

**Lançador:** `tools/EXECUTAR_CORRIDA269_DCN1_FLIP_PELO_HUBP_60S.cmd`.

**Teste só do DCN (pedido do Daniel):** watchdog de **60 s** e **sem a carga T3**. O roteiro é: menu duas ou três vezes e uma janela movida pela tela.

O piso de 120 s do binder e do guardião só cai para 60 s com `BC250_ALLOW_SHORT_WATCHDOG=1`, que o lançador liga. O binder grava `short_watchdog=1` no contrato do guardião, e o teste offline do safe-bind passou. Com 60 s a corrida mede o flip, não a trava: a mediana das travas fica em ~100–200 s.

**Antes:** `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset` e **desligar**.

**O operador anota o que vê na tela:**
- desktop normal e atualizando (relógio, menu, janela da T3);
- congelado;
- preto;
- cores ou listras erradas;
- tremendo.

**Pré-condição** (sem ela não há A, B nem C):
- `[DCN1_FLIP] chave DcnFlip=1 (present=1)`;
- a linha `[DCN1_D2]`;
- `[DCN1_FLIP] ARMADO`.

### Desfechos

- **A — flip funciona e os 60 s passam sem trava, com rollback limpo.**
  - *Assinatura:*
    - `[DCN1_FLIP] #n ... readback OK` crescendo;
    - `[DCN1_LATCH]` com latchados ≈ flips;
    - `DCN1_SEM_COPIA`;
    - nenhuma `PRIMARY_COPY ... BEGIN` depois de armar, salvo recusas logadas;
    - operador vendo o desktop atualizar;
    - `[DCN1_TEARDOWN] ... readback OK` no rollback.
  - *Implicação:* o flip pelo HUBP fica validado (P0 de registrador e de latch; a imagem é observação do operador). Sobre a trava, 60 s não dizem nada. Próximo: corrida longa com a DCN.1 e a T3.
- **B — flip funciona e a trava vem antes dos 60 s.**
  - *Assinatura:* a de A até a trava, com a GPU ociosa.
  - *Implicação:* as cópias de apresentação no SDMA0 saem das causas suficientes. A DCN.1 fica (produto, P9). A investigação volta ao C1 do doc 709 (DBG.3).
- **C — flip programado sem latch, ou tela errada.**
  - *Assinatura:* readback OK, mas `[DCN1_LATCH]` zero ou bem abaixo dos flips. Ou o operador vê tela congelada, preta ou com lixo enquanto os flips sobem.
  - *Implicação:* domínio de endereço, tipo de flip ou alguma condição do HUBP errados (H). Próximo: ler os `EARLIEST_INUSE` logados e desligar a chave.
- **D — não armou.**
  - *Assinatura:* `[DCN1_FLIP] RECUSADO ... recusas=0x....`
  - *Implicação:* a corrida não testa o DCN e vira repetição da 268. A máscara diz qual campo do GOP diverge do modo.
- **E — fora do mapa.** Por exemplo:
  - trava logo no primeiro flip;
  - tela azul;
  - TDR;
  - imagem que só aparece depois do rollback;
  - readback divergente.

  *Implicação:* descrever antes de julgar.

**Regra (Lei 10):** desfecho parcial não é desfecho. A e B exigem flips **e** latches; flips sem latch é C.

**Riscos declarados:**
- **Tela errada (preta, lixo, congelada) durante a corrida:** o rollback do watchdog devolve o HUBP0 ao GOP e o BasicDisplay assume.
- **Endereço:** o DCN passa a ler da VRAM do DWM. O endereço fica preso à VRAM mapeada (256 MB), então não pode sair do carve-out.

**NÃO VALIDADO EM HARDWARE:** a DCN.1 inteira.
