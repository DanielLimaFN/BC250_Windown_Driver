# Corrida 76 — VM flush no anel: execução, TDRs e recuperação

> **23/09/2026, 00:06:09–00:11:54 (America/Sao_Paulo).** Candidato lab
> **3.237.1.0**, SYS SHA-256
> `64348A9E085176FA9365A16D4C46C15E54D3302163229DF2FDB0AEE25B96EDEC`,
> ICD `5988B38E86FEBB514C4C5F1D5FB5CE5A4B6FDD2B57668F2A1B6BACA3B1A0461F`.
> Pasta: `docs/evidence/2026-09-23_00-06-09-DESKTOP-HQAG6T4-lab-binding/`.
> Critério prévio: [docs/344](344_POS_CORRIDA75_UPSTREAM_E_CRITERIO_CORRIDA76_VM_FLUSH_NO_ANEL.md).
> **Veredito: F — regressão de controle; Corrida 76 falhou.** A DX.30 foi exercitada
> mas não eliminou a classe de TDR. Não repetir neste boot.
>
> **Correção de revisão (23/09, depois da Corrida 77).** Dois pontos deste relatório foram revistos
> contra a evidência; o texto original fica abaixo como registro:
> 1. **"Regressão de controle" não está provada.** O `nodcc` saiu 4/5 porque o S5 foi **NÃO MEDIDO**
>    (`SUBMIT/FENCE res=-4`, device perdido), e não FAIL. Pela regra já usada no docs/342, variante
>    derrubada por TDR de outro processo conta como não medida. A letra F do docs/344 é literal, mas a
>    implicação "a DX.30 regrediu um controle" não se sustenta. **Pelo conteúdo**, a 76 respondeu a
>    pergunta: a DX.30 estava ativa (36 transições, ordem PM4 comprovada no dump) e os 3 TDRs de 240
>    ondas voltaram, todos após troca de VMID. A DX.30 **não basta**.
> 2. **A foto do Daniel é da Corrida 75, não da 74** (§6). Ela foi enviada logo depois da 75, que a
>    tabela do §6 omitia. Na 75 o harness teve **1 Present aceito**, copiado pelo KMD por SDMA para o
>    scanout em DstXY=640,180 (`Present #6 ... SUBWINDOW`), com fundo 0xFF2673E5 e triângulo de cor
>    interpolada. A 74 tem o mesmo padrão; a foto não pode ser dela.

## 1. Houve avanço?

**Avanço diagnóstico, não funcional.** A primeira transição registrada (#1, VMID 1→2,
fence 13) traz 35 dwords capturados sob lock: `WAIT_REG_MEM` da fence anterior,
`WRITE_DATA` PT_BASE LO/HI (`0x45FDEA001`), REQ `0x00F80004`, espera de ACK
`0x00000004`, `PFP_SYNC_ME` e `INDIRECT_BUFFER` com VMID 2, nessa ordem
(`display-log-this-bind-FINAL.txt:2539–2548`). O anel aposentou fences posteriores,
portanto passou por essa transição; não há readback isolado dos registradores no
instante exato da primeira troca. O log recuperado registra **36** transições.

Contra a Corrida 75: **3 TDRs novamente** com 240/240 ondas. O harness piorou de
1 Present aceito para **0**, e o autoteste `nodcc` de 5/5 para **4/5**.

## 2. Sucessos medidos

- `nodcc,nonggc` e `nodcc,nongg`: S1–S5 passaram, com contador de 65.536
  fragmentos. `nodcc` passou S1–S4; S5 ficou `NAO_MEDIDO` por submit/fence `-4`.
- GFX CP_RB0 avançou além das transições e, depois dos três TDRs, o reset/restart
  físico teve prova NOP+fence sobre poison nas três ocorrências. No fim do log
  recuperado: **289 submetidas = 289 aposentadas**, com `EmVoo=0` — apenas nesse
  snapshot, não uma prova de desktop estável.
- A UMD registrou textura D3D11 não-nula (grade 28.800/28.800 às 00:06:51)
  antes do Present falho; processos `dwm.exe` também registraram grades não-nulas.
  Isso é prova de bytes em superfícies, **não** de quadro aceito nem de composição
  estável no monitor.

