# Passagem de bastão: corridas 105–110, desktop D3D11 estável, travamento duro e sobreposição do segmento com o TMR (24/09/2026)

> **Para a próxima IA, leia nesta ordem:**
> 1. `CLAUDE.md` (leis, relato obrigatório de 5 seções, Lei 10 do critério);
> 2. este documento;
> 3. o topo de `BASE_DE_AVANCO.md`;
> 4. os docs 413–419.
>
> **Base anterior:** [doc 412](412_HANDOFF_2026-09-24_CORRIDA104_DX60_E_CONTINUIDADE.md) (corridas 101–104, DX.56–60).

## 0. Regras de trabalho com o Daniel (valem sempre)

**Divisão do trabalho:** o Daniel roda o hardware. A IA compila, analisa, prepara e documenta. A IA não instala nem faz bind.

**Antes de cada corrida:**
- Critério escrito antes, conforme a Lei 10: assinatura e implicação em campos separados, desfecho **E — fora do mapa**, e desfecho parcial não é desfecho.
- Uma variável comportamental por corrida. Instrumentação pura pode acompanhar.
- Todo portão novo verificado por mutação. Script de mutação com restauração por bytes e conferência de SHA-256; o `OSError Errno 22` ao gravar fontes já aconteceu duas vezes.
- Subir o `DriverVer` no INF fonte antes de cada pacote. **O empacotador não compila:** rode o build de laboratório antes e **de novo depois** de suítes de mutação.
- `.cmd` sempre em **CRLF** (a auditoria recusa LF).

**Depois de cada corrida:**
- As 5 seções: avanço, sucessos, falhas, veredito, recuperação/pendências.
- Doc de resultado, entrada no topo da BASE e tabela-checklist ✅/🟡/❌/🔜 com o "caminho mais curto".
- Classificar as afirmações em P0/P1/P2/H.

**Proibições e hábitos:**
- Sem WARP ou fallback de CPU. Sem escrita no DCN (leitura é permitida).
- Não usar `-Force` em criação de arquivo.
- **Não commitar.** Toda a série DX.56–DX.67 está **fora de commit** (último commit `8a73051`; `git status`: 20 modificados, 54 não rastreados).
- `C:\Windows\Temp` é ilegível para a sessão não elevada. Os logs da UMD pós-travamento são copiados pelo guardião (DX.57) para `umd-logs-pos-travamento/`.
- Heredoc do Bash corrompe barras invertidas: use Edit/Write, ou Python lendo e gravando bytes.

## 1. O que foi feito nesta sessão (DX.61–DX.67)

| Faixa | O quê | Estado |
|---|---|---|
| **DX.61** | Guardião: journal de amostras PnP só com acréscimos (`safe-bind-pnp-samples.jsonl`), saída da recovery por tentativa, assentamento de até 45 s **sem afrouxar** 5 amostras BasicDisplay/0, e `BLOCKED` secundário só no journal | Validado: **R1** (recuperação no mesmo boot) nas 105, 106 e 107. Nunca precisou do assentamento. |
| **DX.62** | Sonda de alocação v2: `OpenCalls/OpenStatus/DestroyCalls` por token; recusas do `OpenAllocation` e do Destroy passam a falar | Validado. Laço do Explorer = **D3**: o dxgkrnl cria, abre com **sucesso** no nosso Open e destrói. A recusa é posterior às nossas DDIs. |
| **DX.63 / 63b** | Present `Blt` copia para a **alocação de destino** (antes ia para o scanout numa posição centralizada inventada, o que dava dois escritores e `BYTE_PROOF FAIL`). Alocações padrão staging/GDI publicam geometria | Validado (106): **janela D3D11 com o triângulo composta pelo DWM na posição real** (fotos em `…16-57-40…/fotos-operador/`). |
| **DX.64** | `ResourceCopyRegion` (Vulkan) usa e grava o layout rastreado. Antes, a SRV era recusada como "nunca escrita", o Draw recusado chamava `SetErrorCb(E_NOTIMPL)` e o DWM recriava o device, e a recriação não sobrevivia | Validado (107): **desktop estável nos 180 s**, zero recriação. |
| **DX.65** | Rastro durável de paginação com **batimento** a cada 100 ms (registro v2; `Reserved` = tick) e resumo `heartbeat_after_last_paging` no decodificador | Validado (109): a máquina para **inteira** (escritor PASSIVE e paginação no mesmo período de 100 ms). |
| **DX.66** | Migalhas de MMIO em página UNCACHED de VRAM, lidas no StartDevice seguinte antes do fusível (`bc250_mmio_breadcrumb.h` + ciclo de vida no wrapper `Bc250DxgkDdiStartDeviceV5Telemetry`) | Corrida 110: **não armou**. A trava de segurança achou a página 0x0FE80000 **dentro do TMR** (ver §3). Página a realocar. |
| **DX.67** | Corte do fim do segmento do VidMm abaixo de **toda** região interna em uso (dois alocadores) + mapa `[DX67_VRAM_MAPA]` | **EM ANDAMENTO:** código editado, **compila**, auditoria de segurança PASS. **Sem** portão próprio, sem mutação, sem `build-contract`, sem pacote. Ver §4. |

