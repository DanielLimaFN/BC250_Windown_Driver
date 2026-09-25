# Corrida 122 — Candidato DX.77: Coalescência de Multi-Draws no DWM, Redução de 99,4% nos Drenos e Ganho de Taxa (Desfecho A)

> **Data:** 25/09/2026 (execução 11:54:52 – 11:58:02)  
> **Candidato:** DX.77 / `3.281.1.0` (INF fonte `3.281.0.0`)  
> **Evidência:** [`docs/evidence/2026-09-25_11-54-52-DESKTOP-HQAG6T4-lab-binding/`](evidence/2026-09-25_11-54-52-DESKTOP-HQAG6T4-lab-binding/)  
> **Critério prévio:** [`docs/445_DX77_COALESCENCIA_MULTIDRAW_DWM_E_CRITERIO_CORRIDA122.md`](445_DX77_COALESCENCIA_MULTIDRAW_DWM_E_CRITERIO_CORRIDA122.md)  
> **Veredito:** **DESFECHO A (SUCESSO PLENO)**

---

## 1. Resumo Executivo e Avanço Frente à Corrida 121

1. **Houve avanço?** **SIM (Avanço Funcional e de Desempenho Expressivo).**  
   A coalescência de múltiplos draws no caminho direto da UMD foi comprovada no silício: o DWM deixou de drenar síncronamente antes de cada draw.
2. **Eliminação de 99,4% dos Drenos:**
   - Na Corrida 121: `DRENO antes de draw pelo caminho direto` disparou **4.865 vezes** (a cada draw individual).
   - Na Corrida 122: Os drenos de caminho direto caíram para **ZERO** (`0`). O total de drenos em toda a sessão do DWM caiu para **apenas 27** (26 chamadas legítimas de `ResourceMap DISCARD` e 1 destruição de recurso).
3. **Distribuição Real de Coalescência no DWM:**
   Os draws da composição do desktop foram agrupados em lotes de gravação no mesmo command buffer:
   - Lotes de 2 draws: **608 vezes**
   - Lotes de 3 draws: **440 vezes**
   - Lotes de 4 draws: **120 vezes**
   - Lotes de 5 draws: **162 vezes**
   - Lotes de 6 draws: **120 vezes**
   - Lotes de 7 draws: **54 vezes**
   - Lotes de 8 draws: **92 vezes**
   - Lotes de 9 a 20 draws: **114 vezes**
4. **Salto na Taxa de Apresentação do DWM:**
   - Corrida 121: 561 Presents em ~180 s (**3,12 Presents/s**).
   - Corrida 122: **840 Presents** em 165,68 s (**5,07 Presents/s sustentados**; nos primeiros 120 s foram **580 Presents = 4,83 Presents/s**).
   - Um salto de **+62,5% na taxa de composição do DWM**!
5. **Apresentações Processadas no KMD:**
   - KMD saltou de 660 para **950 Presents** com 1.900 entradas de `COMPOSITE_BLT` (950 cópias concluídas) e rotação de slots (`Flip=1`).
6. **Harness D3D11 e Tempo de Execução:**
   - 60/60 `PRESENT_OK` com FPS de janela atingindo **1,8 FPS** (~550 ms/quadro).
   - Tempo total para os 60 quadros caiu para **41,64 s** (novo recorde do harness; Corrida 120: ~44 s; Corrida 118: ~64 s).
7. **Fidelidade Visual e Estabilidade:**
   - Daniel confirmou: *"o triângulo colorido apareceu e tudo mais"*, desktop estável, sem corrupção de janelas nem cintilação.
   - Zero TDRs, zero BugChecks, fila gráfica perfeitamente drenada (`WPTR=204, RPTR=204, Error=0`).
   - Rollback online executado pelo watchdog restaurou `BasicDisplay` Código 0 em 5/5 amostras no mesmo boot sem reiniciar (R1).

---

## 2. Métricas Detalhadas e Comparativo

