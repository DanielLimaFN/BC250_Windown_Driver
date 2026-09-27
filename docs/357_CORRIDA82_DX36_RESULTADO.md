# Corrida 82 — DX.36 (reversão do SWITCH_BUFFER, sonda do alvo 0x2401A1000): resultado

> **23/09/2026, 10:12:15–~10:16.** Candidato lab **3.243.1.0**, SYS `1E1575F5...` (conferido em
> `binding-provenance.json`), `GfxWaveProbe=1`.
> Pasta: `docs/evidence/2026-09-23_10-12-15-DESKTOP-HQAG6T4-lab-binding/`; log `FINAL` completo (24.237 linhas).
> Critério prévio: [docs/356](356_DX36_CRITERIO_CORRIDA82_REVERSAO_E_ALVO_2401A1.md).
> **Veredito:**
> - reversão **R-OK**;
> - sonda do alvo **P1**;
> - TDRs **3 de 3 T1**, cada um com a falta própria capturada pela linha do tempo;
> - sem BugCheck; sem F.

## 1. Houve avanço?

**Sim, de diagnóstico, e decisivo para a classe de TDR.**

**Funcional:** o desktop do nosso DWM apareceu com partes faltando (relato do Daniel) e ficou.
- `MODECHANGE_BLANK_SKIP` preservou o scanout 256/256 inclusive depois do último TDR.
- O cursor foi apresentado por retângulos sujos em posições que **mudam** ao longo da corrida: (1295,620) → (1144,588) →
  (1006,555), com `DIRTY_RECT_PROOF` 5/5 e 10/10 em todas.
- Harness 0/60.

## 2. Sucessos medidos

- **R-OK:** 37 trocas com `PM4DW=31`, zero `0xC0008B00`, e o anel avança depois das trocas.
- **Linha do tempo de faltas:** 16 amostras. Pela primeira vez, **cada TDR tem a sua falta registrada logo antes dele**
  (tabela no §3).
- **Sonda `[DX_TARGET_2401A1]`:** 176 linhas, 36 com `V=1`, 11 delas com **`ReadOnly=1`** (RawFlags `0x29`, PTE
  `...031` sem o bit 6).
- **Controles:**
  - `nonggc`/`nongg` 5/5; `nodcc` com S1 PASS e S2 `MAP_FALHOU` (NÃO MEDIDO);
  - zero BugCheck (o último `MEMORY.DMP` é o da 81);
  - zero `PREAMBLE_CNTL`;
  - `Eng=0` em 37;
  - `TDR_RESTART PROVADO` 3/3;
  - boot novo; watchdog `RECOVERY_OK_ONLINE` com marcador gravado.

## 3. TDRs — 3 de 3 T1, com a falta de cada um

| TDR | IB travado (`IB1 Base`) | IB anterior | VMID das ondas | PC das ondas | Falta na linha do tempo, logo antes do TDR |
|---|---|---|---|---|---|
| 1 | `0x2400E0000`, VMID 2 | VMID 1 | **1** | `0x200040C00` | `Vmid=1 Cid=9` (busca de instrução), `Perm=0x8` (execução), `0x200040000` |
| 2 | `0x240030000`, VMID 2 | VMID 3 | **3** | `0x2003C0B00` | `Vmid=3 Cid=9`, `Perm=0x8`, `0x2003C0000` |
| 3 | `0x241CA8000`, VMID 2 | VMID 1 | **1** | `0x201800D00` | `Vmid=1 Cid=9`, `Walker=6 Mapping=1`, `0x201800000` |

Em todos:
- as ondas estão com `FATAL_HALT` + `XNACK_ERROR`;
- a página da falta é a página do PC das ondas;
- o VMID da falta é o das ondas, que é o **VMID do IB anterior**, não o do IB travado.

É o mecanismo completo, **P0**: o dispatch do IB novo lança ondas com o VMID do IB anterior; elas buscam o shader do
processo novo na tabela do processo antigo, dão falta de execução ou de mapeamento e ficam presas. **Três de três, sem
exceção.**

A Corrida 80 teve um TDR T2. Nesta, com a linha do tempo, os três casam com T1. O T2 da 80 fica sem reinterpretação:
ele não teve a própria falta capturada.

## 4. Escrita sem permissão em 0x2401A1000 — P1

- **Mapeamento (P0):** em cada processo de vida curta, o `UpdatePageTable` mapeia em `0x2401A0000`:
  1. primeiro, uma alocação de 16 páginas, gravável (`ReadOnly=0`, PTE `...071`);
  2. em seguida, no **mesmo VA**, uma de 20 páginas, **somente leitura** (`ReadOnly=1`, PTE `...031`), às vezes até
     com o mesmo PFN (hProc `29A`: PFN `0xDD1` nas duas).
- **Quem escreve (P0):** o IB `0x2401DC000` (552 DW), decodificado do dump `gfx-ib-dump`, programa
  **`CB_COLOR0_BASE = 0x02401A00`**. O render target do draw (`DRAW_INDEX_AUTO` de 3 vértices) é esse VA.
- **Casamento (P0):** o processo que mapeia a alocação somente leitura (`hProc` X) cria o contexto `X+3`, que submete
  esse IB. A falta sai com o VMID desse contexto, nas páginas `0x2401A1000`/`A2000`/`A3000`, todas dentro da alocação
  de 20 páginas.
- **Leitura (H):** reuso de VA. O render target foi liberado e o VA passou a uma alocação somente leitura, sem mudar o
  endereço que o draw usa. Na RADV, `radv_wddm2_cs.c:550` cria os buffers de comando com
  `READ_ONLY|GL2_BYPASS|GTT_WC`, e `radv_wddm2_bo.c:636` mapeia com `Write=!READ_ONLY`.
- Isso **não causa TDR**, porque o IB aposenta. Mas as escritas desse draw são descartadas. Se o render target for
  conteúdo de janela, é **candidato** (H) para as "partes cortadas" que o Daniel viu.

## 5. Veredito frente ao critério (docs/356)

- **R-OK.**
- **P1:** `V=1 ReadOnly=1` no mesmo processo do fault. A implicação escrita antes (identificar o BO e por que o render
  target caiu num BO somente leitura) está parcialmente respondida no §4.
- **TDRs:** T1, T1, T1.
- **F:** não. **H:** não. Nenhum BugCheck.

## 6. Recuperação e pendências

- Watchdog: a tentativa 1 pediu reboot; a tentativa 2 terminou em `RECOVERY_OK_ONLINE`. BasicDisplay Código 0.
- Marcador de boot frio armado com o boot de 10:14:52.
- **NÃO VALIDADO EM HARDWARE:** eliminação dos TDRs, desktop íntegro e interativo, 60 quadros.

**Próximos passos propostos (não implementados):**
1. **TDR (T1, 3 de 3):** o candidato já escrito no docs/354, desfecho C — `CONTEXT_CONTROL` na troca, com
   `load_cs_sh_regs` (`gfx_v10_0_ring_emit_cntxcntl`: `dw2 = 0x80000000 | 0x8001 | 0x01000000 | 0x10002` em troca de
   contexto).
   - H: o estado do dispatch no pipe gráfico (incluindo o VMID das ondas) é recarregado na troca de contexto que o
     upstream sinaliza, e nós não sinalizamos.
   - É uma variável comportamental, e o risco de travar como o `SWITCH_BUFFER` tem de estar escrito no critério.
2. **Reuso de VA do render target:** offline, pelos logs da UMD, achar o processo (`pid`) dos `hProc` de vida curta e a
   sequência destroy/create do recurso de 16 páginas antes da submissão do IB de 552 DW.
