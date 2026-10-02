# 483 — Corrida 142 (DX.90): o batimento do GPU funcionou, a trava não reproduziu porque o harness rodou com o Explorer recém-nascido; critério da Corrida 143

> **Corrida:** 27/09/2026. Bind às 19:13:40 após boot frio; rollback online às 19:23:41 (`RECOVERY_OK_ONLINE`). **Candidato:** DX.90 / 3.296.1.0, **com** o harness.
> **Evidência:** `docs/evidence/2026-09-27_19-13-40-DESKTOP-HQAG6T4-lab-binding` (logs `-FINAL`); rastro a partir do offset 93.239.632 de `C:\BC250_PAGING_TRACE.bin`.
> **Relato do Daniel:** "pronto rodei". Não houve travamento.

## 1. Relato obrigatório
**1. Houve avanço?** Houve avanço de instrumentação e de diagnóstico, sem avanço funcional. A trava não foi exercitada.

**2. Sucessos (P0):**
- **BGH1 funcionou na 1ª corrida:** 10.114 registros, 5.045 DEPOIS medidos com `ReadMask=0x3F` (as seis leituras) e nenhum ANTES órfão. A instrumentação não regrediu nada.
- 600 s sem trava; paginação 95.741/95.741; 931 Presents no KMD, 5 `BYTE_PROOF PASS`; DWM 554 Presents `hr=0`.
- Harness 60/60 `PRESENT_OK`, exit 0.
- Zero TDR, fault, `InsideIb`, `IB_TRIPOD TIMEOUT` ou `DEVICE_REMOVED`.
- **Linha de base do anel gráfico, que não existia:**
  - ocioso em 5.034/5.045 medições;
  - a fence ficou atrás da última submetida em só 11 ticks, nunca dois seguidos;
  - ocioso = `GRBM_STATUS=0xA0003028` e `CP_STAT=0`;
  - ocupado (raro) = `CP_STAT≠0` e `GRBM_STATUS=0xE5F15028`.

**3. Falhas:**
- **Explorer 2924 caiu às 19:14:01**, 11 s depois de nascer, com `0xC0000005` no próprio `Explorer.EXE`. O log dele termina dentro da inicialização da ponte Vulkan de um segundo device D3D. A causa não foi estabelecida.
- SearchApp caiu 7 vezes (`0xC000027B`). Houve 2 LiveKernelEvent 193/804.
- O Explorer 7720, que substituiu o 2924, chegou a N=1.024 e teve 29 `vkAllocateMemory` com `-2`; 367 `NO_SPACE`.

**4. Veredito frente ao critério do doc 482: D.**
- Houve 600 s sem trava com o harness, **mas a condição das 139/140 não se formou**. O harness rodou de 19:14:09 a 19:14:43 sobre um Explorer com ~7 s de vida, com N entre ~300 e ~480; nas 139/140, N estava em ~780.
- O ensaio não discrimina A/B/C. Não conclui nada sobre a trava.

**5. Recuperação e pendências:**
- BasicDisplay Código 0 no mesmo boot; `CLEAN_BOOT_REQUIRED` armado.
- **NÃO VALIDADOS EM HARDWARE:** a causa da trava e a estabilidade do Explorer.

## 2. Dois erros meus no critério da 142, corrigidos antes de uso
1. **"Ocupado = bit 31 (GUI_ACTIVE) de `GRBM_STATUS`" estava errado.** O valor ocioso medido já tem o bit 31 ligado (`0xA0003028`). O discriminante passa a ser `CP_STAT≠0` ou `GRBM_STATUS≠0xA0003028`, junto com o progresso de RPTR e da fence.
2. **O decodificador comparava `CP_RB0_RPTR` e `WPTR` crus.** O anel tem 16.384 dwords e o RPTR do hardware dá a volta, enquanto o nosso WPTR é cursor monotônico: 0x88D4 contra 0x08D4 é anel **ocioso**. É a mesma armadilha da A15. Agora o decodificador reduz os dois módulo o tamanho do anel e publica `gfx_ring_pending_dw` e `gfx_fence_behind`.

## 3. O que muda para a 143
- **`-D3D11StartDelaySeconds`** no binder (padrão 0, sem mudança histórica), ligado por `BC250_D3D11_START_DELAY` no `EXECUTAR_D3D11_NATIVO.cmd`. Há uma recusa se sobrarem menos de 120 s de harness antes do watchdog.
- **Runner:** `tools\EXECUTAR_CORRIDA143_DX90_HARNESS_ATRASADO_600S.cmd`. Mesmo pacote 3.296.1.0 (mesmos hashes, preparo da 142), harness atrasado **150 s**.
- Na 141, com o Explorer vivo, N passou de ~720 entre 2 e 3 min. O Daniel usa o desktop desde o início.

## 4. Critério da Corrida 143 (escrito antes)
**Condição de validade:** o ensaio só conta se, ao iniciar o harness, o Explorer da vez estiver vivo e com N ≥ 700 no rastro. Se não, o desfecho é **D**.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Trava; último BGH1 é **ANTES** (`read_never_returned=true`) | MMIO parou de responder. Os DEPOIS anteriores dão o estado do anel. |
| **B** | Trava; últimos DEPOIS com `gfx_fence_behind>0` e `gfx_ring_pending_dw>0` por **2 ou mais ticks seguidos**, e `CP_STAT≠0` | A GPU travou no trabalho gráfico antes da máquina. Próximo: o IB e o estado do harness. |
| **C** | Trava; últimos DEPOIS ociosos (`pending=0`, `fence_behind=0`, `CP_STAT=0`) até o fim | GPU ociosa: o problema está do lado da CPU/SO. |
| **D** | Sem trava, ou condição de validade não cumprida | Não conclui. Com N ≥ 700 e sem trava: a sobreposição sozinha não basta. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

Controle a não regredir: desktop visível, DWM apresentando, paginação ENTER==LEAVE, zero TDR.
