# 557 — Corrida 200: SDMA0 ocioso na parada; achado de clock gating; candidato CG.1 e critério da Corrida 201

**Corrida 200:** 01/10/2026, bind às 22:14:46. Travou com tela preta; reset; boot às 22:16:25.
**Candidato:** lab **3.347.1.0**, que é o da 199 mais `BSQ1` (SDMA0 a cada 100 ms) e `BCG1` (clock gating a cada 10 s), somente leitura ([doc 556](556_W3_SMU_NAO_E_DIFERENCA_SDMA0_E_CLOCK_GATING_NO_RASTRO_CRITERIO_CORRIDA200.md)). CMOS em 256 MB.
**Evidência:** `docs/evidence/2026-10-01_22-14-46-DESKTOP-HQAG6T4-lab-binding/`, coletada por `tools\coletar-pos-trava.ps1`, que funcionou na primeira vez:
- `display-log-this-bind-RECUPERADO.txt` (sha256 `70C008CA…`);
- `paging-trace-this-bind-RECUPERADO.bin` (`E2E3D3C3…`) e `rastro-corrida-resumo.txt`;
- `reset-reason-pre-bind.txt` e `reset-reason-pos-trava.txt` (`25DE40F8…`);
- 13 logs da UMD do fim da corrida em `umd-logs-pos-travamento\`.

---

## 1. Relato obrigatório

**1. Houve avanço?** Sim, de diagnóstico; funcionalmente não (travou de novo). O critério da 200 foi respondido sem ambiguidade, e apareceu a primeira diferença de configuração **nossa** contra o Linux.

**2. Sucessos (P0):**
- 253 Presents, 5 `BYTE_PROOF PASS`, 1.032 `IB EXECUTADO`, 957 FILLs.
- Zero TDR, `GFXHUB_FAULT`, `DEVICE_REMOVED` e `IB_TRIPOD TIMEOUT`.
- Rastro completo até a parada: 535 ticks; BGH1, BSQ1, BPS1 e BCG1 com leitura completa (`read_mask=0x7F`) depois do tick 10.
- Tctl de 77,4 a 84,3 °C, caindo até o fim.
- A coleta pós-trava trouxe pela primeira vez os logs da UMD do **fim** da corrida.

**3. Falhas:**
- **Trava dura**, ~59 s de rastro depois do início do driver. Tela preta, sem BugCheck.
- **Falta de memória logo antes:** `vkAllocateMemory (Image) res=-2` no `SystemSettings.exe` às 22:15:38 (3,5 MB) e duas vezes no `dwm.exe` às 22:15:51 (1 MB e 512 KB). A última linha da UMD do DWM é de 22:15:53.
- Processos com device D3D na corrida: DWM, Explorer, ShellExperienceHost, TextInputHost, SystemSettings, msedge e Claude. É mais que o roteiro de "só o menu Iniciar": falta confirmar o que foi aberto à mão e o que abriu sozinho.
- 6 `Journal NO_SPACE`.

**4. Veredito frente ao doc 556 §5: C — SDMA0 ocioso na parada.** Os quatro sinais estão presentes no último BSQ1 DEPOIS (tick 535):
- `IDLE=1` e `RB_EMPTY=1` (`STATUS=0x46DEED57`);
- `INSIDE_IB=0`;
- anel drenado (`RPTR=WPTR=0x14F44`), com `STATUS1=0x3FF`.

O anel gráfico também estava ocioso: `CP_STAT=0`, zero pendente, fence em dia.

**Ressalva de janela:** o log do KMD foi até 184 ms depois do último BSQ1 (174,848 contra 174,664 s), e nesse intervalo ainda **concluiu** um FILL (#1032) e uma submissão gráfica (#134 retirada). O sistema parou com o trabalho conhecido terminado. Não houve D: o último registro está completo.

**5. Recuperação e pendências:**
- BC-250 em Código 43 pelo fusível, com o 3.347.1.0 instalado. Antes de qualquer bind: `BC250_RECOVERY.cmd`, `inspect-boot-fuse.ps1 -Reset` e boot frio.
- **NÃO VALIDADO EM HARDWARE:** a causa da trava.

## 2. Leitura

### 2.1 O motivo de reset fecha o bit 19 (P0, com uma confirmação pendente)

| Leitura | Valor | Novo |
|---|---|---|
| 22:12:05 | `0x40000400` | nada |
| 22:14:08 | `0x40080400` | bit 19 |
| 22:14:23 (pré-bind) | `0x40000400` | nada |
| 22:17:18 (pós-trava) | `0x40080400` | bit 19 |

Se entre 22:12 e 22:14 o Daniel fez o teste do botão de reset com o Windows normal, **o bit 19 é o botão de reset** (P0 com essa confirmação). Então nenhuma das duas travas registrou erro próprio no FCH.

**Atenção:** nesse caso a 200 rodou depois de um reset **quente**, não de boot frio. Registro como desvio do protocolo, não como causa: a 199, com boot frio, travou do mesmo jeito.

### 2.2 Clock gating: uma diferença criada por nós (P0 nos valores, P1 nos campos e no Linux)

| Registrador | Nós | Significado (`gc_10_1_0_sh_mask.h`) | Linux (doc 552) |
|---|---|---|---|
| `SDMA0_CLK_CTRL` | `0xFF0C0100` | `SOFT_OVERRIDE0..7 = 1`: **MGCG desligado** | SDMA MGCG **On** |
| `SDMA1_CLK_CTRL` | `0x000C0100` | overrides em 0: MGCG ligado (**padrão do POST**, ninguém toca) | — |
| `RLC_CGTT_MGCG_OVERRIDE` | `0xFFFFFFFF` | GFX MGCG desligado | Off |
| `RLC_CGCG_CGLS_CTRL` | `0x0001003C` | CGCG_EN=0, CGLS_EN=0 | Off |
| `SDMA0/1_POWER_CNTL` | `0x40000050` | PG desligado, sem light sleep | — |

**O Linux não mexe em nada disso na Cyan Skillfish (P1):**
- `nv.c`: `cg_flags = 0; pg_flags = 0;` para GC 10.1.3/10.1.4;
- `amdgpu_discovery.c`: SDMA0/1 = `IP_VERSION(5, 0, 1)`;
- `sdma_v5_0_set_clockgating_state` só age em 5.0.0/5.0.2/5.0.5, e não há outra escrita em `SDMA0_CLK_CTRL`.

**Logo, a máscara `0x8090800` do Linux é o estado do POST.** O nosso SDMA1 confirma: está com o mesmo padrão.

**Quem desliga o MGCG do SDMA0 somos nós:**
- `bc250_sdma_present.c` faz `CLK_CTRL |= 0xFF000000` no init do anel e duas vezes no restart pós-TDR. Foi o "des-gating antes de qualquer configuração" de 29/08, por causa do `EX_IDLE_POLL_TIMER_EXPIRE` persistente.
- Nunca era desfeito.
- A transação do F32 (`bc250_sdma_f32_transaction.c`) restaura o próprio valor e não é a responsável.

Que isso cause a trava é **H, e fraca**: clock forçado ligado costuma ser o estado mais seguro. Mas é a **única** diferença de configuração medida contra uma referência que não trava, o teste custa uma variável, e os dois resultados informam.

### 2.3 O que a trava tem em comum, agora com três corridas

| | 198 | 199 | 200 |
|---|---|---|---|
| Última operação conhecida | GFX (Node 1) | preparação de FILL de 11 MB | FILL concluído e GFX retirado |
| Engines no último registro | — | SDMA0 não medido | **SDMA0 e GFX ociosos** |
| Calor | — | 80 °C | 77 °C |
| Erro no FCH | — | nenhum novo | nenhum novo |
| Falta de memória antes | — | não medida no fim | **sim, 2 s antes (DWM)** |

**H nova, a medir:** a trava acompanha a pressão de memória com 256 MB (despejos e preenchimentos de paginação em sequência), não um comando específico. Contra ela: o Linux aguentou 149 despejos completos com 256 MB. A favor: as duas corridas com 256 MB travaram em 1–2 minutos de uso, e a 200 travou logo depois de o DWM ficar sem memória.

## 3. Candidato CG.1 (lab 3.348.1.0, assinado, auditado, NÃO INSTALADO)

**Mudança única:** os `SOFT_OVERRIDE` do `SDMA0_CLK_CTRL` voltam ao valor do POST **depois do anel provado**.
- Valor de entrada capturado uma vez, antes do primeiro force.
- Restauração no init só no ramo `TripodValidated = TRUE`, e no restart pós-TDR só depois do NOP de prova.
- Só nos bits de override (Lei 6), com releitura e log `[SDMA_CG]`.
- O force durante a configuração continua, como em 29/08.
- Define `BC250_SDMA0_RESTORE_POST_CLOCK_GATING`, em 1 neste candidato.

**Provas offline:**
- `build-contract` EXIT=0.
- `audit_cg1_sdma0_post_clock_gating` novo: **4 de 4 mutações reprovam** (define em 0; restauração que zera em vez de usar o valor de entrada; restauração do restart removida; restauração do init removida).
- Auditoria de segurança e auditoria do pacote: PASS.
- `[SDMA_CG]` está no SYS assinado.

| Arquivo | SHA-256 |
|---|---|
| SYS | `F45986ABE6609532D2830690EC3B3359C4DB25DDB7245369F70BCFA8240DB31C` |
| INF | `F03FF0ED539714D0C91376D009534BCFB990C83EFE06DDDC216F029CC26E7D4D` |
| CAT | `7241954B1EA6748960B85A1F899C5F0E2DB409B3BA8B8BC72F8D6FCF5FA22A77` |
| UMD | `ED025FAB…` (a mesma da 199/200) |

RADV `5447A17D…`. Scripts: `tools\prepare-corrida201-sdma0-mgcg-post.ps1` e `tools\EXECUTAR_CORRIDA201_SDMA0_MGCG_600S.cmd`.

## 4. Corrida 201 — critério (escrito antes, Lei 10)

**Única variável contra a 200:** o MGCG do SDMA0. A instrumentação é a mesma.

**Preparação:**
1. `BC250_RECOVERY.cmd`.
2. `inspect-boot-fuse.ps1 -Reset`.
3. **Boot frio de verdade (tomada)**, sem reset quente depois dele.
4. `EXECUTAR_CORRIDA201_SDMA0_MGCG_600S.cmd`.
5. **Mesmo uso da 200**, para comparar: abrir só o menu Iniciar e a Pesquisa.
6. Se travar: `tools\coletar-pos-trava.ps1` antes de tudo.

**Desfechos:**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A — o MGCG resolve** | `[SDMA_CG] INIT: … OVERRIDES DO POST RESTAURADOS`, BCG1 com `sdma0_clk_ctrl` igual ao SDMA1 (`0x000C0100`), **e** 600 s sem trava, **com** falta de memória (`res=-2`) acontecendo como na 200 | Clock gating do SDMA0 vira causa provável. Repetir uma vez antes de fechar (n=1) |
| **B — o MGCG não muda a trava** | Restauração confirmada (as duas linhas acima) **e** trava como na 200 | CG.1 refutada como causa. Fica como alinhamento ao Linux, e o próximo suspeito é a pressão de memória (§2.3): M1b (doc 554) ou corrida que meça a trava com a carga mínima |
| **C — o SDMA0 não aguenta o MGCG** | Restauração confirmada e depois `IB_TRIPOD TIMEOUT` ou `TDR_QUIESCE` com SDMA0 não drenado, sem trava dura | O problema de 29/08 voltou: o SDMA0 precisa do clock forçado com a nossa programação. Reverter CG.1 e investigar o porquê |
| **D — a restauração não pegou** | `[SDMA_CG] … RELEITURA DIVERGE`, ou BCG1 ainda com `0xFF0C0100` | A variável não foi aplicada; a corrida não testa nada |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar |

**Regras:**
- **Lei 10.3:** A exige os três sinais. Em especial, 600 s sem trava **sem** pressão de memória **não** é A: a 199 e a 200 travaram justamente sob ela.
- **Risco declarado:** o desfecho C pode virar `STATUS_IO_DEVICE_ERROR` e BugCheck `0x119`. Isso é resultado do ensaio, não falha do protocolo.

**NÃO VALIDADO EM HARDWARE.**
