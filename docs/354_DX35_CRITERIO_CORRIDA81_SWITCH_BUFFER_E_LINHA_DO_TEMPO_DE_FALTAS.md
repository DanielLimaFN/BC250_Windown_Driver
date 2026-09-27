# DX.35 — par de SWITCH_BUFFER na troca de VMID e linha do tempo de faltas. Critério da Corrida 81

> **23/09/2026. OFFLINE; zero bind.** Parte do resultado da Corrida 80 ([docs/353](353_CORRIDA80_DX34_RESULTADO.md),
> com a correção de revisão no topo). Critério escrito **antes** do ensaio (Leis 9 e 10). **NÃO VALIDADO EM HARDWARE.**

## 1. O que motiva

**Corrida 80, P0:**
- No TDR 1, as 4 ondas presas tinham `VM_ID=1`. O IB travado era do VMID 3 e o anterior do VMID 1.
- O fault latchado: VMID 1, busca de instrução (`Cid=9`), sem permissão de execução, na página `0x200040`. Essa página
  é uma alocação `STAGING` `NoExecute=1` no espaço do VMID 1.
- No TDR 2, o fault das ondas não foi capturado. O latch guarda só o **primeiro** fault desde o último CLEAR, e o único
  CLEAR era o dreno do TDR anterior. Um fault de escrita do VMID 2 chegou antes.

**Leitura do upstream (P1).** Fontes baixadas de `torvalds/linux` v6.6 para o scratchpad da sessão: `amdgpu_ib.c`,
`amdgpu_vm.c` e `gfx_v10_0.c`.
- `amdgpu_vm_flush` emite, em ordem:
  1. `emit_pipeline_sync` (`WAIT_REG_MEM` da fence no PFP);
  2. `emit_vm_flush` (PT_BASE, invalidate/ACK e `PFP_SYNC_ME` no gfx);
  3. `amdgpu_fence_emit`;
  4. **dois `emit_switch_buffer`**, com o comentário "the double SWITCH_BUFFER here *cannot* be skipped".
- No gfx10, `emit_switch_buffer = gfx_v10_0_ring_emit_sb` = `PACKET3(PACKET3_SWITCH_BUFFER, 0), 0`. O opcode
  `0x8B` está em `nvd.h:550`.
- A nossa troca (DX.30) já emitia os itens 1 e 2 e usava EOP compatível com `gfx_v10_0_ring_emit_fence`
  (`CACHE_FLUSH_AND_INV_TS_EVENT`, índice 5). **Faltava só o par de `SWITCH_BUFFER`.**
- Fora da troca, o upstream também emite `CONTEXT_CONTROL`, `FRAME_CONTROL` e um `SWITCH_BUFFER` no fim de cada job.
  Esses ficam **fora** desta faixa, para manter uma única variável.

## 2. O que mudou

| # | Mudança | Tipo |
|---|---|---|
| V1 | `Bc250Pm4EmitGfxVmSwitch` emite `SWITCH_BUFFER` duas vezes depois do `PFP_SYNC_ME` (`0xC0008B00, 0` ×2). A troca passa de 31 para 35 DW, e a reserva do anel deriva de `BC250_PM4_GFX_VM_SWITCH_DWORDS + 15`. | **Comportamental** (uma variável) |
| M1 | `[GCVM_FAULT_TIMELINE]`: a cada troca de VMID, fora do spinlock, lê `GCVM_L2_PROTECTION_FAULT_STATUS`. Se não-zero, drena pelo contrato do TDR (`Bc250FaultLatchDrain`, CNTL restaurado com readback, teto 8) e imprime cada entrada decodificada com os mesmos shifts de `[TDR_GCVM_FAULT]`. | Medição (escreve o CLEAR do latch quando há fault) |

A sonda de ondas continua **armada** (`GfxWaveProbe=1`), como na 80.

**Classificação:**
- a sequência upstream é **P1**;
- que o par de `SWITCH_BUFFER` faça o estado de dispatch adotar o VMID novo é **H desta faixa**; nenhuma fonte local
  diz isso.

**Riscos declarados:**
- `SWITCH_BUFFER` num anel sem CE ativo pode ser inócuo (desfecho B) ou travar o PFP (desfecho D);
- o dreno em cada troca soma algumas leituras MMIO por troca (~60–90 trocas por corrida).

**Verificação offline:**
- `build-contract` EXIT=0, com `pm4_packet_builder_test` travando os 4 dwords novos e o tamanho 35;
- mutação de teste: trocar o 2º `SWITCH_BUFFER` por NOP derruba a suíte;
- portão novo `audit_dx35_switch_buffer_and_fault_timeline`: **8 de 8 mutações reprovam**;
- o portão DX.30 foi endurecido: exige a reserva derivada, não o literal 46;
- auditorias de segurança, matriz de DDI, callbacks no-op e RADV: PASS;
- ferramentas de mutação DX.31, DX.33 e DX.34: 0 falhas;
- safebind offline: PASS.

