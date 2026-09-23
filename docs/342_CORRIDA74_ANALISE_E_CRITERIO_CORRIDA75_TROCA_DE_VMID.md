# Corrida 74: análise e critério da Corrida 75 (sincronia de troca de VMID + fork sem PREAMBLE_CNTL)

> **22/09/2026.** Evidência da 74: `docs/evidence/2026-09-22_22-15-50-DESKTOP-HQAG6T4-lab-binding/`.
> A Parte 3 é o critério da 75, **escrito antes do ensaio** (Leis 9 e 10).
> **Estado da faixa DX.29: NÃO VALIDADO EM HARDWARE.**

## Parte 1 — Corrida 74 (resumo; detalhe na BASE)

**Desfecho G do docs/341.** O A ficou parcial, e pela Lei 10.3 isso não conta.

- **P0, primeiro quadro de app D3D11 no monitor:**
  - o harness limpou em azul 0xFF2673E5 e desenhou o triângulo (0xFF7F4040);
  - o Present do quadro 1 foi aceito;
  - o KMD copiou o quadro de 1280×720 por SDMA para o scanout em DstXY=640,180.
  - Foi Present Blt do próprio harness, não composição do DWM.
- **P0, rotação DX.28:** 5 rotações pelas 3 imagens da cadeia e 5 `RTV_SEGUE_RECURSO`, zero recusas. As 5 grades de Present deram 86400/86400, nenhuma voltou a zero, e o desktop apareceu inteiro.
- **P0, TDRs:** 3, todos com 240 ondas.

## Parte 2 — Faixa DX.29 (duas variáveis, por decisão do Daniel)

### 2.1 A análise que motiva
Contei, pelo `IB1 Base` de cada TDR, qual IB travou e qual foi o anterior:

| Corrida | IB travado | VMID dele | VMID do IB anterior |
|---|---|---|---|
| 73 | Fence 108 (280 DW) | 4 | **1** |
| 73 | Fence 504 (568 DW) | 1 | **3** |
| 74 | Fence 171 (280 DW) | 4 | **1** |
| 74 | Fence 224 (280 DW) | 1 | **2** |
| 74 | Fence 276 (288 DW) | 3 | **1** |

**5 de 5** travamentos num IB logo depois de uma troca de VMID. As trocas são 92 de 506 IBs na 73 e 79 de 307 na 74 (18–26%); ao acaso, 5 de 5 teria chance da ordem de 10⁻³. Nos faults de busca de instrução (CID 9), o VMID do fault é o do IB **anterior**:
- 74, TDR 1: o shader do harness buscado sob o VMID 1;
- 74, TDR 3: o shader do próprio DWM (0x201600000) sob o VMID 1, com WalkerError=6.

**H:** o PFP avança para o IB novo com o anterior ainda em curso.

### 2.2 Variável 1 (KMD): sincronia de troca de VMID no anel gráfico
Em `Bc250ActiveGfxEngineSubmit`, quando o VMID do IB difere do anterior:
1. `WAIT_REG_MEM` com mem_space=memória, engine=**PFP**, função `>=`, sobre a fence do anel, até a última submetida. É o `gfx_v10_0_ring_emit_pipeline_sync` do upstream (usepfp=1).
2. `PFP_SYNC_ME`, que é o fim do `gfx_v10_0_ring_emit_vm_flush`.

Log: `[GfxEngine] VMID_SWITCH_SYNC #n: VMID a -> b`. Codificação em `nvd.h:166-186` e `:322` (P1).

**Divergências declaradas:**
- `>=` no lugar de `==`: a fence é monotônica, e `>=` não trava se o slot já estiver adiante;
- o flush de TLB continua por MMIO na instalação do VMID, e não no anel.

`LastSubmitVmid` volta a "nenhum" no boot e no restart de TDR.

### 2.3 Variável 2 (ICD): `PREAMBLE_CNTL` removido do fork
Aplicado o patch `external/radv-wddm/pending/DX29-...`: `radv_emit_graphics` fica igual ao upstream, sem `PKT3(0x4A)`.
- ICD novo: **`5988B38E86FEBB514C4C5F1D5FB5CE5A4B6FDD2B57668F2A1B6BACA3B1A0461F`**.
- Patch espelhado: `74304831...`.
- `PROVENANCE.md` atualizado.

