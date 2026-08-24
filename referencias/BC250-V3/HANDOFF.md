# HANDOFF — BC250-V3, continuação do trabalho

**Escrito em:** 2026-08-22
**Para:** o próximo agente que pegar este projeto
**Build do KMD no disco:** `1.0.0822.0756` (compilado e assinado; a versão *instalada* pode ser anterior — confira antes de tirar conclusões)

Leia isto inteiro antes de tocar em qualquer coisa. Vai economizar dias.

---

## 0. A regra manda em tudo

`../CLAUDE.md` importa `../BC250_HARDWARE_RULES.md`. Ela **prevalece sobre este documento, sobre os relatórios e sobre qualquer comentário no código**.

O resumo operacional: nada é "funciona" sem prova física no silício. Enumerar API, carregar DLL, retornar `STATUS_SUCCESS`, fence incrementada pela CPU, telemetria constante — nada disso conta. Quando faltar prova, escreva exatamente:

**NÃO VALIDADO EM HARDWARE**

Este projeto já foi vítima disso uma vez: o driver tinha um loop de soma em CPU dentro do dispatch de compute, produzindo o "resultado correto" que a suíte exibia como aceleração de GPU. Levou meses até alguém verificar.

---

## 1. Onde está cada coisa

| Arquivo | O que é | Confiabilidade |
|---|---|---|
| `ANALISE_REFERENCIAS_E_PLANO.md` | Análise dos 2 projetos da comunidade + plano em fases | **Tem erros marcados em linha.** A §2.1 e o §4 estão superados; leia os avisos |
| `FASE0_RESULTADO.md` | Execução da Fase 0 + correções | Atual. A §7b é a mais recente |
| `HANDOFF.md` | Este arquivo | Atual |
| `ROADMAP_WDDM2_SILICON.md` | Roadmap original do projeto | Anterior à Fase 0; a ordem das fases mudou |
| `PLANO_DIAGNOSTICO_TELEMETRIA_E_PSP.md` | Documento antigo | Não revisado |
| `README.md` | Descreve o driver | **Contém afirmações desatualizadas** (1536 shaders como fato, 16 GB, "prova de silício") |

Referências externas em `referencias/`:

- `AMD-BC-250-Windows-Driver/` — driver WDM da comunidade. Fraco em arquitetura, **ouro em engenharia reversa**. `AGENTS.md` (1871 linhas) e `docs/NEXT-STEPS-PLAN.md` são leitura obrigatória.
- `BC250-windowsDriverTest/` — miniport full-WDDM que nunca rodou em hardware. Valor: disciplina de engenharia e **o amdgpu do Linux inteiro offline** em `docs/upstream/`.

---

## 2. O fato mais valioso que descobri

**A tabela de registradores do driver estava majoritariamente errada, e a da referência também.**

A fórmula autoritativa, extraída de `referencias/BC250-windowsDriverTest/docs/upstream/generated/gc_10_1_0_offset.h` + `cyan_skillfish_reg_init.c`:

```
BAR5_byte = (segment_base + mmREG) * 4

segment_base = 0x1260  para BASE_IDX 0
segment_base = 0xA000  para BASE_IDX 1
```

Verificação: `mmCP_ME_CNTL = 0x0F56`, `BASE_IDX 0` → `(0x1260 + 0x0F56) * 4 = 0x86D8`, que é o valor que já funcionava no driver.

**Nunca escreva um offset à mão.** Pegue `mmXXX` e `mmXXX_BASE_IDX` do header e aplique a fórmula. No `wddm_hw.c` isso está encapsulado em `SOC15_GC0()` / `SOC15_GC1()`.

Como isso quebrou tantas conclusões:

| Registrador | Offset errado | Offset correto | O que o errado lia |
|---|---|---|---|
| `GRBM_STATUS` | `0x03040` / `0x03260` | **`0x08010`** | `0x00000000` constante → "GPU idle/morta" |
| `CP_STAT` | `0x08684` | **`0x08680`** | `0xDEF4DEF4` (fill) → reportado como telemetria |
| `SPI_PG_ENABLE_STATIC_WGP_MASK` | `0x05C3C` | **`0x0935C`** | `0` sempre → "WGPs SOS-locked" |
| `CC_GC_SHADER_ARRAY_CONFIG` | `0x09C1C` | **`0x089BC`** | valor errado |
| `SCRATCH_REG0/1` | `0x032D4` | **`0x30100/4`** | não aceitava escrita |
| `GRBM_GFX_INDEX` | `0x034D0` | **`0x30800`** | (é BASE_IDX 1) |
| `RLC_PG_ALWAYS_ON_WGP_MASK` | `0x03D64` | **`0x3B14C`** | `0xFFFFFFFF` (fill) |
| bloco `COMPUTE_*` | `0x080E0+` | **`0x0B800+`** | o driver programava compute em endereços aleatórios |

