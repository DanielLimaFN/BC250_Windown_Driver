# 723 — Corrida 275: D19 medido no silício, e tela com artefatos no bind (sem trava, sem BugCheck)

**Data/hora:** 13/10/2026, 08:20:37–08:23:52. **Candidato:** lab `3.425.1.0` (`DcnFlip=1`, `DcnExt=381`, `DcnExtAuxHwModeArmed=false`, `DcnExtVTotalRefreshTestArmed=true`). **Evidência:** `docs/evidence/2026-10-13_08-20-37-DESKTOP-HQAG6T4-lab-binding/`. Continuação do critério escrito no [doc 722](722_PACOTE_3425_D19_E_CRITERIO_CORRIDA275.md).

## 1. Houve avanço?

**Sim, no que foi medido; e um achado novo, não mapeado no critério.** D19 (troca de refresh pelo `V_TOTAL`) fechou por medição — primeira vez que este driver reprograma a temporização de varredura (`OTG0_V_TOTAL`) com o monitor real ativamente sincronizado nela. Avanço de capacidade: confirma que o registrador certo foi identificado e que a escrita/leitura/devolução funcionam no silício. Avanço de diagnóstico: a troca de timing ao vivo produziu um efeito visual real (tela preta cheia de artefatos) que nenhuma corrida anterior tinha produzido — informação nova sobre o custo visual de mexer em temporização do OTG com o monitor já travado nela, mesmo mexendo só no front porch (sem tocar blank/sync).

## 2. Sucessos

- **D19 confirmado no registrador (desfecho A do critério, Lei 10):** sequência completa **antes** do `StartDevice LEAVE` (ou seja, antes de qualquer `CommitVidPn`):
  - `APLICADO`: `OTG0_V_TOTAL` 0x456→0x534 (1111→1333), pedido 49,996 Hz.
  - `MEDIDO`: 50,006 Hz pela varredura (0,01 Hz do pedido — dentro da tolerância de ~0,1 Hz do critério).
  - `DEVOLVIDO`: `V_TOTAL` de volta a 0x456/1111 (idêntico ao valor do GOP), medido 59,723 Hz.
- **Readback confere em todas as escritas**; nenhuma recusa (`recusa=0x00` nas duas leituras de `DCN2_TIMING`).
- **Zero `TDR_QUIESCE`, zero BugCheck, zero `GFXHUB_FAULT_SNAPSHOT`, zero `DEVICE_REMOVED`** nos ~103 MB de log da corrida inteira (conferido por contagem, não por amostra).
- **D05 continua funcionando**: contabilidade de latch via interrupção cresceu de 0/1 (986 ms) para 17/18 (99 s) e 32/33 (156 s) — a mesma assinatura boa das corridas 273/274.
- **Cursor por hardware ficou armado e posicionado** (`DCN2_CURSOR`) antes do primeiro flip, e o operador confirma que o cursor do mouse respondia durante o episódio de artefatos — o problema não derrubou entrada nem o resto do sistema.
- **GPU seguiu trabalhando o tempo todo**: `BC250_MONITOR.csv` mostra `ib_s`/`present_s`/`fills` crescendo continuamente, Tctl estável em 75–78,5 °C (sem excursão térmica), `tdr`/`fault` em zero em toda a janela de 08:20:32–08:23:32.
- **Recuperação limpa na 1ª tentativa** (diferente da 274): `safe-bind-final.json` → `RECOVERY_OK_ONLINE`, `attempt_number=1`, BasicDisplay Código 0 confirmado em 5 amostras, sem reboot forçado. O log de recuperação (`bc250-recovery-log-attempt1.txt`) não repetiu o erro "ainda existe depois da exclusão forçada" da 274 — a pendência D32 **não se repetiu** desta vez (não valida que foi corrigida; só que não ocorreu nesta corrida).

## 3. Falhas

- **Achado novo, fora do mapa do critério (Lei 10, regra 2): tela preta cheia de artefatos no momento do bind.** Relato do operador: os artefatos apareceram, persistiram por um tempo perceptível (não foi um flash de 1-2 s), o cursor do mouse continuou respondendo durante esse período, os artefatos depois desapareceram por conta própria, e o watchdog agiu normalmente ao final — **sem precisar de reset nem religamento**.
  - **O que os logs confirmam (P0):** nenhum travamento, nenhum BugCheck, nenhuma falta de GPU durante toda a corrida; a GPU continuou processando IBs e Presents; o primeiro flip do HUBP0 (linha ~1246) latchou em 53.152 µs (~53 ms, 3+ quadros) — mais lento que o normal, mas não é inédito (é o mesmo padrão "primeiro flip custa mais" já visto antes de a interrupção de flip estar armada).
  - **Hipótese mais provável (H, não P0):** o próprio D19 é a causa. É a primeira vez neste projeto que o driver reprograma `OTG0_V_TOTAL` (a temporização de varredura) com o monitor físico já sincronizado nela. A troca foi "segura" no sentido de só alterar o front porch (blank_start e sync ficaram intocados: antes `V 3/10`, depois `V 225/10`, sync sempre `10`) — mas um monitor real pode perder o lock por um tempo ao ver o total de linhas por quadro mudar de 1111→1333→1111 em menos de um segundo, mesmo sem a área ativa ou o pulso de sync terem mudado. Isso é compatível com "tela preta cheia de artefatos" que depois se resolve sozinha.
  - **O que não dá para descartar (H concorrente):** o primeiro flip lento (53 ms, sem interrupção ainda armada) ocorre poucas centenas de linhas de log **depois** do D19 (ou seja, próximo no tempo); não é possível, só pelo log, separar com certeza "artefato por causa do V_TOTAL" de "artefato por causa do primeiro conteúdo real sendo composto no HUBP0 antes de tudo assentar". As duas coisas aconteceram perto uma da outra.
  - **Discrepância pequena a registrar:** o Hz medido após `DEVOLVIDO` (59,723 Hz) ficou ~0,27 Hz abaixo do Hz medido antes de qualquer mudança (59,996 Hz), mesmo com `V_TOTAL` lido de volta exatamente igual (0x456/1111, idêntico ao do GOP). O registrador bateu bit a bit; só a frequência **medida pela varredura** variou um pouco mais que a tolerância usada para o `APLICADO` (~0,1 Hz). Mais compatível com ruído da própria medição (a contagem de quadros por tempo decorrido) do que com um desvio real de timing, mas fica registrado sem arredondar.
  - **A ferramenta T3 (`RODAR_PROCESSO_NOVO.cmd`) não foi exercitada nesta corrida**: o `BC250_PROCESSO_NOVO.log` capturado na pasta de evidência é um resíduo da Corrida 274 (timestamps de 12/10 23:47–23:48), não desta corrida. Não há prova de que o operador conseguiu rodá-la com a tela comprometida.

