PROMPT DE HANDOFF — BC-250 WDDM — 19/09/2026, apos a Corrida 40
================================================================

Voce vai assumir o desenvolvimento do driver WDDM da AMD BC-250 neste
repositorio. Este documento e' o seu ponto de partida. Leia-o inteiro antes
de tocar em qualquer arquivo.

Voce NAO executa hardware. Quem executa toda corrida fisica e' o Daniel, o
dono do projeto. Voce compila, le codigo, minera evidencia, empacota e
escreve criterios. Ele roda e volta com os logs.


================================================================
1. O QUE LER, NESTA ORDEM, ANTES DE ESCREVER CODIGO
================================================================

Obrigatorio:

  CLAUDE.md
      As leis inegociaveis do projeto. Leia as dez. Em especial:
      Lei 1 (sinceridade absoluta), Lei 2 (tripe de validacao no silicio),
      Lei 4 (zero fakes, zero fallback de software), Lei 8 (vocabulario de
      confianca P0/P1/P2/H/Q), Lei 9 (economia de corridas fisicas) e
      Lei 10 (como se escreve o criterio de uma corrida).
      O bloco historico gigante dentro do CLAUDE.md NAO e' modelo de
      resposta. E' registro. Nao o reproduza.

  BASE_DE_AVANCO.md
      Tabela 1 = o que ja existe com prova fisica.
      Tabela 2 = o que falta, em ordem de prioridade, com o teste que fecha
      cada item. E' a fonte unica de verdade do estado do projeto.

  docs/294_CRITERIO_CORRIDA_40_TEXTURA_PELO_UPDATE.md
      O criterio da ultima corrida, e o primeiro escrito sob a Lei 10.
      Use-o como modelo de formato para o proximo criterio que voce escrever.

  docs/03_CATALOGO_DE_REGISTRADORES.md
      Consulte ANTES de escalar qualquer duvida sobre registrador. Ja
      aconteceu tres vezes de a resposta estar aqui e alguem "pesquisar
      fora". Lei 7: endereco sem procedencia catalogada nao entra no codigo.

Recomendado:

  rules.md
  docs/164_ROADMAP_WDDM_JOGOS.md
  docs/291_PROMPT_HANDOFF_DESCRIPTOR_SET_E_SONDA.md  (o handoff anterior)
  docs/293_CRITERIO_CORRIDA_39_TEXTURA_E_RECUSA_NOMEADA.md


================================================================
2. ONDE ESTAMOS HOJE (medido, nao suposto)
================================================================

A Corrida 40 (19/09/2026 17:38:38, pacote lab 3.198.1.0) foi o maior avanco
funcional da sequencia. Evidencia em:

  docs/evidence/2026-09-19_17-38-38-DESKTOP-HQAG6T4-lab-binding/

O que FOI PROVADO NO SILICIO (P0) nessa corrida:

  - 6 texturas do dwm.exe foram copiadas para VkImage
    ("textura sincronizada com VkImage"), contra ZERO na corrida anterior.
    A maior foi 2560x1080, 11.059.200 bytes.

  - 2 descriptor sets completos foram vinculados:
    "[VK_DESCRIPTOR] BOUND: bindings=3 (CB=1 SRV=1 SAMPLER=1)".
    Primeira vez na historia do projeto. Antes disso, zero, sempre.

  - 1 draw grafico executou com a fence concluida:
    "ExecuteVkDraw: DRAW fence concluida (VkDraws=1, Verts=42, Inst=1,
    Indexed=1)". Tambem primeira vez.

  - 137 submissoes no anel GFX, ZERO TDR, ZERO BugCheck, ZERO GFXHUB_FAULT,
    ZERO NAO_CONVERGIU, rollback limpo (RECOVERY_OK_ONLINE).

  - A violacao de memoria da Corrida 37 (FATAL_HALT + XNACK_ERROR + EXCP
    bit 8) NAO reapareceu. A sonda de onda estava ARMADA e nao teve o que
    registrar.

