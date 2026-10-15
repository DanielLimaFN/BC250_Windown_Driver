# 658 — Plano depois das auditorias (docs 653, 653-TMR, 656, 657): checklist e Corrida 249

**08/10/2026, offline.** Consolida as três auditorias externas e a análise do MetalCyan. Cada afirmação delas foi conferida no código antes de entrar aqui. **Regra nova (Daniel):** corrida só quando necessária, e cada corrida testa um pacote de correções com repetição, não uma variável isolada.

## 1. O que as auditorias disseram, conferido no código

| # | Achado | Fonte | Conferência | Situação |
|---|---|---|---|---|
| 1 | **TMR do PSP devolvido ao alocador** depois do `SETUP_TMR`/`LOAD_IP_FW`; o VidMm põe tabelas de página ali | 653-TMR | `bc250_pfp_direct_load.c:1484-1487` libera `tmrToken` e `token` no `Cleanup` (**P1, confirmado**). Linux com 256 MB: `reserve 0x400000 from 0xf40f800000 for PSP TMR`, fixado (P0, doc 552) | **Defeito real. Prioridade 1** |
| 2 | TMR passado ao PSP com PA da **BAR**, não da DRAM | 653-TMR | `:656` `tmrPa = bar->Start + off` (**P1, confirmado**) | Corrigir junto com o 1 |
| 3 | Fallback do TMR em `+0x1F800000` (era dos 512 MB) | 653-TMR | `:658-659` (**confirmado**); com 256 MB cai fora da VRAM | Falhar fechado |
| 4 | Contagem de comandos aceitos pelo PSP sobrescrita | 653-TMR | a conferir na implementação (`:935` × `:1264`) | Instrumentar |
| 5 | Buffers `static` no split do SDMA, com lock solto entre pedaços | 653, 657 | `bc250_sdma_present.c:2514-2515` (**confirmado**) | Bug real; corrigir |
| 6 | `SetPowerState(D0)` devolve `STATUS_NOT_SUPPORTED` | 653, 657 | `bc250_mcdm_callbacks.c:4388-4395` (**confirmado**) | Corrigir (não deve ser chamado nas corridas) |
| 7 | CPU lê 10 dwords da VRAM em todo `COMPOSITE_BLT` | 653 | `bc250_active_miniport.c:18303` (**confirmado**) | Limitar às primeiras 16 composições |
| 8 | `IhRing` não zerado antes do free | 657 | a conferir | Corrigir (higiene) |
| 9 | `BC250_PTE_SNOOPED` nas PTEs de sistema "contra o Linux" | 653, 657 | linha `:1280` confirmada, **mas a comparação está errada**: o `gart_pte_flags` citado é da tabela GART do kernel; nas PTEs de VM o amdgpu põe `SNOOPED` em página de sistema **cacheada** (`amdgpu_ttm_tt_pde_flags`, P1 de memória, a reconferir no upstream) | **Contestado.** Tirar o snoop de página cacheada dá dado errado, não estabilidade. Só mexer depois de saber o cache da CPU no segmento 2 |
| 10 | "Não desligamos CG/PG" | 653 | **Falso:** `Bc250ActiveRlc1Resume` já zera `RLC_CGCG_CGLS_CTRL` e `RLC_PG_CNTL` (657 acertou) | Descartado |
| 11 | MGCG por bloco (GL2, TA, SPI...) continua como o POST deixou | 657 | P1 por leitura | Medir antes de mudar |
| 12 | A trava cresce com eventos, não com tempo: FILLs até a trava, Mann-Whitney RLC rodando × ROM p≈0,006 | 653-TMR | a refazer com script próprio | Adotar FILLs como régua |
| 13 | RLC rodando encurta a vida (40/108/8/22 s contra 289/33/228 s) | todas | a 246 (ROM, 33 s) é o contra-exemplo (657) | Sinal provável, não causa única |
| 14 | **Sonda DCN escreve no DCN**, fora do transporte MMIO | 657 | `bc250_dcn_probe.c:53-90` escreve `AUX_SW_DATA`/`AUX_CNTL` (**confirmado**), chamada no StartDevice (`bc250_active_miniport.c:9233`). Offsets `0x4800–0x480C` **não estão no catálogo docs/03** (Lei 7) | **Não é só leitura.** Ver §3 |
| 15 | MEC quebrado no GC 10.1.3, "Linux desliga o MEC" | 656 (MetalCyan) | Ver §4 | Não é o próximo passo |
| 16 | SDMA trava no opcode `GPUVM_INV` | 656 | Não usamos esse opcode (grep vazio) | Já estamos do lado certo |
| 17 | `SDMA1_CNTL.TRAP_ENABLE` | 656 | SDMA1 conclui por polling hoje | Só importa se o SDMA1 passar a usar interrupção |