Faixas anteriores desta sessão (DX.54, DX.55, docs 399–403) estão no doc 403. As faixas DX.56–60 da outra IA estão no doc 412.

## 2. Corridas desta sessão

| Corrida | Candidato | Janela | Resultado | Doc |
|---|---|---|---|---|
| 105 | 3.266.1.0 | 180 s | Blt 60/60 recusado (geometria do destino) → B; D3 no Explorer; R1 | 414 |
| 106 | 3.267.1.0 | 180 s | **A**: triângulo composto pelo DWM; "bugar" aos ~140 s causado pela recriação do device | 415 |
| 107 | 3.268.1.0 | 180 s | **A**: desktop estável, zero recriação; R1 | 416 |
| 108 | 3.268.1.0 | 600 s | **Travamento duro aos ~6 min 3 s**; padrão Radeon Software (H) | 417 |
| 109 | 3.269.1.0 | 600 s, **Radeon Software desligada** | **Travamento aos 3 min 16 s**, ao mexer o mouse; Radeon refutada como necessária; B′ (a máquina parou inteira) | 418 |
| 110 | 3.270.1.0 | 600 s | **Travou "depois de um tempo"**; migalhas não armaram (página dentro do TMR); achado do §3 | **sem doc de resultado**, ver §3 |

**Evidência da 110:** `docs/evidence/2026-09-24_20-59-51-DESKTOP-HQAG6T4-lab-binding/`.
- `display-log-this-bind-RECUPERADO.txt`: 53.081.180 bytes, sha256 `2e34f78a…4f26`. O boot seguinte começa na linha 377.891.
- `paging-trace-this-bind.bin`: 3.547 registros, sha256 `591d70bd…c81b`. **Ainda não decodificado:** falta a hora do travamento.
- Recovery: `RECOVERY_OK_ONLINE` às 21:11:53, no **boot novo**.
- `ANTERIOR AUSENTE` nos dois StartDevice, com magic lixo **variando** (0x83E0E8E6 → 0x8BE0E8E6). É coerente com uma página dentro de uma região gerida por firmware. **Isso não diz nada sobre a VRAM sobreviver ao reinício a quente.**

## 3. Achado principal da 110 (P0 no log do mesmo boot)

**O segmento e o TMR:**
- O segmento entregue ao VidMm é `SegBase=0xA8C000`, `SegBytes=0xF374000`, ou seja **[0x00A8C000, 0x0FE00000)** (o teto 0x0FE00000 está no código).
- Há uma região **em uso** do alocador do bootstrap: **[0x0FB00000, +0x400000), papel 5 (`BC250_VRAM_ROLE_TMR`)**, alocada por `core/bc250_compute_bootstrap.c:230` (4 MB, de cima para baixo, persistente).
- **Sobreposição de 3 MB** (0x0FB00000–0x0FE00000).

**O que o VidMm pôs lá:** o VidMm coloca as **tabelas de página raiz** dos contextos de cima para baixo no fim do segmento: `McdmSetRootPageTable Offset=0xF2F9000 / 0xF33E000 / 0xF34B000`. Somando a base, isso dá VRAM **0x0FD85000 / 0x0FDCA000 / 0x0FDD7000**, **dentro** da região TMR.

**Por que o erro existia:** `Bc250ActiveQueryRuntimeCaps` só descontava a dummy da MMU e o bloco do motor de submissão. O comentário do código afirmava que a faixa 0x0FE00000–0x0FF00000 era folga livre. **Os dois estavam errados.**