O que NAO foi provado, e voce nao pode afirmar:

  - Nada disso acende a tela. TELA PRETA E' O RESULTADO CORRETO e esperado.
  - Os texels NAO foram conferidos. O proprio log diz "pixels NAO
    CONFERIDOS". Nao existe leitura de volta.
  - Nao esta provado que a SRV vinculada e' uma das 6 texturas
    sincronizadas. A linha BOUND nao imprime o handle do recurso. A
    proximidade temporal e' indicio (P2), nao medicao.

A PAREDE ATUAL, com nome:

  O dwm.exe criou 14 VkImages e preencheu 6. O segundo pipeline dele
  (slot 1) foi recusado duas vezes com:

    "[VK_DESCRIPTOR] RECUSADO: binding index=2
     motivo=SRV_LAYOUT_UNDEFINED_NUNCA_TRANSICIONADA"

  Ou seja: o recurso TEM VkImage, mas nunca foi preenchido nem usado como
  render target. Existem duas causas possiveis, e AS DUAS sao compativeis
  com tudo o que foi medido:

    (a) o DWM ainda vai escrever nela mais adiante no quadro, e o problema
        e' ordem — NAO e' defeito nosso; ou
    (b) o DWM acha que ja a preencheu por um caminho que nos ignoramos.

  NAO ESCOLHA UMA SEM MEDIR. Escrever codigo em cima de (b) sem prova seria
  tratar hipotese como causa, e isso e' violacao da Lei 1.


================================================================
3. O QUE VOCE TEM QUE FAZER
================================================================

Sao quatro frentes. As tres primeiras sao OFFLINE (sem corrida fisica) e
devem ir juntas numa unica faixa, seguidas de um empacotamento e de um
criterio escrito antes do ensaio. A quarta pode ir junto ou depois.


----------------------------------------------------------------
P1 — CONSERTAR A COLETA DE EVIDENCIA E O LOG DUPLICADO
----------------------------------------------------------------

Sao dois defeitos distintos. Os dois envenenam qualquer corrida futura, e
por isso vem primeiro.

P1.a — A COLETA FINAL FECHA CEDO DEMAIS.

  Defeito localizado, com linha: tools/bind-bc250mcdm-lab.ps1, por volta da
  linha 2004 e 2020.

    if (-not $RunVulkanCube -and ... -and -not $RunD3D11Native) {
        $collectDelay = [Math]::Max(30, $WatchdogTimeoutSeconds - 90)
        Start-Sleep -Seconds $collectDelay
    } else {
        # "Para ensaios de harness, a execucao ja ocorreu; pausa breve"
        Start-Sleep -Seconds 3
    }

  A premissa do ramo "else" e' que o harness ocupa a janela inteira. Na
  Corrida 40 o harness de conformidade morreu em 16 segundos
  (D3D11CreateDeviceAndSwapChain falhou com 0x887A0004), e a coleta FINAL
  disparou as 17:39:00, apenas 22 s depois do bind, enquanto o watchdog so'
  fechou as 17:40:44.

  Consequencia medida: os TRES crashes do dwm.exe (17:39:01, 17:39:05 e
  17:39:08, e0464645 em KERNELBASE+0x12d862) aconteceram DEPOIS da coleta e
  nao deixaram uma linha de log de UMD. E o log do KMD ficou 154.648 bytes
  curto — eu resgatei esse trecho a mao para
  display-log-this-bind-POS-FINAL.txt (sha256 CBCF634DF465D991...), e ele
  continha mais 29 submissoes GFX, todas sem TDR.

  Esta e' a QUINTA vez no projeto que a coleta custa o dado decisivo (ver
  o bloco historico do CLAUDE.md: corridas de 13/09 08:12, 11:25, 11:32 e
  11:55). O comentario dentro do proprio arquivo, na linha ~1917, descreve
  essa mesma classe de falha. Leia-o.

  O que fazer: a janela fisica tem de terminar pelo RELOGIO, nao pela saida
  do harness. Se o harness morrer cedo, o binder deve continuar observando
  ate a janela fechar. Escreva um portao de auditoria que impeca a regressao.

