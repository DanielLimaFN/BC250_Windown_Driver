# 526 — Passagem de bastão: a trava da partida, faixas HDP.1 a X4, Corridas 166 a 172

> Escrito em 29/09/2026 para a próxima IA que seguir este projeto.
>
> **Leia antes, nesta ordem:**
> 1. [`CLAUDE.md`](../CLAUDE.md), inteiro: Leis 1–10 e a diretriz de relato de corridas.
> 2. [`PLANO_DE_TRABALHO_TRAVA_E_DESKTOP.md`](../PLANO_DE_TRABALHO_TRAVA_E_DESKTOP.md): regras de operação (§1) e procedimentos (§3). Ele foi escrito **antes** desta série; onde este documento o contradiz, **vale este**.
> 3. Este documento.
> 4. Os docs de cada faixa: [519](519_HDP1_FLUSH_REAL_E_UTCL1_INV0_CRITERIO_CORRIDA166.md), [520](520_T31_ALIASING_WC_UC_DA_VRAM_CRITERIO_CORRIDA167.md), [521](521_EVT1_ANEL_DE_EVENTOS_DA_GPU_E_T6_CRITERIO_CORRIDA168.md), [522](522_X1_ESPERA_DO_SDMA_SEM_HDP_CRITERIO_CORRIDA169.md), [523](523_X2A_FILL_DE_PAGINACAO_EM_256K_E_MIGALHAS_DE_VRAM_CRITERIO_CORRIDA170.md), [524](524_X3_CPU_NAO_LE_VRAM_COM_SDMA_OCUPADO_CRITERIO_CORRIDA171.md) e [525](525_X4_SEM_CANARIOS_EM_EXECUCAO_CRITERIO_CORRIDA172.md). Cada um tem a mudança, as provas offline, os hashes, o critério escrito antes e o resultado da corrida.
>
> **Nada desta série resolveu a trava. Nada aqui foi validado em hardware como correção.** O que está P0 está marcado.

---

## 1. Resumo em poucas linhas

- **O problema:** a máquina inteira congela nos primeiros ~1–4 min de desktop sob o nosso driver (tela preta, sem BugCheck, sem WHEA, VRAM perdendo conteúdo durante a trava). As 7 corridas desta série (166–172) travaram, e o plano registrava 13 travas em 15 corridas antes delas.
- **O achado mais forte (P0, instrumento novo):** nas 5 corridas com o anel de eventos da GPU (168–172), a máquina parou **com um FILL de paginação recém-submetido ao SDMA e sem conclusão**. Somando as 9 anteriores pelo log de texto, são **14 de 14 travas** terminando assim.
  - O FILL é o zeramento que o VidMm pede para cada alocação nova. A partida e a abertura do menu Iniciar são as fases de mais alocações novas, e a Corrida 171 travou exatamente no clique do menu.
- **O acesso da CPU no instante da trava varia e é colateral:** leitura de volta da invalidação do HDP, leitura dos canários pela BAR, leitura do registrador RPTR. É o que a CPU que sondava o SDMA estava fazendo.
- **Seis hipóteses foram testadas e nenhuma basta:** flush do HDP, aliasing WC/UC, C-states da CPU, HDP nas esperas, tamanho do pacote de FILL e canários. Todas as correções ficaram no código, porque são defeitos reais ou reduções de risco sem regressão medida.
- **Próximo passo recomendado:** X2b, zerar por **cópia** em vez de `CONST_FILL` (§8). É o teste que separa "o pacote CONST_FILL" de "o destino recém-mapeado".

---

## 2. Estado atual

### 2.1 Pacote pronto e último instalado

**3.317.1.0 (X4)**, em `driver/wdk/package/output_lab/`. Auditoria do pacote PASS.

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `E29882ED846F9AE25529C3EC133AC3AFC7DE4A227DEE9979EF862EFB327664C6` |
| `bc250umd.dll` (**idêntica desde a 3.310**; UMD-fonte `8623E38A…`) | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `A0751F6C487BF91A2028F16044B4C48BE51791A3E43134CD15A890EA63ADF309` |
| `bc250mcdm.cat` | `61071BC93FA0AD2B8611F8EF232EFD759F71608A96292CCD7F6FEA9E1CB63C9A` |

