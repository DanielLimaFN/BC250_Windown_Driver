# 720 — Corrida 274: D05 confirmado no silício; recuperação precisou de reboot automático; tela preta transitória no Alt+Tab

**Data:** 12/10/2026, 23:45:17–23:49:21. **Pacote:** lab 3.424.1.0 (SYS `AB4A59C9`), `DcnExt=125` (0x7D: D06 incluído, D12 **não** armado).

**Evidência:**
- `docs/evidence/2026-10-12_23-45-09-DESKTOP-HQAG6T4-lab-binding/`;
- log do KMD completo desta corrida: `display-log-this-bind-FINAL.txt` (confirmado "completo, nada faltando" pela ferramenta de recuperação de trecho);
- `BC250_PROCESSO_NOVO.log` (sessão `20261012-234737-7140`), `BC250_MONITOR.csv`, `safe-bind-watchdog-journal.jsonl`.

## 1. Houve avanço?

**Sim, avanço funcional e de diagnóstico, com uma observação nova que precisa de investigação antes da 275.**

- **D05 (completar o flip pela interrupção) está CONFIRMADO NO SILÍCIO**, não mais hipótese: isso é avanço funcional direto do trabalho desta sessão.
- **D06 (roteamento de IRQ) foi medido** — resultado inconclusivo quanto à semântica, mas é informação nova (antes não havia nenhuma leitura desses dois registradores).
- **Achado novo, não previsto no critério:** a recuperação automática precisou de um reboot forçado antes de fechar, e o log do KMD nunca mostrou o teardown rodando. Isso é aprendizado de diagnóstico, não confirma nem refuta nada da lista de prioridades, mas abre uma investigação própria.
- A trava dura de sempre **não apareceu** nesta corrida (GPU nunca ficou ociosa e parada); o que apareceu foi uma classe de evento diferente (ver §3).

## 2. Sucessos (P0, medido)

- **D05 confirmado:** no último resumo antes do fim do log (171.168 ms armado), `latchados_via_irq_do_flip=3330 de latchados=3332` — **99,9%** dos latches foram resolvidos pela própria interrupção do flip do HUBP0, não pelo tique periódico de 4 ms. Essa proporção já estava em 97,7% no primeiro resumo (43 flips) e subiu para 99,6%+ a partir de ~8 s de corrida — ou seja, não é um efeito transitório, é o regime normal da corrida inteira.
- **Zero recusa de flip, zero falha de readback, conta sempre fechando:** em nenhum dos resumos periódicos (16 ao todo) apareceu `Recusas>0`, `fora_do_seg1>0` ou qualquer sinal de `DIVERGE`. 3.332 flips latchados ao todo nesse ponto (32 por `SET_ADDRESS` + 3.300 por `MPO3`), todos contabilizados.
- **Refresh medido:** 60,005–60,258 Hz ao longo da corrida (OTG real), igual ao padrão das corridas anteriores.
- **D06 medido (não decide, mas é dado novo):** `OTG0_INTERRUPT_DEST=0x00000000` e `DCHUB_INTERRUPT_DEST2=0x00000000` — os quatro bits de destino, **inclusive o do flip** (que sabemos chegar ao IH, D05), leram 0. Isso **refuta** a hipótese simples "0 = roteado ao host" proposta no doc 716: se fosse assim, VSTARTUP/VUPDATE/VLINE0 (que não chegam) teriam de ler diferente do flip (que chega), e os quatro leram igual. O bit sozinho não explica por que só o flip chega.
- **T3 rodou 4 ciclos completos** (códigos 0, zero falha) e um 5º em andamento quando a corrida foi interrompida pelo watchdog; nenhuma textura falhou (4/4 em todos os ciclos).
- **Temperatura e carga normais:** Tctl 72–73 °C, CPU 12–17%, sem TDR nem fault em nenhuma amostra do monitor.
- **Zero BugCheck, zero TDR, zero fault, zero trava dura.**

## 3. Falhas e observações

### 3.1 Tela preta transitória ao Alt+Tab (relatada pelo operador)

O operador relatou: ao tentar Alt+Tab depois de abrir uma janela, a tela ficou preta, mas o mouse continuava respondendo; depois voltou ao normal. Mais tarde, o watchdog agiu enquanto ele tentava abrir o vídeo.

