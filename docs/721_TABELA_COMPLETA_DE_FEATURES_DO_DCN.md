# 721 — Tabela completa de features do DCN: o que já funciona, o que falta, o que cada coisa faz

**Data:** 13/10/2026. Checklist de referência, complementar à Tabela viva (seção P9 de [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md)), que é a fonte oficial e é atualizada depois de toda corrida. Esta aqui existe para ler de uma vez: o que é cada feature, em português simples, e onde ela está hoje.

**Legenda:**
- ✅ **Provado na placa** (P0: medido, não hipótese).
- 🟡 **Parcial** — funciona em parte, ou medido mas sem fechar.
- 🧪 **Código pronto, aguardando corrida** — implementado, testado offline, auditado, mas ainda `NÃO VALIDADO EM HARDWARE`.
- ❌ **Testado e falhou**, com diagnóstico do motivo.
- ⏸️ **Desligado de propósito** (causa conhecida; não vale a pena ligar agora).
- ⬜ **Não iniciado.**
- 🚫 **Fora do escopo** — decisão do Daniel, não será feito.

---

## Caminho de imagem na tela (o essencial para ter desktop)

| # | Feature | O que ela faz | Status | O que já foi feito | O que falta |
|---|---|---|:---:|---|---|
| D01 | Imagem herdada do GOP | A tela que você vê ao ligar a placa (2560×1080) já vem configurada pelo firmware (UEFI GOP) antes do Windows carregar qualquer driver nosso — timing, clock de pixel, link do DisplayPort, tudo isso já está ligado. Nós não acendemos nada do zero; só lemos o que está lá e apontamos para ele. | ✅ | Base de todo o resto. | — |
| D02 | Flip pelo HUBP0 sem cópia | Quando uma janela (ou o Windows inteiro) tem um quadro novo para mostrar, alguém precisa dizer ao hardware "mostre esta imagem agora". Antes, fazíamos isso copiando 11 MB da imagem nova para dentro do framebuffer do GOP a cada quadro (lento, ~26 quadros/s no máximo). Agora, apontamos um "leitor" de hardware (HUBP0) direto para o endereço novo na memória — sem copiar nada. | ✅ | Provado desde a Corrida 269; 3.332 flips na 274, zero recusa. | — |
| D03 | VSYNC só depois do latch | O Windows precisa saber quando a troca de imagem (flip) realmente aconteceu de verdade no hardware, não só quando foi pedida — senão ele pensaria que uma imagem já está na tela quando na verdade ainda não está. | ✅ | Confirmado desde a 269. | — |
| D05 | Completar o flip pela interrupção | Em vez do driver perguntar ao hardware "já terminou de trocar a imagem?" a cada 4 milissegundos (um "cutucão" periódico), o próprio hardware agora AVISA (dispara uma interrupção) no instante exato em que a troca terminou. | ✅ | **Confirmado na Corrida 274**: 99,9% das trocas (3.330 de 3.332) foram detectadas pela interrupção, não pelo "cutucão". | — |
| D32 | Devolver tudo ao GOP ao sair | Toda vez que nosso driver para (erro, desligamento, falha), ele tem que devolver CADA registrador que tocou para o valor exato que o firmware tinha deixado — senão o driver de emergência do Windows (BasicDisplay) herda um estado quebrado e a tela trava ou corrompe. | 🟡 | Confirmado em todas as corridas 269–273. | **Na 274, o log não mostrou essa devolução acontecendo** — o desktop voltou (o guardião forçou um reboot de segurança), mas não vimos o nosso código fazer a devolução dentro do tempo esperado. Instrumentação extra entra na 275. |
| D33 | Código que roda no nível mais crítico não pode travar | Uma parte do nosso código roda dentro de uma interrupção do Windows no nível MAIS alto possível (DIRQL) — nesse nível, se você tentar "esperar" por qualquer coisa (um cadeado comum, por exemplo) e outra interrupção precisar do mesmo recurso, a máquina trava pra sempre (tela azul). | ✅ | Corrigido depois de causar uma tela azul (0x133) na Corrida 272; provado sem travar na 273/274. | — |