## 4. Veredito

**Parcial.** Pelo critério escrito antes (doc 722): o desfecho **A** (D19 confirmado por medição) **ocorreu exatamente como descrito** — isso fecha a pergunta "a troca de refresh pelo `V_TOTAL` funciona no silício?" com sim. Mas a corrida também produziu um resultado que **não estava em nenhuma das letras A–E do critério**: um efeito visual real e incômodo (tela cheia de artefatos) que não trava nada, não gera BugCheck, mas também não é "nenhuma regressão" — é categoria nova. Pela Lei 10 (regra 3, "desfecho parcial não é desfecho"), não force isso em "A" (que falaria só do registrador) nem em "D" (que falaria de regressão do que já estava fechado): é um resultado descrito por si, ainda sem causa provada.

## 5. Recuperação e pendências

- **Driver ativo ao final:** não — o watchdog de 180 s disparou o rollback como programado, e a recuperação removeu `bc250mcdm.sys` e devolveu BasicDisplay (Código 0) **na primeira tentativa**, sem reboot.
- **NÃO VALIDADO EM HARDWARE:** a causa exata dos artefatos. H principal = efeito do próprio D19 ao trocar `V_TOTAL` com o monitor sincronizado; H concorrente = o primeiro flip do HUBP0 antes da interrupção estar armada. Nenhuma das duas é P0.
- **Pendência D32 (doc 720) não se repetiu nesta corrida** — bom sinal, mas uma corrida sem o erro não prova que foi corrigido; o diagnóstico novo (`Win32_SystemDriver`) ficou pronto e não precisou disparar.
- **Próximo passo recomendado, sem gastar corrida ainda:** isolar as duas hipóteses por leitura/desenho antes de propor novo ensaio — por exemplo, uma corrida que meça só o efeito do D19 isoladamente (sem deixar o HUBP0 fazer o primeiro flip tão perto no tempo), ou que insira uma pausa/medição entre o `DEVOLVIDO` do D19 e o primeiro `CommitVidPn`, para separar os dois eventos no log por tempo real (timestamp), não só por posição de linha.
- D12 continua fora do pacote, sem autorização. D18 continua bloqueado (VBIOS/ATOM).

## 6. Tabela-checklist da corrida 275

| Item | Resultado | Prova |
|---|:---:|---|
| D19 aplicado antes do desktop aparecer | ✅ | `APLICADO` na linha 156, `StartDevice LEAVE` na linha 519 |
| D19 mede Hz próximo do pedido | ✅ | 50,006 Hz medido vs 49,996 Hz pedido (Δ0,01 Hz) |
| D19 devolve `V_TOTAL` ao valor do GOP | ✅ | 0x456/1111, idêntico ao lido do GOP no boot |
| D19 devolve o Hz exatamente ao original | 🟡 | 59,723 Hz medido vs 59,996 Hz original (Δ0,27 Hz); registrador bate, frequência medida não bate igual |
| Zero TDR / BugCheck / fault na corrida | ✅ | contagem exata no log: 0/0/0/0 |
| D05 (flip por interrupção) continua funcionando | ✅ | 32/33 latchados via IRQ aos 156 s |
| Tela sem artefatos durante o bind | ❌ | relato do operador: preta e cheia de artefatos, autorrecuperada |
| Recovery sem reboot forçado | ✅ | `RECOVERY_OK_ONLINE`, tentativa 1 |
| T3 exercitada | ⬜ | log capturado é residual da 274, não desta corrida |
| D12 armado | ⏸️ | deliberadamente fora, sem autorização |

## 7. Tabela viva do DCN — deltas desta corrida

Ver atualização em [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md) (seção P9). Resumo das mudanças:
- **D19**: de 🧪 (pronto, não testado) para 🟡 (medido no registrador; Hz do registrador bate, Hz medido na devolução com pequena discrepância; e um efeito visual novo, não mapeado, correlacionado no tempo).
- **D32**: nota atualizada — a pendência da 274 (recovery sem log de `StopDevice`) não se repetiu na 275 (recovery limpa na 1ª tentativa), mas isso não fecha o item; fica como estava, aguardando repetição ou explicação.
