# 698 — Corrida 264: FT.1 em efeito e trava na subida do desktop (desfecho D)

**Data/hora:** 10/10/2026, bind às 21:40:57; processos do shell carregaram a UMD às 21:41:05,8; última linha da UMD às 21:41:15,471; máquina de volta às 21:41:57 (evento 41 às 21:42:01).
**Candidato:** lab 3.411.1.0. SYS assinado `73EDA3F6A715EE83…`, conferido na pasta de evidência.
**Evidência:** `docs/evidence/2026-10-10_21-40-57-DESKTOP-HQAG6T4-lab-binding/`. Coleta automática concluída às 21:44:00 (exit 0); teste de RAM sem erro; nenhum evento WHEA-Logger.
**Critério:** [doc 697](697_FT1_FLUSH_TLB_SO_DAS_VMIDS_DO_PROCESSO_PACOTE_3411_E_CRITERIO_CORRIDA264.md) §5.

---

## 1. Pré-condição: completa

| Item | Medido |
|---|---|
| `[FT1_FLUSH_POR_PROCESSO] chave=1` | presente |
| `[T2_PTE_MTYPE]` | `0/0 \| 0/1 \| 3`, status 0 |
| Base | `[4C_HDP_FLUSH_BYPASS] ATIVO`, `[C1_GFX_DOORBELL] ATIVA` |
| Desktop | 150 `COMPOSITE_BLT OK`; o último `DXGI Present` do DWM voltou com `hr=0` às 21:41:15,471 |
| `raiz desconhecida` | 0 |
| T3 | **não rodou**: o log da T3 não tem sessão desta corrida (o último registro é da 263, 20:44) |

**A FT.1 pegou (contadores BFT1, P0):**

| Tique | paginação | usuário c/ VMID | usuário s/ VMID | VMIDs invalidadas | VMIDs − paginação |
|---|---|---|---|---|---|
| 20 | 752 | 0 | 209 | 752 | **0** |
| 30 | 1.086 | 87 | 184 | 1.229 | 143 |
| 40 | 379 | 19 | 107 | 414 | 35 |
| 50–80 | 19–54 | 7–20 | 0 | 33–94 | 14–40 |
| 90 | 125 | 31 | 10 | 169 | 44 |

- No tique 20 houve 209 FLUSH_TLB de processo sem VMID e **zero** VMID invalidada por eles. Na 263b, eles somavam ~3,7 VMIDs cada.
- No total: 179 FLUSH_TLB de usuário com VMID invalidaram 306 VMIDs (1,7 por flush), contra 3,7 na 263b.
- A condição `vmids − paginação ≤ 16 × usuário_c/_VMID` vale em todos os intervalos.

## 2. Desfecho: D

- **Assinatura:** a trava veio ~10 s depois de o shell subir, antes da T3. Último tique do rastro: 90 (~10,4 s depois do StartDevice). Zero TDR, zero `GFXHUB_FAULT`, zero BugCheck; 1.164 IBs executados.
- As últimas linhas da UMD (DWM e Explorer) são de 21:41:15,471, todas com retorno: o `pfnPresentCb` do DWM entrou e saiu com `hr=0`.
- **Implicação (pelo critério):** a FT.1 não evita a trava da subida do desktop, que já aconteceu na 256b (~8 s), na 262 (~3 s) e na 263b (~3 s).
- **Fora do mapa (E):** não houve fault, TDR nem imagem corrompida reportada; nada indica invalidação faltando.

## 3. O que esta corrida acrescenta

- **A rajada de invalidação não estava presente na hora da trava.** Nos intervalos 50→90 (os ~5 s finais) a paginação ficou entre 19 e 125 FLUSH_TLB por intervalo, zero a 10 de processo sem VMID, e 33 a 169 VMIDs invalidadas. Na 263b a trava veio no meio da rajada (+214 sem VMID no último intervalo). Aqui veio com a máquina quase parada na paginação. Isso **enfraquece** "invalidação em excesso" como mecanismo (P2).
- O que segue presente nos intervalos finais, como na 263 e na 263b: leituras do SQ (27–46 por intervalo, o DWM desenhando), faixas de PTE no SDMA1 (21–115), troca de VMID 7 pela CPU, poll do MEC a cada 1 ms, e 7 invalidações pela CPU no último intervalo.
- A pista do processo novo (P2/H) fica mais fraca nesta corrida: os processos do shell começaram ~10 s antes da trava, não ~3 s.

## 4. Relato obrigatório

1. **Houve avanço?** Funcional, não: travou na subida, como a 263b. Diagnóstico, sim: a FT.1 está em efeito, medida, e a trava veio sem a rajada de invalidação.
2. **Sucessos:** FT.1 provada pelos contadores; desktop apresentando (150 composições, Present com `hr=0`); zero TDR/fault; nenhuma VMID ficou sem invalidação que gerasse fault.
3. **Falhas:** trava dura aos ~10 s do desktop; a T3 não rodou; Código 43 pelo fusível.
4. **Veredito:** desfecho D. A FT.1 fica por paridade com o amdgpu e por não ter causado fault.
5. **Recuperação e pendências:** BasicDisplay ativo; Código 43 pelo fusível. Antes do próximo bind: `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset` e desligar. **NÃO VALIDADO EM HARDWARE:** a causa da trava.

## 5. Próximo passo

Pelo teto do doc 697, a FT.1 não se repete sozinha e a próxima corrida muda outra variável. Candidatos do doc 694 §2.3, em ordem:
1. **`GfxWaveProbe=0`**: tira as leituras do SQ/CP pela CPU a cada submissão do GFX. É chave do INF de laboratório, sem mudança de código; as leituras do SQ estão presentes em todos os intervalos finais desta corrida.
2. **Poll do MEC sem o seletor GRBM** (escrita de `GRBM_GFX_INDEX`/`SRBM` a cada 1 ms pela CPU), que o amdgpu não faz.