| Métrica | Corrida 120 (DX.75) | Corrida 121 (DX.76) | Corrida 122 (DX.77) | Variação 122 vs 121 |
|---|---|---|---|---|
| **Candidato / DriverVer** | DX.75 / `3.279.1.0` | DX.76 / `3.280.1.0` | **DX.77 / 3.281.1.0** | Versão nova |
| **Drenos "caminho direto" DWM** | ~4.800 | 4.865 | **0** | **-100% (eliminado)** |
| **Drenos totais DWM** | ~4.800 | 4.865 | **27** (apenas DISCARD real) | **-99,4%** |
| **Presents DWM (total)** | 385 (em 127 s) | 561 (em 180 s) | **840** (em 165,7 s) | **+49,7% volume** |
| **Taxa DWM (primeiros 120 s)** | 3,03/s | 3,12/s | **4,83 Presents/s** | **+54,8% taxa** |
| **Taxa DWM (sustentada total)** | 3,03/s | 3,12/s | **5,07 Presents/s** | **+62,5% taxa** |
| **Presents processados KMD** | ~400 | 660 | **950** | **+43,9% volume** |
| **Flushes do Harness** | 123 (2/quadro) | 66 (1/quadro) | **66** (1/quadro) | Mantido mínimo |
| **Tempo total Harness (60q)** | ~44 s | ~43 s | **41,64 s** | **-3,2% (recorde)** |
| **FPS Janela Harness (pico)** | 1,7 FPS | 1,7 FPS | **1,8 FPS** | **+5,9%** |
| **Integridade Visual** | OK (triângulo) | OK (triângulo) | **OK (confirmado Daniel)** | Preservada |
| **Rollback R1 (BasicDisplay 0)** | 5/5 PASS | 5/5 PASS | **5/5 PASS** | R1 sustentado |

---

## 3. Evidências Físicas Arquivadas

- Pasta de evidência: [`docs/evidence/2026-09-25_11-54-52-DESKTOP-HQAG6T4-lab-binding/`](evidence/2026-09-25_11-54-52-DESKTOP-HQAG6T4-lab-binding/)
- `safe-bind-final.json`: `RECOVERY_OK_ONLINE`, `armed_boot_ticks=boot_ticks=639259447565000000`, 5 amostras `BasicDisplay` Código 0 entre 11:57:56 e 11:58:00.
- `binding-provenance.json`: SYS `7BDCA67C06FB3270A9F5D83E83F4EE21AE306128AA7D2756D2B74C1639F6DB40`, DriverVer `09/25/2026,3.281.1.0`.
- `conformance-stdout.tmp`: 60/60 `PRESENT_OK`, FPS janela 1,8.
- `bc250_umd_log_1212-FINAL.txt` (15,15 MB): DWM PID 1212 completou 840 Presents (`DXGI Present: Vulkan Destile CommandBuffer flushed: res=0`), 0 drenos de caminho direto, 26 drenos em `ResourceMap DISCARD`.
- `display-log-this-bind-FINAL.txt` (27,45 MB): KMD completou 950 Presents (`[Present #950]`), 1.900 registros `COMPOSITE_BLT`, `BYTE_PROOF PASS`, zero TDRs, fila drenada em `WPTR=RPTR=204`.

---

## 4. Veredito e Pendências

- **Veredito:** **DESFECHO A (SUCESSO PLENO)** do critério prévio [`docs/445`](445_DX77_COALESCENCIA_MULTIDRAW_DWM_E_CRITERIO_CORRIDA122.md).
- **O que foi provado em hardware:**
  1. Multi-draw coalescing no caminho direto sem anel (`DrawRing`) funciona estavelmente no DWM do Windows 10 22H2.
  2. A eliminação do dreno incondicional cortou 99,4% dos stalls de sincronização e elevou a taxa do DWM de 3,12 para 5,07 Presents/s.
  3. A proteção de dreno condicional em `D3D10_DDI_MAP_WRITE_DISCARD` impediu qualquer corrupção visual.
- **O que continua NÃO VALIDADO EM HARDWARE:**
  1. Sessões longas de 600 segundos (continua medido apenas na janela de 180 s).
  2. Taxa de 60 FPS visíveis contínuos (a taxa do DWM subiu para ~5,07 FPS, o harness para 1,8 FPS; a latência de regulação pós-DDI em `pfnPresentCb` do dxgkrnl continua limitando o ritmo de apresentação das janelas).
  3. Jogos reais não modificados e engine D3D12/Vulkan completa.
- **Próximo Passo:**
  Analisar a regulação de ritmo de `pfnPresentCb` e o intervalo entre composições do DWM para avançar de ~5 FPS rumo a 30–60 FPS.