## 2. Checklist

### 2.1 Antes de qualquer corrida (offline, sem custo)

- [x] **TMR.1:** (feito; ativo na 249 e na 250) TMR persistente (nunca liberado), PA da DRAM (`GCMC_VM_FB_OFFSET<<24 + off`), sem fallback; região quad do CP também persistente; o corte do segmento (DX.67) passa a excluir o TMR. Portão de auditoria com mutações.
- [~] **Instrumentação do PSP:** (respostas feitas; contador de PDEs no TMR pendente) resposta de `SETUP_TMR` e de cada `LOAD_IP_FW` em linha própria; contador de PDEs do VidMm que caem no TMR (deve ser 0 depois da correção).
- [x] **Correções de defeito** (sem efeito esperado na trava, mas são bugs): buffers do split do SDMA no contexto, com lock; `D0` devolve sucesso; `IhRing` zerado antes do free.
- [x] **Leitura da VRAM pela CPU no `COMPOSITE_BLT`:** só nas 16 primeiras composições (prova continua no começo, silêncio em regime).
- [x] **Sonda DCN:** (A + B/C só leitura; log corrigido na 250, dados no doc 660) decisão do Daniel (§3). Recomendação: Bateria A só com offsets catalogados, B e C atrás de chave em 0.
- [x] **Monitor de telemetria:** em toda corrida, numa janela separada, só com métricas do sistema operacional (§5).
- [ ] **Script de régua:** contar FILLs, `FlushTlb` e Presents até a trava em todas as corridas desde a 210, e refazer o teste estatístico RLC rodando × ROM. Vira saída padrão da análise.
- [ ] **SNOOPED:** conferir `amdgpu_ttm_tt_pde_flags` no upstream e o atributo de cache que o VidMm dá às páginas do segmento 2. Só depois decidir.

### 2.2 Fora do driver (decisão e mãos do Daniel, não conta como corrida)

- [x] **L1 no Linux com `amdgpu.dc=0`:** desfecho A, 2 × 615 s sem trava (doc 662); ressalva: VRAM pouco cheia o display fica no GOP, como o nosso. Mesma carga do doc 552. Se o Linux travar, o mecanismo é o dono do DCN; se aguentar 2 × 10 min, o display não basta.

### 2.3 Corridas (só quando o pacote estiver pronto)

- [x] **249:** B+, 4.429 FILLs (doc 659) pacote de correções (§6).
- [x] **250:** B, 1.754 FILLs: o ganho da 249 não se repetiu (doc 660) repetição da 249, obrigatória se a 249 passar.
- [~] **251:** pronta (doc 664): SOMBRA.1 + foto do clock gating e refresh real

### 2.4 Depois (não agora)

- [ ] SMU: métricas (GFXCLK, FCLK, MCLK, SOCCLK, tensões) amostradas pelo KMD a 1 Hz, sob o lock do KMD. É novo ator falando com o SMU; entra só se a 249/250 não resolverem.
- [ ] Estado de MGCG por bloco perto do flush de TLB (657 H1), com offsets do catálogo.
- [ ] MEC fora do StartDevice (§4).
- [ ] P5 (VM de 4 níveis).
- [ ] DCN com escrita (AUX, EDID real): P9, pela governança.

## 3. A sonda DCN

A outra sessão descreveu a sonda como "só leitura". Ela não é:
- **Bateria A** lê OTG0/HUBP0. Leitura é permitida, mas os offsets precisam estar no catálogo; o `OTG0_OTG_STATUS_FRAME_COUNT` já está e já é lido pelo VSync.
- **Baterias B e C** escrevem `AUX_SW_DATA` e `AUX_CNTL` para disparar transações AUX. É escrita em registrador de DCN, proibida fora da P9 pelo `CLAUDE.md`, em offsets (`0x4800–0x480C` da BAR5) **sem procedência** no catálogo. Uma escrita num offset errado da BAR5 pode cair em outro bloco.
- A sonda mapeia a BAR5 por conta própria (`MmMapIoSpace`), fora do transporte com allowlist.
- O doc 654 diz que o teste unitário usa "dados medidos no silício"; o EDID do teste tem serial `0x12345678`, ou seja, é dado sintético.

