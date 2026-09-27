# 467 — DX.85: a tela preta das Corridas 130 e 131 era um deadlock da própria instrumentação DX.83; critério da Corrida 132

> **Data:** 26/09/2026 · **Trabalho offline, sem corrida.** Nada aqui foi validado em hardware.
> **Candidato:** DX.85 / pacote lab **3.290.1.0** (INF fonte 3.290.0.0), perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, P1 PERSISTENT SUBMISSION.
> **Retifica:** [463](463_CORRIDA130_DX83_ANALISE_TRAVAMENTO_ETW_EDID_E_RESULTADO.md), [465](465_CORRIDA131_DX84_TOPOLOGIA_UID0_SUCESSO_E_TRAVAMENTO_RADV_VKALLOCATE.md) e [466](466_HANDOFF_2026-09-26_CORRIDA131_UID0_EDID_E_RADV_VKALLOCATE.md).

---

## 1. Reanálise das Corridas 129, 130 e 131 (relato obrigatório)

### 1.1 Houve avanço?
**Aprendizado de diagnóstico, não avanço funcional.** Nenhuma das três corridas produziu desktop. A
reanálise identifica a causa da tela preta de 130/131 (P0) e separa um segundo defeito, anterior, que
trava a thread de paginação dentro de `FLUSH_TLB` nas 127–129.

### 1.2 O que a evidência mostra

| Fato | Corrida 130 | Corrida 131 | Classe |
|---|---|---|---|
| `BC250_HANG op=PAGING_DDI phase=ENTER` / `LEAVE` (só FLUSH_TLB) | 110 / 109 | 90 / 89 | P0 |
| Último `ENTER` sem `LEAVE` | `id=110 operation=12` | `id=90 operation=12` | P0 |
| Rastro DX.56: último estágio registrado | `active_count=0` aos ~2,4 s | `AFTER_DISPATCH op=11`, seq 4485, `active_count=0` no tick 20 | P0 |
| `BUILD_ENTER` seguinte (1ª instrução do Body) | nunca registrado | nunca registrado (seq 4486 não existe) | P0 |
| Migalha DX.66 no boot seguinte | — | `CPU00 PAGINACAO PAGING_DDI 0x0C fase=ANTES (acesso NAO concluido)` | P0 |
| Writer do rastro continuou | até 14,5 min | 632 ticks (~63 s) | P0 |

Fontes: `docs/evidence/2026-09-26_00-48-23-.../bc250_display_log-tail-recovered.txt`,
`docs/evidence/2026-09-26_08-19-24-.../display-log-this-bind.txt`, cauda de `C:\BC250_DISPLAY_LOG.txt`
a partir do offset 2.752.112.370, fatia de `C:\BC250_PAGING_TRACE.bin` a partir do offset 39.676.656.

### 1.3 Causa (P0 pelo log + P1 pelo código)
A DX.83 acrescentou em `Bc250McdmBuildPagingBuffer` (wrapper) duas linhas `BC250_HANG ENTER/LEAVE`
**antes** do incremento de `g_Bc250PagingActiveCount`, que só acontece dentro do Body. Com o contador
em zero e IRQL PASSIVE, `Bc250DisplayLog` (`bc250_display_ddi.c`) faz `ZwCreateFile` + `ZwWriteFile` +
`ZwFlushBuffersFile` + `ZwClose` — **I/O de arquivo síncrono na thread de paginação do VidMm**, o que o
próprio comentário do logger proíbe por causar deadlock com NTFS/Cache Manager. A thread parou entre a
linha escrita e a primeira instrução do Body: dentro do logger.

Consequências, todas **sintomas** desta trava:
- a paging fence para de avançar → `WaitPaging` do Explorer (fence 7004) nunca retorna;
- `vkAllocateMemory` do DWM/Explorer na RADV espera a mesma paging fence → o DWM nunca apresenta
  (`DxgkDdiPresent = 0`) → tela preta.

### 1.4 O que fica refutado nos docs 463/465/466
1. **"KMD Paging Engine 100% vivo / não travou"** — refutado. O batimento prova só que a *thread do
   writer* vivia; a paginação parou aos ~2 s com uma chamada `FLUSH_TLB` presa.
2. **"UID1 impedia o CommitVidPn" (463)** — refutado pelo próprio log da 130: `CommitVidPn` = 1 com
   `ChildUid=1`, como nas 118 (880 Presents) e 126 (1150 Presents), todas com `ChildUid=1`/`SyntheticEdid=0`.
   A mudança para UID0/EDID sintético não era necessária. A 131 mostra que UID0 também comita (P0),
   mas não é a variável que faltava.
3. **"Gargalo na espera síncrona da RADV" (465/466)** — é sintoma da mesma paging fence parada. O patch
   proposto (timeout de 1000 ms e seguir em frente) **não foi aplicado**: seria sucesso sem trabalho.
4. **"WaitPaging com timeout salvou o Explorer" (465)** — o timeout de 1000 ms devolvia `BC250_WINSYS_OK`
   com a paginação pendente (Lei 4). Não salvou nada; mascarou a trava e deixou o Explorer usar memória
   com paginação incompleta.