- **DriverVer fonte** (`driver/wdk/package/bc250mcdm.inf`): `3.317.0.0`. **O próximo candidato precisa subir para `3.318.0.0`** antes de empacotar.
- **Perfil do empacotamento (sempre o mesmo nesta série):**
  ```
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -ArmGfxWaveProbe -IAcceptSelectorWritesDuringTdr
  ```
- **A UMD não foi recompilada em nenhuma faixa desta série**, para manter uma variável. Não recompile sem motivo: o binário em `driver/umd/output/` é o da 3.310.

### 2.2 Máquina (29/09, ~10:05)

- BC-250 em **Código 43** (`CM_PROB_FAILED_POST_START`), com o `bc250mcdm` 3.317.1.0 preso e o **fusível TRIPPED**. É o estado normal depois de uma trava.
- **Antes de qualquer bind** (o Daniel executa, elevado): `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset`, e boot frio com o monitor ligado antes da placa.
- **T6 revertida:** `IDLEDISABLE` AC = 0, conferido na 169.
- Boot sem `DEBUG` (`SystemStartOptions: TESTSIGNING NOEXECUTE=OPTIN NODEBUG NOVGA`).

### 2.3 Git

**Nada foi commitado nesta série** (regra: só com pedido do Daniel). Branch `p7-desktop-estabilidade`.

Arquivos desta série:
- **Novos:**
  - `driver/core/bc250_hdp_contract.h` e `driver/core/bc250_vram_cache_policy.h`;
  - `tools/analyze-bc250-freeze-run.py` e `tools/mutacoes/mutate_*.py` (7 scripts);
  - `tools/prepare-corrida166…172-*.ps1` e `tools/EXECUTAR_CORRIDA166…172_*_600S.cmd`;
  - `docs/519`–`526`;
  - `referencias/linux_upstream_2026-09-29/` (com `PROVENIENCIA.md`; confira se `referencias/` entra no git).
- **Modificados:**
  - `driver/core/`: `bc250_blt_proof_contract.{c,h}`, `bc250_compute_mqd_contract.h`, `bc250_gfx_ring_transaction.{c,h}`;
  - `driver/tests/`: `blt_proof_contract_test.c`, `mcdm_callbacks_test.c`;
  - `driver/wdk/`: `bc250_active_miniport.c`, `bc250_compute_service.c`, `bc250_display_ddi.{c,h}`, `bc250_mcdm_callbacks.c`, `bc250_mmio_breadcrumb.h`, `bc250_mmio_topology_transport.c`, `bc250_pfp_direct_load.c`, `bc250_sdma_present.{c,h}`, `bc250_sdma_ring_work.c`, `bc250_submission_engine.c`, `bc250_vram_probe_transaction.c`, `package/bc250mcdm.inf`;
  - `tools/audit-bc250mcdm-safety.py`;
  - `docs/03_CATALOGO_DE_REGISTRADORES.md`, `BASE_DE_AVANCO.md`, `PLANO_DE_TRABALHO_TRAVA_E_DESKTOP.md`.

O repositório já tinha muitas modificações não commitadas de sessões anteriores (UMD, contratos, testes). Não misture os dois conjuntos num commit sem separar.

---

## 3. Linha do tempo: faixas e corridas desta série

