# Corrida 80 — DX.34: resultado

> **23/09/2026, 09:14:40–09:18:00.** Candidato lab **3.241.1.0**, SYS `0D681AC8...` (conferido em
> `binding-provenance.json`), `GfxWaveProbe=1`, ICD `5988B38E...`.
> Pasta: `docs/evidence/2026-09-23_09-14-40-DESKTOP-HQAG6T4-lab-binding/`; log `FINAL` completo (26.943 linhas).
> Critério prévio: [docs/352](352_DX34_CRITERIO_CORRIDA80_RETANGULOS_SUJOS_E_VMID_DA_ONDA.md).
> **Veredito:**
> - TDR 1: **T1**; TDR 2: **T2**;
> - display: **G** (imagem persistente com entrada congelada; A não fecha);
> - H1 (host): funcionou;
> - sem F.

> **Correção de revisão (23/09, preparação da DX.35).** Eu escrevi que a 80 revelou "pela primeira vez" o estado
> das ondas presas. É **falso**: `FATAL_HALT=1` e `XNACK_ERROR=1` já tinham sido lidos pela sonda na **Corrida 45**
> (20/09), junto com o fault `0x0070153C` decodificado (comentário `FAULT DE TRADUCAO DECODIFICADO` em
> `bc250_active_miniport.c`). O que a 80 trouxe de novo é o **VMID da onda** (`SQ_WAVE_HW_ID2`): igual ao VMID do IB
> anterior no TDR 1. O texto abaixo fica como registro.

## 1. Houve avanço?

**Sim, de diagnóstico, e o maior da série de TDRs.** Pela primeira vez sabemos o estado das ondas presas:
- estão **mortas por falta fatal de memória**;
- no TDR 1, rodavam com o **VMID do IB anterior** e buscavam instrução numa página sem permissão de execução.

