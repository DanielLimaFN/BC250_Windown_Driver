# BC250-V3 — Análise das Referências da Comunidade e Plano Real de Correção

**Data:** 2026-08-22
**Alvo:** AMD BC-250 (Cyan Skillfish / Oberon, `PCI\VEN_1002&DEV_13FE`)
**Base analisada:** `BC250-V3` (driver atual) + `referencias/AMD-BC-250-Windows-Driver` + `referencias/BC250-windowsDriverTest`
**Evidência:** coletada na máquina alvo com o driver `oem5.inf` v1.0.822.39 carregado (`CM_PROB_NONE`)

---

> **ATUALIZAÇÃO 2026-08-22 (pós-Fase 0).** A execução da Fase 0 corrigiu duas
> conclusões deste documento. Ambas estão marcadas em linha abaixo, e o relatório
> completo está em [FASE0_RESULTADO.md](FASE0_RESULTADO.md):
>
> 1. **A tabela de registradores estava errada** — inclusive os offsets que eu
>    tratei aqui como "confirmados no silício" na §2.1. Derivados corretamente de
>    `gc_10_1_0_offset.h`, `GRBM_STATUS` é `0x08010` (lê `0x00003028`, não zero),
>    `SPI_PG_ENABLE_STATIC_WGP_MASK` é `0x0935C` (lê `0x0000FFFF` e **aceita
>    escrita** — não é SOS-locked) e `CC_GC_SHADER_ARRAY_CONFIG` é `0x089BC`.
> 2. **A SDMA0 não está validada.** Com verificação de conteúdo byte a byte, a
>    cópia não move dado nenhum. Os "12 GB/s" eram tamanho ÷ latência de submissão.

## 0. Sumário executivo

Três conclusões que mudam o rumo do projeto:

1. **O bloco GC está vivo e alcançável.** O teste de scratch (`0x32D4`) grava e lê de volta. O problema nunca foi "GPU morta" nem aperture errada.
2. **A falha do PSP não é bug nosso.** Nosso protocolo GPCOM está byte-a-byte correto e idêntico ao do projeto de referência que funciona. O SOS da BC-250 simplesmente **não tem itens de firmware** para CP/RLC/SDMA — `LOAD_IP_FW` só aceita `fw_type=18` (SMU). Isso é confirmado no Linux: `psp_v11_0_8.c` não tem `load_ip_fw` nenhum.
3. **Estamos rodando uma sequência SMU documentada como perigosa.** `wddm_hw.c:552` envia `ForceGfxFreq (0x39)` sem tabelas DPM carregadas — o projeto de referência marca isso como "**DANGER! Causes crash without DPM tables!**".

O caminho real para compute/gráficos na BC-250 sob Windows **não passa por PSP LOAD_IP_FW**. Passa por: **DPM do SMU → firmware por caminho direto → fila MQD/HQD → PM4 MAP_QUEUES**. É o que o Linux faz, e é o que nenhum dos três projetos (incluindo o nosso) implementou ainda.

---

## 1. Perfil dos dois projetos de referência

### 1.1 `AMD-BC-250-Windows-Driver` (Keshas-dev)

| Aspecto | Descrição |
|---|---|
| Tipo | Driver WDM de controle/IOCTL (não é miniport WDDM real no Win11 26100) |
| Foco | Engenharia reversa de silício: PSP, SMU, SMN, mapas de registrador |
| Volume | ~60 ferramentas de teste em `test-tools/`, 1871 linhas de notas em `AGENTS.md` |
| Valor para nós | **Altíssimo.** É a fonte primária de todos os offsets e descobertas de PSP |
| Fraqueza | Arquitetura fraca (WDM IOCTL), muitas conclusões contraditórias e revertidas ao longo do tempo |

Metodologia notável: o `AGENTS.md` abre com um aviso de **"Historical-data caution"** — trata conclusões antigas como *hipóteses a re-verificar*, não fatos, e exige re-execução dos testes após cada correção. Várias conclusões "definitivas" deles foram depois revertidas (ex: "PSP ring permanentemente indisponível" era offset errado).

### 1.2 `BC250-windowsDriverTest`

| Aspecto | Descrição |
|---|---|
| Tipo | Miniport full-WDDM (`DxgkInitialize`) + UMD D3D9/10/11/12, tudo atrás de gates |
| Foco | Arquitetura correta e **honestidade de estado** |
| Volume | Código enxuto + `docs/upstream/` com o amdgpu do Linux inteiro (gfx_v10_0.c, psp_v11_0.c, smu_v11_0.c, cyan_skillfish_ppt.c…) |
| Valor para nós | **Alto** em disciplina de engenharia e como fonte Linux offline |
| Fraqueza | Nunca rodou em hardware real. Tudo é "preparado", nada é comprovado |

Metodologia notável: gates de compilação (`BC250_GFX_OFFSETS_VALIDATED=0`, `BC250_PSP_RING_VALIDATED=0`) que **impedem fisicamente** o driver de reportar sucesso enquanto o offset não foi confirmado no silício. E este comentário, que é exatamente a regra que a nossa V3 quebra:

```c
/* src/gfx/bc250_gfx.c:499 */
/* Do not write the completion value from the CPU. QueryCurrentFence and
 * the DPC observe only the value written by the GPU-side WRITE_DATA packet. */
```

---

## 2. Medições novas feitas hoje no nosso hardware