**Recomendação:** na 249, só a Bateria A, com os offsets conferidos no catálogo; B e C desligadas por chave em 0. Se o Daniel quiser B e C mesmo assim, é decisão dele, registrada como exceção à regra da P9, e de preferência numa corrida que não seja a da trava.

## 4. O MEC, como no MetalCyan

- **O que o Linux faz (P1 + P0):** o `amdgpu` **cria** filas de compute no GC 10.1.3 (`num_compute_rings = num_kcq`, `gfx_v10_0.c:7424`). Na coleta Linux desta placa (doc 552) as filas de compute existem e ficaram com fence 1: foram testadas e nunca usadas. Quem não usa o MEC é a camada de cima (Mesa), não o kernel.
- **O que nós fazemos (P0, log da 248b):** o MEC só trabalha **dentro do StartDevice**: 17 publicações, linhas 140–350 de 53 mil. Em regime ele fica ocioso, como no Linux, e a RADV submete pelo nó 3D (GFX). Nenhum nó de compute é exposto ao WDDM.
- **Conclusão:** comentar o MEC agora não é igualar ao Linux (lá ele é inicializado) e mexe na sequência de boot. Fica na lista do depois. O risco que o MetalCyan descreve (trava 0,2 s depois de mandar compute ao MEC) não acontece em regime aqui, porque nada é mandado ao MEC depois do boot.

## 5. Monitor de telemetria em todas as corridas

`tools\MONITOR_TELEMETRIA_2S.cmd` → `tools\monitor\bc250_monitor.exe`:
- **Métricas do sistema operacional** (clock da CPU, RAM, VRAM por `IDXGIAdapter3::QueryVideoMemoryInfo`): seguras.
- **Métricas de hardware** (clock e tensão da GPU pelo SMU, temperaturas por SMN): passam pelo driver `bc250compute`, que **escreve MMIO arbitrário** na BAR5 (`IOCTL_BC250_WRITE_MMIO_REG` sem allowlist) e usa o par índice/dado SMN `0x38/0x3C`, **o mesmo par que o nosso KMD usa para o Tctl**. Dois atores no mesmo par índice/dado podem ler e escrever o endereço SMN errado.

**Regra:** o monitor entra em toda corrida, numa janela separada, e o lançador **recusa** a corrida se o serviço `bc250compute` estiver rodando. Hoje ele está instalado e parado; com ele parado, o monitor mostra só as métricas do sistema operacional. As métricas do SMU entram depois, pelo próprio KMD (§2.4).

## 6. Corrida 249 (critério escrito antes)

**Pacote 3.394.1.0** = 3.393.1.0 (G.1 + RLC rodando + LOGMIN.1) + TMR.1 + instrumentação do PSP + correções de defeito + leitura da VRAM só no começo + monitor + DCN só Bateria A.

**Por que a base com o RLC rodando:** é a configuração que trava mais rápido (4/4 abaixo de 133 s e de ~722 FILLs). Se o pacote a levar a 600 s, a chance de acaso é pequena; na base com RLC na ROM, ~15% das corridas passam de 600 s por sorte.

**Segunda métrica (performance, sem custo extra):** Presents por segundo e tempo médio de blt, tirados do log, comparados com a 248b.

**Pré-condição:** resposta do `SETUP_TMR` registrada; TMR fora do segmento no mapa DX.67; desktop desenhado; roteiro do menu até o rollback.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | 600 s com rollback limpo, mais de 1.500 FILLs, contador de PDEs no TMR = 0 | Repetir (250). Se as duas passarem, o pacote resolve; separar depois, já com uma base estável |
| B | Trava antes de 600 s, com a pré-condição OK | O pacote não basta. As correções ficam (são defeitos). 250 vira SMU a 1 Hz + estado de MGCG (H2/H1 do 657) |
| B+ | Trava, mas depois de bem mais FILLs que a base (acima de ~1.500) | Melhora real sem cura: TMR provavelmente contribui. Repetir para confirmar |
| C | `SETUP_TMR` recusado pelo PSP | O PSP tem TMR do BIOS em outro lugar: achar onde (offline) |
| D | Regressão no boot (Código 43, CP sem firmware) | Reverter TMR.1; o log do PSP diz por quê |
| E | Fora do mapa | Descrever antes de julgar |

