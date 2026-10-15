# 586 — Corrida 214: a LOG.1 funcionou; a AGP trava o SDMA0 na primeira leitura do anel

**Data:** 04/10/2026, bind às 21:22:17 · **Candidato:** lab **3.362.1.0** (D2 + LOG.1) · **Critério:** [doc 585](585_CORRIDA213_REPETIDA_D2_TIMEOUT_NA_RAM_E_NO_ROLLBACK_LOG_PERDIDO_E_CRITERIO_CORRIDA214.md) §5
**Evidência:** `docs/evidence/2026-10-04_21-22-17-DESKTOP-HQAG6T4-lab-binding/display-log-this-bind-LOG1.txt` (5.775 bytes, SHA-256 `07b665ce…`).

## 1. Medido (P0, log do KMD)

**LOG.1 provada:** 61 linhas do DriverEntry ao RemoveDevice, com o StartDevice tendo falhado. Antes, uma falha assim deixava zero linhas.

**Até a D2, tudo normal:**
- PSP carregou o firmware do SDMA0;
- tripé de boot provado (`TRIPE_PROVADO`, WPTR=RPTR=68, fence sobre poison, 4/4 dados);
- CG.1 aplicada;
- MMU global programada com a AGP **ligada**: `AGP_BOT=0x0000F800 AGP_TOP=0x0000FFFF SYS_APERTURE=[0x003D0000,0x003FFFFF]`, readback aceito.

**D2:**
- RAM em PA `0x453B1F000` (abaixo da VRAM em `0x460000000`) → MC `0xFC53B1F000`.

| Tentativa | Anel | Resultado do NOP de prova |
|---|---|---|
| 1 | RAM pela AGP | `SYNC_NOP TIMEOUT: WPTR=40 RPTR=4 FenceObs=0x43DAFFFE STATUS=0x44DC5042` |
| 2 (rollback) | VRAM `0xF40FF00000` | `SYNC_NOP TIMEOUT: WPTR=40 RPTR=0 FenceObs=0x43DAFFFE STATUS=0x44DC6452` |

- Resultado `0xC0000483`, `RECUSADO fail-closed: D2 sem rollback provado`, Código 43, fusível CLEAN, sem tela azul.

## 2. Leitura

- **Na RAM pela AGP, o SDMA0 avançou só 4 bytes (1 dword) dos 40 e parou** (P0). Uma leitura pela AGP não completou; o restante do anel nunca chegou.
- **Depois disso, nem o anel na VRAM, que funcionou segundos antes, anda** (RPTR=0, P0). O soft reset do SDMA0 (`GRBM_SOFT_RESET.SDMA0`) não desfaz o estado deixado pelo acesso pela AGP.
  - A requisição presa provavelmente fica fora do SDMA0 (UTCL2/GCVM L2 ou a malha). Isso é **H**.
- **Desfecho A do doc 585:** RPTR parado logo no começo, então nem a leitura do anel completa pela AGP. A AGP sai da mesa neste chip. Isso casa com o Linux, que não a liga no BC-250 (P0, coleta L-B).

## 3. Relato (5 pontos)

1. **Avanço?** Sim, de diagnóstico: o log de StartDevice com falha deixou de se perder, e a AGP fica refutada como caminho com prova no silício. Funcional: não.
2. **Sucessos:** LOG.1; tripé de boot, MMU com AGP e readback aceitos; o caminho de falha da D2 fechou como desenhado (Código 43, sem tela azul, RAM retida de propósito).
3. **Falhas:**
   - o NOP de prova pela AGP parou após 1 dword;
   - o rollback na VRAM também falhou, porque o motor ficou preso.
4. **Veredito:** D2-por-AGP **falhou** (desfecho A). A hipótese H-BAR **não foi testada**.
5. **Recuperação e pendências:** BasicDisplay após o rollback do watchdog; **boot frio obrigatório**, porque o SDMA0 ficou preso. NÃO VALIDADO EM HARDWARE: anel na RAM por qualquer caminho, a H-BAR, e 600 s sem trava com 256 MB.

## 4. Próximo passo (decisão do Daniel)

1. **D2 por GART, como o Linux:** tabela de página da VMID 0 (contexto 0) na VRAM, faixa `[0, 512 MB)` como na coleta L-B, PTEs de sistema para a RAM do anel, invalidação da VMID 0.
   - É o caminho que funciona no mesmo chip (P0).
   - É maior que a AGP: contexto 0, tabela, invalidação, e a mesma prova e rollback.
   - Pode deixar o motor preso do mesmo jeito se a tradução falhar. O rollback não protege contra isso, como a 214 mostrou.
2. **Parar a D2** e ir para a D1 (conclusão por interrupção), que não mexe no caminho de memória do anel.
3. **Até lá:** a chave `BC250_D2_SDMA_RING_IN_SYSTEM_MEMORY` deve ir a 0 em qualquer candidato que não seja o ensaio da D2. A LOG.1 fica.