## Relógio da tela (VSYNC, taxa de atualização, perdidos)

| # | Feature | O que ela faz | Status | O que já foi feito | O que falta |
|---|---|---|:---:|---|---|
| D04 | Taxa de atualização real | Quando o Windows pergunta "a que taxa (Hz) essa tela atualiza?", nós respondemos com um número MEDIDO no hardware, não um número chutado/fixo. | ✅ | 59,999 Hz medido (era 56,13 Hz fixo antes). | — |
| D06 | VSYNC pela interrupção do próprio quadro | Parecido com D05, mas para o COMEÇO de cada quadro na tela (não a troca de imagem — o início do próprio ciclo de desenho). Se funcionasse, o Windows saberia exatamente quando cada quadro novo começa, sem precisar perguntar. | 🟡 | **Medido na Corrida 274**: lemos os dois registradores que decidem para onde essa interrupção vai (para nós ou para um microcontrolador interno) — os quatro bits relevantes leram 0, inclusive o do flip (D05), que SABEMOS que chega até nós. Isso prova que a hipótese simples ("0 = vem para nós") está errada. | Descobrir por outro caminho como esse roteamento é decidido neste chip específico. |
| D07 | Contagem de "avisos perdidos" | Um contador de quantas vezes deixamos de avisar o Windows sobre um novo quadro, por limitação de como medimos isso hoje (um "cutucão" a cada 4 ms em vez de uma interrupção de verdade). | 🟡 | 6,4% perdidos fora da janela de tela apagada (273). | Desaparece (ou quase) se D06 funcionar. |
| D08 | Apagar/acender a tela (DPMS) | Quando o Windows manda a tela "dormir" (economia de energia, protetor de tela), nós realmente apagamos o sinal de vídeo, e acendemos de volta quando pedido. | ✅ | Confirmado, operador viu a tela apagar e voltar. | — |
| D17 | Soma de verificação do quadro (CRC) | O próprio hardware calcula um "código de identidade" da imagem exibida. Serve para provar, sem copiar a imagem inteira, se a tela mudou ou ficou igual entre dois quadros. | ⏸️ | Ligamos e vimos que o código muda TODO quadro mesmo com a tela parada — identificamos a causa: o próprio hardware mistura um padrão de tremulação de cor (dithering) na imagem antes de calcular o código, e esse padrão gira sozinho a cada 4 quadros. | Comparar o código só entre quadros na MESMA posição do giro (a cada 4), não quadro a quadro. Opcional, não bloqueia nada. |
| D19 | Trocar só a velocidade (Hz), mesma resolução | Mudar a taxa de atualização (por exemplo, de 60 Hz para 50 Hz) SEM tocar na resolução, no relógio de pixel nem no cabo — só esticando o "tempo de descanso" entre quadros. | 🧪 | **Pronto para a Corrida 275**: muda, mede pela varredura de verdade (não só confere o registrador), e devolve ao valor original antes do desktop aparecer. Só aceita DIMINUIR a velocidade (é a direção segura). | Rodar a corrida 275. |
| D27 | VRR / FreeSync | A taxa de atualização muda sozinha, quadro a quadro, sincronizada com o que o jogo está desenhando — serve para eliminar o "rasgado" (tearing) e engasgos sem ligar v-sync tradicional. | ⬜ | — | Depende de D19 funcionar primeiro na prática. |

## Ponteiro do mouse (cursor por hardware)