| Corrida | Data/bind | Candidato | Variável testada | Duração até a trava | Veredito | Doc |
|---|---|---|---|---|---|---|
| 166 | 28/09 22:54:51 | 3.311.1.0 **HDP.1** | Flush do HDP real (`0x385C`) no lugar da escrita em `0x4A80` | 49,6 s | B: não basta | 519 |
| 167 | 29/09 08:08:57 | 3.312.1.0 **T3.1** | Janela WC da VRAM sem aliasing com mapeamentos UC | 61,8 s | D pela letra (PFP do boot sobrepôs, transitório); na prática "não basta" | 520 |
| 168 | 29/09 08:36:19 | 3.313.1.0 **EVT.1** + T6 | C-states desligados (`IDLEDISABLE=1`); anel de eventos ligado | 54,9 s | B1: FILL em voo (P0); T6 não basta | 521 |
| 169 | 29/09 08:54:56 | 3.314.1.0 **X1** | Espera do SDMA só pelo registrador RPTR, sem HDP e sem ler VRAM | **227,0 s** (recorde; primeira depois de 200 s) | B1: FILL em voo | 522 |
| 170 | 29/09 09:19:46 | 3.315.1.0 **X2a** | FILL de paginação fatiado em 256 KiB (era 4 MiB) | 61,1 s | B2: migalha `VRAM_CANARY_READ` ANTES (P2, ver §6.3) | 523 |
| 171 | 29/09 09:47:14 | 3.316.1.0 **X3** | Canários só lidos com o SDMA ocioso | 53,8 s, **ao abrir o menu** | Portão nunca agiu (premissa errada); `VRAM_CANARY_READ` ANTES de novo | 524 |
| 172 | 29/09 10:01:27 | 3.317.1.0 **X4** | Sem canários em execução | 51,9 s | B2: sem acesso à VRAM pendente; FILL de 64 KB em voo | 525 |

Evidência de cada corrida: `docs/evidence/<data>-DESKTOP-HQAG6T4-lab-binding/display-log-this-bind-RECUPERADO.txt` (recuperado do log vivo, com SHA-256 no doc da faixa). Na 167 também há `paging-trace-this-bind.bin` e `umd-logs-pos-travamento/`.

---

## 4. O que está provado e o que foi refutado

### 4.1 P0 (medido)

1. **Na trava há um FILL de paginação no SDMA sem conclusão.** São 5 de 5 com anel EVT.1 (168–172) e 9 de 9 pelo log de texto antes do anel. O FILL pode ser pequeno: 64 KB na 172, que executa em microssegundos.
2. **O GFX estava ocioso ou concluído** no momento da parada (168: 35 ms; 171: 59 ms; 172: 3 ms).
3. **Nenhuma regressão funcional nas 7 faixas:** tripé, Presents e `BYTE_PROOF PASS` presentes; zero `IB_TRIPOD TIMEOUT`, `GFXHUB_FAULT`, TDR e `DEVICE_REMOVED` em todas as corridas.
4. **`UTCL1_INV0` (SDMA0, `0x4A80`) vale `0x800` depois do POST:** só `INVREQ_IDLE`; as habilitações de invalidação já vêm em 0.
5. **Base NBIO seg2 `0xD20` confirmada:** `RCC_CONFIG_MEMSIZE` (`0x378C`) = `0x200` (512 MiB), coerente com `FB_LOCATION 0xF400..0xF41F`.
6. **O flush real do HDP (`0x385C=0` + leitura de `0x378C`) roda sem falha e sem travar sozinho.**
7. **Mapa da VRAM vista pela CPU** (166/167):
   - segmento do VidMm `[0xA8C000, 0xFA00000)`, cortado na página de migalhas;
   - faixa privada do driver `[0xFA00000, 0x10000000)`: migalhas `0xFA00000`, anel do SDMA `0xFF00000`, anel gráfico `0xFF70000`, MEC `0xFF90000`, firmware `0xFFBD000`, dummy `0xFFFF000`.
8. **A 169 durou 227 s**, a primeira trava depois de 200 s no corpus. É n=1.

### 4.2 Refutado ou "não basta" (n=1 cada, pela regra do plano §3.5)

