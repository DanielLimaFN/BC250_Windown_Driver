# Fase 1 + 1.5 — Instruções de implementação

**Para:** o agente que vai executar
**Pré-requisito:** ler `HANDOFF.md` inteiro antes de tocar em código
**Estado de partida:** Fase 0 concluída; suíte em 5 PROVADO / 1 BLOQUEADO / 5 NÃO VALIDADO

---

## Por que a Fase 1 vem antes da 1.5

A SDMA0 é o único motor que já executou alguma coisa, e ela morre de forma latchada
após ~3 submissões. É a pista mais quente do projeto. Mesmo assim, **não comece por
ela.**

Dois itens da Fase 1 são candidatos diretos a causa dessa morte:

- `ForceGfxFreq (0x39)` sem tabelas DPM, executado no bring-up
- GFXOFF nunca desativado — não existe uma linha de GFXOFF no V3

Se qualquer um dos dois for a causa, a Fase 1 resolve a 1.5 de graça. O inverso não
vale: investigar o estado latchado com essas duas variáveis ativas é medir com ruído.
A Fase 1 é pequena e de risco baixo. Faça primeiro, re-meça, depois decida.

---

## Regras de trabalho

1. **Uma tarefa por vez.** Compile e rode a suíte entre cada uma. Se duas mudanças
   entram juntas e o resultado muda, você não sabe qual causou.
2. **Toda medição carrega a build.** O `BuildId` está no registro. Anote-o ao lado de
   qualquer número que você reportar.
3. **Nunca reinicie a máquina por conta própria.** Instalar o KMD exige desinstalar →
   reiniciar → instalar → reiniciar. Peça ao usuário.
4. **Não invente offset.** Derive de `referencias/BC250-windowsDriverTest/docs/upstream/generated/gc_10_1_0_offset.h`
   com `BAR5 = (segment_base + mmREG) * 4`, `segment_base = 0x1260` (BASE_IDX 0) ou
   `0xA000` (BASE_IDX 1). Use as macros `SOC15_GC0()` / `SOC15_GC1()` já em `wddm_hw.c`.
5. **`python tests/check_no_fallback.py` tem de passar** antes de qualquer compilação.
6. Se uma medição contradisser algo escrito nos relatórios, **a medição ganha**.
   Corrija o documento e marque a retratação.

---

## FASE 1 — seis tarefas

### 1.1 Remover `ForceGfxFreq (0x39)` do bring-up

**Onde:** `wddm_hw.c` linha ~627

```c
(VOID)BcSmuSendMsg(Dev, 0, 0x39, 1500, &rsp); /* PPSMC_MSG_ForceGfxFreq = 1500 MHz */
```

**O que fazer:** apagar essa chamada do caminho de bring-up.

A referência documenta `0x39` como `DANGER! Causes crash without DPM tables!`
(`referencias/AMD-BC-250-Windows-Driver/docs/NEXT-STEPS-PLAN.md`, Fase 3). O bring-up
executa isso em todo boot, sem DPM inicializado.

Não remova o `0x39` de `Bc250HwSetPowerProfile` (~linha 2207) — ali é uma ação
explícita do usuário, não do bring-up. Adicione um comentário avisando do risco.

Mantenha `0x37 GetGfxFrequency` (leitura, seguro). Se quiser piso/teto, use
`0x35 SetSoftMinCclk` / `0x36 SetSoftMaxCclk`, documentados como seguros.

**Verificação:** após reinstalar, `test_telemetry` deve continuar PROVADO. O clock
pode mudar de 1500 MHz para o que o DPM decidir — **isso é o resultado correto**, não
uma regressão. Um clock que varia é mais honesto que um travado.

---

### 1.2 Remover `RequestActiveWgp (0x18)` do bring-up

**Onde:** `wddm_hw.c` linha ~581, dentro de `Bc250HwConfigureComputeUnits`

```c
(VOID)BcSmuSendMsg(Dev, 0, 0x18, targetWgps, &smuRsp); /* PPSMC_MSG_RequestActiveWgp */
```

Documentado como timeout garantido sem DPM. Apague.

**Verificação:** o bring-up deve ficar mais rápido. `Hw_SmuTestQ0_Rsp` continua 1.

---

### 1.3 Adicionar disable de GFXOFF por SMN

**Isto é código novo — hoje não existe GFXOFF nenhum no V3.**