**Teto:** 249 + 250. A 251 só é escrita depois de ler as duas.

**NÃO VALIDADO EM HARDWARE:** tudo deste doc.

## 7. Corrida 249 pronta (08/10, offline)

| Artefato | Valor |
|---|---|
| Pacote | **3.394.1.0** |
| SYS lab | `76751BB9…` |
| SYS assinado | `AB2A2160…` |
| INF | `C1C18C1E…` |
| CAT | `528F50B3…` |
| UMD | `B73F9BDA…` (a da 248, com LOGMIN.1) |
| RADV | `B4AE2E14` (a mesma) |
| Auditorias | pacote PASS; segurança PASS com o portão novo `audit_tmr1_tmr_persistente_como_amdgpu` (5 mutações reprovam) |
| Pacote 3.393 (248b) guardado em | `driver\wdk\package\output_lab_3.393.1.0_248b\` |
| Lançadores | `tools\EXECUTAR_CORRIDA249_TMR1_PACOTE_COM_KD_MENU_600S.cmd` e `..._MENU_600S.cmd` |

**O que entrou:**
- **TMR.1** (`bc250_pfp_direct_load.c`): o `SETUP_TMR` usa o TMR persistente que o bootstrap do CP já reservava (`[0xFB00000,+4 MiB)`, papel TMR, fora do segmento); se ele não existir, aloca um persistente. PA da DRAM (`VramBaseOffset + offset`), sem fallback. A região quad do CP ficou persistente. Nada volta ao alocador. Linhas novas: `[TMR1] TMR persistente ...` e `[TMR1] PSP: ... SETUP_TMR fence/resp ... LOAD_IP_FW ... aceitos=N/6`.
- **Split do SDMA:** os dois fluxos saíram de `static` para um buffer de pool por chamada; sem pool, `[SDMA_SPLIT] RECUSADO`.
- **`SetPowerState(D0)`** devolve sucesso (teste atualizado).
- **`IhRing`** zerado no desarme do IH.
- **VRD.1:** a CPU só lê a VRAM na prova das 16 primeiras composições.
- **Sonda DCN:** Bateria A como estava. **B e C em modo só leitura** do bloco AUX do DCN (`0x15040–0x15050`); as transações antigas em `0x4800` ficaram atrás de `BC250_DCN_PROBE_AUX_TRANSFERS 0`. **Divergência declarada do pedido do Daniel (A+B+C completas):** `0x4800` não é o AUX do DCN (o próprio repositório de referência deriva o AUX em `0x15040` e chama `0x4800` de "legado" sem medição), e a sequência de escrita é a do DCE. Escrever ali seria escrever num registrador de outro bloco no meio do ensaio da trava. A Bateria B desta corrida diz se o bloco AUX real responde; com isso a escrita AUX correta vira a próxima etapa da sonda.
- **Monitor:** `tools\monitor-corrida.py` abre numa janela separada depois do preflight. Mostra CPU, RAM, memória de vídeo (PDH), Tctl (amostra de plataforma do próprio KMD), IBs/s, Presents/s, FILL/EVICT/FLUSH_TLB, TDR e faults, e avisa quando o log do KMD para de crescer. Grava `C:\BC250_MONITOR.csv` com fsync a cada 2 s. Não abre o `bc250compute`; o lançador recusa a corrida se esse serviço estiver rodando. O `MONITOR_TELEMETRIA_2S.cmd` da outra sessão não entra: a parte de hardware dele passa pelo `bc250compute` (MMIO arbitrário e o par SMN do KMD).

**Leitura da corrida, além do §6:**
- `[TMR1] PSP:` com `SETUP_TMR fence=1 resp=0x00000000` e `aceitos=6/6`;
- `[DX67_VRAM_MAPA]` com o fim do segmento abaixo da quad (`0xF8F0000`);
- `[DCN_BATTERY_A]` e `[DCN_BATTERY_B]` (bloco AUX vivo ou morto);
- CSV do monitor: Tctl e taxas no último minuto antes de qualquer trava.