P1.b — O LOG DA UMD SAI DUPLICADO EM ALGUNS PROCESSOS.

  Medicao (P0): no arquivo bc250_umd_log_1204-FINAL.txt (dwm.exe) sao 7408
  linhas, das quais 3705 sao identicas a linha imediatamente anterior —
  mesmo timestamp ao milissegundo, mesmos ponteiros. Ou seja, cada linha
  aparece duas vezes. O mesmo ocorre em pid=2276 (dxgiadaptercache.exe).
  NAO ocorre em pid=5256 (Explorer.EXE) nem em pid=9376 (o harness).

  Eu testei a hipotese "a DLL foi carregada duas vezes no processo" e ela
  FALHA: o pid 9376 carregou duas vezes e nao duplica. NAO HA CAUSA
  IDENTIFICADA. Trate isso como investigacao aberta, classifique como P0 no
  efeito e nao invente explicacao.

  Ponto de partida: Bc250WinsysTraceRaw em driver/umd/bc250_wddm_winsys.c
  linha 20. E' o sink unico; Bc250UmdTrace (bc250_d3d_umd.c:27) delega a
  ele. Note que ele chama OutputDebugStringA ANTES de escrever no arquivo.

  Isso importa porque as minhas primeiras contagens da Corrida 40 sairam
  dobradas. Toda contagem futura sai errada por dois enquanto isso existir.


----------------------------------------------------------------
P2 — A RECUSA TEM QUE NOMEAR O RECURSO, NAO SO' A CONDICAO
----------------------------------------------------------------

Sitio: driver/umd/bc250_umd_descriptors.c, linha 206.

  Bc250UmdTrace("[VK_DESCRIPTOR] RECUSADO: binding index=%u motivo=%s ...")

A faixa anterior dividiu um motivo agregado em sete nomes, e isso funcionou:
a Corrida 40 disse exatamente qual condicao disparou. Mas a linha nao diz
DE QUAL RECURSO se trata, e por isso nao da para distinguir as causas (a) e
(b) da secao 2.

O que a linha precisa passar a imprimir, para o caso de SRV:

  - o ponteiro do BC250_UMD_RESOURCE
  - largura x altura e formato
  - BindFlags / Usage declarados na criacao
  - quantas vezes aquele recurso recebeu ResourceUpdateSubresourceUP ou
    ResourceUnmap (um contador por recurso, incrementado no helper
    Bc250UmdSyncTextureToVkImage, em bc250_d3d_umd.c:1237)
  - o VkLayout atual, por nome legivel e nao por numero

Com isso, a proxima corrida separa "o DWM ainda nao escreveu" de "o DWM
escreveu por um caminho que ignoramos", que hoje sao indistinguiveis.

Dado que ja esta medido e voce pode usar: no dwm.exe da Corrida 40
apareceram SOMENTE quatro DDIs de escrita em recurso —
ResourceUpdateSubresourceUP, ResourceUnmap, ResourceMap e
DefaultConstantBufferUpdateSubresourceUP. NAO houve ResourceCopyRegion.
Entao nao e' um quinto DDI que estamos ignorando.


----------------------------------------------------------------
P3 — ATRIBUIR O FAIL-FAST DO dwm.exe
----------------------------------------------------------------

O dwm.exe morre com codigo de excecao 0xe0464645 (RtlFailFast) no modulo
KERNELBASE.dll, offset 0x12d862. Essa assinatura e' IDENTICA em todas as
corridas desde 08/09/2026. Nunca foi atribuida a uma causa. Sao onze dias
de divida.

Na Corrida 40 o primeiro crash foi ~1 segundo depois da recusa da SRV.
Isso e' CORRELACAO, nao causa, e voce nao pode escrever que uma coisa
causou a outra.