Consequência que ninguém na comunidade percebeu: **`SPI_PG` não é SOS-locked.** No offset correto ele lê `0x0000FFFF` e **aceita escrita** (testei `0x1F`, grudou, restaurei). Todo o veredito de "WGPs fundidos" veio de sondar `0x5C3C`, que é outro registrador. Isso *não* prova que 40 CUs destravam — prova que a hipótese foi encerrada no endereço errado e precisa ser reaberta.

---

## 3. Estado real do silício (medido, não presumido)

```
Device            PCI\VEN_1002&DEV_13FE, CM_PROB_NONE
GRBM_STATUS       0x00003028   (GUI_ACTIVE = 0)
CP_STAT           0x00000000
CP_ME_CNTL        0x00000000   (ME não halted)
CP_MEC_CNTL       0x50000000   (MEC HALTED)
SPI_PG (0x0935C)  0x0000FFFF   gravável
CC   (0x089BC)    0xFFF80000   INACTIVE_WGPS=0xFFF8 → 3 WGP/SA → 24 CU → 1536 SP
PSP C2PMSG_64     0x80020000   TOS pronto + eco do ring KM
PSP C2PMSG_35     0xFFFFFFFF   bootloader concluiu
VRAM mapeada      256 MB       (BAR0 real; NÃO são 16 GB)
Bloco GC          alcançável   scratch 0x30100 faz round-trip
```

Firmware, por `fw_type` (códigos TEE crus, expostos no registro pela Fase 0):

```
SDMA0(9) RLC_G(8) CP_CE(3) CP_PFP(2) CP_ME(1) CP_MEC(4)  →  0xFFFF0008  TEE_ERROR_ITEM_NOT_FOUND
CP_MEC2(5)                                                →  0x00000000  TEE_SUCCESS
```

O SOS da BC-250 **não implementa** `LOAD_IP_FW` para esses tipos. Isso bate com o Linux: `psp_v11_0_8.c` não tem `load_ip_fw` nenhum. **Não é bug nosso** — não gaste tempo tentando consertar o protocolo GPCOM, ele está byte a byte correto (MP0 base `0x58000`, cmd `buf_size@+0 / version@+4 / cmd_id@+8 / união@+28 / resp@+864`, frame de 64 B, WPTR +16 dw).

O `CP_MEC2` retornando `TEE_SUCCESS` é uma anomalia que **não investiguei**. Vale um olhar.

---

## 4. O que está provado e o que não está

| Bloco | Veredito | Evidência |
|---|---|---|
| Telemetria SMU | **PROVADO** | tabela de métricas por DMA, variância real entre amostras (Δ3 °C, Δ671 mW) |
| Detecção de configuração | **PROVADO** | 24 CU derivado de `CC`, 256 MB da BAR, tudo com procedência |
| Mapa de registradores | **PROVADO** | sem fill patterns, scratch R/W confirmado |
| Display (KMDOD) | funciona | VidPN, scanout, VSync — mas **não é aceleração** |
| **SDMA0** | **NÃO VALIDADO** | 1/20 cópias corretas, 3/20 fences; o motor morre após 3 submissões |
| **CP PM4 ring** | **NÃO VALIDADO** | RPTR travado em `0x00000000`, nunca consome |
| **Compute** | **BLOQUEADO** | sem microcódigo, nenhuma engine de shader inicializada |
| APIs gráficas | inexistentes | driver ainda é KMDOD display-only |

---

## 5. A pista quente: como a SDMA morre

Este é o achado mais acionável do projeto agora.

Rodando 20 cópias consecutivas de 4 KB:

| Tentativa | `fenceWritten` | `rptrOnly` |
|---|---|---|
| 01, 02, 03 | **1** | 0 |
| 04 … 20 | **0** | **1** |

A transição é **monotônica e permanente**. Não é ruído. O motor executa ~3 submissões e depois nunca mais escreve fence — mas **continua drenando o anel** (`rptrOnly=1` = RPTR alcança WPTR).