## 3. Falhas e lacunas

- **3 TDRs** com `WaveLevelSA0=240` e `WaveLevelSA1=240`. Os IBs pendentes eram
  fence 26 / VMID 3 / 824 DW, fence 49 / VMID 2 / 288 DW e fence 139 / VMID 1 /
  280 DW. Todos vieram logo após outro VMID; os dois últimos repetem o tamanho
  da classe `DRAW_READBACK` das corridas anteriores. A DX.30 não a eliminou.
- **Tela preta observada pelo Daniel durante o ensaio**, não apenas após o
  reboot. Às 00:07:40, ainda antes do rollback das 00:08:10, a UMD registrou
  `DIRECT SCANOUT res=0` com grade 86.400/86.400 não-nula. O KMD registrou
  `Present #22` Flip com 256/256 pixels não-nulos e chamou
  `SetVidPnSourceAddressWithMultiPlaneOverlay3` em `0x112C000`. Há conteúdo na
  superfície e chamadas de apresentação, **mas não prova de pixels exibidos**.
  O ponto exato do desacordo entre memória e monitor segue não medido.
- No TDR da fence 26, o fault CID 9 aparece sob VMID 1 (anterior) enquanto o IB
  pendente era VMID 3. **Não atribuir causalidade sem ressalva**: o próprio log
  indica latch preso e que o status pode ser de um fault mais antigo. Nos TDRs
  seguintes, os faults lidos são CID 0/escrita e também têm janela de publicação
  alterada; a caminhada da tabela feita no TDR não reconstrói a PTE do instante
  da falha. `ObservedComputeVmid` não foi medido nesta corrida.
- Harness: `Present` do quadro 1 falhou com `hr=0x887A0005`,
  `DeviceRemovedReason=0x887A0007`, **0/60 quadros completos**. A linha final
  `EXIT CODE: 0 / SUCESSO COMPROVADO` do wrapper é contradita pelo próprio
  resultado interno (`TESTE REPROVADO`); não é prova de sucesso.
- O autoteste `nodcc` ficou 4/5, violando o controle 5/5 nas três variantes.
  O desktop não foi demonstrado estável: três TDRs, DWM reiniciados e watchdog
  sem confirmação. Não há prova de 10 grades consecutivas do **mesmo** `dwm.exe`
  com mouse móvel. Nenhum BugCheck foi identificado nesta pasta; o reboot
  observado foi solicitado pelo watchdog, não inferido como BugCheck.
- `display-log-final-archive.json` marcou `complete_at_capture=false`.
  `recover-bc250mcdm-lost-tail.ps1 -MaxFolders 1` salvou o trecho posterior em
  `bc250_display_log-tail-recovered.txt` (2.731.397 bytes, SHA-256
  `DD9D8A888A01A7AE5A7F061C16C5CF97E76D3FCACB4D410666503BB4445C6668`).
  Esse trecho sobrepõe parte do arquivo FINAL e, sem bind posterior conhecido,
  pode incluir atividade após a janela; não somar contagens dos dois arquivos.

## 4. Veredito frente ao critério

**F (regressão de controle), falha.** O sinal VM-FLUSH-ATIVO e a ordem do primeiro
trecho PM4 aparecem, mas o autoteste abaixo de 5/5 é assinatura explícita de F;
há TDRs da classe-alvo e 0 Present aceito. Não classificar como A/B. A assinatura
do primeiro TDR lembra C, porém o fault CID 9 pode ser antigo e não há
`ObservedComputeVmid`; não anunciar que `CONTEXT_CONTROL` está indicado por
prova conclusiva. A variável nova foi exercitada, mas seu benefício não apareceu.

## 5. Recuperação e pendências

O watchdog expirou às 00:08:10 sem confirmação explícita. A primeira recovery
online foi incompleta (exit 5) e **solicitou reboot**. No boot seguinte, a
segunda tentativa executou rollback online (exit 0), restaurou BasicDisplay
Código 0 em **5 amostras** às 00:11:45–00:11:49, e terminou em
`RECOVERY_OK_ONLINE` às 00:11:54. Leitura CIM posterior confirmou
`BasicDisplay`, Código 0. Isso é recuperação, não sucesso do nosso DWM.

