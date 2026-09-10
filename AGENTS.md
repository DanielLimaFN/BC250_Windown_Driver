# AGENTS.md — Diretrizes Universais para Agentes de IA

> **Governança para qualquer agente autônomo neste repositório** (OpenAI Codex, ChatGPT,
> Copilot Workspace, Cursor, auto-agents).
> *Target: AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64*
> **Última revisão: 2026-09-07** · Núcleo normativo idêntico a `CLAUDE.md`, `GEMINI.md` e `.cursorrules`.

---

## 🎯 1. Onde queremos chegar

**Jogos no Windows 10 22H2 rodando na BC-250** — 24 CUs, 16 GB GDDR6, monitor no DisplayPort da
própria placa, D3D12/Vulkan, **sem WARP e sem fallback de software**.

**Fonte única de verdade:** [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md).
- **Tabela 1** — o que já está validado no silício (45 itens, com prova arquivada).
- **Tabela 2** — o que falta, em ordem de prioridade `P1→P9`, **com o teste que fecha cada item**.

**Roadmap:** [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md).

### Arquitetura vigente
Display v1 é **por herança do UEFI GOP**: VidPn de um modo único, scanout em VRAM real,
`DxgkDdiPresent` no modelo **blt** por SDMA, VSync **lido** do frame counter do OTG.
**Não implementamos DCN no caminho crítico.** Escrita em registrador de DCN é **proibida**
fora da prioridade P9. Leitura é permitida.

### Onde estamos, de verdade
O adaptador WDDM **alcançou Código 0 em 02/09/2026** com `DRIVERCAPS`, `QUERYSEGMENT4`,
Paging Context e VidPn ativos no silício por 121 s (itens 97 e 102 fechados com evidência
arquivada). O compute direto e o display KMDOD/GOP também estão provados no silício.
A corrida de produção de 07/09 16:19 chegou a Código 0, MMU global ativa, CommitVidPn e
12 Present, mas a primária ficou zerada e o DWM fail-fastou com `0x887A0004` porque a UMD
não tem backend D3D10/11. O próximo bloqueador é o IB WDDM real, seguido do backend D3D.

---

## 🛑 2. Barra de status obrigatória

Abra toda resposta técnica com esta barra. **Ela reflete medição, não desejo.**

