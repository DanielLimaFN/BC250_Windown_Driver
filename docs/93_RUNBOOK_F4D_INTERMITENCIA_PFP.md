# Runbook F4d — Caracterização da intermitência do PFP

> Procedimento de bancada para reproduzir e caracterizar a falha intermitente do PFP
> (`PC=0x17F5` em loop vs `PC=0x284` assentado). Preparado em 2026-08-29.
> Contexto e evidências: [`STATUS_E_ROADMAP_CONSOLIDADO.md`](../STATUS_E_ROADMAP_CONSOLIDADO.md) seção 2.

---

## 0. O que este ensaio responde

**Hipótese**: o PFP degrada por estado residual acumulado **dentro do mesmo boot**, e o reboot
recupera.

**Base**: 167 capturas históricas em que o PC final do PFP previu o resultado do CP com 100%
de acurácia. No dia 28/08, seis falhas consecutivas a partir das 18:17 no **mesmo binário**
(`version=94`, `staged=65792dw/0mismatch`, `JT_STAT=FF0303` idênticos), com recuperação às
21:14 após reboot.

**Pergunta objetiva**: em qual iteração o PC migra de `0x284` para `0x17F5`?

---

## 1. AVISO — por que este ensaio tem risco

A máquina hoje roda em **modo display-only** (`SkipHwBringup=1`). Nesse modo o `StartDevice`
retorna cedo em [`bc250_active_miniport.c:2364`](../driver/wdk/bc250_active_miniport.c:2364),
**sem tocar em PFP, CP ou GCVM** — é exatamente por isso que o candidato 3.24 dá Código 0 sem
tela preta.

Rodar o F4d exige `SkipHwBringup=0`, o que **religa o bring-up no mesmo dispositivo que serve
o display**. Isso reativa `R_GCVM_INVALIDATE_ENG0_REQ`
([`bc250_pfp_direct_load.c:886`](../driver/wdk/bc250_pfp_direct_load.c:886)), que no BC-250
desliga o scanout — é a causa-raiz nº 2 da tela preta.

**Expectativa realista: a tela vai apagar.** O watchdog de rollback é a rede de segurança.
Não faça este ensaio sem acesso remoto ou sem estar disposto a usar o recovery às cegas.

---

## 2. Pré-condições (verificar ANTES de mexer em qualquer coisa)

| # | Checagem | Comando | Esperado |
|---|---|---|---|
| 1 | Recovery íntegro | `powershell -File tools\audit-bc250-recovery-readiness.ps1` | `PASS`, SHA source == installed |
| 2 | `BC250_RECOVERY.cmd` acessível | conferir `C:\BC250_RECOVERY.cmd` | presente |
| 3 | SHA de referência do código | `git status` limpo, ou `git rev-parse HEAD` anotado | sem pendências não commitadas |
| 4 | Watchdog de reboot armado | `powershell -File tools\arm-bc250mcdm-reboot-watchdog.ps1` | tarefa `BC250-RebootGuard-*` criada |
| 5 | Sessão elevada | — | Administrador |

> **Pendência 9 do STATUS** — havia 1.152 linhas não commitadas em `bc250_compute_service.c`
> (+784) e `bc250_active_miniport.c` (+77) sobre `771d108`. Sem SHA de referência, o resultado
> do ensaio não fica ancorado em nada. Resolver antes do passo 3.

---

## 2.5 Cadeia de instalação — build NÃO é suficiente

> Aprendido na marra em 2026-08-29: rodar `build-bc250mcdm-package.ps1` e ir direto para o
> `bind` **não funciona**. Faltam um passo e uma limpeza.

**O `bind` não faz o stage.** Ele exige o pacote já publicado via `pnputil /add-driver` e o
marcador `C:\BC250_TEST_OEMINF.txt` preenchido com o `oemNN.inf` publicado. A cadeia real:

```text
build-bc250mcdm-package.ps1      -> assina o pacote em driver\wdk\package\output_active
        |
stage-bc250mcdm-package.ps1      -> pnputil /add-driver + grava o marcador   <- PASSO OMITIDO
        |
bind-bc250mcdm-failclosed.ps1    -> liga o dispositivo ao bc250mcdm
```

**O gate de pré-binding é a checklist autoritativa.** É read-only, então rode-o entre cada
passo em vez de adivinhar:

```bash
powershell -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-prebinding.ps1
```

Ele exige host limpo: **sem** serviço `bc250mcdm`, **sem** `System32\drivers\bc250mcdm.sys`,
**sem** marcador preexistente, e **sem reboot pendente**.

### Armadilhas conhecidas

| Sintoma no gate | Causa | Resolução |
|---|---|---|
| `servico bc250mcdm apareceu durante stage` | Serviço residual de instalação anterior | `sc.exe delete bc250mcdm` |
| `bc250mcdm.sys apareceu em System32\drivers` | Binário residual | Geralmente sai no reboot via `PendingFileRenameOperations` |
| `marcador OEM preexistente` | `C:\BC250_TEST_OEMINF.txt` obsoleto — o stage **recusa** se ele já existir | Apagar o arquivo |
| `reboot pendente: PendingFileRenameOperations` | `clean_all_drivers.ps1` agendou exclusões | **Reiniciar** |
| `cardinalidade INF MCDM inesperada: 0` | O stage nunca rodou | Rodar `stage-bc250mcdm-package.ps1 -Execute` |
| `hash divergente no FileRepository` | Pacote novo sobre publicação antiga | Despublicar antes (`clean_all_drivers.ps1`) |

