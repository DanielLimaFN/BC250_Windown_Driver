# 696 — Corrida 263b: trava na subida do desktop (desfecho D) e o que os contadores BFT1 mediram

**Data/hora:** 10/10/2026, bind às 21:23:20; desktop às 21:23:30,5; trava por volta de 21:23:33,5; máquina de volta às 21:24:06.
**Candidato:** lab 3.410.1.0. SYS assinado `F65336997E34C8A4…`, conferido na pasta de evidência.
**Evidência:** `docs/evidence/2026-10-10_21-23-20-DESKTOP-HQAG6T4-lab-binding/`. Coleta automática concluída às 21:26:11; teste de RAM sem erro.
**Critério:** [doc 695](695_PACOTE_3410_CONTADORES_BFT1_E_CRITERIO_CORRIDA263B.md) §5.

---

## 1. Pré-condição: completa

| Item | Medido |
|---|---|
| `[T2_PTE_MTYPE]` | `folha padrao MTYPE=0 snoop=0 \| coerente MTYPE=0 snoop=1 \| pedido P6 MTYPE=3`, status 0 |
| Base | `[4C_HDP_FLUSH_BYPASS] ATIVO`, `[C1_GFX_DOORBELL] ATIVA` |
| Desktop | 15 Presents pelo nosso driver (`COMPOSITE_BLT OK` com `SAMPLED_PASS 5/5`) |
| BFT1 | 3 registros da corrida (tiques 10, 20 e 30) mais 1 do boot de recuperação |
| T3 | **não chegou a rodar**: o log da T3 não tem sessão desta corrida |

## 2. Taxa: desfecho D

- A trava veio ~3 s depois de os processos do desktop começarem (dwm 21:23:30,63, Explorer 30,50, taskmgr 30,54, dxgiadaptercache 30,49, TextInputHost). As últimas linhas da UMD são de 21:23:33,53.
- O último tique do rastro é o 35 (~4 s depois do StartDevice): tudo aposentado.
- Zero TDR, zero fault, zero BugCheck. 1.065 IBs executados e 2.965 FLUSH_TLB no log.
- **Desfecho D** do doc 692/695: a trava veio antes da T3, como nas 262 (~3 s) e 256b (~8 s). Conta como ponto de dado de processo novo (processos do shell), mas não dá taxa da T3.
- A linha de base fica com 2 das 3 corridas do teto gastas: 263 = B (1/22), 263b = D.

## 3. O que os contadores BFT1 mediram (P0)

**Valores cumulativos no tique 30** (~3,5 s depois do StartDevice, ~0,6 s antes da trava):

| Campo | Valor |
|---|---|
| `flush_paging` (processo de paginação, 1 VMID cada) | 2.045 |
| `flush_user_vmid` (processo com VMID) | 44 |
| `flush_user_sem_vmid` (processo **sem** VMID) | **522** |
| `vmids_invalidadas` | 4.140 |
| `faixas_pte_sdma1` | 1.854 |
| `leituras_sq` | 27 |
| `invalidacoes_cpu_eng17` | 12 |

**Conferência:** 2.045 + 44 + 522 = 2.611, exatamente o `flush_tlb_count` do BPG1 no mesmo tique. Os contadores batem com o contador antigo.

**Último intervalo** (tique 20→30, ~1,2 s antes da trava): +797 paginação, +44 com VMID, **+214 sem VMID**, +1.968 VMIDs invalidadas, +758 faixas no SDMA1, +27 leituras do SQ, +5 invalidações pela CPU.

### Leitura pelo critério do doc 695

- **M1 — confirmado por medição.**
  - 20% de todos os FLUSH_TLB (522 de 2.611) vêm de processos **sem VMID**, e cada FLUSH_TLB de usuário invalida em média **3,7 VMIDs** (2.095 VMIDs para 566 flushes).
  - O amdgpu não faria **nenhuma** dessas invalidações para processo sem VMID.
  - A diferença de paridade da T5 (doc 694 §1.4) deixa de ser só leitura de código: é medida, P0.
- **M2 — inconclusivo.** Só há dois intervalos na corrida; não dá para calcular percentil. O SDMA1 recebeu 758 faixas no último intervalo, contra 1.025 no anterior.
- **M3 — positivo.** 27 leituras do SQ no último intervalo: o DWM começou a desenhar.
- **M4 — positivo nesta corrida.**
  - 5 invalidações pela CPU no último intervalo.
  - O log mostra instalação da VMID 3 pela CPU **0,167 s** antes do fim e troca do VMID 7 **0,011 s** antes do fim.
  - Na 263, a última foi 12,4 s antes. Logo, a programação de VMID pela CPU **não é comum às duas travas**.

**O que é comum às duas travas (263 e 263b):**
- FLUSH_TLB de processo sem VMID invalidando todas as VMIDs (na 263 o processo travado era um filho da T3, sem VMID; o contador não existia lá, mas o código é o mesmo);
- faixas de PTE no SDMA1;
- leituras do SQ com o DWM desenhando;
- poll do MEC a cada 1 ms.

Nenhum desses itens está provado como causa.

## 4. Defeito meu, corrigido offline

O resumo de `tools/decode-bc250-paging-trace.py` mostrava zero no BFT1 por dois defeitos:
1. Pegava o último registro do arquivo, que é do boot de recuperação. Agora corta no primeiro recuo do `writer_tick`, como a memória do projeto manda.
2. Com menos de 21 registros, comparava cada registro com ele mesmo nos deltas. O defeito era antigo e valia também para o BPG1 em corridas curtas.

Os decodificados da 263 e da 263b foram regerados. **Os contadores do driver estavam certos**; o erro era só do resumo.

## 5. Recuperação e pendências

- **Estado:** Código 43 pelo fusível; o BasicDisplay ativo.
- **Antes do próximo bind:** `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset` e desligar.
- **NÃO VALIDADO EM HARDWARE:** a causa da trava.
- **Próximo passo (decisão do Daniel):**
  1. **264 = FT.1**: FLUSH_TLB de processo de usuário invalida só as VMIDs onde a raiz daquele processo está instalada, e **nenhuma** para processo sem VMID, como o amdgpu. Os contadores BFT1 continuam: a pré-condição será `vmids_invalidadas` crescer só com a paginação e com processos que têm VMID. Uma mudança, de paridade, com o efeito medido nesta corrida.
  2. Ou a **263c** (terceira e última da linha de base) antes de qualquer mudança.