**Onde:** nova função em `wddm_hw.c`, chamada no bring-up **antes** de qualquer
programação de anel.

Referência funcional: `referencias/AMD-BC-250-Windows-Driver/src/amdbc250_reg/amdbc250_reg.c:52`

```
SMN_GFXOFF_FEATURE_LOW   0x03B10528
SMN_GFXOFF_FEATURE_HIGH  0x03B10998
GFXOFF_BIT               0x00000004   (bit 2)
```

O driver já tem `BcSmnRead` / `BcSmnWrite` (`wddm_hw.c` ~164/171). Sequência:

1. ler `FEATURE_LOW` e `FEATURE_HIGH` → guardar em `Dev->HwGfxOffFeatureLowBefore` / `HighBefore`
2. escrever `FEATURE_LOW` com o bit 2 limpo
3. **reler** → guardar em `HwGfxOffFeatureLowAfter`
4. `Dev->HwGfxOffDisabled = (após & 0x4) == 0`

**Não presuma sucesso.** A prova 9 da regra exige GFXOFF comprovadamente desativado,
não apenas solicitado. Se o bit não limpar, registre e siga — mas `HwGfxOffDisabled`
fica 0.

Exponha no registro: `Hw_GfxOffLowBefore`, `Hw_GfxOffLowAfter`, `Hw_GfxOffHighBefore`,
`Hw_GfxOffDisabled`. Adicione `GfxOffDisabled` ao `BC250_WDDM_ESCAPE_COMPUTE_CAPS`
(incremente `BC250_CAPS_SCHEMA_VERSION` para 2).

**Verificação:** novo teste `test_gfxoff.c` no harness, com critério "o bit 2 de
FEATURE_LOW deve ler 0 após a escrita". Adicione ao `run_suite.cmd` logo depois de
`test_regmap`.

---

### 1.4 Chaves fail-closed

**Onde:** `wddm_driver.c` ~725, junto dos `Bc250ReadTunable` existentes.

O padrão já existe (`SkipHwBringup`, `EnableDangerousEscapes`, `HwCuUnlockMode`).
Adicione:

| Chave | Padrão | Efeito |
|---|---|---|
| `HwUnhaltCp` | **0** | quando 0, o bring-up **nunca** escreve `CP_ME_CNTL = 0` |
| `HwInitMaxStep` | 0 (sem limite) | interrompe o bring-up após o passo N |
| `HwInitFirmware` | 1 | pula a carga de firmware quando 0 |
| `HwInitGfxRing` | 1 | pula a programação do CP ring quando 0 |
| `HwInitSdmaRing` | 1 | pula a inicialização da SDMA quando 0 |

`HwUnhaltCp = 0` por padrão é o mais importante — é o risco R2. Hoje
`wddm_hw.c` ~694 faz `BcRegWrite(Dev, A_CP_ME_CNTL, 0)` incondicionalmente. Isso é
inofensivo enquanto não há microcódigo, mas vira BSOD `0x1A` (DMA rogue corrompendo
memória de sistema) no dia em que a Fase 3 carregar firmware. A referência desativou
esse caminho exatamente por isso.

`HwInitMaxStep` permite bissecção: se o bring-up travar, o usuário acha o passo
culpado mexendo numa chave de registro, sem recompilar.

**Verificação:** `diag.ps1` deve listar todas as chaves com os padrões corretos.
Com `HwUnhaltCp=0`, `Hw_MeCntlReadback` deixa de ser 0.

---

### 1.5 Detecção que ainda falta

A regra exige detectar em runtime: núcleos de CPU, VRAM, GTT, BAR/ReBAR. Hoje só a
BAR é detectada.

- **CPU:** `KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS)` em `DriverEntry`.
  Expor `Hw_CpuCoresActive`.
- **ReBAR:** comparar `pDesc->u.Memory.Length` da BAR0 com o tamanho declarado na
  config PCI. Se forem diferentes, ReBAR está limitando. Expor `Hw_Bar0Size` e
  `Hw_ReBarActive`.
- **GTT:** não temos GART funcional; reporte `0` com procedência `ASSUMIDO`, não
  invente.

**Verificação:** `test_hw_info` passa a exibir esses campos com procedência.

---

### 1.6 Corrigir o cabeçalho obsoleto do `test_sdma.c`

Linhas 21-31 ainda afirmam:

> a GPU ESCREVE a fence em VRAM (esperada 0x06, observada 0x06)
> o destino permanece com o poison 0x5C intacto