### 2.4 Verificação
- `build-contract` EXIT=0, com o teste PM4 estendido (controle 0x115, header de `PFP_SYNC_ME` 0xC0004200, pacote de 7 DW).
- Fork recompilado; SYS de laboratório compilado; UMD inalterada desde a 74 (`5D66F9B3`).
- **Auditorias PASS:** MCDM (portão DX.29 novo), RADV (portão DX.29 novo), matriz de DDI e callbacks no-op.
- **Mutações que reprovam:**
  - 5 no MCDM: sem `PFP_SYNC_ME`, espera sem condição de troca, engine ME, restart sem zerar, `LastSubmitVmid` não atualizado;
  - 1 no RADV: o fork sem o patch;
  - 1 no teste: engine ME faz o teste PM4 sair com código 1.
- **lab 3.236.1.0** assinado, com `Lab package audit: PASS`, NÃO INSTALADO:

| Artefato | SHA-256 |
|---|---|
| SYS | `52CD8A05...` |
| UMD | `5D66F9B3...` |
| INF / CAT | `82BC8BD9...` / `B751F262...` |

**Riscos:**
- Se a fence do anel não alcançar o valor esperado, o PFP espera para sempre e o TDR vem com o ME/PFP em `WAIT_REG_MEM` logo depois de uma linha `VMID_SWITCH_SYNC` (assinatura D).
- Tirar o `PREAMBLE_CNTL` muda todo IB do RADV.

## Parte 3 — Critério da Corrida 75

### 3.1 Como separar as duas variáveis
- **O autoteste mede só a variável 2.** Ele roda antes do harness, num processo só, sem troca de VMID. O `decode` dos IBs mostra se o `PREAMBLE_CNTL` sumiu.
- **A variável 1 é medida pelos TDRs pós-troca**, que só existem com vários processos.

### 3.2 Sinais
- **ICD-OK:** stderr do autoteste com `gfx_level=12`, **e** nenhum `PREAMBLE_CNTL` nos IBs (`pm4list` / `decode`), **e** autoteste 5/5 nas três variantes.
- **SYNC-ATIVA:** linhas `VMID_SWITCH_SYNC` no log do KMD, em número próximo ao de trocas de VMID entre IBs consecutivos.
- **SEM-TDR-POS-TROCA:** zero TDR cujo IB travado (pelo `IB1 Base`) venha logo depois de IB de outro VMID.
- **DESKTOP-ESTAVEL:** o mesmo dwm.exe vivo do primeiro Present até o fim da janela, com ≥10 grades de Present não-nulas, e o mouse se mexendo (Daniel).
- **HARNESS:** quantidade de quadros completos, de 60.

### 3.3 Desfechos
**A — A troca de VMID era a causa dos TDRs**
- Assinatura: ICD-OK **e** SYNC-ATIVA **e** zero TDR na corrida inteira.
- Implicação: o desktop pode estabilizar. Próximo: DESKTOP-ESTAVEL e 60 quadros do harness, depois o caminho de composição do DWM para a janela do app.

**B — Os TDRs pós-troca somem, mas sobra TDR de outro tipo**
- Assinatura: ICD-OK, SYNC-ATIVA e SEM-TDR-POS-TROCA, com pelo menos um TDR restante.
- Implicação: a troca era uma causa. Descrever o TDR restante (ondas, pacote, fault, sentinela) antes de propor nada.

**C — A sincronia está ativa e o TDR pós-troca continua**
- Assinatura: SYNC-ATIVA, e algum TDR de 240 ondas num IB logo após troca de VMID.
- Implicação: o PFP avançando não era o mecanismo. Próximo: flush de VM **no anel** (base da tabela + invalidação), como o upstream.

**D — A sincronia trava**
- Assinatura: TDR com ME ou PFP em `WAIT_REG_MEM` imediatamente depois de uma `VMID_SWITCH_SYNC`, com a fence do anel abaixo do valor esperado.
- Implicação: o valor da espera está errado. Desligar a variável 1 e revisar a contabilidade da fence.

**E — O ICD novo regrediu**
- Assinatura: autoteste abaixo de 5/5, ou `PREAMBLE_CNTL` ainda nos IBs.
- Implicação: reverter a variável 2. Qualquer TDR fica sem atribuição.

**G — Fora do mapa**
- Descrever antes de julgar.

Desfecho parcial não é desfecho (Lei 10.3).

### 3.4 Execução
Boot frio (marcador armado) e depois `tools\EXECUTAR_D3D11_NATIVO.cmd -Force`. O executor implanta o ICD novo do `build\` do fork. Anotar o que aparecer no monitor e se o mouse se mexe.