| Hipótese | Onde caiu |
|---|---|
| Faltava o flush de escrita do HDP nas tabelas de página (T1) | 166 |
| Zerar `UTCL1_INV0` desligava a invalidação da UTCL1 do SDMA | 166 (o valor do POST já é 0 nas habilitações) |
| Aliasing WC/UC permanente da VRAM (T3.1) | 167 |
| Transição de energia da CPU / C-states (T6) | 168 |
| HDP e leitura de VRAM nas esperas do SDMA | 169 |
| Tamanho do pacote de FILL (rajada de 4 MiB) | 170 |
| Leitura de canários concorrente com o SDMA trabalhando | 171 (o SDMA estava ocioso) |
| Leitura de canários como causa | 172 (sem canários, travou igual) |

### 4.3 Hipóteses vivas (H)

- **H1, pacote:** o `CONST_FILL` neste chip, sob alguma condição, leva o fabric ou o controlador de memória a parar. A TRANSFER (cópia) nunca foi o último trabalho de uma trava, embora seja ~40% dos movimentos.
- **H2, destino:** o FILL costuma ser a **primeira escrita da GPU numa alocação nova**, sob PTEs recém-escritas pela CPU (UpdatePageTable, depois flush do HDP, depois FLUSH_TLB). PTE errada leva a uma escrita de DMA para um endereço físico ruim, e isso é trava clássica de plataforma. Os destinos físicos amostrados na 172 estavam todos na VRAM (`0x45…`), mas são só 6 amostras.
- **H3, sondagem por MMIO:** o Linux quase não lê registradores do SDMA durante a execução (RPTR e fence em memória do sistema). Nós lemos RPTR por MMIO no portão do X1. É menos provável, porque a trava existia antes do X1 com outro padrão de acesso.

---

## 5. O que cada faixa mudou no código (tudo continua ativo)

| Faixa | Mudança | Arquivos / funções | Gate (`tools/audit-bc250mcdm-safety.py`) |
|---|---|---|---|
| **HDP.1** | `0x4A80` é `SDMA0_UTCL1_INV0`, não flush de HDP. Ninguém o escreve mais. O flush real é `0x385C=0` + leitura de `0x378C`, com `_mm_mfence()` antes, em todo sítio de coerência, **ao fim de todo UpdatePageTable** e antes do laço de invalidação do FLUSH_TLB. Nomes falsos de `0x3FC4` corrigidos. O gate DX.14 (que exigia o erro) foi reescrito. | `bc250_hdp_contract.h` (fonte única); `Bc250SdmaPresentHdp`, `Bc250SdmaHdp`, `Bc250ActiveHdpWriteFlush`, publicação do MEC, `Bc250ComputeHdpFlush`; allowlist do transporte | `audit_hdp1_write_flush_is_real` |
| **T3.1** | Janela WC do framebuffer em `[0, 0xFA00000)`, antes 256 MB. Sem fallback UC. A sonda de VRAM mapeia só as páginas testadas. Todo UC permanente declara `[T31_UC_MAP]`. | `bc250_vram_cache_policy.h`; `Bc250DisplayInit`; `bc250_vram_probe_transaction.c`; `Bc250LogUcVramMapping` | `audit_t31_vram_no_wc_uc_alias` |
| **EVT.1** | O anel de 48 eventos da GPU (desenhado na DX.96, **nunca ligado**) passou a ter escritores e leitor. | `Bc250CrumbEvent` em FILL/TRANSFER/DISCARD, SUBMIT/RETIRE de SDMA e GFX; `Bc250CrumbReportEvents` | `audit_evt1_gpu_event_ring_is_wired` |
| **X1** | Aposentadoria assíncrona e blts do Present decidem a conclusão **só pelo registrador RPTR** (`Bc250TripodRingEntryConsumed`, função pura com teste offline). HDP e leitura da fence só depois, com migalha `VRAM_FENCE_READ`. | `Bc250SdmaPresentPollRetirement`, `BltCore`, `BltRectCore`, `Bc250SdmaSyncEntryConsumed`, `Bc250SdmaReadFenceAfterConsumed` | `audit_x1_no_hdp_or_vram_read_while_sdma_busy` |
| **X2a** | FILL de paginação fatiado em 256 KiB (`BC250_SDMA_PAGING_FILL_MAX_BYTES`). Migalhas em cópias para o anel e em canários. | `Bc250McdmBuildFillVirtual`; `mcdm_callbacks_test` reescrito (multipasse) | `audit_x2a_paging_fill_sliced_and_vram_crumbed` |
| **X3** | Canários só com o anel ocioso. Migalhas em tabela de página, IB e provas do escape. | `PollRetirement`; `Bc250ActiveReadVramU64` e outros | `audit_x3_no_canary_vram_read_while_sdma_busy` |
| **X4** | **Sem canários em execução:** prova de duas pernas declarada `NAO CONFERIDA`. | `Bc250X4DisableRuntimeCanaries` em `SubmitIndirectAsync` e `SubmitFillAsync` | `audit_x4_no_runtime_canaries` |