**Pacote lab 3.242.1.0**, assinado, `Lab package audit: PASS`, `GfxWaveProbe=1`, NÃO INSTALADO:

| Artefato | SHA-256 |
|---|---|
| SYS assinado | `91767BD0AC1B8E15275A07381E4521C1F6E1E27286BE5B5C453AD03ABF37131D` (fonte `CA9E9018...`) |
| UMD | `239B046C...` (inalterada) |
| INF / CAT | `C7E5A282...` / `20BFFE2C...` |
| ICD | `5988B38E...` (inalterado) |

As strings `GCVM_FAULT_TIMELINE`, `TDR_GFX_WAVE_VMID` e `DIRTY_RECT_PROOF` estão no SYS empacotado.

## 3. Critério da Corrida 81

### 3.1 Sinais
- **SB-ATIVO:** no dump da primeira troca (`VMID_SWITCH_VM_FLUSH_PM4`), `C0004200 00000000` seguido de
  `C0008B00 00000000 C0008B00 00000000` e então `C0023F00` (IB). `PM4DW=35`.
- **ANEL-VIVO:** `SUBMIT SUCESSO` e fences aposentadas **depois** das trocas; nenhum TDR com o ME/PFP parado num
  `SWITCH_BUFFER`. `ME_HEADER_DUMP`/`PFP_HEADER_DUMP` ≠ `0xC0008B00`.
- **ONDA-VMID:** em cada TDR, `WaveVmid` × `CARIMBO -0`/`-1`.
- **TIMELINE:** linhas `[GCVM_FAULT_TIMELINE]` com Vmid/Cid/Permission/GpuVa, **e** o fault latchado no TDR comparado
  com a última amostra.
- **Controles:**
  - autoteste 5/5 nas três variantes, com NÃO MEDIDO por device perdido não contando como FAIL;
  - zero BugCheck;
  - zero `PREAMBLE_CNTL`;
  - `Eng=0`;
  - boot novo com marcador gravado pelo watchdog.
- **Visual (Daniel):** se a imagem fica, e se o cursor e o desktop respondem.

### 3.2 Desfechos (assinatura e implicação separadas)

**A — A classe de TDR some**
- Assinatura: SB-ATIVO, ANEL-VIVO e **zero TDR** na corrida inteira.
- Implicação: o par de `SWITCH_BUFFER` era a peça que faltava na troca. Próximo: desktop interativo e 60 quadros.

**B — O VMID errado some, mas sobra TDR**
- Assinatura: SB-ATIVO, com TDRs em que **todo** `WaveVmid` coerente é igual ao do `CARIMBO -0` (nenhum igual ao `-1`).
- Implicação: a mistura de VMID acabou, e o que resta é outra falta, com a própria assinatura na TIMELINE (Cid,
  permissão, endereço). Descrever antes de propor.

**C — O VMID errado persiste**
- Assinatura: SB-ATIVO e algum TDR com `WaveVmid` = VMID do `CARIMBO -1` ≠ `-0`.
- Implicação: o par de `SWITCH_BUFFER` não basta. Próximo candidato pela mesma leitura upstream: `CONTEXT_CONTROL` com
  `load_cs_sh_regs` na troca de contexto (`gfx_v10_0_ring_emit_cntxcntl`, dw2 `0x01000000`).

**D — O `SWITCH_BUFFER` trava o anel**
- Assinatura: TDR com `ME_HEADER_DUMP` ou `PFP_HEADER_DUMP` = `0xC0008B00`, ou o anel parado com RPTR dentro dos 4 DW
  novos, logo depois de uma `VMID_SWITCH_VM_FLUSH`.
- Implicação: reverter V1. Nada sobre o VMID é concluível.

**E — Onda sem leitura**
- Assinatura: TDR sem `[TDR_GFX_WAVE_VMID]` coerente (`INDETERMINADO`/`selector_stuck` sem onda válida).
- Implicação: a classificação B/C fica sem resposta nesse TDR; usar só a TIMELINE.

**F — Regressão de controle**
- Assinatura: autoteste FAIL (não NÃO MEDIDO), BugCheck, `PREAMBLE_CNTL` ou perda de `Eng=0`.
- Implicação: atribuir primeiro.

**H — Host**
- Assinatura: o bind sai no mesmo boot de um rollback, ou o watchdog termina sem marcador.
- Implicação: corrigir o host.

**G — Fora do mapa:** descrever antes de julgar.

Desfecho parcial não é desfecho (Lei 10.3). Com dois ou mais TDRs, cada um é classificado e a corrida reporta todos.

### 3.3 Execução
- `C:\BC250_REQUIRE_CLEAN_BOOT.json` **está armado** com o boot atual (09:17:17).
- **Desligamento completo**: `shutdown /s /t 0`, esperar apagar e ligar pelo botão.
- Preflight PASS, depois `tools\EXECUTAR_D3D11_NATIVO.cmd` elevado, **sem `-Force`**.
- Anotar o que aparece, se fica, e se o cursor e o desktop respondem.
- Teto: uma corrida.
