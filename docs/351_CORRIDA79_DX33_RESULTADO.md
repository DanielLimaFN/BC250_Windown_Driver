# Corrida 79 — DX.33 (pulo de ModeChange, VMID do pipe): resultado

> **23/09/2026, 08:40:28–08:42:31.** Candidato lab **3.240.1.0**, SYS `BC6F4FCE...` (conferido em
> `binding-provenance.json`), ICD `5988B38E...`.
> Pasta: `docs/evidence/2026-09-23_08-40-28-DESKTOP-HQAG6T4-lab-binding/`.
> - O log `FINAL` ficou incompleto (`complete_at_capture=false`); a cauda foi resgatada em
>   `bc250_display_log-tail-recovered.txt` (3.123.761 bytes, SHA-256 `4021850B...`).
> - A análise usa `display-log-this-bind.txt` + cauda (27.559 linhas), contíguos pelo offset.
>
> Critério prévio: [docs/350](350_DX33_CRITERIO_CORRIDA79_MODECHANGE_E_VMID_DO_PIPE.md).
> **Veredito:** display **G** (fora do mapa), TDR **T3**, sem F.

## 0. Violação de protocolo (P0) — ler antes de tudo

**A 79 rodou sem boot frio depois da 78.**
- `LastBootUpTime` é 08:16:30, que é o reboot **quente** pedido pelo watchdog da 78.
- O `boot_ticks` do watchdog da 79 (`639257589905000000`) é igual ao do fim da 78.
- Por isso o rollback automático da 79 foi **BLOQUEADO** ("rollback já foi tentado neste boot"). O rollback que restaurou
  o BasicDisplay às 08:42:30 foi o do próprio binder.

**Por que o binder deixou passar** (defeito de host, meu, por não ter visto antes):
- `watch-bc250mcdm-safe-bind.ps1`, no caminho `RECOVERY_OK_ONLINE`, **não grava** `C:\BC250_REQUIRE_CLEAN_BOOT.json`.
- O marcador da 78 ficou com o `boot_ticks` do boot anterior ao reboot do watchdog.
- No boot seguinte, o binder viu ticks diferentes, concluiu "boot posterior confirmado" e apagou o marcador.
- Além disso, o guarda não distingue reboot quente de boot frio.

Os resultados abaixo valem como medição, com a ressalva de estado de engine possivelmente herdado da 78.

## 1. Houve avanço?

**Diagnóstico sim; funcional não.** Tela preta, com o cursor aparecendo travado às vezes (relato do Daniel). O harness
teve **0/60** (falhou no quadro 1), contra 12/60 na 78.

## 2. Sucessos medidos