**Dívidas conhecidas e deixadas de propósito:**
- o PFP direct load (StartDevice) mapeia UC `[0xF8F0000, +0x110000)` dentro da janela WC, de forma transitória, antes do segmento;
- o PFP não recebe flush de escrita real antes de o CP ler o firmware;
- `bc250_compute_service.c` (serviço legado) ainda alterna `HDP_MISC`;
- `dbg/hw.c` (driver de laboratório separado) ainda escreve `0x4A80`;
- `FORCE_INVALIDATION` da UTCL1 segue ligado em todo FLUSH_TLB (T3.3);
- o portão do X3 continua no código, mas ficou sem efeito depois do X4.

---

## 6. Instrumentação disponível e como ler

### 6.1 Anel de eventos da GPU (EVT.1)

- **Onde fica:** na página de migalhas (VRAM `0xFA00000`), dwords 432–1007, 48 eventos de 12 dwords, com check XOR.
- **Quando é lido:** no StartDevice **seguinte** à trava, pelo leitor, que emite:
  - `[EVT1_GPU] anel de eventos …: validos=N degradados=M` (degradado = check não fecha, VRAM decaída);
  - `[EVT1_GPU] seq=… TIPO no=… op=… bytes=… addr=… fence=… cpu=… antes_do_ultimo=X ms`, do mais novo ao mais antigo;
  - `[EVT1_EM_VOO] no=0|1: último submit … -> CONCLUIDO | SEM CONCLUSAO REGISTRADA`.
- **Tipos:** `PAGING_FILL` (op = dwords úteis do IB), `PAGING_TRANSFER`, `PAGING_DISCARD`, `SUBMIT_SDMA` (bytes do IB e fence do dxgkrnl), `RETIRE_SDMA`, `SUBMIT_GFX` e `RETIRE_GFX` (fence do anel).
- **Casar FILL com submit:** o `SUBMIT_SDMA` de um FILL tem `bytes = arredondar_para_8(op) × 4`. Exemplos: op 5 → 32, op 0x14 → 96, op 0x2D → 192.
- **Reset rápido** (≤10 s) preserva mais eventos: na 169, 31 de 48 degradaram.

### 6.2 Migalhas de MMIO/VRAM (DX.66)

96 slots (32 CPUs × camadas INTERNA/PAGINACAO/SUBMISSAO). Sítios (`bc250_mmio_breadcrumb.h`):

| Código | Nome | Detalhe |
|---|---|---|
| 1/2 | TRANSPORT_READ/WRITE | offset BAR5 |
| 3/4 | SDMA_REG_READ/WRITE | offset na janela `0x3000` (ex.: `0xFC4` = `0x3FC4`, `0x1B8C` = RB_RPTR, `0x85C` = flush do HDP) |
| 8/9 | VRAM_SAMPLE_LOOP / VRAM_COMPARE | amostragem e prova do Present |
| 10/11 | PAGING_DDI / SUBMIT_DDI | escopo de DDI (op 0xC = FLUSH_TLB, 0xB = UPDATE_PAGE_TABLE) |
| 12 | VRAM_FENCE_READ | X1 |
| 13/14 | VRAM_CANARY_READ/WRITE | X2a (sem uso em execução depois do X4) |
| 15 | VRAM_RING_WRITE | X2a, todas as cópias para o anel |
| 16 | VRAM_PT_READ | X3, `detalhe` = página de VRAM |
| 17 | VRAM_IB_READ | X3 |
| 18 | VRAM_PROOF_READ | X3 |

