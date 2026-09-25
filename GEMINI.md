# GEMINI.md — Diretrizes de Operação para Google Antigravity e Gemini

> **Contexto normativo para Antigravity / Gemini CLI neste repositório.**
> *Target: AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64*
> **Última revisão: 2026-09-25** · Núcleo normativo idêntico a `CLAUDE.md`, `AGENTS.md` e `.cursorrules`.

O Antigravity carrega `GEMINI.md` e `AGENTS.md` da raiz para o contexto hierárquico. Este
arquivo é o ponto de entrada; as leis são as mesmas dos demais.

---

## 🎯 1. Onde queremos chegar

**Jogos no Windows 10 22H2 rodando na BC-250** — 24 CUs, 16 GB GDDR6, monitor no DisplayPort da
própria placa, D3D12/Vulkan, **sem WARP e sem fallback de software**.

**Fonte única de verdade:** [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md) — Tabela 1 (já feito,
com prova física) e Tabela 2 (o que falta, prioridade `P1→P9`, com o teste que fecha cada
item). **Roadmap:** [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md).

**Arquitetura vigente:** display v1 **por herança do UEFI GOP** — VidPn de modo único,
scanout em VRAM real, `DxgkDdiPresent` no modelo **blt** por SDMA, VSync **lido** do frame
counter do OTG. **DCN não entra no caminho crítico**; escrita em registrador de DCN é
proibida fora da prioridade P9.

**Onde estamos em 25/09/2026:** a [Corrida 118](docs/437_CORRIDA118_DX73_TRIANGULO_PIXELS_COPIA_E_R1.md)
com DX.73/3.277.1.0 completou 180 s com desktop e triângulo visualmente corretos,
harness 60/60, amostras de centro/canto preservadas em 60/60 cópias e rollback para
BasicDisplay Código 0 no mesmo boot. Na 117 o triângulo faltou; a falha visual é
intermitente. DWM 2,68 inícios de Present/s nos primeiros 120 s, harness 0,9 FPS.
Travamentos longos, 600 s, 60 FPS e jogos seguem abertos. O próximo bind exige boot frio.
Leia o [handoff 438](docs/438_HANDOFF_2026-09-25_CORRIDAS_111_A_118_VISUAL_VELOCIDADE_E_TRAVAMENTO.md).

---

## Diretriz vigente — qualidade e relato de corridas (16/09/2026)

Esta decisão do proprietário prevalece sobre orientações históricas conflitantes abaixo. Cumprir [DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md](DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md).

- **Qualidade antes de velocidade:** WDDM display+render, desktop acelerado e aplicações/jogos não modificados. Não reduzir a meta a demos, render-only ou fallback de software.
- **Refazer Marcos 3 e 4:** tela colorida e cubo com vkCreateGraphicsPipelines, vertex/fragment shaders e vkCmdDraw, engine física identificada e pixels conferidos. Preservar compute/SDMA como prova desse escopo; conferir hashes antes de atribuir fontes atuais a binários históricos.
- **UMD D3D11 traduzida para Vulkan:** DDIs reais, recursos compartilhados, sincronização, residência, apresentação Windows e recuperação. DXVK local por aplicativo não fecha a entrega. Não anunciar GRAPHICS apenas removendo uma trava.
- **DCN básico integra o produto final em P9**, respeitando P5 e a governança de escrita; não é pré-requisito do primeiro desktop no GOP.
- **Está revogada a barra “STATUS DO DRIVER BC-250” antes de cada mensagem**, proposta, plano ou análise. Blocos antigos abaixo são histórico, não modelo de resposta nem status atual.
- **Após cada corrida física analisada, informar obrigatoriamente ao Daniel:**
  1. **Houve avanço?** Sim, não ou inconclusivo, comparando com a corrida anterior pertinente. Diferenciar avanço funcional de aprendizado de diagnóstico.
  2. **Sucessos:** o que passou, métricas e evidência.
  3. **Falhas:** erros, timeouts, pixels incorretos, device-lost, TDR, BugCheck e problemas observados; se nenhum foi observado, limitar a afirmação ao escopo medido.
  4. **Veredito:** sucesso, parcial, falha ou inconclusivo frente ao critério definido antes do ensaio. Código 0 e Exit 0 isolados não fecham o teste.
  5. **Recuperação e pendências:** driver ativo ao final, resultado do rollback, o que continua NÃO VALIDADO EM HARDWARE e próximo passo justificado.
- Identificar data/hora, candidato e pasta de evidência. Se faltarem logs ou medições, declarar a lacuna, sem inventar métricas. Desktop restaurado pelo BasicDisplay é recuperação, não sucesso do nosso DWM.
- Arquivar provas e atualizar BASE_DE_AVANCO.md quando houver conclusão nova. Alteração offline não é corrida. Não repetir percentuais históricos como medida de prontidão do produto.

### Registro histórico — não reproduzir automaticamente


