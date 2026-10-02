# 489 — Corrida 147 (DX.91 sem KD): trava; a correção não agiu; submissão e flush em voo em CPUs diferentes

> **Corrida:** 27/09/2026. Bind às 22:59:53, sem KD (`NODEBUG`). Trava com tela preta e reset; boot às 23:04:31. **Candidato:** DX.91 / 3.297.1.0, harness atrasado 150 s.
> **Evidência:**
> - pasta `docs/evidence/2026-09-27_22-59-53-DESKTOP-HQAG6T4-lab-binding`;
> - cauda do KMD a partir do offset 3.522.841.083;
> - rastro a partir do offset 128.439.504 (1.540 ticks).
>
> **Relato do Daniel:** "deu tela preta e reiniciei".

## 1. Relato obrigatório
**1. Houve avanço?** Sim, de diagnóstico: um dado novo nas migalhas. Sem avanço funcional.

**2. Sucessos (P0):**
- O último BGH1 é DEPOIS completo: anel gráfico ocioso, fence em dia, `CP_STAT=0`, MMIO e VRAM respondendo. É a assinatura das 143/144.
- 22 superfícies 1280x720 no KMD: o harness estava em execução.

**3. Falhas:**
- Trava dura; paginação ENTER 26.760 contra LEAVE 26.759.
- **O último estágio do rastro é o FLUSH_GFXHUB_WAIT (42)**, dentro do flush de TLB. Nas outras travas era o commit do journal (44).
- **BCR1 na última gravação: a CPU 7 dentro de `SUBMIT_DDI` e a CPU 9 dentro de `PAGING_DDI` (FLUSH_TLB), ao mesmo tempo, sem acesso MMIO em andamento em nenhuma das duas.** É a primeira vez que o banco de submissão aparece numa trava (`records_in_flight=1`, então não se sabe há quanto tempo).
- **Zero `RANGE_SCAN`, recusado ou não:** a DX.91 não agiu e a máquina travou assim mesmo. **A resolução cruzada não é a causa da trava** (continua sendo a causa provável da corrupção da 145).

**4. Veredito frente ao doc 487 §3: C (trava).** A última `DX91_PAGING_MOVE` foi um FILL de 5 MB, sem anormalidade; não houve `RANGE_SCAN RECUSADO` antes.

**5. Recuperação e pendências:**
- Às 23:08 o dispositivo estava em erro (fusível), com o watchdog ainda observando; rollback pendente.
- **NÃO VALIDADO EM HARDWARE:** a causa.

## 2. Quadro das travas
| Com KD | Sem KD |
|---|---|
| 145, 146: 0 travas em 2 | 139, 140, 143, 144, 147: 5 travas em 5 |

Isto reforça a hipótese de condição de corrida que o tempo do KD esconde (H, amostra pequena).

## 3. Leitura de código feita agora (P1)
- O flush de TLB pela CPU usa o **motor de invalidação 17** (`Bc250PagingBuildGfxhubTlbFlushPlanForEngine(..., 17u, ...)`); a troca de VMID do anel gráfico usa o **motor 0** por PM4. Não disputam o mesmo par REQ/ACK, que é o desenho do amdgpu.
- `TlbLock` só é tomado em `Bc250ActiveFlushVmidTlb`, com laço limitado a 1.000 polls.
- Espera cruzada entre flush e submissão: **não encontrada por leitura**. O dado que falta é **onde, dentro da submissão, a CPU 7 estava**.

## 4. Próximo passo proposto
**DX.92, só instrumentação:** marcar estágios dentro de `SubmitCommandVirtual` (Node 0 SDMA e Node 1 GFX: entrada, locks, resolução, cópia, escrita de WPTR, saída) no mesmo rastro DX.56 que já marca a paginação. Na próxima trava, o rastro diz o estágio exato da submissão e do flush em voo. Sem esse dado, repetir a mesma corrida só confirma o que já sabemos.