### 6.3 **Armadilha grave das migalhas (achada na 172)**

`BC250_CRUMB_PHASE_BEFORE = 0xB0` e `BC250_CRUMB_PHASE_AFTER = 0xA0` diferem **num único bit** (bit 4). A VRAM que decai na trava vira bits **0→1**, então **um DEPOIS degradado aparece como ANTES**.
- Isso explica os `PAGING_DDI … fase=ANTES` com `seq` antiga que aparecem em quase toda corrida.
- **Rebaixa para P2** o "a CPU parou dentro da leitura dos canários" das Corridas 170 e 171. Era a última migalha da máquina nas duas, então decaimento no mesmo bit das duas vezes é improvável, mas possível.
- **Correção pendente (instrumentação, barata):** fases com distância de Hamming grande. Por exemplo, BEFORE `0x5AC3F00F` e AFTER `0xA53C0FF0`, e qualquer outro valor como INVÁLIDO no leitor.

### 6.4 Linhas de log de cada faixa

| Linha | Faixa | O que diz |
|---|---|---|
| `[HDP1_INIT]` | HDP.1 | `UTCL1_INV0` e `RCC_CONFIG_MEMSIZE` no boot |
| `[HDP1_FLUSH] #n` / `DIVERGENCIA` | HDP.1 | flush real e leitura de confirmação |
| `[HDP_PT_FLUSH] #n OK em FLUSH_TLB/UPDATE_PAGE_TABLE` / `FALHA` | HDP.1 | flush depois de escrever tabelas |
| `[T31_CACHE_MAPA]`, `[T31_UC_MAP] … privada=0/1` | T3.1 | mapa de tipos de cache (`privada=0` = sobreposição) |
| `[X1_POLL]` | X1 | leituras de fence só depois do RPTR; sondagens sem HDP |
| `[X3_CANARY]` | X3 | canário adiado |
| `[X4_CANARY]` | X4 | canários desligados em execução |
| `IB_ENGINE_2LEGS` | X4 | IB com prova de duas pernas |

### 6.5 Ferramentas novas

- **`python tools/analyze-bc250-freeze-run.py [pasta]`:** recupera o log da corrida pelo offset do baseline (grava `display-log-this-bind-RECUPERADO.txt` com SHA-256), mede a duração pelo QPC, conta as assinaturas e imprime migalhas (sem as decaídas óbvias) e o anel EVT.1. **É o primeiro passo da análise de toda corrida.**
- **`python tools/mutacoes/mutate_<faixa>.py`:** aplica cada mutação num arquivo, roda a auditoria, confere que ela **reprova** e restaura o arquivo. Todas as 7 foram rodadas sobre o código atual em 29/09 e todas as mutações reprovam. Se aparecer `NAO APLICADA`, o código mudou e a mutação precisa ser adaptada.
- **`referencias/linux_upstream_2026-09-29/`:** cópias do Linux usadas para procedência (ver `PROVENIENCIA.md`).

---

## 7. Procedimento que funcionou (siga igual)

