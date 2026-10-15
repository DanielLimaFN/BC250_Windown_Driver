# 610 — Corrida 229: P3.0 deu desfecho A (MSI entregue, MMHUB fotografado) e 600 s sem trava dura

**Corrida:** 06/10/2026, bind às 08:40:58 e rollback às 08:51. Candidato lab **3.376.1.0** (SYS `C25E9857…`), CMOS em 256 MB, boot frio.
**Evidência:** `docs/evidence/2026-10-06_08-40-58-DESKTOP-HQAG6T4-lab-binding/`, com o log em `display-log-this-bind-FINAL.txt`.
**Critério:** doc 609 §4.

## 1. Desfecho A (P0)

- `[P3_IH] recurso de interrupcao #1: MENSAGEM (MSI) Vector=82 Level=5 Affinity=0xFFFF Flags=0x0003`. O Windows honrou o MSI do INF.
- `[P3_MMHUB] FOTO: 17/17 registradores lidos; nenhuma escrita no MMHUB`.
- `[P3_IH] NAO ARMADO: chave=0`, como previsto.
- StartDevice `status=0`, P1b ativa (`alias provado por escrita`).

## 2. A foto do MMHUB

Os campos foram decodificados pelo `inc/linux_ref/mmhub_2_0_0_sh_mask.h`.

| Registrador | Valor | Leitura |
|---|---|---|
| `MMVM_CONTEXT0_CNTL` | `0x007FFE80` | `ENABLE_CONTEXT=0`: **VMID 0 desligada** |
| `MMVM_CONTEXT0_PAGE_TABLE_BASE`/`START`/`END` | 0 | sem tabela de página |
| `MMVM_L2_CNTL` | `0x00080602` | `ENABLE_L2_CACHE=0` |
| `MMVM_L2_PROTECTION_FAULT_STATUS`/`ADDR` | 0 | nenhum fault |
| `MMMC_VM_MX_L1_TLB_CNTL` | `0x00000501` | L1 ligado, `SYSTEM_ACCESS_MODE=0`, `ADVANCED_DRIVER_MODEL=0`, `ECO_BITS=0xA`, `MTYPE=0` |
| `MMMC_VM_FB_LOCATION_BASE`/`TOP` | `0xF400`/`0xF40F` | FB em `0xF400000000`, 256 MB |
| `MMMC_VM_FB_OFFSET` | `0x460` | DRAM `0x460000000`, a mesma base provada pela P1b |
| `MMMC_VM_SYSTEM_APERTURE_LOW`/`HIGH` | `0x3D0000`/`0x3D0400` | abertura de sistema sobre o FB |
| `MMMC_VM_AGP_TOP`/`BOT` | 0/0 | AGP fechada |
| `MMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB` | 0 | — |

**Conclusão (P0):** o MMHUB está do jeito que o firmware deixou, só com as aberturas físicas. A VMID 0 está desligada e não há L2 nem tabela. A decisão do doc 609 de não armar o IH por VA está confirmada por medição.

## 3. Estabilidade

Comparação com a 226 (mesma P1b, 600 s) e com a 228 (trava aos ~454 s):

| Métrica | 229 | 226 | 228 |
|---|---|---|---|
| Duração | **600 s, sem trava dura** | 600 s | trava aos ~454 s |
| IBs aposentados | 7.718 (`NotifyFailed=0`) | 20.838 | 15.760 |
| Presents | 5.299 | 5.152 | 3.401 |
| `COMPOSITE_BLT OK` | 4.355 | 4.278 | — |
| SDMA0 | `Submitted=13052 Completed=13052 Fallbacks=0 HostCpuCopies=0` | 12.723/12.723 | — |
| HDP omitido (P1b) | 165.848 | 228.376 | — |

**Zero ocorrências de:**
- `TDR_QUIESCE`, `IB_TRIPOD TIMEOUT`, `Blt RECUSADO`, `Blt Timeout`;
- `GFXHUB_FAULT`, `DEVICE_REMOVED`, `NO_MEMORY`, BugCheck;
- eventos de TDR no log do Sistema e crashes de aplicativo (evento 1000).

**Também zero, ao contrário da 226:**
- `COMPOSITE_BLT RECUSADO (resolucao)`, `ResolveGpuVa RECUSADO`, `ActiveOpenAlloc RECUSADO`.

**As duas linhas `TDR_RESTART PROVADO`** saem antes do `[P3_IH]`, dentro do StartDevice. São da reconstrução do anel no bring-up, não de TDR.

**Teardown e rollback:**
- teardown limpo: `StopDevice Reason=2`, VMID8 desinstalada, GFXHUB restaurado (`Regs=26`), fusível CLEAN;
- `RECOVERY_OK_ONLINE`, com BasicDisplay em Código 0 em 5 amostras.

## 4. Atribuição

A 229 difere da 228 só pelo MSI no INF e por 17 leituras. Com a P1b ativa, a conta fica em 2 corridas de 600 s (226 e 229), 1 trava (228) e 1 tela azul (227, defeito FB.2, já corrigido).

**A ausência de trava NÃO é atribuível ao MSI.** A 226 também chegou a 600 s sem ele, e a 222 a 600 s sem a P1b.

**H, não verificado:** com a interrupção por linha, o Windows atendia uma linha que talvez fosse compartilhada; com MSI, não. É uma hipótese, e só a repetição separa as duas coisas.

**O menu Iniciar NÃO foi aberto nesta corrida** (Daniel, depois da análise). As travas anteriores vieram ao abrir o menu ou a Pesquisa, então estes 600 s tiveram carga menor que o roteiro. Isso explica, plausivelmente, os IBs a menos (7.718 contra 20.838 da 226). **A 229 não conta como amostra de estabilidade comparável à 228.**

## 5. Pendências

**NÃO VALIDADO EM HARDWARE:**
- a GART no MMHUB (P3.0b);
- toda a P3.1;
- a causa da trava dura.

**Próximo:**
1. Repetir a 229 com o mesmo pacote e boot frio. Custa uma corrida e nenhum build, e mantém uma variável por corrida.
2. Em paralelo, offline: desenhar a P3.0b como o `mmhub_v2_0_gart_enable`, com o IH ainda desligado.