**O que o log mostra no intervalo correspondente (~85–109 s de corrida):** a rota `SET_ADDRESS` (usada por `SetVidPnSourceAddress` direto, tipicamente transição de modo exclusivo/tela cheia de uma janela específica, diferente da rota `MPO3` da composição normal do DWM) saltou de 3 para 32 numa rajada — a maior parte disso (3→31) em menos de 6 s — e depois **parou completamente**: zero eventos `SET_ADDRESS` novos nos 62 s seguintes, enquanto `MPO3` (o desktop normal) continuou subindo sem interrupção (2.238 → 3.300) até o fim do log. Nenhuma dessas transições teve recusa, falha de readback ou flip pendente — a conta de latch bate exatamente com o total de flips em todos os resumos dessa janela.

**Leitura (H, não P0):** o padrão é consistente com uma janela específica entrando e saindo de apresentação exclusiva/tela cheia durante o Alt+Tab — comportamento em que um quadro preto transitório é esperado mesmo em GPUs de produção, porque o compositor momentaneamente não tem conteúdo da janela de origem para mostrar. Do lado do nosso driver, **nada falhou**: todo flip pedido nessa janela foi programado, confirmado por readback e latchado. Não há, nos dados desta corrida, como identificar qual processo/janela gerou a rajada — isso exigiria instrumentação adicional (ex.: nomear o processo em cada `SET_ADDRESS`), que não existe hoje.

### 3.2 Recuperação automática precisou de um reboot forçado

O watchdog atuou corretamente no prazo (174,5 s, dentro da janela de 180 s configurada), mas a **primeira** tentativa de recuperação falhou:

```
Apagando binario experimental exato: C:\Windows\System32\drivers\bc250mcdm.sys
O arquivo já está sendo usado por outro processo.
...
ERRO: C:\Windows\System32\drivers\bc250mcdm.sys ainda existe depois da exclusao forcada.
...
ERRO: recuperacao incompleta. Nao instale nem ative outro driver.
```

O dispositivo foi desabilitado com sucesso e o BasicDisplay chegou a ser confirmado (Código 0), mas o **binário do driver continuou com um handle aberto** e não pôde ser apagado. Isso disparou o mecanismo de segurança já existente no guardião (`RECOVERY_REBOOT_REQUESTED`, `watch-bc250mcdm-safe-bind.ps1:552`): reboot automático em 10 s. Depois do reboot, a segunda tentativa fechou limpa (`RECOVERY_OK_ONLINE`, 5 amostras consecutivas de BasicDisplay/Código 0).

**O mecanismo de segurança funcionou exatamente como projetado** — zero intervenção manual, zero perda de estado — mas o **gatilho é novo**: nenhuma corrida documentada em `CLAUDE.md` (269 a 273) precisou dessa escalada; todas fecharam na primeira tentativa, online.

**Achado que aponta para a causa provável:** o log do KMD desta corrida **nunca mostrou nenhuma linha de teardown** — nem `[DCN1_TEARDOWN]`, nem a mensagem de `Bc250DcnFlipRestoreGop`, nem qualquer `StopDevice`. O log termina abruptamente em pleno `PAGING_DDI` (paginação em andamento), sem nenhum sinal de desligamento ordenado. Em todas as corridas 269–273, essa linha sempre apareceu. A ferramenta de recuperação de trecho confirmou que o arquivo capturado está completo — ou seja, a ausência não é um problema de coleta, é porque **o próprio teardown não chegou a rodar (ou não chegou a logar) antes do reboot.**

Isso é compatível com uma teoria simples: se `Bc250DxgkDdiStopDeviceAndReleasePostDisplayOwnership` (ou o caminho que ele dispara) travou ou demorou demais, o handle do driver ficaria preso, explicando tanto a ausência do log de teardown quanto o `bc250mcdm.sys` "em uso" na primeira tentativa. **Não é P0** — não há, nesta evidência, uma pilha de chamada ou um dump que prove isso; é a explicação que melhor encaixa os dois sintomas juntos.

### 3.3 Itens do roteiro não alcançados

