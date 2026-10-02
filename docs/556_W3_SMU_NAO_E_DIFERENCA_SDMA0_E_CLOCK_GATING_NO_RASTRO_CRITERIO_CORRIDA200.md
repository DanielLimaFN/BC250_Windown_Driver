# 556 — W3: a SMU não é a diferença; SDMA0 e clock gating no rastro; critério da Corrida 200

**Data:** 01/10/2026 · **Tipo:** offline, sem corrida · **Candidato:** lab **3.347.1.0**
**Antecede:** [doc 555](555_CORRIDA199_256MB_SOBE_TRAVA_SEM_CALOR_SEM_ERRO_DE_FABRIC.md) (Corrida 199) e [doc 552](552_COLETA_LINUX_256MB_AMDGPU_ATIVO_GABARITO_E_O_QUE_MUDA.md) (Linux).

---

## 1. W3: o que o Linux faz com a SMU, e por que isso não é a diferença

- **Fonte (P1):** `drivers/gpu/drm/amd/pm/swsmu/smu11/cyan_skillfish_ppt.c`, upstream.
  - No init, o `amdgpu` só prepara a tabela de métricas (`smu_v11_0_init_smc_tables`).
  - **Não manda mensagem de relógio nem de tensão.**
  - As mensagens `RequestGfxclk`, `ForceGfxVid` e `UnforceGfxVid` só saem quando alguém escreve em `pp_od_clk_voltage`. Quem faz isso é um governor externo.
- **Medida na coleta Linux (P0):** o dmesg só registra governors térmicos e de cpuidle; não havia governor de GPU (oberon ou cyan-skillfish).
  - `amdgpu_pm_info` mostra `SMC Feature Mask: 0xffffffffffffffff`: o firmware da SMU gerencia sozinho, com todas as features ligadas.
- **Consequência:** o doc 552 listou "o Linux gerencia relógios e tensões" como diferença contra nós. **Isso está errado como formulado.** Nos dois sistemas quem decide relógio e tensão é o firmware, sem driver mandando nada. A SMU sai da lista de suspeitos da trava (H forte).

**O que sobra como diferença com prova (P0 do inventário Linux):**
1. **Clock gating.** A máscara do Linux é `0x8090800`, com SDMA MGCG, BIF MGCG e HDP MGCG **ligados** e todo o clock gating de GFX desligado. **Não sabemos** como deixamos esses registradores.
2. **Fences por interrupção** contra o nosso polling.
3. **SDMA1:** usado pelo Linux. Na 199 confirmamos que nós o deixamos parado.

## 2. A análise da 199 que motiva o instrumento

1. **O último registro do rastro** (785,095 s desde o boot) estava completo. O próximo, 100 ms depois, nunca saiu. O log do KMD ainda escreveu até 785,269 s (QPC de 10 MHz). Ou seja, a thread PASSIVE parou primeiro e o resto do sistema morreu em seguida.
2. **Migalhas de MMIO (DX.87):** nenhuma leitura ou escrita de registrador estava em voo no último registro.
3. **A última operação** foi a preparação de um FILL de 11 MB no SDMA0. Na 198 foi GFX.

O batimento DX.90 só cobre o anel gráfico. **Não temos o estado do SDMA0 no momento da parada.** É o que a 200 mede.

## 3. O que foi feito (I.SDMA0 e I.CG), tudo somente leitura

| Registro | Período | Conteúdo | Procedência (P1) |
|---|---|---|---|
| `BSQ1` | 100 ms | SDMA0 `STATUS_REG` 0x4A14, `STATUS1` 0x4A18, `STATUS2` 0x4A60, `GFX_RB_RPTR` 0x4B8C, `GFX_RB_WPTR` 0x4B94, `GFX_IB_RPTR` 0x4BAC, `GFX_CONTEXT_STATUS` 0x4BC4 | `gc_10_1_0_offset.h`, base 0x1260 |
| `BCG1` | tick 10 e a cada 10 s | `SDMA0_POWER_CNTL` 0x49E8, `SDMA0_CLK_CTRL` 0x49EC, `SDMA1_POWER_CNTL` 0x61E8, `SDMA1_CLK_CTRL` 0x61EC, `RLC_CGTT_MGCG_OVERRIDE` 0x3B120, `RLC_CGCG_CGLS_CTRL` 0x3B124, `CGTT_SPI_CLK_CTRL` 0x3C200 | idem; base 0xA000 para idx 1 (docs/03, P1/P0) |

**Como funciona:**
- Mesmo layout de 72 bytes e mesma regra ANTES/DEPOIS do BGH1/BPS1. Roda depois deles, na mesma thread.
- A sonda lê por **tabela fixa**. Nenhuma escrita: os oito offsets novos entraram só na allowlist de **leitura**.
- O decodificador reduz `RPTR`/`WPTR` a uma volta do anel de 4096 bytes (lição da A15) e decodifica IDLE, RB_EMPTY e INSIDE_IB (`gc_10_1_0_sh_mask.h`).

**Ferramenta nova: `tools\coletar-pos-trava.ps1`.** Um comando só, elevado, no primeiro boot depois da trava:
1. motivo do reset;
2. logs da UMD de `C:\Windows\Temp` e de AppContainer;
3. caudas do log do KMD e do rastro pelos baselines;
4. decodificação e SHA-256.

