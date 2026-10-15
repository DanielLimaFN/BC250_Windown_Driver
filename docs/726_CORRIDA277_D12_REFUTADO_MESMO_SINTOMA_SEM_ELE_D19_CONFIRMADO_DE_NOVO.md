# 726 — Corrida 277: D12 refutado como causa (mesmo sintoma sem ele), D19 confirmado de novo

**Data/hora:** 13/10/2026, 09:55:43–09:59:07. **Candidato:** lab `3.427.1.0` (`DcnExt=381`, `DcnExtAuxHwModeArmed=false`, `DcnExtVTotalRefreshTestArmed=true`). **Evidência:** `docs/evidence/2026-10-13_09-55-43-DESKTOP-HQAG6T4-lab-binding/`. Continuação do [doc 725](725_CORRIDA276_D19_CONFIRMADO_TELA_COM_ARTEFATOS_O_TESTE_INTEIRO_D12_SUSPEITO.md).

## 1. Houve avanço?

**Sim, de diagnóstico: a hipótese principal da 276 (D12) está refutada.** Isolamento de variável única: exatamente a receita da 275 (D19 + D19b), com D12 desarmado — zero linha `[DCN2_AUXPAD]` no log inteiro, confirmando que D12 não executou. **O mesmo sintoma apareceu de novo**, segundo o relato do operador: tela com artefatos, mesmo padrão da 276. Isso **refuta D12 como causa** — o suspeito apontado no doc 725 não se sustenta.

## 2. Sucessos

- **D12 comprovadamente inativo** (desfecho limpo): zero ocorrência de `[DCN2_AUXPAD]` no log de ~95 MB.
- **D19 confirmado por medição pela 3ª vez**, com batimento ainda melhor: `DEVOLVIDO` 60,001 Hz contra o original medido ~60,000 Hz.
- **D19b continua respondendo**: `qpc_abs` do `DEVOLVIDO` (1622857727) e do primeiro `[DCN1_LATCH]` (1628430406) dão **~557 ms** de distância real — quase idêntico aos ~559 ms medidos na 276. Esse intervalo não depende de D12 estar ligado ou não, o que já era esperado (D12 roda e termina bem antes do D19, muito antes do primeiro flip).
- **Zero TDR, zero BugCheck, zero fault** de novo; GPU seguiu processando o tempo todo (Tctl 78-81 °C, atividade de IB crescendo).
- **Recovery limpa na 1ª tentativa**, sem reboot forçado (diferente da 276).

## 3. Falhas

- **O sintoma visual persistiu mesmo sem D12** — a tela ficou com artefatos de novo, pelo relato do operador, no mesmo padrão da 276 (não apenas no bind, autorrecuperado, como a 275).
- **A métrica que eu tinha usado como evidência na 276 (perda de VSYNC crescente) voltou a crescer igual, sem D12**: 56 perdidos aos 973 ms → 414 aos 45,5 s → 818 aos 101,6 s — taxa de crescimento no mesmo patamar da 276 (que tinha 364→727→1.180). Isso **invalida minha leitura anterior**: eu havia apontado essa perda crescente como o sinal mais limpo de que D12 era a causa; como ela se repete sem D12, não é D12 que a produz.

## 4. Veredito

**D12 refutado. Correção de registro (Lei 1): eu estava errado ao apontá-lo como suspeito principal no doc 725** — foi leitura apressada de uma comparação com `n=1` de cada lado. Com `n=1` adicional (esta corrida), a mesma perda crescente de VSYNC e o mesmo sintoma visual aparecem **sem** D12, isolando a causa para fora dele.

**O que resta como explicação, honestamente:** a 273 (muito antes de D19/D19b/D12 existirem) já registrava 10,6% de perda bruta de VSYNC (6,4% fora do DPMS) — ou seja, essa perda **já variava run a run antes de qualquer coisa desta faixa existir** (item D07, já catalogado como 🟡, "some quando D06 funcionar"). A leitura mais honesta agora é: **a 275 foi a corrida rara/limpa (perda achatada em ~1%), e a 276/277 estão dentro da dispersão já conhecida e histórica do D07/D06**, não uma regressão nova introduzida por D19b ou D12. Não há, nos dados disponíveis, uma via P0 que explique por que duas corridas consecutivas vieram "ruins" — fica como H em aberto, sem necessidade de mais mudança de código para testar.

## 5. Recuperação e pendências

- **Driver ativo ao final:** não — watchdog disparou aos ~180 s como programado; recovery limpa na 1ª tentativa.
- **NÃO VALIDADO EM HARDWARE:** a causa real do sintoma visual continua sem explicação P0. D12 está fora da lista de suspeitos.
- **Decisão do Daniel: rearmar D12.** Como ele não é a causa, não há motivo para mantê-lo fora — ele adianta D12/D13/D14 sem custo adicional de risco identificado.
- Pendência do `BC250_RECOVERY.cmd` (log sobrescrito entre tentativas) continua sem corrigir; não foi exercitada nesta corrida (recovery OK na 1ª tentativa).

## 6. Tabela-checklist da corrida 277

| Item | Resultado | Prova |
|---|:---:|---|
| D12 inativo (confirmado) | ✅ | zero `[DCN2_AUXPAD]` no log |
| D19 aplicado/medido/devolvido | ✅ | linhas 156-160 |
| D19 devolve o Hz ao original | ✅ | 60,001 Hz vs ~60,000 Hz |
| D19b: qpc_abs logado nos dois pontos | ✅ | `DEVOLVIDO qpc_abs=1622857727`, `LATCH #1 qpc_abs=1628430406` (~557 ms) |
| Zero TDR/BugCheck/fault | ✅ | contagem exata: 0/0/0 |
| Tela sem artefatos durante o teste | ❌ | relato do operador: mesmo sintoma da 276 |
| VSYNC sem perda crescente | ❌ | 56→414→818 — mesmo padrão da 276, sem D12 |
| Recovery sem reboot forçado | ✅ | `RECOVERY_OK_ONLINE`, tentativa 1 |

## 7. Tabela viva do DCN — deltas desta corrida

Ver atualização em [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md). Resumo: **D12 volta a 🧪 (pronto, suspeita descartada)** — rearmado para a Corrida 278, junto com D19/D19b. A causa do sintoma visual (persistente em 276 e 277) fica registrada como dispersão provável do D07/D06, não atribuída a nenhum código novo desta faixa.