```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pre-WDDM (Lab)        ███████████████████░  96%   Estavel; 600 quadros sem BugCheck, fusivel CLEAN em todas
SDMA (DMA Engine & Robustez)     ████████████████████ 100%   MARCO 3 SUSTENTADO (12/09 21:22): 600 blts, 0 TDR, Fallbacks=0, HostCpuCopies=0, 21,9 FPS
GCVM & Traducao de Memoria       ███████████████████░  97%   PTEs fisicas provadas; 600 quadros com zero fault/timeout sob carga DWM
RLC, CP & PM4 Direct Load        ███████████████████░  96%   Fence em VRAM provada; ring drenado (WPTR=RPTR=7200)
Telemetria & Observabilidade     ████████████████████  99%   Watchdog com rollback sem reboot restaurando BasicDisplay em Codigo 0
Compute & Async Queues (MEC/MQD) ███████████████████░  98%   RADV compute + MEC validados: 600 submits aceitos e aposentados (Exit 0)
Miniport WDDM Display+Render     ███████████████████░  94%   Swapchain 60 FPS Vulkan 100% concluido; DWM contido via D3D11LabPromotion (DeviceRemoved=False)
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
=============================================================================================
CORRIDA 10/09 11:22 (P0, lab 3.126.1.0) e AUDITORIA de 10/09 que corrigiu o enquadramento dela.
NOTA DE MANUTENCAO: esta barra estava parada em 08/09; as corridas de 09 e 10/09 entraram na
BASE_DE_AVANCO.md e nao aqui. O detalhe completo delas esta lá; aqui fica o que manda no veredito.
PROVADO NO SILICIO (P0): (1) a PAREDE UNICA DO D3D11 CAIU -- dwm.exe (pid 1204), Explorer.EXE
(5220) e dxgiadaptercache.exe (1964) carregaram a bc250umd.dll e receberam "OpenAdapter10_2:
Interface=0x000B000B, Version=0x00020009 -> S_OK (BC250 Native)"; (2) OpenAllocation fechou --
41/41 e 29/29 em status 0 nos binds 11:10 e 11:21, contra 5.745 falhas em 7.154 chamadas no bind
09:34; (3) 769 submissoes INDIRECT consecutivas com 12 voltas completas do anel SDMA; (4) a causa
raiz do BugCheck 0x119 no FenceId=770 e' VISIBILIDADE DE HDP, nao execucao -- RPTR=50244 =
12*4096+1092 no writeback E no registrador, STATUS1=000003FF (engine OCIOSA), RPTR_SAMPLES plano
por 200.000 polls, e FENCE_VISIBILIDADE antes_hdp=43DAFFFE depois_hdp=00001304 esperado=00001304.
A fence apareceu NO FLUSH. O flush ativo no laco de poll (bc250_sdma_present.c) ataca a causa
certa e teria saido no primeiro poll -- NAO VALIDADO EM HARDWARE.
RETIRADO DO VEREDITO POR NAO SE SUSTENTAR: o "Marco Historico: desktop real D3D11 exibido no
monitor" NAO ACONTECEU nesta corrida. As linhas Present #1 DESKTOP REAL DETECTADO e BYTE_PROOF
PASS citadas estao no offset 8.161.041 do log cumulativo, dentro da janela do bind das 09:34
(7.659.265..14.831.198); o bind das 11:22 (start_offset 15.009.172) teve 10 Presents, ZERO
deteccao de desktop, ZERO BYTE_PROOF e ZERO SDMA=YES. Tres binds foram fundidos num so' marco.
E o detector dispara com NonZero=1/256 -- 255 dos 256 dwords amostrados sao ZERO, e SCANOUT,
PRIMARY e SLOT_WORD amostraram 0/256: frame preto com um dword nao-nulo. No bind das 11:10, com
OpenAlloc 100% OK, foram 194 Presents e 10 blts, TODOS BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO),
BlankStreak=4. E nenhum pixel veio do D3D11: o log da UMD do DWM nao tem UMA linha de Present,
Flush ou blt; tem 1 "DrawIndexed: RECUSADO" e termina 2 s depois do load dentro de 4.159
CheckMultisampleQualityLevels. A imagem distorcida vista pelo operador e' atribuivel a transicao
de adaptador/BasicDisplay -- o MESMO engano ja cometido e corrigido em 07/09 18:16. O stride skew
7680->10240 e' H, nao medicao.
DIVIDA DISTINTA E ABERTA, que o flush de HDP NAO cobre: nos dois timeouts de 09/09 o RPTR congelou
EXATAMENTE em IndirectEndDW (1056=264dw; 1568=392dw), o flush NAO revelou a fence (depois_hdp=
43DAFFFE em 31 de 31 ocorrencias do corpo inteiro) e um deles trouxe GFXHUB_FAULT_SNAPSHOT
STATUS=00841B40 ADDR=0x82C -- fault de traducao REAL. Os dois eram o IB pesado Fill=1/11.075.584.
O caminho TIMEOUT -> STATUS_IO_DEVICE_ERROR -> 0x119 continua vivo para essa classe.
FAIXA C1, OFFLINE -- o fallback de CPU no Draw foi REMOVIDO. Achado por leitura de codigo no
candidato 3.126.1.0 ainda nao commitado: Bc250UmdCompositeDraw atendia pfnDraw e pfnDrawIndexed
com um laco de memcpy entre os shadows de CPU, marcava handled=TRUE ANTES de tentar qualquer
caminho de GPU e devolvia sucesso silencioso -- a linha honesta "DrawIndexed: RECUSADO" desaparecia
e shader, VB, IB, topologia, blend e escala de viewport eram ignorados. E' fallback de software
disfarcado de renderizacao (Lei 4) e tornaria INFALSIFICAVEL qualquer desktop na proxima corrida.
AGRAVANTE: o bc250_d3d_umd_test.c EXIGIA esse memcpy, congelando a mentira -- TERCEIRA ocorrencia
da classe (A15 obrigava a comparacao crua de RPTR; mcdm_callbacks_test exigia STATUS_SUCCESS sem
executor de TDR). Agora: memcpy removido, Bc250UmdSubstituteDrawWithBlt em fonte unica com
Bc250UmdBltGpu como UNICO produtor de pixel, elegibilidade por politica pura
Bc250UmdDrawBltSubstitutionIsLawful (tunable armado + winsys + residencia + mesmas dimensoes +
mesmo formato + viewport na origem cobrindo tudo + SEM blend, contando alpha-to-coverage e write
mask parcial), fechada por DEFAULT pelo tunable D3D11DrawBltSubstitution, log declarando "Draw
SUBSTITUIDO por copia 1:1 no SDMA -- NAO e' rasterizacao" e SubstitutedDrawCount contado SEPARADO
de DrawCount. Substituicao por blt NAO e' rasterizacao: sem compilador nao se sabe se o PS ligado
e' passe identidade, e afirmar equivalencia seria H apresentada como P0.
ACHADO COLATERAL DA C1: a suite OFFLINE dependia do estado da MAQUINA. CreateDevice e
GetCaps(Type=130) decidiam inline lendo HKLM\SOFTWARE\BC250\Umd\D3D11LabPromotion, que ficou em 1
depois do bind das 11:21 -- com ela armada o bc250_d3d_umd_test.c reprovava em duas assercoes SEM
NENHUMA MUDANCA DE CODIGO, o que REFUTA o "42/42 PASS" reportado. Extraida
Bc250Umd3DPipelineIsDeclared como fonte unica das duas respostas; a suite testa a tabela pura nos
quatro casos e assere o DDI por COERENCIA com o ambiente medido.
FAIXA C2, OFFLINE -- a evidencia das corridas estava sendo PERDIDA. C:\BC250_DISPLAY_LOG.txt e'
cumulativo e sobrevive a reboot, mas o binder o copia no FIM do roteiro; com BugCheck o watchdog
nao roda e a pasta fica truncada SEM QUE NADA INDIQUE. Medido em 10/09: SEIS pastas truncadas --
09:06 (123.676 bytes), 09:34 (6.934.920), 10:03 (65.987, sem arquivo), 10:23 (89.001, sem
arquivo), 11:10 (177.974) e 11:21 (438.038, contendo o FenceId=770 e a prova de HDP). A primeira
passada desta auditoria concluiu que a hipotese de HDP estava REFUTADA por analisar so' a pasta
truncada; a prova estava em disco o tempo todo. Novo tools/recover-bc250mcdm-lost-tail.ps1 resgata
o trecho com SHA-256 e proveniencia, delimitando o fim pelo start_offset do bind SEGUINTE quando
existe e DECLARANDO no json quando nao existe; o binder passou a chama-lo ANTES de cada bind,
depois de gravar o baseline novo. Ja executado: 6/6 pastas recuperadas.
Testes: build-umd EXIT=0 com "C1 politica 9/9, Draw fail-closed sem memcpy e tokens unicos PASS",
build-contract EXIT=0, quatro auditorias PASS, dois portoes novos (audit_draw_has_no_cpu_fallback
e audit_lost_log_tail_is_recovered) e SEIS mutacoes verificadas.
NADA DE C1/C2 FOI VALIDADO EM HARDWARE. Fusivel em TRIPPED (State=2, Boots=3): -Reset e boot frio
antes de qualquer bind. Desarmar D3D11LabPromotion se nao for objetivo da corrida.
A PROXIMA CORRIDA NAO DEVE PRODUZIR DESKTOP: com o Draw recusado honestamente e a substituicao
desarmada, TELA PRETA e' o resultado esperado e correto; qualquer imagem que aparecer deve ser
atribuida ao rollback/BasicDisplay ate prova em contrario.
Auditoria: docs/234_AUDITORIA_2026-09-10_C1_DRAW_SEM_CPU_E_C2_EVIDENCIA_PERDIDA.md
=============================================================================================
CORRIDA 10/09 12:16 (P0, lab 3.127.1.0) -- FLUSH DE HDP VALIDADO NO SILICIO, e BugCheck 0x133.
O criterio escrito ANTES da corrida foi CUMPRIDO: 1819 submissoes INDIRECT (1805 Fill=0/0 e 14
Fill=1/4096), FenceId ate 5335, 84 voltas do anel, ZERO IB_TRIPOD TIMEOUT, ZERO IB NAO EXECUTADO,
ZERO 0x119. Distribuicao de polls (n=1977): min 2, p50 3, p90 49, p99 180, MAX 181 -- contra
MAX 164.167 e morte no FenceId 770 na corrida das 11:22. A linha FENCE_VISIBILIDADE nao aparece
nenhuma vez, porque ela so' sai no caminho de timeout e nao houve timeout. A cauda ilimitada
acabou.
C1 VALIDADA EM HARDWARE: "DRAW_BLT_SUBSTITUTION: desarmado" e "DrawIndexed: RECUSADO --
compilador/shader-state/draw PM4 ausente (Rejected=1)" no dwm.exe (pid 1220); dwm, Explorer.EXE
(5304) e dxgiadaptercache.exe (10104) receberam S_OK (BC250 Native). TELA PRETA era o resultado
ESPERADO E CORRETO -- Draw recusado honestamente, zero memcpy, zero sucesso silencioso.
C2 VALIDADA EM HARDWARE: o arquivamento truncou em 15.725.444 e o resgate recuperou 3.492.427
bytes (sha256 896A6811...), que contem a corrida INTEIRA. Sem a C2 esta analise nao existiria.
PAREDE NOVA -- 0x133 DPC_WATCHDOG_VIOLATION (0x0, 0x501, 0x500, 0xfffff805220fb320) as 12:19:07,
MEMORY.DMP de 827.376.956 bytes. Arg2=0x501 contra Arg3=0x500: tempo excessivo em DISPATCH_LEVEL.
O log termina LOGO APOS uma submissao bem-sucedida (IB EXECUTADO ... FenceId=5335), sem timeout e
sem recusa -- NAO e' o caminho do 0x119.
HIPOTESE (H): DxgkDdiSubmitCommandVirtual e' chamada em DISPATCH_LEVEL e o laco de poll de
bc250_sdma_present.c:1226 roda DENTRO do KeAcquireSpinLock da linha 1129, soltando so' nas
1302/1408 -- 1819 submissoes, ate 181 polls de MMIO cada, em ~137 s. A A15 declarou esta divida
por escrito ("o laco do tripe pode gastar ~1 s segurando spinlock em DISPATCH_LEVEL"). NUANCE que
manda na leitura: os casos de 100-181 polls NAO sao visibilidade -- o flush ja dispara no
casamento de RPTR; sao espera REAL da engine. O flush matou a cauda INFINITA; sobrou o fato de
esperarmos de forma SINCRONA.
PROXIMO PASSO, SEM CUSTO DE CORRIDA: atribuir o 0x133 pelo stack do dump (comando em docs/235
secao 2.2). Somente depois implementar a conclusao ASSINCRONA. NAO baixar o teto de polls: teto
menor devolve STATUS_IO_DEVICE_ERROR, que e' o caminho conhecido do 0x119 -- trocaria um BugCheck
por outro.
NAO EXERCITADO: o IB de fill de 11.075.584 bytes nao apareceu nesta corrida, entao o
GFXHUB_FAULT STATUS=00841B40 ADDR=0x82C segue SEM TESTE e o flush de HDP NAO cobre essa classe.
ESTADO DA MAQUINA: BC-250 em CODIGO 43 (CM_PROB_FAILED_POST_START) com o 3.127.1.0 preso, fusivel
TRIPPED (Boots=2, LastStatus=0xC0000001) e SEM rollback (o BugCheck matou o watchdog).
tools\BC250_RECOVERY.cmd elevado, inspect-boot-fuse.ps1 -Reset e BOOT FRIO antes de novo bind.
ATENCAO AO EMPACOTAR: 3.127.1.0 JA FOI INSTALADO; suba o DriverVer fonte antes de gerar outro
candidato, senao dois artefatos diferentes carregam o mesmo DriverVer (erro do 3.92.1.0).
Handoff para a proxima IA: docs/235_HANDOFF_2026-09-10_12-16_FLUSH_HDP_VALIDADO_E_0x133.md
=============================================================================================
12/09 -- TRES CORRIDAS. A CAUSA RAIZ DA TELA PRETA FECHOU, E DUAS DELAS FORAM GASTAS EM
DEFEITO DE HOST (o segundo, meu, empilhando duas mudancas num candidato).
CORRECAO DE REGISTRO QUE MANDA NO VEREDITO: o present do "Marco 2" de 11/09 22:46 foi feito
por RtlCopyMemory da CPU -- zero linhas de SDMA no log daquele bind. O veredito saiu 10/10
PASS porque sdma_blt_executed casava com a string PRESENT_BLT, e o contador FallbackCpuCount
era CEGO a copia que o escape realmente fazia. QUARTA ocorrencia de "o gate carimbava o falso
sucesso" (A15, mcdm_callbacks_test, C1).
12:29 (P0, 3.149.1.0) -- primeira corrida com present 100% GPU. Com a copia por CPU removida,
o defeito que ela mascarava apareceu na tela: preto com faixa cinza. CAUSA RAIZ, por leitura
de codigo: Bc250GpuvaSpaceResolveAddress devolve BackingSegmentOffset, e o segmento 1 comeca
em LocalSegmentBaseOffset=0xA8C000, nao no byte zero. O present somava so' VramMcStart e lia
11.059.200 bytes ANTES da imagem. O tripe inteiro passava -- nenhuma das tres pernas confere
de ONDE se leu.
12:51 (P0, 3.150.1.0) -- a correcao trocou tambem a BASE por engano (caps.VramMcBase=0x450000000
no lugar de VramMcStart=0xF400000000). Origem e destino em dominios diferentes: blt em timeout
com fence no poison, 8 de 8, e present caindo na copia por CPU. O operador viu as barras
animando e relatou sucesso -- e o portao no_host_copy_fallback REPROVOU a corrida. Sem ele,
teria virado o terceiro marco falso pela mesma causa.
13:01 (P0, 3.151.1.0) -- PRESENT 100% POR GPU VALIDADO NO SILICIO. SegOff=0x1560000
SegBase=0xA8C000 SrcMc=0xF401FEC000 DstMc=0xF400000000; Submitted=584 Completed=584
Fallbacks=0 HostCpuCopies=0; BltMax=2171 us, media 1,37 ms. A "Garantia de Scanout GOP" por
CPU foi REMOVIDA na C3.1 e o ramo de CPU nao executou nenhuma vez, logo o UNICO escritor do
scanout foi o SDMA. Cadeia fechada: shader Vulkan no MEC -> VRAM -> byte proof 8/8 -> SDMA ->
scanout do GOP -> monitor. 584 quadros consecutivos (recorde anterior 473), a ~26 FPS contra
~1,25 FPS da copia por CPU.
PAREDE REMANESCENTE, identica nas tres: ruptura com status_device_removed, tdr_events=0,
Sub==Ret==Notified e ZERO StopDevice durante a corrida. O RADV leu ExecutionState=4 =
D3DDDI_DEVICEEXECUTION_STOPPED (P1, d3dumddi.h:3655) -- nao HUNG, nao DMAFAULT -- com "all
fences satisfied". A engine NAO travou: o device foi parado administrativamente. Correlacao
(H, nao P0): 7 crashes do dwm.exe + 8 WER em cada corrida, e a falha 0,46 s (13:01) e 0,5 s
(12:29) depois do SETIMO e ultimo. O que encerra a corrida parece ser o orcamento de crashes
do DWM, nao a contagem de quadros.
PROXIMA CORRIDA, falsificavel e sem rebuild: armar D3D11LabPromotion para o DWM receber device
e parar de fail-fastar, com D3D11DrawBltSubstitution DESARMADO (Draw segue RECUSADO, sem pixel
falso). Precedente que sustenta: em 10/09 12:16, com essa mesma combinacao, o dwm.exe gerou
546 KB de log e nenhum crash naquela pasta. 600/600 -> hipotese confirmada; parada em ~580 ->
REFUTADA.
DIVIDAS DECLARADAS: os 26 FPS NAO sao do blt (1,37 ms de 38 ms = 3,6%) -- a hipotese de que a
espera sincrona explicava o FPS esta REFUTADA por medicao; o custo esta no Vulkan. A divida do
laco em DISPATCH_LEVEL (A15) segue existindo, mas nao e' gargalo de FPS.
PACOTE 3.151.1.0 assinado e auditado: SYS 0D3E83AC..., UMD DD4DFED4... (inalterada desde o
3.149.1.0). Fusivel CLEAN, marcador de boot frio ARMADO: boot frio antes de qualquer bind.
=============================================================================================
13/09, OFFLINE e sem corrida -- o 0x116 da corrida 12/09 22:57 NAO foi hang de engine, e o
diagnostico do doc 260 estava errado nos tres pontos que mandavam no roteiro.
EVIDENCIA QUE NAO EXISTIA: a pasta da corrida tinha "display-log-this-bind.txt ausente no fim
do bind" (o BugCheck matou o watchdog). A cauda foi resgatada do log cumulativo pelo offset do
baseline e arquivada: display-log-this-bind-RECUPERADO.txt, 1.308.135 bytes, sha256 5E938F84...
Mais docs/evidence/dump-0x116-2026-09-12.txt, do MEMORY.DMP. Sem os dois, nada disto existiria.
P0 DO DUMP: a pilha e' VidSchiWorkerThread -> VidSchiCheckHwProgress -> VidSchiReportHwHang ->
TdrIsRecoveryRequired -> TdrBugcheckOnTimeout. ZERO quadros nossos. Arg3=0, ou seja NENHUMA
operacao nossa falhou. O "Failure.Exception.IP = Bc250McdmCollectDbgInfo" e' o owner tag que o
dxgkrnl publica em Arg2 para nomear o adaptador -- nao e' endereco de falha e nao houve excecao.
P0 DO LOG RECUPERADO: 1357 IBs executados, ZERO IB_TRIPOD TIMEOUT e CINCO TDR_QUIESCE -- em
todos os cinco RPTR==WPTR e STATUS1=000003FF (idle), e TDR_RESTART PROVADO 5 de 5. A engine
NUNCA travou e a nossa recuperacao funcionou nas cinco. O 0x116 e' o Windows estourando o
TdrLimitCount e desistindo do sexto, nao o driver falhando em recuperar.
ARMADILHA DO LOG, ERRO MEU APANHADO ANTES DE VIRAR VEREDITO: cheguei a concluir que a
aposentadoria parava no FenceId=15, porque so' ha 16 linhas [SdmaRetire] para 1357 IBs. FALSO --
[SdmaRetire] e' classe AMOSTRADA (bc250_display_ddi.c:574, FIRST=16 PERIOD=1024) e "IB EXECUTADO"
esta na lista de excecao. A assimetria era do logger. A aposentadoria rodou o tempo todo.
CAUSA RAIZ: a fence monitorada do blt era sinalizada FORA DE BANDA. Bc250UmdBltGpu montava um
SDMA_OP_FENCE sobre a VA da fence e o D3DDDICB_SUBMITCOMMAND saia sem objeto de sincronizacao.
O valor chegava a memoria; o dxgkrnl nunca ficava sabendo. Isso explica a intermitencia exata:
Bc250WinsysWaitForFenceFromCpu tem atalho que devolve OK se a fence ja chegou ao alvo -- o 1o
blt ganha a corrida contra a GPU e passa (foi o Substituidos=1); o 2o (janela 590x800, 800
pacotes de scanline) nao ganha, cai no pfnWaitForSynchronizationObjectFromCpuCb de verdade e
bloqueia PARA SEMPRE. O VidSch ve contexto sem progresso e reporta hang com a engine ociosa.
Casa com o log da UMD parando as 22:57:36.223 no ResourceUnmap anterior ao 2o draw: o trace
"Draw SUBSTITUIDO" so' sai DEPOIS de Bc250UmdBltGpu retornar. Segundo defeito no mesmo sitio:
Bc250SdmaEmitFence e' de 32 bits e a fence do WDDM e' de 64 (Bc250SdmaEmitFence64 esta ao lado).
CORRECAO DO 260: "tornar o Blt assincrono" nao tem o que fazer -- a submissao do Node 0 JA E'
assincrona desde antes dessa corrida (1974 marcadores (async), fila multi-slot, aposentadoria
por DPC/timer, e o portao audit_sdma_submission_is_asynchronous ja existia e passa).
FAIXA APLICADA (offline): Bc250WinsysSignalMonitoredFenceFromGpu nova sobre
pfnSignalSynchronizationObjectFromGpuCb, fail-closed em callback ausente, valor 0, fence nao
criada e contexto nao criado; o pacote de fence proprio SAIU do stream do blt (que caiu de 128
para 96 bytes) e o GCR ficou; quem materializa a escrita agora e' Bc250McdmBuildSignalMonitoredFence,
que JA EXISTIA no KMD, JA usa Fence64 e NUNCA tinha sido exercitado por este caminho.
Testes: TestSignalMonitoredFenceFromGpu novo no winsys, stub BltTestSignalGpu e invariante
g_BltSignals == g_BltSubmitsOk no teste do blt, mais modo de falha 5. build-umd EXIT=0,
build-contract EXIT=0, quatro auditorias PASS, portao novo audit_blt_fence_is_signalled_by_contract.
TRES MUTACOES VERIFICADAS: remover o sinal reprova a suite; reintroduzir Bc250SdmaEmitFence
reprova o portao; e fazer o teste voltar a exigir 128 bytes TAMBEM reprova -- esta terceira
existe porque "o gate congelava a mentira" ja apareceu quatro vezes (A15, mcdm_callbacks_test,
C1, sdma_blt_executed). O PRIMEIRO invariante que escrevi estava errado e a suite me pegou:
submit que FALHA nao deve sinalizar, e o curto-circuito do || e' proposital.
DIVIDAS DECLARADAS E NAO TOCADAS: (1) Bc250UmdBltGpu continua bloqueando dentro da DDI de Draw
-- com o sinal correto isso vira custo de FPS, nao mais correcao; (2) 800 pacotes de scanline
por janela, e COPY_LINEAR_SUB_WINDOW NAO tem procedencia local (Lei 7: inc/linux_ref/ so' tem
gc_10_1_0_sh_mask.h) -- nao inventar o encoding; (3) Journal NO_SPACE 8x mais um ActiveFlushTlb
-> STATUS_IO_DEVICE_ERROR, que comecam DEPOIS do 1o TDR e portanto nao sao a origem (doc 252);
(4) o BltCommand e' uma unica alocacao sobrescrita a cada blt, hoje segura SO' porque o wait e'
sincrono -- tornar o Draw nao-bloqueante exige anel de slots ANTES, ou a GPU le buffer reescrito.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES: confirmada -> o ciclo do DWM passa do 2o draw,
Substituidos cresce alem de 1 e a corrida termina com no maximo 1 TDR_QUIESCE; refutada -> voltam
os 5 TDR_QUIESCE com RPTR==WPTR e STATUS1=0x3FF, e o proximo suspeito passa a ser o contexto de
RENDER do DWM, nao o do blt. Procurar "[SignalMonitoredFence] Completo: FenceGpuVa=... Value=..."
no log: se NAO aparecer, o sinal nao chegou ao KMD e o defeito e' anterior, na winsys.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE. Nao foi empacotado candidato: o DriverVer fonte
precisa subir antes, porque o ultimo candidato lab foi 3.161.1.0. EMPACOTADO EM 13/09 e NAO INSTALADO:
PACOTE LAB 3.162.1.0, BuildKind=MCDM_LAB_BRINGUP, perfil DISPLAY_RENDER_LAB (RenderNode=1,
RenderOnlyLab=0), contrato WDDM 2.7, modo P1 PERSISTENT SUBMISSION, schema 135.
SYS fonte 97A48176..., SYS assinado 59F3D925..., UMD 6C7C70EB..., INF 9085671C...,
CAT A7243D41... Lab package audit PASS. A UMD assinada FOI CONFERIDA por string: as duas
linhas SignalMonitoredFenceFromGpu ENTER/LEAVE estao no binario, entao o empacotador nao
copiou a DLL anterior -- armadilha conhecida, o empacotador nao compila, so' copia.
Handoff: docs/261_HANDOFF_2026-09-13_0x116_E_A_FENCE_FORA_DE_BANDA.md
=============================================================================================
CORRIDA 13/09 05:50 (P0, lab 3.162.1.0) -- a correcao da fence NAO impediu o 0x116, e o
criterio que EU escrevi era irrespondivel pela instrumentacao existente.
Cauda resgatada e arquivada (o watchdog morreu de novo): display-log-this-bind-RECUPERADO.txt,
1.510.070 bytes, sha256 EE96AB8B... Dump novo: docs/evidence/dump-0x116-2026-09-13.txt, com
pilha IDENTICA a de 12/09 -- VidSchiWorkerThread -> VidSchiCheckHwProgress -> ReportHwHang ->
TdrIsRecoveryRequired -> TdrBugcheckOnTimeout, Arg3=0, Arg4=0xd, zero quadros nossos.
MEU CRITERIO ESTAVA MAL ESPECIFICADO. Escrevi "se [SignalMonitoredFence] nao aparecer, o sinal
nao chegou ao KMD". Nao apareceu, e Op=16 (DXGK_OPERATION_SIGNAL_MONITORED_FENCE) tambem nao --
mas isso NAO prova falha: o 1o blt devolveu S_OK com Substituidos=1, e isso exige
*BltFence.CpuVirtualAddress == BltSequence. Alguem escreveu a fence. O dxgkrnl honrou o sinal
por outro caminho, sem nos pedir para montar pacote. O criterio nao discriminava nada.
PIOR: BC250_WINSYS_LOG escrevia SO' em OutputDebugStringA e stderr, e o dwm.exe nao tem stderr.
NENHUMA linha da winsys chegou ao log arquivado, em nenhuma corrida. Eu escrevi um criterio
sobre linhas que a instrumentacao nao podia produzir.
ACHADO NOVO QUE INVERTE O SUSPEITO (P0): os DOIS blts executam corretamente na GPU, com
RPTR==WPTR e fence sobre poison -- 6 de 30.304 bytes (tela cheia 2560x1080, 1080 copias) e 5 de
22.464 (janela 590x800, 800 copias), todos Vmid=7. E os CINCO TDRs disparam SEMPRE logo apos o
blt de TELA CHEIA (FenceId 45, 428, 760, 993, 1372); NENHUM apos o de janela. O 260 e eu
culpavamos o 2o draw com blend; a medicao diz que o congelamento e' DEPOIS do blt de tela cheia
completar e ANTES da submissao seguinte -- intervalo que so' contem WaitPaging(src),
WaitPaging(dst) e o pfnLockCb do UploadAllocation. Correcao de registro: eu disse "10
TDR_QUIESCE"; sao CINCO -- o grep contou duas linhas por evento.
FAIXA APLICADA (offline): sink de trace passou a ser UNICO. Bc250WinsysTraceRaw nova em
bc250_wddm_winsys.c carimba hora/pid/imagem e anexa em C:\Windows\Temp\BC250_UMD_LOG_<pid>.txt;
BC250_WINSYS_LOG passou a escreve-lo (stderr fica para as suites offline) e Bc250UmdTrace
DELEGA a ele, eliminando a segunda copia da escrita em arquivo que existia na UMD.
Bc250WinsysSubmitGpuVa ganhou ENTER/LEAVE -- era a UNICA funcao da winsys sem trace, e
pfnSubmitCommandCb bloqueia com a fila do contexto cheia.
Testes: build-umd EXIT=0, build-contract EXIT=0, quatro auditorias PASS, portao novo
audit_umd_trace_reaches_the_evidence_file. TRES MUTACOES VERIFICADAS: tirar o roteamento do
sink reprova; renomear SubmitGpuVa ENTER reprova; e Bc250UmdTrace voltar a chamar CreateFileA
por conta propria reprova. O portao antigo que exigia BC250_UMD_LOG_%lu.txt em bc250_d3d_umd.c
foi reapontado para a fonte unica.
PACOTE LAB 3.163.1.0 assinado, auditado e NAO INSTALADO: SYS fonte C1229885..., SYS assinado
55D2F2E9..., UMD BCAB3634..., INF 04288201..., CAT EE6BC8F3..., perfil DISPLAY_RENDER_LAB,
WDDM 2.7, P1 PERSISTENT SUBMISSION, schema 135. Lab package audit PASS. A UMD assinada FOI
CONFERIDA por string: SubmitGpuVa ENTER/LEAVE, WDDM_WINSYS (29x), BC250_UMD_LOG_ e
SignalMonitoredFenceFromGpu ENTER estao no binario.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES e agora RESPONDIVEL: o log do dwm.exe passa a conter
linhas [WDDM_WINSYS]. A ULTIMA linha antes do congelamento NOMEIA o callback que bloqueou --
WaitPaging, Upload/Lock ou SubmitGpuVa. Se a ultima for um ENTER sem LEAVE, o bloqueio e'
naquele callback e a correcao deixa de ser hipotese. Se as linhas [WDDM_WINSYS] NAO aparecerem,
o sink falhou e o defeito e' da instrumentacao, nao do driver.
ESTA FAIXA E' SO' INSTRUMENTACAO: nao altera nenhum caminho de GPU e NAO deve corrigir o 0x116.
Esperar TDR e BugCheck de novo -- o que muda e' que desta vez o log diz ONDE.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
CORRIDA 13/09 06:08 (P0, lab 3.163.1.0) -- A INSTRUMENTACAO PAGOU E O PONTO DE BLOQUEIO TEM NOME.
Cauda resgatada: display-log-this-bind-RECUPERADO.txt, 1.067.888 bytes, sha256 EBEDED30...
Dump: docs/evidence/dump-0x116-2026-09-13.txt, pilha IDENTICA as anteriores (VidSchiWorkerThread
-> VidSchiCheckHwProgress -> ReportHwHang -> TdrIsRecoveryRequired -> TdrBugcheckOnTimeout,
Arg3=0, Arg4=0xd, zero quadros nossos). 244 linhas [WDDM_WINSYS] no dwm.exe -- o sink unico
funcionou na primeira tentativa.
O SINAL DE FENCE PELO CONTRATO ESTA VALIDADO EM HARDWARE para a 1a submissao (P0): o 1o blt fez
SubmitGpuVa 30304 -> LEAVE hr=0, SignalMonitoredFenceFromGpu Value=1 -> LEAVE hr=0,
WaitForFenceFromCpu Target=1 Current=0 -> LEAVE hr=0 Current=1, tudo em 2 ms. A fence avancou
0->1 PELO CAMINHO DO CONTRATO. A correcao da manha nao era errada; era insuficiente.
PONTO DE BLOQUEIO, COM NOME (P0): o 2o blt fez SubmitGpuVa 22464 -> LEAVE hr=0,
SignalMonitoredFenceFromGpu Value=2 -> LEAVE hr=0, e entao
"WaitForFenceFromCpu ENTER: TargetFence=2 CurrentFence=1" SEM LEAVE. O KMD registrou
IB EXECUTADO do IB de 22464 com RPTR==WPTR e fence sobre poison: a GPU FEZ o trabalho; a fence
monitorada simplesmente nao avancou de 1 para 2. Op=16 (SIGNAL_MONITORED_FENCE) nunca foi
pedido e nao houve Unhandled Op.
DUAS CORRECOES DE REGISTRO MINHAS: (1) eu afirmei que TODOS os TDRs seguiam o blt de TELA CHEIA
e usei isso para inverter o suspeito -- ERRADO; nesta corrida 4 dos 5 seguem o blt de JANELA. O
invariante real e' banal: o TDR segue o ULTIMO blt, qualquer que seja. (2) eu disse "10
TDR_QUIESCE"; sao CINCO -- o grep contava duas linhas por evento.
CORRECAO DE VERDADE (offline), atacando a causa e nao o sintoma -- a espera por draw SAIU:
(A) ANEL DE SLOTS. Havia UMA alocacao de command buffer reescrita a cada blt, e por isso a DDI
de Draw PRECISAVA esperar a fence da propria submissao antes de retornar -- era isso que
congelava a thread de composicao do DWM e fazia o VidSch reportar hang com a engine OCIOSA.
Agora sao BC250_BLT_SLOT_COUNT=4 command buffers, um por slot, e a unica espera que sobrou e' a
de RECICLAGEM: pela submissao que usou AQUELE slot 4 blts atras, que na pratica ja terminou. No
regime normal ninguem bloqueia.
(B) HONESTIDADE DO TRACE: S_OK agora significa SUBMETIDO, nao concluido. A linha virou
"Draw SUBSTITUIDO por copia 1:1 SUBMETIDA ao SDMA -- NAO e' rasterizacao e NAO e' prova de
conclusao". A prova de execucao continua sendo a do KMD (IB EXECUTADO, RPTR==WPTR, fence sobre
poison), nao a da UMD.
(C) DUAS FALHAS MUDAS na DDI Bc250McdmSignalMonitoredFence, achadas por leitura: o retorno
STATUS_INVALID_PARAMETER com pDmaBuffer==NULL nao logava NADA -- era impossivel distinguir "o
dxgkrnl nunca chamou" de "nos recusamos calados", exatamente a ambiguidade que a Lei 4 proibe e
que audit_process_lifecycle_ddis_speak ja trava em outras DDIs. Agora a entrada SEMPRE fala,
com FenceGpuVa, Value, DmaSize, pDma e KernelSubmissionType.
(D) BUG REAL DE CAPACIDADE nas DUAS variantes do sinal: Bc250PagingFloorPow2Dwords arredondava
a capacidade do buffer LINEAR para baixo ate potencia de dois -- 20 dwords viravam 16 e a DDI
recusava com INSUFFICIENT_DMA_BUFFER um buffer que cabia de sobra. Agora exigimos o tamanho
REAL e declaramos aos emissores uma janela fixa de 32 dwords que cabe dentro do verificado.
Testes: o laco generico de modos de falha foi reescrito em casos EXPLICITOS, porque o modo 3
(fence que nao avanca) deixou de ser falha e virou a PROVA de que a DDI nao espera a propria
submissao -- 4 blts consecutivos passam com a fence congelada. Mais o invariante do anel
(4 submissoes, 4 GPUVAs distintas, zero esperas) e o contraponto (com a fence congelada, a
5a volta PRECISA esperar). build-umd EXIT=0, build-contract EXIT=0, quatro auditorias PASS,
portao novo audit_blt_draw_does_not_block_on_its_own_fence. DUAS MUTACOES VERIFICADAS:
reintroduzir a espera pela propria fence reprova o portao; e o teste deixar de exigir
"g_BltWaits == 0" TAMBEM reprova -- esta segunda existe porque "o gate congelava a mentira" ja
apareceu cinco vezes (A15, mcdm_callbacks_test, C1, sdma_blt_executed, teste do blt com 128
bytes). Tres assercoes minhas estavam erradas e a suite me pegou nas tres.
PACOTE LAB 3.164.1.0 assinado, auditado e NAO INSTALADO: SYS fonte C571C731..., SYS assinado
2468E0A4..., UMD 9A0B61D0..., INF 2D2014A9..., CAT A406BD6A..., perfil DISPLAY_RENDER_LAB,
WDDM 2.7, P1 PERSISTENT SUBMISSION, schema 135. Lab package audit PASS. Binarios CONFERIDOS por
string: "SUBMETIDA ao SDMA" e "SubmitGpuVa ENTER" na UMD; "[DdiSignalMonitoredFence] ENTER" e
"RECUSADO: pDmaBuffer NULL" no SYS.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES: confirmada -> nao existe mais "WaitForFenceFromCpu
ENTER" sem LEAVE no log do dwm.exe, Substituidos cresce muito alem de 1, e a corrida termina
com no maximo 1 TDR_QUIESCE. Refutada -> voltam os cinco TDR com a engine ociosa, e entao o
bloqueio nao era o unico caminho pelo qual o dxgkrnl perde o progresso.
INFORMACAO NOVA EM QUALQUER DESFECHO: se o dxgkrnl chamar a DDI de sinal, a linha
"[DdiSignalMonitoredFence] ENTER" aparece e diz COM QUE ARGUMENTOS; se ele nunca chamar, a
ausencia passa a ser conclusiva porque toda saida da DDI agora fala.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
CORRIDA 13/09 08:12 (P0, lab 3.164.1.0) -- O ANEL DE SLOTS NAO IMPEDIU O 0x116, e a corrida
entregou o DISCRIMINANTE que reposiciona o problema inteiro.
Cauda resgatada: display-log-this-bind-RECUPERADO.txt, 963.360 bytes, sha256 59EFAFFE...
Quarto dump seguido com pilha IDENTICA (docs/evidence/dump-0x116-2026-09-13c.txt): Arg3=0,
Arg4=0xd, zero quadros nossos. !analyze -v esgotou o que tinha a dar -- parar de gasta-lo.
O DISCRIMINANTE (P0): o KMD executou 958 IBs. 953 eram buffers de PAGINACAO do proprio dxgkrnl
(Op=2/8/11/12) e nao produziram UM TDR. Os outros 5 eram submissoes de CONTEXTO da UMD no
Node 0 -- que e' o no de PAGINACAO (PagingNodeIndex=0, bc250_display_ddi.c:1571) -- e CADA UMA
foi seguida de um TDR, sempre com RPTR==WPTR e STATUS1=0x3FF (engine ociosa, anel drenado).
Cinco blts, cinco TDRs, 1 para 1. E as submissoes de UMD no Node 1 (RADV, 600 quadros de
12/09) NUNCA deram um TDR. Nao e' o blt: e' que NENHUMA submissao de contexto e' aceita como
progresso pelo escalonador.
DUAS HIPOTESES MINHAS MORTAS NA MESMA CORRIDA: (1) "nao notificamos conclusao de contexto" --
FALSO, a linha IB EXECUTADO sai de dentro de Bc250ActiveSdmaRetireAndNotify, entao os blts SAO
aposentados e entram em lote com DMA_COMPLETED; (2) a marca d'agua por no descartando
notificacoes -- REFUTADA por medicao: 958 FenceIds estritamente crescentes, zero retrocessos.
Zero GFXHUB_FAULT e FaultStatus=0 nos cinco TDRs: nao ha falha de traducao.
CORRECAO DE REGISTRO: na corrida anterior eu disse que todos os TDRs seguiam o blt de TELA
CHEIA; aqui 4 dos 5 seguem o de JANELA. O invariante e' banal -- o TDR segue o ULTIMO blt.
DEFEITO DE COLETA, MEU: os bc250_umd_log_*.txt da pasta de evidencia sao SNAPSHOTS de ~5 s
depois do bind. O do dwm.exe termina no meio de um CreateResource, antes de qualquer draw; os
logs do fim da corrida ficaram em C:\Windows\Temp e sumiram. O criterio que escrevi para a
08:12 ficou SEM RESPOSTA por isso. A licao da C2 nao tinha sido estendida aos logs da UMD.
FAIXA APLICADA (offline) -- a composicao SAIU do no de paginacao:
(A) Novo opcode BC250_ESCAPE_OP_COMPOSITE_BLT no contrato de escape. A UMD alcanca o KMD por
pfnEscapeCb e o KMD executa com Bc250SdmaPresentBlt -- o mesmo caminho que fez 584 blts
consecutivos sem UM TDR em 12/09 13:01, que nao cria pacote de contexto e nao passa pelo
escalonador. Origem E destino sao resolvidos por Bc250SegmentOffsetToMc (a aritmetica da C3.4),
com recusa para faixa que nao couber no segmento e para sobreposicao.
(B) A UMD nao cria mais contexto, paging queue, command buffer nem fence para o blt. Paging
queue, contexto virtual, anel de slots e Bc250UmdBltInit/Teardown foram REMOVIDOS -- nao ficou
dead code; esta no git.
(C) ESCOPO DECLARADO E ESTREITO: so' copia LINEAR (pitch igual nos dois lados, retangulo
cobrindo linhas inteiras a partir da origem). E' o draw de tela cheia do DWM. O anel do KMD tem
4096 bytes e nao comporta copia por scanline; retangulo de JANELA e' RECUSADO com log honesto,
nunca aproximado. Isso significa que janelas NAO serao compostas nesta corrida.
(D) Bug real no guard do contrato de escape: ele escolhia kernel-mode por NTDDI_VERSION, que a
UMD tambem define, e incluir <ntdef.h> depois de <windows.h> redefinia _PROCESSOR_NUMBER,
_GROUP_AFFINITY e _FLOAT128. Corrigido para _KERNEL_MODE.
(E) O (a) que vinha de graca: CTX_PROBE e CONTEXT SWITCH sairam da amostragem. Na 08:12
sobraram 3 CTX_PROBE em ~958 submissoes, e eram exatamente as linhas que diriam se a root do
VMID do contexto foi instalada. Sao raras por natureza, nao um diluvio.
Testes: bc250_d3d_umd_test reescrito em torno do invariante NEGATIVO -- a composicao nao pode
gerar NENHUMA submissao de contexto (g_BltSubmits == 0) --, mais recusa por pitch diferente,
por retangulo deslocado, por pfnEscapeCb ausente, e propagacao de falha do KMD.
build-umd EXIT=0, build-contract EXIT=0, quatro auditorias PASS, portao novo
audit_composition_never_submits_a_context substituindo os dois portoes do caminho antigo.
DUAS MUTACOES VERIFICADAS: reintroduzir Bc250WinsysSubmitGpuVa no blt reprova; e o teste deixar
de exigir zero submissao TAMBEM reprova.
ARMADILHA REGISTRADA: minha funcao nova de resolucao continha o mesmo anchor textual que
audit_present_source_uses_segment_base usa para delimitar a janela do ramo de present, e isso
ampliou a janela ate englobar a soma crua legitima do caminho de endereco fisico. O portao
reprovou corretamente; renomeei o local em vez de afrouxar o portao.
PACOTE LAB 3.165.1.0 assinado, auditado e NAO INSTALADO: SYS fonte 010C076D..., SYS assinado
DF38D09E..., UMD 2F1F8E2B..., INF E9FCA938..., CAT DAED531F..., perfil DISPLAY_RENDER_LAB,
WDDM 2.7, P1 PERSISTENT SUBMISSION, schema 135. Lab package audit PASS. Binarios CONFERIDOS por
string: "BltViaKmd ENTER" e a recusa de scanline na UMD; "COMPOSITE_BLT OK", a recusa de
resolucao e "CTX_PROBE" no SYS.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES: confirmada -> aparecem linhas
"[Bc250Escape] COMPOSITE_BLT OK" e a corrida termina com NO MAXIMO 1 TDR_QUIESCE, contra os
cinco de todas as corridas anteriores. Refutada -> voltam os cinco TDR mesmo sem UMA submissao
de contexto, e entao o no de paginacao nao era o discriminante e o suspeito passa a ser o
proprio ciclo de vida do contexto do DWM, nao a submissao.
NAO ESPERAR DESKTOP: janelas sao RECUSADAS por escopo declarado, e o Draw real continua sem
backend. Tela preta segue sendo o resultado correto; o que esta em teste e' o TDR, nao o pixel.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
CORRIDAS 13/09 11:25 e 11:32 (P0, lab 3.165.1.0) -- DUAS CORRIDAS SEGUIDAS SEM TDR E SEM
BUGCHECK, as primeiras de toda a sequencia. Contra CINCO TDRs e 0x116 em todas as anteriores.
Confirmado pelo Event Log: o ultimo MEMORY.DMP e' das 08:14, da corrida anterior.
11:25 -- 2741 IBs, ZERO TDR. O DWM morreu quatro chamadas de DDI antes do primeiro Draw, entao
o escape nao rodou e o criterio ficou sem resposta.
11:32 -- 3283 IBs, ZERO TDR, e o caminho novo FOI ALCANCADO: "BltViaKmd ENTER: 2560x1080
pitch=10240" no dwm.exe. Primeira vez que o DWM chega ao Draw pelo caminho do KMD.
ACHADO QUE REFUTA A MINHA PROPRIA HIPOTESE: a desamostragem do CTX_PROBE (2741 e 3283 linhas,
contra 3 na corrida 08:12) mostrou 37 submissoes de CONTEXTO em VMID 7 com ZERO TDR, mais
"VMID7 CONTEXT SWITCH" com root valida (0x45FDFF000). Na 08:12 havia 22 submissoes em VMID 7 e
so' 5 eram nossos blts -- as outras 17 nunca deram TDR e eu nao tinha visto porque a amostragem
escondia. "Submissao de contexto no no de paginacao causa TDR" esta REFUTADO: era algo
especifico do NOSSO blt, nao da classe inteira. Isso elimina um espaco grande de suspeitos.
O CRITERIO NAO FECHOU, POR DEFEITO MEU, E SAO DOIS:
(1) O log do KMD teve ZERO linhas COMPOSITE_BLT -- nem OK, nem RECUSADO. As duas saidas de
recusa do handler que eu escrevi ontem eram MUDAS (payload menor que 4 bytes no dispatch, e
tamanho menor que o contrato no composite). Impossivel distinguir "nao chegou ao handler" de
"recusamos calados" -- a mesma ambiguidade que a Lei 4 proibe e que eu havia corrigido de manha
em Bc250McdmSignalMonitoredFence, reintroduzida por mim no codigo novo.
(2) O log do dwm.exe termina no "BltViaKmd ENTER" as 11:32:33.465, mas o primeiro crash do DWM
foi as 11:33:03 -- TRINTA SEGUNDOS depois. O log nao terminou no escape: ele e' um SNAPSHOT
tirado pelo coletor ~6 s apos o bind. A linha "BltViaKmd LEAVE", com o hr do pfnEscapeCb, que
responderia a corrida inteira, existiu e foi descartada. TERCEIRA corrida seguida perdida pela
mesma causa (08:12, 11:25, 11:32), depois de eu ter identificado o defeito e nao o corrigido.
Dado colateral: 3.282 escapes de TERCEIROS com opcode 0x2 chegaram ao nosso handler na 11:32 --
nenhum nosso (o DWM chamou BltViaKmd UMA vez). O log 1-em-1 que eu pus na recusa afogou o
arquivo, e "RECUSA" esta na lista que escapa da amostragem global.
FAIXA APLICADA (offline): (A) entrada do handler de composicao SEMPRE fala, com DataSize
recebido e esperado, e a recusa por tamanho nomeia a causa ("ABI divergente entre UMD e KMD");
(B) a recusa de payload invalido no dispatch deixou de ser muda; (C) a recusa de opcode
desconhecido ganhou teto por contador PROPRIO (8 primeiras + 1 em 1024), porque o teto global
nao a alcanca; (D) o binder passou a coletar os logs da UMD TAMBEM no fim da janela fisica, em
arquivos -FINAL que NAO sobrescrevem o snapshot -- os dois juntos mostram o que mudou.
Testes: build-umd EXIT=0, build-contract EXIT=0, quatro auditorias PASS, portoes novos dentro
de audit_composition_never_submits_a_context e audit_umd_trace_reaches_the_evidence_file. DUAS
MUTACOES VERIFICADAS: a recusa por tamanho voltar a ser muda reprova; e o binder voltar a
coletar so' no inicio TAMBEM reprova.
PACOTE LAB 3.166.1.0 assinado, auditado e NAO INSTALADO: SYS fonte DB2B794A..., SYS assinado
CFF5EBC7..., UMD 128C1FFC..., INF 38A95E60..., CAT 54668C1B..., perfil DISPLAY_RENDER_LAB,
WDDM 2.7, P1 PERSISTENT SUBMISSION, schema 135. Lab package audit PASS. Binarios CONFERIDOS por
string: "COMPOSITE_BLT ENTER", "ABI divergente entre UMD e KMD" e "payload ausente ou menor que
4 bytes" no SYS; "BltViaKmd ENTER" na UMD; bc250_umd_log-FINAL.txt no binder.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES e agora RESPONDIVEL PELO LOG DO KMD, que sobrevive:
a linha "[Bc250Escape] COMPOSITE_BLT ENTER" aparece com DataSize -- se DataSize divergir do
esperado, a ABI entre UMD e KMD esta quebrada e a linha diz os dois numeros; se aparecer
"COMPOSITE_BLT OK", a composicao executou no caminho provado do KMD; se NAO aparecer NENHUMA
das duas, o escape nao chega ao handler e o defeito e' do dxgkrnl recusando a chamada, nao
nosso. Os tres desfechos sao distinguiveis, o que nao era verdade nas tres corridas anteriores.
ESTA FAIXA E' CORRECAO DE DEFEITO, NAO EXPERIMENTO: as duas saidas mudas eram violacao da Lei 4
introduzida por mim, e a coleta em snapshot custou tres criterios.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
CORRIDA 13/09 11:55 (P0, lab 3.166.1.0) -- TERCEIRA CORRIDA SEGUIDA SEM TDR E SEM BUGCHECK
(3645 IBs, zero TDR_QUIESCE), e o criterio FECHOU no terceiro desfecho.
Cauda resgatada: display-log-this-bind-RECUPERADO.txt, 3.878.073 bytes.
O SYS novo estava rodando, PROVADO pelo proprio teto que ele introduziu: o flood de escapes de
terceiros caiu de 3.282 linhas (11:32) para 11 (8 primeiras + periodicas), com "ocorrencia #".
CRITERIO, RESPONDIDO: COMPOSITE_BLT ENTER = 0, OK = 0, RECUSADO = 0, "payload ausente" = 0.
A linha ENTER agora e' INCONDICIONAL no topo do handler, logo a ausencia e' conclusiva: o
NOSSO escape NAO chega ao handler, enquanto escapes de TERCEIROS chegam normalmente. Pelo
criterio escrito antes da corrida, isso significa que o dxgkrnl recusa a chamada antes de nos
alcancar -- nao e' recusa nossa. O dwm.exe chamou BltViaKmd ENTER as 11:55:38.052.
DEFEITO MEU, QUARTA VEZ NA MESMA CLASSE: a coleta -FINAL que eu havia adicionado caiu dentro
de `if ($EnablePhysicalRenderOnly)`, ramo que este perfil NAO executa. Nao ha arquivos -FINAL
na pasta e a linha "BltViaKmd LEAVE", com o hr do pfnEscapeCb -- o unico dado que nomeia a
recusa --, perdeu-se de novo. Os logs em C:\Windows\Temp ja haviam sido apagados.
FAIXA APLICADA (offline), tres frentes:
(A) CORRECAO DE CONTRATO, nao instrumentacao: o escape passou a declarar
args.Flags.HardwareAccess = 1. O handler do KMD dirige o anel SDMA0; sem essa flag o dxgkrnl
nao serializa o escape contra o dispositivo. E' candidato direto a causa da recusa.
(B) ENTRADA DA DDI DE ESCAPE FALA, antes de qualquer despacho: [McdmEscape] ENTER imprime
DataSize, o primeiro dword (o opcode do contrato), hDevice, hContext e Flags, mais se o handler
esta instalado. Sem isso era impossivel separar "o dxgkrnl nunca nos chamou" de "chamou com um
payload que nao reconhecemos". Teto por contador proprio (8 primeiras + 1 em 512), e qualquer
dword com a assinatura 0xBC25xxxx escapa do teto -- o nosso NUNCA e' amostrado fora.
A ausencia de handler instalado tambem deixou de ser muda.
(C) COLETA FINAL NO CAMINHO COMUM do binder, agora fora do ramo morto: janela de
WatchdogTimeoutSeconds-60 s (piso 30) e entao copia dos BC250_UMD_LOG_*.txt para arquivos
-FINAL que NAO sobrescrevem o snapshot, mais a cauda do log do KMD pelo offset do baseline. O
watchdog continua sendo a rede de seguranca.
(D) LOG MAXIMO na UMD: BltViaKmd ENTER agora imprime Opcode, sizeof do payload, hDevice,
hContext e Flags; o LEAVE imprime hr e ResultCode. Se o KMD imprimir um DataSize diferente do
sizeof da UMD, a ABI esta divergente e os DOIS numeros estarao no log.
Testes: build-umd EXIT=0, build-contract EXIT=0, quatro auditorias PASS. TRES MUTACOES
VERIFICADAS: tirar HardwareAccess reprova; a entrada da DDI de escape voltar a ser muda
reprova; e o binder perder a coleta FINAL TAMBEM reprova.
PACOTE LAB 3.167.1.0 assinado, auditado e NAO INSTALADO: SYS fonte 772045D5..., SYS assinado
BC64C7A7..., UMD F832A457..., INF FCBF218E..., CAT D60E37C1..., perfil DISPLAY_RENDER_LAB,
WDDM 2.7, P1 PERSISTENT SUBMISSION, schema 135. Lab package audit PASS. Binarios CONFERIDOS por
string: "[McdmEscape] ENTER #%u", "nenhum handler instalado" e "COMPOSITE_BLT ENTER" no SYS;
"BltViaKmd ENTER", "hDevice=%p hContext=%p Flags" e "dxgkrnl ou o KMD recusaram" na UMD.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES -- tres desfechos, todos distinguiveis e todos
respondidos por logs que agora SOBREVIVEM:
  1. "[McdmEscape] ENTER" com Dword0=0xBC250002 -> o escape chega ao KMD; a linha seguinte
     (COMPOSITE_BLT ENTER/OK/RECUSADO) diz o que fizemos com ele.
  2. "BltViaKmd LEAVE: hr=..." com hr != 0 e NENHUM [McdmEscape] com 0xBC250002 -> o dxgkrnl
     recusou; o hr nomeia o motivo, e HardwareAccess deixa de ser hipotese num sentido ou no
     outro.
  3. Nenhuma das duas -> a coleta FINAL falhou de novo e o defeito e' do binder, nao do driver.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
CORRIDA 13/09 12:09 (P0, lab 3.167.1.0) -- QUARTA CORRIDA SEGUIDA SEM TDR E SEM BUGCHECK
(3414 IBs, zero TDR_QUIESCE), A COLETA FINAL FUNCIONOU, e a flag que EU adicionei nesta faixa
foi a causa do bloqueio.
A COLETA ESTA RESOLVIDA: o binder gravou 12 arquivos bc250_umd_log_*-FINAL.txt e
display-log-this-bind-FINAL.txt (3.644.356 bytes) ao fim da janela de 120 s, no caminho comum.
Cinco corridas seguidas haviam perdido o dado decisivo; esta nao perdeu. A tentativa anterior
tinha caido dentro de `if ($EnablePhysicalRenderOnly)`, ramo que este perfil nao executa.
CORRECAO DE UM VEREDITO MEU, PELA EVIDENCIA NOVA: eu havia concluido, olhando so' o lado do
KMD, que "o dxgkrnl RECUSA o nosso escape". FALSO. O log da UMD mostra SETE "BltViaKmd ENTER"
-- um por instancia do DWM -- e ZERO "LEAVE". O pfnEscapeCb NAO RETORNA. Nao e' recusa, e'
BLOQUEIO.
CAUSA, MEDIDA: Flags=0x00000001 nas sete chamadas -- o HardwareAccess que eu armei nesta faixa
apresentando-o a voce como "exigencia de contrato, nao enfeite". HardwareAccess=1 faz o dxgkrnl
SERIALIZAR o escape contra o dispositivo, esperando a GPU quiescer antes de despachar. Isso e'
chamado de dentro da DDI de Draw do DWM, com o dispositivo ocupado: deadlock por construcao. O
handler do KMD dirige o anel de present dele, protegido pelo NOSSO spinlock, e nao precisa da
serializacao do dxgkrnl.
O CONTRASTE ESTA NOS PROPRIOS DADOS: os 14 [McdmEscape] ENTER da corrida sao todos de
TERCEIROS, com Dword0=0x00000002, DataSize=388/492 e Flags=0x00000000 -- e atravessam. O unico
escape com HardwareAccess era o nosso, e foi o unico que travou. Zero escapes com
Dword0=0xBC250002 chegaram ao KMD, e essa ausencia e' conclusiva porque o teto do log ISENTA
qualquer dword com assinatura 0xBC25xxxx.
DADO COLATERAL QUE VALE: bloquear dentro de um escape NAO dispara TDR -- sete threads de DWM
travadas e zero TDR_QUIESCE. Bloquear numa fence monitorada dispara. Sao caminhos diferentes no
escalonador, e isso estreita o modelo.
CONFIRMADO PELO LOG DA UMD: Opcode=0xBC250002 e sizeof=48 nas sete chamadas -- a ABI do lado da
UMD esta correta, e o DataSize que o KMD veria seria 48.
FAIXA APLICADA (offline): args.Flags.Value = 0u. Nao e' "tentar o outro valor" -- a
serializacao de dispositivo nao nos serve e cria o deadlock. hDevice fica COMO ESTA de
proposito, para mudar UMA variavel: se o escape ainda falhar, o hr do LEAVE nomeia o motivo, e
o hDevice nao-nulo (que difere dos 14 que atravessam) vira o proximo suspeito ja identificado.
O PORTAO FOI INVERTIDO: audit_composition_never_submits_a_context EXIGIA HardwareAccess porque
eu o havia declarado contrato; agora o PROIBE, com a medicao no comentario, e exige
args.Flags.Value = 0u. Mutacao verificada: rearmar a flag reprova nos dois sentidos.
build-umd EXIT=0, build-contract EXIT=0, quatro auditorias PASS.
PACOTE LAB 3.168.1.0 assinado, auditado e NAO INSTALADO: SYS assinado 80374141..., UMD
FCB3B552..., INF D4382AAE..., CAT FD042E32..., perfil DISPLAY_RENDER_LAB, WDDM 2.7,
P1 PERSISTENT SUBMISSION. Lab package audit PASS. ARMADILHA REGISTRADA: a primeira passada do
empacotador falhou com IOException em Copy-Item e deixou o pacote INCONSISTENTE -- a auditoria
reprovou com seis hashes divergentes. Repetir o empacotador resolveu. NUNCA aceitar pacote sem
a auditoria PASS logo depois.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES: a linha "BltViaKmd LEAVE: hr=..." PRECISA aparecer
-- a coleta FINAL ja provou que consegue captura-la. Entao:
  1. LEAVE com hr=0 e "[Bc250Escape] COMPOSITE_BLT OK" no KMD -> a composicao executa pelo
     caminho provado do KMD.
  2. LEAVE com hr != 0 e nenhum [McdmEscape] com 0xBC250002 -> o dxgkrnl recusa, e o hr nomeia
     o motivo; hDevice passa a ser o suspeito nomeado.
  3. LEAVE ausente de novo -> o bloqueio nao era o HardwareAccess, e a hipotese cai.
NAO ESPERAR DESKTOP: janelas seguem RECUSADAS por escopo declarado e o Draw real continua sem
backend. Tela preta e' o resultado correto.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
AUDITORIA DA CORRIDA 13/09 16:28:27 (pacote 3.172.0.0) -- O DIAGNOSTICO DO docs/265 NAO SE
SUSTENTA NA PROPRIA EVIDENCIA DELE, E A PAREDE E' OUTRA. Tudo abaixo e' OFFLINE, zero corridas.
O QUE A CORRIDA REALMENTE PROVOU (P0, e vale): 0x10E eliminado, zero BugCheck, zero TDR, 3.757
IBs indiretos, rollback online limpo (RECOVERY_OK_ONLINE). Isso e' ESTABILIDADE.
O QUE ELA NAO PROVOU, contra o que o 265 afirmou:
(1) ZERO chamadas a Bc250DxgkDdiPresent na corrida inteira -- nao ha UMA linha [Present #N] em
5,4 MB de log. A ausencia e' CONCLUSIVA: [Present #...] nao cai em classe de amostragem
(bc250_display_ddi.c, ramo "else return TRUE"), FrameBufferVirtual e' nao-NULL
([DisplayInit] Virt=FFFFBAF97B800000) e o ramo de recusa contem REJECT, isento de amostragem.
O caminho de transferencia para o scanout GOP e' CODIGO MORTO nesta corrida. O 265 propoe
consertar codigo que nunca rodou.
(2) NAO HA QUADRO COMPOSTO. 1787 de 1787 COMPOSITE_BLT terminam em Prova=ZERO_INCONCLUSIVO,
precedidos por "COMPOSITE_DATA_PROOF INCONCLUSIVO_ZERO: zero contra zero NAO prova pixel". Os
5.165 escapes moveram ZEROS entre buffers. Contar operacao como progresso e' a QUINTA ocorrencia
da classe "o gate carimbava o falso sucesso" (A15, mcdm_callbacks_test, C1, sdma_blt_executed).
(3) NENHUM destino foi o scanout: os sete DstMc distintos (0xF40208C000, 0xF400B8C000, ...) sao
backbuffers do DWM; o scanout e' 0xF400000000.
(4) O dwm.exe esta em LACO DE DEVICE-LOST, 90 vezes: Present -> destroi todo recurso ->
DestroyDevice -> CloseAdapter -> recarrega a DLL -> CreateDevice, a ~1 Hz. O Present FALHA. E e'
Flags=0x00000002 = FLIP (dxgiddi.h:71), nao Blt.
(5) NAO HA RASTERIZACAO, e o proprio log declara: "SHADER_TRANSLATOR: TokenStream -> SPIR-V ...
Traducao NAO e' pipeline: descriptors, render target, anel grafico e Draw seguem ausentes."
CAUSA RAIZ DO PRESENT FALHAR (H de alta confianca -- codigo e log concordam; NAO e' P0 porque o
hr do pfnPresentCb nunca foi registrado): a UMD e o KMD discordam do NOSSO PROPRIO contrato de
alocacao. Bc250WinsysAllocate zerava D3DDDICB_ALLOCATE e NUNCA atribuia hResource
(d3dumddi.h:3457), Flags.Primary nem VidPnSourceId (d3dukmdt.h:554 e :549). O dxgkrnl so' liga
DXGKARG_CREATEALLOCATION.Flags.Resource quando a UMD passa hResource, e o ramo do KMD que marca
primaria (bc250_active_miniport.c) esta atras justamente desse bit -- logo era INALCANCAVEL para
toda alocacao nossa. A primariedade existia SO' no nosso token privado, que o KMD nem consultava.
Confirmado no log: todo [CreateAllocation] ENTER de origem UMD traz Resource=0 ->
"Non-resource allocation"; ha UM unico "Primary token confirmed" na corrida e e' o Resource=1 do
bind (handle 0000000800000008, o mesmo do CommitVidPn/SetVidPnSourceAddress 0xAAC000).
FAIXA APLICADA (offline), quatro frentes:
(A) VINCULO DE RECURSO. Bc250WinsysAllocateForResource nova com BC250_WINSYS_RESOURCE_BINDING
(RuntimeResource/VidPnSourceId/Primary); Bc250Umd10CreateResource passa hRTResource.handle.
FONTE UNICA de primariedade: a classe do token manda, e vinculo que DISCORDE dela falha fechado
-- sem isso voltariamos a ter duas verdades, que e' a classe de defeito que a faixa corrige.
Primaria SEM hRTResource e' RECUSADA antes do callback: suceder calado ali produziria de novo
uma "primaria" que o kernel nunca veria como tal. Desalocacao ficou SIMETRICA (criada com
recurso, devolvida com recurso, d3dumddi.h:4068), senao o objeto de recurso do kernel fica orfao.
(B) I1 -- Bc250DxgiPresent registra o hr do pfnPresentCb. Era `return callback(...)` direto: o
numero que decidia a corrida nunca foi gravado.
(C) I2 -- Bc250DxgkDdiPresent tinha TRES saidas mudas (hContext NULL, pPresent NULL,
FrameBufferVirtual NULL). Entrada agora e' INCONDICIONAL e anterior a toda guarda, com contador
proprio (8 primeiras + 1 em 256); a ausencia da linha passa a ser conclusiva.
(D) I3 -- [McdmQAI], [McdmCreateDevice] e [McdmCreateContext] dividiam UM contador de amostragem.
A rajada de QAI no bind (17 linhas medidas) esgota as 16 primeiras da classe, e por isso as 90
criacoes de device do dwm.exe nao deixaram UMA linha. Contadores independentes (12, 13, 14).
(E) No KMD, a primaria de UMD NAO pode mais sobrescrever g_Bc250DisplayState.hPrimaryAllocation
-- ate esta faixa o ramo era inalcancavel e o unico escritor era a primaria do GOP; agora as do
dwm.exe tambem chegam la. Preenche so' se vazio; as demais sao contadas e nomeadas.
EFEITO COLATERAL DECLARADO: com Flags.Resource passando a 1 para recursos de render, o ramo
`else if (Flags.Resource == 0u && info->FlagsWddm2.CpuVisible)` deixa de aplicar-se a eles, e
CpuVisible fica FALSE -- o que REFORCA a correcao do 0x10E em vez de afrouxa-la. ResourceMap usa
shadow de CPU (calloc), nao pfnLockCb, entao nao ha regressao de mapeamento.
Testes: bloco de primaria do bc250_wddm_winsys_test reescrito em torno dos tres campos exigidos
um a um, mais tres casos de fail-closed e um CONTRAPONTO (command buffer/scratch sem recurso
continuam funcionando -- sem ele a correcao viraria peneira). build-umd EXIT=0, build-contract
EXIT=0, tres auditorias PASS, dois portoes novos
(audit_primary_allocation_is_declared_to_the_kernel e audit_present_path_is_not_mute).
NOVE MUTACOES VERIFICADAS: tirar hResource reprova; zerar Flags.Primary reprova; desalocacao
assimetrica reprova; afrouxar a recusa de primaria sem recurso reprova; reverter a divisao de
amostragem reprova; remutar o DdiPresent reprova; voltar ao return direto no Present reprova;
CreateResource sem vinculo reprova; e o KMD sobrescrever a primaria de display reprova.
ARMADILHA REGISTRADA: a primeira passada da mutacao 4 reprovou pelo assert ERRADO -- a allowlist
de token do stub disparava antes do meu invariante. Falha nao-atribuivel e' portao fraco; o stub
foi corrigido para que quem reprove seja "AllocateCalls nao mudou", que nomeia a causa.
ESTA FAIXA NAO ACENDE A TELA, E ISSO E' ESPERADO. Ela remove a parede de APRESENTACAO. A parede
de RASTERIZACAO (sem pipeline grafico, sem descriptors, sem render target, sem Draw PM4) continua
inteira, e enquanto ela existir nenhum pixel e' produzido por ninguem. TELA PRETA segue sendo o
resultado CORRETO; qualquer imagem deve ser atribuida ao rollback/BasicDisplay ate prova em
contrario.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES -- tres desfechos, todos distinguiveis:
  1. "[CreateAllocation] Primary token confirmed (UMD #1)" aparece E "[DdiPresent] ENTER #1"
     aparece -> a cadeia de apresentacao fechou; o laco de device-lost do dwm.exe tem de parar
     e "DXGI Present LEAVE cb: hr=0x00000000" confirma pelo lado da UMD.
  2. "DXGI Present LEAVE cb: hr=" com hr != 0 e nenhum [DdiPresent] ENTER -> o dxgkrnl ainda
     recusa, e AGORA o hr nomeia o motivo; a hipotese da primaria cai e o proximo suspeito passa
     a ser o no do contexto (Winsys.KernelContext e' de COMPUTE, node 1).
  3. Nenhuma das duas -> a coleta falhou; defeito de binder, nao de driver.
INFORMACAO NOVA EM QUALQUER DESFECHO, o que nao era verdade na corrida 16:28.
PACOTE LAB 3.173.1.0 assinado, auditado e NAO INSTALADO. BuildKind=MCDM_LAB_BRINGUP, perfil
DISPLAY_RENDER_LAB (RenderNode=1, RenderOnlyLab=0), contrato WDDM 2.7, modo P1 PERSISTENT
SUBMISSION, schema 135. SYS fonte 529B645B..., SYS assinado 8D07EBB4..., UMD assinada AEB6A452...,
INF AE3E88EB..., CAT 9480733D... Lab package audit PASS.
Os binarios foram CONFERIDOS POR STRING (o empacotador nao compila, so' copia): no SYS assinado
estao "[DdiPresent] ENTER #", "RECUSADO: FrameBufferVirtual NULL" e "Primary token confirmed (UMD";
na UMD assinada estao "DXGI Present ENTER cb:", "DXGI Present LEAVE cb: hr=", "Allocate ENTER:
Token=" e "primaria sem hRTResource".
CORRECAO DE REGISTRO: o docs/265 chama a corrida 16:28:27 de "pacote 3.172.0.0". E' FALSO -- o
INF daquele output_lab diz DriverVer=3.170.1.0 e o SYS instalado era 59E08349..., o mesmo que
tools/_last_lab_binding.json registra. O DriverVer fonte foi para 3.173.0.0 (candidato 3.173.1.0)
para nao haver duvida de identidade.
ESTADO DA MAQUINA NA HORA DO EMPACOTAMENTO: BasicDisplay em Codigo 0, fusivel INEXISTENTE (proximo
boot assume CLEAN) e SEM marcador C:\BC250_REQUIRE_CLEAN_BOOT.json. Ainda assim o BOOT FRIO E'
OBRIGATORIO: o rollback de 16:28 foi ONLINE, sem reboot, entao o estado de engine da corrida
anterior continua vivo e nao cai com desbind.
=============================================================================================
CORRIDA 13/09 17:18:35 (P0, lab 3.173.1.0) -- A FAIXA DA PRIMARIA FUNCIONOU, A HIPOTESE NAO
FECHOU, E O LOG PASSOU A RESPONDER. Desfecho 2 do criterio escrito antes da corrida.
ESTABILIDADE, sem regressao: zero TDR_QUIESCE, zero BugCheck, zero IB_TRIPOD TIMEOUT, zero
DEVICE_REMOVED, zero StopDevice, zero GFXHUB_FAULT. Rollback online limpo (RECOVERY_OK_ONLINE,
BasicDisplay Codigo 0 em 5 amostras).
O QUE A CORRECAO (1) CONSEGUIU, P0: (a) CreateAllocation de origem UMD passou de Resource=0
("Non-resource allocation") para 7/7 Resource=1; (b) na UMD, SrcPrimary=1 SrcDeclaredPrimary=1
SrcRTResource=0000000006010000; (c) SetVidPnSourceAddress saiu de 0xAAC000/hAlloc=0x08 (primaria
do GOP do boot) para Addr=0x4B5C000/hAlloc=0xA8, uma alocacao criada pela UMD -- o dxgkrnl so'
aponta o source address para alocacao que ele aceita como primaria; (d) Bc250DxgkDdiPresent foi
ALCANCADA pela PRIMEIRA VEZ na historia do projeto: 6 ENTER, contra zero em todas as corridas
anteriores. Os 6 fecharam ate o silicio -- origem resolvida (SegOff=0x4B60000 -> VramOff=
0x55EC000), blt de 11.059.200 bytes, SDMA=YES, DMA buffer de 4 bytes submetido e executado
(IB EXECUTADO ... FenceId=833).
TELA PRETA PELO MOTIVO PREVISTO: 6 de 6 com "AVISO: Superficie de origem esta com todos os
pixels em zero (NonZero=0/256)" e BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO). A parede de
rasterizacao segue inteira; nao havia pixel para apresentar.
A HIPOTESE DA PRIMARIA ESTA REFUTADA COMO CAUSA SUFICIENTE, P0: 84 de 84 pfnPresentCb
devolveram hr=0x80004005 (E_FAIL) no MESMO milissegundo da chamada -- rejeicao sincrona de
validacao, nao falha de submissao -- com a primaria ja corretamente declarada. O laco de
device-lost do dwm.exe continua: 61 CreateDevice/DestroyDevice. O I1 pagou: o numero existe.
CORRECAO DO MEU PROPRIO RACIOCINIO: eu escrevera que o proximo suspeito seria "o no do contexto
(COMPUTE, node 1)". ERRADO como formulado -- o GetNodeMetadata da propria corrida diz Node 0 =
EngineType 6 (COPY) e Node 1 = EngineType 1 (3D). Node 1 NAO e' um no de compute para o
dxgkrnl. O que SOBREVIVE e' mais estreito e e' medicao, decodificando DXGK_CREATECONTEXTFLAGS
(d3dkmddi.h:1518, P1): os dois unicos contextos pelos quais o dxgkrnl apresentou estao no
Node 0 -- Ctx 0x04 Flags=0x05 (SystemContext+VirtualAddressing) e Ctx 0x07 Flags=0x06
(GdiContext+VirtualAddressing), que fez os 6 --, enquanto TODO contexto da nossa UMD e' Node 1
Flags=0x04 (VirtualAddressing apenas) e NUNCA apresentou. NAO NOMEIO a causa do E_FAIL:
0x80004005 e' generico e a corrida nao o determinou. E' H.
PONTO CEGO MEU, DECLARADO: "Primary token confirmed (UMD" saiu 0 vezes e isso NAO e'
conclusivo. A classe 9 do amostrador ([CreateAllocation] + [ActiveOpenAlloc] +
[McdmCreateAllocation] + [McdmCloseAllocation]) somou 45 linhas contra o orcamento de 16
primeiras. E' o MESMO defeito de contador compartilhado que eu corrigi na classe 12 e nao
corrigi na classe 9 -- meu proprio I3 foi incompleto, e o buraco caiu em cima da medicao que eu
queria.
FAIXA APLICADA (offline), duas frentes:
(A) I3 COMPLETADO -- as quatro tags da classe 9 ganharam contadores proprios (9, 15, 16 e 17).
A ausencia de "Primary token confirmed (UMD" passa a ser conclusiva.
(B) CONTEXTO DE APRESENTACAO DEDICADO. Bc250WinsysCreatePresentContext/DestroyPresentContext
novas; o Present passa a ir por Winsys.PresentContext, criado no Node 0, enquanto o contexto de
trabalho continua no Node 1. SystemContext/GdiContext sao decididos no kernel e nao estao em
D3DDDI_CREATECONTEXTFLAGS (d3dukmdt.h:791), entao NodeOrdinal e' a unica variavel sob nosso
controle -- e o ensaio move exatamente essa uma. SEM FALLBACK para o contexto de trabalho: a
ausencia do contexto de apresentacao RECUSA com motivo nomeado, porque cair de volta
reproduziria o estado ja medido falhando 84/84 e misturaria as duas variaveis. O teardown
destroi os DOIS -- o dwm.exe fez 61 ciclos de device em ~3 min e contexto nao destruido vaza um
por ciclo.
Testes: bloco novo de coexistencia no bc250_wddm_winsys_test (handles distintos por no, contexto
de trabalho intocado, idempotencia fail-closed, destruicao pelo proprio handle), stubs de
umd_node1_submit_contract_test e umd_node1_tripod_contract_test atualizados para os dois
contextos com invariante "2 criados, 2 destruidos". build-umd EXIT=0, build-contract EXIT=0,
tres auditorias PASS, portoes estendidos em audit_present_path_is_not_mute.
SEIS MUTACOES VERIFICADAS (10..15): reverter a divisao da classe 9 reprova; o Present voltar ao
contexto de trabalho reprova; remover a recusa nomeada reprova; a criacao do contexto de
apresentacao escrever no de trabalho reprova; o teardown perder a destruicao reprova; e o STUB
devolver o mesmo handle para os dois nos TAMBEM reprova -- esta ultima existe porque "o gate
congelava a mentira" ja apareceu cinco vezes no projeto.
ARMADILHA REGISTRADA: a primeira versao do teste criava o contexto de apresentacao em
TestLifecycle e o destruia em TestResidencyAndPrimaryClass -- funcoes diferentes. A suite pegou.
Tres assercoes de contagem que congelavam "1 contexto" tiveram de virar "2 criados, 2
destruidos", e um stub que exigia o handle 0xC001 constante passou a comparar com a FONTE
(context.KernelContext), para continuar valendo se o no do contexto de trabalho mudar.
PACOTE LAB 3.174.1.0 assinado, auditado e NAO INSTALADO. BuildKind=MCDM_LAB_BRINGUP, perfil
DISPLAY_RENDER_LAB (RenderNode=1, RenderOnlyLab=0), WDDM 2.7, P1 PERSISTENT SUBMISSION,
schema 135. SYS fonte 638D83F2..., SYS assinado 957A3873..., UMD assinada 90F90E2A...,
INF B64F0320..., CAT 8F34DF19... Lab package audit PASS. Binarios CONFERIDOS POR STRING (o
empacotador nao compila, so' copia): no SYS "[DdiPresent] ENTER #", "Primary token confirmed
(UMD", "[McdmCloseAllocation]" e "[ActiveOpenAlloc]"; na UMD "CreatePresentContext ENTER",
"DestroyPresentContext ENTER", "PRESENT_CONTEXT: Node=", "PresentNode=" e "DXGI Present
RECUSADO: contexto de apresentacao ausente".
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES -- quatro desfechos, todos distinguiveis:
  1. "DXGI Present LEAVE cb: hr=0x00000000" aparece -> o no do contexto ERA a causa; o laco de
     device-lost do dwm.exe tem de parar e [DdiPresent] ENTER cresce muito alem de 6.
  2. "DXGI Present LEAVE cb: hr=0x80004005" com "PresentNode=0" na linha ENTER -> apresentamos
     pelo Node 0 e ainda assim falhou. O contexto MORRE como suspeito, e o proximo passa a ser
     a forma da cadeia de flip (hDstAllocation=0, buffers do swapchain criados um a um, sem
     registro de cadeia).
  3. "DXGI Present RECUSADO: contexto de apresentacao ausente" -> a criacao no Node 0 falhou; a
     linha "PRESENT_CONTEXT: criacao FALHOU" diz o status e o ensaio nao chegou a acontecer.
  4. "Primary token confirmed (UMD #N)" aparece -> fecha, por medicao direta, o que a corrida
     17:18 so' pode sustentar por indicio (o SetVidPnSourceAddress ter ido para hAlloc de UMD).
NAO ESPERAR DESKTOP: a parede de rasterizacao continua inteira e nenhum pixel e' produzido por
ninguem. TELA PRETA segue sendo o resultado CORRETO; qualquer imagem deve ser atribuida ao
rollback/BasicDisplay ate prova em contrario.
ESTADO DA MAQUINA NO EMPACOTAMENTO: BasicDisplay Codigo 0, fusivel INEXISTENTE (proximo boot
assume CLEAN), sem marcador C:\BC250_REQUIRE_CLEAN_BOOT.json. BOOT FRIO CONTINUA OBRIGATORIO: o
rollback de 17:18 foi ONLINE, sem reboot, entao o estado de engine da corrida anterior segue
vivo e nao cai com desbind.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
```

