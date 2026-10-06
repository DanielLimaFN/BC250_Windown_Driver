# 569 — 4c.2: `FlushTlb` pelo SDMA no anel, antes do IB, e o critério da Corrida 206

**Data:** 02/10/2026 · **Tipo:** implementação offline da etapa 2 da 4c ([doc 567](567_4C_COMPLETA_EM_5_ETAPAS_E_CRITERIO_CORRIDA205.md))
**Antecede:** [doc 568](568_CORRIDA205_4C1_ANEL_PROVADO_IB_TRAVA_NO_SRBM_WRITE_600S_SEM_TRAVA_DURA.md) (Corrida 205: invalidação pelo SDMA provada no anel; dentro de IB o SDMA para no `SRBM_WRITE`).

---

## 1. O que muda (uma variável)

| | Até a 205 | 4c.2 |
|---|---|---|
| Quem invalida a TLB do `FlushTlb` | A CPU, por MMIO (engine 17), **no build** do paging buffer, sob spinlock, uma transação por VMID | O **SDMA0**, por pacote no **anel** (engine 1), **antes do `INDIRECT`** da submissão seguinte do Node 0 |
| Quando | Quando o VidMm monta a operação, com IBs anteriores possivelmente em voo | Quando o SDMA chega lá: depois de todo o trabalho que ele já tinha, antes do IB que segue o build |
| Flush de HDP | No build | No build, **igual** (as PTEs ainda são escritas pela CPU no build) |
| PTE | CPU no build | CPU no build, **igual** (é a 4c.4) |
| VMIDs invalidados | 8, 7, 9 + as do pool instaladas | **A mesma máscara**, agora de fonte única |

Outros usos do flush por MMIO (instalação de VMID, compute) continuam por MMIO; saem na 4c.5.

## 2. Implementação

- `bc250_active_miniport.c`:
  - `Bc250ActiveTlbFlushVmidMasks`: **fonte única** da máscara, usada pelo flush por MMIO e pelo do anel. Copia o pool sob `VmidPoolLock` e solta antes de devolver; recusa o bit 0.
  - `Bc250ActiveFlushTlbHandler`: com `SdmaRingTlbFlushArmed=1`, **nenhum MMIO de invalidação**: só `Bc250ActiveFlushTlbHdpOnly` (escrita e invalidação de leitura do HDP, como antes) e `SdmaRingTlbFlushPending++`. Desarmado, segue exatamente o caminho da 205.
  - Submissão `INDIRECT` do Node 0: consome o pendente, pega a máscara e passa ao SDMA. Se o IB não for enviado, **devolve** o pendente em vez de perdê-lo. Log `[RING_TLB_FLUSH] #N no anel antes do INDIRECT` nas 8 primeiras e a cada 512.
  - Ensaio de boot: **só a passada do anel** (a do IB já foi respondida na 205 e custava um reset do SDMA0 em todo boot). `PROVADO` + tunable `SdmaRingTlbFlush` (padrão 1) → `[RING_TLB_FLUSH] ARMADO`. Qualquer outra coisa → `DESARMADO`, e o `FlushTlb` fica por MMIO.
- `bc250_sdma_present.c`, `Bc250SdmaPresentSubmitIndirectAsync(..., TlbFlushVmidMask, ...)`:
  - Monta fora do lock a invalidação de cada VMID da máscara (15 dwords cada, a sequência provada na 205), recusando a VMID 0.
  - **Espaço no anel conferido pelo `RPTR` do hardware** antes de escrever (pacote + preenchimento até o fim do anel + 16 de folga). Até hoje não havia conferência nenhuma; bastava a fila de 32 slots × 22 dwords caber em 1024. Com até 225 dwords de flush por entrada, isso deixou de ser garantido. Sem espaço, espera o `RPTR` andar (até 1 s) e, se não andar, recusa com log.
  - A entrada no anel fica `[invalidação][pad][INDIRECT][GCR][FENCE]`; `StartCursor` do slot aponta o começo da invalidação.

