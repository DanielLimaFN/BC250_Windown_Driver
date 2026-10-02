# 476 — Corrida 137 (DX.88): terceira janela de 600 s, log íntegro, Explorer caindo com uso real e a recusa da textura compartilhada localizada

> **Corrida:** 26/09/2026, bind 22:50:03 após boot frio, rollback online 23:00:18 no mesmo boot. **Candidato:** DX.88 / 3.293.1.0. Boot `NODEBUG`.
> **Evidência:** `docs/evidence/2026-09-26_22-50-03-DESKTOP-HQAG6T4-lab-binding` (log do KMD `-FINAL` completo) e ETW `docs/evidence/2026-09-26_22-49-57-DX83-HANG-ETW` (fechado, `STOPPED`).
> **Relato do Daniel:** "desktop visível durante todo o teste" (ele usou o desktop durante a janela).

## Relato obrigatório
**1. Houve avanço?** Sim. Terceira janela de 600 s seguida sem trava com boot frio (134, 136, 137), log do KMD confiável, e a etapa da recusa da textura compartilhada estreitada.

**2. Sucessos (P0, exceto o visual, que é relato):**
- 600 s sem trava; `RECOVERY_OK_ONLINE` no mesmo boot; desktop visível durante todo o teste.
- **Log íntegro (DX.88 validada):** 294.648 linhas `BC250_HANG`, **zero malformadas** e zero marcadores no meio de linha (136: uma linha corrompida).
- DWM (pid 1184) de 22:50:13 a 22:59:56: **2.426 Presents `hr=0`** (136: 1.544). KMD: 3.301 Presents, 3 `BYTE_PROOF PASS` no scanout.
- `FLUSH_TLB`: 147.324 chamadas, p50 1,55 ms, p90 2,24 ms; wrapper 147.324/147.324.
- Harness D3D11 60/60. Zero TDR_QUIESCE, IB_TRIPOD TIMEOUT, GFXHUB_FAULT, `DEVICE_REMOVED`, `InsideIb`, `Blt Timeout`, `COMPOSITE_BLT FALHOU`.

**3. Falhas:**
- **Explorer caiu 6 vezes** (22:50:22, 22:52:13, 22:53:06, 22:54:07, 22:57:35, 22:59:55), todas `0xC0000602` em `KERNELBASE+0x12d862`; SearchApp 31; 7 LiveKernelEvent. Com uso real do desktop o defeito volta; a 136 sem quedas foi circunstancial.
- 376 `NO_SPACE` de GPUVA.

**4. Veredito frente ao critério do doc 475: A.** 600 s sem trava, desktop visível e zero linhas corrompidas. O desfecho B não ocorreu: o ETW não trouxe um evento de erro nomeando a recusa. Trouxe, porém, a localização abaixo, que ficou fora das assinaturas previstas.

**5. Recuperação e pendências:** BasicDisplay Código 0 no mesmo boot, `CLEAN_BOOT_REQUIRED` armado. **NÃO VALIDADOS EM HARDWARE:** Explorer estável, recurso compartilhado, jogos, 60 FPS.

## A recusa da textura compartilhada (P0 pelo ETW + UMD)
- Caso limpo: Explorer pid 804, `Allocate LEAVE hr=0x80070057` às 22:59:55,168 e queda no mesmo segundo (~596,4 s no ETL).
- Com `Resource` (0x40) ligado, o ETW circular de 128 MB só cobre os últimos ~45 s; esse caso está dentro. Em todo o trecho coberto, **nenhum `AdapterAllocation` de 3.407.872 bytes enquanto o nosso driver estava ativo.** Os 34 que aparecem (609–617 s) são de depois do rollback, criados sob o BasicDisplay e com sucesso.
- Com o log da UMD (`Seen=1`, `KmdStatus=0`, `hAlloc=1`), isso posiciona a recusa **entre o retorno do nosso `DxgkDdiCreateAllocation` e a criação do objeto de alocação do VidMm**: o dxgkrnl rejeita algo que o nosso KMD devolve para a alocação compartilhada.
- Comparação indireta: a textura compartilhada sob o BasicDisplay aparece com `Flags=0x20000005`, e as nossas alocações comuns com `Flags=0x41`. Esse campo é a representação interna do VidMm e **não** foi decodificado; não se atribui causa a ele.
- O que o nosso KMD devolve nessa alocação (P1 pelo código): `SupportedRead/WriteSegmentSet = AllocationSegmentSet` (só o segmento local), `PreferredSegment = 1`, `EvictionSegmentSet = 0`, `AllocationPriority = NORMAL`, `FlagsWddm2.CpuVisible = 1` (a UMD marca `CPU_VISIBLE` para toda classe `RESOURCE`), `hResource` de saída nulo.
- Hipóteses (**H**) para o próximo ensaio, uma variável de cada vez: (a) `CpuVisible` em alocação compartilhada de segmento local; (b) conjunto de segmentos sem o segmento de memória do sistema para uma alocação que precisa poder ir a outro processo/dispositivo; (c) falta de dados privados no nível do recurso.
- Próximo passo offline: registrar fora da amostragem o `DXGK_ALLOCATIONINFO` completo que o KMD devolve para alocações com `Flags.Resource=1` maiores que 1 MB, e testar a hipótese (a) primeiro, porque é a de menor alcance.
