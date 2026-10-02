# 505 — Corrida 157: 48 minutos travada sem BugCheck; a trava dura está abaixo do Windows

> Corrida de 28/09/2026. Bind às 13:09:59, candidato 3.306.1.0 (o mesmo da 155/156). Trava às ~13:12:19,6 (~2 min 20 s depois do bind, antes do harness). Reboot manual às 14:00:25.
>
> Evidência:
> - pasta `docs/evidence/2026-09-28_13-09-59-DESKTOP-HQAG6T4-lab-binding/`, com a cauda recuperada (a corrida vai até a linha ~275.736);
> - logs da UMD em `umd-logs-pos-travamento/`.
>
> Nota: a tentativa anterior das 13:05 foi **recusada antes do bind** porque o monitor ativo era o "Monitor Genérico não PnP", sem EDID (o LG `GSM5B98` não foi identificado naquele boot). Não é corrida.

## 1. Relato (cinco pontos)

**1. Houve avanço?** De diagnóstico, **sim, e o maior da série**: a primeira trava com espera longa e medida, que descarta uma classe inteira de causas. Funcional, não.

**2. Sucessos (P0):**
- FPS.3b pela terceira vez: SDMA submit → conclusão com p50 1,50 ms e p90 1,56 ms (n=3.965).
- DX.95 limpa: uma troca do VMID8, zero IB em voo, zero parada do SDMA, zero TDR, zero `GFX_FAULT_SENTINELA`.
- 1.057 Presents e 5 `BYTE_PROOF PASS` antes da trava.

**3. Falhas (P0):**
- **Trava dura às ~13:12:19,6.**
  - O DWM (última linha às 13:12:19,575) estava alocando uma superfície de 11.796.480 bytes; o Explorer (13:12:19,591) entrou num `WAIT_PAGING`.
  - O último fill grande (11.796.480 bytes, 3 pacotes) **executou e concluiu** em ~1,5 ms. Todos os IBs submetidos concluíram.
  - O último registro do KMD é uma entrada de FLUSH_TLB, normal.
- **~48 minutos travada, sem BugCheck:**
  - nenhum evento 1001;
  - o evento Kernel-Power 41 traz `BugcheckCode=0`;
  - o `MEMORY.DMP` continua sendo o da 151 (09:16).
- O Daniel relatou **tela preta** (não imagem congelada).
- O FPS do harness não foi medido.

**4. Veredito contra o doc 503:** **E.** A parte da latência bateu de novo, e a corrida terminou numa trava fora do mapa.

**Contra o teste da espera longa (doc 504 §3): o desfecho é o H2.**

**5. Recuperação:**
- O fusível vai recusar o StartDevice.
- `BC250_RECOVERY.cmd`, `inspect-boot-fuse.ps1 -Reset` e boot frio **com o monitor ligado desde antes**.
- **NÃO VALIDADO EM HARDWARE:** a causa da trava.

## 2. O que a espera de 48 minutos prova

- **P0 de apoio:** o caminho de dump funciona (a 151 gerou o dump do 0x10E), e o depurador estava desligado (`NODEBUG`).
- **P1, comportamento do Windows:** o DPC watchdog roda no tique do relógio (CLOCK_LEVEL). Um processador preso em código nosso ou do Windows em DISPATCH_LEVEL ou DIRQL dispara 0x133 com dump em ~120 s.
- **Conclusão:** em 48 minutos ele não disparou. Então nenhum processador estava atendendo o relógio de forma que o watchdog pudesse agir.

  Isso **descarta** (forte, não absoluto) a classe "spinlock ou laço nosso preso" (**H1**) e aponta para uma trava **abaixo do sistema operacional** (**H2**): processadores parados numa transação de memória ou de barramento que nunca termina, ou algo da plataforma.