Ferramenta escrita para esta análise, usando os escapes `READ_REG`/`WRITE_REG` da própria V3.

### 2.1 Leitura de registradores

> ⚠️ **Esta tabela usa os offsets antigos e está superada.** Ela foi montada com
> os valores do projeto de referência, que também estão errados. Os offsets
> corretos e as leituras reais estão no Apêndice A e em `FASE0_RESULTADO.md`.
> Mantida aqui para registro de como a conclusão errada foi alcançada.


| Registrador | Offset | Valor | Interpretação |
|---|---|---|---|
| `CP_SCRATCH` | `0x32D4` | `0x4D585042` | **Grava e lê de volta → bloco GC alcançável** |
| `GRBM_STATUS` | `0x3260` | `0x00000000` | Idle permanente |
| `GRBM_STATUS2` | `0x326C` | `0x00000000` | Idle permanente |
| `GRBM_GFX_INDEX` | `0x34D0` | `0xE0000000` | Broadcast ativo (nosso write) |
| `SPI_PG_ENABLE_STATIC_WGP_MASK` | `0x5C3C` | `0x00000000` | **SOS-locked** (write de `0x1F` não gruda) |
| `SPI_PG_MASK` | `0x34FC` | `0xF7F77F80` | Parcialmente gravável (registrador diferente) |
| `CC_GC_SHADER_ARRAY_CONFIG` | `0x9C1C` | `0x1F000000` | **Gravável** |
| `RLC_PG_ALWAYS_ON_WGP_MASK` | `0x3D64` | `0xFFFFFFFF` | Locked |
| `PSP_C2PMSG_64` | `0x58200` | `0x80020000` | **TOS ready + eco do ring KM** |
| `PSP_C2PMSG_81` | `0x58244` | `0x00650C60` | SOS vivo |
| `PSP_C2PMSG_35` | `0x5818C` | `0xFFFFFFFF` | Bootloader completou |
| `CP_STAT` (nosso `A_CP_STAT`) | `0x8684` | `0xDEF3DEF3` | **Padrão de fill → offset inválido** |
| — | `0x8688` | `0xDEF0DEF0` | Padrão de fill |

### 2.2 Estado do CP Ring 0 programado pela V3

| Registrador | Offset | Valor lido | Comentário |
|---|---|---|---|
| `CP_RB0_BASE` | `0xC100` | `0xF40FE000` | **Nosso valor gravou** |
| `CP_RB0_CNTL` | `0xC104` | `0x50000709` | Nosso valor gravou (write-stick confirmado) |
| `CP_RB0_RPTR_ADDR` | `0xC10C` | `0x0FE01000` | Nosso valor gravou |
| `CP_RB0_RPTR_ADDR_HI` | `0xC110` | `0x000000F4` | Nosso valor gravou |
| `CP_RB0_WPTR` | `0xC150` | `0x00000008` | Nosso WPTR do teste PM4; **read-only pelo host** |
| `A_CP_RB0_RPTR` (nosso) | `0x8700` | `0x00000000` | **Sempre 0 — offset suspeito** |
| `CP_RING0_RPTR` (alias ref.) | `0xDA6C` | `0x01200000` | Valor vivo, **constante em 8 amostras** |
| `CP_RING0_BASE_LO` (alias ref.) | `0xDA60` | `0x00000000` | Read-only, write ignorado |

**Leitura disso:** a programação do ring da V3 **está grudando** no aperture `0xC1xx`. O ring existe, o base address está lá, o WPTR avançou. O CP simplesmente **não executa**, porque não tem microcódigo.

### 2.3 Teste de write-stick

| Registrador | Escrito | Resultado |
|---|---|---|
| `CP_RB0_CNTL` (`0xC104`) | `0x0000110B` | **GRAVOU** |
| `CP_RB0_WPTR` (`0xC150`) | `0x000000AA` | IGNORADO (read-only pelo host) |
| `CP_RING0_WPTR` (`0xDA78`) | `0x000000AA` | GRAVOU |
| `CP_RING0_BASE_LO` (`0xDA60`) | `0xDEADB000` | IGNORADO (read-only) |
| `SPI_PG_STATIC_WGP` (`0x5C3C`) | `0x1F` | **IGNORADO — SOS-locked** |
| `CC_GC_SHADER_ARRAY` (`0x9C1C`) | `0x0` | GRAVOU |

---

## 3. O que cada descoberta significa para a V3

### 3.1 PSP — nosso protocolo está certo, o SOS é que não coopera

A V3 já absorveu as descobertas críticas:

| Item | Referência | Nossa V3 | Status |
|---|---|---|---|
| MP0 base | `0x58000` | `wddm_hw.c:34` `MP0_BASE 0x58000` | ✅ igual |
| C2PMSG 64/67/69/70/71/81 | `+0x200/20C/214/218/21C/244` | idênticos | ✅ igual |
| `psp_gfx_cmd_resp` | `buf_size@+0`, `version@+4`, `cmd_id@+8`, união `@+28`, resp `@+864` | `PSP_CMD_BUF_DECLARED 0x400`, `PSP_RESP_OFFSET 864` | ✅ igual |
| Frame de 64 B, WPTR `+16` dw | sim | sim | ✅ igual |
| Ring KM tipo 2 | sim | `PSP_RING_TYPE_KM << 16` | ✅ igual |

E o resultado no hardware bate exatamente com o deles:

| `fw_type` | Nossa V3 | Referência (hardware) |
|---|---|---|
| SDMA0 (9), RLC_G (8), CE (3), PFP (2), ME (1), MEC (4) | `NTSTATUS 0xC0000483` (resp≠0) | `0xFFFF0008` = `TEE_ERROR_ITEM_NOT_FOUND` |
| SMU (18) | **nunca tentado** | `0x00000000` = `TEE_SUCCESS` |

> **Conclusão:** parar de tratar `LOAD_IP_FW` para CP/RLC/SDMA como bug. Não é. É o SOS da BC-250 que não implementa esses itens — igual ao Linux, que carrega esses firmwares por outro caminho.

**Bug real que temos:** não expomos `resp[0]` (o código TEE) em lugar nenhum. `Hw_Fw_*` no `diag.ps1` mostra só o NTSTATUS genérico. Perdemos a informação mais importante do diagnóstico.

### 3.2 Caminho direto C2PMSG_35/36/37 — 8/8 PASS na referência, ausente na V3

O projeto de referência tem um **segundo** caminho de firmware que a V3 não tem:

```c
/* referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_psp.c:191 */
WRITE_REGISTER_ULONG(base + 0x58190, paLo);          /* C2PMSG_36 */
WRITE_REGISTER_ULONG(base + 0x58194, paHi);          /* C2PMSG_37 */
WRITE_REGISTER_ULONG(base + 0x5818C, 0x06 | (FwType << 16));  /* C2PMSG_35 */
/* poll C2PMSG_35 bit31 */
```

Resultado deles no hardware (2026-08-21): **8/8 PASS** — CE, PFP, ME, MEC, MEC2, RLC, SDMA0, SDMA1, todos `Result=1`, `C2PMSG_35=0xFFFFFFFF`.

E o nosso `C2PMSG_35` **já lê `0xFFFFFFFF` agora** (§2.1) — o bootloader está em estado de conclusão. Este caminho está aberto para nós.

⚠️ Ressalva honesta: "8/8 PASS" ali significa *o mailbox aceitou o comando*, não que o CP passou a executar. Eles próprios registram que mesmo com firmware "carregado" não houve atividade de engine. Precisa ser validado com o teste de scratch/PM4 real, não com o código de retorno.

### 3.3 IC_BASE DMA — funciona, mas causa BSOD 0x1A

A referência implementou o caminho `AMDGPU_FW_LOAD_DIRECT` do Linux (upload de microcódigo via `CP_HYP_*_UCODE_ADDR/DATA` em `0x172B0..0x172C4`) e **desativou deliberadamente**:

```c
/* amdbc250_dream_fw_load.c:231 */
/* We deliberately do NOT use the host IC_BASE DMA path: on BC-250 the host
 * cannot un-halt the CP after loading microcode — doing so makes the GPU run
 * the loaded firmware and perform a rogue host DMA write, which corrupts
 * system memory (0x1A MEMORY_MANAGEMENT). */
```

**Regra de segurança para nós:** nunca fazer unhalt do CP (`CP_ME_CNTL = 0`) depois de carregar microcódigo pelo host sem antes ter GPUVM/page tables configuradas. A V3 hoje faz `BcRegWrite(Dev, A_CP_ME_CNTL, 0)` em `wddm_hw.c:694` — hoje é inofensivo porque não há microcódigo, mas vira um BSOD garantido no momento em que o firmware carregar.

### 3.4 SMU — estamos usando mensagens marcadas como perigosas

Da lista de mensagens `smu_v11_8_ppsmc.h` documentada pela referência:

| Msg | Nome | Nota da referência | V3 usa? |
|---|---|---|---|
| `0x39` | `ForceGfxFreq` | **"DANGER! Causes crash without DPM tables!"** | ✅ **SIM** — `wddm_hw.c:552`, com 1500 MHz |
| `0x3B` | `ForceGfxVid` | **"DANGER!"** | não |
| `0x2C` | `SetCoreEnableMask` | não usar sem DPM | só na CLI (`unlock-cpu`) |
| `0x18` | `RequestActiveWgp` | timeout sem DPM | ✅ SIM — `wddm_hw.c:506` |
| `0x37` | `GetGfxFrequency` | seguro | ✅ SIM |
| `0x35/0x36` | `SetSoftMin/MaxCclk` | seguro | não |

E a causa raiz que eles identificaram: **sem DPM inicializado, o GFX fica em deep sleep a ~15 MHz** e não processa rings, mesmo que os registradores pareçam legíveis. Nossa telemetria reporta 1500 MHz — mas isso é o resultado do `ForceGfxFreq`, um clock forçado sem tabela de DPM por baixo, não um DPM funcional.

### 3.5 WGPs — SOS-locked confirmado no nosso silício também

`SPI_PG_ENABLE_STATIC_WGP_MASK (0x5C3C)` lê `0` e escrita de `0x1F` não gruda — **idêntico** ao resultado deles. Eles testaram exaustivamente e rejeitaram a hipótese de "bank select errado" (`smn-gc-alias-scan v2`, todas as combinações de `GRBM_GFX_INDEX`).

**Mas a conclusão deles de "fusível queimado" está errada** — e eles mesmos registram a contradição: no Linux o mesmo silício reporta `SE 2, SH per SE 2, CU per SH 10, active_cu_number 24`. As 24 CUs base funcionam no Linux. O que está travado no Windows é o *unlock* para 40 CUs, não as 24 base.