Motor que consome frames sem executá-los é motor em estado de falha latchado.

Confirmação pelo poison: última fence `esperada=0x00000019`, `observada=0xFFFFFFE6` = exatamente `~0x19`. A GPU não tocou no endereço.

Fatos que restringem as hipóteses:

- **Sem wrap de anel.** RPTR final `0x44C`, delta `0x2C` por submissão, anel de 4096 B. 20 submissões não dão a volta.
- **O pacote está correto.** `SDMA_OP_COPY` em `wddm_hw.c` é byte a byte igual a `sdma_v5_0_vm_copy_pte` do Linux (header, `count-1`, src lo/hi, dst lo/hi).
- **Coerência do host está correta.** Escrita: `RtlCopyMemory` + barreira + HDP flush. Leitura: HDP invalidate antes. Verificado nos dois caminhos.
- **`CONST_FILL` também não escreve** — mas rodou *depois* das 20 tentativas, com o motor já morto. **Esse diagnóstico não vale ainda.**

### Próximos passos concretos para a SDMA

1. **Ler `SDMA0_STATUS_REG` depois da morte.** É o registrador que diz se a engine está em fault, halted ou idle. Não está na nossa tabela. Deriva de `referencias/BC250-windowsDriverTest/docs/upstream/generated/sdma5_4_2_2_offset.h` com a mesma fórmula SOC15.
2. **Rodar `CONST_FILL` logo após a inicialização**, antes de qualquer COPY. Se o FILL funcionar no estado bom, o motor executa movimentação de dados e o problema do COPY é na leitura da origem. O escape já existe: `BC250_ESCAPE_SDMA_FILL (0xBD0C)`.
3. **Separar as variáveis:** número de submissões vs. intervalo vs. tamanho do bloco. Um teste de 3 cópias → espera 1 s → mais 3 diz se o estado é permanente ou se recupera.
4. **Tentar reinicializar o anel** (`Bc250SdmaRingInit`) depois da morte e ver se volta. Se voltar, é estado de engine; se não, é algo mais profundo.

---

## 6. Erros que eu cometi — não repita

### 6.1 Retornar NTSTATUS de falha destrói a evidência

O `dxgkrnl` **não copia o buffer privado de volta ao modo usuário quando o escape retorna NTSTATUS de falha.** Eu fiz o driver retornar `STATUS_DEVICE_NOT_READY` quando a fence não era escrita — isso apagava `FenceObserved`, `RptrAfter` e `FenceWritten`, e o teste via zeros. Passei um bom tempo perseguindo um "0 de 20" que era artefato do transporte.

**Padrão correto, já aplicado em `SDMA_COPY` e `COMPUTE_DISPATCH`:** o escape retorna `STATUS_SUCCESS` por ter entregue a medição; o status real da engine vai em `Header.Status`; o veredito físico vai no payload (`FenceWritten`, `ExecutionProven`). Isso não viola a regra — nenhum campo afirma execução, e quem consome é obrigado a olhar a evidência.

### 6.2 Contar bits do registrador errado

Derivei a contagem de WGP de `SPI_PG`, que em broadcast lê `0x0000FFFF`. Contar bits 0..4 disso dá 5 WGP/SA → **40 CU / 2560 SP**, e o teste imprimiu isso como `[DETECTADO]`. Errado.

A fonte autoritativa é `CC_GC_SHADER_ARRAY_CONFIG`, campo `INACTIVE_WGPS` em `[31:16]`: bit limpo = WGP ativo.

```
CC = 0xFFF80000 → INACTIVE_WGPS = 0xFFF8 → bits 0,1,2 limpos → 3 WGP/SA
3 × 4 SA = 12 WGP = 24 CU = 1536 SP     ← bate com o stock documentado
```

Já corrigido. `SPI_PG` agora é só conferência, e a divergência entre as fontes é registrada em `Hw_WgpSourcesDisagree` em vez de resolvida escolhendo o número maior.

### 6.3 Deixar teste interativo dentro de bateria automatizada

`test_power_profile` abre um menu e espera `scanf`. Dentro do `run_suite.cmd` ele **aplicou 1850 MHz @ 900 mV** no meio da bateria, via `ForceGfxFreq` sem tabelas DPM — o caminho que a referência marca como `DANGER! Causes crash without DPM tables!`. O clock ficou alterado para os testes seguintes.

Corrigido: a suíte chama com `--readonly`. **Nunca deixe um teste mudar o estado do silício numa bateria.**