1. Uma variável de comportamento por candidato. Instrumentação pode ir junto.
2. Código, depois gate no `audit-bc250mcdm-safety.py` (registrar no `main`), depois script de mutação em `tools/mutacoes/`.
   - **Use o Write, nunca heredoc do Bash, para arquivos com `\n` ou `\`**: o heredoc corrompe.
   - **Um gate que procura só o nome de uma função deixa passar `if (0u && …)`**. Exija a condição inteira (lição da X1e).
3. Subir o DriverVer fonte, depois:
   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
   ```
   Rode também os executáveis de teste em `driver\wdk\output\` (`mcdm_callbacks_test`, `blt_proof_contract_test`, `paging_contract_test`, `sdma_packet_builder_test`, `active_miniport_test`) e confira EXIT 0.
4. Empacotar (§2.1), `tools\audit-bc250mcdm-lab-package.ps1` (**PASS obrigatório**) e conferir strings novas no SYS assinado.
5. Criar `prepare-corridaNNN-*.ps1` e `EXECUTAR_CORRIDANNN_*_600S.cmd` a partir do anterior, trocando os 3 hashes (SYS, INF, CAT), o DriverVer do manifesto e os textos. **O `.cmd` precisa estar em CRLF.** Rodar o preparo em leitura (EXIT 0).
6. Doc da faixa com o critério escrito antes (Lei 10: assinatura separada da implicação, desfecho E, parcial não conta).
7. BASE: uma linha no topo com link.
8. Passar ao Daniel: `BC250_RECOVERY.cmd`, `inspect-boot-fuse.ps1 -Reset`, boot frio, runner. Roteiro: não abrir nada; ~20 s depois do desktop, abrir e fechar o menu Iniciar algumas vezes; reset em até 10 s.
9. Depois da corrida: `tools/analyze-bc250-freeze-run.py`, os eventos do System (`Id=6005,6008,41`), julgar contra o critério e relatar com os 5 pontos da diretriz e a tabela ✅🟡❌🔜.

---

## 8. Próximo passo recomendado: X2b (zerar por cópia)

**Objetivo:** separar H1 (o pacote `CONST_FILL`) de H2 (o destino recém-mapeado).

**Desenho proposto** (não implementado):
1. Em `Bc250McdmBuildFillVirtual`, quando `FillPattern == 0`, reservar no fim do paging DMA buffer uma **região de zeros** alinhada (por exemplo 32 KiB, escrita pela CPU com `RtlZeroMemory`).
   - A GPUVA dela é `Args->DmaBufferGpuVirtualAddress + (deslocamento da região em relação a Args->pDmaBuffer na entrada)`. O campo está no contrato em `d3dkmddi.h:5197` (`DXGKARG_BUILDPAGINGBUFFER.DmaBufferGpuVirtualAddress`).
   - **Confirmar a semântica antes de usar:** o campo aponta para o início do buffer ou para a posição atual de `pDmaBuffer`? Validar no silício com o `IB_PROOF_BIND` (o PA que a caminhada resolve para o IB contra o PA medido pela CPU com `MmGetPhysicalAddress`).
2. Emitir `COPY_LINEAR` (já existe `Bc250SdmaEmitCopyLinear`), da região de zeros para cada fatia do destino. Isso é o mesmo pacote e o mesmo caminho (sistema para VRAM) da TRANSFER, que nunca foi o último trabalho de uma trava.
3. Com padrão ≠ 0, manter `CONST_FILL`. Todos os FILLs observados até aqui têm `Pattern=0`.
4. O paging buffer (`PagingBufferSize=0x10000`) comporta região + pacotes. Senão, usar o multipasse legal que já existe.
5. Log e evento: `op=FILL_POR_COPIA` distinguível no `[DX91_PAGING_MOVE]` e no EVT.1.

**Critério sugerido:**
- **A:** 600 s sem trava, duas vezes seguidas → o `CONST_FILL` era o gatilho.
- **B:** trava com o anel mostrando a **cópia de zeros** em voo → é o destino (H2). A próxima frente é validar as PTEs do destino antes do submit, pela vista de software (journal), sem ler VRAM.
- **B':** trava com outro trabalho em voo.
- **E:** fora do mapa.

**Junto, só instrumentação:**
- fases das migalhas com distância de Hamming grande (§6.3);
- o **endereço físico** do destino de cada FILL no EVT.1, que o submit já resolve (`destPhysStart`), com um tipo novo de evento.

---

## 9. Pendências, em ordem sugerida

1. **X2b** (§8).
2. **Se a trava persistir com a cópia (H2):** validar as PTEs do destino do FILL pela vista de software, e comparar com o Linux (`amdgpu_vm.c`: ordem entre atualização de tabela, flush e TLB, antes do primeiro uso).
3. **Modelo do Linux para conclusão** (plano T2 item 4): fence e writeback do RPTR do SDMA em **memória do sistema** (abertura AGP ou do sistema do GCMC), CPU sondando RAM e **zero acesso à BAR durante a execução**. Mais trabalho, com procedência a levantar (doc 491 e M1).
4. **T3.2:** semáforo do engine de invalidação 17 (patch gfx1013 do Linux para o 13FE).
5. **T7 / DX.98:** tabelas de página escritas pelo SDMA. O bloqueio da Lei 7 (`navi10_sdma_pkt_open.h`) pode ser resolvido baixando o arquivo.
6. Dívidas do §5: aliasing do PFP no boot, flush do PFP, `HDP_MISC` no serviço legado, `dbg/hw.c`, `FORCE_INVALIDATION`.
7. **Desktop, depois da trava:** menu Iniciar visível (T9), shaders recusados (T10), desempenho (T11/T12).

---

## 10. Armadilhas e lições desta série

- **Nome no código não é procedência.** `0x4A80` era chamado de flush do HDP em 5 arquivos, com um gate exigindo a escrita, e era `SDMA0_UTCL1_INV0`. Faça sempre a conta **reversa** do offset (`0x4A80/4 − 0x1260 = 0x40`) contra `inc/linux_ref/gc_10_1_0_offset.h`. Registradores de NBIO e HDP não estão em `inc/linux_ref`: use `referencias/linux_upstream_2026-09-29/`.
- **O gate que congela o erro** apareceu de novo (DX.14). Quando uma correção reprova um gate antigo, leia o gate antes de "consertar" o código.
- **Último acesso da CPU ≠ causa.** Três faixas (X1, X3, X4) atacaram o acesso que aparecia por último. Ele mudava, e a trava ficava. O que se repete é o **trabalho da GPU** (FILL). Olhe primeiro o anel EVT.1.
- **Fases de migalha com 1 bit de distância** (§6.3).
- **Premissa de concorrência sem medir** (X3): o portão nunca agiu. Antes de propor "enquanto X está ocupado", confira no log se X estava ocupado.
- **Contagem acumulada:** eu escrevi "13 de 13" na 172 e eram 14. Recontar a partir da lista.
- **A cauda do log de texto se perde na trava.** O anel EVT.1 e as migalhas vão além dele (na 168, o submit do anel era posterior à última linha do log).
- **`C:\Windows\Temp` e os logs de AppContainer** (menu Iniciar, `SearchApp`) só são copiados pelo binder se ele chegar ao fim. Na trava, copie à mão de `%LOCALAPPDATA%\Packages\*\AC\Temp\BC250_UMD_LOG_*.txt`, filtrando pela hora (foi feito na 166).
- **O Daniel executa tudo o que toca o hardware e o sistema** (bind, reboot, `powercfg`, recuperação). Nada de commit sem pedido.

---

## 11. Índice de documentos

| Doc | Conteúdo |
|---|---|
| 519 | HDP.1: `0x4A80` = `UTCL1_INV0`; flush real `0x385C`; Corrida 166 |
| 520 | T3.1: aliasing WC/UC da VRAM; exame do FILL; Corrida 167 |
| 521 | EVT.1: anel de eventos ligado + T6; Corrida 168 (FILL em voo, P0) |
| 522 | X1: espera sem HDP; aritmética do RPTR; Corrida 169 (227 s) |
| 523 | X2a: FILL em 256 KiB; migalhas de VRAM; Corrida 170 |
| 524 | X3: canários com o SDMA ocioso; Corrida 171 (premissa refutada) |
| 525 | X4: sem canários em execução; Corrida 172; armadilha das fases |
| 526 | Este documento |