A medição mais recente contradiz: 0/20 fences, e a engine para de drenar o anel na 4ª
tentativa (`0xC00000B5` = `STATUS_IO_TIMEOUT`, `rptrOnly=0`). Reescreva com o dado
atual e a data.

---

## FASE 1.5 — caracterizar a morte da SDMA0

**Só comece depois de reinstalar o KMD da Fase 1 e re-medir.** Se a SDMA voltar a
funcionar, `ForceGfxFreq` ou GFXOFF era a causa — documente e pule para a Fase 2.

### Fato que muda o desenho do teste

O estado da engine **persiste entre processos**. Medido: `SDMA0 copias` congelou em 8
e não subiu em nenhuma execução posterior. Um teste que roda depois de outro está
medindo um cadáver.

Consequência: **todo teste de SDMA precisa de uma sentinela de estado no início.**
Leia `SdmaCopyCount` via `BC250_ESCAPE_CMD_QUERY_INFO`; se ele não incrementar na
primeira submissão, declare `BLOQUEADO` com "engine já estava morta" em vez de
produzir números sobre um motor parado.

Isto invalida a conclusão que o teste imprime hoje:

```
Nem o FILL escreve: o motor so' processa a FENCE.
```

O motor **não** está processando a FENCE (`fenceWritten=0` nas 20). A mensagem precisa
distinguir os três estados: executa / drena sem executar / nem drena.

### 1.5.1 Adicionar os registradores de diagnóstico da SDMA

Já derivados, prontos para usar. Nossa tabela de SDMA está correta (confirmei
`CNTL`, `F32_CNTL`, `GFX_RB_CNTL`, `GFX_RB_RPTR`, `STATUS2` contra o header). Faltam:

```c
#define A_SDMA0_STATUS_REG    SOC15_GC0(0x0025u)  /* 0x04A14 */
#define A_SDMA0_STATUS1_REG   SOC15_GC0(0x0026u)  /* 0x04A18 */
#define A_SDMA0_STATUS3_REG   SOC15_GC0(0x004Cu)  /* 0x04AB0 */
#define A_SDMA0_FREEZE        SOC15_GC0(0x002Bu)  /* 0x04A2C */
#define A_SDMA0_UTCL1_RD_STATUS  SOC15_GC0(0x003Eu)  /* 0x04A78 */
#define A_SDMA0_UTCL1_WR_STATUS  SOC15_GC0(0x003Fu)  /* 0x04A7C */
#define A_SDMA0_UTCL1_TIMEOUT    SOC15_GC0(0x0047u)  /* 0x04A9C */
```

`STATUS_REG` diz se a engine está idle, halted ou em fault. Os `UTCL1_*` dizem se
houve **fault de tradução de endereço** — que é a hipótese número um para "executa
algumas vezes e depois latcha", já que a SDMA acessa VRAM por endereço MC
(`0xF400000000 + offset`) e não temos GPUVM configurada.

Adicione todos ao payload do `SDMA_COPY` e imprima no teste antes e depois de cada
submissão.

### 1.5.2 Rodar o `CONST_FILL` numa engine comprovadamente viva

O `FILL` já foi movido para antes das 20 cópias, mas isso não basta — ele ainda roda
sobre o cadáver se uma execução anterior matou a engine.

Faça o teste começar verificando a sentinela. Se a engine estiver morta, `BLOQUEADO`.
Se estiver viva, `FILL` primeiro, depois as cópias.

### 1.5.3 Separar as variáveis

Três hipóteses, três experimentos:

| Hipótese | Experimento | O que confirma |
|---|---|---|
| Contagem de submissões | 3 cópias → espera 1 s → 3 cópias | se as 3 seguintes falham, é cumulativo |
| Taxa/back-to-back | 20 cópias com 100 ms entre elas | se todas passam, é taxa |
| Tamanho do bloco | 1 cópia de 256 B, 1 de 4 KB, 1 de 1 MB | se só as grandes falham, é count/fault |

### 1.5.4 Testar ressurreição

Adicione um escape `BC250_ESCAPE_SDMA_RING_RESET` que chama `Bc250SdmaRingInit`.
Depois da morte, dispare e re-teste.

- Se ressuscitar → estado de engine recuperável. Registre o `STATUS_REG` de antes e
  depois; provavelmente o motor está em fault e precisa de reset.