O vídeo no Edge (passo 5) não chegou a ser testado — o watchdog agiu antes. Cursor e DPMS não foram comentados pelo operador nesta corrida (sem relato de problema, mas também sem confirmação).

## 4. Veredito

Frente ao critério escrito no [doc 719](719_PACOTE_3424_D05_D06_E_CRITERIO_CORRIDA274.md):

- **Desfecho A (D05 confirmado): SIM, integralmente.** `latchados_via_irq_do_flip` é a grande maioria de `latchados` (99,9%), e a conta nunca diverge. D05 fecha por medição.
- **D06:** medido, mas não decide por si — nem confirma nem refuta de forma simples a semântica do bit; abre uma pergunta nova (ver §3, e o item atualizado no `BASE_DE_AVANCO.md`).
- **Quanto ao restante do critério (sem trava dura, sem regressão das entregas da 273):** não houve trava dura, e os números de flip/latch/timing da 273 não regrediram. Mas apareceu um **desfecho E (fora do mapa)**: a recuperação incompleta com reboot forçado, e a ausência do log de teardown. Isso não estava em nenhum dos quatro desfechos escritos antes da corrida, e por regra (Lei 10, item 2) tem de ser descrito antes de julgado — é isso que a seção 3.2 faz.

**Veredito geral: parcial.** O item que a corrida foi desenhada para testar (D05) fechou com sucesso total. Mas a corrida revelou um comportamento de recuperação que nunca tinha acontecido antes, e que precisa de investigação própria antes de empilhar mais mudança.

## 5. Recuperação e pendências

- **Driver ativo ao final:** nenhum — BasicDisplay, Código 0, confirmado em 5 amostras consecutivas na 2ª tentativa de recovery.
- **Rollback:** `RECOVERY_OK_ONLINE` na 2ª tentativa, depois de um reboot automático completo na 1ª (não solicitado pelo operador; decisão do próprio guardião).
- **NÃO VALIDADO EM HARDWARE** (como sempre, até o produto fechar): cursor, DPMS, vídeo em tela cheia nesta corrida específica (não chegaram a ser exercitados até o fim).
- **Pendência nova, antes da 275:** investigar por que o StopDevice não gerou o log de teardown e por que o binário ficou com handle aberto. Proposta: instrumentar `Bc250DxgkDdiStopDeviceAndReleasePostDisplayOwnership` com um log de ENTRADA incondicional (como já existe em outras DDIs críticas) e revisar se alguma chamada nova desta sessão (D05, D06) pode bloquear dentro do teardown — embora nenhuma das duas escreva algo que pareça capaz de travar essa DDI especificamente.
- D12 continua fora (não armado). D17 continua opcional.

## 6. Tabela-checklist da corrida 274

| Item | Resultado |
|---|---|
| D05 completar o flip pela interrupção | ✅ confirmado (99,9% via IRQ) |
| D06 leitura do roteamento de IRQ | 🟡 medido, semântica simples refutada |
| Flip pelo HUBP0 (DCN.1) | ✅ zero recusa, conta fecha |
| Timing real no VidPn | ✅ 60,0x Hz |
| T3 (processo novo) | ✅ 4 ciclos completos, 0 falhas |
| Trava dura de sempre | ✅ não ocorreu |
| BugCheck / TDR / fault | ✅ zero |
| Tela preta transitória (Alt+Tab) | 🟡 relatada, autorrecuperável, sem sinal de falha no driver |
| Teardown (`[DCN1_TEARDOWN]`) | ❌ não apareceu no log |
| Recuperação automática | 🟡 precisou de reboot forçado (mecanismo funcionou) |
| Cursor / DPMS / vídeo tela cheia | ⬜ não exercitados até o fim |
| D12 | ⏸️ não armado (decisão deliberada) |

## 7. Tabela viva do DCN (atualizada)

Ver a seção P9 completa em [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md); resumo do que mudou nesta corrida:

- **D05: ❌→✅.** Confirmado por medição direta.
- **D06: ❌→🟡.** Medido; hipótese simples refutada, pergunta reaberta.
- **D32 (retorno ao GOP no teardown): ✅→🟡.** Nenhuma linha de teardown apareceu nesta corrida; investigar antes da 275.

Nada mais na tabela mudou de estado.