```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pre-WDDM (Lab)        ██████████████████▋░  93%   Estavel e Isolado
SDMA (DMA Engine & Robustez)     ████████████████████  98%   B1 FECHADO: IB WDDM real sob VMID8 com TRIPE completo (5 canarios); wrap do anel causou 0x119
GCVM & Traducao de Memoria       ███████████████████░  95%   MMU global 26 regs, GPUVA de IB resolvida no silicio, Generation=54; Node1 nao provado
RLC, CP & PM4 Direct Load        ███████████████████░  95%   Fence em VRAM provada
Telemetria & Observabilidade     ██████████████████░░  90%   B2 FECHADO: logs da UMD coletados no lab; dwm.exe E_NOTIMPL provado no silicio (P0)
Compute & Async Queues (MEC/MQD) ██████████████████░░  90%   P1 17/17 e P2 16/16 provados; 1º submit Node1 no silicio (WPTR 204->216); tripe Node1 falhou (disputa VMID8)
Miniport WDDM Display+Render     ███████████████▋░░░░  78%   08/09: lab tambem comita -- CommitVidPn + SetVidPnSourceAddress + 60 GetStdAlloc + 367 VSYNC; duas paredes viraram UMA (D3D10/11); tela preta
=============================================================================================
Veredito Global: a corrida de 06/09 20:34 validou a correcao da root SegmentId=1, Offset=0.
Os IBs de paginacao 0x22F000/20 tiveram snapshot Status=0 e caminhada CPU/PT coincidente.
Dois VIRTUAL_FILL diretos (81.920 e 4.096 bytes) fecharam RPTR==WPTR, fence fisica sobre
poison e dados byte a byte. Isso nao prova execucao do IB sob VMID8 nem compute Node1.
=============================================================================================
O display do perfil lab nao convergiu: 97 IsSupportedVidPn e 65 EnumVidPnCofuncModality,
todos sem GetStandardAllocationDriverData de primaria, CommitVidPn, SetVidPnSourceAddress
ou Present. A corrida de producao de 05/09 comitou apos 18 consultas; qual flag do perfil
lab causa a divergencia permanece NAO VALIDADO EM HARDWARE.
CORRIGIDO EM 08/09 PELA MINERACAO DA CORRIDA 13:00: dizer que o perfil lab "nao convergiu" era
LEITURA ERRADA da minha parte. Ele CONVERGE -- 39 vezes na 13:00, com Scale=1/Rot=1, os dois
modos fixados e "TargetMode matches GOP -> Supported=TRUE!", o mesmo estado que a producao
comita -- e a convergencia e' DESCARTADA 46 vezes. E' um LACO, nao uma nao-convergencia, e o
que o quebra e' um processo que nasce e morre entre a convergencia e o commit. Ver o bloco da
corrida 13:00 e docs/201.
=============================================================================================
Faixa A (arrumar) em 06/09, tudo offline e sem corrida: A1 caps de tipo de driver passaram a
sair de fonte unica (Bc250GetDriverCapsPolicy) -- SupportNonVGA corrigido para TRUE nos dois
handlers, e o teste que congelava FALSE foi refeito; SupportSmoothRotation e SupportDirectFlip
seguem FALSE por decisao citada na norma, com DirectFlip como DIVIDA DECLARADA. A5 build do SYS
passou a usar /Oy- (FPO desligado, exigencia de WDDM 1.2+ e pre-requisito da call stack do dump).
A8, achado no caminho: build-contract.bat estava em LF e o cmd.exe comia o primeiro caractere das
linhas depois do vcvars -- readonly_miniport_test NUNCA linkava. Convertido para CRLF, a suite
inteira voltou a EXIT=0 e o teste passou a existir. Dois gates novos na auditoria de seguranca.
A6 o binder de laboratorio passou a arquivar o log integral do KMD com SHA-256.
A2 e A3 foram reclassificados por leitura de codigo: o EDID sintetizado JA e coerente com os
56,13 Hz medidos (px 173.611.437 / 2784 / 1111), e derivar o mode set do target do monitor e
circular enquanto o EDID e sintetizado por nos -- depende do item 77 (EDID real por AUX).
Nada disso foi validado em hardware: sao mudancas de contrato, build e coleta de prova.
=============================================================================================
Toolchain migrada em 07/09: VS2026 18.9.1 / MSVC 19.51.36256.0 com SDK+WDK
10.0.28000.2526 (layout 10.0.28000.0). KMD, UMD, lab, RADV gate e suites offline passaram;
pacotes ativo 3.81.0.0 e lab 3.81.1.0 foram assinados e auditados. Esta migracao nao
constitui prova de execucao: efeito no silicio -- NAO VALIDADO EM HARDWARE.
Handoff da toolchain: docs/173_TOOLCHAIN_VS2026_WDK28000.md
=============================================================================================
O RADV entrou no ICD explicito e parou dentro de vkCreateDevice. O KMD criou dois contextos
Node1, mas recebeu zero submit Node1. O ultimo registro foi BuildPagingBuffer Op=11; o dump
de hang ficou com zero bytes e houve reboot nao limpo sem TDR/bugcheck. Ponto interno exato,
compute Vulkan, desktop visivel e jogos -- NAO VALIDADOS EM HARDWARE.
Handoff completo: docs/172_HANDOFF_2026-09-06_20-34.md
=============================================================================================
07/09, tudo OFFLINE e sem corrida -- tres das quatro perguntas de registrador que sustentavam
o B1 fecharam por leitura do NOSSO PROPRIO catalogo, que ja tinha as respostas com P1:
Q4 PAGE_TABLE_DEPTH = Levels-1 = 1 (docs/03, travado por gcvm_context8_cntl_test);
Q5 a PTE carrega PA, nao MC -- PA = offset_de_VRAM + (GCMC_VM_FB_OFFSET << 24);
Q3 o hub esta CERTO (sdma_v5_0.c: ring->vm_hub = AMDGPU_GFXHUB(0)) e a invalidacao confere
nos cinco pontos do catalogo, logo FAULT_STATUS=0 e medicao de verdade. A CSA ja havia sido
descartada. Nao eram lacunas de conhecimento; eram lacunas de BUSCA.
HIPOTESE (H) que substituiu a Q3: as sete classes de fault tem par ENABLE_DEFAULT/INTERRUPT;
o POST tinha os 14 ligados e o nosso alvo liga os 7 DEFAULT e DESLIGA os 7 INTERRUPT. Pela
tabela do catalogo, DEFAULT=1 redireciona para a dummy page. Isso explica as tres medicoes do
B1 juntas: RPTR==WPTR, FAULT_STATUS zero, destino sem dados. NAO PROVADO -- nao se sabe se o
STATUS latcha com DEFAULT=1 e INTERRUPT=0; se latchar, a hipotese esta REFUTADA.
Proxima corrida proposta produz informacao nos DOIS desfechos: ligar as 7 *_ENABLE_INTERRUPT
(0x00555403 | 0x002AAA00 = 0x007FFE03, que difere do POST em so 0x83) e submeter o MESMO IB.
Chega IV de fault -> confirmada, e o FAULT_ADDR diz qual endereco. Nao chega -> refutada, a
engine nao executa o pacote. Declarado em BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE e
deliberadamente NAO usado por nenhum caminho: armar e decisao do dono do projeto.
Faixa A11 no mesmo dia, offline: geometria de GPUVA passou a ter fonte unica. O blob
UMDRIVERPRIVATE publicava GpuVaBits=48 enquanto o driver declarava VaBits=34 ao WDDM (medido
no log 11:08) -- defeito meu, segunda fonte de verdade, corrigido para sair de runtimeCaps e
travado por gate com teste de mutacao. Tambem: BC250_ADAPTER_GPUVA_BITS passou a derivar,
o journal de GPUVA deixou de aceitar VA ate 2^48 (fail-open; agora 2^34), o bloco de
PROVENIENCIA de bc250_gpuva_space_contract.h estava FALSO (descrevia o desenho plano anterior)
e docs/03 afirmava falsamente que o driver le GCMC_VM_FB_OFFSET em vez de usar constante --
corrigido, com log de DIVERGENCIA no SetRootPageTable e zero mudanca de comportamento.
Build do KMD e do UMD EXIT=0, tres auditorias PASS. NADA DISSO FOI VALIDADO EM HARDWARE.
Handoff completo da sessao de 07/09 (offline, zero corridas):
docs/174_HANDOFF_2026-09-07_GPUVA_FONTE_UNICA_E_TELEMETRIA_SDMA.md
Detalhe por pergunta: docs/167_CONSULTA_EXTERNA_IB_PAGINACAO_SDMA_VMID8.md secoes 8b em diante
BLOQUEADOR ATIVO: HKLM/SOFTWARE/BC250/Umd/D3D11CapsProbe ficou em 1 e precisa ser DESARMADO
antes de qualquer bind -- tools/arm-d3d11-caps-probe.ps1 -Disarm. Com ele ligado a UMD publica
a tabela de adapter, o DWM falha no 3DPIPELINESUPPORT e fail-fasta: tela preta de proposito.
O pacote output_lab esta ESTAGNADO (anterior as faixas A11/A12); reconstruir exige o flag de
bring-up destrutivo, que e decisao do dono do projeto.
=============================================================================================
Faixa A12, mesmo dia e tambem offline -- a pergunta 2 do doc 167 deixou de ser vazia. O
docs/168 ja tinha os dados e declarava, na secao 6 item 6, que a semantica de CONTEXT_STATUS
e SKIP_CNTL "nao sei o que significa e nao interpretei". Estava em
inc/linux_ref/gc_10_1_0_sh_mask.h, que existe neste repo -- eu havia escrito no doc 167 que
nao havia copia local do upstream, e isso era FALSO: nao ha .c do amdgpu (logica = H), mas ha
a tabela de mascaras (campos de registrador = P1). Decodificando as leituras de 05/09:
  PHYS/VMID0  CONTEXT_STATUS=0x0D = SELECTED + IDLE + EXPIRED
  EXEC/VMID8  CONTEXT_STATUS=0x09 = SELECTED + EXPIRED, IDLE=0
  PHYS IB_CNTL=0x00000101 e EXEC IB_CNTL=0x00080101
Os dois CONTEXT_STATUS diferem em UM bit -- o IDLE -- e os dois IB_CNTL em UM campo -- o
CMD_VMID. Medicao P0 mais decodificacao P1, nao hipotese. HIPOTESE (H): sob VMID8 a engine
consumiu o pacote do ANEL, latchou IB_BASE e CMD_VMID e PAROU DENTRO DO IB; casa com a
hipotese da pergunta 3. NAO RESOLVE: a variante PHYS foi a IDLE, ou seja TERMINOU, e a prova
de bytes dela tambem falhou -- sem traducao envolvida, aquela falha nao pode ser de GCVM. Sao
DUAS falhas diferentes. Armadilhas catalogadas: IB_RPTR/IB_OFFSET tem o campo comecando no
bit 2 (0x20 sao 8 dwords, nao 32) e EXCEPTION e campo de 3 bits, nao flag; o bit 1 de
CONTEXT_STATUS nao tem nome no upstream e sai como BITS_SEM_NOME. O SubmitCommandVirtual do
Node 0 passou a emitir IB_QUEUE_RAW/DECODED/STATUS/DISCRIMINANTE no caminho de TIMEOUT --
somente leitura, fora do caminho de sucesso para nao pesar o Present a 60 Hz. A nova
telemetria NUNCA RODOU: NAO VALIDADA EM HARDWARE.
=============================================================================================
CORRIDA 07/09 16:19 (P0) e faixa A13 no mesmo dia (OFFLINE, zero corridas). A corrida chegou a
Codigo 0, MMU global Regs=26, CommitVidPn, SetVidPnSourceAddress(0xAB0000) e 12 Present, e a
tela ficou PRETA: quatro dwm.exe fail-fastaram com e0464645/887A0004 e a UMD recusou
OpenAdapter10/10_2 (512 e 1008 vezes). Handoff: docs/179.
Mineracao do log da 16:19 -- contagem exata, P0: 14 "IB EXECUTADO" = 2 PLANO A DIRECT FILL +
12 PLANO A SYNC NOP, ZERO INDIRECT e ZERO linhas IB_QUEUE_*. Ou seja a corrida recomendada da
A12 JA FOI GASTA e nao produziu informacao: os PLANO A tem precedencia e absorveram 14/14, o
INDIRECT nao foi exercitado e a pergunta 2 do doc 167 continua SEM RESPOSTA.
Faixa A13, tudo offline: (1) a frase "IB EXECUTADO" saia nos TRES ramos -- falso sucesso da
Lei 4, corrigido, agora so o INDIRECT tem direito a ela e os PLANO A dizem que a operacao foi
REIMPLEMENTADA no anel e que a GPU NAO leu o DMA buffer; (2) ACHADO NOVO -- a linha
"TRIPE NO SILICIO COMPROVADO WPTR=68 RPTR=52" nao era telemetria errada, era GATE errado: a
perna 1 (RPTR==WPTR) nunca entrava na decisao do bringup e um timeout de 2000 polls passava.
Agora o veredito sai de Bc250ClassifySdmaTripod, o log diz QUAL leitura de RPTR casou e anel
nunca armado sai INDETERMINADO; (3) tunable SdmaIndirectOnly (default 0, fail-closed) desliga
a precedencia dos PLANO A para que o INDIRECT possa ser medido -- armar e decisao do dono.
Testes: submit_command_contract_test e blt_proof_contract_test com casos novos, build-contract
EXIT=0, tres auditorias PASS e os dois portoes novos verificados POR MUTACAO. As cinco strings
do candidato estao no .sys. NADA DISSO FOI VALIDADO EM HARDWARE.
Handoff: docs/180_HANDOFF_2026-09-07_A13_HIGIENE_DE_PROVA.md
PACOTE DO ENSAIO DO INDIRECT, assinado e auditado em 07/09 20:03 e NAO INSTALADO:
DriverVer 3.82.0.0, fonte 1C30E3B1..., assinado 0CF9472F..., CAT 7798EC55..., INF D0F73BE5...
O INF declara SdmaIndirectOnly=1 e o manifesto DERIVA esse campo do INF (o empacotador
recusa o pacote se a linha faltar). As cinco strings do candidato estao no SYS assinado.
Novo: tools/arm-sdma-indirect-only.ps1 arma/desarma sem reinstalar, e o binder ganhou dois
preflights -- o do D3D11CapsProbe, que a norma exigia e NAO EXISTIA (medido agora: chave
presente, valor ausente = desarmado), e o aviso do modo de submissao lido do manifesto.
Risco declarado desta corrida: com os PLANO A suprimidos, submissao cuja GPUVA de IB nao
resolva passa a RECUSA (STATUS_IO_DEVICE_ERROR), caminho conhecido do BugCheck 0x119.
A UMD nao foi recompilada e BC250_D3D10_11_NATIVE_READY continua 0: a tela deve ficar preta
de novo. Esta corrida mede o INDIRECT, nao o desktop. NAO VALIDADO EM HARDWARE.
=============================================================================================
CORRIDA 07/09 17:13 (P0) -- O IB WDDM REAL EXECUTOU SOB VMID8. Com SdmaIndirectOnly=1 lido
(present=1), o Node 0 escolheu 10 INDIRECT e 4 RECUSADO em 14 submissoes: dez
"IB EXECUTADO (INDIRECT)" de DMA buffers de 64 bytes em VA=0x22F000..0x22F1C0, cada um com
RPTR==WPTR e fence 0x1002..0x1014 sobre poison, ~111 polls, zero timeout e FAULT_STATUS=0.
A divida central -- "o INDIRECT e o unico que nao funciona" -- esta REFUTADA como formulada.
O risco declarado NAO se materializou: as 4 recusas devolveram STATUS_IO_DEVICE_ERROR, sem
BugCheck, e o dispositivo ficou em Codigo 0 ate o rollback (ONLINE ROLLBACK: PASS).
RESSALVA QUE MANDA NO VEREDITO: "IB_TRIPOD PASS" era rotulo de DUAS pernas -- o gate nao
conferia byte nenhum do destino. Pela Lei 2 o B1 NAO FECHA com isso.
A pergunta do tripe de boot fechou por medicao: TRIPE_VEREDITO disse RPTR_Wb=52 RPTR_Reg=68
Casou=REGISTRADOR -- o 52 de todas as corridas era o WRITEBACK atrasado, e o item 63
sobrevive ao gate estrito.
Display avancou e continuou preto: 5 CommitVidPn, 5 SetVidPnSourceAddress, 20 Present (10 com
SDMA=YES), mas 3 BYTE_PROOF INCONCLUSIVO com NonZero=0/256 e BlankStreak=3; 7 falhas de
dwm.exe (e0464645) e dois dumps de ~244 MB e ~203 MB. A imagem que apareceu no fim NAO e
atribuivel ao nosso driver -- coincide com o rollback restaurando o BasicDisplay.
Faixa A14, offline: (1) a perna 3 do tripe passou a EXISTIR no INDIRECT, por canarios nao
nulos plantados antes do disparo (zero sobre zero e inconclusivo; zero sobre poison e prova),
com Bc250PlanDataProofCanaries em fonte unica com o fill direto -- a string IB_TRIPOD PASS
deixou de existir no binario; (2) Trim() na confirmacao visual do binder, que recusou o
DISPLAY_OK digitado por espaco na borda; (3) DestroyProcess passou a invalidar as faixas de
GPUVA do processo -- o REJECT source contract dos Present #16..#20 tinha status=10
OWNER_MISMATCH em faixa viva de um dwm.exe MORTO, defeito nosso de ciclo de vida.
CORRECAO DE ENQUADRAMENTO: eu havia dito que esse REJECT era "a proxima parede do video";
minha propria mineracao refuta -- SlotSeg=0 e status=9 dizem que a origem nao estava
residente porque o DWM morreu antes. A parede continua sendo o backend D3D10/11.
Testes: blt_proof_contract_test com 5 blocos novos, build-contract EXIT=0, quatro auditorias
PASS e os dois portoes novos verificados POR MUTACAO.
PACOTE DA PERNA 3, assinado e auditado, NAO INSTALADO: DriverVer 3.83.0.0, fonte DCA7D06D...,
assinado FDCC5AB2..., CAT F7A6DD4F..., SdmaIndirectOnly=1. NADA DISSO FOI VALIDADO EM HARDWARE.
Handoff: docs/181_HANDOFF_2026-09-07_17-13_INDIRECT_EXECUTOU.md
=============================================================================================
CORRIDA 07/09 17:43 (P0) -- B1 FECHADO NO SILICIO, e BugCheck 0x119 por aritmetica nossa.
Duas submissoes sairam com IB_TRIPE_VEREDITO: TRIPE_PROVADO -- as TRES pernas sobre o DMA
buffer REAL do dxgkrnl: RPTR==WPTR nas duas leituras, fence da GPU sobre poison 0x43DAFFFE e
CINCO canarios nao-nulos plantados antes do disparo, todos substituidos (Divergentes=0,
PostGPU=[0 0 0 0 0], Esperado=0). Zero sobre poison e conclusivo. O IB de paginacao executa
sob VMID8 e PRODUZ o efeito declarado. Isso nao fecha Node 1, desktop nem jogos.
Depois, na submissao FenceId=24: IB_TRIPOD TIMEOUT TargetWPTR=516 RPTR=4612 com
FenceObs==FenceExp==0x1030. 4612 = 4096 + 516 e o anel tem 4096 bytes -- a GPU TERMINOU e a
comparacao CRUA de RPTR nunca fecharia. ~1 s de polling, IB NAO EXECUTADO,
STATUS_IO_DEVICE_ERROR e o VidMm derrubou a maquina: 0x119 Arg1=2 Arg2=0xC0000185.
A aritmetica JA existia no arquivo (Bc250SdmaRptrMatches, com comentario da MESMA falha em
17/17 fills); o caminho INDIRECT nao a chamava. E a auditoria EXIGIA a comparacao crua no
caminho de blt -- o gate obrigava o defeito a existir. Os dois corrigidos.
O mesmo defeito envenenou o diagnostico: IB_QUEUE_DISCRIMINANTE disse anel_drenado=0 com o
anel drenado. Dado novo da telemetria A12, que finalmente disparou: IB_RPTR=15 dw,
IB_SIZE=0, IB_SUB_REMAIN=0, CONTEXT_STATUS=09 (SELECTED+EXPIRED, IDLE=0), ERR_LOG=0x0F.
O FUSIVEL ANTI-BOOT-LOOP FUNCIONOU: apos o reboot o StartDevice foi recusado (State=2,
Boots=6) -- e por isso o dispositivo esta em Codigo 43 com bc250mcdm ligado. NAO ha loop de
boot; eu havia alarmado risco de repeticao antes de ler o fusivel, e retrato.
Faixa A15, offline: Bc250TripodWrapRingPointer em fonte unica, perna 1 do INDIRECT pelo
helper, anel_drenado corrigido, comparacoes cruas removidas dos tres sitios, gate novo que
PROIBE a comparacao crua, e teste com os numeros medidos (4612/4096/516) mais o caso negativo
4608 para a reducao nao virar peneira. build-contract EXIT=0, quatro auditorias PASS.
DIVIDA DECLARADA: o laco do tripe pode gastar ~1 s segurando spinlock em DISPATCH_LEVEL; e
tripe que falha de verdade continua devolvendo STATUS_IO_DEVICE_ERROR -- a resposta correta e
o item 25 (TDR), que segue ABERTO.
ANTES DE NOVA CORRIDA: tools/BC250_RECOVERY.cmd elevado e tools/inspect-boot-fuse.ps1 -Reset
(o fusivel esta TRIPPED e o binder recusa), depois boot frio.
PACOTE 3.84.0.0 assinado e NAO INSTALADO: fonte 36B68E8C..., assinado 1D47F15C...,
CAT 3CF45DF9..., SdmaIndirectOnly=1. NADA DA A15 FOI VALIDADO EM HARDWARE.
Handoff: docs/182_HANDOFF_2026-09-07_17-43_B1_FECHADO_E_0x119.md
=============================================================================================
CORRIDA 07/09 18:16 (P0) -- 32 submissoes INDIRECT, ZERO timeout, ZERO BugCheck, Codigo 0 ate
o rollback, e o B1 REPRODUZIDO (2x TRIPE_PROVADO com 5 canarios, Divergentes=0). Contra a
17:43, que morreu na 15a submissao com 0x119. Zero REJECT source contract e zero
SpaceResolveAddress failed, com 4 [DestroyProcess] GPUVA -- a classe OWNER_MISMATCH da A14.3
desapareceu num cenario com a mesma rotatividade de dwm.exe que a produzia (10 falhas,
2 dumps). RESSALVAS: o RPTR casou com o alvo CRU nas submissoes pos-wrap, entao a assinatura
de falha da 17:43 (RPTR alem de uma volta) NAO reapareceu -- a A15 nao foi contrariada nem
exercitada no modo que importa; e a imagem que voltou foi o ROLLBACK do watchdog as 18:18,
nao o nosso caminho de pixel (30 Present, 10 SDMA=YES, 4 BYTE_PROOF INCONCLUSIVO,
BlankStreak=4). O log vivo integral foi arquivado com SHA-256 (2708 linhas contra 1965 do
coletor) e o watchdog passou a fazer isso sozinho antes do rollback.
=============================================================================================
B3 e B4 em 07/09, OFFLINE e sem corrida.
B3: os PLANO A SAIRAM DA PRECEDENCIA. O default do Node 0 passou a ser somente INDIRECT --
o unico ramo que le o DMA buffer do dxgkrnl, e o que fechou o tripe completo em 17:43/18:16.
O campo do contrato deixou de ser IndirectOnly e virou PlanoAFallbackEnabled; o tunable
SdmaIndirectOnly foi substituido por SdmaPlanoAFallback (default 0, fail-closed) e a string
antiga esta PROIBIDA por gate. INF, manifesto (derivado do INF), binder e o script
arm-sdma-plano-a-fallback.ps1 acompanham. RISCO DECLARADO: submissao cuja GPUVA de IB nao
resolva agora sai como STATUS_IO_DEVICE_ERROR POR DEFAULT, caminho conhecido do 0x119 sem
TDR -- e' deliberado, falhar visivel e melhor que suceder por reimplementacao.
B4: duas coisas medidas. (a) A recusa de submissao no Node 1 era MUDA -- STATUS_NOT_SUPPORTED
sem uma linha de log, e por isso "2 contextos Node1 e zero submits" nao distinguia "o
scheduler nao submeteu" de "nos recusamos". Agora a recusa nomeia o motivo com o BlockMask
decodificado bit a bit, e a abertura do gate tambem aparece. (b) O gate NAO PODIA ABRIR no
pacote de producao: GpuvaMappingsOperational so' vai a 1 quando GpuvaLabPromotion esta
presente e em 1, e essa leitura existia SO' no #else de UNSAFE_STARTDEVICE -- mesma classe
de defeito do SdmaIndirectOnly da A13. Agora e' lida no caminho de producao; o default
continua 0 e a elegibilidade ainda precisa fechar. O caminho depois do gate JA existe
completo (view de contexto, snapshot fixado, cobertura sem gaps, reserva de fence WDDM e
Bc250SubmissionEngineSubmit no anel MEC) -- faltava chegar nele e saber por que nao chegava.
Testes: submit_command_contract_test com a polaridade invertida, build-contract EXIT=0,
cinco auditorias PASS e os portoes novos verificados POR MUTACAO.
PACOTE 3.85.0.0 assinado e NAO INSTALADO: fonte 359B1C88..., assinado 39B76788...,
CAT 8CE3C272..., SdmaPlanoAFallback=0, GpuvaLabPromotion=0.
NADA DE B3/B4 FOI VALIDADO EM HARDWARE.
Handoff: docs/183_HANDOFF_2026-09-07_B3_B4_SUBMISSAO.md
=============================================================================================
B6 / item 25 em 07/09, OFFLINE -- o par de TDR deixou de mentir. Bc250McdmResetFromTimeout e
Bc250McdmRestartFromTimeout escreviam UMA LINHA DE LOG e devolviam STATUS_SUCCESS sem tocar
em registrador nenhum: violacao direta da Lei 4 no par de DDIs em que o dxgkrnl confia para
decidir se o adaptador foi quiescido e se voltou. Agravante: mcdm_callbacks_test EXIGIA esse
STATUS_SUCCESS sem executor instalado -- o teste congelava a mentira, mesma classe do gate
que obrigava a comparacao crua de RPTR a existir (A15).
Agora: ResetFromTimeout faz quiesce FISICO do SDMA0 (snapshot de RPTR/WPTR/STATUS/STATUS1/
ERR_LOG, fecha HardwareEnabled ANTES de mexer no silicio, desliga RB_ENABLE e trava o F32,
SEM desmapear o BAR) e RestartFromTimeout rearma o anel dos dois lados e PROVA com NOP
fisico e fence sobre poison -- se a fence nao retirar, HardwareEnabled volta a FALSE e a DDI
devolve STATUS_DEVICE_NOT_READY em vez de declarar adaptador pronto. Os executores entram
por Bc250McdmInstallTdrExecutors (mesmo padrao dos outros), e ponteiro ausente = falha
fechada por construcao. ResetEngine continua recusando, com o motivo honesto que ja tinha.
Testes: bloco 9 do mcdm_callbacks_test reescrito (sem executor -> DEVICE_NOT_READY; executor
FALSE -> DEVICE_NOT_READY; TRUE -> SUCCESS; desinstalar volta ao fail-closed), gate novo
audit_tdr_pair_has_backend que tambem detecta se o TESTE voltar a exigir a mentira,
build-contract EXIT=0, seis auditorias PASS e mutacao verificada.
O ITEM 25 CONTINUA ABERTO: falta a corrida com hang induzido medindo deteccao -> reset ->
readmissao de submissao -> display vivo. DIVIDA DECLARADA: nao existe hang induzido em
tools/, e a deteccao de duas passagens do bc250_tdr_recovery_contract NAO esta ligada a
watchdog interno -- quem detecta hoje e o dxgkrnl pelo timeout de 2 s (item 24).
PACOTE 3.86.0.0 assinado e NAO INSTALADO: fonte 11F5EEAE..., assinado 85042920...,
CAT FA713F19..., SdmaPlanoAFallback=0, GpuvaLabPromotion=0.
NADA DO B6 FOI VALIDADO EM HARDWARE.
Handoff: docs/184_HANDOFF_2026-09-07_B6_TDR_COM_BACKEND.md
=============================================================================================
ITENS 24 e 25 em 07/09, OFFLINE -- as duas dividas do TDR fechadas em codigo.
item 24: TERCEIRO falso sucesso do dia, e o pior colocado. Bc250McdmQueryEngineStatus punha
EngineStatus.Responsive=1 SEM OLHAR REGISTRADOR, e o dxgkrnl chama essa DDI DURANTE o TDR
para saber se a engine esta viva -- o driver sabotava a propria recuperacao. E, de novo, o
mcdm_callbacks_test EXIGIA Responsive==1 sem probe instalada. Agora a resposta sai de duas
passagens de RPTR/WPTR pelo contrato Bc250TdrDetectHang: sem probe -> NOT_SUPPORTED; probe
que nao mede -> DEVICE_NOT_READY; medicao viva -> Responsive=1; medicao travada ->
Responsive=0 com STATUS_SUCCESS. "Nao sei" nao vira "esta viva". ARMADILHA EVITADA por
medicao anterior: os quatro ponteiros sao REDUZIDOS a uma volta do anel antes da comparacao
-- com os valores crus de 17:43 (RPTR=4612, alvo 516, anel 4096) o contrato leria trabalho
pendente num anel drenado e diria TRAVADA para engine ociosa.
item 25: o hang induzido que faltava. POLL_REGMEM esperando 0xDEADDEAD no slot de fence, com
retry e intervalo no teto: a engine para DE VERDADE (o gate PROIBE a variante simulada por
KeStallExecutionProcessor). O submit ACEITA e NAO notifica completion, de proposito -- e a
fence pendente que faz o timeout de 2 s do dxgkrnl disparar o TDR. Fail-closed em tres
camadas: tunable TdrInduceHang default 0, exige present==1 && valor==1, e disparo UNICO por
bind por InterlockedCompareExchange. Ferramenta: tools/arm-tdr-induce-hang.ps1; o manifesto
declara TdrInduceHang/TdrInduceHangArmed DERIVADOS do INF e o binder avisa alto.
Testes: blocos 6 e 9 do mcdm_callbacks_test reescritos com stubs (inclusive um que escreve 1
e exige que a DDI zere), dois gates novos, build-contract EXIT=0, oito auditorias PASS e
mutacao verificada nos dois.
O ITEM 25 CONTINUA ABERTO: so' fecha com a corrida do hang induzido -- e, por exigir DISPLAY
SOBREVIVENDO, o gate de saida da P6 so' fecha de verdade junto com o B7. Risco declarado da
corrida: BugCheck e reboot sao desfechos POSSIVEIS e sao resultado do ensaio.
PACOTE 3.87.0.0 assinado e NAO INSTALADO: fonte A4F6892A..., assinado AC4E9276...,
CAT 3FBCEDA2..., SdmaPlanoAFallback=0, GpuvaLabPromotion=0, TdrInduceHang=0.
NADA DOS ITENS 24/25 FOI VALIDADO EM HARDWARE.
Handoff: docs/185_HANDOFF_2026-09-07_ITEM24_25_TDR_MENSURAVEL.md
=============================================================================================
CORRECAO DA ORDEM DO ENSAIO TDR, 07/09, OFFLINE: a tentativa de armar pelo registro antes
da instalacao foi corretamente recusada. A chave de servico so' nasce pelo AddReg do INF e o
KMD le TdrInduceHang no StartDevice; escrever depois do bind so' vale no proximo start.
O INF fonte continua fail-closed em 0. O empacotador agora materializa 1 apenas no INF do
candidato, exige versao nova e aceite explicito; o binder exige o mesmo aceite.
PACOTE TDR 3.88.0.0 assinado, auditado e NAO INSTALADO:
BuildKind=MCDM_TDR_INDUCED_HANG, fonte 5B6F07ED..., assinado C4498C9B...,
CAT 5F0E41F6..., INF 45CDC569..., TdrInduceHang=1 derivado do INF empacotado.
Package audit PASS, safety audit com gate da ordem PASS, tentativa sem aceite RECUSADA.
O ITEM 25 CONTINUA ABERTO E NAO VALIDADO EM HARDWARE.
Handoff: docs/186_HANDOFF_2026-09-07_TDR_ORDEM_E_PACOTE_ARMADO.md
=============================================================================================
CORRIDA TDR 07/09 20:41 (P0): o hang induzido disparou o dxgkrnl; ResetFromTimeout fez
quiesce com RPTR=68, WPTR=92 e ERR_LOG=0x0F, mas o restart antigo nao abortou o POLL_REGMEM.
O SYNC_NOP ficou em WPTR=40/RPTR=68, fence no poison, e RestartFromTimeout devolveu
STATUS_DEVICE_NOT_READY. Seguiu BugCheck 0x116 com esse status em Arg3. QueryEngineStatus
nao apareceu: item 24 nao foi exercitado nessa rota. OFFLINE depois da corrida: restart
agora pulsa GRBM_SOFT_RESET.SDMA0, reconstrói integralmente o anel, exige readback e so'
depois prova com NOP/fence. Build e safety audit PASS; dois gates provados por mutacao.
PACOTE TDR 3.89.0.0 assinado, auditado e NAO INSTALADO: fonte CC8B1615..., assinado
9573685E..., CAT 0A765FFB..., INF 53F5900D..., TdrInduceHang=1. O ITEM 25 CONTINUA ABERTO;
A CORRECAO DO RESET INTEGRAL NAO FOI VALIDADA EM HARDWARE.
Handoff: docs/187_HANDOFF_2026-09-07_TDR_0x116_E_RESET_INTEGRAL.md
=============================================================================================
CORRIDA TDR 07/09 21:49 (P0): o Windows gerou live dump 0x117 atribuido a bc250mcdm.sys.
A cadeia fechou HANG_INDUZIDO -> TDR_QUIESCE -> SYNC_NOP TRIPOD PASS em 2 polls ->
TDR_RESTART PROVADO. Depois do restart, 40 INDIRECT executaram, quatro com tripe completo,
houve 36 Present, zero recusa/timeout e Codigo 0 ate o rollback. O reboot foi planejado pelo
watchdog, nao BugCheck. A tela continuou preta: DWM fail-fast sem backend D3D11, superficies
zeradas. Deteccao, reset, restart e readmissao estao VALIDADOS EM HARDWARE. O item 25 segue
ABERTO somente na perna display visivel sobrevive, dependente do B7. Nao repetir TDR antes
do B7. Handoff: docs/188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md
=============================================================================================
CORRIDA 08/09 11:48 (P0) -- BIND DO CANDIDATO LAB 3.90.1.0 EM CODIGO 43, e a corrida do B7
foi GASTA sem medir o Node 1. Cadeia medida: fusivel armado, EDID sintetizado, GOP adquirido
2560x1080, PSP+F32 OK, TRIPE_PROVADO de boot (RPTR_Reg=68, Casou=REGISTRADOR, Polls=1) e MMU
global Regs=26 -- e entao StartDevice devolveu 0xC00000BB, sem UMA linha de bring-up fisico.
O gate compute-display marcou UMD_NODE1_SKIPPED corretamente (sem primaria, sem CommitVidPn):
o runner do tripe Node 1 nem chegou a ser elegivel. Node 1 segue sem qualquer medicao.
CAUSA RAIZ FECHADA por codigo + telemetria, contra o bind 09:50 do MESMO perfil que deu
Codigo 0: um bloco novo no StartDevice mapeia Core.MmioTransport para programar a MMU global e
seguia com ele MAPEADO ate o SEGUNDO preflight readonly. Bc250DxgkDdiStartDeviceReadonly
recusa transporte nao-vazio com STATUS_INVALID_PARAMETER na guarda de ENTRADA -- antes de
escrever LastStage. Probes: 09:50 SecondPreflightStage=8 e Gate=1; 11:48 estagio=3 e Gate=0.
O 3 e' RESIDUO do PRIMEIRO preflight -- qualquer caminho que passe da guarda escreve 1
(INTERFACE_ACCEPTED), logo o 3 PROVA que a segunda chamada saiu antes disso. Nao e' hipotese.
DOIS DEFEITOS, e o segundo escondeu o primeiro: com o gate fechado, TODO o bring-up fisico foi
pulado, persistentStatus ficou no INICIALIZADOR (STATUS_NOT_SUPPORTED), e o rodapé do
StartDevice o publicou como veredito porque PersistentSubmission=1 e persistentReady=FALSE.
O 0xC00000BB NAO foi produzido por medicao nenhuma. Terceira corrida seguida perdida em
defeito de host, nao de silicio (as outras: comparacao crua de RPTR na 17:43; ordem do
registro no ensaio de TDR).
FAIXA A16, OFFLINE e sem corrida -- as tres correcoes: (1) o transporte volta a VAZIO antes do
segundo preflight, so' no caminho de laboratorio e com falha fechada se o unmap nao fechar
(producao mantem o mapa por design e nao tem segundo preflight); desmapear e' seguro porque a
MMU ja esta programada e cada transacao do bring-up mapeia/desmapeia sozinha --
Bc250MmioTopologyMapReadonly devolve WRONG_STATE se achar o transporte mapeado, entao deixa-lo
mapeado quebraria o bring-up INTEIRO, nao so' o preflight; (2) probe novo
SECOND_PREFLIGHT_STATUS com o NTSTATUS CRU mais log com o estado do transporte -- o estagio
sozinho e' ambiguo POR CONSTRUCAO; (3) persistentAttempted: o rodapé so' pode trocar o status
pelo do ensaio de submissao persistente se esse ensaio TIVER ACONTECIDO, e status real ja
carregado manda sobre ensaio que nunca rodou.
Testes: dxgk_readonly_ddi_test reproduz a 11:48 offline (runtime STAGED + transporte MAPEADO
-> INVALID_PARAMETER, LastStage intocado, RuntimeStaged=1) e prova o contraponto (transporte
vazio -> estagio 8, gate 1). Gate novo audit_second_preflight_transport_empty. build-contract
EXIT=0, tres auditorias PASS. QUATRO mutacoes verificadas -- e a primeira reprovou o MEU
proprio gate: a busca casava com o PRIMEIRO preflight, a janela saia invertida e o gate
passava sem examinar nada. Falso negativo silencioso, corrigido e re-testado.
PACOTE 3.92.1.0 assinado, auditado e NAO INSTALADO: SYS fonte 76E5ADCA..., assinado
A1973547..., CAT 99E8E5BE..., INF 8E960DA6..., UMD 2927FCBC..., SkipHwBringup=0,
GpuvaLabPromotion=0, Node1TripodDefaultArmed=0, D3D10_11NativeReady=0. Package audit PASS.
O rebuild foi PROVADO pelas cinco marcas da A16 no SYS assinado (quatro strings de log mais o
nome de probe SecondPreflightStatus em UTF-16), e NAO pelo schema de telemetria: eu havia
proposto o schema como verificador e estava ERRADO -- $labSchemaVersion e' constante fixa do
empacotador, gravada no INF, e nao deriva do enum; nunca ia se mover sozinha. Corrigido no
mesmo dia: o schema subiu para 135 porque o campo novo mudou o conjunto, e a versao fonte foi
para 3.92.0.0 para nao existirem dois artefatos com o mesmo DriverVer.
A A16 FOI VALIDADA EM HARDWARE NA CORRIDA 12:36 -- ver o bloco seguinte. No momento em que
este bloco foi escrito ela ainda nao tinha sido; a linha fica como registro da ordem dos fatos.
O pacote 3.90.1.0 instalado as 11:48 JA FOI REVERTIDO pelo watchdog (BasicDisplay Codigo 0) e
C:\BC250_REQUIRE_CLEAN_BOOT.json esta ARMADO: boot frio antes de qualquer novo bind.
Handoff: docs/199_HANDOFF_2026-09-08_A16_ORDEM_DO_SEGUNDO_PREFLIGHT.md
=============================================================================================
CORRIDA 08/09 12:36 (P0, candidato 3.92.1.0) -- A16 VALIDADA EM HARDWARE, e Codigo 43 por
defeito DISTINTO e ANTERIOR. As duas linhas novas responderam a pergunta da corrida:
"Transporte devolvido ao preflight: unmap=ok (State=0)" e "Segundo preflight: status=0xC00000BB,
stage=8, gate=1, MmioState=0". Contra a 11:48: estagio 3->8, gate 0->1 e V5StartDeviceStatus
0xC00000BB->0. O StartDevice DEVOLVEU SUCESSO e o bring-up seguiu ate ChildRelations
(DP ChildUid=1). O 0xC00000BB que sobrou no SecondPreflightStatus e' o NOT_SUPPORTED LEGITIMO
do fim do gate readonly -- e o probe novo existe exatamente para separar os dois.
PAREDE NOVA, fechada por aritmetica: RuntimeCapsStatus=0xC0000182. O ramo SEALED de
Bc250ActiveQueryRuntimeCaps exige SecondPreflightStage==PREFLIGHT_READY e, ate a A16 destravar
o segundo preflight, NUNCA tinha executado num bind real -- todas as corridas caiam no
fallback. O DEFEITO E' ANTERIOR A A16; a correcao apenas o tornou alcancavel.
A conta, com os numeros medidos: janela de CPU 268.435.456; localEnd (plano de firmware)
268.161.024; reservas = scanout offset 0 len 11.059.200 + dummy page da MMU offset 268.431.360
len 4.096 + carveout opaco offset 268.435.456 (pulado) -- soma 279.498.752, que e' exatamente
RuntimeInventoryReservedLow32. A dummy e' alocada TOP_DOWN (DummyPA=0x45FFFF000 menos base
0x450000000 = offset 268.431.360) e termina no TOPO da janela; o laco tratava TODA reserva
como PREFIXO a pular, entao localStart=268.435.456 >= localEnd=268.161.024 e o segmento
colapsava. DRIVERCAPS entao devolveu DEVICE_NOT_READY -- o c00000a3 do fusivel. Agravante: a
dummy JA era tratada corretamente ~60 linhas abaixo truncando localEnd; era contada DUAS vezes
e prevalecia a contabilidade errada.
CORRECAO (offline): prefixo empurra o inicio, reserva alta trunca o fim, com o passe de prefixo
repetindo ate estabilizar porque a lista nao e' ordenada. Segmento resultante
[11.059.200, 268.161.024) = 257.101.824 bytes, com a dummy fora dele.
Teste Bc250RunSealedSegmentReservationTest com os numeros da corrida sem arredondar, em tres
casos (layout medido passa; reserva que cobre tudo RECUSA, para a assercao nao virar carimbo;
reserva alta abaixo do firmware trunca em vez de empurrar). Mutacao 5 verificada: removendo a
guarda de prefixo a suite reprova. build-contract EXIT=0, tres auditorias PASS.
RESSALVA DE ESCOPO DO TESTE: active_miniport_test linka a variante OFFLINE do callbacks, onde
QUERYSEGMENT4 esta compilado fora; por isso a assercao e' por DRIVERCAPS. A prova byte a byte
do tamanho do segmento sai da PROXIMA corrida, na linha [McdmQAI] QUERYSEGMENT4 (esperado
VRAM=0xF52A000 = 257.101.824).
A CORRECAO DO SEGMENTO SEALED NAO ESTA VALIDADA EM HARDWARE: exige rebuild do SYS, pacote novo
e corrida. O 3.92.1.0 instalado as 12:36 JA FOI REVERTIDO pelo watchdog (BasicDisplay Codigo 0)
e C:\BC250_REQUIRE_CLEAN_BOOT.json esta ARMADO.
Handoff: docs/200_HANDOFF_2026-09-08_12-36_A16_VALIDADA_E_SEGMENTO_SEALED.md
=============================================================================================
CORRIDA 08/09 13:00 (P0, candidato 3.92.1.0 SYS 7B34393F) -- CODIGO 0 e o SEGMENTO SEALED
VALIDADO NO SILICIO, com os dois numeros previstos ANTES da corrida: RuntimeCapsBaseLow32=
11.059.200 (0xA8C000), RuntimeCapsLocalLow32=257.101.824 (0xF531000), RuntimeCapsStatus=0,
FullCapsReady=1, Segment4Ready=1, QUERYSEGMENT4 com VRAM=0xF531000 e SetRootPageTable com
readback OK PT_BASE=450a8c000 sob VMID8. Duas faixas fechadas no silicio hoje: A16 (12:36) e
segmento sealed (13:00). Correcao de registro: eu escrevi o alvo como 0xF52A000; o HEX estava
errado, o decimal previsto 257.101.824 estava certo e foi o que apareceu.
DOIS ENQUADRAMENTOS MEUS CORRIGIDOS PELA MINERACAO:
(1) o VidPn NAO e' a parede -- ele CONVERGE. 39 convergencias completas ("TargetMode matches
GOP -> Supported=TRUE!" com Scale=1/Rot=1 e os dois modos fixados, o mesmo estado que a
producao comita) contra 46 reinicios do zero. Nao e' "nao converge"; e' um LACO.
(2) a hipotese de que o E_NOTIMPL da nossa UMD matava o processo foi REFUTADA na mesma
corrida: NENHUM BC250_UMD_LOG_*.txt foi criado, isto e', bc250umd.dll nunca carregou -- e a
DLL esta instalada em System32. Na corrida de PRODUCAO de hoje 08:44 a mesma DLL carregou em
dwm.exe e recusou OpenAdapter10/10_2 com E_NOTIMPL, com cinco logs de prova. SAO DUAS PAREDES:
producao comita -> DWM roda -> UMD recusa (parede do D3D11); laboratorio converge -> um
processo nasce e MORRE -> nunca comita -> DWM nunca roda -> UMD nunca carrega (parede
ANTERIOR, SEM CAUSA IDENTIFICADA). Confianca da ausencia de logs: P2.
Entre a convergencia e o descarte o log mostra CreateProcess -> CreateDevice -> CreateContext
(Node 0) -> DestroyProcess: 8/9/8/7, TODOS Node 0, zero Node 1, zero submissao. O dxgkrnl
registrou LiveKernelEvent P1=193 (0x193 VIDEO_DXGKRNL_LIVEDUMP) P2=804 P3=c0000001
(STATUS_UNSUCCESSFUL) P4=108 as 13:02:30. O SUBCODIGO 804 NAO FOI DECODIFICADO -- nao ha fonte
neste repositorio e nao se infere.
INSTRUMENTACAO (offline): o log so' tinha handles opacos e nao dizia QUEM morre.
DXGKARG_CREATEPROCESS carrega pProcessName e Flags desde WDDM 2.4 (d3dkmddi.h) e declaramos
2.7, entao o nome vem do CONTRATO, sem API nao documentada. Bc250McdmCreateProcess passou a
registrar Nome= e Flags(System/Gdi/Vm/VmWorker/Isolated), com guarda de pProcessName NULL,
ProcessNameLength em WCHARs (nao bytes) e truncamento em 63 chars. ARMADILHA REGISTRADA: a
copia caractere a caractere foi reconhecida pelo /O2 e reemitida como memcpy, ausente do link
offline das suites (LNK2019); RtlCopyMemory tem o mesmo destino. Resolvido com ponteiro de
destino volatile. build-contract EXIT=0, tres auditorias PASS.
A INSTRUMENTACAO NUNCA RODOU: NAO VALIDADA EM HARDWARE. O INF fonte ja esta em 3.93.0.0.
NOTA DE REGISTRO: 3.92.1.0 designa DOIS artefatos -- SYS A1973547 (A16, corrida 12:36) e SYS
7B34393F (A16 + segmento, corrida 13:00) --, porque eu nao subi o INF fonte antes do
empacotamento. Desambiguavel pelo hash, que o binder imprime e o manifesto registra.
O 3.92.1.0 foi revertido pelo watchdog (BasicDisplay Codigo 0) e
C:\BC250_REQUIRE_CLEAN_BOOT.json esta ARMADO.
Handoff: docs/201_HANDOFF_2026-09-08_13-00_SEGMENTO_VALIDADO_E_O_LACO_DO_VIDPN.md
=============================================================================================
CORRIDA 08/09 14:06 (P0, candidato 3.93.1.0 SYS EF91F17F) -- a instrumentacao pagou na PRIMEIRA
tentativa e respondeu as DUAS perguntas. Estado bom mantido: V5StartDeviceStatus=0, estagio 8,
gate 1, RuntimeCapsStatus=0, base 11.059.200 e segmento 257.101.824. Laco do VidPn identico:
39 convergencias, 46 reinicios, zero CommitVidPn, zero Present.
(1) QUEM MORRE: pProcessName veio NULL (permitido pelo contrato), mas as FLAGS responderam --
o processo que repete e' Gdi=1/System=0, isto e', o cliente GDI, NAO o dwm.exe. Casa com a
medicao da 13:00 (nenhum BC250_UMD_LOG criado): o DWM nunca entra em cena neste perfil e a
bc250umd.dll nunca carrega. A parede do D3D10/11 existe, mas e' POSTERIOR a esta.
(2) ALGUMA DDI NOSSA RECUSOU? NAO -- zero RECUSADO na corrida inteira. Hipotese eliminada.
O ACHADO: o OK estava MENTINDO. Oito vezes
"[McdmCreateContext] OK: Node=0, ..., DmaBuf=0x0, SegSet=0x0". Contexto com DmaBufferSize=0 e'
inutil -- o dxgkrnl nao tem como alocar DMA buffer. Causa: Bc250ActiveCreateContextObject faz
"*ContextInfo = context->ContextAbi", SOBRESCREVENDO os defaults que Bc250McdmCreateContext
acabara de escrever, e ContextAbi so' era populado dentro do bloco
"#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)" -- so' na imagem de PRODUCAO. Na de
laboratorio ContextAbiReady ia a 1 com a estrutura inteiramente zerada. TERCEIRA VEZ da mesma
classe: A13 (tunable de submissao), B4 (GpuvaLabPromotion) e agora ContextAbi -- campo escrito
em UM SO' ramo do #if, invisivel no outro.
CORRECAO (offline): fonte unica Bc250ActiveInitializeContextAbi chamada nos TRES sitios antes
de declarar ContextAbiReady. Dois gates novos: audit_context_abi_before_ready (entre cada
ContextAbiReady=1 e o anterior tem de haver chamada a fonte unica -- SEM janela de linhas --,
e DmaBufferSize nao pode ser escrito fora do helper) e audit_process_lifecycle_ddis_speak
(todo return nao-SUCCESS depois do log de entrada exige log nas 6 linhas anteriores, nas tres
DDIs de ciclo de vida). QUATRO mutacoes verificadas (6..9), e DUAS versoes iniciais dos MEUS
gates foram reprovadas por elas: a primeira exigia so' a presenca da string RECUSADO (passava
removendo uma de tres) e a segunda usava janela de 12 linhas (reprovava o sitio de producao
legitimo, com 19 linhas de comentario). Ambas refeitas com invariante sem janela.
O gate do B3 pegou o MEU PROPRIO comentario por citar o nome banido do tunable antigo.
build-contract EXIT=0, tres auditorias PASS.
CONFIANCA: DmaBuf=0x0 publicado e' P0 (medido, oito vezes); que ISSO mate o cliente GDI e' H.
Criterio da proxima corrida: "DmaBuf=0x10000, SegSet=0x2" e o ciclo CreateProcess->
DestroyProcess PARAR, com CommitVidPn aparecendo -> hipotese confirmada; ciclo continuar com
DmaBuf correto -> REFUTADA, e o campo deixa de ser suspeito. Os dois desfechos informam.
A CORRECAO NAO ESTA VALIDADA EM HARDWARE. INF fonte em 3.94.0.0; candidato sai 3.94.1.0.
3.93.1.0 revertido pelo watchdog; BasicDisplay Codigo 0 e marcador de boot frio ARMADO.
Handoff: docs/202_HANDOFF_2026-09-08_14-06_QUEM_MORRE_E_O_CONTEXTO_ZERADO.md
=============================================================================================
CORRIDA 08/09 14:26 (P0, candidato 3.94.1.0 SYS 8CA949DC) -- O COMMITVIDPN ACONTECEU. O
criterio escrito ANTES da corrida bateu inteiro: DmaBuf 0x0->0x10000 e SegSet 0x0->0x2,
CreateProcess 8->2, DestroyProcess 7->0, convergencias/reinicios 39/46 -> 8/8, e CommitVidPn
0 -> 1. O laco que consumiu quatro corridas ACABOU, e a causa era a ABI de contexto publicada
zerada (doc 202). Pela primeira vez no perfil lab a cadeia inteira acordou: SetVidPnSourceAddress
(SourceId=0, Segment=1, Addr=0xAAC000), SetVidPnSourceVisibility(Visible=1), 60
GetStandardAllocationDriverData, 20 CreateAllocation, 30 OpenAllocation e 8 CloseAllocation --
TODAS com status 0 -- mais 367 ControlInterrupt de VSYNC. Zero StopDevice, zero BugCheck,
zero RECUSADO; adaptador vivo e estavel ate o rollback programado. O estado bom das faixas
anteriores se manteve: estagio 8, gate 1, base 0xA8C000, segmento 0xF531000.
A PAREDE NOVA E' A PAREDE ANTIGA DA PRODUCAO: dwm.exe agora RODA neste perfil e fail-fasta
com 0xe0464645 em KERNELBASE.dll+0x12d862, oito vezes entre 14:26:12 e 14:26:54 -- a MESMA
assinatura da corrida de producao. Present=0 porque o DWM nao sobrevive para apresentar.
MUDANCA DE ESTADO DO PROJETO: ate hoje havia DUAS paredes -- a de producao (backend D3D10/11,
conhecida) e a de laboratorio (um processo que nascia e morria, SEM causa identificada). Os
dois perfis CONVERGIRAM. Resta UMA parede para o desktop, com nome e arquivo:
Bc250Umd10CreateDevice devolve E_NOTIMPL porque BC250_D3D10_11_NATIVE_READY=0, fail-closed
deliberado pela Lei 4.
RESSALVA QUE MANDA NO VEREDITO: NENHUM BC250_UMD_LOG foi criado nesta corrida, apesar das oito
mortes do dwm.exe. Que o DWM fail-fasta e' P0; que ele fail-fasta POR CAUSA da recusa da nossa
UMD e' P2 -- se o OpenAdapter chegou a ser consultado antes do fail-fast e' INDETERMINADO
nesta corrida. O modulo da falha e' KERNELBASE (RtlFailFast), compativel com o caminho
CreateD3D11Device -> DXGI_ERROR_UNSUPPORTED ja documentado por dump, mas compatibilidade nao
e' prova. O LiveKernelEvent P1=193 P2=804 P3=c0000001 P4=108 reapareceu as 14:28:32 e o
SUBCODIGO 804 CONTINUA NAO DECODIFICADO.
PROXIMA ETAPA, E FICOU BARATA: o gate do runner do Node 1 exige primaria, CommitVidPn e
SetVidPnSourceAddress -- os TRES agora acontecem. Em todas as corridas anteriores o runner era
corretamente marcado UMD_NODE1_SKIPPED porque o pre-requisito nao existia. O pacote 3.94.1.0
JA ESTA EM DISCO e e' o mesmo que produziu o CommitVidPn: basta boot frio e o mesmo bind com
-RunUmdNode1Tripod. O Node 1 segue SEM QUALQUER MEDICAO desde o inicio do projeto.
build-contract EXIT=0, tres auditorias PASS. Pacote revertido pelo watchdog; BasicDisplay
Codigo 0 e marcador de boot frio ARMADO.
Handoff: docs/204_HANDOFF_2026-09-08_14-26_COMMITVIDPN_E_A_PAREDE_UNICA.md
Dossie para auditoria externa da sessao inteira: docs/203_AUDITORIA_EXTERNA_2026-09-08.md
=============================================================================================
CORRIDA 08/09 15:18 (P0) e faixa B7.1 offline -- B2 FECHADO NO SILICIO E BLOQUEADOR DO NODE 1
ISOLADO. Com -RunUmdNode1Tripod e arquivamento B2 ativo, o pacote lab 3.94.1.0 alcancou Codigo 0
e os logs da UMD foram capturados (bc250_umd_log.txt, 43 KB).
dwm.exe (pid=3704): OpenAdapter10_2 e OpenAdapter10 recusados com E_NOTIMPL por
BC250_D3D10_11_NATIVE_READY=0 -> A CAUSA DO FAIL-FAST DO DWM E' AGORA PROVADA NO SILICIO (P0),
nao mais P2.
Harness Node 1 (pid=788 bc250-umd-node1-tripod.exe): executou sob watchdog, autenticou o token,
criou a paging queue e contexto de compute no Node 1, e alocou 64 KB de command buffer.
Em MapAllocation: o runtime WDDM retornou hr=0x8000000A (E_PENDING) com VA=0x11000 e
PagingFence=7001. A winsys da UMD tratou FAILED(0x8000000A) como erro fatal (CALLBACK_FAILED).
Faixa B7.1 (offline): Bc250WinsysMapAllocation e Bc250WinsysMakeResident corrigidos para
aceitar E_PENDING (0x8000000AL), Bc250WinsysWaitPaging retorna OK quando PagingFenceValue=0.
Suite TestPagingPendingStatus adicionada em bc250_wddm_winsys_test.c; build-contract EXIT=0,
build-umd EXIT=0, 17 portoes de seguranca PASS.
PACOTE LAB 3.94.2.0 assinado e auditado (SYS 8CA949DC, UMD 35FBD781, CAT 5FB2E86E).
NADA DA FAIXA B7.1 FOI VALIDADO EM HARDWARE.
Handoff: docs/205_HANDOFF_2026-09-08_15-18_B2_PROVADO_E_MAPALLOCATION_PENDING.md
```