**O que não está provado:**
- se essa região de TMR é o TMR **vivo** do PSP. O `SETUP_TMR` aparece em `bc250_pfp_direct_load.c:853`, com **outra** alocação de papel FIRMWARE, e em `compute_bootstrap` (payload). O carregamento de firmware do SDMA da 110 usou buffers em RAM do sistema;
- se o **travamento duro** vem daí (**H**). A hipótese é plausível: o VidMm sobrescreve memória de firmware/protegida à medida que cria contextos (o laço do Explorer cria ~300 contextos/min, cada um com tabela raiz no topo do segmento), até algo da GPU travar o barramento.

**Nas corridas, o maior offset de segmento observado nas amostras do log foi ~0x87B0000** (não chegou à faixa). Mas as tabelas raiz vivem ali por construção do VidMm.

## 4. Onde parei exatamente (DX.67, a terminar)

**Arquivo:** `driver/wdk/bc250_active_miniport.c`, dentro de `Bc250ActiveQueryRuntimeCaps`, logo depois do corte do motor de submissão (bloco com `DX.67 (Corrida 110)`).
- Para cada região em uso de `context->GlobalMmuAllocator` e `context->Core.StartDevice.Owned.Bootstrap.Allocator`: se ela começa dentro de `[localStart, localEnd)`, faz `localEnd = region->Offset`.
- Se começa **antes** da base e invade o segmento, registra `[DX67_VRAM_MAPA] SOBREPOSICAO_NA_BASE RECUSA de correcao` e **não corrige** (mudar a base é outra variável).
- Imprime o mapa `[DX67_VRAM_MAPA]` só quando o fim muda (static `s_Bc250Dx67LoggedEnd`).

**Estado:** o build de laboratório compila (SYS em `driver/wdk/output_lab/`, sha256 começa `0eb3d9be…`) e a auditoria de segurança dá PASS. **O `DriverVer` fonte ainda está em 3.270.0.0**, e o 3.270.1.0 já foi instalado; suba para 3.271.0.0.

**Falta:**
1. Portão `audit_dx67_segment_excludes_internal_regions`: o laço sobre os dois alocadores antes do `if (localStart >= localEnd)`, a truncagem `localEnd = region->Offset`, e o mapa. Mutações: remover a truncagem, remover um dos alocadores, inverter a condição.
2. `build-contract` (EXIT=0) e as suítes de mutação anteriores: `test-dx61…`, `test-dx62-dx63-mutations.py` (22), `test-dx66-breadcrumb-mutations.py` (9), `test-dx56…`.
3. **Realocar a página das migalhas (DX.66):**
   - 0x0FE80000 está no TMR, e o portão DX.66 hoje só exige a faixa [0x0FE00000, 0x0FF00000), **que está errada**.
   - A primeira corrida com a DX.67 imprime o mapa real das regiões. Escolha uma página livre fora do segmento **e** fora de toda região, ou reserve a página com `Bc250VramReserveFixed` no alocador do bootstrap **antes** das alocações de cima para baixo (e confira o outro alocador).
   - Atualize o portão e a mutação "pagina fora da folga".
4. **Pacote e pré-voo:**
   - pacote 3.271.1.0 e auditoria do pacote;
   - `prepare-corrida111-*.ps1`, copiado do `prepare-corrida110-dx66.ps1`, que já exige a Radeon Software desligada; troque hashes e versão;
   - runner `EXECUTAR_CORRIDA111_*.cmd` em CRLF, 600 s.
5. **Doc de resultado da 110 e critério da 111.** Sugestão de critério:
   - **controle:** `[DX67_VRAM_MAPA] segmento [0xA8C000, X)` com X ≤ início da menor região interna; tabelas raiz (`SetRootPageTable Offset`) + base < X;
   - **A:** 600 s sem travar com R1 (a sobreposição era necessária ao travamento);
   - **B:** travamento com o segmento limpo (a hipótese cai; as migalhas, se armadas, localizam);
   - **E.**
   - É **uma** variável comportamental (o tamanho do segmento); as migalhas são instrumentação.

## 5. Outras pendências abertas (em ordem sugerida)

1. **Travamento duro** (§3/§4). É o bloqueio principal.
   - Tempos com o candidato atual: 100 ~6,0 min, 101 ~6,6, 102 ~8,5, 108 ~6,0, 109 ~3,3, 110 "depois de um tempo" (decodificar o rastro).
   - Assinatura: sem BugCheck, Kernel-Power 41, a máquina para inteira.
