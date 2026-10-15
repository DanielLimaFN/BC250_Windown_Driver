# 673 — Corrida 253: C1 (doorbell do GFX) ativa e provada, e trava dura ao abrir o menu (~40 s) — desfecho B

**Bind:** 09/10/2026 10:11:52, lab **3.399.1.0** (SYS `F4156E65…`, UMD `B73F9BDA`, RADV `B4AE2E14`), sem KD, só o menu.
**Evidência:** `docs/evidence/2026-10-09_10-11-52-DESKTOP-HQAG6T4-lab-binding/`:
- `display-log-this-bind-RECUPERADO.txt` (5.842.667 bytes, sha256 `A17E4EE1…`; linhas 1–46.291 = o bind, cópia em `b.txt`);
- `paging-trace-this-bind-RECUPERADO.bin`;
- `monitor-corrida.csv` (MON.1, 24 amostras).

Kernel-Power 41 às 10:13:21. Placa em Código 43 pelo fusível.

## 1. Pré-condição do doc 672 §5: completa

| Item | Medido |
|---|---|
| C1-a | `[C1_GFX_DOORBELL] ATIVA (ONLINE): indice 0x116 (byte 0x458) CONTROL=0xC0000458 HIT=1 RANGE=0x00000458..0x0FFFFFFC MEC_UPPER=0x00000450`. Prova por 4 NOPs **só pela doorbell**, RPTR=964=WPTR em 1 poll. `HIT=1`: o CP viu a doorbell (P0) |
| C1-b | `WPTR_CANAIS ... PollCntlPedido=0 ... Antes=0x00400000 ... DoorbellGfx(C1)=1`: o `CP_RB_WPTR_POLL_CNTL` ficou no valor da placa |
| C1-c | zero `WPTR_READBACK PASS` e zero `SUBMIT RECUSA`: nenhuma publicação por MMIO; zero `DESARMADA`/`PROVA FALHOU` |
| C1-d | 769 `SubmitVirtual Node1 GFX] ENTER` e 769 `GfxRetire` (todas pela doorbell); 894 Presents, 649 `COMPOSITE_BLT OK`; desktop visível até a trava |
| C2 | `HqdActiveWptrPollCntlReadback=0` e `P1Snap{Act,Re,Sub}WptrPollCntl1=0`: MEC sem polling de WPTR, como o `amdgpu` |
| MON.1 | 32 amostras de Tctl no rastro (82,5–85,75 °C); o monitor gravou temperatura e uso 3D/Copy até 10:12:35 |

## 2. Como morreu: a assinatura de sempre

- **Duração:** ~36 s de log (QPC) e tique 320 do rastro, ~40 s depois do StartDevice. Aconteceu ao abrir o menu (relato do Daniel).
- **Fim do log:** normal, no meio de uma rajada: `COMPOSITE_BLT OK`, `CTX_PROBE`, `PAGING_DDI ENTER id=5404 operation=12` (FLUSH_TLB). Sem erro, sem repetição.
- **Último trabalho:** todo aposentado. A última submissão do SDMA0 (fence 2005) foi executada (`IB EXECUTADO ... FenceId=2005`), e o último GFX (ring fence 770) também. GPU ociosa.
- **Régua:** **255 FILLs**, 65 evicts, 5.300 FLUSH_TLB. Para comparar: 252 = 831/~124 s; 251b = 412/~53 s.
- **Monitor:** última linha às 10:12:35 com Tctl 82,5 °C, GPU 3D 12,6 %, Copy 3,2 %, 25,7 GFX/s, 19,9 IB/s. Nenhum sinal de carga ou calor fora do comum; a 199 travou a 80 °C.

## 3. Veredito: desfecho B

Pré-condição completa e trava dura de sempre.
- **O canal do WPTR do GFX** (MMIO com `GRBM_GFX_INDEX` em broadcast por submissão, e a cadência `POLL_FREQUENCY=4` escolhida por nós) **sai das causas suficientes** (P0 para a forma testada).
- **A C1 fica:** é o caminho do `amdgpu`, provou-se no silício (`HIT=1`, 769/769) e tira MMIO de toda submissão.
- **A C2 fica confirmada** como já em paridade.
- **A MON.1 funcionou:** a temperatura voltou ao rastro e ao monitor.

**Avanço:**
- Funcional: sim. Doorbell do GFX pela primeira vez neste projeto, sem nenhum MMIO de WPTR em regime.
- Na trava: não. Morreu em ~40 s, dentro da dispersão de sempre (8–584 s).

## 4. Próximo

Pela lista do doc 671, a próxima diferença com o Linux é a **C3**: o Windows e a UMD acessam a VRAM pela BAR/HDP (`CpuTranslatedAddress` = BAR), e o `amdgpu` em APU acessa pela DRAM. Depois vem a **C4** (zeramento por `CONST_FILL` em 4 MiB).

Antes da próxima corrida: `tools\BC250_RECOVERY.cmd` elevado, `tools\inspect-boot-fuse.ps1 -Reset` e boot frio.

**NÃO VALIDADO EM HARDWARE:** a causa da trava dura.