> ⚠️ **Aviso histórico.** Até 2026-09-02 este arquivo continha uma barra declarando
> *"Miniport WDDM Display+Render 100% — Canal Display/VidPN/VSync/Present Ativo em Código 0"*
> e o veredito *"COMPROVADOS NO SILÍCIO"*. **Era falso** e contradizia a evidência medida.
> Agentes que leram aquela versão produziram análises otimistas sem base. Não reintroduza.

---

## ⚖️ 3. Leis inegociáveis

1. **Sinceridade absoluta.** Reporte a medição, não a expectativa. Prova ausente →
   escreva literalmente **`NÃO VALIDADO EM HARDWARE`**. Hipótese nunca é apresentada como causa.
2. **Tripé no silício.** `RPTR == WPTR` + fence física gravada **pela GPU** em VRAM com
   poison check + dados conferidos **byte a byte**. Sem os três, não houve execução.
3. **Todo item fecha com um teste nomeado.** Diga qual teste comprova execução no hardware
   e onde a evidência será arquivada (`docs/evidence/`). A Tabela 2 da `BASE_DE_AVANCO.md`
   já traz a prova exigida de cada item. Trabalho sem teste nomeado não é entregável.
4. **Zero fakes.** Proibido: fence ou timestamp incrementado pela CPU; loop de delay
   simulando execução; `STATUS_SUCCESS` em DDI sem backend; stub/mock/no-op anunciado como
   pronto; telemetria fixa ou estimada; WARP/GDI/llvmpipe; fallback silencioso.
