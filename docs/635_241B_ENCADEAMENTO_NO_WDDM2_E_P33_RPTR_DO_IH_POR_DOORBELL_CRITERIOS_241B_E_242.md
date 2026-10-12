# 635 — 241b (encadeamento GFX upstream também no `wddm2`) e P3.3 (RPTR do IH por doorbell): critérios da 241b e da 242

**Escrito antes dos ensaios (Lei 10), 07/10/2026, offline.** Continua o [doc 634](634_CORRIDA241_INVALIDA_O_WINSYS_WDDM2_REESCREVE_O_ENCADEAMENTO.md).

Pedido do Daniel: 241b e P3.3. Ficam em **duas corridas**, uma variável por vez. A 241b mexe só na RADV e usa o KMD da 240. A P3.3 vai num KMD novo (3.386), testado na 242 com a RADV da 241b.

---

## Parte 1 — Corrida 241b

### 1.1 Mudança

`mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_cs.c` (~1486). No fallback de encadeamento do submit, o ramo GFX não toca mais o controle que `radv_winsys_cs_chain` gerou (upstream: `CHAIN | VALID | PRE_ENA | tamanho`). O ramo de compute (MEC) fica como está.

Uma busca em `src/amd/vulkan/winsys` não acha outro ponto que escreva o controle de IB do GFX; os `(7u << 24)` que sobram são todos do compute.

| Artefato | Valor |
|---|---|
| RADV | **`B4AE2E1402505405…`**, nas duas cópias: build e `C:\BC250_LIB` |
| Cópia anterior | `C:\BC250_LIB\vulkan_radeon.dll.pre-wddm2chain-DFD8D120` |
| KMD | 3.385.1.0, o mesmo da 240 e da 241 (SYS `40916F50…`) |
| Script | `tools\EXECUTAR_CORRIDA241B_ENCADEAMENTO_UPSTREAM_WDDM2_MENU_600S.cmd` |