| # | Feature | O que ela faz | Status | O que já foi feito | O que falta |
|---|---|---|:---:|---|---|
| D10 | Cursor a cores por hardware | O ponteiro do mouse é desenhado por um circuito próprio da GPU que soma a seta por cima da imagem — a imagem de fundo não precisa ser redesenhada toda vez que o mouse se move. | ✅ | 138 formas diferentes, 14.390 movimentos sem problema (273). | — |
| D11 | Cursor monocromático/mascarado | Formas de cursor mais simples (preto/branco, ou com transparência simples) que o Windows usa em certas situações. | 🟡 | Código pronto e testado. | O Windows, nas corridas até agora, nunca mandou esse TIPO específico de forma — não há como provar sem ele mandar. |
| D11b | Cursor com inversão (o "I" de texto) | O cursor de texto (aquele "I" que pisca ao editar texto) é tecnicamente um cursor que INVERTE a cor de tudo que está por baixo dele, em vez de desenhar uma cor fixa. | ⬜ | Identificamos que o hardware desta família de chip tem um modo para isso, mas a documentação pública não descreve como programá-lo (os bits existem, o significado não está documentado). | Fica em software (o Windows já faz isso sozinho, mais lento mas correto) até surgir uma referência confiável. |

## Monitor, cabo e identidade (AUX, EDID, link)

| # | Feature | O que ela faz | Status | O que já foi feito | O que falta |
|---|---|---|:---:|---|---|
| D15 | Detectar se tem monitor plugado (HPD) | Lê o fio que avisa "tem um cabo conectado" — hoje só LEITURA, nunca decide nada sozinho. | ✅ | Leitura confirmada nas corridas 272/273. | — |
| D12 | Canal AUX em modo correto | O DisplayPort tem um canal de dados separado (AUX) usado para "conversar" com o monitor fora do próprio sinal de vídeo. Descobrimos que o pino físico estava configurado no modo ERRADO (modo de monitores antigos/DDC, não o modo AUX do DisplayPort) — por isso toda tentativa de conversa falhava. | 🧪 | **Pronto, mas não armado ainda**: código que corrige o modo do pino, só durante a leitura, com volta garantida ao estado original. Precisa da sua autorização explícita porque é uma escrita de registrador nova. | Você decidir se arma na 275 ou numa corrida futura. |
| D13 | Capacidades do monitor (DPCD) | Depende do AUX funcionar: pergunta ao monitor "quantas pistas de dados você aceita, que velocidade" etc. | ⬜ | — | Depende de D12. |
| D14 | Identidade real do monitor (EDID) | Hoje nós INVENTAMOS a identidade do monitor (fabricante, resolução nativa) porque não conseguimos ler a de verdade. Essa feature é ler a identidade REAL do monitor físico. | ❌ | Tentado; falhou exatamente pelo motivo do D12 (canal AUX no modo errado). | Depende de D12. |
| D09 | Desligar o cabo de verdade | Quando apagamos a tela (D08), hoje só paramos de mandar imagem do nosso lado. Esta feature avisaria o PRÓPRIO monitor, pelo canal AUX, que o link vai parar — mais "educado" com o hardware do monitor. | ⬜ | — | Depende de D12. |
| D28 | Negociação do cabo (treino de link) | A "negociação" elétrica entre a placa e o monitor sobre velocidade e número de pistas de dados do DisplayPort. | ⬜ | Herdado do firmware; nunca tocamos. | Mexer aqui tem alto risco de tela preta; não planejado. |
| D29 | Som pelo cabo de vídeo | Mandar áudio digital pelo mesmo cabo DisplayPort que já leva a imagem. | ⬜ | — | Só depois do vídeo estar completamente estável. |

## Resolução, escala e qualidade de imagem