- Se não ressuscitar → o problema é mais profundo (GPUVM, UTCL1, power).

---

## Ciclo de trabalho

```bash
cd C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V3 && python tests\check_no_fallback.py
```

```bash
cd C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V3 && build.bat
```

```bash
cd C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V3\tests && build_tests.bat
```

Instalação — **peça ao usuário, envolve dois reinícios**:

```bash
cd C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V3\package && install-wddm.bat
```

```bash
cd C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V3\tests && run_suite.cmd
```

Armadilhas de shell neste ambiente, já pagas:

- `python3` não existe; é `python`.
- Via PowerShell, `cd X && prog.exe` costuma falhar com "não é reconhecido". Use
  `cmd /c "cd /d ... && .\prog.exe"`, ou rode pelo Bash com `./prog.exe`.
- **Não gere strings C por script Python via heredoc.** O `\n` dentro do literal é
  reinterpretado e corrompe o arquivo. Use a ferramenta de edição de arquivo.
- `vswhere.exe` não está no PATH; os scripts reclamam mas funcionam.

---

## Critérios de aceite

**Fase 1 está concluída quando:**

- `check_no_fallback.py` passa
- nenhuma mensagem SMU marcada como perigosa é enviada no bring-up
- `diag.ps1` mostra `Hw_GfxOffLowBefore`, `Hw_GfxOffLowAfter` e `Hw_GfxOffDisabled`,
  com o bit 2 comprovadamente limpo — ou `HwGfxOffDisabled=0` registrado honestamente
- `HwUnhaltCp=0` é o padrão e `CP_ME_CNTL` não é mais zerado no bring-up
- `test_hw_info` exibe núcleos de CPU e estado de ReBAR com procedência
- a suíte roda inteira e o veredito geral continua sendo reportado com honestidade

**Fase 1.5 está concluída quando** você conseguir responder, com medição:

1. A engine está viva no início do teste? (sentinela)
2. O `CONST_FILL` funciona numa engine viva?
3. O que `SDMA0_STATUS_REG` e `UTCL1_*_STATUS` dizem no momento da morte?
4. A morte é por contagem, por taxa ou por tamanho?
5. Reinicializar o anel ressuscita?

**Nenhuma dessas conclui que a SDMA está validada.** Validar exige as provas 2 a 6 em
todas as tentativas. O objetivo da 1.5 é diagnóstico, não aprovação.

---

## O que não fazer

- **Não tente consertar o PSP `LOAD_IP_FW` para CP/RLC/SDMA.** O protocolo está
  byte a byte correto e o SOS da BC-250 simplesmente não implementa esses itens
  (`0xFFFF0008 TEE_ERROR_ITEM_NOT_FOUND`). Confirmado pelo Linux, cujo `psp_v11_0_8.c`
  não tem `load_ip_fw`. Isso é Fase 3, por outro caminho.
- **Não mexa em `SPI_PG` / `CC_GC_SHADER_ARRAY` no caminho de submissão.** Nos offsets
  corretos essas escritas reconfiguram o shader array de verdade. Reconfiguração é
  bring-up deliberado, com DPM antes.
- **Não faça unhalt do CP depois de carregar microcódigo pelo host** sem GPUVM pronta.
  A referência mediu BSOD `0x1A` fazendo isso.
- **Não deixe teste interativo na bateria.** Já aconteceu: `test_power_profile`
  aplicou 1850 MHz no meio da suíte e contaminou os testes seguintes.
- **Não reintroduza fallback de software** para fazer um teste passar. O `T8` pega,
  e é o motivo de o projeto ter perdido meses.

---

## Estado de referência para comparar

Medido em 2026-08-22, KMD `1.0.0822.0756`:

```
GRBM_STATUS       0x00003028        SPI_PG (0x0935C)  0x0000FFFF (gravável)
CP_STAT           0x00000000        CC     (0x089BC)  0xFFF80000 → 24 CU / 1536 SP
CP_ME_CNTL        0x00000000        PSP C2PMSG_64     0x80020000
CP_MEC_CNTL       0x50000000        PSP C2PMSG_35     0xFFFFFFFF
CP_RB0_RPTR       0x00000000        VRAM mapeada      256 MB
SDMA0 copias      8 (congelado)     Firmware do CP    nenhum aceito
```

Suíte: 5 PROVADO / 1 BLOQUEADO / 5 NÃO VALIDADO

**NÃO VALIDADO EM HARDWARE**
