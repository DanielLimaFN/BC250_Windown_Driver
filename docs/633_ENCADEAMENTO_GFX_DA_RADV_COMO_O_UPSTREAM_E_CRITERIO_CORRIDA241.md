# 633 — Encadeamento GFX da RADV como o upstream; critério da Corrida 241

**Escrito antes do ensaio (Lei 10), 07/10/2026, offline.** Continua o [doc 632](632_CORRIDA240_RLC_PARADO_CONFIRMADO_PREAMBULO_DO_MENU_E_ENCADEAMENTO_DA_RADV_COM_VMID7.md) §4.

## 1. Mudança (uma variável)

`mesa-radv-wddm/src/amd/vulkan/winsys/common/radv_winsys_cs.h`, só nos ramos `AMD_IP_GFX`, de volta ao commit base (upstream):

| Ponto | Antes (local, sem commit) | Agora |
|---|---|---|
| `radv_winsys_cs_grow` (encadeia CS crescido) | `CHAIN \| (1<<22) \| (7<<24)` | `CHAIN \| VALID` |
| `radv_winsys_cs_chain` (encadeia CSs; é o fim do preâmbulo) | `CHAIN \| PRE_ENA \| (1<<22) \| (7<<24) \| size` | `CHAIN \| VALID \| PRE_ENA \| size` |
| `emit_secondary_ib2` | `ac_emit_cp_indirect_buffer` + `(1<<22) \| (7<<24)` | só `ac_emit_cp_indirect_buffer` |
| `execute_ib` | idem | só `ac_emit_cp_indirect_buffer` |

Os ramos de compute (MEC) ficam como estavam. KMD **igual** ao da 240 (pacote 3.385.1.0, SYS `40916F50…`).

- RADV recompilada **do zero** (108 objetos apagados, lição do ninja parcial): **`DFD8D120…`**; avisos só os 7 de redefinição de macro de sempre; auditoria da RADV PASS. Cópia da anterior: `vulkan_radeon.dll.pre-chain-7B82C4AE`.
- O KMD só descreve encadeamentos (`[GfxIbInspect]`), não os valida; nada a mudar lá.

**Risco declarado:** não há registro de por que o encadeamento GFX foi mudado para bit 22 + VMID 7. Se a mudança corrigia algo, isso pode voltar (desfecho D).

## 2. Roteiro

Igual ao da 239/240 (só o menu, abrir/fechar a cada ~30 s até o rollback). `tools\EXECUTAR_CORRIDA241_ENCADEAMENTO_UPSTREAM_MENU_600S.cmd`, elevado, depois de **desligar** e com Claude, Edge, WebView2, Configurações, Gerenciador e Microsoft 365 Copilot fechados (o preflight recusa reinício e esses apps). Dry-run: passa nos hashes do pacote e da RADV nova.

**Pré-condição de validade:** os despejos do menu mostram o preâmbulo (VA `0x2407D4000`, 184 dw) terminando com controle **sem** bit 22 e **sem** VMID 7 (esperado `0x00904008` ou com `PRE_ENA`, `0x00B04008`). Sem isso, a RADV nova não estava em uso e a corrida é inválida.

## 3. Desfechos

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | 600 s de menu (≥12 ciclos) **sem** `TDR_GFX_QUIESCE` e **sem** trava dura; rollback limpo | O encadeamento era a causa do TDR do menu (n=1; na 239/240 o TDR veio aos ~7–9 min). Repetir uma vez; depois atacar a trava sem TDR (RLC rodando, VALID do KMD) |
| B | Sem TDR do GFX, mas **trava dura** com a assinatura de sempre (GPU ociosa no último tique) | O TDR some e a trava fica: são problemas separados. Próximo: RLC rodando como o amdgpu |
| C | TDR do GFX de novo no 1º IB de contexto novo (preâmbulo, depois de troca de VMID) | O encadeamento não é a causa. Próximo suspeito do mesmo TDR: o `DX43_VMID_WARMUP_IB` (3 dw do preâmbulo executados antes) e o VALID que o KMD põe no IB do anel |
| D | Regressão: TDR, fault de VM ou device-lost **cedo** e em outros processos (DWM/Explorer), ou recusas da UMD que não existiam | A mudança local corrigia outra coisa. Reverter para `7B82C4AE`, documentar o motivo e voltar ao C |
| E | Fora do mapa | Descrever antes de julgar |

**Ressalva estatística:** o TDR veio nas duas corridas só-menu (239, 240). Um desfecho A em n=1 é indício, não prova.

**Teto:** 2 corridas com este par (KMD 3.385.1.0 + RADV `DFD8D120`).

**NÃO VALIDADO EM HARDWARE:** tudo deste doc.