### 6.4 Fraqueza que deixei no teste de SDMA

`test_sdma.c` valida o envenenamento lendo **só o byte 0**:

```c
if (!VramLer(...) || lido[0] != 0x5C) { /* descarta */ }
```

Se o poison assentar parcialmente, o destino guarda restos da cópia anterior e a métrica "bytes corretos" mede lixo antigo. **Enquanto isso não for corrigido para comparar o bloco inteiro, os números de "bytes corretos" são inconclusivos.** O que permanece sólido é `fenceWritten` e a leitura da fence contra o poison.

### 6.5 Confiar em conclusão antiga

O `AGENTS.md` da referência abre com um aviso de *historical-data caution* — e está certíssimo. Várias conclusões "definitivas" deles foram revertidas (o "PSP ring permanentemente indisponível" era offset errado). Eu mesmo retratei duas afirmações minhas neste projeto.

**Trate todo relatório antigo como hipótese a re-verificar. Marque cada medição com a build do driver que a produziu.**

---

## 7. A suíte de testes

`tests/test_verdict.h` é o harness. Todo teste imprime `[CRITERIO]` antes de `[MEDIDO]` e deriva o veredito de `BcProva()`. Três estados:

- `PROVADO` — todas as provas aplicáveis satisfeitas
- `NÃO VALIDADO EM HARDWARE` — executou, faltou prova
- `BLOQUEADO` — pré-requisito ausente; não conta como falha do teste

| Teste | Cobre |
|---|---|
| `test_regmap` | T1 scratch round-trip, T3 write-stick, T4 detector de fill + regressão dos offsets antigos |
| `test_cp_execution` | **T2** — host grava A, PM4 grava B, host lê. Prova irrefutável de execução do CP. Custa 8 dwords |
| `test_fw_status` | códigos TEE crus por `fw_type`; distingue chave ausente de valor zero |
| `test_sdma` | poison + comparação byte a byte + 20 tentativas + RPTR/fence separados + diagnóstico FILL |
| `test_telemetry` | T6 — exige variância; constante não é telemetria |
| `test_hw_info` | procedência DETECTADO/ASSUMIDO de cada número |
| `test_compute` | evidência de fence real; BLOQUEADO sem firmware |
| `test_opencl` | verifica que a camada **falha honestamente** |
| `check_no_fallback.py` | T8 — falha o build se houver aritmética de CPU em função de dispatch, offset proibido, telemetria literal ou texto triunfalista. Ignora comentários |

**Rode `run_suite.cmd`, não `test_all.bat`.** O `test_all.bat` é a bateria antiga com 6 testes e sem exit code.

Pendências na suíte:

- `test_vram_rw.c` **não foi convertido** ao harness; ainda imprime `INTEGRIDADE DE VRAM 100% VALIDADA` no estilo antigo.
- `test_hw_info` exibe `SDMA0 copias: N (12232 MB/s medidos)` — esse número é resíduo de tamanho ÷ latência e é enganoso. Na mesma sessão o `test_sdma` mediu 651 MB/s.
- `test_reg_probe.c` e `test_ring_probe.c` são ferramentas de sondagem que escrevi durante a análise; não estão no `run_suite.cmd` e não usam o harness.

---

## 8. Como compilar, instalar e testar neste ambiente

O ambiente tem VS2022 Enterprise e WDK 10.0.26100. `vswhere.exe` não está no PATH — os scripts reclamam mas funcionam.

```bash
cd C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V3 && build.bat
```

```bash
cd C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V3\tests && build_tests.bat
```

```bash
cd C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V3\tests && run_suite.cmd
```

Instalar o KMD exige desinstalar, reiniciar, instalar, reiniciar. **Peça ao usuário** — não reinicie a máquina por conta própria.

```bash
cd C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V3\package && install-wddm.bat
```

Armadilhas de shell que me custaram tempo:

- O `cd X && programa.exe` via PowerShell frequentemente falha com "não é reconhecido". Use `cmd /c "cd /d ... && .\programa.exe"` ou rode pelo Bash com caminho relativo `./programa.exe`.
- Heredocs com `\n` dentro de strings C sendo geradas por Python via Bash **corrompem os literais**. Para editar strings C, use a ferramenta de edição de arquivo, não geração por script.
- `python3` não existe; é `python`.

---

## 9. O plano, atualizado