2. **Laço do Explorer (D3):** a textura compartilhada 1024×832 `Bind=0xA8 Misc=0x2` é criada → aberta (OK) → destruída pelo dxgkrnl → `E_INVALIDARG`. O KMD responde igual ao controle aceito do DWM. Próximo: trace ETW do DxgKrnl, ou comparar contrato com um driver WDDM real. Não mexer em `hResource`/`CpuVisible` por palpite. Nenhuma criação `Misc=0x2` teve sucesso em corrida alguma.
3. **Recriação de device do DWM não sobrevive** (camada 2 do doc 415). Mesmos tokens de alocação entre recargas da DLL (o contador da UMD recomeça), VAs reutilizadas não resolvidas (`status=9`), e `PRIMARY_FALLBACK` somando a base do segmento duas vezes (`Addr` do `SetVidPnSourceAddress` já é deslocamento de VRAM). Não exercitada desde a DX.64, mas vai reaparecer em qualquer TDR ou recriação.
4. **Velocidade:** ~1 FPS no harness. Custos medidos nos docs 398–403.
5. **DX.60 (0x119 da 103):** nunca exercitada (zero `PREEMPT_DONE Node=0` desde então).
6. **Pixels do destino** do Blt: sem prova byte a byte. A prova visual são as fotos da 106.

## 6. Estado do ambiente na máquina

- **Radeon Software desligada pelo Daniel** para as corridas 109–110: tarefa `StartCN` desabilitada, serviço "AMD External Events Utility" desabilitado/parado. O pré-voo da 110 **recusa** o bind se ela puder abrir. Para desfazer (o Daniel, elevado):
  - `Enable-ScheduledTask -TaskName StartCN`
  - `Set-Service -Name "AMD External Events Utility" -StartupType Automatic`
  - `Start-Service -Name "AMD External Events Utility"`
- BasicDisplay em Código 0 após a recovery da 110, e `CLEAN_BOOT_REQUIRED` ativo. **Antes do próximo bind: desligamento completo, com corte de energia.**
- **Se travar numa corrida com migalhas armadas:** reinício a quente (reset), **sem** cortar a energia. Deixe o Windows subir e o guardião agir no prazo; a leitura das migalhas sai no StartDevice do boot novo.
- **Recuperar evidência após travamento** (o binder não chega à coleta final):
  - log do KMD: `C:\BC250_DISPLAY_LOG.txt` a partir de `display-log-baseline.json.start_offset`; o boot seguinte começa no segundo `[DX48_LIFECYCLE] AddDevice ENTER`;
  - rastro: `C:\BC250_PAGING_TRACE.bin` a partir de `paging-trace-baseline.json.start_offset`, decodificado com `python tools/decode-bc250-paging-trace.py <arquivo> --last N`. Registros com `latest_sequence=0` são do boot novo.

## 7. Ferramentas criadas nesta sessão

| Tipo | Arquivos |
|---|---|
| Mutações | `tools/test-dx61-safe-bind-recovery-mutations.py` (7), `tools/test-dx62-dx63-mutations.py` (22: DX.62, 63, 63b, 64, 65), `tools/test-dx66-breadcrumb-mutations.py` (9) |
| Decodificador | `tools/decode-bc250-paging-trace.py` (v1/v2 + batimento), teste `tools/test-dx56-paging-trace-decoder.py` |
| Pré-voos e runners | `tools/prepare-corrida105…110-*.ps1` e `tools/EXECUTAR_CORRIDA105…110_*.cmd` |
| Portões | `audit_dx61…`, `audit_dx62…`, `audit_dx63…` (com 63b), `audit_dx64…`, `audit_dx66…`, e o DX.65 dentro de `audit_dx56_paging_trace` |

## 8. Tabela-checklist

| Item | Estado |
|---|---|
| Janela D3D11 composta pelo DWM | ✅ (106/107, visual) |
| Desktop estável 180 s | ✅ (107) |
| Recuperação no mesmo boot sem travamento | ✅ (105–107) |
| Travamento duro (3–8 min) | ❌ → 🔜 DX.67 (sobreposição segmento/TMR) + migalhas realocadas |
| Laço do Explorer | 🟡 D3; falta o que o dxgkrnl valida |
| Recriação do device sobrevive | ❌ camada 2 aberta |
| Velocidade | ❌ ~1 FPS |
| DX.60 / 0x119 | 🔜 |
| Jogo | 🔜 |

**Caminho mais curto:** terminar a DX.67 (portão, mutação, contrato), realocar a página das migalhas, empacotar 3.271.1.0 e rodar a Corrida 111 com 600 s, depois de desligamento completo com corte de energia.
