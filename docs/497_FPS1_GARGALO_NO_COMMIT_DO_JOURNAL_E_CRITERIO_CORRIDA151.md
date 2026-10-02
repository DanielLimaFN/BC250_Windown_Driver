# 497 — FPS.1: o gargalo de FPS era o commit do journal de GPUVA dentro do FLUSH_TLB; reconstrução ~95× mais rápida; critério da Corrida 151

> 28/09/2026, offline. Decisão do Daniel: atacar o desempenho antes da memória (doc 496). Medições sobre a Corrida 150 (`docs/evidence/2026-09-28_08-12-20-DESKTOP-HQAG6T4-lab-binding`).

## 1. Onde o tempo estava (P0, logs da 150)
| Medida | Valor |
|---|---|
| Harness | **1,64 FPS** (60 quadros em 36 s) |
| `pfnPresentCb` do harness | mediana **440 ms** por quadro, 75% do tempo do quadro |
| Correlação por QPC UMD↔KMD (60/60 quadros) | UMD→KMD 1,3 ms; **nosso `DdiPresent` 0,7 ms**; **KMD→UMD 437 ms** |
| Depois do Present | ~15 operações de paginação no Node 0, uma a cada ~20 ms (mediana), antes de o próximo Present sair |
| DDI de paginação | **95.631 FLUSH_TLB, mediana 3,0 ms, soma 490 s dos 600 s**; intervalo entre DDIs 0,12 ms |
| Estágios do FLUSH_TLB (rastro, média) | **commit do journal 7,5 ms**; invalidação do GFXHUB 0,15 ms |
| DWM | 3,2–3,5 FPS; trabalho Vulkan próprio pequeno (1,7 s de espera e 0,3 s de envio em 36 s) |

**Conclusão (P0):** o FPS baixo não vinha da GPU, do dxgkrnl nem do timer. A **thread de paginação passava 82% do tempo dentro do nosso FLUSH_TLB**, e o custo era o commit do journal de GPUVA. Present e composição esperam essa fila de paginação.

## 2. Causa no código (P1)
`Bc250GpuvaSpaceReplaceRangeTransactional` reconstrói o espaço inteiro **por mudança do lote**:
- zera um scratch de ~120 KB;
- reinsere **cada uma das N faixas** pela `InsertRange`, que verifica sobreposição contra **os 1.024 slots** e procura a posição ordenada **do início**;
- copia ~120 KB de volta **byte a byte por ponteiro `volatile`**.

Com N≈1.000 isso dá ~1 M comparações e ~240 KB de acessos serializados **por mudança**, e o lote chega a 129 mudanças.

## 3. O que mudou (FPS.1), sem mudar o resultado
1. **`Bc250GpuvaSpaceStoreRange`**: fonte única do preenchimento do slot e da inserção ordenada, usada pela `InsertRange` e pela reconstrução. A posição é procurada **de trás para frente**: com a ordenação invariante, dá o mesmo índice, e é O(1) para entrada em ordem.
2. **`Bc250GpuvaSpaceAppendTrusted`**: reinserção das faixas **já válidas** e dos fragmentos delas.
   - Pula a checagem de sobreposição contra 1.024 slots, que ali não pode reprovar.
   - Mantém as guardas O(1).
   - Usa o próximo slot livre (e cai no caminho completo se não estiver livre).
   - **A faixa nova (`Param`) continua pela `InsertRange` completa.**
3. **Cópias de ~120 KB** (espaço e snapshot) por `__movsq` (REP MOVSQ intrínseco). `memcpy` não linka nas suítes offline, que é a armadilha já registrada, e o tamanho múltiplo de 8 é conferido em compilação.

## 4. Provas offline
- **Benchmark** (mesma CPU, modo usuário, N=1.000, 2.000 substituições; fonte antiga do `HEAD` contra a nova): **1.770,2 µs → 18,7 µs por substituição (~95×)**, com 2.000/2.000 ok nas duas.
- **Teste de propriedade novo** (`Bc250RunGpuvaRebuildPropertyTest`): 700 substituições aleatórias em três donos, conferindo depois de cada uma, contra um modelo de referência:
  - ordenação;
  - `LiveCount` igual ao modelo;
  - offset exato nas duas bordas de cada intervalo.

  **Três mutações, três reprovações:** inserção sempre no fim, sufixo sem ajuste de offset, reinserção descartando a faixa.
- **Portão `audit_fps1_gpuva_rebuild_is_linear`**, com **três mutações e três reprovações**: cópia do espaço volta a `volatile`, faixa nova pela reinserção confiável, cópia do snapshot volta a `volatile`.
- `build-contract` EXIT 0 (suítes GPUVA de espaço, snapshot, journal e broker PASS), build de laboratório EXIT 0, auditorias PASS.

| Artefato (3.301.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `20089E9578A87ED1FAE9317B59545B90BE5DF402BC3897AA79675DB0C6521869` |
| `bc250mcdm.sys` fonte lab | `17B3A2D5DE95DE14392802AA4E6FF058D196EF4EA79B98D21FBE24544584856F` |
| `bc250umd.dll` (igual à 149/150) | `FAE9F89EB95B33C2A4E2A39D1688609EA4A3FF802B43492C72D45964A3406911` |
| `bc250mcdm.inf` | `E9CF0E3453140A5586B9ACEB56FDDB662FB295E3454EB4E750FFD0CDA7412E66` |
| `bc250mcdm.cat` | `02B23CB767D0E6E73805EFB5C85BFF0312F074BDE01CC26659711ABD31FAC0CA` |

## 5. Critério da Corrida 151 (escrito antes)
**Runner:** `tools\EXECUTAR_CORRIDA151_FPS1_600S.cmd`, sem KD, harness atrasado 150 s. Roteiro da 148–150. Boot frio antes. **Linha de base:** a Corrida 150.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Soma do FLUSH_TLB **< 245 s** (metade de 490 s) **e** harness **≥ 3,3 FPS** (o dobro de 1,64), 60/60; zero TDR/fault; desktop correto | O commit era o gargalo; FPS melhora. Próximo: o que sobra no quadro, pela mesma correlação QPC. |
| **B** | FLUSH_TLB cai para menos da metade, mas o FPS do harness não dobra | O gargalo seguinte é outro (retire por timer, DWM, envio gráfico); medir de novo. |
| **C** | FLUSH_TLB não cai | A reconstrução não era o custo real no kernel, ou o N/lote mudou; rever com o rastro. |
| **D** | Regressão: corrupção visual, falha do harness, trava ou TDR | Reverter a FPS.1 e isolar. O teste de propriedade diz que a semântica é a mesma, mas só o hardware confirma. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

**NADA DA FPS.1 FOI VALIDADO EM HARDWARE.**