### 1.5 Segundo defeito, anterior e ainda aberto
Nas Corridas 127, 128 e 129 o rastro termina com a thread de paginação em `BEFORE_DISPATCH` de
`FLUSH_TLB` (op 12), `active_count=1`. Na 128 o writer parou no mesmo instante (máquina inteira); na
129 parou 12,3 s depois. Esses binários ainda não tinham os estágios 7 e 40–46, logo o ponto interno
é **desconhecido**. Candidatos por leitura (auditoria de código, P1 para custo e H para causa):
1. **Commit do journal GPUVA** com custo ~M·(N·teto + tamanho do espaço): cada
   `Bc250GpuvaSpaceReplaceRangeTransactional` zera um Scratch do tamanho do espaço inteiro, reinsere cada
   faixa viva com `InsertRange` (que varre todos os slots) e copia o espaço byte a byte. A DX.81 subiu
   o teto de 1024 para 4096, e 128/129 são as duas primeiras corridas com 4096. **H:** a Corrida 115 já
   travara aos ~2m19s com o mesmo último estágio.
2. **Leitura MMIO que não completa** no flush do GFXHUB/HDP/UTCL1, possivelmente concorrendo com
   quiesce de TDR (a 127 teve 23 resets) — única candidata que explica a máquina inteira parar. H.
3. **`ObjectLock` retido** por outra thread fazendo I/O de log ou MMIO sob o lock. P1 para latência, H
   para travamento permanente.

---

## 2. Alterações da DX.85 (offline)

| # | Alteração | Arquivo | Por quê |
|---|---|---|---|
| 1 | `g_Bc250PagingActiveCount` passa a cobrir o **wrapper inteiro**, incluindo as linhas `BC250_HANG` | `driver/wdk/bc250_mcdm_callbacks.c` | Causa da tela preta das 130/131 (§1.3) |
| 2 | `WaitPaging` e `WaitForFenceFromCpu` usam `Bc250WinsysBoundedCpuWait`: evento assíncrono, teto de 5000 ms (> TdrDelay de 2 s) e **timeout = falha** `0x80070102`, nunca OK | `driver/umd/bc250_wddm_winsys.c` | Lei 4; a DX.84 continuava como sucesso |
| 3 | Topologia de volta a `ChildUid=1`, `VidPnTargetId=1`, `EnableSyntheticEdid=0` (código, INF e testes) | `bc250_display_ddi.c` (revertido ao HEAD), `bc250_active_miniport.c`, `bc250mcdm.inf`, `display_vidpn_test.c`, `active_miniport_test.c` | Estado com imagem provada nas 118–128; a premissa da troca foi refutada (§1.4) |
| 4 | `BC250_GPUVA_SPACE_MAX_RANGES` e `BC250_GPUVA_JOURNAL_MAX_CHANGES` de volta a **1024** | `driver/core/bc250_gpuva_space_contract.h`, `..._journal_contract.h` | Estado medido das 118–127 (600 s na 127); corta ~4× o custo do commit sob `ObjectLock` |
| 5 | Marcador 44 (`FLUSH_JOURNAL_WAIT`) grava `(M << 16) \| N` no status; decodificador mostra `commit_changes_m` / `commit_live_ranges_n` | `bc250_active_miniport.c`, `tools/decode-bc250-paging-trace.py` | Se travar no commit, saber se o lote era anormal |
| 6 | Portões `audit_dx85_paging_thread_never_does_file_io` e `audit_dx85_cpu_wait_timeout_is_failure` | `tools/audit-bc250mcdm-safety.py` | Impedir as duas classes de voltar |
| 7 | Teste: stub aceita a espera e **não** sinaliza → `WaitPaging` devolve `CALLBACK_FAILED` com `0x80070102` e a fence não avança | `driver/tests/bc250_wddm_winsys_test.c` | Congelar o comportamento correto, não a mentira |

**Mantido e declarado:** a UMD da DX.83 (`HangTrace`, só com `HKLM\SOFTWARE\BC250\Umd\HangTrace=1`); os
marcadores 7 e 40–46 do KMD; e o `RLC_SAFE_MODE` + soft-reset de CP/GFX acrescentado em `StopDevice`,
`GfxEngineOffline` e no quiesce de TDR. Esse último vem de referência externa e **nunca foi validado em
hardware**; roda só em teardown e TDR, não no caminho de apresentação.

**Não aplicado:** patch de timeout na RADV (`radv_wddm2_bo.c`). O RADV da 132 é o mesmo da 131 (`FCAA11FE…`).

### Verificação offline
- `driver\umd\build-umd.bat` EXIT 0; `WDDM WINSYS CONTRACT: PASS` com a linha
  `WaitPaging TIMEOUT (5000ms): fence NAO chegou -- FALHA fechada, sem continuar` produzida pelo teste novo.
- `driver\wdk\build-contract.bat` EXIT 0 (antes e depois da reversão para 1024).
- `python tools\audit-bc250mcdm-safety.py` PASS. **Duas mutações verificadas:** mover o incremento para
  depois da linha `ENTER` reprova com "Bc250DisplayLog fora da janela de paginacao"; remover
  `hr = BC250_WINSYS_HR_WAIT_TIMEOUT` reprova com "timeout de espera vira falha nomeada".