- **Recompilação:** só mudou um `.c`, então foi incremental, sem recompilar do zero.
- **DLL travada:** a DLL da pasta de build estava aberta pelo Explorer (pid 6304). Ela foi renomeada para `.locked-explorer-*` antes do link.
- **Por que as duas cópias:** a UMD tenta primeiro a DLL da pasta de build e depois `C:\BC250_LIB` (`bc250_umd_vulkan_bridge.c:94/103`). O preflight confere as duas.
- **Coleta:** o binder passa a copiar `<pacote>\AC\Temp\BC250_GFX_IB_DUMP` para `gfx-ib-dump-appcontainer\` também numa corrida sem trava (`bind-bc250mcdm-lab.ps1`). A auditoria de segurança passa.

### 1.2 Pré-condição de validade

Os despejos do menu (`gfx-ib-dump-appcontainer\Microsoft.Windows.StartMenuExperienceHost_*`) mostram o preâmbulo terminando com o controle **com o bit 23 (VALID) e sem VMID 7**:
- forma `0x00B0xxxx`, ou `0x0090xxxx` sem `PRE_ENA`;
- nunca `0x0750xxxx`.

Sem isso a corrida é inválida, como a 241.

**Roteiro:** abrir e fechar o menu a cada ~30 s **até o rollback**. A 241 parou em ~1 min e não alcançou a janela de 7–9 min em que a 239 e a 240 tiveram o TDR.

### 1.3 Desfechos

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | Pré-condição OK, ≥12 ciclos de menu, zero `TDR_GFX_QUIESCE`, sem trava dura, rollback limpo | O encadeamento era a causa do TDR do menu (n=1, indício). Segue a 242 |
| B | Pré-condição OK, sem TDR do GFX, **trava dura** com a assinatura de sempre | TDR e trava são problemas separados. Segue a 242, depois o RLC rodando |
| C | Pré-condição OK e TDR do GFX de novo no 1º IB de contexto novo | O encadeamento não é a causa. Suspeitos seguintes: `DX43_VMID_WARMUP_IB` e o VALID que o KMD põe no IB do anel |
| D | Regressão cedo (fault de VM, device-lost, recusas novas no DWM ou no Explorer) | Reverter para `DFD8D120`, registrar o motivo e voltar ao C |
| E | Fora do mapa | Descrever antes de julgar |

**Teto:** 2 corridas com este par.

---

## Parte 2 — P3.3 e a Corrida 242

### 2.1 Mudança (KMD 3.386, chave `BC250_P33_IH_RPTR_BY_DOORBELL 1`)

Como o `navi10_ih` com `use_doorbell=true` (P1, `inc/linux_ref/navi10_ih.c`, `nbio_v2_3.c`, `amdgpu_doorbell.h`):

| Item | Valor | Fonte |
|---|---|---|
| Índice da doorbell do IH | `AMDGPU_NAVI10_DOORBELL_IH 0x178 << 1 = 0x2F0` dwords = byte **`0xBC0`** da BAR de doorbell. A do SDMA0 fica em `0x800` (faixa de 20), sem colisão | navi10_ih.c:573 |
| `IH_DOORBELL_RPTR` | `OFFSET 0x2F0 \| ENABLE (bit 28)`, escrito em `navi10_ih_enable_ring`, com o anel ainda desligado | osssys sh_mask |
| `BIF_IH_DOORBELL_RANGE` | `0x3BC8` = `(0xD20 + 0x1D2) × 4`; `OFFSET 0xBC0`, `SIZE 2`; programado depois do anel e antes de ligar as interrupções | `nbio_v2_3_ih_doorbell_range` |
| ISR | publica o RPTR com uma escrita de 32 bits pela doorbell, no lugar de `IH_RB_RPTR` | `navi10_ih_set_rptr` |

**Lei 5:**
- Pré-condição: BAR de doorbell mapeada e `RCC_DOORBELL_APER_EN` ligado (P2c).
- Antes de escrever: snapshot de `DOORBELL_RPTR` e da faixa do BIF.
- Depois de escrever: readback dos dois.
- Prova: depois do NOP com TRAP, o `IH_RB_RPTR` lido (bits [17:2]) tem de igualar o RPTR **não-zero** que o ISR publicou pela doorbell. É a conferência do `navi10_ih_irq_rearm`.
- Sem prova: a doorbell é desfeita e o RPTR volta ao MMIO com o IH armado (`[P3_3_IH_DOORBELL] NAO ATIVA`).
- O desarme do IH e o desarme da P2 (que fecha a abertura do BIF, inclusive numa reconstrução por TDR) rebaixam o RPTR do IH para o MMIO antes de mexer.
- A linha periódica do IH ganha `rptr_por_doorbell` e `escritas_doorbell`.

**Verificação offline:**
- Auditoria `audit_p33_ih_rptr_doorbell` PASS; duas mutações verificadas (ISR voltando ao MMIO; prova sem a exigência de não-zero) reprovam.
- Build de laboratório EXIT=0, SYS `825AC196…` (`driver\wdk\output_lab`), com as strings da P3.3 no binário.
- Ainda **não empacotado**: empacotar sobrescreve `driver\wdk\package\output_lab`, que a 241b usa. O pacote 3.386.1.0 sai depois da 241b.
- **Correção (defeito meu):** o build também sobrescreveu `driver\wdk\output_lab`, que a auditoria do pacote compara com o manifesto. O preflight da 241b reprovou às 11:29, antes do bind.
  - O SYS e o PDB do 3.385 foram restaurados: o SYS sem assinatura recuperado com `signtool remove /s` sobre a cópia assinada da evidência da 241 deu exatamente `B7351EA7…`, o hash do manifesto; o PDB `66C76361…` veio da mesma pasta. Auditoria do pacote PASS.
  - O build da P3.3 ficou guardado em `driver\wdk\output_lab_p33_3.386_825AC196\`.
  - Para empacotar o 3.386.1.0: recompilar com `build-bc250mcdm-lab-bringup.ps1` (ou devolver essa cópia) e conferir `825AC196` antes.

### 2.2 Critério da 242 (KMD 3.386.1.0 + RADV `B4AE2E14`, mesmo roteiro do menu)

**Assinaturas:**
- **A:** `[P3_3_IH_DOORBELL] ATIVO` no boot, `rptr_por_doorbell=1` em todas as linhas de regime com `escritas_doorbell` crescendo, zero overflow do IH, e conclusão por interrupção igual à da 233/234.
- **A1:** além de A, sem trava dura em 600 s.
- **A2:** além de A, trava dura com a assinatura de sempre.
- **B:** `NAO ATIVA` (readback ou prova) e o IH segue por MMIO, como na P3.1.
- **C:** regressão do IH (overflows, ISR parado, `DESFEITA` em regime sem TDR do SDMA0).
- **E:** fora do mapa.

**Implicações:**
- A1: a paridade da P3.3 entra junto com o resultado da 241b; precisa de repetição.
- A2: a P3.3 sai das causas.
- B: a doorbell do IH não chega; comparar com a P2c (a abertura do BIF chega para o SDMA0 e o MEC).
- C: reverter a chave para 0.

**Teto:** 2 corridas.

**NÃO VALIDADO EM HARDWARE:** tudo deste doc.
