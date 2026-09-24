# DX.45b — detector em duas fases, guardião durável; critério da Corrida 89

> 23/09/2026. Faixa **offline**, sem bind. Resultado anterior:
> [Corrida 88](377_CORRIDA88_DX45_RESULTADO.md), que travou o PC. Teto: **uma**
> corrida, depois da recuperação e de um desligamento completo.

## 1. O que a análise da 88 acrescentou (P0)

1. **O log do DPC não sobrevive a um travamento.** Acima de PASSIVE, o
   `Bc250DisplayLog` só grava num anel em memória (`bc250_display_ddi.c`,
   `KeGetCurrentIrql() > PASSIVE_LEVEL`), drenado depois em PASSIVE. Tudo que o
   poll escreve se perde num travamento duro. A "ausência" de
   `[CP_RESET_DETECTED]` na 88 vale ainda menos.
2. **O guardião perdeu o estado no travamento:**
   - `C:\BC250_SAFE_GUARD\armed.json` ficou com 1.178 bytes **todos zero**;
   - o XML das tarefas `BC250-SafeBindGuard`/`BC250-SafeBindDeadline` sumiu de
     `System32\Tasks`, e as tarefas não existem;
   - por isso não houve rollback no boot seguinte e o `bc250mcdm` ficou
     vinculado (Código 43, contido pelo fusível);
   - é a mesma classe do `_last_lab_binding.json` zerado pelo 0x116 da 81.
3. **Defeito de desenho da DX.45:** as leituras extras vinham antes da linha de
   log (registrado na docs/377).

## 2. As mudanças

**KMD (`bc250_active_miniport.c`), só leitura, nenhuma decisão muda:**
- **Em DPC:** o detector lê só `CP_ME_CNTL`. Se o CP estiver parado, guarda a
  primeira detecção em memória (`PENDENTE_EM_DPC`, uma linha no anel) e **não
  faz leitura extra**.
- **Em PASSIVE (submit), duas fases:**
  - **Fase 1:** a linha `[CP_RESET_DETECTED] #n` sai **primeiro**, com escrita
    e flush síncronos. Traz `CP_ME_CNTL`, os RPTR, as fences e a detecção
    pendente do DPC.
  - **Fase 2:** cada leitura extra é precedida de `LENDO <reg>` e seguida de
    `LIDO <reg>=`, todas síncronas. Se o PC travar, a última linha nomeia o
    registrador.
  - **Ordem das leituras:** `GRBM_STATUS`, `CP_STAT`, `RLC_PG_CNTL` (já lidos
    muitas vezes em corrida), depois `RLC_CNTL` e `RLC_GPM_STAT` (nunca lidos
    em corrida).
- Stub `KeGetCurrentIrql` (PASSIVE) no `active_miniport_test`.

**Host (`bind-bc250mcdm-lab.ps1`):**
- `armed.json` gravado com `WriteThrough` + `Flush(true)`;
- depois do registro das tarefas: `RegFlushKey(HKEY_LOCAL_MACHINE)` e
  `Write-VolumeCache -DriveLetter C`;
- confere que a tarefa existe e que a config tem `task_name`; **recusa o bind**
  se algo não ficou durável;
- imprime `[OK] Guardiao duravel`.

**Mantidas:** DX.43 (aquecimento) e DX.44 (preempção falante).

## 3. Verificação offline

- `build-contract` EXIT=0: a primeira passada falhou no link offline por falta
  de `KeGetCurrentIrql`, e o stub resolveu. Build lab EXIT=0.
- Portão DX.45 reescrito:
  - linha antes de leitura;
  - `LENDO` antes de cada leitura;
  - nada de fase 2 em DPC;
  - somente leitura.
  - `tools/test-dx45-cp-reset-detector-mutations.py`: **11 de 11 mutações
    reprovadas**.
- Portão do host (write-through e flushes antes de iniciar o guardião)
  verificado por mutação: tirar o flush do volume reprova.
- Parse do binder OK. `RegFlushKey(HKLM)` devolve 0 nesta máquina, e
  `Write-VolumeCache` existe (módulo Storage).
- DX.43 7/7, DX.44 6/6, DX.31 PASS; auditorias de segurança, matriz DDI,
  callbacks e RADV: PASS.

## 4. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.250.1.0`. SYS `700C6D935895419EB94E80814086C31A4FF336AD823AE5787C496854E08CFBD6`, INF `A87275B6…C143`, CAT `2CA238D4…2885`. Auditoria do pacote PASS; strings DX.43, DX.44 e DX.45b no SYS empacotado. |
| UMD / RADV | inalteradas: `45BB28C3…`, DX.42 `FCAA11FE…` |
| Pré-voo | `tools/prepare-corrida89-dx45b.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA89_DX45B.cmd`, elevado |

**Pré-requisitos, na ordem.** O estado atual é o `bc250mcdm` 3.249.1.0 ainda
vinculado, em Código 43, com o fusível `TRIPPED`.
1. `tools\BC250_RECOVERY.cmd`, elevado;
2. `powershell -ExecutionPolicy Bypass -File tools\inspect-boot-fuse.ps1 -Reset`;
3. desligamento completo.

## 5. Critério, escrito antes

**Pergunta 0 — o guardião é durável?**
- Assinatura: `[OK] Guardiao duravel` no console do bind.
- Se houver novo travamento, no boot seguinte o journal ganha uma entrada de
  retomada (`OBSERVING`/`ROLLBACK_STARTED` no boot novo) e o rollback acontece.
- Ausência do OK significa que o bind foi recusado antes de tocar o driver.
  Isso é defeito de host, não corrida.

**Pergunta 1 — reset silencioso e travamento:**

| | Assinatura | Implicação |
|---|---|---|
| **A1 / A2** | Como no docs/376: fase 1 escrita e as cinco `LIDO` completas; A1 com sinal de power gating, A2 sem | A1: H1 volta. A2: outro agente resetou o CP com o bloco ligado. |
| **F** | PC trava e a **última** linha do log recuperado é `[CP_RESET_DETECTED] #n LENDO <reg>` | A leitura daquele registrador com o CP naquele estado **trava o barramento**. É também a causa do travamento da 88 (alta confiança). Próximo passo: tirar a leitura e tratar o reset. |
| **G** | PC trava **sem** nenhuma linha de fase 1 em disco | O travamento não passa pela fase 2. Suspeitos: a leitura de `CP_ME_CNTL` no DPC, ou algo fora do detector. Um `PENDENTE_EM_DPC` perdido no anel é possível e **não** pode ser inferido. |
| **B** | Nenhuma detecção, nenhum travamento, snapshots de TDR com `CpMeCntl=0` | O reset não se repetiu. Até aqui: 1 em 5 corridas, mais um travamento sem causa. |
| **C** | Nenhuma detecção, mas um snapshot de TDR com CP parado antes do nosso quiesce | O detector falhou. |
| **D** | Detecção em rajada no bind ou dentro do nosso quiesce | Falso positivo. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Perguntas 2 e 3:** a DX.43 (4ª amostra: trocas `Emitido=1`, TDRs com ondas no
VMID anterior) e a contagem e posição de `[PREEMPT_COMMAND]`.

**Função:** harness (base 16/60 na 87), desktop e device-lost. Qualquer TDR,
travamento ou menos de 60/60 é falha funcional.

Desfecho parcial não é desfecho. Exemplo: F exige que a linha `LENDO` seja a
última da corrida, não apenas presente.