- `tools\build-bc250mcdm-lab-bringup.ps1 -Execute …` EXIT 0; SYS de laboratório `578346DB…`.
- Empacotado com `-EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -ArmGfxWaveProbe
  -IAcceptSelectorWritesDuringTdr`. A primeira passada caiu na armadilha conhecida (`IOException` no
  `Copy-Item`, auditoria BLOCKED com hashes divergentes); a segunda fechou e `audit-bc250mcdm-lab-package.ps1`
  deu PASS.
- Binários conferidos: `SourceSysSha256` = SYS recém-compilado; UMD empacotada contém "FALHA fechada, sem
  continuar" e não contém "continuando sem travar"; SYS contém `Returned DP ChildUid=1`.
- `tools\prepare-corrida132-dx85.ps1` PASS (somente leitura).

### Pacote 3.290.1.0

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` (assinado) | `F3F491F8AFE151692E120FE33BB5AE1A0977973F43FB1E7F4FB1765C120B0BDF` |
| `bc250mcdm.sys` (fonte lab) | `578346DBC7AB751A5C59D801F5AC8C57019CB101328164D887DDDD4EB36DBCAE` |
| `bc250umd.dll` | `3A9731939A970F7386EA75E69277D11764BBEF3178AFC9D98343161A4B6F2A0A` |
| `bc250mcdm.inf` | `844A31DFFD30CD0EC288667D294E35A0280D865C711952336F8A3EBA7EE239B0` |
| `bc250mcdm.cat` | `F4B62BB05511EA3B403733454C482520595685D18D2C0262E658B9C4BD46CCC7` |
| `vulkan_radeon.dll` (inalterado) | `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C` |

---

## 3. Critério da Corrida 132 (escrito antes; Lei 10)

**Runner:** `tools\EXECUTAR_CORRIDA132_DX85_600S.cmd` (600 s, ETW de travamento e captura de EDID
ligados). **Pré-requisito:** boot frio com corte de energia (`C:\BC250_REQUIRE_CLEAN_BOOT.json` presente).
**Teto:** uma corrida para este candidato. Se der C, parar e conferir hashes antes de qualquer outra.

Medições a extrair: contagem de `PAGING_DDI phase=ENTER`/`LEAVE`; último registro do rastro DX.56
(estágio, `active_count`, M/N se for 44); `[Present #` no KMD; `WaitPaging TIMEOUT` nos logs da UMD;
relato visual do Daniel; estado final do watchdog.

| Desfecho | Assinatura (o que o log/tela mostra) | Implicação (para onde leva) |
|---|---|---|
| **A** | `ENTER` e `LEAVE` do wrapper diferem em no máximo 1, só no instante do rollback; rastro DX.56 continua avançando até o rollback; `[Present #` > 0 no KMD; zero `WaitPaging TIMEOUT`; Daniel vê desktop durante os 600 s; watchdog R1 com BasicDisplay Código 0 | O deadlock da DX.83 era a causa da tela preta de 130/131. Com 1024, a trava de `FLUSH_TLB` não apareceu em 600 s: isso *reforça*, mas não prova, a hipótese do custo do commit. Próximo passo: medir custo do commit (M/N) em corrida normal e só então reabrir capacidade. |
| **B** | Mesma assinatura de A no início (Presents > 0, desktop visível) e depois congelamento antes de 600 s; último registro do rastro dentro de `FLUSH_TLB` | Deadlock corrigido; trava de `FLUSH_TLB` persiste com 1024. O estágio decide: **40 sem 41** → alguém segura `ObjectLock` (auditar holders); **42 sem 43** → MMIO do GFXHUB/HDP/UTCL1 não completou; **44 sem 45** → commit do journal, com M/N no status; **46 ou fora de 40–46** → não é o handler. |
| **C** | Tela preta e de novo um `ENTER` de wrapper sem `LEAVE`, sem `BUILD_ENTER` seguinte | A correção não está no binário que rodou. Conferir o hash do SYS instalado contra `F3F491F8…` antes de qualquer hipótese nova. |
| **D** | Tela preta, wrapper balanceado, rastro de paginação avançando, e `WaitPaging TIMEOUT (5000ms)` nos logs da UMD | A paging fence para por outra causa (o VidMm espera algo que não é a nossa DDI). A falha agora é nomeada na UMD; o `TargetFence`/`CurrentFence` da linha e o último estágio do rastro orientam a próxima leitura. |
| **E** | Algo fora do mapa | Descrever o observado antes de julgar. |

Desfecho parcial não é desfecho: A exige desktop visível **e** Presents **e** rastro vivo; se faltar um,
não é A.

**Esperado ao final em qualquer desfecho:** BasicDisplay Código 0 (rollback R1 no mesmo boot ou no boot
seguinte), `CLEAN_BOOT_REQUIRED` armado. 60 FPS, jogos e a trava de `FLUSH_TLB`: **NÃO VALIDADOS EM HARDWARE**.