Nao existe dump WER do dwm.exe em disco. A forma de obter um e' habilitar
LocalDumps para dwm.exe no registro antes da corrida. Isso nao custa
corrida fisica nenhuma — e' uma chave de registro que o binder pode armar
e desarmar, com a mesma disciplina fail-closed dos outros tunables (default
desarmado, armar e' decisao do dono do projeto, e o binder avisa alto).

Com a pilha do dump saberemos se o DWM morre POR CAUSA da nossa recusa ou
por algo anterior e independente.


----------------------------------------------------------------
P4 — GfxIbInspect DA VEREDITO SOBRE IB QUE NAO LEU INTEIRO
----------------------------------------------------------------

Medido na Corrida 40:

  [GfxIbInspect] VEREDITO: INDETERMINADO -- varredura incompleta
  (4096 de 20208 DW, abortada=0, dwords lidos na cabeca=8)

O inspetor esta limitado a 4096 DW e os IBs que importam agora tem 20.208.
O veredito IB_LINEAR_CONFIRMADO so' vale para os IBs pequenos — justamente
os que ja nao sao o problema.

Isto NAO bloqueia pixel e por isso vem por ultimo. Mas o veredito atual e'
honesto so' porque diz INDETERMINADO; se alguem afrouxar isso para
"confirmado", vira falso sucesso. Nao afrouxe.

Ha tambem tools/decode-gfx-ib.py com 19 assercoes offline, e os despejos
brutos ficam em docs/evidence/<corrida>/gfx-ib-dump/. Use-os.


================================================================
4. ONDE A GENTE QUER CHEGAR — ALEM DO P4
================================================================

O objetivo final, que nao muda e nao se negocia:

  RODAR JOGOS no Windows 10 22H2 na BC-250, usando as 24 CUs e os 16 GB de
  GDDR6, com o monitor ligado ao DisplayPort da propria placa, via
  D3D12/Vulkan, SEM WARP e SEM FALLBACK DE SOFTWARE.

Criterio final, textual: jogo real na tela ligada a placa, sem WARP, com
TDR recuperando de hang sem perder o display.

O caminho, na ordem em que as paredes caem:

  1. [FEITO, P0] Anel GFX aceita submissao e a fence volta.
  2. [FEITO, P0] Textura chega a VkImage.
  3. [FEITO, P0] Descriptor set vincula.
  4. [FEITO, P0] Um draw grafico executa com fence concluida.
  5. [AGORA] O dwm.exe completa UM QUADRO inteiro sem recusa e sem morrer.
     E' aqui que P1, P2 e P3 atacam.
  6. Pixel CONFERIDO. Hoje nao existe leitura de volta; nenhuma imagem
     produzida por nos foi verificada byte a byte (Lei 2, perna 3). Sem
     isso, "desktop na tela" nao e' afirmavel — ja foi afirmado errado
     quatro vezes na historia deste projeto e retratado depois.
  7. Desktop acelerado estavel, com TDR recuperando sem perder o display
     (item 25, ainda ABERTO na perna "display visivel sobrevive").
  8. Aplicativo D3D11 nao modificado rodando.
  9. Jogo real rodando.

Fora do caminho critico, e por decisao do dono do projeto:

  - Display v1 e' por HERANCA DO UEFI GOP. Escrita em registrador de DCN e'
    PROIBIDA fora da prioridade P9. Leitura e' permitida.
  - LLM, llama.cpp, DirectML e tokens/s sao subproduto. Nao sao marco, nao
    entram em PASS de etapa e nao aparecem em veredito.
  - Compute isolado NAO e' entregavel. Vale como pre-requisito de jogos.
  - E' PROIBIDO propor compute ou LLM como "caminho mais rapido" quando o
    item de video estiver bloqueado. Item travado se resolve, se declara
    travado, ou se declara NAO VALIDADO EM HARDWARE. Trocar a meta por uma
    mais facil e' fuga de escopo.
  - Demorar e' aceito. Custo em tempo nao e' argumento para trocar o
    objetivo.


================================================================
5. COMO SE TRABALHA AQUI (nao e' opcional)
================================================================

5.1 TODA MUDANCA VEM COM PORTAO E COM MUTACAO.

  As auditorias vivem em tools/audit-bc250mcdm-safety.py. Ao corrigir um
  defeito, escreva um portao que o impeca de voltar — e depois PROVE que o
  portao funciona, reintroduzindo o defeito de proposito e verificando que
  a auditoria REPROVA. Isso se chama mutacao. Sem mutacao, o portao e' um
  carimbo.

  Motivo, e e' historico deste repositorio: SEIS vezes um teste ou portao
  EXIGIU a mentira que deveria proibir. Casos registrados: a comparacao
  crua de RPTR na faixa A15; mcdm_callbacks_test exigindo STATUS_SUCCESS
  sem executor de TDR; o memcpy de CPU no Draw na faixa C1;
  sdma_blt_executed casando com a string errada; o teste do blt exigindo
  128 bytes; e o gate de HardwareAccess que EU proprio inverti depois de
  declara-lo contrato. Escreva os portoes contando com isso.

  Cuidado especifico ja aprendido: a funcao require() casa SUBSTRING, e o
  nome certo costuma ser prefixo do nome errado (GfxIbDumpDir casa dentro
  de GfxIbDumpDirOutro). Use require_token(), que ja existe no arquivo e
  casa por limite de palavra.

  Outro: portao que le COMENTARIO em vez de codigo passa por engano. Use
  strip_c_comments(), que ja existe.

5.2 O CRITERIO DA CORRIDA SE ESCREVE ANTES DO ENSAIO.

  Lei 9 e Lei 10. Use docs/294 como modelo. As tres regras que a Lei 10
  impoe, todas nascidas de falha medida:

    1. Assinatura e implicacao sao CAMPOS SEPARADOS. Uma pode ocorrer sem
       a outra; quando isso acontecer, registre as duas coisas em vez de
       escolher.
    2. Todo criterio leva um desfecho final "E — algo fora do mapa", com a
       instrucao de descrever o observado ANTES de julga-lo. Na Corrida 39
       o maior resultado nao estava em nenhuma das quatro letras previstas.
    3. Desfecho parcial NAO e' desfecho. Se um desfecho exige tres sinais e
       aparecem dois, ele NAO ocorreu.

5.3 ECONOMIA DE CORRIDAS.

  A corrida fisica e' o recurso escasso. Antes de propor ensaio, responda o
  que puder por leitura de fonte, mineracao de docs/evidence/ e comparacao
  com os drivers reais da maquina. O corpus de evidencia e' grande e ja
  respondeu perguntas que alguem ia "escalar".

5.4 EMPACOTAMENTO.

  powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-package.ps1
  python tools\audit-bc250mcdm-safety.py
  python tools\audit-mcdm-ddi-matrix.py
  python tools\audit-ddi-noop-callbacks.py

  ARMADILHA CRITICA: o empacotador NAO COMPILA o SYS nem a UMD. Ele COPIA.
  Pular o build de laboratorio assina o binario do candidato ANTERIOR e voce
  passa a corrida inteira medindo o codigo errado. Sempre confira os
  binarios assinados POR STRING antes de declarar o pacote pronto, e diga
  no relato quais strings voce conferiu.

  Suba o DriverVer fonte antes de cada candidato. Ja existiu no projeto o
  caso de dois artefatos diferentes com o mesmo DriverVer (3.92.1.0),
  desambiguaveis so' por hash.

  O ultimo pacote foi 3.198.1.0. O proximo comeca em 3.199.

5.5 CORRIDA FISICA E RECUPERACAO.

  Quem roda e' o Daniel, sempre. Antes de cada bind: conferir o fusivel com
  tools\inspect-boot-fuse.ps1 (e -Reset se TRIPPED) e BOOT FRIO OBRIGATORIO
  — o estado de engine da corrida anterior NAO cai com desbind, so' com
  boot frio.

  Comando da corrida: tools\EXECUTAR_D3D11_NATIVO.cmd, elevado.
  Recuperacao: tools\BC250_RECOVERY.cmd, elevado.

  NUNCA use "del C:\Windows\System32\drivers\bc*.sys". O padrao casa com
  bcmfn2.sys, que e' driver inbox da Microsoft. E em WinRE o Windows
  raramente e' C:.

5.6 COMO RELATAR UMA CORRIDA (obrigatorio, decisao do dono do projeto).

  Depois de cada corrida fisica analisada, informe ao Daniel:
    1. Houve avanco? Sim, nao ou inconclusivo, comparando com a corrida
       anterior pertinente. Diferencie avanco funcional de aprendizado de
       diagnostico.
    2. Sucessos: o que passou, metricas e evidencia.
    3. Falhas: erros, timeouts, pixels incorretos, device-lost, TDR,
       BugCheck. Se nenhum foi observado, limite a afirmacao ao escopo
       medido.
    4. Veredito frente ao criterio escrito ANTES do ensaio. Codigo 0 e
       Exit 0 isolados nao fecham teste nenhum.
    5. Recuperacao e pendencias: driver ativo ao final, resultado do
       rollback, o que continua NAO VALIDADO EM HARDWARE, proximo passo.

  Identifique data/hora, candidato e pasta de evidencia. Se faltarem logs
  ou medicoes, DECLARE A LACUNA. Nao invente metrica.
  Desktop restaurado pelo BasicDisplay e' recuperacao, nao sucesso do nosso
  driver — esse engano ja foi cometido e retratado duas vezes.


================================================================
6. ARMADILHAS JA CATALOGADAS — NAO REPITA
================================================================

  - TUNABLE LIDO NO RAMO ERRADO DO #if. Ja aconteceu QUATRO vezes
    (SdmaIndirectOnly, GpuvaLabPromotion, ContextAbi, GfxWaveProbe). Um
    campo escrito em UM SO' ramo de
    "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)" fica
    invisivel na imagem de laboratorio e a corrida inteira se perde. O
    portao audit_lab_tunables_are_read_in_the_common_path existe por isso.

  - DDI QUE RECUSA CALADA. Toda saida nao-SUCCESS tem de logar o motivo.
    Sem isso e' impossivel distinguir "o dxgkrnl nunca chamou" de "nos
    recusamos em silencio" — e a ausencia de linha deixa de ser conclusiva.
    Lei 4. Ja custou tres corridas.

  - LOG AMOSTRADO. C:\BC250_DISPLAY_LOG.txt e' cumulativo e o logger
    AMOSTRA por classe. Ausencia de linha NAO prova que o subsistema parou,
    a menos que a tag esteja isenta da amostragem. Ja houve veredito errado
    por isso (classe 9 comendo "Primary token confirmed").

  - EVIDENCIA TRUNCADA. A pasta da corrida pode estar curta; o log vivo em
    C: tem o resto. tools\recover-bc250mcdm-lost-tail.ps1 resgata com
    SHA-256 e procedencia, e o binder o chama antes de cada bind. Confira
    "complete_at_capture" em display-log-final-archive.json — e entenda que
    ele significa completo NO MOMENTO DA CAPTURA, nao completo para o bind.

  - HEREDOC DO BASH CORROMPE ESCAPES. \a vira BEL, \b vira BS, \n vira
    quebra de linha real. Caminhos do Windows e strings em C ficam
    destruidos. Para conteudo com contrabarra ou apostrofo, use a ferramenta
    de escrita de arquivo, nao heredoc.

  - ANCORA TEXTUAL AMBIGUA. Em arquivos de milhares de linhas,
    UNREFERENCED_PARAMETER(Subresource) aparece em varias funcoes. Resolva
    o alvo pela FUNCAO que o contem, nao por texto solto. Ja produziu edicao
    no lugar errado.

  - MESA/RADV FICA FORA DESTE REPOSITORIO, em
    C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm. Eu ja
    afirmei por engano que essa arvore nao existia — uma busca rasa nao a
    alcancou. O delta esta espelhado em external/radv-wddm/ e conferido por
    tools/sync-radv-fork-delta.py. Verifique antes de concluir que algo nao
    existe.

  - O empacotador pode falhar no meio com IOException e deixar o pacote
    INCONSISTENTE. Nunca aceite pacote sem a auditoria PASS logo depois.


================================================================
7. RESUMO EM UMA FRASE
================================================================

O draw funciona; o quadro nao fecha. Conserte a coleta e o log duplicado
(P1), faca a recusa nomear o recurso (P2), arme o dump do dwm.exe (P3),
escreva o criterio sob a Lei 10 e entregue o pacote 3.199.1.0 ao Daniel
para uma corrida. Nao afirme pixel sem conferir byte. Quando faltar prova,
escreva literalmente NAO VALIDADO EM HARDWARE.