Substitui os comandos soltos que falharam na 199.

## 4. Provas offline

| Item | Resultado |
|---|---|
| `build-contract.bat` | EXIT=0 |
| Build de laboratório | SYS fonte `218482003555BAEB…`, bring-up confirmado |
| Decodificador | arquivo sintético: SDMA0 ocioso e drenado; SDMA0 dentro de IB com RPTR além de uma volta (4612 contra 580) **não** drenado; BCG1 lido; ANTES final detectado |
| `audit_isdma_icg_read_only` (novo) | PASS. **4 de 4 mutações reprovam:** escrita na sonda; tabela com offset trocado; offset novo na allowlist de escrita; define com valor errado |
| Auditoria de segurança, auditoria do pacote | PASS / PASS |
| `coletar-pos-trava.ps1` | sem erro de sintaxe. **Não executado** (exige trava e elevação) |

**Pacote 3.347.1.0 (assinado, auditado, NÃO INSTALADO):**

| Arquivo | SHA-256 |
|---|---|
| SYS | `A7F3B14E1FED8233522E9F581458F54FE98D69E8FE67BE040E975D46F0BA0EA9` |
| INF | `D2D02D180ED355C8FC4A02A3891072045F9DEE1C484D2A4CE0E7366E02A47FD3` |
| CAT | `0DEB64262EBB827D82912C16D7C012CD7F4D6880F76164A84509DAD6B93F2A8C` |
| UMD | `ED025FAB…` (idêntica à da 199, não recompilada) |

- RADV `5447A17D…`, idêntica.
- `BSQ1`, `BCG1` e `BPS1` presentes no SYS; M1b ausente (dormente).
- Scripts: `tools\prepare-corrida200-sdma0-cg.ps1` e `tools\EXECUTAR_CORRIDA200_SDMA0_CG_600S.cmd`.

## 5. Corrida 200 — critério (escrito antes, Lei 10)

**Única variável contra a 199:** instrumentação somente leitura (BSQ1 e BCG1). Mesma CMOS (256), UMD e RADV.

**Preparação (Daniel):**
1. `tools\BC250_RECOVERY.cmd` elevado.
2. `tools\inspect-boot-fuse.ps1 -Reset`.
3. **Opcional, 1 minuto, fecha a origem do bit 19 do reset:**
   - com o Windows normal (BasicDisplay), aperte o **botão de reset**;
   - no boot seguinte, rode `tools\vram-cmos\bc250-vram.ps1 -Ler` elevado.
   - Bit 19 aparecendo = é o botão.
4. **Boot frio** (tomada).
5. `tools\EXECUTAR_CORRIDA200_SDMA0_CG_600S.cmd` elevado.
6. Abrir a **Pesquisa** por volta de 1 minuto (foi onde a 199 travou) e anotar a hora.
7. **Se travar:** botão de reset e, no boot seguinte, **antes de tudo**, `tools\coletar-pos-trava.ps1` elevado.

**Desfechos:**

| Desfecho | Assinatura (o que o rastro mostra) | Implicação |
|---|---|---|
| **A — sem trava** | 600 s, BSQ1 e BCG1 contínuos até o rollback | Linha de base de SDMA0 e CG, sem diagnóstico da trava. Comparar CG com o Linux e decidir o próximo passo por isso |
| **B — SDMA0 trabalhando na parada** | Trava; no **último BSQ1 DEPOIS**: `sdma0_idle=false` **ou** `sdma0_inside_ib=true` **ou** `sdma0_ring_drained=false` | A parada coincide com trabalho do SDMA0 (paginação). Próximo: casar o IB com o log do KMD; candidato que fatie FILL/TRANSFER grandes, como teste de uma variável |
| **C — SDMA0 ocioso na parada** | Trava; no último BSQ1 DEPOIS: `sdma0_idle=true`, `sdma0_rb_empty=true`, `sdma0_inside_ib=false` **e** `sdma0_ring_drained=true` | O SDMA0 não estava no meio de nada. Suspeito passa a ser o anel gráfico ou o lado da CPU (BAR/MMIO) e o fabric. Próximo: GFX no mesmo nível de detalhe, e o W1 com o BasicDisplay |
| **D — a leitura do SDMA0 não voltou** | Último registro do arquivo é um BSQ1 **ANTES** sem DEPOIS | Ler o SDMA0 travou o barramento. A sonda sai, e o próprio fato vira dado: o bloco SDMA0 estava travado |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar |

**Regras:**
- **Lei 10.3:** C exige os **quatro** sinais. Um sinal de trabalho basta para B, porque qualquer um deles contradiz "ocioso".
- Os desfechos B–D exigem trava.
- O BCG1 é registrado em **qualquer** desfecho e comparado com a máscara do Linux, decodificando pelos campos `SOFT_OVERRIDE` de `SDMA0_CLK_CTRL` em `gc_10_1_0_sh_mask.h`. **Não há previsão escrita do valor:** é a primeira leitura.

**Risco declarado:** oito leituras novas por tick, todas somente leitura e em blocos já lidos pelo driver (SDMA0) ou pelo RLC. O desfecho D existe para separar a sonda da trava.

**NÃO VALIDADO EM HARDWARE.**