5. **Não é prova de aceleração:** compilar, assinar, carregar DLL, enumerar API, criar
   contexto ou pipeline, abrir janela ou swapchain, `vulkaninfo`, `dxdiag`, `glGetString`,
   ou qualquer string que o próprio driver anuncia.
6. **Ciclo transacional fail-closed:**
   `PREFLIGHT → SNAPSHOT → APPLY → READBACK → PROVA NO SILÍCIO → ROLLBACK/TEARDOWN`
   Abortar se o preflight for inválido; falhar fechado em qualquer etapa.
7. **Máscara SOC15:** `uint32_t val = (lido & ~mask) | (or_val & mask);`
8. **Endereços com proveniência.** Só offsets catalogados em
   [`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md), com
   fonte citada; nada em quarentena. No DCN: `DMU_BASE.seg[BASE_IDX] + reg`, com
   `DMU_BASE = {0x12, 0xC0, 0x34C0, 0x9000}` — **não existe base plana**.
9. **Vocabulário de confiança:** P0 provado no silício · P1 derivado do upstream ·
   P2 parcialmente observado · H hipótese · Q quarentena (proibido). Ver [`rules.md`](rules.md).
10. **Economia de corridas.** A corrida física é o recurso escasso e é o **Daniel** quem a
    executa. Responda primeiro por leitura de fonte e mineração de `docs/evidence/`. Defina
    teto de corridas e critério de abandono antes de armar. Boot frio entre binds.

---

## 🚫 4. Anti-alucinação

Como agente, você **não deve**:
- Afirmar que algo está "pronto", "acelerado" ou "funcionando" porque compilou ou retornou sucesso;
- Declarar causa-raiz sem trace, dump ou telemetria que a sustente;
- Reabrir caminhos encerrados: render-only sob `Class=Display` (reprovado, `docs/162`),
  `MiscCaps.ComputeOnly` (inalcançável), cross-adapter/MCDM como destino (APU de die único);
- Propor escrita em registrador de DCN fora da prioridade P9;
- Omitir a cláusula **`NÃO VALIDADO EM HARDWARE`** quando faltar evidência física.

---

## 📂 5. Mapa do repositório

| O quê | Onde |
|---|---|
| **Base de avanço** | [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md) |
| **Roadmap** | [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md) |
| Regras normativas | [`rules.md`](rules.md) |
| Catálogo de registradores | [`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md) |
| Provas físicas P1 / P2 | `docs/148_...` · `docs/159_...` |
| Ownership de display (prova) | `docs/26_...` · `docs/17_...` |
| Encerramento do render-only | `docs/162_...` |
| Corpus de evidência | `docs/evidence/` |
| Miniport WDDM | [`driver/wdk/`](driver/wdk/) |
| UMD D3D / Vulkan | [`driver/umd/`](driver/umd/) |
| Driver de laboratório KMDF | [`dbg/`](dbg/) |
| ABI de diagnóstico | [`inc/bc250dbg_abi.h`](inc/bc250dbg_abi.h) |
| Harness e auditorias | [`tools/`](tools/) |
| Recuperação | [`RECOVERY_BC250V4.md`](RECOVERY_BC250V4.md) · `tools/BC250_RECOVERY.cmd` |
| Arquivo morto (nada apagado) | [`DocsObsoletos/INDICE.md`](DocsObsoletos/INDICE.md) |