> ⚠️ **Aviso histórico.** Até 2026-09-02 este arquivo trazia uma barra com
> *"Miniport WDDM Display+Render 100%"* e veredito *"COMPROVADOS NO SILÍCIO (CÓDIGO 0)"*.
> **Era falso.** Não reintroduza.

---

## ⚖️ 3. Leis inegociáveis

1. **Sinceridade absoluta.** Medição, não expectativa. Prova ausente → escreva
   **`NÃO VALIDADO EM HARDWARE`**. Hipótese nunca vira causa.
2. **Tripé no silício:** `RPTR == WPTR` + fence física gravada **pela GPU** em VRAM com
   poison check + dados conferidos **byte a byte**.
3. **Todo item fecha com teste nomeado.** Diga qual teste prova execução no hardware e onde
   a evidência será arquivada. A Tabela 2 da `BASE_DE_AVANCO.md` já traz a prova exigida.
4. **Zero fakes:** nunca fence pela CPU; nunca loop de delay; nunca `STATUS_SUCCESS` sem
   backend; nunca WARP/GDI/llvmpipe; nunca telemetria fixa ou estimada.
5. **Não é prova:** compilar, assinar, enumerar API, carregar DLL, criar contexto/pipeline,
   abrir swapchain, `vulkaninfo`, `dxdiag`, `glGetString`.