A diferença que eles levantam: o Linux carrega em initramfs, **antes** do SOS aplicar o lock; o Windows carrega depois. Isso é hipótese, não fato provado.

### 3.6 MQD / HQD — a peça que ninguém implementou

O ponto mais valioso do `NEXT-STEPS-PLAN.md` deles. A forma correta de ativar uma fila de compute na RDNA1 **não é escrever registradores `COMPUTE_*` via MMIO** (o que a V3 faz em `wddm_hw.c:1390-1417`). É:

1. Alocar um **MQD** (Memory Queue Descriptor, 768 B, `struct v10_compute_mqd`) em memória de sistema GTT
2. Preencher: `header=0xC0310800`, `cp_hqd_persistent_state=0x8000014C`, `cp_hqd_pq_base_lo = ring_addr >> 8`, `cp_mqd_base_addr_lo = mqd_addr` (auto-ponteiro), `cp_hqd_active=1`
3. Programar `CP_HQD_*` via MMIO na ordem certa (`CP_HQD_PQ_BASE` em `0x9124`, **não** `KIQ_BASE_LO 0xE060`)
4. Alocar buffer **EOP**
5. Submeter PM4 `SET_RESOURCES` (8 dw) e `MAP_QUEUES` (7 dw, `ENGINE_SEL=1`) pelo ring do KIQ
6. Só então a MEC lê o MQD e a fila fica viva

Sem `MAP_QUEUES`, a MEC nunca sabe que a fila existe. É por isso que nosso `DISPATCH_INITIATOR` "aceita" o comando e nada acontece.

---

## 4. Diagnóstico revisado da V3

O que muda em relação à análise anterior:

| Antes | Correção com os dados novos |
|---|---|
| "PSP rejeita os 7 microcódigos — bloqueio raiz a consertar" | O PSP **não pode** carregá-los. Não há conserto por esse caminho. O bloqueio raiz é DPM + MQD |
| "GRBM=0 / CP_STAT=0xDEF0DEF0 sugere bloco GC desligado" | O bloco GC **está alcançável** (scratch grava). `0xDEF0DEF0` é offset inválido nosso, não bloco morto |
| "Ring programado mas CP não consome" | Confirmado e refinado: base/cntl/rptr_addr **gravaram** em `0xC1xx`. Falta microcódigo, não offset |

O que **permanece igual**:

- Telemetria SMU: genuinamente validada no silício (tabela de métricas por DMA, valores variando)
- ~~SDMA0: genuinamente validada~~ **RETRATADO na Fase 0.** Com poison no destino e
  comparação byte a byte, a cópia não transfere nada: o RPTR alcança o WPTR, a
  fence **não** é escrita, e o destino permanece intacto. O critério de conclusão
  do driver era `fence OU rptr`, e o ramo `rptr` bastava para devolver sucesso
- Compute/PM4/fence: **NÃO VALIDADO EM HARDWARE**
- Fallback de CPU em `wddm_hw.c:1455`, `clBuildProgram` mentiroso, ADL fabricando 48 °C/42 W: **violações ativas da regra**
- KMDOD display-only: nenhuma DDI de render existe

---

## 5. Plano real, em fases, com critérios de aceite

Cada fase só é declarada concluída com a **prova física** listada. Sem prova → "NÃO VALIDADO EM HARDWARE".

---

### FASE 0 — Purga de mentiras (1–2 dias, zero risco)

Nada disso depende de hardware. É pré-requisito de tudo, porque enquanto o código mente não dá para medir progresso.

| # | Ação | Arquivo |
|---|---|---|
| 0.1 | Remover o loop `pC[k] = pA[k] + pB[k]` | `wddm_hw.c:1440-1459` |
| 0.2 | `clBuildProgram` retornar `CL_BUILD_PROGRAM_FAILURE` com log honesto ("sem compilador GFX10") | `opencl/bc250_opencl.c:865` |
| 0.3 | `clEnqueueNDRangeKernel` retornar `CL_INVALID_PROGRAM_EXECUTABLE` enquanto não houver ISA real | `opencl/bc250_opencl.c:1129` |
| 0.4 | ADL: devolver `ADL_ERR` em vez de `48000`/`42` | `adl/atiadlxx.c:528,545,563` |
| 0.5 | Fence de compute: parar de usar contador de CPU; expor `FenceObserved` lido da memória | `wddm_hw.c:1435` |
| 0.6 | OpenCL: reportar `global_mem_size` a partir do BAR/GTT medidos, não `16ULL<<30` | `opencl/bc250_opencl.c:51,110` |
| 0.7 | Expor `resp[0]` (código TEE) e `resp[1]` (session) por FW no `diag.ps1` | `wddm_driver.c` + `wddm_hw.c:347` |
| 0.8 | Corrigir `A_CP_STAT` — `0x8684` lê fill pattern. Achar o offset real ou remover o campo | `wddm.h:541` |
| 0.9 | Renomear `Hw_SmuGfxOff` → `Hw_SmuGetGfxFreqRsp` (o nome atual é enganoso) | `wddm_driver.c:749` |
| 0.10 | Inverter todas as asserções da suíte: falhar quando `RPTR != WPTR`, quando `GRBM` não muda, quando `Duration == 0` | `tests/*.c` |

**Aceite:** a suíte inteira roda e **falha** honestamente. Nenhum teste imprime "100% OPERACIONAL" hoje.

