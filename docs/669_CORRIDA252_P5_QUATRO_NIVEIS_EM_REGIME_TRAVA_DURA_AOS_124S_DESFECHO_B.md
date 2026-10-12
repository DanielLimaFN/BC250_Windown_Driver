# 669 — Corrida 252: P5 (4 níveis, 48 bits) em regime, desktop de volta e trava dura aos ~124 s (desfecho B)

**Bind:** 09/10/2026 08:51:38, lab **3.398.1.0** (SYS `B4EC4FB3…`, UMD `B73F9BDA`, RADV `B4AE2E14`), sem KD, só o menu.
**Evidência:** `docs/evidence/2026-10-09_08-51-38-DESKTOP-HQAG6T4-lab-binding/`.
- `display-log-this-bind-RECUPERADO.txt` (14.468.905 bytes, sha256 `375CCF5A…`): o watchdog morreu e a cauda foi resgatada do log cumulativo pelo offset do baseline.
  - Linhas 1–114.442: o bind.
  - Linha 114.443 em diante: o StartDevice recusado pelo fusível depois do reboot.
- `paging-trace-this-bind-RECUPERADO.bin`, mais `b.txt` (só o bind).
- Kernel-Power 41 às 08:54:41: reinício sem desligamento. A placa está em Código 43 (`CM_PROB_FAILED_POST_START`) pelo fusível.

## 1. Pré-condição do doc 668 §5: completa

| Item | Medido |
|---|---|
| P5-a | `[McdmQAI] GPUMMUCAPS: VaBits=48, Levels=4, PageTableUpdateMode=GPU_VIRTUAL` |
| P5-b | `PAGETABLELEVELDESC` Level 0, 1, 2 e 3, todos `Bits=9, Size=4096` |
| P5-c | 11 `SetRootPageTable` com `NumEntries=512, MaxEntries=512 (rootBits=9)`; zero `[ActiveSetRootPT] FAIL` |
| P5-d | 10 `[VMID_POOL] INSTALACAO ... Readback=1 Flush=1 Ready=1` (VMID01, 02, …). O readback é igualdade exata com `0x007FFE07`, então **DEPTH=3 está provado no silício** (P0) |
| P5-e | zero `[UpdatePT FAIL`, zero `GFXHUB_FAULT`, zero `[TDR_GCVM_FAULT]`, zero `PT_WALK … FALHA` |
| P5-f | desktop de volta: 1.249 flips, 1.290 `COMPOSITE_BLT OK`; o Daniel viu a tela parar, logo ela estava mostrando imagem |

## 2. O que a corrida fez

- 5.656 IBs do SDMA0, todos `IB EXECUTADO`. 1.509 submissões do GFX e 1.509 aposentadas.
- Zero TDR, zero timeout, zero BugCheck, zero DEVICE_REMOVED, zero recusa.
- Na régua: **831 FILLs**, 304 evicts, 14.360 FLUSH_TLB. A 251b teve 415 FILLs em ~53 s; a 250, 1.754 em ~213 s; a mediana histórica é ~793.
- Duração: ~124 s de atividade registrada (QPC da primeira à última linha). O fim fica perto das 08:53:42; o reset veio ~1 min depois.
- Atividade em ondas por janela de 10 s, uma por abertura do menu: rajadas de 1.000–1.350 IBs, intercaladas com quase zero (5–30).

## 3. Como a máquina morreu: a assinatura de sempre

- **Fim do log:** a última linha é normal, no meio de uma rajada.
  - `[McdmEscape] ENTER #1293` (composição);
  - `PAGING_DDI ENTER id=14608 operation=12` (`DXGK_OPERATION_FLUSH_TLB`, `d3dkmddi.h`);
  - `CTX_PROBE`.
  - As últimas 400 operações de paginação são todas FLUSH_TLB, a ~116/s, a mesma taxa da 251b (~132/s). É a sequência normal do VidMm depois de atualizar PTE, não um laço novo.
- **Último trabalho:** todo o trabalho submetido estava aposentado na última observação. SDMA0 fence 5656 submetida e aposentada; GFX ring fence 1510 aposentada. A GPU estava ociosa.
- **Rastro de paginação:** o último tique do bind (1110, `snapshot_time` 853,8 s) só traz contadores subindo. Nada travado em voo.
- **"Parece um laço":** a evidência não mostra laço de software do driver.
  - Não há padrão repetido no fim do log, e não há linha de erro.
  - Um laço em DPC/ISR nosso daria `0x133` em ~20 s, como em 10/09 12:16, e houve ~1 min até o reset sem BugCheck.
  - Nesta trava o KD também morre (memória `bc250-kd-nao-para-a-trava-dura`), o que não acontece com CPU presa em laço.
  - Com P2 (observado, não provado): é congelamento da plataforma, não um `for` sem saída. A tela parou porque a máquina inteira parou.

## 4. Veredito: desfecho B

Pré-condição completa e trava dura com a assinatura de sempre.
- **A forma da árvore de páginas sai das causas suficientes** (P0 para a forma testada: 4 níveis × 9 bits, DEPTH 3, 48 bits, igual ao `amdgpu`).
- A P5 **fica**: é paridade com o `amdgpu` e rodou sem nenhum fault.
- A SOMBRA em 0 devolveu a imagem, como previsto.
- Os 831 FILLs estão dentro da dispersão conhecida. Não é ganho nem perda atribuível à P5.

## 5. Próximo

O doc 668 §5 manda, em B, ir ao núcleo restante: paginação pelo SDMA0, ordem de submissão e MEC. Antes de propor ensaio, fazer mineração e leitura do código (Lei 9):
- comparar a sequência de submissão de FLUSH_TLB/UPDATE_PAGE_TABLE no fim das ~12 travas com rastro BPG1;
- comparar com o que o `amdgpu` faz por `amdgpu_vm_update` (fence de VM antes do uso).

O EDID a 60 Hz segue pendente como item separado.

**NÃO VALIDADO EM HARDWARE:** a causa da trava dura.