**Funcional:**
- o quadro do harness e depois o desktop do nosso DWM **ficaram** na tela (`BYTE_PROOF PASS` 256/256 no Present #10);
- a entrada congelou (relato do Daniel), e o harness teve 1/60.

## 2. Sucessos medidos

- **H1:**
  - o watchdog da 80 armou no boot `...623615`, diferente do da 79 (`...589905`);
  - o rollback terminou em `RECOVERY_OK_ONLINE` e gravou `C:\BC250_REQUIRE_CLEAN_BOOT.json` com o boot corrente
    (`written_by = watch-bc250mcdm-safe-bind.ps1 (RECOVERY_OK_ONLINE)`).
- **Sonda de ondas (V2):**
  - 2 de 2 TDRs com `OndasAchadas=4` e `ValidMask=0x7F` (todas as leituras válidas, inclusive HW_ID2);
  - 8 linhas `[TDR_GFX_WAVE_VMID]`, todas `ME=0 PIPE=0 QUEUE=6`.
- **Display:**
  - `Present #5` SUBWINDOW 1280×720 (o quadro da foto) levou o scanout a 86/256;
  - `Present #7` foi um retângulo de cursor, 24×27 em (1280,532), por `DIRTY_RECTS`, com `DIRTY_RECT_PROOF Match=5/5`;
    o scanout **continuou** 86/256 (SCANOUT-VIVO);
  - dos Presents #9 em diante, flips (`Flip=1`) com o desktop do DWM levaram o scanout a 256/256;
  - `BYTE_PROOF PASS` no #10;
  - 16 `REDUNDANT_SKIP` e 15 `MODECHANGE_BLANK_SKIP`.
- **Controles:**
  - autoteste 5/5 em `nonggc`/`nongg`; `nodcc` com S1/S2 PASS e S3 NÃO MEDIDO (`res=-4`, device perdido);
  - 61 `Eng=0`;
  - zero `PREAMBLE_CNTL`;
  - zero BugCheck (o último `MEMORY.DMP` é de 21/09);
  - `TDR_RESTART PROVADO` 2/2.

## 3. Falhas — e o que a sonda mostrou

**As ondas presas, decodificadas por `gc_10_1_0_sh_mask.h` (P1):**
- `STATUS=0x00818000/0x00810000` = `VALID` + **`FATAL_HALT`** (bit 23);
- `TRAPSTS=0x50000100` = **`XNACK_ERROR`** (bit 28) + `EXCP` bit 8.
  - `EXCP[8]` é `MEM_VIOL` pela ISA RDNA. Essa definição não está no header local, então vale como P1 externo;
- `INST_DW0=0xBF810000`, a instrução no PC.
- Leitura: as ondas morreram numa falta de tradução e **nunca retiram**. É a explicação das 240 ondas presas em 9+ TDRs.

**TDR 1 → T1.**

| Item | Valor |
|---|---|
| IB travado (`CARIMBO -0`) | VMID **3** |
| IB anterior (`CARIMBO -1`) | VMID **1** |
| VMID das 4 ondas (`WaveVmid`) | **1** |
| PC das ondas | `0x200040C00` |

O fault registrado bate campo a campo:
- `Vmid=1`, `Cid=9` (busca de instrução do SQC), `Rw=0`;
- `PermissionFaults=0x8` (execução);
- página `0x200040`.

No espaço do VMID 1, essa página é a alocação `STAGING` da nossa UMD:
- mapeada com `NoExecute=1`;
- PTE `0x...061`, sem o bit 4 `EXECUTABLE` (`amdgpu_vm.h:65`).

Por que a onda estava nessa página:
- a faixa `0x2000xxxxx` é o heap de 32 bits da RADV (`RADV_WDDM2_32BIT_HEAP_START = 0x200000000`), onde ficam os
  shaders;
- no processo do VMID 1, esse endereço não é shader;
- portanto um dispatch buscou um PC válido em outro processo usando a tabela do VMID 1.

Ressalvas:
- o latch estava "preso" e a tabela mudou na janela;
- mas o status é de permissão, não de página ausente, e casa com o VMID das ondas.
- Classificação: **P0 na assinatura, H na atribuição exata ao IB de VMID 3.**

**TDR 2 → T2.**
- IB travado VMID 3, anterior VMID 1; ondas com **VMID 3**, PC `0x201600D00`, também `FATAL_HALT`/`XNACK_ERROR`.
- O fault latchado é de outro VMID: `Vmid=2`, `Cid=0`, escrita, `0x2401A1000`, num VMID "RETIDA_POS_MORTE". É falta
  antiga; a falta destas ondas **não foi capturada**.

**Display.**
- Só 1 Present usou retângulos. Os outros 15 `DIRTY_RECTS_RECUSADOS` eram **flips** (`Flip=1`, `SrcRect=DstRect=0`,
  SubRect de tela cheia), e a cópia integral é o comportamento certo para flip.
- A entrada ficou congelada.
- Harness 1/60, falhando no quadro 2 com device removido, no TDR 1.

## 4. Veredito frente ao critério (docs/352)

- **TDR 1: T1** — `WaveVmid` = VMID do `CARIMBO -1` (1) ≠ `-0` (3).
- **TDR 2: T2** — `WaveVmid` = VMID do `CARIMBO -0` (3).
- A corrida teve os dois desfechos, um em cada TDR; nenhum é universal.
- **Display: G.**
  - RECT-ATIVO e SCANOUT-VIVO aconteceram, e a imagem ficou;
  - mas A exige o cursor se atualizando, e a entrada estava congelada. Desfecho parcial não é desfecho.
  - D não ocorreu (Match 5/5); C também não (1 retângulo exercitado).
- **H:** não ocorreu; H1 funcionou.
- **F:** não ocorreu.

## 5. Recuperação e pendências

- A tentativa 1 do rollback não fechou online, e o watchdog pediu reboot às 09:16:41.
- A tentativa 2 terminou em `RECOVERY_OK_ONLINE` às 09:18, com BasicDisplay Código 0 em 5 amostras.
- O marcador de boot frio está armado com o boot atual (09:17:17). **Desligamento completo antes de qualquer bind.**
- **NÃO VALIDADO EM HARDWARE:**
  - eliminação dos TDRs;
  - desktop interativo;
  - 60 quadros;
  - a causa do TDR 2.

**Próximos passos propostos (não implementados):**
1. **Troca de VMID no anel (T1):** comparar a nossa sequência com a do upstream em `gfx_v10_0.c`:
   - `emit_pipeline_sync`;
   - `emit_vm_flush`;
   - `SWITCH_BUFFER`/`CONTEXT_CONTROL`;
   - `emit_frame_cntl`.

   A leitura é externa; o repositório só tem `gfxhub_v2_0.c`. Hipótese a testar: as ondas do dispatch herdam o VMID
   antigo porque falta algum passo que o upstream emite entre IBs de VMIDs diferentes. Uma variável comportamental,
   escolhida pela leitura.
2. **Captura da falta do TDR 2:** limpar o latch de fault depois de cada TDR recuperado, para que o próximo registre a
   própria falta.