- **V1 atuou como projetado (P0 amostral):**
  - 20 `MODECHANGE_BLANK_SKIP`; zero `SET_ADDRESS` copiando primária vazia.
  - Em 19 deles o scanout já estava 0/256, sem nada a preservar.
  - Em 1 (#18) o scanout tinha 256/256 (hash `00B1BE79`) e foi **preservado**: `Present #7` e `#8` ainda amostraram
    256/256 com o mesmo hash (SCANOUT-PRESERVADO).
- **V2 da DX.32 atuou pela primeira vez:** `PRIMARY_COPY #17 REDUNDANT_SKIP` (MPO3, origem `0x114C000`, 256/256 pares
  conferem), sem dano visível nas amostras seguintes.
- **Engine 0:** 64 transições com `Eng=0`.
- **Autoteste:**
  - `nonggc` e `nongg` 5/5;
  - `nodcc`: S1 e S2 PASS, S3–S5 NÃO MEDIDO.
- **Controles:**
  - zero BugCheck;
  - zero `PREAMBLE_CNTL`;
  - `TDR_RESTART PROVADO` 3/3;
  - 1 `BYTE_PROOF PASS`.

## 3. Falhas

- **3 TDRs de 240/240 ondas, todos pós-troca de VMID:**

  | TDR | IB travado | VMID do IB travado | VMID do anterior |
  |---|---|---|---|
  | 1 | `0x2400E0000` | 1 | 3 |
  | 2 | `0x240030000` | 2 | 3 |
  | 3 | `0x2420E8000` | 2 | 1 |

  O ME está em `EVENT_WRITE` nos três.
- **`PipeVmid=0` e `RB0_VMID=0` nos três**, sem bits espúrios. Zero não é nem o VMID do IB travado nem o do anterior. É
  o VMID do **anel** (o CP_RB0 roda com `CP_RB_VMID=0` por desenho). Leitura (H): `CP_ME0_PIPE0_VMID` reflete o VMID
  do anel, não o do IB. `COMPUTE_VMID` repetiu `0xCFBB8BBC`.
- **O novo apagador do scanout é o próprio Present Blt:**
  - `Present #8` com `SubRects=1` (retângulo do tamanho de cursor, por exemplo `(723,163)-(739,182)` no #9, com
    `CenterPixel` não-nulo);
  - a superfície de origem inteira estava 0/256, e o KMD copiou **11.059.200 bytes** dela para o scanout
    ("Blt acionado por sub-retângulo parcial não-nulo");
  - `Present #9` já amostra o scanout 0/256.
  - O código (`bc250_display_ddi.c`, bloco "Execucao do Blt") ignora `pDstSubRects` e sempre copia a superfície inteira.

  Isso explica a tela preta **e** o cursor aparecendo sozinho: só o cursor tem pixels na origem.
- A superfície do DWM está zerada fora do cursor. Na 77 os flips do DWM tinham desktop.
  - H: os DWMs são derrubados pelos TDRs (device removed) e recriam superfícies vazias.
  - Não provado.

## 4. Veredito frente ao critério (docs/350)

- **Display: G.**
  - A não ocorreu: não houve imagem persistente.
  - B não se sustenta: o scanout não-nulo durou só de `Present #6` a `#8`.
  - C não é literal: um skip preservou 256/256.
  - O observado fora do mapa é o Present Blt de superfície inteira apagando o scanout a partir de um retângulo de cursor.
- **TDR: T3.** `PipeVmid` coerente em forma, mas igual a nenhum dos dois VMIDs. Pela implicação escrita antes: parar de
  buscar o VMID por MMIO no TDR; o próximo passo é um marcador escrito pela própria GPU no IB.
- **F:** não ocorreu.

## 5. Recuperação e pendências

- O rollback do watchdog foi BLOQUEADO; o do binder restaurou o BasicDisplay (Código 0 às 08:42:30). Leitura posterior:
  serviço `BasicDisplay`, ProblemCode 0, serviço `bc250mcdm` ausente.
- `C:\BC250_REQUIRE_CLEAN_BOOT.json` está armado com o boot atual. **Desligamento completo antes de qualquer bind.**
- **NÃO VALIDADO EM HARDWARE:** eliminação dos TDRs, desktop, 60 quadros, VMID real do dispatch.

**Próximos passos propostos (não implementados):**
1. **Host:** o watchdog deve gravar o marcador de boot frio também em `RECOVERY_OK_ONLINE`/`ROLLED_BACK`, com os ticks
   do boot corrente. Isso é correção de defeito, não ensaio.
2. **Display:** o Present Blt deve honrar `pDstSubRects` (contrato do `DXGKARG_PRESENT`) e copiar só os retângulos
   sujos, em vez da superfície inteira. Uma variável comportamental.
3. **TDR (a prioridade real, porque derruba harness e DWMs):** marcador escrito pela GPU no próprio IB. Por exemplo, um
   `WRITE_DATA` de `CP_ME0_PIPE0_VMID`/constante antes do dispatch e um `COPY_DATA` de registrador para memória, lido no
   TDR. Ler o upstream (`gfx_v10_0.c`, `CONTEXT_CONTROL` e a emissão de troca de VM) antes de escolher.
