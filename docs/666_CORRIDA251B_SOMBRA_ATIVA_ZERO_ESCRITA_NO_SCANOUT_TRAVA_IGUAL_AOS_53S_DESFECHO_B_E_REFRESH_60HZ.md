# 666 — Corrida 251b: sombra ativa, zero escrita no scanout, trava igual aos ~53 s (desfecho B), e refresh real de 60,5 Hz

**Data:** 08/10/2026, bind às 22:47:32. **Pacote:** lab 3.397.1.0 (SYS `E93667CD…`), sem KD, roteiro só o menu às cegas.
**Evidência:** `docs/evidence/2026-10-08_22-47-32-DESKTOP-HQAG6T4-lab-binding/`, mais o log contínuo do KMD desde o offset 756.340.450 (47.217 linhas até o reboot) e `C:\BC250_MONITOR.csv` (bloco `# inicio 2026-10-08 22:47:25`).

## 1. Pré-condição (P0): cumprida

| Item | Resultado |
|---|---|
| `[SOMBRA1] ATIVO` | sombra VRAM `[0xE970000,+0xA90000)`, MC `0xF40E970000`; scanout MC `0xF400000000`; allocator `total=0x10000000` |
| Destino dos blts | **330** com `DstMc=0xF40E970000` (a sombra); **zero** com `DstMc=0xF400000000` (o scanout) |
| Present | 515 `[Present #]`, 15 com `SDMA=YES` na amostragem do log |
| `[CG_FOTO] DESLIGADA` | sim |
| `[REFRESH_REAL]` | sim (§3) |
| StartDevice | `LEAVE status=0x00000000`; desktop no ar, com a tela congelada como previsto |

## 2. Resultado (P0)

| Medida | 251b | 250 | 249 |
|---|---|---|---|
| Tempo até a trava (QPC, sem KD) | **~53 s** | ~213 s reais | ~417 s reais |
| FILLs até a trava | **412** | 1.754 | 4.429 |
| IBs executados | 2.581 | 13.210 | 37.029 |
| `COMPOSITE_BLT OK` | 327 | 2.799 | 9.598 |
| TDR / fault / timeout | 0 / 0 / 0 | 0 | 0 |

- **Fim:** a última linha é um IB normal do SDMA0 (`IB EXECUTADO`, RPTR==WPTR, `FromDpc=1`). Assinatura de sempre.
- **Monitor:** rajada inicial (221 IB/s, 42 Presents/s, 231→396 FILLs nos primeiros ~20 s); depois quase parado (0–2 IB/s); 412 FILLs na última amostra, às 22:48:31.
- **Boot seguinte:** o fusível disparou e o StartDevice foi recusado (`0xC0000182`). Placa em **Código 43**, watchdog em `OBSERVING`, sem rollback.

## 3. Refresh real (P0, achado lateral)

`[REFRESH_REAL] quadros 10348 -> 10394 (46) em 760095 us -> 60.518 Hz medidos`.
- Com ±1 quadro de resolução em 0,76 s, o intervalo é 59,2–61,8 Hz.
- A L1 no Linux mediu 60,1 Hz em 615 s.
- **O refresh real é ~60 Hz, não os 56,13 Hz** que o EDID sintetizado declara. O nosso pixel clock derivado (173,6 MHz) está errado. Com 2784 × 1111, o pixel clock real é ~185,6 MHz.
- **Impacto:** o período de VSync que o WDDM recebe de nós está errado em ~7%. Não tem relação com a trava, mas é defeito real do VidPn/EDID. Pendência.

## 4. Veredito

**Desfecho B** do doc 664 §5: trava dura com a assinatura de sempre e a pré-condição OK. **A escrita do SDMA0 na região que o DCN herdado varre não é o mecanismo da trava** (P0 para a forma testada: zero blt no scanout e trava igual).

Somando a L1 (doc 662: o DCN herdado não trava o Linux), **o DCN sai da investigação da trava.**

Ressalva: a trava veio com só 412 FILLs, o menor valor desde o TMR.1. Isso cabe na dispersão conhecida (8–584 s) e não é atribuível à sombra; a sombra tira ~11 MB do segmento, o que aperta a memória.

**Houve avanço?** No diagnóstico, sim: mais uma hipótese fechada com prova, e o refresh real medido. Funcionalmente, não.

## 5. Próximo (proposta)

Pelo critério escrito antes, a próxima frente forte é a **P5: tabela de páginas de 4 níveis como o `amdgpu`** (hoje 2 níveis, 34 bits; o `amdgpu` usa 4 níveis, 48 bits). É a maior diferença estrutural restante no caminho onde a trava acontece (FILL de zeramento, PTE, TLB). Trabalho offline grande antes de qualquer corrida.

**Recuperação antes de qualquer bind:**
1. `tools\BC250_RECOVERY.cmd` elevado;
2. `tools\inspect-boot-fuse.ps1 -Reset`;
3. boot frio.

A SOMBRA.1 volta a 0 no próximo pacote, porque não tem valor de produto.

**NÃO VALIDADO EM HARDWARE:** a causa da trava dura; o EDID com 60 Hz.
