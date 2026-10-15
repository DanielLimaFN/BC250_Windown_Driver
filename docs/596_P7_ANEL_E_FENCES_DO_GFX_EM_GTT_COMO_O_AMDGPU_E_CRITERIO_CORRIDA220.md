# 596 — P7: anel e fences do GFX em GTT, como o amdgpu; critério da Corrida 220

**Data:** 05/10/2026 · **Candidato:** lab **3.368.1.0** · **Base:** [doc 595](595_CORRIDA219_P1_VALIDA_TRAVA_IGUAL_COLETA_AUTOMATICA_FUNCIONOU_E_TESTE_DE_RAM.md) (P1 válida, trava igual na composição GFX)
Trabalho offline. **NÃO VALIDADO EM HARDWARE.**

## 1. Por que a P7

Na 219 a trava veio **durante composição gráfica**: os 12 últimos eventos são `SUBMIT_GFX`/`RETIRE_GFX`, sem paginação.
- **O nosso GFX hoje:** anel, writeback do RPTR, shadow do WPTR e fence ficam num bloco de VRAM, escrito e lido pela CPU pela BAR.
- **O amdgpu:** o anel vai para GTT (`amdgpu_ring_init`, `AMDGPU_GEM_DOMAIN_GTT`), e o writeback e as fences também (`amdgpu_wb_init`).

Já fizemos o mesmo com o SDMA0 (D2 + UC.1), e funcionou. Falta o GFX.

## 2. O que muda

Chave `BC250_P7_GFX_RING_IN_SYSTEM_MEMORY 1` em `bc250_active_miniport.c`.

- **No `Bc250ActiveGfxEngineOnline`,** se a GART da D2 estiver ligada:
  1. aloca 68 KB de RAM contígua não cacheada (64 KB de anel e uma página auxiliar com RPTR wb em +0, shadow do WPTR em +0x10 e fence em +0x20);
  2. mapeia nas entradas **64..80** da tabela da GART da VMID 0, VA `0x40000`, com a PTE de sistema `MTYPE_UC|0x77`. Fica fora das entradas 0–31 do MEC e 32/33 da D2;
  3. confere por readback;
  4. faz HDP (a tabela está na VRAM) e invalida a VMID 0;
  5. aponta `RingGpuVa`, `RptrWbGpuVa`, `WptrPollGpuVa` e `FenceGpuVa` para a GART, e os ponteiros da CPU para a RAM.

  O plano do anel passa a validar contra a janela da GART (base 0, 2 MB). Os registradores programados não mudam: só os endereços.
- **O CSB fica na VRAM,** como o `clear_state_obj` do upstream (domínio VRAM). Só ele continua sendo lido do bloco de VRAM.
- **Sem GART** (D2 desligada) **ou se a alocação falhar:** caminho antigo, na VRAM, com log `[P7_GFX_GTT] NAO TENTADO ...`.
- **`Bc250ActiveGfxEngineOffline`:** depois do rollback e do reset do CP, devolve as PTEs para a dummy, faz HDP, invalida e libera a RAM. O mapeamento da VRAM tem campo próprio (`VramCpuMapped`).
- **Falha depois do apply:** a RAM fica **retida**, porque o CP pode ter lido dela. Falha antes, no mapa do transporte, devolve a RAM.
- **Rollback do TDR do SDMA (gancho da D2):** a GART fica **ligada** também quando o anel do GFX está nela, não só quando o MEC está.

**Logs novos:** `[P7_GFX_GTT] ATIVO: ... GART VA=0x40000 (PTE[64]=0x3..., MTYPE_UC) ...`; e na linha `[GfxEngine] ONLINE SUCESSO` aparecem `Anel=RAM_GART(P7)` e `CsbVram=...`.

## 3. Provas offline

- `build-contract` EXIT=0;
- build de laboratório "TODOS OS TESTES APROVADOS (0 falhas)";
- auditorias de segurança e do pacote: PASS;
- as strings novas estão no SYS assinado.
- **Portão novo `audit_p7_gfx_ring_in_gtt`, com 10 mutações reprovadas:**
  - chave em 0;
  - entrada 32, que colide com a D2;
  - plano na janela da VRAM;
  - CSB na RAM;
  - sem exigir a GART;
  - sem HDP;
  - sem invalidação da VMID 0;
  - PTE sem MTYPE_UC;
  - Offline devolvendo a RAM antes de parar o CP;
  - rollback do SDMA desligando a GART com o GFX nela.

| Arquivo | SHA-256 |
|---|---|
| SYS | `2B75E7D56E2AA92334F890DFB4170DF89BBC5A402B987C71B44825594951D4B3` |
| INF | `8AC32B812E757EC0CC1FB38C90302DC8AE56EFF818D951EAE4C20F842ED9BC25` |
| CAT | `036C4F4E8687FA7EEA4664ED4BF0D8EAB0B9E7C464D2AD31AB8EDB7493F036D9` |
| UMD / RADV | inalteradas |

**Scripts:** `tools/prepare-corrida220-p7-gfx-em-gtt.ps1` e `tools/EXECUTAR_CORRIDA220_P7_GFX_EM_GTT_600S.cmd`.

## 4. Critério da Corrida 220 (Lei 10)

**Pré-condição de validade:**
- `[P7_GFX_GTT] ATIVO ... divergentes=0, flush VMID0 ok`;
- `[GfxEngine] ONLINE SUCESSO ... Anel=RAM_GART(P7)`;
- `BOOT_PREAMBLE ... DrenoOk=1`, ou seja, o CP consumiu o CSB e a fence de boot pelo anel na RAM;
- a base da 219 se manteve (D2 ATIVO, MEC na GART, P1).

Desfechos:

- **A — 600 s sem trava dura.**
  - *Implicação:* anel e fences do GFX na VRAM pela BAR eram a causa, ou parte dela. Próximo: repetir para confirmar, e só então seguir no inventário.
- **B — trava dura com a mesma assinatura.**
  - *Implicação:* a P7 sai das causas suficientes. Próximo: P1b (HDP por MMIO nos laços de espera), P2 (doorbell do SDMA0) e P3 (interrupção).
- **C — o GFX não sobe pela GTT** (`ONLINE FALHOU`, `DrenoOk=0`, TDR gráfico no começo, fault com Cid do CP).
  - *Implicação:* falta um detalhe do upstream para o CP ler o anel pela GART (por exemplo, o MTYPE/política do `CP_RB0_CNTL` ou o snoop do writeback). O log diz qual pacote não retirou.
- **D — o GFX sobe, mas o desktop não aparece** ou o DWM entra em laço.
  - *Implicação:* defeito de montagem desta faixa (algum caminho ainda usa o endereço antigo).
- **E — fora do mapa:** descrever antes de julgar.

**Condição do rollback:** se a janela chegar ao fim, `RECOVERY_OK_ONLINE` no mesmo boot, sem laço, e a linha `[P7_GFX_GTT] DESLIGADO` no Offline. É a primeira prova do teardown da P7 e da LC.1.

## 5. Pendências

- **Daniel:** recuperação, reset do fusível, boot frio e a corrida. A coleta e o teste de RAM rodam sozinhos no boot depois de uma trava.
- **NÃO VALIDADO EM HARDWARE:**
  - a P7;
  - a LC.1 no teardown;
  - 600 s sem trava com 256 MB.
