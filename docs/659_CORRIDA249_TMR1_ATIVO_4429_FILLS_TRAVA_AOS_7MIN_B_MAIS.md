# 659 — Corrida 249: TMR.1 ativo, 4.429 FILLs e ~7 min antes da trava (desfecho B+)

**08/10/2026.**
- **Corrida:** bind às 14:42:30, trava às ~14:50:21 (última linha do monitor), boot seguinte às 14:51:09.
- **Rollback:** o watchdog fez o rollback depois do boot, às 14:52:31 (`RECOVERY_OK_ONLINE`, BasicDisplay em Código 0).
- **Pacote:** 3.394.1.0 (SYS assinado `AB2A2160…`) + RADV `B4AE2E14`.
- **Depurador:** KD ligado (`DEBUGPORT=NET`).
- **Evidência:** `docs/evidence/2026-10-08_14-42-30-DESKTOP-HQAG6T4-lab-binding/`. O log do KMD foi recuperado pela coleta automática (750 mil linhas) e o CSV do monitor foi copiado para `monitor-corrida.csv`.
- **Critério:** [doc 658](658_PLANO_POS_AUDITORIAS_CHECKLIST_E_CORRIDA249.md) §6.

## 1. Pré-condição (P0)

| Item | Medido |
|---|---|
| TMR.1 | `[TMR1] TMR persistente: VRAM [0xF400000,+0x400000) origem=alocado aqui MC=0xF40F400000 PA_DRAM=0x46F400000 (PA da BAR ... seria 0xCF400000); quad do CP persistente em [0xF8F0000,+0x110000)` |
| PSP | `SETUP_TMR fence=1 resp=0x00000000`, os 5 `LOAD_IP_FW` com resposta 0: **aceitos=6/6** com a PA da DRAM |
| Segmento do VidMm | `[0xA8C000, 0xF400000)`: o TMR e a quad ficaram **fora** (antes ia até 0xFA00000) |
| Base | G.1 armada, RLC rodando (`EXEC_ROM_CODE=0`, handshake ligado, 5/5), P3.3 desligada |
| Correções | zero `[SDMA_SPLIT] RECUSADO`; prova de dados do `COMPOSITE_BLT` só no começo |

**Diferença em relação ao desenho:** o TMR persistente do bootstrap não foi encontrado no alocador do PFP, e o TMR.1 alocou um novo, persistente, em 0xF400000. O efeito é o mesmo: memória fixa, fora do segmento.

## 2. Tempo e eventos (P0)

| Medida | 249 | Base com RLC rodando (244, 247, 248, 248b) | Maior valor antes |
|---|---|---|---|
| Tempo até a trava (QPC) | **461,9 s** (~417 s descontando os saltos ≥ 2 s; só 20 s em saltos > 3 s) | 40 / 108 / 8 / 22 s | 584 s (233, RLC na ROM) |
| FILLs até a trava (contador BPG1 do KMD, CSV do monitor) | **4.429** | ≤ ~722 (doc 653-TMR) | 2.890 |
| IBs executados | 37.029 (80/s) | 246b: 13.849 (43/s) | — |
| Presents | **10.807 (~23/s)** | 246b: 2.467 (7,6/s); 247: 958 (7,2/s) | — |
| `COMPOSITE_BLT OK` | 9.598 | — | — |
| TDR / fault / timeout | 0 / 0 / 0 | 0 | — |

O Daniel viu mais FPS, e os números confirmam: ~3× mais Presents por segundo que a 246b/247. Prováveis responsáveis: a CPU deixou de ler a VRAM em toda composição (VRD.1) e o log mínimo da UMD (H; as duas mudaram juntas).

**Fim da corrida:** a última linha do KMD tem uma submissão SDMA normal (`IB_ENGINE_2LEGS`, RPTR==WPTR). É a assinatura de sempre. No último minuto do CSV, FILL vai de 4.167 a 4.429, com IBs a 12–220/s e VRAM dedicada ~210 MB de ~233 MB.

