# DX.55: bissecção com lote desligado; critério da Corrida 100

> 24/09/2026. Faixa **offline**, sem bind.
> **Base:** [Corrida 99](400_CORRIDA99_DX54_RESULTADO_TRAVAMENTO_CEDO.md). Travamento duro em ~39 s com a DX.54 inteira, mas o pool persistente mediu descritores de ~14 ms → **32,5 µs** (P0).
> **Teto:** uma corrida, depois de desligamento completo.

## 1. Pergunta

Com a DX.53 e o pool persistente, o que trava cedo é o **lote de desenhos**?

## 2. A mudança (uma variável contra a 99)

**Mudou:**
- `s_Bc250Dx51DrawBatchEnabled = FALSE`: todo desenho volta ao caminho direto, drenando antes e com flush depois. O anel de 4 MB e o limiar continuam no código, mas não são alcançados.

**Fica igual à 99:**
- pool de descritores persistente + cache de samplers (`s_Bc250Dx54PersistentDescriptors = TRUE`);
- inspeção do Present amostrada;
- tempos `[DX54_TEMPO]`;
- KMD com DX.53 e DX.52;
- dirty rects desligados.

**Portão:** `audit_dx54_desktop_speed` agora exige o lote desligado. `test-dx54-speed-mutations.py` **9/9** (mutação nova: religar o lote reprova).

**Verificação:** `build-umd` EXIT=0 (descritores e lote PASS), auditoria de segurança PASS.

## 3. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.261.1.0`, SYS `F643EDC979EC0D59B338F2EC2B8F86D5C4E85345A656C960DFDF662E5617B7F9`, INF `2E5A977E…1852`, CAT `7125091B…15FA`. Auditoria do pacote PASS; `DESARME ANTES DA DEVOLUCAO` no SYS |
| UMD | `512880520EA9B12670702D4264C409F3D24025354EE12275016802CDBF53DD6E`; `POOL PERSISTENTE criado`, `[DX54_TEMPO]` e `NAO_AMOSTRADO` no binário |
| RADV | DX.42 `FCAA11FE…` |
| Pré-voo | `tools/prepare-corrida100-dx55.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA100_DX55.cmd`, elevado, **600 s**, depois de **desligamento completo** |

## 4. Critério, escrito antes

**Controle:**
- no dwm.exe:
  - `POOL PERSISTENTE criado`;
  - `BOUND` com `Pool=PERSISTENTE`;
  - **zero** `ANEL de dados por draw criado`;
  - todo flush com `DrawsNoLote` ≤ 1;
- no KMD: `[DX53_VMID] DESARME` presente.

**Lacuna conhecida:** se travar, os logs da UMD ficam em `C:\Windows\Temp`. Rode `tools/copiar-logs-umd-corrida99.ps1` elevado **trocando a pasta de destino**, ou o próximo bind os arquiva como residuais.

**Métricas contra a 98R (lote desligado, sem pool):**
- Present/min do dwm.exe (98R: 87–120);
- intervalo entre Presents p50 (98R: 391 ms);
- `DescUs` mediano (99: 32,5 µs);
- `SubmitUs` do harness (99: mediana 89 ms, máximo 1,06 s).

| | Assinatura | Implicação |
|---|---|---|
| **A** | **600 s sem travamento** (guardião reverte sozinho) **e** Present/min > 98R em ≥ 1,5× (≥ ~170/min) ou p50 ≤ 260 ms | O lote é o gatilho do travamento cedo, e o pool dá velocidade sozinho. Estabilidade além de 5 min medida. Próximo: por que o lote trava (IBs maiores com troca de contexto entre processos) antes de religá-lo. |
| **B** | 600 s sem travamento, **sem** ganho (< 1,5×) | O lote é o gatilho; o pool não basta para velocidade. O próximo custo sai de `[DX54_TEMPO]` (`SubmitUs`/`EsperaUs`). |
| **C** | Travamento | O lote não é o único gatilho. Suspeitos, com a DX.53 mantida: pool persistente, inspeção amostrada, ou instabilidade que a 98R não alcançou em 5 min. Ler o último passo nos logs da UMD. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Desfecho parcial não é desfecho:** A exige a janela inteira **e** o ganho.

**Secundárias:**
- carga do Explorer (~290 contextos/min);
- zero `QUARENTENA`;
- `SpaceResolveAddress status=9`;
- relato do operador (fluidez, piscar, mouse).

**Depois da corrida:** as 5 seções e a tabela-checklist.