A ordem mudou depois da Fase 0. `ANALISE_REFERENCIAS_E_PLANO.md` §5 tem o detalhe; o resumo:

**Fase 0 — purga de mentiras.** ✅ Concluída. Fallbacks removidos, tabela de registradores corrigida, suíte reescrita.

**Fase 1 — segurança e detecção.** Parcialmente feita. Falta:
- remover `ForceGfxFreq (0x39)` do bring-up (`wddm_hw.c`, ~linha 552) — documentado como perigoso sem DPM
- remover `RequestActiveWgp (0x18)` do bring-up
- detectar núcleos de CPU (`KeQueryActiveProcessorCountEx`), GTT e ReBAR
- implementar disable de GFXOFF por SMN (`0x03B10528` bit 2) com readback — **hoje não existe nenhuma linha de GFXOFF no V3**
- chaves de registro fail-closed (`HwInitMaxStep`, `HwUnhaltCp=0` por padrão)

**Fase 1.5 — nova: destravar a SDMA.** Ela subiu de prioridade. É o único motor que já executou alguma coisa, e a falha está estreitada a um estado latchado após ~3 submissões. Ver §5.

**Fase 2 — DPM do SMU.** O desbloqueio real. Sem DPM o GFX fica em deep sleep e não processa rings, por mais que os registradores pareçam legíveis. Precisa da PPTable (extrair de `cyan_skillfish2_smc.bin`, ou do `smu11_driver_if_cyan_skillfish.h`, ou dump de um Linux funcionando). Mensagens `0x04`, `0x05`, `0x07`, depois `0x35`/`0x36`. Aceite: `QueryActiveWgp (0x1E)` retornando > 0 e clock reagindo à carga.

**Fase 3 — firmware por caminho viável.** PSP ring só aceita SMU (`fw_type=18`). Alternativas, na ordem: mailbox direto `C2PMSG_35/36/37` (a referência conseguiu 8/8 aceitos), depois IC_BASE DMA — **com o CP obrigatoriamente halted**, porque unhalt após carga pelo host causa DMA rogue e BSOD `0x1A` (documentado pela referência, que desativou esse caminho por isso).

**Fase 4 — MQD/HQD.** A peça que ninguém implementou. Registradores `COMPUTE_*` via MMIO não ativam fila; é preciso MQD em GTT + buffer EOP + PM4 `SET_RESOURCES` e `MAP_QUEUES`. Referência completa em `referencias/AMD-BC-250-Windows-Driver/docs/NEXT-STEPS-PLAN.md` Fase 2, e o código em `docs/upstream/gfx_v10_0.c`.

**Fase 5 — full WDDM miniport.** Só depois da Fase 4 comprovada. Nota: `DxgkInitialize` **não é export do `dxgkrnl.sys`**, vem de `displib.lib` — a referência perdeu tempo com isso.

**Fase 6 — APIs gráficas.** compute/copy validado → UMD mínimo clear/copy → D3D9 → D3D11 → D3D12. Vulkan ICD próprio é mais viável que DirectX pela superfície menor de DDI.

---

## 10. Riscos ativos no código

| # | Risco | Onde |
|---|---|---|
| R1 | `ForceGfxFreq (0x39)` sem DPM no bring-up | `wddm_hw.c` ~552 |
| R2 | `CP_ME_CNTL = 0` (unhalt) vira BSOD `0x1A` no dia em que o firmware carregar | `wddm_hw.c` ~694 |
| R3 | `EnableDangerousEscapes=1` em produção — `WRITE_REG` arbitrário de modo usuário | registro |
| R4 | Registro anuncia 16 GB ao VidMM com BAR0 de 256 MB | `wddm_driver.c` ~801 |
| R5 | `README.md` afirma coisas que a Fase 0 desmentiu | `README.md` |

---

## 11. Se você só for ler uma coisa

O caminho para gráficos na BC-250 sob Windows **não passa pelo PSP `LOAD_IP_FW`**. Passa por:

**DPM do SMU → firmware por caminho direto → fila MQD/HQD → PM4 `MAP_QUEUES`**

É o que o Linux faz, e é o que nenhum dos três projetos (o nosso e os dois de referência) implementou.

E antes de declarar qualquer coisa: rode `test_cp_execution` (T2). Custa 8 dwords e responde, sem ambiguidade, se o CP executa. Enquanto ele falhar, nenhuma API gráfica tem por onde entrar.

**NÃO VALIDADO EM HARDWARE**