---

### FASE 1 — Segurança e detecção real (2–3 dias, risco baixo)

| # | Ação |
|---|---|
| 1.1 | **Remover `ForceGfxFreq (0x39)`** do bring-up. Substituir por `GetGfxFrequency (0x37)` + `SetSoftMin/MaxCclk (0x35/0x36)` |
| 1.2 | Remover `RequestActiveWgp (0x18)` do bring-up (timeout garantido sem DPM) |
| 1.3 | Detecção real: CPU cores (`KeQueryActiveProcessorCountEx`), BAR0/ReBAR (já temos `pDesc->u.Memory.Length`), GTT, e **parar de assumir 3 WGP/SA** quando `SPI_PG` lê 0 — reportar `CuCountDetected=0, CuCountAssumed=24, Source=ASSUMED` |
| 1.4 | Implementar disable de GFXOFF por SMN (`0x03B10528` bit 2 / `0x03B10998`) com readback antes/depois registrado |
| 1.5 | Chaves de registro fail-closed (a Fase 1C do nosso roadmap): `HwInitMaxStep`, `HwUnhaltCp=0` por padrão, `HwInitFirmware`, `DisplayWritesEnabled` |
| 1.6 | Guarda dura: **nunca** escrever `CP_ME_CNTL=0` (unhalt) sem `HwUnhaltCp=1` explícito e GPUVM pronta |

**Aceite:** `diag.ps1` mostra `GfxOffFeatureLow` antes/depois com o bit 2 limpo; nenhum valor de configuração é reportado como "detectado" sem ter sido lido.

---

### FASE 2 — DPM do SMU (1–2 semanas, risco médio) ← **o desbloqueio real**

Esta é a fase que a referência aponta como Fase 1 e que ninguém completou.

1. Obter a **PPTable** da BC-250. Três fontes possíveis:
   - extrair de `cyan_skillfish2_smc.bin` (header de firmware v2.0/v2.1)
   - `struct` em `docs/upstream/deep/smu11_driver_if_cyan_skillfish.h` (já temos, na referência)
   - dump de um sistema Linux funcionando
2. Alocar a tabela em memória contígua e enviar:
   - `0x04` `SetDriverTableDramAddrHigh`
   - `0x05` `SetDriverTableDramAddrLow`
   - `0x07` `TransferTableDram2Smu` (param=0)
3. `0x35 SetSoftMinCclk` / `0x36 SetSoftMaxCclk`
4. `0x3D GetEnabledSmuFeatures` para confirmar quais features acenderam

**Aceite (prova física):**
- `GetGfxFrequency (0x37)` sobe de forma **reativa** — clock varia com carga, não travado por `Force`
- `GetEnabledSmuFeatures` retorna máscara diferente de zero
- `QueryActiveWgp (0x1E)` retorna **> 0** (hoje retorna 0 sempre)
- Telemetria mostra potência subindo sob carga

Se `QueryActiveWgp` continuar 0 depois de DPM real, aí sim temos evidência forte de lock/fusível — e não antes.

---

### FASE 3 — Firmware por caminho viável (1 semana, risco médio-alto)

Ordem de tentativa, do mais seguro ao mais arriscado:

1. **PSP ring, só SMU (`fw_type=18`)** — comprovadamente funciona. Baixo risco.
2. **Direct C2PMSG_35/36/37** para CE/PFP/ME/MEC/MEC2/RLC/SDMA. Precisa de `wddm_fw.h` ou dos `.bin` em `\SystemRoot\System32\drivers\bc-250\`. Risco: baixo enquanto o CP estiver halted.
3. **IC_BASE DMA** (`0x172B0..0x172C4`) — só se 1 e 2 falharem, e **com o CP mantido halted**.

**Aceite (prova física, esta é a que importa):**
- Escrever um valor conhecido em `CP_SCRATCH (0x32D4)` pelo host
- Submeter um PM4 `SET_UCONFIG_REG` ou `WRITE_DATA` que grava **outro** valor no mesmo scratch
- Ler o scratch: se mudou para o valor do pacote → **o CP executou microcódigo**. É a prova mais barata e mais definitiva que existe.
- Em paralelo: `RPTR` alcançando `WPTR`, e `GRBM_STATUS` saindo de 0 durante a submissão

---

### FASE 4 — Fila MQD/HQD e PM4 real (2–3 semanas, risco alto)

1. `amdbc250_dream_mqd.c` equivalente: `struct v10_compute_mqd` (768 B) em GTT
2. Buffer EOP
3. Programação `CP_HQD_*` na ordem do `gfx_v10_0.c` (temos o fonte em `referencias/BC250-windowsDriverTest/docs/upstream/gfx_v10_0.c`, 472 KB)
4. KIQ ring + PM4 `SET_RESOURCES` + `MAP_QUEUES` (`ENGINE_SEL=1`)
5. Só então `DISPATCH_DIRECT` — **por pacote PM4, não por MMIO**
6. `COMPUTE_PGM_RSRC1/RSRC2` calculados de verdade (hoje são `0`, o que zera o `USER_SGPR count` e torna o dispatch impossível mesmo com tudo o resto certo)

**Aceite:** shader mínimo em ISA GFX10 escrito à mão (`v_mov_b32` + `global_store_dword` + `s_endpgm`), montado offline, gravado em VRAM, despachado por PM4, e o valor aparecendo no buffer de saída. Fence escrita pelo `WRITE_DATA` da GPU. `GRBM_STATUS` mudando. Clock/potência reagindo.

---

### FASE 5 — Full WDDM miniport (1–2 meses)

Só depois da Fase 4 comprovada. `DxgkInitialize` + 3 nós + `QUERYSEGMENT` + `BuildPagingBuffer` + `SubmitCommand` + fences monitoradas. O `BC250-windowsDriverTest` já tem o esqueleto dessas DDIs escrito — dá para portar a estrutura em vez de escrever do zero.

⚠️ Nota da referência: `DxgkInitialize` **não é export de `dxgkrnl.sys`** — vem de `displib.lib`. Eles perderam tempo com isso.

### FASE 6 — APIs gráficas

Ordem de menor risco (recomendação do `CAPABILITY_ASSESSMENT.md`, com a qual concordo):
**compute/copy validado → UMD mínimo clear/copy → D3D9 → D3D11 → D3D12.** Vulkan ICD próprio é mais viável que DirectX porque a superfície de DDI é menor.

---

## 6. Nova metodologia de testes

O maior problema da nossa suíte hoje não é falta de testes — é que **os testes não podem falhar**. Sete arquivos de teste, sete "100% OPERACIONAL", zero asserções sobre a condição física.

### 6.1 Princípio: todo teste declara o critério antes de medir

Formato proposto para cada teste:

```
[CRITÉRIO]  RPTR deve alcançar WPTR em até 200 ms
[MEDIDO]    WPTR=0x00000008  RPTR=0x00000000  após 200 ms
[VEREDITO]  FALHOU — NÃO VALIDADO EM HARDWARE
```

Sem `[CRITÉRIO]` impresso antes de `[MEDIDO]`, o teste não conta.

### 6.2 Harness com veredito de 3 estados

| Estado | Significado |
|---|---|
| `PROVADO` | Todas as condições físicas da regra satisfeitas e impressas |
| `NÃO VALIDADO EM HARDWARE` | Executou, mas faltou pelo menos uma prova |
| `BLOQUEADO` | Pré-requisito ausente (ex: firmware não carregado) — não conta como falha do teste |

O `Central-Testes.cmd` deve terminar com uma matriz consolidada, e **sair com código ≠ 0** se qualquer teste retornar `PROVADO` sem as 8 provas presentes no output.

### 6.3 Testes que faltam e que valem mais que todos os atuais

| # | Teste | Por quê |
|---|---|---|
| T1 | **Scratch round-trip host** (`0x32D4`) | Prova em 1 ms que o MMIO alcança o GC. Deveria ser o teste nº 0 de tudo |
| T2 | **Scratch via PM4** | O host escreve A, o PM4 escreve B, o host lê B → **prova irrefutável de que o CP executou**. Custa 8 dwords |
| T3 | **Write-stick sweep** | Para cada registrador que o driver programa: ler, escrever, ler, restaurar. Detecta na hora offsets NBIO-bloqueados ou read-only. Nós descobrimos hoje que `CP_RB0_WPTR` é read-only pelo host — nenhum teste pegava isso |
| T4 | **Fill-pattern detector** | Qualquer leitura `0xDEFxDEFx`, `0xFFFFFFFF` ou `0xBADBAD00` marca o offset como INVÁLIDO automaticamente. Pegaria o `A_CP_STAT` errado imediatamente |
| T5 | **Fence poison** | Pré-envenenar com `~value` antes de submeter (o SDMA já faz isso certo — generalizar). Sem poison, um zero na memória pode ser confundido com sucesso |
| T6 | **Correlação carga × telemetria** | Amostrar clock/potência/temp a 10 Hz durante 5 s de carga e 5 s de idle. Exigir delta estatisticamente significativo. Hoje nossa "prova de clock" é um número constante |
| T7 | **Validação offline de protocolo** | Portar `tools/validate_psp_protocol.py` da referência: assertar layout de `psp_gfx_rb_frame`, offsets C2PMSG, tamanho de frame — **sem tocar em hardware**. Roda no CI, pega regressão de layout de graça |
| T8 | **Detector de fallback** | Grep automatizado no build: falhar a compilação se aparecer soma/cópia em loop de CPU dentro de função com "Dispatch"/"Compute" no nome. Barato e teria evitado o `pC[k]=pA[k]+pB[k]` |
| T9 | **Bisseção por `HwInitMaxStep`** | Registro que limita o bring-up ao passo N. Permite isolar em que passo o silício trava sem recompilar. Já está no nosso roadmap (Fase 1C) — implementar |
| T10 | **Baseline de plausibilidade** | Qualquer resultado de compute é comparado ao teto teórico. 1,15 GFLOPS em hardware de 4,6 TFLOPS = falha automática, mesmo com resultado numérico correto |

### 6.4 Disciplina de dados (roubada do `AGENTS.md`)

- **Marcar toda medição com a build do driver** que a produziu. `BuildId` já existe no registro — imprimir em todo teste.
- **Conclusão antiga = hipótese, não fato.** Depois de qualquer correção, re-rodar e re-anotar explicitamente se o resultado veio da build antiga ou nova.
- **Nunca concluir "hardware limitado" sem checar o Linux.** A referência gastou meses em "compute permanentemente fundido" enquanto o Linux rodava 24 CUs no mesmo chip.

### 6.5 Gates de compilação (roubado do `BC250-windowsDriverTest`)

```c
#define BC250_GFX_OFFSETS_VALIDATED       0
#define BC250_PSP_RING_VALIDATED          1   /* comprovado: C2PMSG_64=0x80020000 */
#define BC250_SDMA_VALIDATED              1   /* comprovado: 12.2 GB/s, fence GPU */
#define BC250_CP_EXECUTION_VALIDATED      0   /* T2 nunca passou */
#define BC250_COMPUTE_VALIDATED           0
```

Com o código de execução fisicamente inalcançável enquanto o gate for 0. Isso torna **impossível** que o driver reporte sucesso de algo não provado — que é exatamente o que a regra exige.

---

## 7. O que copiar de cada projeto — lista concreta

### De `AMD-BC-250-Windows-Driver`

| Item | Onde |
|---|---|
| Caminho direto C2PMSG_35/36/37 de firmware | `src/kmd/amdbc250_psp.c:191` |
| `DreamV3LoadFirmwareFromFile` (ZwCreateFile com path NT) | `src/kmd/amdbc250_dream_fw_load.c:172` |
| Disable de GFXOFF por SMN | `src/amdbc250_reg/amdbc250_reg.c:52` |
| Plano MQD/HQD/MAP_QUEUES completo | `docs/NEXT-STEPS-PLAN.md` Fase 2 |
| Lista de mensagens SMU com marcação de perigo | `docs/NEXT-STEPS-PLAN.md` Fase 3 |
| Mapa de registradores validado | `docs/REGISTER-MAP-BC250.md`, `AGENTS.md` §BC-250 hardware facts |
| Disciplina "historical-data caution" | `AGENTS.md` topo |
| ~60 ferramentas de teste focadas | `test-tools/` |

### De `BC250-windowsDriverTest`

| Item | Onde |
|---|---|
| Regra "CPU nunca escreve a fence" | `src/gfx/bc250_gfx.c:499` |
| Gates `*_VALIDATED` de compilação | `src/gfx/bc250_gfx.c:209` |
| Validação offline de protocolo PSP | `tools/validate_psp_protocol.py` |
| Coletor de diagnóstico Windows | `tools/collect_windows_bringup.ps1` |
| Esqueleto full-WDDM (Create/Context/Alloc/Patch/Submit) | `src/kmd/bc250_full_wddm.cxx` |
| **Fonte amdgpu do Linux offline** — `gfx_v10_0.c`, `psp_v11_0.c`, `smu_v11_0.c`, `cyan_skillfish_ppt.c`, `gmc_v10_0.c`, `sdma_v5_0.c`, headers gerados `gc_10_1_0_offset.h` | `docs/upstream/` |

> `docs/upstream/generated/gc_10_1_0_offset.h` (1,2 MB) é a **fonte autoritativa de offsets GC10**. Todo `A_*` do nosso `wddm_hw.c` deveria ser derivado dali com `GC_BASE=0x1260`, não escrito à mão. É assim que se elimina a classe inteira de bugs tipo `A_CP_STAT=0x8684`.

---

## 8. Riscos ativos no código atual

| # | Risco | Local | Severidade |
|---|---|---|---|
| R1 | `ForceGfxFreq (0x39)` sem DPM — crash documentado | `wddm_hw.c:552` | **Alta** |
| R2 | `CP_ME_CNTL=0` (unhalt) vira BSOD 0x1A no dia em que o firmware carregar | `wddm_hw.c:694` | **Alta (latente)** |
| R3 | `EnableDangerousEscapes=1` em produção — `WRITE_REG` arbitrário de modo usuário | registro | Média |
| R4 | `A_CP_STAT=0x8684` lê fill pattern e é reportado como dado | `wddm.h:541` | Média |
| R5 | Resultado de compute vindo da CPU pode mascarar corrupção real de VRAM | `wddm_hw.c:1455` | Média |
| R6 | 16 GB anunciados ao VidMM com BAR0 de 256 MB → alocações que não existem | `wddm_driver.c:801` | Média |

---

## 9. Resposta direta: as referências ajudam?

**Sim, decisivamente — e mudam o plano.**

- O `AMD-BC-250-Windows-Driver` **elimina meses de tentativa e erro**: já sabemos que PSP `LOAD_IP_FW` para CP não vai funcionar, que `SPI_PG` é SOS-locked, que `ForceGfxFreq` sem DPM é perigoso, que IC_BASE + unhalt causa BSOD, e temos o mapa de registradores validado no silício.
- O `BC250-windowsDriverTest` fornece **a disciplina de engenharia que falta na V3** (gates, fence só da GPU, validação offline) e, sobretudo, **o amdgpu do Linux inteiro offline** — que é a especificação real de como fazer bring-up desse chip.
- A V3 tem telemetria SMU genuinamente provada, o que nenhum dos dois alcançou. ~~E SDMA a 12 GB/s com fence de GPU.~~ **RETRATADO:** a Fase 0 mostrou que a cópia SDMA não move bytes; a estrutura do código está certa, a execução não acontece.

A combinação certa: **disciplina do `BC250-windowsDriverTest` + descobertas de silício do `AMD-BC-250-Windows-Driver` + o SDMA/telemetria que já funcionam na V3.**

E o próximo passo concreto, hoje, não é código de API gráfica: é **Fase 0 + o teste T2 (scratch via PM4)**. T2 custa 8 dwords e responde definitivamente se existe caminho para gráficos nesta placa sob Windows.

---

## Apêndice A — Mapa de registradores (corrigido na Fase 0)

**Fórmula autoritativa**, de `gc_10_1_0_offset.h` + `cyan_skillfish_reg_init.c`:

```
BAR5_byte = (segment_base + mmREG) * 4
segment_base = 0x1260 (BASE_IDX 0)  |  0xA000 (BASE_IDX 1)
```

Verificação: `mmCP_ME_CNTL 0x0F56` → `(0x1260+0x0F56)*4 = 0x86D8`, que é exatamente
o valor que já estava certo no nosso driver. Todo `A_*` deve ser derivado assim.

| Nome | mmREG | idx | BAR5 | Lido no silício | Antes |
|---|---|---|---|---|---|
| `GRBM_STATUS` | `0x0DA4` | 0 | **`0x08010`** | `0x00003028` | `0x03040` → sempre 0 |
| `GRBM_STATUS2` | `0x0DA2` | 0 | **`0x08008`** | `0x00000008` | `0x0326C` |
| `CP_STAT` | `0x0F40` | 0 | **`0x08680`** | `0x00000000` | `0x08684` → fill `0xDEF4DEF4` |
| `CP_ME_CNTL` | `0x0F56` | 0 | `0x086D8` | `0x00000000` | já correto |
| `CP_MEC_CNTL` | `0x0E2D` | 0 | **`0x08234`** | `0x50000000` (MEC halted) | ausente |
| `CP_RB0_RPTR` | `0x0F60` | 0 | `0x08700` | `0x00000000` | já correto |
| `CP_RB0_BASE` | `0x1DE0` | 0 | `0x0C100` | `0xF40FE000` | já correto |
| `CP_RB0_CNTL` | `0x1DE1` | 0 | `0x0C104` | `0x50000709` | já correto |
| `CP_RB0_WPTR` | `0x1DF4` | 0 | `0x0C150` | read-only pelo host | já correto |
| `SCRATCH_REG0` | `0x2040` | 1 | **`0x30100`** | R/W ✅ | `0x032D4` → não grava |
| `SCRATCH_REG1` | `0x2041` | 1 | **`0x30104`** | R/W ✅ | ausente |
| `SPI_PG_ENABLE_STATIC_WGP_MASK` | `0x1277` | 0 | **`0x0935C`** | `0x0000FFFF`, **escrita gruda** | `0x05C3C` → sempre 0 |
| `CC_GC_SHADER_ARRAY_CONFIG` | `0x100F` | 0 | **`0x089BC`** | `0xFFF80000` (= 24 CU stock) | `0x09C1C` |
| `RLC_PG_ALWAYS_ON_WGP_MASK` | `0x4C53` | 1 | **`0x3B14C`** | `0x00000003` | `0x03D64` → fill |
| `GRBM_GFX_INDEX` | `0x2200` | 1 | **`0x30800`** | `0x00000000` | `0x034D0` |
| `COMPUTE_DISPATCH_INITIATOR` | `0x1BA0` | 0 | **`0x0B800`** | — | `0x080E0` |
| `COMPUTE_PGM_LO` | `0x1BAC` | 0 | **`0x0B830`** | — | `0x08110` |
| `COMPUTE_USER_DATA_0` | `0x1BE0` | 0 | **`0x0B900`** | — | `0x081E0` |
| `PSP_C2PMSG_35/36/37` | — | — | `0x5818C/90/94` | `0xFFFFFFFF` | já correto |
| `PSP_C2PMSG_64/81` | — | — | `0x58200/0x58244` | `0x80020000` / não-zero | já correto |

**A consequência mais séria:** `SPI_PG_ENABLE_STATIC_WGP_MASK` **não é SOS-locked.**
A conclusão de toda a comunidade — "WGPs fundidos / travados pelo SOS" — vem de ler e
escrever `0x5C3C`, que é um registrador diferente. No offset correto `0x0935C` o valor
é `0x0000FFFF` e uma escrita de `0x1F` gruda (foi restaurado a `0xFFFF` em seguida).

## Apêndice B — `GFX_FW_TYPE` e resultado do PSP na BC-250

| `fw_type` | Nome | `LOAD_IP_FW` via ring |
|---|---|---|
| 1 | `CP_ME` | `0xFFFF0008` ITEM_NOT_FOUND |
| 2 | `CP_PFP` | `0xFFFF0008` |
| 3 | `CP_CE` | `0xFFFF0008` |
| 4 | `CP_MEC` | `0xFFFF0008` |
| 5 | `CP_MEC2` | `0xFFFF0008` |
| 8 | `RLC_G` | `0xFFFF0008` |
| 9 | `SDMA0` | `0xFFFF0008` |
| 10 | `SDMA1` | `0xFFFF0008` |
| **18** | **`SMU`** | **`0x00000000` TEE_SUCCESS** ✅ |

Comandos GPCOM suportados pelo SOS da BC-250: `GET_FW_ATTESTATION (0x0F)` ✅, `SETUP_TMR (0x05)` ✅, `LOAD_IP_FW (0x06)` só SMU. Não suportados: `GET_FW_ATTESTATION2 (0x10)`, `FB_FW_RESERV_ADDR (0x50/0x51)`, `PROG_REG` — todos `0x00000100 PSP_ERR_UNKNOWN_COMMAND`.