## 3. Veredito

**Desfecho B+** do doc 658 §6: travou antes dos 600 s, mas com **6× os FILLs** da base que trava rápido e acima de qualquer corrida anterior.
- Pela régua de eventos, que a auditoria 653-TMR mostrou ser a que reduz a dispersão, é a corrida que mais longe chegou.
- Com n=1 não é prova: o TMR.1 e as correções podem ter mudado o risco por evento, ou esta corrida pode ser cauda.
- **A regra do doc 658 manda repetir (250) com o mesmo pacote.**

O TMR.1 sozinho **não elimina** a trava; isso está medido.

## 4. Lacunas desta corrida

- **Sonda DCN sem saída:** nenhuma linha `[DCN_*]` no log. O `bc250_dcn_probe.c` é compilado em `build-contract.bat:418` sem `/DBC250_ENABLE_DXGK_REGISTRATION=1`, e sem essa definição `Bc250DisplayLog` é o stub vazio de `bc250_display_ddi.h:272`. A sonda rodou e fez as leituras da Bateria A e do bloco AUX, mas tudo o que registrou foi descartado. Defeito da integração, sem custo de hardware para corrigir.
- **Temperatura (Tctl) no monitor:** sem amostra **por desenho**. Com `BC250_P1_SILENCIO_DA_BAR` a thread do rastro não lê registrador em regime (`bc250_mcdm_callbacks.c:981`), então não há registro BPS1. Ligar isso é variável nova (a P1).
- **Contador de PDEs no TMR:** não implementado nesta faixa; a pré-condição foi conferida pelo mapa DX.67 (o TMR ficou fora do segmento).

## 5. Próximo (proposta)

**250:** repetição. Para que seja comparável, só duas mudanças, as duas de registro:
1. compilar a sonda DCN com o log real, para os dados do DCN saírem;
2. (opcional) nada mais.

Critério igual ao do doc 658 §6, mais: FILLs acima de ~2.900 de novo = o ganho é real (P0 com n=2).

**NÃO VALIDADO EM HARDWARE:** a causa da trava; se o ganho de FILLs se repete; GFXRST.1 (sem TDR).

## 6. Corrida 250 pronta (repetição)

| Artefato | Valor |
|---|---|
| Pacote | **3.395.1.0** |
| SYS lab | `17F0B145…` |
| SYS assinado | `C987E574…` |
| INF | `B26CB47D…` |
| CAT | `FC4F14F4…` |
| UMD | `B73F9BDA…` (a mesma) |
| Pacote 3.394 (249) guardado em | `driver\wdk\package\output_lab_3.394.1.0_249\` |
| Lançador | `tools\EXECUTAR_CORRIDA250_REPETICAO_249_COM_KD_MENU_600S.cmd` |

**Única diferença em relação à 249:** a sonda DCN passa a ter o log real. O `build-contract.bat` compila `bc250_dcn_probe.c` duas vezes: sem `DXGK_REGISTRATION` para os testes offline e com ela (`bc250_dcn_probe_driver.obj`) para o `bc250mcdm.sys`. O comportamento no hardware é o mesmo; só o log muda.

**Critério, escrito antes:**
- O desfecho segue o doc 658 §6.
- O ganho conta como **real (n=2)** se os FILLs até a trava ou até o rollback passarem de ~2.900 de novo; se ficarem na faixa antiga (≤722), a 249 foi cauda.
- Pré-condição nova: linhas `[DCN_PROBE]`, `[DCN_BATTERY_A]` e `[DCN_BATTERY_B]` no log.

**Erro meu no caminho:** uma edição com `sed -i` no Git Bash converteu o `build-contract.bat` para LF, e o build falhou (`'^.obj'`, `'9009'`). O CRLF foi restaurado e a auditoria passou; nada foi instalado.