**NÃO VALIDADO EM HARDWARE:** eliminação dos TDRs, desktop acelerado estável,
60 quadros D3D11, jogos e atribuição causal do fault ao VMID anterior. O próximo
passo é análise offline da cronologia de faults/IB e da falha S5/Present; se
for necessária outra variável, escrever critério novo antes de outra corrida.
Boot frio obrigatório antes de qualquer novo bind.

## 6. Reanálise visual comparativa pedida pelo Daniel (foto anexada)

A foto da conversa mostra desktop claro e um triângulo RGB sobre fundo azul,
congelados. Sem metadado confiável de captura, a foto sozinha não data a corrida.
Ela é **especialmente compatível com a Corrida 74**: nessa corrida o harness
teve **1 Present aceito** e o KMD copiou por SDMA um quadro não-nulo ao scanout.
Na 73 o harness teve **0 Presents aceitos**; havia conteúdo parcial do DWM,
mas o triângulo do harness não é atribuível à 73 pelos logs.

Comparação da prova do caminho físico, antes do rollback em cada ensaio:

| Corrida | DWM/VRAM | Cópia comprovada para scanout `0x0` | Harness | Relato visual |
|---|---|---|---|---|
| 73 | quadro completo e depois regiões/zeros pela rotação incorreta | 2 `BYTE_PROOF PASS`, com 256/256 e 246/256 amostras não-nulas | 0/60 Presents | partes/quadrados congelados |
| 74 | 5 grades de Present 86.400/86.400 após DX.28 | 3 `BYTE_PROOF PASS` (o terceiro só 1/256), incluindo quadro inicial 256/256 | 1/60 Present; triângulo azul/RGB | desktop + triângulo congelados |
| 75 *(linha acrescentada na revisão)* | grades de Present sem zerar nos 3 DWMs | 2 `BYTE_PROOF PASS` | 1/60 Present; `Present #6` SUBWINDOW 1280×720 copiado ao scanout em 640,180 | **a foto enviada pelo Daniel** (desktop + triângulo RGB, congelados) |
| 76 | grades não-nulas na UMD e 3 `TILE_PROBE` 256/256 em buffers de Flip | **0 `BYTE_PROOF PASS`**; `Present #10` copiou zero→zero ao scanout, `BlankStreak=1` | 0/60 Presents | tela preta |

Os watchdogs só iniciaram rollback às **21:51:39** (73), **22:17:50** (74) e
**00:08:10** (76). A UMD da 74 registrou um DWM apresentando com grade cheia
às 22:16:06, bem antes do rollback. Logo, a imagem vista **durante** 73/74
não pode ser descartada como apenas BasicDisplay restaurado. Mas a foto não
prova que toda a composição do desktop vinha do nosso DWM: o GOP pode conservar
pixels anteriores e o quadro ficou congelado. O triângulo aceito na 74 tem
atribuição mais forte ao nosso caminho de Present Blt.

Na 76, `Present #13/#14/#22` encontrou pixels não-nulos em buffers de Flip e
chamou `SetVidPnSourceAddressWithMultiPlaneOverlay3`, mas isso **não é prova de
scanout físico** no monitor. A diferença mensurável é que nenhum Present da 76
teve a prova byte a byte de conteúdo no scanout `0x0` que existiu nas 73/74;
o único `BYTE_PROOF` da 76 foi inconclusivo para quadro preto. Ainda houve um
`PRESENT_BLT` de Escape com 5/5 amostras não-nulas para `0x0`, o que impede
dizer que nunca existiu dado no endereço — porém não demonstrou imagem visível.
Assim, a tela preta da 76 não é o rollback: é uma **regressão de visibilidade
física**, com ponto exato entre buffer, cópia/Flip e saída de vídeo ainda aberto.

**Arquivo posterior à corrida (23/09, preparação da DX.31):** antes de substituir
`output_lab`, o pacote 3.237.1.0 e seu manifesto foram preservados em
`package-3.237.1.0-postrun-archive.zip` nesta pasta (SHA-256
`E0CCC3E8615A5976E9BE7C8895F6B532EC1351A9C61A446D521047FBDCD90B0B`).
É cópia pós-ensaio do pacote assinado, **não** log capturado durante a corrida.