`clean_all_drivers.ps1` remove INF e FileRepository e desliga o dispositivo para
`BasicDisplay`, mas **deixa o serviço e o `.sys` em System32** — os dois itens que o gate
reprova. Contar com ele sozinho não basta.

---

## 3. Reconfiguração para o modo de ensaio

O tunable é lido via `RTL_REGISTRY_SERVICES` + `bc250mcdm\Parameters`
([`bc250_active_miniport.c:37-59`](../driver/wdk/bc250_active_miniport.c:37)), ou seja:

```text
HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters
    SkipHwBringup  (REG_DWORD)   atual = 1
```

**Anotar o valor atual antes de mudar** (é `1`, e o default do código também é `1`, então
apagar o valor NÃO desliga o skip — é preciso gravar `0` explicitamente).

Para ligar o bring-up:

```bash
reg add "HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters" /v SkipHwBringup /t REG_DWORD /d 0 /f
```

O valor só passa a valer no próximo `StartDevice` — que é justamente o que o script de ensaio
provoca a cada iteração.

---

## 4. Execução

**Passo 1 — preflight (não toca em nada).** Deve passar agora que `SkipHwBringup=0`:

```bash
powershell -ExecutionPolicy Bypass -File tools\ensaio-pfp-intermitencia.ps1
```

O preflight recusa executar se `SkipHwBringup=1`, se não estiver elevado, ou se o dispositivo
não estiver ligado ao `bc250mcdm`. É fail-closed por design.

**Passo 2 — primeira falha, parando nela.** Preserva o estado exato da transição:

```bash
powershell -ExecutionPolicy Bypass -File tools\ensaio-pfp-intermitencia.ps1 -Iterations 40 -StopOnFirstFailure -Execute -IAcceptTheRisk
```

**Passo 3 — reboot e repetir.** Confirma que o reboot é o que recupera. Mesmo comando após o
reboot; se a primeira iteração voltar a dar `OK-SETTLED`, a hipótese de acumulação está
sustentada.

**Passo 4 — série completa** (só se os passos 2 e 3 forem consistentes):

```bash
powershell -ExecutionPolicy Bypass -File tools\ensaio-pfp-intermitencia.ps1 -Iterations 40 -Execute -IAcceptTheRisk
```

---

## 5. Como ler o resultado

O script classifica cada ciclo pelo PC final do PFP:

| Veredito | PC final | Significado |
|---|---|---|
| `OK-SETTLED` | `0x284` | PFP assentou; CP deve consumir o ring |
| `FAIL-LOOP` | `0x17F5` / `0x17F6` | **O modo que estamos caçando** — PFP em loop ativo |
| `FAIL-STALL-OLD` | `0x41E` | Modo antigo de 27/08, já superado |
| `FAIL-NO-START` | `0x0` | PFP nunca saiu do reset |
| `!DIVERGE` | — | **O PC não previu o `PfpDirectStatus`** |

**`!DIVERGE` é o achado mais importante que o ensaio pode produzir.** Significa que a regra
110/110 do corpus histórico quebrou, e a assinatura do PC deixou de ser discriminador
confiável. Se aparecer, pare e investigue antes de qualquer outra coisa.

Saída: CSV em `docs\evidence\ensaio-pfp-<timestamp>.csv` com `PfpDirectStatus`, `PfpPcLast`,
`PfpPcTransitions`, `Rb0RptrAfter`, `Rb0WptrApplied`, `Rb0FenceAfter`, `HqdActiveStatus`,
`HqdTriadPassed`, `Verdict`.

> **Bônus**: como o HQD roda depois do PFP no mesmo `StartDevice`, o CSV também mostra se o
> tripé de compute (`HqdTriadPassed=1`) acompanha o sucesso do PFP. Isso confirma ou refuta a
> leitura da seção 3.4 de que compute e CP caem juntos.

---

## 6. Reversão (obrigatória ao fim do ensaio)

```bash
reg add "HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters" /v SkipHwBringup /t REG_DWORD /d 1 /f
```

Depois reiniciar o dispositivo (ou a máquina) e confirmar o retorno ao modo display-only:

```bash
powershell -ExecutionPolicy Bypass -File tools\soak-bc250mcdm-display.ps1 -Minutes 2 -IntervalSeconds 15
```

Esperado: `Servico: bc250mcdm`, `ConfigMgrError: 0`, `SkipHwBringup: 1`, sem regressão.

**Se a tela não voltar**: `C:\BC250_RECOVERY.cmd` restaura o `BasicDisplay`.

---

## 7. Próximos passos conforme o resultado

| Resultado | Próximo passo |
|---|---|
| `FAIL-LOOP` reproduziu e reboot recuperou | Hipótese confirmada. Atacar os suspeitos de estado residual na ordem da seção 2.4 do STATUS: icache do PFP não invalidado; TMR/PSP não reciclado (`DESTROY` prossegue sem ACK após 500 polls, [linha 972](../driver/wdk/bc250_pfp_direct_load.c:972)); `R_CP_ME_CNTL` restaurado já contaminado |
| Não reproduziu em 40 ciclos | A degradação depende de tempo ou carga, não de contagem. Repetir com intervalo maior entre ciclos, ou sob carga de compute |
| `!DIVERGE` apareceu | Parar tudo. O discriminador do PC caiu — reavaliar a seção 2.1 do STATUS antes de prosseguir |
| Falhas em modo diferente de LOOP | Ver coluna `Verdict` no CSV; pode ser um quarto modo ainda não catalogado |

Em qualquer caso, implementar a **pendência 3** (gate de preflight `PC == 0x284`, fail-closed)
sai de graça deste trabalho e converte uma falha silenciosa em recusa explícita.