**Consistente com tudo o que já foi medido:**
- a GPU ociosa no último batimento;
- nenhuma migalha pendente;
- a última migalha da máquina concluída;
- nenhum evento WHEA;
- o atalho de teclado sem efeito (145);
- a tela preta.

## 3. Mecanismos possíveis dentro do H2, e o que separaria cada um

| # | Hipótese (H) | O que a sustenta | O que a enfraquece | Teste que separa |
|---|---|---|---|---|
| a | **Um acesso da CPU à VRAM pela BAR que nunca termina** (fora do nosso código instrumentado: mapeamentos da UMD/RADV ou do VidMm). Um processador parado numa leitura não atende interrupções; os outros ficam esperando uma interrupção entre processadores com prioridade acima do relógio, e o sistema congela sem watchdog. | A maior parte da VRAM movida é visível pela CPU (4.487 alocações, ~1,6 GB acumulados, contra 292 MB não visíveis). As travas vêm durante alocação e paginação. | Não há medição de acesso de CPU à BAR pela UMD. | Tirar a VRAM do alcance da CPU: alocações visíveis pela CPU no segmento de sistema (a M1b, já mapeada no doc 496). Se a trava some em várias corridas, (a) ganha força. |
| b | **Um acesso da GPU nos últimos <100 ms trava o barramento de dados** (o batimento só vê a cada 100 ms). | A taxa de travas **acompanha a taxa de paginação** (tabela abaixo). | A última migalha e todos os IBs vistos concluíram. | Persistir a última operação submetida a cada submit numa página que sobrevive ao reboot, em vez de só a cada 100 ms. |
| c | **Plataforma: energia ou temperatura** (a BC-250 é uma placa de servidor com dissipação passiva). | A tela preta; a trava sem nenhum erro. | Travas aos 77 s de um boot frio, com a GPU quase ociosa; o uso normal com BasicDisplay não trava (a confirmar com o Daniel). | Registrar as temperaturas durante a corrida com ferramenta externa (decisão do Daniel), ou perguntar se a placa já travou alguma vez fora das corridas. |

**Correlação com a taxa de paginação (P0 nas contagens, H na leitura):**

| Configuração | Travas |
|---|---|
| Antes da M1, sem KD | 5 de 5 |
| Com KD, o que deixa a paginação muito lenta (memória `bc250-kd-e-dbgprint-derrubam-a-paginacao`) | 0 de 2 |
| M1 (menos tráfego de VRAM da UMD): 149, 150 | 0 de 2 |
| Depois da FPS.1 (paginação 5× mais rápida): 152, 153, 154, 155, 156, 157 | 5 de 6 |

Uma trava que aparece mais quanto mais paginação por segundo acontece é o comportamento de uma corrida de hardware ou de protocolo, não de um defeito determinístico. Não prova nenhuma das três, mas pesa a favor de (a) ou (b) contra (c).

## 4. DCN
Nada disto aproxima o DCN da causa. A tela preta na trava é o que se espera se o controlador de memória parar, porque o scanout deixa de receber dados. Não é sinal de defeito de DCN. A recomendação do doc 504 §4 continua de pé.

## 5. Próximo passo recomendado
1. **Perguntas ao Daniel (grátis):**
   - nas travas anteriores a tela também ficou preta, ou a imagem ficou congelada?
   - a placa já travou alguma vez usando o Windows normal, sem o nosso driver?
2. **Instrumentação da (b), sem mudar comportamento:** gravar a cada submit de paginação, numa página que sobrevive ao reboot, a operação, o destino, o tamanho e a fence. Com isso, no boot seguinte se sabe qual foi a **última** operação entregue à GPU, com precisão de milissegundo. Custo: pequeno. Resposta: se a última operação é sempre do mesmo tipo (por exemplo, fill ou cópia grande de superfície), (b) ganha nome.
3. **Se (b) não nomear**, a M1b é o teste da (a). Ela é maior (três bloqueios mapeados), mas também é trabalho de produto (memória unificada).
