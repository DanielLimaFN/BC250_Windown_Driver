# 640 — MEC.1: faixa de doorbell do MEC como o amdgpu; critério da Corrida 243

**Escrito antes do ensaio (Lei 10), 07/10/2026, offline.** Continua o [doc 639](639_MINERACAO_P33_TAXAS_IGUAIS_E_FAIXA_DE_DOORBELL_DO_MEC_DIFERENTE_DO_AMDGPU.md).

## 1. Mudança

| Item | Antes (firmware, P0) | MEC.1 | amdgpu (P1) |
|---|---|---|---|
| `CP_MEC_DOORBELL_RANGE_LOWER` (`0xC170`) | `0x110` | **`0x110`** (não escrito) | `0x000` |
| `CP_MEC_DOORBELL_RANGE_UPPER` (`0xC174`) | `0x0FFFFFFC` | **`0x450`** | `0x450` (`gfx_v10_0.c:6699`) |

- **Por que só o `UPPER`:** a ativação da nossa fila do MEC deriva o offset da própria doorbell do `LOWER` (`bc250_compute_mqd_contract.c:2305`). Zerá-lo seria uma segunda variável. A diferença no `LOWER` fica declarada.
- **O que a mudança tira da faixa do MEC:** as doorbells do SDMA0 (`0x800`) e do IH (`0xBC0`). A da nossa fila (`0x110`) continua dentro.
- **Lei 5:**
  - `Bc250ActiveMec1Arm` registra antes a foto `[MEC1] FOTO` das faixas do MEC e do RB e do `CP_RB_DOORBELL_CONTROL`. As do RB só são lidas.
  - Escreve só o `UPPER`, pela fórmula SOC15, confere por readback e restaura se divergir.
  - Roda logo antes do arme do IH, ou seja, antes de a doorbell do IH começar.
  - `Bc250ActiveMec1Disarm` devolve o valor do firmware no teardown da GART, depois de o IH parar.
- **Transporte:** os cinco registradores entraram na lista de leitura; só o `UPPER` do MEC entrou na de escrita.
- **Auditoria:** `audit_mec1_doorbell_range` PASS; duas mutações reprovam (desarme fora do teardown; `LOWER` na lista de escrita).
- **Chaves:** `BC250_MEC1_DOORBELL_RANGE_LIKE_AMDGPU 1` e **`BC250_P33_IH_RPTR_BY_DOORBELL 1` (religada só para este ensaio).**

| Artefato | Valor |
|---|---|
| Pacote | **3.387.1.0** |
| SYS fonte | `1EE8C282…` |
| SYS assinado | `E8125C34…` |
| INF | `2AE45C18…` |
| CAT | `A1D4940A…` |
| UMD | `8782A5E5…` (a mesma) |
| RADV | `B4AE2E14` (a mesma) |
| Auditoria do pacote | PASS |
| Pacote 3.386 (242) guardado em | `driver\wdk\package\output_lab_3.386.1.0_242\` |
| Script | `tools\EXECUTAR_CORRIDA243_MEC1_FAIXA_DOORBELL_MENU_600S.cmd` |

O roteiro é o mesmo: só o menu, abrindo e fechando até o rollback.

## 2. Pré-condição de validade

O log mostra, nesta ordem:
1. `[MEC1] FOTO` com o MEC em `0x110..0x0FFFFFFC`;
2. `[MEC1] ARMADO` com o `UPPER` lido = `0x450`;
3. `[P3_3_IH_DOORBELL] ATIVO`.

Sem os três, a corrida não testa a pergunta.

## 3. Desfechos

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | Pré-condição OK e a máquina passa de **60 s** depois do bind, com desktop e menu (a 242/242b morreram em 15–20 s). `rptr_por_doorbell=1` nas linhas de regime | A faixa do MEC era o mecanismo da trava precoce da P3.3. A MEC.1 fica (paridade) e a P3.3 também. A duração até a trava dura ou o rollback é medida à parte |
| A1 | A, e 600 s sem trava dura | Além disso, a trava dura de sempre tinha a ver com a faixa do MEC (a doorbell do SDMA0, em `0x800`, cai nela desde a 223). Precisa de repetição |
| A2 | A, e trava dura depois de 60 s com a assinatura de sempre | A trava precoce saiu; a trava de sempre tem outra causa |
| B | Pré-condição OK e **trava em menos de 60 s**, como a 242/242b | A faixa do MEC sai das causas da trava precoce. A P3.3 volta a 0; a MEC.1 fica (paridade) |
| C | `[MEC1] RECUSADO` (readback) ou `[P3_3_IH_DOORBELL] NAO ATIVA` | O ensaio não aconteceu; registrar o motivo |
| D | Regressão nova: a fila do MEC para (submissões do compute com timeout, `HitObserved` ausente), ou TDR/fault cedo | A faixa nova corta algo de que o MEC precisava; reverter a chave |
| E | Fora do mapa | Descrever antes de julgar |

**Teto:** 2 corridas. Uma repetição só se der A, para separar A1 de A2.

**NÃO VALIDADO EM HARDWARE:** tudo deste doc.
