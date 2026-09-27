# Corrida 91 — mesmo candidato da 90, janela de 300 s

> 23/09/2026. Preparação **offline**, sem bind. Base:
> [Corrida 90](381_CORRIDA90_DX46_RESULTADO.md). Teto: **uma** corrida, depois
> de desligamento completo.

## 1. Por que só a janela

**Correção de registro do docs/381.** Escrevi que o harness da 90 foi cortado
"pelo prazo de 120 s". Não foi exatamente isso:
- o binder dá ao harness `runnerTimeout = min(180, max(60, janela − 60))`
  (`bind-bc250mcdm-lab.ps1`, ramo `RunD3D11Native`);
- com a janela de 120 s, o harness tinha **60 s**: começou por volta das
  17:45:00 e parou às 17:46:01, no submit 140;
- a ~0,5 FPS, 60 s dão uns 20–30 quadros, que é o que se viu (≥20/60 sem
  falha).

**Com janela de 300 s:**
- o harness ganha **180 s**, suficiente para 60 quadros nessa cadência;
- a coleta FINAL e o watchdog ficam com folga;
- o motor gráfico roda o bastante para o WPTR passar de **16384**, a volta real
  do anel de 64 KB, que a 90 não exercitou (máximo de 15720).

**Nenhuma mudança de driver.** O candidato é o **mesmo** da 90: lab
`3.251.1.0`, SYS `BE60274C4CCBFBD7EB273BDECE7939FAF180B6434FA44F6629FD9F8C9FE3F16C`,
UMD `45BB28C3…`, RADV DX.42 `FCAA11FE…`.

## 2. Mudança de host (procedimento)

- `EXECUTAR_D3D11_NATIVO.cmd`: `-WatchdogTimeoutSeconds` passa a vir de
  `BC250_WATCHDOG_SECONDS`, com **padrão 120** (comportamento anterior
  preservado).
- `EXECUTAR_CORRIDA91_DX46_JANELA300.cmd` define `300`. Esse valor está dentro
  dos limites do binder (30–600) e do guardião (120–600).
- Portão novo na auditoria de segurança exige o parâmetro com padrão 120.
  Verificado por mutação: voltar o valor fixo reprova.
- `prepare-corrida91-dx46-janela300.ps1`: PASS, com os mesmos hashes da 90 e a
  auditoria do pacote. DX.46 6/6 e DX.44 6/6.

## 3. Critério, escrito antes

**Controle:**
- `CntlPlanned=0x50000B0D` no bring-up;
- `Watchdog Timeout: 300 segundos` no console;
- a janela de observação do binder termina cerca de 240 s depois do bind.

Sem esses três, a corrida é **inconclusiva**.

| | Assinatura | Implicação |
|---|---|---|
| **A** | Harness **60/60** `PRESENT_OK` (`TESTE APROVADO` ou equivalente), zero TDR, zero device-lost **e** o WPTR passa de 16384 com o submit que cruza concluído | A pilha D3D11 → Vulkan → GFX aguenta 60 quadros e o anel volta corretamente. Próximo foco: desktop interativo e lentidão. |
| **A′** | 60/60 sem TDR, mas o WPTR **não** passa de 16384 | Harness fechado; a volta real do anel continua não exercitada. |
| **B** | TDR no submit que leva o WPTR além de 16384 (ou em múltiplo de 16384) | A volta real do anel tem defeito próprio (WPTR monotônico versus anel). Investigar `WPTR_HI` e o cálculo de espaço livre. |
| **C** | O harness falha antes de 60 com device-lost ou TDR, e o WPTR fica abaixo de 16384 | Classe nova ou antiga sem relação com o anel. Descrever pelo snapshot do TDR. |
| **D** | O harness não chega ao fim dos 180 s por lentidão, sem falha | Limite de desempenho. Nenhum problema de GPU medido; a lentidão vira prioridade. |
| **E — fora do mapa** | Travamento, BugCheck ou qualquer outra coisa | Descrever antes de julgar. Um travamento testa o guardião durável (DX.45b host). |

**Secundárias:**
- DX.43, 6ª amostra;
- `[PREEMPT_COMMAND]`;
- `[CP_RESET_DETECTED]`;
- quadros por segundo reais no log do harness;
- relato visual do Daniel (desktop e interação).

Desfecho parcial não é desfecho. Exemplo: A exige os **dois** sinais, 60/60 e
a volta além de 16384.

## 4. Execução

Depois de **desligamento completo** (o marcador está armado no boot das
17:47:15), em terminal elevado em `C:\Users\Daniel\Desktop\NamixWDDM`:

```
tools\EXECUTAR_CORRIDA91_DX46_JANELA300.cmd
```

A janela inteira leva uns 5 minutos. **Não interromper antes do rollback.**
