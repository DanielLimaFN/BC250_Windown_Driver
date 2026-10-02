# 565 — Corrida 204: 4b válido, travou ao abrir a Pesquisa — tirar a leitura da CPU não basta

**Data:** 02/10/2026, bind às 11:44:04 · **Candidato:** lab **3.351.1.0** (UMD `6654865D…`) · **CMOS:** 256 MB · **Boot:** frio
**Evidência:** `docs/evidence/2026-10-02_11-44-04-DESKTOP-HQAG6T4-lab-binding/`
- Cauda do KMD e rastro com SHA-256.
- Coletor rodado: `umd-logs-pos-travamento/`, `reset-reason-pos-trava.txt`.

**Critério:** [doc 564](564_4B_CPU_NAO_LE_A_VRAM_PELA_BAR_E_CRITERIO_CORRIDA204.md) §3.

---

## 1. Validade do ensaio (P0)

**4b aplicada:** 19 `INSPECAO LINEAR_BUF DESLIGADA (4b)` e 1.259 `NonZeroGrade=DESLIGADA_4B`.

**Grades na janela 11:44–11:47: zero.** Os logs contêm 36 linhas `INSPECAO LINEAR_BUF:` e 36 `GRADE`, mas todas são **resíduo** de corridas anteriores:
- datas de 09:47 a 09:51;
- arquivos de AppContainer reaproveitados pelo mesmo nome (StartMenu 7016, SearchApp 7248).

Com o filtro pelo horário, sobram zero.

**Armadilha registrada:** contar linhas no arquivo inteiro teria dado o desfecho C (inválido). O coletor e a análise precisam filtrar pela janela do bind.

**KMD:**
- 1.381 `FILL_POR_COPIA`, todos com `Confere=1`;
- zero divergência;
- CG.1 aplicada.

**Carga comparável:** 125 `res=-2` no SearchApp durante a corrida. O `dwm.exe` 1276 fechou o adaptador às 11:46:19 e um novo `dwm.exe` (6160) subiu às 11:46:24.

## 2. O que aconteceu (P0)

- Trava dura ao abrir a Pesquisa, aos ~134 s de log do KMD.
- Nenhum TDR, `IB_TRIPOD TIMEOUT` ou `GFXHUB_FAULT`.
- Tctl entre 77 e 84 °C.
- **Rastro, última amostra (235,44 s, ~150 ms antes do fim):** SDMA0 ocioso (`RPTR=WPTR`, fora de IB); GFX ocioso.
- **Final do log do KMD:**
  1. `DX93_ALLOC_MAP` de uma alocação nova de 3 MB (segmento 1, `CpuVisible=0`);
  2. `FILL_POR_COPIA` em dois passos;
  3. a caminhada do submit passou (`PT_RANGE DEST PASS_PONTAS`, 622 páginas);
  4. **`KMD_SDMA_SUBMIT` do segundo passo (2,5 MB)** é a última linha. Não há `IB EXECUTADO` depois.
- **A máquina parou com o SDMA escrevendo pela primeira vez numa alocação recém-mapeada.** É o mesmo final das 14 travas da série 166–172 (doc 526).

## 3. Veredito pelo critério escrito antes

**Desfecho B:** ensaio válido, trava, e o fim do log do KMD não está num `[PT_WALK]` (está no submit, depois da caminhada).

- **A leitura da VRAM pela CPU em concorrência com a GPU não é causa suficiente** (P0). As três travas anteriores coincidiam com ela, mas a trava aconteceu sem ela.
- **O que se repete em todas as travas medidas** (199–204, e 166–172): uma **alocação nova**. As PTEs dela foram escritas pela CPU pela BAR, e a GPU a acessa logo em seguida, por FILL, cópia, draw ou leitura.
- **A diferença P1 contra o Linux que ainda está de pé** (doc 563 §3): o Linux escreve as PTEs de VM gráfica **pelo SDMA**.

**Implicação, pela decisão do Daniel:** seguir para a **4c**, PTE pelo SDMA como o Linux.

## 4. Estado

- Placa em **Código 43** (fusível). Recuperação e boot frio antes de novo bind.
- 4b e X2b ficam no código (sem regressão medida). A decisão de mantê-los fica para depois da 4c.

**NÃO VALIDADO EM HARDWARE:** desktop estável com 256 MB.
