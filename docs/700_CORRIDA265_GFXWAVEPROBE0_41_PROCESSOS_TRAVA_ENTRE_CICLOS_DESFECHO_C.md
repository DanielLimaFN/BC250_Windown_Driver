# 700 — Corrida 265: GfxWaveProbe=0, 41 processos novos completos e trava entre ciclos (desfecho C)

**Data/hora:** 10/10/2026, bind às 21:59:22; T3 iniciada às 21:59:43; trava por volta de 22:03:55,7 (~273 s depois do bind); máquina de volta às 22:04:23.
**Candidato:** lab 3.412.1.0. SYS assinado `A8A6C087…`, conferido na pasta de evidência.
**Evidência:** `docs/evidence/2026-10-10_21-59-22-DESKTOP-HQAG6T4-lab-binding/`. Coleta automática concluída às 22:06:27 (exit 0); teste de RAM sem erro.
**Critério:** [doc 699](699_GFXWAVEPROBE0_PACOTE_3412_E_CRITERIO_CORRIDA265.md) §4.

---

## 1. Pré-condição: completa

| Item | Medido |
|---|---|
| `[StartDevice] GfxWaveProbe=0 (present=1)` | presente |
| `[FT1_FLUSH_POR_PROCESSO] chave=1`, `[T2_PTE_MTYPE] 0/0 \| 0/1 \| 3` | presentes |
| Base | `[4C_HDP_FLUSH_BYPASS] ATIVO`, `[C1_GFX_DOORBELL] ATIVA` |
| `leituras_sq` (BFT1) | **0** nos 238 registros |
| `KMD_GFX_SWITCH`, `[GFX_IB_IDENTITY]`, `[TDR_GFX…]` | 0, 0, 0 |
| FT.1 em efeito | `vmids − paginação − 16 × usuário_c/_VMID` ≤ 0 em todos os intervalos |
| `raiz desconhecida` | 0 |
| Desktop | 1.773 `COMPOSITE_BLT OK`; Present do DWM com `hr=0` até 22:03:55,633 |

## 2. Desfecho: C

**Assinatura:**
- A T3 completou **41 processos novos**, todos com código 0. O 41º (pid 12040) abriu às 22:03:51,04 e saiu às 22:03:53,03. O 42º não chegou a abrir (previsto para ~22:03:58).
- As últimas linhas da UMD são de 22:03:55,63:
  - o DWM com Present `hr=0`;
  - o **Explorer** fez `MapAllocation` e `MakeResident` de uma alocação nova (`E_PENDING`, fence de paginação 7252) e entrou em `WAIT_PAGING` **sem LEAVE**.
- O rastro termina no tique 2384. Os deltas dos tiques 2350→2370 têm paginação baixa (58 a 313 FLUSH_TLB) e o 2380 tem zero.
- Zero TDR, zero `GFXHUB_FAULT`, zero BugCheck; 9.307 IBs executados; 806 FILLs; 441 despejos.
- Tctl desta corrida entre 76,4 e 83,5 °C (76,5 °C no fim): não é calor.

**Por que C e não B:** a trava veio entre ciclos. O último filho começou ~4,6 s antes e já tinha saído ~2,6 s antes. Nenhum processo novo nos 3 s anteriores.

**Implicação (pelo critério):** as leituras do SQ/CP pela CPU a cada submissão não são causa suficiente. A hipótese do processo novo enfraquece mais uma vez.

## 3. Comparação com a linha de base

| Corrida | Pacote | Desfecho | Processos da T3 até a trava | Tempo até a trava |
|---|---|---|---|---|
| 263 | 3.409 (T2) | B | 21 (trava dentro do 22º) | — |
| 263b | 3.410 (+BFT1) | D | 0 | ~3 s depois do shell |
| 264 | 3.411 (+FT.1) | D | 0 | ~10 s depois do shell |
| **265** | 3.412 (+GfxWaveProbe=0) | **C** | **41** | **~273 s** |

**Leitura honesta:** foi a corrida mais longa da série 263–265 e a que mais processos novos passou. Mas com a taxa da 263 (1 trava em 22 processos), passar 41 teria ~15% de chance mesmo sem mudança nenhuma. A dispersão histórica vai de 3 s a 600 s. **Não dá para atribuir a melhora à GfxWaveProbe=0 com n=1.**

## 4. Padrão que se repete (P2)

Nas duas travas que caíram com a T3 rodando (263 e 265), o último evento visível é um processo esperando a **fence de paginação de uma alocação nova** (`WAIT_PAGING` sem LEAVE):
- 263: filho da T3, 3ª alocação, fence 7008;
- 265: Explorer, fence 7252.

Isso não mostra se a espera é a causa ou só o último evento antes de a máquina morrer: o fim do log é a última drenagem, não o último evento (doc 582). Fica como pista para a mineração, não como mecanismo.

## 5. Relato obrigatório

1. **Houve avanço?** Inconclusivo. Foi a corrida mais longa e com mais processos da série, mas dentro da dispersão conhecida.
2. **Sucessos:** a sonda está desligada e provada (`leituras_sq` = 0); a FT.1 continua em efeito; 41 processos D3D11 novos completaram com código 0; o desktop apresentou por ~4,5 min (1.773 composições); zero TDR e zero fault.
3. **Falhas:** trava dura aos ~273 s, entre ciclos; Código 43 pelo fusível.
4. **Veredito:** desfecho C. `GfxWaveProbe=0` fica, porque a sonda é instrumentação de TDR e o amdgpu não lê SQ a cada submissão.
5. **Recuperação e pendências:** BasicDisplay ativo; Código 43 pelo fusível. Antes do próximo bind: `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset` e desligar. **NÃO VALIDADO EM HARDWARE:** a causa da trava.

## 6. Próximo passo

Pelo critério, o próximo é o **poll do MEC sem o seletor GRBM**. Hoje a CPU escreve `GRBM_GFX_INDEX`/seletor a cada 1 ms para olhar o MEC; o amdgpu não faz isso em regime. Ainda está no inventário do doc 694 §2.3.

Opcional, sem custo de corrida: minerar os 41 ciclos desta corrida contra os 21 da 263, para ver se a espera de paginação de alocação nova muda de duração com o tempo.