**Testes e portões:**
- `build-contract` EXIT=0, auditoria de segurança PASS.
- O portão (2h.3) procurava as três invariantes da máscara dentro de `Bc250ActiveFlushMappedGfxhubTlb`. Ele foi **reapontado para a fonte única**, com as mesmas três invariantes (pool incluído, lock solto antes do MMIO, bit 0 recusado), e passou a exigir também que o caminho do anel use a mesma fonte e que o SDMA recuse a VMID 0.
- **Quatro mutações verificadas**, todas reprovadas: tirar o pool da fonte única; não recusar o bit 0; o SDMA aceitar a VMID 0; o caminho do anel usar máscara própria. Fontes restauradas; auditoria PASS.
- O portão X2a (uma migalha por cópia para o anel) pegou a minha primeira versão; a cópia foi reorganizada para continuar contável.
- **Não há teste offline do `SubmitIndirectAsync`** (só existe no kernel). O encoder da invalidação tem gabarito do Linux desde a 4c.1.

**Pacote (assinado, auditado, NÃO INSTALADO):** lab **3.353.1.0**.
- SYS `CAC00090…41E4CA`, INF `AB2B4C0B…DEEAD9`, CAT `3BFDAEF1…097ED4`.
- UMD `6654865D…F2F1` e RADV `5447A17D…DE82`: iguais às da 204/205.
- Strings no SYS assinado: `[RING_TLB_FLUSH] #%ld no anel`, `[RING_TLB_FLUSH] %s (tunable`, `anel sem espaco por 1 s`, `FLUSH_TLB_ANEL`, `SdmaRingTlbFlush`.
- Lab package audit: PASS.

**Diferença de boot contra a 205, declarada:** sem a passada do IB, não há mais quiesce + restart do SDMA0 no boot (o boot volta a ser como o da 204).

## 3. Critério da Corrida 206 (escrito antes, Lei 10)

**Roteiro:** boot frio, CMOS em 256 MB, `tools\EXECUTAR_CORRIDA206_4C2_FLUSH_TLB_NO_ANEL_600S.cmd`, mesmo uso das 201–205. Se travar: botão de reset e `tools\coletar-pos-trava.ps1` primeiro.

**Linhas que decidem:** `[VMFLUSH_PROBE] VEREDITO ANEL`, `[RING_TLB_FLUSH] ARMADO|DESARMADO`, `[RING_TLB_FLUSH] #N no anel` e `[RING_TLB_FLUSH] RECUSADO`, mais as de sempre (`GFXHUB_FAULT`, `TDR_QUIESCE`, `IB_TRIPOD TIMEOUT`, migalhas pós-trava).

| Desfecho | Assinatura (o que o log mostra) | Implicação (para onde vai) |
|---|---|---|
| **A** | `ARMADO`, linhas `#N no anel` crescendo durante a corrida, **~600 s sem trava dura**, zero `GFXHUB_FAULT`, zero `RECUSADO` | Compatível com a invalidação fora de ordem como causa, **mas n=1**: 201 e 205 também não travaram sem ela. **Repetir o mesmo candidato** (n=2) antes da 4c.3 |
| **B** | `ARMADO`, `#N no anel` presentes até a parada, e **trava dura** com a assinatura de sempre (operação de paginação em voo) | A invalidação da TLB pela CPU não é a causa. Seguir para a 4c.3 |
| **C** | `ARMADO` e aparecem `GFXHUB_FAULT`, TDR real ou `IB_TRIPOD TIMEOUT`, que não aparecem nas 204/205 | A invalidação pelo anel está errada ou tarde para algum cliente (VMID faltando, ordem). Voltar offline; `SdmaRingTlbFlush=0` reverte sem reinstalar |
| **D** | `DESARMADO` (ensaio do anel não `PROVADO`) | O `FlushTlb` ficou por MMIO; a corrida é uma repetição da 204 e não mede a 4c.2 |
| **E** | Algo fora do mapa, inclusive `[RING_TLB_FLUSH] RECUSADO: anel sem espaco` ou `ARMADO` sem nenhuma linha `#N no anel` | Descrever antes de julgar |

**Regras:**
- Desfecho parcial não conta. Em especial, A exige as linhas `#N no anel` **crescendo**: `ARMADO` sem flush emitido não testou nada.
- O relato separa o que é da 4c.2 (flush no anel) do que é efeito de rodar uma corrida a mais (alternância das travas).
- **Riscos declarados:** invalidar mais tarde que antes (no SDMA, não no build) pode expor um cliente que dependia do flush imediato (desfecho C); e a espera por espaço no anel roda com a submissão segurando o contexto por até 1 s.

**NÃO VALIDADO EM HARDWARE.**
