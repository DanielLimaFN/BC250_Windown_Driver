# 702 — Corrida 266: MEC.2 em efeito, SDMA1 não subiu e trava dentro do 8º processo (desfecho B)

**Data/hora:** 12/10/2026, bind às 08:31:53; T3 iniciada às 08:32:15; trava por volta de 08:33:01,7 (~68 s depois do bind); máquina de volta às 08:33:28.
**Candidato:** lab 3.413.1.0. SYS assinado `89E1C6AD…`, conferido na pasta de evidência.
**Evidência:** `docs/evidence/2026-10-12_08-31-53-DESKTOP-HQAG6T4-lab-binding/`. Coleta automática concluída às 08:35:37 (exit 0); teste de RAM sem erro.
**Critério:** [doc 701](701_MEC2_POLL_DO_MEC_SEM_SELETOR_GRBM_PACOTE_3413_E_CRITERIO_CORRIDA266.md) §5.

---

## 1. Pré-condição

| Item | Medido |
|---|---|
| `[MEC2_POLL_SEM_SELETOR] chave=1`, `GfxWaveProbe=0`, `[FT1…] chave=1`, `[T2_PTE_MTYPE] 0/0 \| 0/1 \| 3` | presentes |
| Base | `[4C_HDP_FLUSH_BYPASS] ATIVO`, `[C1_GFX_DOORBELL] ATIVA` |
| **BMP1** | `polls_mec` = 5.228, **`polls_com_seletor` = 0** (0%), em todos os 52 registros |
| `leituras_sq` (BFT1) | 0 |
| FT.1 | em efeito: no tique 520, 14 flushes sem VMID e zero VMID somada por eles |
| `[MEC_PENDING]`, `FENCE_REGRESSION`, recusa do poll | 0, 0, 0 |
| Desktop | 784 `COMPOSITE_BLT OK`; Present do DWM com `hr=0` às 08:33:01,699 |

**A MEC.2 pegou:** nenhum poll escreveu o seletor GRBM.

**Correção de premissa minha (P0):** o poll do MEC roda **~85 vezes por segundo**, não 1.000. O critério esperava "~1.000 por segundo", porque eu tinha lido "timer de 1 ms" no código, e o docs 694/701 repetiram isso. O timer está vivo, o que era o propósito do item. Então a carga de MMIO removida pela MEC.2 era ~85 seletores/s, não ~1.000/s.

## 2. Variável a mais, não planejada: o SDMA1 não subiu

`[P4_SDMA1] BringUp=PROOF_TIMEOUT`: a prova do anel do SDMA1 não fechou em 20.000 polls (WPTR=64, RPTR=0, fence no poison). O driver falhou fechado: `NAO ATIVO -- SDMA1 parado, RAM devolvida`. As faixas de PTE rodaram todas no SDMA0, como antes da P4.2: `faixas_pte_sdma1` = 0, contra 15.862 na 265.

- **Histórico:** 2 em 38 binds com o SDMA1 tiveram esse PROOF_TIMEOUT (243 e 266). O caminho do SDMA1 é anterior ao poll do MEC e a MEC.2 não o toca (P2, por leitura de código).
- **Consequência:** a 266 difere da 265 em **duas** coisas, MEC.2 e SDMA1 ausente. Como a trava veio igual, isso não muda o veredito sobre a MEC.2. A falha intermitente da prova do SDMA1 é uma pendência separada.

## 3. Desfecho: B

**Assinatura:**
- A T3 completou 7 processos (código 0). O 8º (pid 3620) abriu às 08:33:00,8, criou o device, alocou 4 texturas de 8 MB e passou pelas esperas de paginação (`WaitPaging` fence 7010 com LEAVE). A última linha dele é às 08:33:01,693: `WaitPaging SKIP (already reached) 7004/7010`. O processo não chegou a terminar.
- O DWM apresentou com `hr=0` até 08:33:01,699.
- O rastro termina no tique 526. No último intervalo (510→520) houve 93 FLUSH_TLB da paginação, depois de três intervalos sem nenhum.
- Zero TDR, zero `GFXHUB_FAULT`, zero BugCheck; 3.657 IBs executados.
- Tctl entre 77,4 e 79,5 °C.

**Implicação (pelo critério):** o seletor GRBM do poll do MEC não é causa suficiente. A MEC.2 fica por paridade.

## 4. Comparação

| Corrida | Mudança | Desfecho | Processos até a trava | Tempo |
|---|---|---|---|---|
| 263 | T2 | B | 21 | — |
| 263b | +BFT1 | D | 0 | ~3 s do shell |
| 264 | +FT.1 | D | 0 | ~10 s do shell |
| 265 | +GfxWaveProbe=0 | C | 41 | ~273 s |
| **266** | +MEC.2 (e SDMA1 ausente) | **B** | **7** | **~68 s** |

A 265 não se sustentou como tendência: a 266 travou mais cedo. Isso é coerente com a dispersão conhecida (3 s a 600 s) e com a leitura do doc 700, que já não atribuía a melhora da 265 à mudança.

## 5. Relato obrigatório

1. **Houve avanço?** Funcional, não. De diagnóstico, sim: o seletor do MEC sai da lista, e a taxa real do poll do MEC (~85/s) foi medida.
2. **Sucessos:** MEC.2 provada (0 de 5.228 polls com seletor); FT.1 e sonda desligada em efeito; 7 processos novos completos; desktop apresentando; zero TDR/fault.
3. **Falhas:**
   - trava dura ~68 s depois do bind, dentro do 8º processo;
   - SDMA1 não subiu (PROOF_TIMEOUT);
   - Código 43 pelo fusível.
4. **Veredito:** desfecho B. A MEC.2 fica.
5. **Recuperação e pendências:** BasicDisplay ativo; Código 43 pelo fusível. Antes do próximo bind: `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset` e desligar. **NÃO VALIDADO EM HARDWARE:** a causa da trava e a causa do PROOF_TIMEOUT intermitente do SDMA1.

## 6. Balanço da série 264–266 e próximo passo

Três mudanças de paridade que reduzem trabalho da CPU contra a GPU saíram das causas suficientes:
- FT.1: invalidação de VMIDs só do processo;
- `GfxWaveProbe=0`: leituras do SQ/CP a cada submissão;
- MEC.2: seletor GRBM no poll do MEC.

Pelo critério, o próximo é o inventário do MMIO que sobra em regime (doc 694 §2):
- RPTR/WPTR do SDMA0 e do SDMA1 por MMIO;
- RPTR do IH por MMIO;
- contador de quadros do OTG a cada 4 ms;
- Tctl por SMN;
- instalação de VMID pela CPU.

Antes de outra corrida, uma tarefa sem custo de corrida: **medir a taxa real de cada um desses acessos**. Esta corrida mostrou que a taxa do MEC estava errada por um fator de ~12. A próxima mudança deve atacar o acesso mais frequente, com a taxa medida e não suposta.