---

## 🔄 6. Manutenção

`CLAUDE.md`, `AGENTS.md`, `GEMINI.md` e `.cursorrules` compartilham o mesmo núcleo
normativo. **Ao mudar uma lei ou a barra de status, atualize os quatro.** Divergência entre
eles já produziu agentes declarando sucesso inexistente.

---

### Lei do escopo (06/09) — decisão do dono do projeto

1. **O objetivo é WDDM acelerado por GPU e jogos com Vulkan.** Nada mais.
2. **LLM é secundário e é subproduto.** `llama.cpp`, tokens/s e DirectML não são marco, não
   entram em PASS de etapa e não aparecem em veredito.
3. **Compute isolado não é entregável.** Compute vale como **pré-requisito de jogos** — a
   submissão Vulkan passa por ele. Não é destino.
4. **É proibido propor compute ou LLM como "caminho mais rápido" quando o item de vídeo
   estiver bloqueado.** Item travado se resolve, se declara travado, ou se declara
   `NÃO VALIDADO EM HARDWARE`. Trocar a meta por uma mais fácil é fuga de escopo.
5. **Demorar é aceito.** Custo em tempo não é argumento para trocar o objetivo.
6. Critério final: **jogo real na tela ligada à placa, sem WARP, com TDR recuperando de hang
   sem perder o display.**

> ⚠️ **Plataforma real (06/09):** **Windows 10 Pro 22H2, build 19045, UBR 2965** — não
> Windows 11. Confirmado pelo dump do DWM e pelo registro da máquina. WDDM máximo aqui é 2.9,
> compatível com o contrato 2.0/2.7 em uso.

> ⚠️ **Barreira ativa do vídeo (06/09, decidida por dump):** o `dwm.exe` exige device
> **D3D11** em `CLegacyRenderTarget::EnsureSwapChain` → `CD3DDevice::CreateD3D11Device`, e ao
> receber `DXGI_ERROR_UNSUPPORTED` ele **fail-fasta** em vez de cair para software.
> `DriverSupportsCddDwmInterop` **não** é rota de fuga — já está declarado e não mudou nada.
> Logo **backend D3D10/11 real na `bc250umd.dll` é obrigatório** para vídeo acelerado.