6. **Ciclo transacional fail-closed:**
   `PREFLIGHT → SNAPSHOT → APPLY → READBACK → PROVA FUNCIONAL → ROLLBACK/TEARDOWN`
7. **Máscara SOC15:** `uint32_t val = (lido & ~mask) | (or_value & mask);`
8. **Endereços com proveniência.** No DCN o offset é `DMU_BASE.seg[BASE_IDX] + reg`, com
   `DMU_BASE = {0x12, 0xC0, 0x34C0, 0x9000}` — **não existe base plana**. Nada em quarentena.
9. **Vocabulário de confiança:** P0 / P1 / P2 / H / Q — ver [`rules.md`](rules.md).
10. **Economia de corridas.** Corrida física é o recurso escasso e é o **Daniel** quem
    executa. Leitura de fonte e mineração de evidência primeiro; teto e critério de
    abandono antes de armar; boot frio entre binds.
11. **Como se escreve o critério de uma corrida.** Três regras, nascidas de falhas medidas
    na Corrida 39 (19/09), não de estilo:
    (a) **assinatura e implicação em campos separados** — o que o log vai mostrar, e à parte,
    para onde isso leva; acoplados, a assinatura de um desfecho foi falsificada enquanto a
    conclusão dele se confirmou, e não havia como aceitar uma sem a outra;
    (b) **todo critério leva um desfecho final `E — algo fora do mapa`**, descrito antes de
    julgado; na Corrida 39 o maior resultado (118 submissões gráficas sem hang) caiu fora das
    quatro letras porque a convergência era tratada só como controle a não regredir;
    (c) **desfecho parcial não é desfecho** — dois de três sinais não fecham nada, ainda mais
    quando falta justamente o que define o desfecho.