| # | Feature | O que ela faz | Status | O que já foi feito | O que falta |
|---|---|---|:---:|---|---|
| D18 | Trocar de resolução | Usar uma resolução DIFERENTE da que o firmware deixou configurada (ex.: 1920×1080 em vez de 2560×1080). | ⬜ **bloqueado** | Investigamos o que seria necessário: além de reprogramar o temporizador da tela (igual D19), isso exige um relógio de pixel NOVO — e descobrimos, lendo o código de referência deste chip específico, que **o próprio firmware (VBIOS) é quem calcula esse relógio**, não o driver de vídeo. Não temos, catalogado, o jeito de pedir isso à VBIOS neste chip. | Pesquisa própria (fora do escopo de uma corrida): descobrir como pedir um relógio de pixel novo à VBIOS, ou achar outro caminho. Até lá, continua bloqueado — tentar "no escuro" arrisca tela preta precisando de boot a frio. |
| D20 | Escala (imagem menor, estourada para tela cheia) | Mostrar um jogo/app numa resolução menor que a da tela, mas sem barras pretas — o hardware estica a imagem. | ⬜ | — | Depende de D18 ou de uma fonte menor que a nativa. |
| D21 | Rotação/espelhamento | Girar ou inverter a imagem direto no hardware (por exemplo, para um monitor montado de lado). | ⬜ | — | Não iniciado. |
| D22 | Formatos de cor além do padrão | Usar mais bits por cor (10 bits, ponto flutuante) em vez do padrão de 8 bits por canal — relevante para HDR. | ⬜ | — | Não iniciado. |
| D23 | Correção de cor (gamma/LUT) | Curvas de cor como "luz noturna" do Windows, ou calibração fina de monitor. | ⬜ | — | Não iniciado. |
| D25 | Múltiplos planos de vídeo (overlay/MPO) | Mostrar mais de uma camada de vídeo ao mesmo tempo sem recompor tudo em software (ex.: um vídeo e as legendas como camadas separadas). | ⬜ | Hoje o Windows já pergunta e nós dizemos "não dá" honestamente. | Não iniciado. |
| D26 | DirectFlip | Um aplicativo em tela cheia escrever DIRETO na tela, sem passar pelo compositor do Windows — menor latência, melhor para jogos. | ⬜ | — | Exige uma parte da nossa UMD (driver de user-mode) que ainda não existe. |
| D31 | Energia e relógios internos do DCN | Controle fino de quais partes do bloco de vídeo ficam ligadas ou "dormindo" para economizar energia. | ⬜ | Herdado do firmware. | Não iniciado; não é prioridade. |

## Fora do escopo (decisão do Daniel — não será feito)

| Item | Por quê |
|---|---|
| HDR / espaço de cor ampliado | Sem prioridade para o objetivo do projeto. |
| Segundo monitor | A placa BC-250 só tem uma saída DisplayPort física. |
| Hotplug (avisar troca de monitor automaticamente) | O fio de detecção desta placa específica é defeituoso — nem o Linux consegue usá-lo. A LEITURA do estado (D15) continua, porque ajuda a escolher o canal AUX certo; só a REAÇÃO automática a plugar/desplugar é que não será feita. |

---

## Resumo rápido (o que fazer em qual ordem, do jeito mais barato primeiro)

1. ~~D01–D05, D08, D10, D32, D33~~ — essencialmente prontos e provados.
2. **D19** (trocar Hz, mesma resolução) — pronto, aguardando a Corrida 275.
3. **D06** (interrupção de VSYNC) — precisa de mais investigação; sem ela, D07 fica pela metade.
4. **D12** (canal AUX no modo certo) — pronto, aguardando autorização para armar; abre a porta para D09, D13, D14.
5. **D17** (CRC por fase do dither) — barato, opcional.
6. **D18** (trocar de resolução de verdade) — bloqueado até resolvermos como pedir um relógio de pixel novo ao firmware; é o item mais caro e mais arriscado da lista.
7. D20–D23, D25, D26, D27, D29, D31 — dependem de algum item acima, ou de decisões futuras do Daniel.

Esta tabela deve ser atualizada junto com a Tabela viva do DCN em `BASE_DE_AVANCO.md` depois de cada corrida, enquanto o DCN não estiver completo.
