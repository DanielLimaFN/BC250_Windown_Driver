# docs/253 — Corrida 18:45: reavaliação do “freeze” e C3.16

**Corrida:** `2026-09-12_18-45-33-DESKTOP-HQAG6T4-lab-binding`  
**Candidato executado:** `3.158.1.0`  
**Desfecho:** interface estática, watchdog sem rollback e reset físico; sem BugCheck e sem live dump novo.  
**Estado pós-reboot:** fusível recusou o StartDevice (`State=2`, `Boots=2`, `LastStatus=C0000001`); `bc250mcdm` ficou em Código 43.

## Veredito corrigido

“A tela congelou” **não equivale a “a GPU parou”** nesta corrida. A fatia nova de
`C:\BC250_DISPLAY_LOG.txt`, iniciada no offset `1.067.370.707`, contém:

| Medida | Resultado |
|---|---:|
| Linhas novas | 1.179.761 |
| Bytes novos | 100.009.452 |
| Commits do journal | 38.090 |
| `McdmSubmitCommandVirtual` | 8.896 |
| `IB EXECUTADO` | 8.895 |
| Tripés completos | 2.648 |
| Conclusões com duas pernas | 6.245 |
| `Journal NO_SPACE` | 239 |
| Falhas de publicação do snapshot | 13 |
| Pico do snapshot | 876/1024 MAPPED, 0 TEARDOWN |
| TDR/reset/restart | 0 |
| GFXHUB fault registrado | 0 |

O KMD e a SDMA, portanto, continuaram trabalhando enquanto a imagem permanecia
parada. O evento posterior é `Kernel-Power 41`, consequência do reset físico, não
prova da causa original.

## Por que a imagem ficou parada

O harness RADV desenha um padrão e termina. Ele não é um desktop animado. Ao mesmo
tempo, a UMD ainda recusa a operação que o DWM precisa para desenhar:

```text
DrawIndexed: RECUSADO -- compilador/shader-state/draw PM4 ausente
```

Logo, o padrão visível prova o caminho Vulkan → WDDM → GPU → SDMA → scanout, mas
**não prova um desktop D3D11 funcional**. Manter `D3D11LabPromotion=1` faz o DWM
criar e reciclar recursos contra um backend incompleto; isso explica a enorme carga
de paginação e não deve mais ser tratado como progresso visual por si só.

## O que a 3.158.1.0 refutou

Aumentar o journal de 256 para 1024 não era uma solução arquitetural. O maior
`UpdatePT` individual teve 4.608 entradas, e a soma bruta de entradas recebidas
antes de um flush chegou a 17.471. Entretanto, o estado final publicado nunca
passou de 876 ranges.

O journal guardava **histórico de eventos**, enquanto o snapshot representa
**estado final**. Dimensionar ambos pelo mesmo número sem eliminar reescritas
obsoletas era conceitualmente errado.

## Última operação antes do reset

O último submit registrado foi o `FenceId=8896`, um `TransferVirtual` de
3.072.000 bytes, com IB de 352 bytes/88 dwords:

- origem: `[0x23F000, 0x52D000)`;
- destino: `[0x52D000, 0x81B000)`;
- aproximadamente 750 páginas por faixa;
- publicado, sem linha de aposentadoria antes do reset.

Isso **não prova** que esse submit travou: o operador pode ter resetado antes da
linha seguinte. Mas expôs uma falha objetiva do gate: ele verificava somente a
primeira, a página do meio e a última. Uma página interior inválida podia alcançar
a SDMA mesmo com os três probes aprovados.

## C3.16 implementada offline

1. **Journal como overlay de estado final.** Updates novos recortam/substituem
   intervalos antigos, preservando apenas as partes ainda válidas. Um teste repete
   17.471 escritas sobre a mesma VA e exige `ChangeCount=1`; outro substitui o meio
   de um range e exige as duas pontas intactas.
2. **Cobertura integral antes da SDMA.** IB, origem e destino agora têm todas as
   páginas de 4 KiB caminhadas e as permissões conferidas. A primeira falha recusa
   a submissão antes de tocar no motor e aciona o walker verboso somente naquela
   página.
3. **Telemetria amostrada.** Falhas e vereditos permanecem integrais; famílias de
   sucesso de alto volume emitem as 16 primeiras ocorrências e depois 1/1024. O
   teardown registra contadores totais.
4. **Guardião SYSTEM para toda corrida.** O binder deixou de usar um PowerShell
   filho descartável. A tarefa `BC250-SafeBindGuard`, persistente e `AtStartup`,
   passa a proteger todos os perfis; o runner recusa execução se não provar estado
   `OBSERVING`, tarefa `Running`, processo exato e prazo restante.

Build integral, teste do journal, auditoria de segurança e parse dos scripts:
`PASS`. O teste de safe-bind encontrou corretamente o estado local ainda inseguro
(`bc250mcdm`, Código 43) e, por isso, reprovou o preflight de ambiente; isso exige
recuperação e boot frio antes de qualquer futuro bind.

Pacote LAB `3.159.1.0` construído, assinado, auditado e **não instalado**:

| Artefato | SHA-256 |
|---|---|
| SYS fonte | `389079F3E20F440FB16559C1D0AA44B30EEE00ADE20411EBC150D9542C5407EC` |
| SYS empacotado | `1BAC538B5F51570B60A4AC8D819A6FAE34F492D6D02FA5FEF37BC044E298BC25` |
| UMD | `DD4DFED4DDC0A8E18E133FB2C82F1D2AE788F268799383BE6D91A9C36A5C13B1` |
| INF | `30FCD136EED607E4FFC223520314C7D6403F99E6956A0E49EA528C459C3ABB3E` |
| CAT | `7A481948D9D7FDAAA3E3EC85CE7C167E9237D3EC9AE6E9D9C4B6FA3AB0DBF1E0` |

## Decisão de engenharia

Não repetir o ensaio completo de DWM/RADV até existir um candidato novo e o host
estar recuperado. A próxima corrida deve ser curta e responder somente:

- o overlay elimina `Journal NO_SPACE`;
- o gate integral encontra alguma página interior inválida antes do submit;
- o guardião SYSTEM efetivamente executa rollback no prazo.

Mesmo se essas três respostas forem positivas, o próximo bloqueador para desktop
e jogos continua sendo real: implementar o backend D3D10/11 de draw/shader-state.
O padrão estático do RADV não substitui essa implementação.

**C3.16 NÃO VALIDADA EM HARDWARE.**