### Caminhos encerrados — não reabrir
Render-only sob `Class=Display` (reprovado, `docs/162`) · `MiscCaps.ComputeOnly`
(inalcançável) · cross-adapter/MCDM como destino (APU de die único).

---

## 🔗 4. Navegação

Use sempre links Markdown ao referenciar arquivos.

| O quê | Onde |
|---|---|
| **Base de avanço** | [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md) |
| **Roadmap** | [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md) |
| Regras normativas | [`rules.md`](rules.md) |
| Catálogo de registradores | [`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md) |
| Provas físicas P1 / P2 | `docs/148_...` · `docs/159_...` |
| Ownership de display | `docs/26_...` · `docs/17_...` |
| Encerramento do render-only | `docs/162_...` |
| Corpus de evidência | `docs/evidence/` |
| Miniport WDDM | [`driver/wdk/`](driver/wdk/) |
| UMD D3D / Vulkan | [`driver/umd/`](driver/umd/) |
| Driver de laboratório | [`dbg/`](dbg/) |
| ABI de diagnóstico | [`inc/bc250dbg_abi.h`](inc/bc250dbg_abi.h) |
| Harness e auditorias | [`tools/`](tools/) |
| Recuperação | [`RECOVERY_BC250V4.md`](RECOVERY_BC250V4.md) · [`tools/BC250_RECOVERY.cmd`](tools/BC250_RECOVERY.cmd) |
| Arquivo morto (nada apagado) | [`DocsObsoletos/INDICE.md`](DocsObsoletos/INDICE.md) |

---

## 🔄 5. Manutenção

`CLAUDE.md`, `AGENTS.md`, `GEMINI.md` e `.cursorrules` compartilham o mesmo núcleo
normativo. **Ao mudar uma lei ou a regra de relato de corridas, atualize os quatro.**

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
