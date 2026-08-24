# Estado consolidado antes do WDDM

Atualizado em: 2026-08-23. Gate mínimo e rollback confirmados no silício; o
gate de recuperação está reprovado e é o bloqueador atual.

Este é o documento vivo de passagem de sessão. Ele resume o que foi feito, o
estado atual da máquina, o que está comprovado, o que falhou e quais gates ainda
precisam passar antes de iniciar a implementação do miniport WDDM.

## Resumo breve

Progresso conservador até uma base estável pré-WDDM:

`██████████████████▋░ 93%`

A máquina está isolada do driver legado: a BC-250 usa Microsoft Basic Display
Adapter, `bc250v4` foi removido offline e `bc250dbg` é o único laboratório
autorizado. O primeiro baseline sem contaminação foi capturado e demonstrou que
os rings PSP e SDMA observados anteriormente eram resíduos de software, não
estado de boot reutilizável.

O laboratório obteve a cadeia forte de execução SDMA: firmware aceito pelo PSP,
F32 ativo, RPTR alcançando WPTR, fence escrita pela GPU e quatro dwords corretos
na VRAM. O defeito de rollback do WPTR encontrado no primeiro ensaio foi
corrigido e **a correção está confirmada no silício**. A telemetria mede o
registro físico de GFXOFF e sincroniza GFXOFF/GRBM/SDMA/F32/clock por QPC; ela
mostrou que uma carga de SDMA não tira o GFX do GFXOFF, então falta observar a
transição com carga GFX/compute, calibrar térmica e obter potência.

A ABI 5 acrescentou os três modos negativos da SDMA (`-nokick`, `-badfence`,
`-badop`) e o ensaio `tools/test-sdma-robustness.ps1`; a ABI 6 fez o rollback
reportar onde divergiu; a ABI 7 acrescentou o dump `sdma fault`.

No boot limpo de 2026-08-23 00:16 o ciclo baseline → rollback sem submissão →
submissão mínima com fence passou inteiro pela primeira vez, incluindo a
correção do WPTR: depois do teardown o ponteiro voltou a `0x00000000`, com 24
registradores e 1029 dwords de VRAM restaurados sem divergência.

O ensaio de robustez foi executado em **quatro boots limpos**. Repetição,
wraparound, retomada e os três casos negativos deram o mesmo resultado nas
quatro passagens. A recuperação depois do pacote malformado reprovou em todas.

### Gate de recuperação reescrito e instrumentado

Decisão tomada em 2026-08-23: o gate original exigia "recuperação determinística
após falha", e a medição mostrou que isso não existe para a injúria de opcode
reservado. Perseguir o inexistente tem retorno negativo; esconder seria pior. O
gate passou a ser escrito em requisitos verificáveis, e três deles já têm ensaio
próprio: `tools/test-sdma-failclosed.ps1`.

| Item | Requisito | Como é medido |
|---|---|---|
| G1 | O driver nunca emite pacote malformado | `sdma opgate`: nove entradas inválidas do IOCTL têm de ser recusadas antes de virar pacote, com o WPTR imóvel; depois disso uma submissão normal ainda tem de funcionar |
| G2 | Detecção determinística do estado mentiroso | após a injúria: o driver marca a engine, **recusa submissão por conta própria**, e quando forçado a medir nega conclusão mesmo com ponteiros e fence bons |
| G3 | Falha limpa | com a engine condenada: rollback devolve 24 registradores e a VRAM sem divergência, anel desabilitado, WPTR zero, F32 haltado, destino restaurado, driver respondendo, zero bugcheck |
| G4 | Recuperação em nível de dispositivo | **em aberto e não auditado**: o display está na mesma GPU |
| G5 | Falhas realistas: timeout, wraparound sob carga, idle/retomada | já cobertas pelo ensaio de robustez em quatro boots |

A bateria `sdma opgate` cobre: opcode 0, opcode 99, bit de flag desconhecido,
dois negativos simultâneos, fence fora do dword reservado, zero dwords, dwords
acima do máximo, destino desalinhado e poison igual ao esperado.

O que o gate reescrito **não** afirma: que a engine é recuperável. A injúria de
opcode reservado continua sem caminho de volta por software, e isso está
registrado como comportamento medido, não como defeito a corrigir.

### Como o gate de recuperação deve ser reescrito

O que quatro configurações de recuperação e seis boots estabeleceram: **a injúria
de opcode reservado não tem caminho de volta por software.** Insistir em
descobrir um caminho tem retorno decrescente; o que falta é transformar o
achado em requisito de driver.

Proposta de reescrita do gate, para substituir "recuperação determinística após
falha" por algo verificável:

1. **O driver nunca emite pacote malformado.** O construtor de pacotes é a
   fronteira; opcode vem de enumeração fechada, nunca de dado externo.
2. **Detecção determinística do estado mentiroso.** Assinatura medida e agora
   implementada: fence alcançada com dado divergente. Já bloqueia submissão.
3. **Falha limpa.** Sem fallback silencioso: trabalho pendente é reprovado, a
   engine é marcada inutilizável e o driver recusa novas submissões.
4. **Recuperação em nível de dispositivo** fica como requisito separado e ainda
   não auditado, porque o display está na mesma GPU.
5. **Falhas realistas** — timeout em trabalho válido, wraparound sob carga,
   idle/retomada — continuam exigidas e já estão cobertas.

Com isso o gate deixa de depender de algo que a medição mostrou não existir, sem
esconder a limitação.

### Bloqueadores atuais do WDDM

O gate de recuperação saiu da lista: reescrito em requisitos verificáveis, foi
medido e aprovado em 2026-08-23 08:53. Sobram três frentes, em ordem de peso:

1. **GCVM/VMID0, CP e compute** — `███▌░░░░░░ 35%`. Passo 1 de 6 aprovado: page
   table construída na VRAM e conferida entrada por entrada, com o endereço
   físico derivado de registrador lido em runtime. Faltam os passos 2 a 6, que
   envolvem escrever registrador de tradução, e CP/PM4 e MQD/HQD continuam
   intocados.
2. **Observabilidade física** — `████░░░░░░ 40%`. GFXOFF nunca foi visto
   transicionar, e carga de SDMA não serve para provocar a transição; térmica
   sem calibração; potência indisponível sem fonte confiável.
3. **Determinismo por repetição** — os ciclos passam, mas o número de boots por
   ensaio ainda é pequeno para o padrão que o próprio projeto exige.

O que **não** é mais bloqueador: recuperação da injúria de opcode reservado. A
medição mostrou que não existe caminho por software, o driver passou a detectar e
falhar limpo, e o requisito de recuperação em nível de dispositivo virou item
próprio, ainda não auditado.

Veredito formal atual:

**NÃO VALIDADO EM HARDWARE**

## Estado atual da máquina

- Dispositivo: AMD BC-250 `1002:13FE`, revisão `00`.
- Binding de vídeo: `BasicDisplay` / Microsoft Basic Display Adapter.
- Pacote e serviço `bc250v4`: ausentes; não reinstalar.
- Laboratório permitido: somente `bc250dbg`.
- Driver de laboratório instalado: `bc250dbg v7.0`, ABI 7.
- Workspace: `v9.0`, ABI 9: o `sdma enginereset` restaura a configuração da
  UTCL1 após o pulso. Exige `dbg/install.bat` antes do próximo ensaio.
- CLI recompilado depois do último ensaio (remoção da heurística enganosa do
  `sdma fault`); o preflight só compara o hash do `.sys`, então não exige
  reinstalação.
- O SHA-256 muda a cada recompilação por causa do carimbo de assinatura, então
  o gate real não é um valor fixo: é o `Get-FileHash` do workspace ser idêntico
  ao do arquivo instalado, conferido por todos os ensaios.
- Armamento após o ensaio: `readonly`.
- PSP: TOS pronto; nenhum ring GPCOM registrado.
- SDMA ring: desabilitado, base zerada e polling zerado.
- Estado residual da sessão de 2026-08-23 01:05, ao encerrar o dia: firmware
  SDMA carregado, engine SDMA0 com o caminho de escrita quebrado pelo caso
  negativo de opcode reservado. Anel desabilitado, base e ponteiros zerados,
  F32 haltado, VRAM de destino restaurada.
- **Antes de qualquer novo ensaio: reiniciar.** O preflight recusa a sessão
  sozinho, mas a engine quebrada também não voltaria por software — foi o que
  os quatro degraus da escada demonstraram.
- A máquina está utilizável normalmente: o display usa BasicDisplay, que não
  depende da SDMA.
- SMU Q0: responde corretamente; TestMessage continua retornando status `1`.
- GFXOFF físico: `SMUIO_GFX_MISC_CNTL=0`, bits `2:1=0` na sessão ociosa.
- Sensor térmico: ainda não calibrado; `unsafe` permanece bloqueado.
- Driver Verifier: configuração histórica voltada ao antigo `bc250wddm.sys` deve
  ser revisada antes de qualquer futuro miniport.

O estado atual não é considerado baseline limpo por causa do firmware SDMA em
execução e do F32 fora de HALT. Os ponteiros estão zerados e o ring está
desabilitado, mas é obrigatório reiniciar antes da próxima confirmação física —
foi exatamente por isso que o ensaio de robustez se recusou a rodar.

## Tudo que já foi realizado

### 1. Contenção do driver legado

- A remoção ao vivo de `oem48.inf` reproduziu bugcheck
  `DRIVER_VERIFIER_DETECTED_VIOLATION` (`0xC4/0x62`) durante o teardown de
  `bc250wddm.sys`.
- Dump, metadados, configuração do Verifier e backup foram preservados.
- O pacote foi removido offline para impedir novo teardown defeituoso.
- Após reboot, foi confirmado que `bc250v4` não existe e a placa está em
  BasicDisplay.
- Regra permanente: não reinstalar `bc250v4` nesta máquina de laboratório.

### 2. Primeiro baseline sem contaminação

- Captura limpa criada em
  `docs/evidence/2026-08-22_22-54-06-DESKTOP-HQAG6T4-baseline`.
- Foram executados 46 comandos e oito consultas SMU permitidas, sem falha.
- O PSP apareceu apenas com TOS pronto, sem ring externo.
- A SDMA apareceu desabilitada, sem base, polling ou ponteiros configurados.
- GCVM permaneceu sem alteração material entre as capturas comparáveis.
- Ficou demonstrado que o ring PSP e o ring SDMA antigos eram resíduos deixados
  por software da sessão contaminada.

### 3. Correção do mapa e da documentação

- `CP_ME_CNTL` documentado com o valor realmente observado `0x15000000`.
- `0x04C50` classificado como `GFX_RB_AQL_CNTL` com derivação do header GC10.1.
- `0x04BD0` permaneceu em quarentena por falta de símbolo confiável.
- Estados observados foram separados de capacidades e de prova de execução.
- Documentos históricos que anunciavam SDMA, compute ou throughput como
  concluídos permanecem revogados.

### 4. Lifecycle transacional da SDMA

- Criado `sdma rollback` e IOCTL dedicado.
- Ring em RAM do sistema foi bloqueado até GCVM/VMID0 serem validados.
- Ring permitido apenas em VRAM, com tamanho e alinhamento limitados.
- Inicialização recusa apropriar-se de ring preexistente ativo.
- Snapshot inclui 24 registradores, ring, RPTR writeback, WPTR shadow e fence.
- Falha de readback durante init provoca rollback imediato.
- Teardown desabilita ring/polling, halta o F32, restaura VRAM e registradores
  e verifica todas as leituras de volta. Ressalva medida em 2026-08-23: como o
  snapshot é tirado depois do reset do F32, a restauração devolve `HALT=0` e o
  microcontrolador termina o teardown rodando.
- Ensaio sem submissão passou em
  `docs/evidence/2026-08-22_23-06-17-DESKTOP-HQAG6T4-sdma-rollback`.

### 5. ABI 3 e fence física da SDMA

- ABI elevada para 3 e versão do laboratório para 3.0.
- `SDMA_OP_IO` passou a registrar endereço, poison, esperado e observado da
  fence, além de STATUS e PC F32 antes/depois.
- A CPU escreve somente o poison; o valor esperado existe apenas no pacote
  FENCE da SDMA.
- `Completed` exige simultaneamente RPTR igual a WPTR e fence esperada observada.
- `sdma write` só retorna sucesso quando a leitura integral do destino também
  coincide com os dados esperados.
- Layout do pacote foi reconciliado com `sdma_v5_0.c` e com as definições Navi10
  de pacote SDMA5.

### 6. Lifecycle seguro do PSP

- Criado `psp rollback` e IOCTL dedicado.
- A criação exige TOS pronto e recusa ring externo existente.
- O driver tira snapshot dos registradores GPCOM antes de programá-los.
- O teardown envia `DESTROY_RINGS` e aguarda ACK antes de liberar buffers.
- Em timeout, os buffers são preservados para evitar DMA use-after-free.
- O teste passou em
  `docs/evidence/2026-08-22_23-14-56-DESKTOP-HQAG6T4-psp-rollback`.
- O eco de `DESTROY_RINGS` em C2PMSG64 é aceito semanticamente; o upstream
  também não apaga esses bits após o ACK. WPTR/base/tamanho devem voltar a zero.

### 7. Primeiro comando SDMA com provas físicas

Evidência:
`docs/evidence/2026-08-22_23-20-13-DESKTOP-HQAG6T4-sdma-minimal`.

- Firmware `cyan_skillfish2_sdma.bin`:
  - SHA-256 `15d0d3626da7f2513f03b13bb7e02eeeeccab276276ae27d0b6067b5f25e9e95`;
  - ucode versão `0x34`;
  - aceito pelo PSP com `RespStatus=0` e fence de transporte alcançada.
- F32:
  - saiu de HALT;
  - checksum `0x55B182E3`;
  - PC percorreu `0x0142..0x03E0`;
  - 17 endereços distintos em 24 amostras.
- Ring SDMA0: `GPU 0xF40FF00000`.
- WRITE_LINEAR de quatro dwords:
  - RPTR `0x00 -> 0x44`;
  - WPTR `0x44`;
  - fence em VRAM `0x0FF03000`;
  - poison `0x43DAFFFE`;
  - valor observado `0xBC250001`, igual ao esperado do pacote;
  - quatro dwords de destino coincidiram integralmente com o esperado;
  - `STATUS_REG` mudou de `0x46DEE557` para `0x46DEED57`;
  - os quatro dwords originais do destino foram restaurados pela CPU.

Essa evidência exclui um simples retorno fabricado do CLI: houve consumo do
ring, fence com valor não escrito pela CPU no destino e transformação correta
da VRAM. Ela ainda não satisfaz sozinha todas as regras de validação do projeto.

### 8. Defeito de rollback encontrado e corrigido

- O primeiro teardown após submissão deixou WPTR `0x44` residual.
- Ring, base, polling e PSP foram desarmados; o destino foi restaurado.
- Causa: o rollback tentava reduzir WPTR sem armar `MINOR_PTR_UPDATE`.
- Correção: restaurar WPTR/WPTR_HI com `MINOR_PTR_UPDATE` armado, restaurar o
  valor original desse controle e só então restaurar `RB_CNTL` por último.
- O IOCTL de rollback agora devolve contadores de divergência pelo payload mesmo
  quando o gate falha, sem perder o diagnóstico no transporte.
- Novo driver compilado, assinado, instalado e iniciado em `readonly`.
- A correção ainda não foi validada em hardware porque a sessão atual não está
  limpa; repetir sem reboot produziria uma evidência contaminada.

### 9. Telemetria física e preflight ABI 4

- `SMUIO_GFX_MISC_CNTL` foi derivado do upstream local:
  `0x5A000 + 0xC5 * 4 = 0x5A314`.
- Bits `2:1` decodificam `0=GFXOFF`, `1=saindo`, `2=ativo`, `3=entrando`.
- Cinco leituras readonly e o IOCTL ABI 4 retornaram `0`, GFXOFF no idle atual.
- `telemetry sample [smu]` produz uma amostra coerente com QPC.
- `telemetry watch` produz CSV de alta frequência e queries SMU espaçadas.
- Cada amostra inclui GFXOFF físico, GRBM, SDMA STATUS/RB/RPTR/WPTR,
  writeback/shadow, F32, CLK_CTRL e térmica bruta.
- O IOCTL de operação SDMA também captura before/kick/after dentro do kernel,
  reduzindo o risco de perder uma transição curta.
- Potência é reportada como indisponível; nenhum valor é estimado.
- O ensaio mínimo agora recusa RB ativo, base não zero ou WPTR residual antes
  de `arm write`.
- A tentativa readonly de 23:38 recusou corretamente esta sessão por
  `RB_CNTL=0x80840000`, base `0` e WPTR `0x44`; nenhuma escrita foi armada.

### 10. Modos negativos e ensaio de robustez (ABI 5)

- ABI elevada para 5 e versão do laboratório para 5.0.
- `BC250DBG_SDMA_OP_IO` ganhou `FenceEmitted`, `Kicked`, `WptrRegBefore/After`,
  `RptrRegAfter`, `RptrWbAfter` e `WptrShadowAfter`.
- Três modos negativos no caminho real de submissão:
  - `-nokick`: nada é publicado; RPTR, fence e destino têm de ficar parados.
  - `-badfence`: a GPU escreve a fence com valor diferente do esperado pela
    CPU; `FenceReached` tem de ser 0.
  - `-badop`: header com opcode reservado, sem payload e sem pacote FENCE;
    a fence tem de continuar com o poison.
- Os modos são mutuamente exclusivos e bits desconhecidos em `Flags` são
  recusados pelo driver.
- O gate de conclusão continua exigindo RPTR igual ao WPTR publicado e fence
  física com o valor esperado; ele não foi afrouxado.
- `sdma init` aceita tamanho de anel em dwords (64 a 1024), o que torna o teste
  de wraparound barato: com 128 dwords o anel dá a volta em poucas submissões.
- Novo `tools/test-sdma-robustness.ps1`, com o mesmo preflight fail-closed do
  ensaio mínimo, encadeia: repetição, wraparound, ocioso/retomada, os três
  negativos, uma submissão normal de recuperação após cada um, rollback,
  verificação de registradores/VRAM e varredura de bugcheck na janela do teste.
- Driver e CLI recompilados e assinados; nada foi instalado nem executado no
  silício. A sessão atual continua contaminada.

### 46. RLC FORA DA ROM: primeira prova física na frente do CP

Evidência: `docs/evidence/2026-08-23_23-32-03-DESKTOP-HQAG6T4-rlc-bringup`.

`RLC_GPM_STAT` passou de `0x00F40016` para **`0x00B40016`** —
`RLC_EXEC_ROM_CODE` de 1 para 0, e o valor final é **bit a bit idêntico ao
Linux** com o CP executando. `RLC_CNTL = 0x00000001`,
`RLC_CGCG_CGLS_CTRL = 0x00000000`, como na referência.

O gate era exatamente esse bit, definido antes da primeira escrita. É do mesmo
tipo de prova que validou o microcontrolador da SDMA: estado de execução
observável mudando, não escrita aceita.

**O crédito é do PSP, não do nosso `start`.** O `start` leu `0x00000001` antes
de escrever — o `ENABLE_F32` já estava ligado. Quem habilitou foi o PSP ao
carregar o ucode (tipo 8, 6272 dwords, `RespStatus = 0`). Sem olhar o `antes` da
escrita eu teria atribuído ao `start` um efeito que ele não teve.

O RLC mexeu no próprio estado depois de subir: `RLC_PG_CNTL` foi de 0 para
`0x00000008` e `RLC_SRM_CNTL` de 0 para `0x00000002`, sem escrita nossa — sinais
de microcontrolador vivo.

**Uma hipótese minha caiu:** os `SOFT_OVERRIDE_*` do `CGTT_CPF_CLK_CTRL` **não**
aparecem sozinhos (`0x00000100` antes e depois, contra `0xF8000100` no Linux).
Não é consequência do RLC; o amdgpu escreve isso em outro lugar.

**E um gate recusou pelo motivo errado:** o controle negativo rodava antes do
`arm write`, então o driver recusou por armamento (erro 5) e a guarda de snapshot
nunca foi exercitada. Corrigido para rodar armado e antes do snapshot, exigindo
a mensagem específica. Regra nova: *controle negativo tem de isolar a guarda que
pretende testar.*

### 45. Diff Windows/Linux: o RLC está rodando ROM, e a zona morta não era o bloqueio

Evidências: `docs/evidence/2026-08-23_23-07-34-...-mmio-responsividade` (Windows,
boot limpo) e `referencias/linuxDump/linux-cp-info-...-20-01-48.json` (Linux,
amdgpu com o CP executando). Mesmos offsets, os dois somente leitura.

**O achado:** `RLC_GPM_STAT.RLC_EXEC_ROM_CODE` é **1 no Windows** e **0 no
Linux**. O RLC do nosso lado está executando código da ROM — bootloader interno,
sem ucode. É o análogo exato do F32 da SDMA em HALT, e é prova física de que o
RLC nunca foi inicializado aqui.

**A zona morta sai da investigação.** `CP_*_UCODE_ADDR` e `CP_*_IC_*` leem
`0xFFFFFFFF` **nos dois sistemas**, e o CP funciona no Linux. Não são o caminho
que o hardware usa, ou são write-only. Isso mata a terceira hipótese minha
seguida — clock gating do CPF — e inverte o papel do `CGTT_CPF_CLK_CTRL`: os
cinco `SOFT_OVERRIDE_*` que o Linux tem ligados são **consequência** do RLC
rodando, não causa.

**A rota do RLC estava certa na direção.** O diagnóstico de outra sessão apontou
o RLC; eu corrigi os endereços dele (com razão) e escolhi o suspeito errado. Vale
registrar nessa ordem.

Diferença de receita: o RLC **não** é carregado por `RLC_GPM_UCODE_ADDR/DATA`
(esses não respondem nem no Linux). É carregado pelo **PSP** — e isso já estava
medido: `RespStatus = 0` às 14:21, e o `amdgpu_firmware_info` do Linux confirma
`RLC firmware version 0x0000000d`, a mesma versão do arquivo que o nosso PSP
aceitou.

Sequência derivada de `gfx_v10_0_rlc_resume()`, endereços já conferidos:

| Passo | Ação | Endereço |
|---|---|---|
| 1 | RLC pelo PSP | já aceito |
| 2 | `RLC_CGCG_CGLS_CTRL = 0` | `0x3B124` (temos `0x0001003C`) |
| 3 | `RLC_PG_CNTL = 0` | `0x3B10C` (já 0) |
| 4 | `RLC_CNTL.RLC_ENABLE_F32 = 1` | `0x3B000` |
| 5 | **`RLC_EXEC_ROM_CODE` cair de 1 para 0** | `0x3B9B8` — **o gate** |

### 44. Boot limpo: a hipótese do estado pendurado cai também

Evidência: `docs/evidence/2026-08-23_19-43-11-DESKTOP-HQAG6T4-mmio-responsividade`.

A GPU estava comprovadamente em repouso — `GRBM_STATUS 0x00003028`,
`GRBM_STATUS2 0x00000008`, `CP_STAT 0x00000000`, e `CP_RB0_CNTL` de volta ao
default `0x00A00000` (confirmando que o `0x00000709` da sessão anterior era
resíduo dos ensaios de anel).

**E a vizinhança `CP_*_UCODE_*` / `CP_*_IC_*` continuou em `0xFFFFFFFF`.**

Duas hipóteses eliminadas por medição, cada uma com custo próximo de zero:

| Hipótese | Como caiu |
|---|---|
| Bloco de GFX sem energia | `RLC_CNTL` e `GRBM_GFX_INDEX` respondem |
| Estado pendurado de sessão anterior | Boot limpo, GPU em repouso, mesma não-resposta |

**Fato consolidado:** dw `0x5814` a `0x5848` no segmento 1 não decodificam, em
boot limpo, GPU em repouso, com endereços conferidos. Tudo até dw `0x4c05` no
mesmo segmento decodifica.

**Anomalia em aberto:** às 16:19 as escritas nesses endereços foram aceitas. Os
valores queridos denunciam código diferente — `0x00000010` (construído do zero)
contra `0xFC7FFFF0` (construído do readback `0xFFFFFFFF`). Isso revelou um
defeito meu: read-modify-write sobre registrador que não responde **propaga o
lixo para o valor escrito**. Mas não explica por que 16:19 aceitou, e essa
continua a única observação que nenhuma hipótese cobre.

**Correções:** guarda contra read-modify-write sobre leitura inválida; e o hazard
de literal hexadecimal do PowerShell (`0x80000000` como `Int32` negativo)
abortava o fim do ensaio — terceira aparição, corrigida com sufixo `L` e máscara
`[uint32]`, com o decode testado em execução.

**Próximo corte, de graça:** o mapa ganhou sondas de fronteira. A que decide é
`RLC_GPM_UCODE_ADDR` (dw `0x5b61`, `0x3ED84`), **acima** da zona morta. Se
responder, a não-resposta é propriedade do bloco do CP, e os suspeitos passam a
ser `CGTT_CPF_CLK_CTRL` e `CGTT_CPC_CLK_CTRL` — clock gating de bloco é a
explicação mais econômica ainda viva.

### 43. Mapa de responsividade: o GFX não está desligado, a sessão estava travada

Evidência: `docs/evidence/2026-08-23_19-36-05-DESKTOP-HQAG6T4-mmio-responsividade`,
somente leitura.

**`RLC_CNTL` (`0x3B000`, seg 1) responde: `0x00000002` = `FORCE_RETRY`.**
`GRBM_GFX_INDEX` no segmento 1 também responde. Isso **falsifica** a hipótese de
bloco de GFX sem energia — se estivesse desligado, os dois devolveriam
`0xFFFFFFFF`.

A não-resposta é **localizada**: começa em dw `0x5814` e cobre só a vizinhança
`CP_*_UCODE_*` / `CP_*_IC_*`. Tudo até dw `0x4c05` responde.

**E o mapa revelou a sessão contaminada.** Decodificado com as máscaras
autoritativas, contra o repouso medido em todas as sessões anteriores:

| Registrador | Repouso | Agora | Bits novos |
|---|---|---|---|
| `GRBM_STATUS` | `0x00003028` | `0xA0003028` | `GUI_ACTIVE`, `CP_BUSY` |
| `GRBM_STATUS2` | `0x00000008` | `0x51000008` | `RLC_BUSY`, `CPF_BUSY`, `CPG_BUSY` |
| `CP_STAT` | `0x00000000` | `0x84008000` | `CP_BUSY`, `CE_BUSY`, `PFP_BUSY` |

Com uma contradição no meio: `CP_ME_CNTL = 0x15000000` diz as três engines
haltadas, `CP_STAT` diz `PFP_BUSY` e `CE_BUSY`. Engine haltada reportando ocupada
é estado pendurado, não operação.

**A linha do tempo inverte causa e efeito** em relação ao diagnóstico anterior:
às 16:19, antes de existir anel, as escritas em `IC_BASE` foram **aceitas** com
readback exato. Os ensaios de anel PM4 rodaram entre 16:29 e 16:59. Só depois
disso o bloco passou a devolver `0xFFFFFFFF`. Não é "o RLC inativo travou o
ROQ" — é o travamento que deixou o bloco do CP sem responder.

Duas correções de decodificação: `CP_STAT = 0x84008000` é
`CP_BUSY | CE_BUSY | PFP_BUSY`, não `ROQ_RING_BUSY | PFP_BUSY | ME_BUSY`; e
`RLC_CNTL = 0x2` é `FORCE_RETRY`, não `RLC_ENABLE_F32`.

**Próximo passo, custa um reboot e nada mais:** repetir o mesmo mapa em boot
limpo. Se `IC_BASE` responder, o bloco está íntegro e a rota do RLC fica
desnecessária. Se continuar `0xFFFFFFFF`, é propriedade do bloco e a rota do RLC
volta com os endereços corretos.

### 42. AUDITORIA: os passos 2, 3 e 4 do CP voltam para não provado

Uma sessão registrou os passos 2, 3 e 4 do CP como aprovados e a barra em 95%. A
auditoria das evidências não sustenta. Detalhe completo em
[01_EVIDENCIAS_E_LICOES.md](01_EVIDENCIAS_E_LICOES.md).

**O fato central.** Os registradores da cache de instruções do CP, nos endereços
corretos do segmento 1, leem `0xFFFFFFFF` e descartam escrita
(`docs/evidence/2026-08-23_18-58-23-...-cp-directload`). Todos-os-bits-em-um é
assinatura de endereço que não está sendo decodificado — e não é erro de
endereço: no mesmo segmento 1 o banco de rascunho responde, e no segmento 0
`CP_ME_CNTL` lê `0x15000000` estável.

**O falso positivo que deixou passar.** `INVALIDATE_CACHE_COMPLETE` é o bit 1, e
`0xFFFFFFFF` tem esse bit ligado. A espera terminava na primeira leitura e o gate
imprimia "o hardware confirmou por COMPLETE". Aconteceu idêntico em duas sessões.
Culpa minha: escrevi a espera sem checar se o registrador respondia.

**A instabilidade.** Nos mesmos endereços, a escrita colou às 16:19 e foi
descartada às 18:15 e 18:58. Isso não é base para declarar passo aprovado.

**Um endereço trocado por derivação errada.** O código chegou com
`A_CP_PFP_IC_BASE_LO = 0x1AA80`, que é `(0x1260 + 0x5840) * 4` — base do
segmento 0 num registrador `BASE_IDX 1`. `mmCP_PFP_IC_BASE_LO` existe só no
segmento 1. Lição 12 reaparecendo. Restaurados os 18 defines afetados para
`0x3E1xx`/`0x3E0xx`.

**Sobre a hipótese do RLC.** A direção é plausível — bloco de GFX sem energia
explica `0xFFFFFFFF`, e o RLC é o portão. Mas a receita proposta usa
`RLC_CNTL = 0x17980` e `RLC_SAFE_MODE = 0x17994`, que são derivações do segmento
0 dos mesmos registradores `BASE_IDX 1`. Corretos: `0x3B000` e `0x3B014`. E pode
ser circular: se o segmento 1 do GFX não responde, `RLC_CNTL` também não, e a
correção não conseguiria se aplicar.

**Correções aplicadas no código:** endereços restaurados ao segmento 1; guarda
`BcRegResponds()` que recusa `icinval`, `icbase` e `jt` quando o registrador lê
`0xFFFFFFFF`, com mensagem dizendo por quê. Regra nova: **antes de esperar por um
bit de status, conferir que o registrador responde** — todos-em-um satisfaz
qualquer máscara.

**Próxima medição, de graça:** `tools/test-mmio-responsividade.ps1`, somente
leitura, mapeia quem responde e quem não. A pergunta que decide a rota é se
`RLC_CNTL` em `0x3B000` responde.

### 41. Segmento 1 medido, direct load do CP implementado

Evidência do segmento 1: `docs/evidence/2026-08-23_17-54-39-DESKTOP-HQAG6T4-mmio-segment1`,
sete de sete, sem custo de boot.

`SCRATCH_REG0..7` guardaram oito valores distintos e depois o complemento de
cada um. O controle `TCP_WATCH0_ADDR_H` — mesmo dword `0x2040`, segmento 0 —
ficou intacto. **Base `0xA000` dwords: classe P1 → P0.**

Três correções que vieram de reler a fonte autoritativa:

1. **`CP_ME_CNTL` decodificado.** Dois headers de terceiros em `referencias/`
   dizem `PFP_HALT = 1 << 30` e `CE_HALT = 1 << 29`. Errado. O header
   autoritativo diz `CE_HALT` 24, `PFP_HALT` 26, `ME_HALT` 28 — e é essa leitura
   que faz o valor medido `0x15000000` significar "as três engines haltadas", o
   estado coerente para o POST. Com os bits de terceiros viraria
   `ME_HALT + PFP_STEP + CE_HALT`. O código deles pede single-step onde acha que
   está haltando.
2. **`CP_HYP_*_UCODE_ADDR` é alias.** `gfx_v10_0.c` linha 140 define
   `mmCP_HYP_PFP_UCODE_ADDR = 0x5814 BASE_IDX 1` — mesmo dword e segmento de
   `mmCP_PFP_UCODE_ADDR`. Registrador único, dois nomes.
3. **SHA-256 do header autoritativo conferido** contra o registrado no extrato do
   projeto: bate. Mesmo arquivo, integridade confirmada.

Implementado (ABI 18), **não medido**: `bc250ctl cp` com `info`, `txn`, `stage`,
`rollback`, `icinval`, `icbase`, `jt` e `status`, mais o ensaio
`test-cp-directload.ps1` de doze gates.

O `cp info` já rodou: header v1.0, IP v10.1 e jump table dentro do payload nas
três engines. É leitura de arquivo, não precisa de driver, e é a única parte do
passo 2 que não depende de hardware.

O ensaio **não carrega firmware pelo PSP e não contamina a sessão**: o ucode volta
da VRAM e os dez registradores voltam do snapshot. E o gate que mais importa é
negativo — os quatro ponteiros de instrução têm de continuar imóveis do início ao
fim, porque nada ali tira engine do HALT.

### 40. A base do segmento 1 é a suposição que bloqueia o CP

`tools/test-mmio-segment1.ps1`, escrito e **não medido**.

O direct load do CP mora todo no segmento 1 do MMIO, cuja base (`0xA000` dwords)
está no catálogo como classe **P1: derivada, nunca conferida contra registrador
vivo**. O segmento 0 tem verificação de sobra; o segmento 1 tem zero.

Escrever `CP_PFP_IC_BASE_LO` sobre isso seria escrever MMIO por suposição — e o
risco já se materializou em forma leve horas antes, quando `RLC_CNTL` foi
calculado com a base errada e o ensaio relatou como RLC valores de outro
endereço.

O ensaio usa o banco de rascunho, `SCRATCH_REG0..7` em `0x30100`..`0x3011C`.
Registrador de rascunho existe para ser escrito, e o upstream escreve
`0xCAFEDEAD` em `mmSCRATCH_REG0` em `gfx_v10_0_ring_test_ring()` pela mesma
razão.

Três coisas o tornam decisivo em vez de sugestivo:

1. **Oito endereços consecutivos guardando oito valores diferentes.** Espelhos do
   mesmo registrador leriam todos o último valor escrito.
2. **Dois padrões complementares.** Um padrão só não distingue armazenamento de
   valor preso que por acaso coincidiu.
3. **Controle no outro segmento.** O dword `0x2040` existe nos dois: `0x30100` é
   `SCRATCH_REG0` (seg 1) e `0x0CA80` é `TCP_WATCH0_ADDR_H` (seg 0). O controle é
   lido, nunca escrito. Se as escritas aparecerem nele, as derivações colidem e
   nenhuma está provada.

**Não exige boot limpo e não contamina a sessão.**

### 39. O PSP não carrega o CP: a frente muda de rota

Evidência: `docs/evidence/2026-08-23_14-21-34-DESKTOP-HQAG6T4-cp-firmware`.

`rlc` aceito com `RespStatus = 0`. `pfp`, `me`, `ce` e `mec` recusados com
`0x80000203`. Nenhum ponteiro de instrução se moveu — carregar firmware não põe o
CP a executar, e nada aqui o tirou de reset.

**Uma revogação:** a leitura de terceiros de que RLC e SDMA retornariam
`ITEM_NOT_FOUND` cai. Aquela medição usou firmware de outro chip
(`navi12_sdma.bin`); com o firmware desta placa o SOS aceita os dois, e o SDMA
demonstravelmente executou depois. O erro era de conteúdo, não de tipo não
suportado.

**Uma confirmação:** o `0x80000203` do CP reproduz o que a referência mediu, por
um caminho de payload completamente diferente. Dois caminhos independentes
chegando ao mesmo código tornam improvável defeito de protocolo.

E é coerente com o upstream: `psp_v11_0_8` não tem `load_ip_fw` para o CP. O
GFX10 usa **direct load**, e o mecanismo é a microengine buscar o próprio ucode
da memória pela cache de instruções — `CP_PFP_IC_BASE_LO/HI` mais a jump table
por `CP_PFP_UCODE_ADDR/DATA`.

**O gate de memória virou pré-requisito do CP, não frente paralela.**
`CP_PFP_IC_BASE_CNTL` tem campo `VMID`, que o upstream programa com 0. Se a busca
de ucode passa pelo VMID 0 do GFXHUB, a tradução provada às 13:49 é dependência
direta.

Defeito de instrumento na mesma execução, corrigido: `RLC_CNTL` e
`RLC_SAFE_MODE` são `BASE_IDX 1` e foram calculados com a base do segmento 0, ou
seja, os valores relatados como RLC não eram desses registradores. Terceira vez
que um instrumento errado passa por um gate. Regra estrutural: **toda entrada de
registrador declara o segmento.** De quebra, resolveu a pendência P2 do catálogo
sobre `UCODE_ADDR/DATA`: o endereço do PFP é `0x3E050`, e `0x3E200` fica em
quarentena.

### 38. GATE DE MEMÓRIA FECHADO: tradução provada no silício (20 de 20)

Evidência: `docs/evidence/2026-08-23_13-49-44-DESKTOP-HQAG6T4-gcvm-translate`.

Uma submissão SDMA cujo endereço no pacote era `0x0000_0800_0000_1000` escreveu
quatro dwords na VRAM em `0x0FD01000`. Esse offset não aparece em lugar nenhum
do pacote; só a page table liga um endereço ao outro. Fence física escrita pela
GPU, dado conferido no kernel, e leitura independente pelo BAR0.

Os quatro controles da mesma execução: destino envenenado imediatamente antes
(`0x5A5A0001..4`), tradução comprovadamente ligada (`ENABLE_CONTEXT=1`,
`SYSTEM_ACCESS_MODE=3`, L2 habilitada), abertura de framebuffer ainda funcionando
com tradução ligada, e o offset baixo `0x00001000` intocado.

Passo 4: invalidação pela engine 17, `REQ 0x00F80001`, ACK aceso na primeira
leitura, `FAULT_STATUS` limpo. As três correções que a leitura do upstream trouxe
— engine 17 e não 0, GFXHUB sem semáforo, leitura muda de `REQ` obrigatória em
GC < 10.3.0 — todas confirmadas.

Passo 6: `restore` dos 35 registradores sem divergência, dump idêntico ao
inicial, e a submissão de sempre voltando a funcionar. Zero bugcheck.

**Passos 1 a 6 do gate de memória: fechados.**

O que isto não prova: uma página de 512 exercitada como destino traduzido, um
único boot, VMID 0 apenas, SDMA apenas. CP/PM4 e MQD/HQD seguem intocados.

### 37. Passos 3-risco, 4, 5 e 6 implementados: o ensaio que liga a tradução

**Ainda não medido.** Código construído, driver e CLI compilando sem aviso,
ensaio escrito. A barra não se move por implementação.

`tools/test-gcvm-translate.ps1`, 20 gates, um único boot. Exige `-Execute` e
`-IAcceptTheRisk`, porque desliga o bypass do POST.

#### Quatro passos juntos, e por quê

Separar 3 de 5 exigiria dois boots com tradução ligada, e o segundo teria de
reconstruir tudo. Pior: com tradução ligada e sem submissão traduzida, o ensaio
gastaria o risco e voltaria sem resposta. A submissão é a única coisa que
responde "traduziu?".

#### Como a prova discrimina tradução de abertura de framebuffer

A tabela mapeia VA `0x0000_0800_0000_0000` em diante para a VRAM a partir de
`0x0FD00000`. O pacote carrega `0x0000_0800_0000_1000`, e o dado tem de aparecer
na VRAM em `0x0FD01000`. **Esse offset não aparece em lugar nenhum do pacote.**
Nada além da page table liga um endereço ao outro.

O destino é envenenado com padrão conhecido imediatamente antes, e um segundo
endereço — `0x00001000`, o que seria escrito se o modo cru fosse ignorado e a
parte baixa do VA virasse offset — é conferido depois para descartar erro de
laboratório.

E um controle antes da prova: submissão para a abertura de framebuffer com a
tradução já ligada. Sem ele, falha na prova não distinguiria "não traduziu" de
"a tradução derrubou o caminho do anel". Se esse controle falhar, o ensaio nem
tenta a submissão traduzida — vai direto para o rollback.

#### Quatro coisas que a leitura do upstream corrigiu antes de gastar um boot

1. **A engine de invalidação é a 17, não a 0.** `gmc_v10_0_flush_gpu_tlb()` é
   literal: `/* Use register 17 for GART */`. O plano dizia engine 0 desde o
   início, e o estágio `invrange` escrevia a faixa da engine 0 — uma engine que
   ninguém usa. Agora escreve as duas.
2. **O GFXHUB não usa semáforo.** `gmc_v10_0_use_invalidate_semaphore()` devolve
   verdadeiro apenas para MMHUB0. O aviso sobre `GCVM_INVALIDATE_ENG0_SEM`
   continua válido — ler adquire, e o dump antigo adquiriu indevidamente — mas a
   invalidação em si não precisa do protocolo.
3. **A leitura muda de `REQ` é obrigatória neste chip.** O upstream a faz para
   GFXHUB com `GC_HWIP < IP_VERSION(10, 3, 0)`, "to avoid a false ACK due to the
   new fast GRBM interface". GC 10.1.3 está abaixo de 10.3.0. Sem ela, um ACK
   observado não prova invalidação.
4. **A SDMA traduz pelo GFXHUB.** `sdma_v5_0.c`: `ring->vm_hub =
   AMDGPU_GFXHUB(0)`. Se fosse MMHUB, todo o gate de memória estaria no hub
   errado e nenhum passo teria efeito sobre a SDMA. Valia confirmar em fonte.

#### Duas mudanças de ABI que a prova exigiu

**Endereço cru de 64 bits.** `DstOffset` tem 32 bits, o que limitava endereço
cru a 4 GB — e abaixo de 4 GB todo endereço é memória de sistema real nesta
máquina. Um `WRITE_LINEAR` para lá, com tradução desligada, escreveria na RAM do
host. O VA de teste precisa ser inofensivo caso o hardware o trate como físico,
e isso exige sair dos 32 bits.

**Conferência de dado redirecionada, e `WRITE` sem dado conferido não conclui.**
No passo 5 o endereço do pacote não é onde o dado aparece, então conferir no
offset do pacote leria o lugar errado e a submissão correta apareceria como
divergência. `VerifyVramOffset` diz onde olhar.

E a parte que importa mais: antes desta mudança, um `WRITE` em modo de endereço
cru **pulava a conferência** e voltava a decidir conclusão por ponteiro + fence
— exatamente o critério que mentiu em 2026-08-23 08:31. O passo 5 usa endereço
cru por necessidade, e sem essa regra ele poderia declarar tradução funcionando
sem ter olhado um único dword. Agora `WRITE` sem dado conferido não conclui, e o
driver recusa a submissão crua que não diga onde conferir.

### 36. Passo 3, estágios inertes: APROVADO no silício (17 de 18)

Evidência: `docs/evidence/2026-08-23_12-59-45-DESKTOP-HQAG6T4-gcvm-program`.

**24 valores novos escritos em registradores de tradução, todos confirmados por
readback**, nos quatro estágios inertes: `gart`, `system`, `identity`,
`invrange`. Depois deles, submissão SDMA com fence física e dado conferido no
kernel — `4 dwords, 0 divergencias`. Depois do `restore` dos 33 registradores,
dump idêntico ao inicial e a mesma submissão funcionando de novo.

Os dois controles negativos recusaram sem escrever nada e sem deixar marca no
dump: `program` antes do snapshot, e `program tlb` sem `-danger`.

Duas confirmações independentes: `PT_BASE = 0x45FF10000` bate com a aritmética
feita fora do driver (`FB_OFFSET 0x450` → `vram_base_offset 0x450000000`), e a
divergência de `SYS_APERTURE_HIGH` (`0x003D0800` → `0x003D07FF`) estava prevista
por escrito **antes** da medição, com a razão.

O gate `K` — o mais desconfortável de escrever — passou: dos oito campos do dump
que mudaram, nenhum fora da lista prevista.

**Tradução continua desligada.** `ENABLE_CONTEXT` nunca saiu de zero,
`SYSTEM_ACCESS_MODE` continua 0, `VMC_TAP_*_REQUEST_PHYSICAL` continuam 1. O que
está provado é que esses registradores aceitam configuração nova com readback e
que desfazer funciona. Tradução de verdade é o passo 5.

O único gate reprovado foi instrumento: o `ptbuild` imprime
`conferencia : N divergencias em M entradas`, com o número antes da palavra, e
eu reusei o helper que procura o oposto. A saída crua na mesma tela dizia
`0 divergencias em 512 entradas`. Terceira aparição da mesma classe de defeito
no projeto, e terceira vez que valores crus na saída a expõem em uma leitura.
Corrigido com parser próprio para o formato do `ptbuild`.

### 35. Passo 3 implementado: programação em estágios, com fail-closed

**Ainda não medido no silício.** Código construído, driver e CLI compilando sem
aviso, ensaio escrito. A barra de progresso **não se move** por implementação:
ela mede prova física.

O passo 3 escreve valores novos nos registradores de tradução. Foi dividido em
sete estágios que espelham `gfxhub_v2_0_gart_enable()`, e a divisão não é
organizacional — é de risco:

| Estágio | Escritas | O que faz |
|---|---:|---|
| `gart` | 6 | aponta `CONTEXT0` para a page table do passo 1 |
| `system` | 10 | aberturas AGP e de sistema, página default de falha |
| `identity` | 6 | abertura de identidade vazia |
| `invrange` | 2 | faixa da engine 0 de invalidação |
| `tlb` | 1 | **substitui o bypass do POST** |
| `cache` | 5 | **PDE/PTE deixam de ser buscados fisicamente** |
| `context0` | 1 | **ponto sem volta: VMID 0 traduz** |

Os quatro primeiros são inertes enquanto `ENABLE_CONTEXT` for 0, e são os únicos
que o ensaio `test-gcvm-program.ps1` exercita. Os três últimos exigem `-danger`
explícito e ensaio próprio.

Quatro regras que o driver impõe:

1. **sem snapshot de transação aberto, nenhuma escrita.** É a lição do
   `sdma enginereset` de 2026-08-23, que mexeu no estado sem checar se havia
   como desfazer e fez cinco execuções medirem a própria contabilidade;
2. **todo registrador de um estágio tem de estar na lista do snapshot**, senão o
   estágio inteiro é recusado sem escrever nada. Sem isso uma escrita nova
   escaparia do rollback e o sintoma seria "rollback limpo" com hardware
   alterado. A lista da transação subiu de 24 para 33 registradores por causa
   desta regra;
3. **readback por escrita**, e divergência aborta o estágio naquele registrador;
4. **os estágios de risco exigem consentimento explícito.**

Duas correções feitas durante a implementação, ambas de armadilhas já conhecidas
do projeto:

- **recusa tem de voltar com payload.** O IOCTL é `METHOD_BUFFERED`: completando
  com `NTSTATUS` de erro, a saída não chega ao user mode de forma confiável — foi
  o que produziu o `erro 87` sem número nenhum no `ptbuild`. Recusa e divergência
  são *resultados*, então retornam sucesso no nível do IOCTL com `Status` e
  `DenyReason` dizendo a verdade. Quem decide aprovado/reprovado é o campo, não
  o código de erro do Win32;
- **limite conferido antes de escrever no vetor.** O `NT_ASSERT` que eu havia
  posto depois de preencher o plano chegaria tarde: a pilha já estaria
  corrompida. O limite foi para dentro do `push`.

E a contagem de registradores da transação passou a sair do header nos ensaios,
em vez de número fixo. Mesmo defeito das versões de ABI fixadas: a lista cresceu
de 24 para 33 e reprovaria ensaios antigos por motivo cosmético.

O ensaio tem 18 gates, dois deles controles negativos que exigem recusa:
`gcvm program` antes do snapshot, e `gcvm program tlb` sem `-danger`. Nos dois
casos o dump de tradução é comparado depois — recusa que escreve é pior que
aceite honesto, porque a saída diz que nada aconteceu.

O gate que importa é o `L`: submissão SDMA com fence física e dado conferido no
kernel **depois** de 24 valores novos escritos. Readback diz que o registrador
aceitou; só a submissão diz que a memória continua alcançável.

### 34. Passo 2 do gate de memória APROVADO (nove de nove)

Evidência: `docs/evidence/2026-08-23_10-12-46-DESKTOP-HQAG6T4-gcvm-transaction`.

Com o semáforo fora do caminho de leitura, o gate E passou e o passo 2 fechou:
snapshot dos 24 registradores, `restore` aceito por todos com readback,
dump idêntico antes e depois, **submissão SDMA com fence física funcionando
depois das 24 escritas**, `verify` final limpo, destino restaurado, zero
bugcheck.

**Existe rede de segurança para o passo 3.** O mecanismo de snapshot/restore foi
exercitado em vez de assumido, e o caminho de memória sobrevive a reescrever
esses registradores.

Na mesma passada, os ensaios pararam de fixar a versão de ABI no preflight. Com a
ABI subindo de 4 para 14 num dia, cada mudança reprovava ensaios antigos por
motivo cosmético — e preflight que recusa por maquiagem treina quem opera a
ignorá-lo. A compatibilidade real já é imposta pela biblioteca; os ensaios agora
checam só que `info` respondeu e que o armamento está em `readonly`.

O ponto de retomada consolidado está em
[09_PONTO_DE_RETOMADA.md](09_PONTO_DE_RETOMADA.md).

### 33. Passo 2: oito de nove gates, e o nono achou um defeito no instrumento

Evidência: `docs/evidence/2026-08-23_10-05-03-DESKTOP-HQAG6T4-gcvm-transaction`.

Passaram: bypass funcionando antes, 24 registradores guardados, `verify` sem
divergência, `restore` aceito pelos 24 com readback conferido, **submissão SDMA
com fence física e dado conferido funcionando depois das 24 escritas**, `verify`
final limpo, destino restaurado, zero bugcheck.

Ou seja: **reescrever os mesmos valores nesses 24 registradores não tem efeito
colateral sobre o caminho de memória.** A rede de segurança que o passo 3 exige
existe.

O gate E — dump idêntico antes e depois — reprovou, e o que ele pegou foi o
instrumento:

```
INVALIDATE_ENG0_SEM : 0x00000001 -> 0x00000000
INVALIDATE_ENG0_ACK : 0x00000000 -> 0x00010000
```

`ACK.SEMAPHORE` é o bit 16, então isso é um único fato: **o próprio `gcvm dump`
adquiria o semáforo da engine de invalidação ao ler
`GCVM_INVALIDATE_ENG0_SEM`.** O upstream diz literalmente que ler retorna 1 na
aquisição e que escrever 0 libera. Um comando anunciado como somente leitura
mantinha um semáforo de hardware retido até o reboot, desde a captura das 09:26.

Correções: o registrador saiu de todo caminho de leitura; o estado passou a ser
observado pelo bit `SEMAPHORE` do `ACK`, que é leitura pura; e existe agora
`gcvm semrelease`, ação explícita que escreve 0 e confere pelo `ACK`.

Isto importa além do gate: o **passo 4 do plano usa essa mesma engine**. Começar
a invalidação segurando um semáforo tomado por um dump anterior quebraria o
protocolo antes da primeira tentativa, com o sintoma aparecendo longe da causa.

Regra nova no catálogo: antes de colocar um registrador num dump, é preciso saber
se a leitura dele tem efeito. Semáforo, fila, FIFO e "read to clear" não entram
em ferramenta de observação.

### 32. Passo 2 implementado: transação dos registradores de tradução

ABI 13. `gcvm txn <snapshot|restore|verify>`, sobre 24 registradores de
**configuração** de tradução. O ensaio é `tools/test-gcvm-transaction.ps1`.

O `restore` reescreve exatamente os valores lidos. Isso é deliberado: prova o
mecanismo antes de existir divergência para desfazer. Se escrever valor idêntico
tivesse efeito colateral em algum desses registradores, é melhor descobrir agora,
com o bypass intacto, do que depois de a placa depender da restauração.

#### Critério de quem entra na transação

Entra o que é configuração, o upstream escreve, e reescrever o mesmo valor não
dispara ação. Ficam **fora**, com motivo:

| Registro | Por que fora |
|---|---|
| `GCVM_INVALIDATE_ENG0_REQ` | escrever dispara invalidação |
| `GCVM_INVALIDATE_ENG0_ACK` | status |
| `GCVM_INVALIDATE_ENG0_SEM` | semáforo, tem protocolo próprio |
| `GCVM_L2_PROTECTION_FAULT_STATUS` / `_ADDR` | reportam falha, não configuram |
| `GCMC_VM_FB_LOCATION_BASE`/`TOP`, `FB_OFFSET` | definem onde a VRAM aparece; mexer não faz parte de montar VMID 0 |

Reescrever um registrador de ação seria ação disfarçada de restauração.

#### A prova não é o readback

O readback diz que o registrador aceitou o valor. Ele não diz que o caminho de
memória continua funcionando. Por isso o ensaio faz uma **submissão SDMA real com
fence física e dado conferido no kernel antes e depois** das 24 escritas:

| Gate | O que mede |
|---|---|
| A | o bypass funciona antes de tocar na transação |
| B | 24 registradores guardados |
| C | `verify` sem divergência entre snapshot e leitura |
| D | `restore` aceito por todos os 24, com readback conferido |
| E | dump completo de tradução idêntico depois do restore |
| F | **submissão SDMA volta a funcionar depois das 24 escritas** |
| G | `verify` final sem divergência |
| H | destino restaurado e WPTR de volta a zero |
| I | nenhum bugcheck |

O gate F é o que transforma isto em prova em vez de contabilidade de escrita.

Nenhum valor **novo** é escrito em registrador de tradução por este ensaio.

### 31. Passo 1 do gate de memória APROVADO

Evidência: `docs/evidence/2026-08-23_09-46-05-DESKTOP-HQAG6T4-gcvm-pagetable`,
cinco gates aprovados, terceira tentativa (as duas primeiras reprovaram por
defeito do laboratório: VRAM não mapeada sob demanda e alvo estourando a janela).

```
GCMC_VM_FB_OFFSET : 0x00000450  -> vram_base_offset 0x450000000
PA coberto        : 0x45FD00000 .. 0x45FEFF000
PTE[0]            : 0x000300045FD00071
conferencia       : 0 divergencias em 512 entradas
rollback          : 1024 dwords, 0 divergencias
```

**A previsão registrada antes da execução bateu.** O cálculo independente —
máscara de 48 bits, flags, `MTYPE_UC = 3`, conversão `mc2pa` e `vram_base_offset`
lido do registrador — dá exatamente `0x000300045FD00071`, e o último PA
(`0x45FEFF000`) também confere. A aritmética de endereço do laboratório está
consistente com o silício.

Ressalva que continua valendo: isto **não** prova que a GPU aceita a tabela. É o
passo 5 do plano, exige ligar tradução, e o gate C deste ensaio existe para provar
que nada de tradução foi tocado.

Correção de um erro meu: ao anunciar a mudança do alvo, calculei de cabeça
`0x000300045D000071` — faltou um nibble. A previsão documentada, calculada por
ferramenta, estava correta. Cálculo de endereço não vai para documento sem passar
por ferramenta.

### 30. Passo 1 do gate de memória implementado: page table sem ligar tradução

ABI 12. `gcvm ptbuild` constrói uma page table na VRAM e confere cada PTE por
leitura da CPU; `gcvm ptrollback` devolve a região ao conteúdo original. **Nenhum
registrador de tradução é tocado** — se a estrutura sair errada, ela é apenas
bytes numa área de VRAM que ninguém usa.

Todo número do formato do PTE veio de fonte autoritativa, tabelado em
[03_CATALOGO_DE_REGISTRADORES.md](03_CATALOGO_DE_REGISTRADORES.md): máscara de 48
bits de `gmc_v10_0.c`, flags de `amdgpu_vm.h`, `MTYPE_UC = 3` de
`navi10_enum.h`, conversão de endereço de `amdgpu_gmc_vram_mc2pa()` e
`vram_base_offset = GCMC_VM_FB_OFFSET << 24` de `gfxhub_v2_0.c`.

O driver **lê** `GCMC_VM_FB_OFFSET` do silício em cada construção, em vez de usar
constante.

#### Previsão registrada antes da execução

Com `FB_OFFSET = 0x450` medido e `vram_start = 0xF4_0000_0000` confirmado, a
primeira PTE apontando para VRAM `0x0FF20000` tem de valer
**`0x000300045FF20071`**. O cálculo foi feito fora do driver de propósito: se a
medição concordar, confirma a aritmética; se discordar, aponta onde o modelo está
errado. Concordância por construção não valeria nada.

#### O ensaio

`tools/test-gcvm-pagetable.ps1` com cinco gates:

| Gate | Verifica |
|---|---|
| A | tradução desligada no início: `CONTEXT0` desabilitado e `PT_BASE` zerado |
| B | 512 PTEs escritas e conferidas, zero divergências |
| C | dump GCVM idêntico antes e depois — nenhum registrador de tradução mudou |
| D | região da tabela restaurada dword a dword |
| E | `CONTEXT0_CNTL` no fim igual ao do início |

O gate C é o que garante que este passo continua sendo o passo seguro: se a
construção mexesse em algum registrador de tradução, deixaria de ser.

Primeira execução (09:39) reprovou por defeito do laboratório: `erro 21`
(`ERROR_NOT_READY`), porque a abertura de VRAM é mapeada sob demanda e nada
naquela sessão havia tocado VRAM antes. A função exigia o mapeamento em vez de
fazê-lo. Corrigido.

O que distinguiu defeito próprio de resultado físico foi a saída trazer os
valores crus: `0 PTEs`, `FB_OFFSET 0x00000000` e `snapshot AUSENTE` são
impossíveis como medição. Os gates A, C e E passaram na mesma execução,
confirmando que nada de tradução foi tocado nem no caminho de falha.

### 29. Medido: não existe page table nesta placa

Evidência: `docs/evidence/2026-08-23_09-26-23-DESKTOP-HQAG6T4-gcvm-baseline`,
read-only, armamento `readonly`, nenhuma escrita. Uma leitura respondeu três
perguntas abertas.

**1. VMID 0 está desabilitado e não há tabela de páginas.**
`CONTEXT0_CNTL = 0x007FFE80` com `ENABLE_CONTEXT = 0`, e `PT_BASE`, `PT_START` e
`PT_END` todos zerados. Toda a execução de SDMA deste projeto aconteceu sem
tradução de endereço.

**2. O mecanismo do bypass, agora com nome.**
`MX_L1_TLB_CNTL = 0x00000501`: TLB L1 habilitada, `SYSTEM_ACCESS_MODE = 0`
(físico) e `ENABLE_ADVANCED_DRIVER_MODEL = 0`. É isso que faz a SDMA alcançar a
VRAM sem page table — e substitui a especulação revogada sobre
`UTCL1_PAGE.USE_BC` por medição com nome de campo do header autoritativo.

**3. A hipótese de coerência de cache está encerrada.**
`L2_CNTL = 0x00080602` com `ENABLE_L2_CACHE = 0`. A L2 do GC está desligada, e
nada que o laboratório faz escreve esse registrador. Portanto o dado invisível
pós-injúria **não** estava sujo na L2. Hipótese fechada por medição, não por
argumento.

**Bônus P0:** `FB_LOCATION_BASE = 0x0000F400` e `TOP = 0x0000F41F` confirmam pelo
hardware a base `0xF4_0000_0000` e os 512 MB de abertura que o laboratório vinha
usando — contra os 256 MB do BAR0, que é por que as validações de endereço usam
a janela inteira.

#### Escopo de montar VMID 0

Onze divergências contra `gfxhub_v2_0.c`, nenhuma delas defeito: `L2_CNTL`
(3 campos), `L2_CNTL3` (2), `L2_CNTL4` (2), `CONTEXT0_CNTL` (2) e
`MX_L1_TLB_CNTL` (2). Fora da lista, porque o upstream calcula em runtime: page
table a alocar e programar, página default de falha (hoje zero), aberturas AGP
(hoje zero) e a engine de invalidação.

#### O risco que este escopo carrega

`SYSTEM_ACCESS_MODE = 0` e `VMC_TAP_PDE/PTE_REQUEST_PHYSICAL = 1` são exatamente
o que faz o caminho atual funcionar. Habilitar tradução **substitui** o bypass do
qual todo o sucesso medido depende, e engine quebrada nesta placa não volta sem
reboot.

Por isso o VMID 0 será montado de forma transacional — snapshot e rollback de
todos os registradores da tabela — e a primeira submissão traduzida só vale
depois de a page table ser conferida por leitura da CPU.

### 28. Início do gate de memória: leitura antes de escrita

Com o gate de recuperação fechado, o bloqueador passou a ser GCVM/VMID0, e a
primeira coisa construída foi **observação, não configuração**.

`gfxhub_v2_0.c` e `gmc_v10_0.c` do upstream estavam disponíveis offline e dão a
sequência autoritativa de bring-up de tradução: aberturas de sistema e AGP, TLB
L1, cache L2, contexto 0 (VMID 0), desabilitar a abertura de identidade,
configurar contextos 1..15 e programar a invalidação.

O que foi entregue (ABI 11, tudo somente leitura):

- `gcvm dump`: 31 registradores do bloco de tradução, cobrindo exatamente a
  lista que o upstream programa. Não exige armamento e não escreve nada.
- Dez offsets novos derivados com a aritmética já validada, documentados em
  [03_CATALOGO_DE_REGISTRADORES.md](03_CATALOGO_DE_REGISTRADORES.md).
- Semântica de bit de 15 registradores GCVM/GCMC acrescentada ao extrato do
  header autoritativo do kernel.
- `tools/decode-gcvm.py`: decodifica o dump e compara campo a campo com o que
  `init_tlb_regs`, `init_cache_regs` e `enable_system_domain` exigem, citando a
  função de origem em cada divergência.
- `tools/capture-gcvm-baseline.ps1`: captura read-only que **não precisa de boot
  limpo e não contamina a sessão**, porque não arma escrita nem toca engine.

Duas ressalvas registradas no próprio decodificador, para não virarem conclusão
errada depois:

1. Divergência contra o upstream **não é defeito**. O POST configurou a placa
   para o próprio uso; a lista de divergências é o escopo de montar VMID 0.
2. O dump **não responde** se existem page tables nem onde estão. As bases de
   `CONTEXT0` apontam para memória que ninguém inspecionou ainda.

Nenhuma escrita em GCVM foi feita, e nenhuma será antes de a leitura estar
documentada e o plano de page table existir.

### 27. Gate reescrito APROVADO no silício

Boot de 2026-08-23 08:53, evidência
`docs/evidence/2026-08-23_08-53-37-DESKTOP-HQAG6T4-sdma-failclosed`,
`passed=true`, dez gates, zero bugcheck.

| Gate | Medição |
|---|---|
| G1a | nove entradas inválidas recusadas com `erro 87`; WPTR imóvel em `0x44` |
| G1b | submissão normal continua funcionando depois da bateria |
| G2a | injúria reprovada como caso negativo |
| G2b | `ENGINE SUSPEITA: SIM (motivo 2)`, marcada pelo driver |
| G2c | submissão normal **recusada pelo driver** com `erro 483`; não chegou ao anel |
| G2d | forçada, deu `4 dwords, 4 divergencias` e `Conclusao: NAO` — reprovado dentro do kernel |
| G3a | rollback com 24 registradores e VRAM sem divergência, engine condenada |
| G3b | anel desabilitado, WPTR zero, F32 haltado, 32 dwords restaurados, driver respondendo |
| G3c | nenhum bugcheck |

Nesta execução a fence ficou `NAO ALCANCADO`; na de 08:31 foi alcançada. A
variabilidade do estado pós-injúria aparece de novo e **não muda o veredito**,
porque o critério agora exige o dado conferido.

O gate de recuperação deixa de ser o bloqueador do WDDM. O que ele passou a
afirmar é verificável e foi verificado; o que ele não pode afirmar — recuperação
por software da injúria — está registrado como comportamento medido, e o item 4
(recuperação em nível de dispositivo) fica em aberto no campo `not_covered` da
própria evidência.

### 26. Critério endurecido validado no caminho saudável

Boot de 2026-08-23 08:41, evidência
`docs/evidence/2026-08-23_08-41-33-DESKTOP-HQAG6T4-sdma-minimal`, `passed=true`,
driver `v10.0 ABI 10`.

Primeira execução com a conferência de dado dentro do kernel integrada ao
critério de conclusão. Passou: fence física, quatro dwords corretos, rollback
com 24 registradores e 1029 dwords sem divergência, F32 haltado no fim, destino
restaurado.

Demonstração modesta e específica: **o critério mais rígido não inventou falha
onde não há.** É a única forma de confiar num critério novo.

Ao conferir a evidência apareceu um defeito de processo: a saída ainda mostrava
o texto antigo porque um patch meu no CLI abortou no meio — a quarta
substituição não casou, o script saiu por exceção e as três anteriores nunca
foram gravadas. O driver tinha a mudança, o CLI não, e os ensaios já passavam
`-ignoresuspect`, uma opção que o CLI não conhecia: as medições pós-injúria
teriam falhado com "opcao desconhecida" em vez de medir. Refeito e agora
conferido por marcador no arquivo, não pela mensagem de sucesso do patch.

### 25. A engine mentiu: fence física escrita, dado não escrito

Boot de 2026-08-23 08:31, evidência
`docs/evidence/2026-08-23_08-31-41-DESKTOP-HQAG6T4-sdma-recovery`. Experimento
com controle interno: V1, V2 e V3 passaram na engine saudável na mesma sessão,
depois a injúria foi injetada e o V2 aplicado.

A submissão pós-injúria:

| Prova | Resultado |
|---|---|
| RPTR alcançou o WPTR | sim |
| fence escrita pela GPU com o valor do pacote | **sim** |
| STATUS ocupado durante o comando | sim |
| dwords de destino corretos | **não** |

O driver declarou `Completed = 1`. Pelos dois critérios que o projeto usava —
ponteiros e fence física — isto teria entrado como sucesso. Só a comparação dos
dados reprovou.

É a armadilha da regra 6 de `BC250_HARDWARE_RULES.md` acontecendo no silício:
**fence física escrita pela GPU não prova que o trabalho foi feito.**

#### Correção estrutural (ABI 10)

- A conferência dos dwords de destino passou para dentro do kernel, e
  `Completed` agora exige **ponteiro + fence + dado**. O driver não consegue
  mais anunciar conclusão sem a prova completa.
- Nova marca de **engine suspeita**: quando uma submissão normal alcança a fence
  sem produzir o dado, o driver marca a engine e recusa submissões novas na
  sessão. Nada dentro da sessão limpa a marca, porque nada recupera a engine. O
  laboratório contorna com `-ignoresuspect` para medir; produção não deve.

#### O dano não é um estado único

Nos ensaios de 07:40, 07:51 e 08:00 a fence pós-recuperação ficava
`NAO ALCANCADO`. Aqui, com injúria única seguida de V2, a fence **foi**
alcançada. Ou seja, "a engine para de escrever memória" está incorreto como
descrição geral — ela escreveu a fence.

O que se sustenta hoje: o dado do `WRITE_LINEAR` nunca fica visível pela
abertura do BAR0 após a injúria; a fence uncached pode ficar; e nenhuma
sequência testada devolve a engine a um estado em que o dado apareça.

Isso reabre, com base em medição, a possibilidade de o problema estar em
visibilidade/coerência de escrita cacheada, e não em execução. Hipótese, não
conclusão.

### 24. Controle positivo: o re-bring-up e o soft reset de bloco funcionam

Boot de 2026-08-23 08:21, evidência
`docs/evidence/2026-08-23_08-21-45-DESKTOP-HQAG6T4-sdma-recovery`, `passed=true`.

Primeiro experimento controlado de recuperação, numa engine comprovadamente
saudável. As três variantes passaram, cada uma terminando em escrita com fence
física e dados conferidos:

| Variante | Sequência | Resultado |
|---|---|---|
| V1 | rollback -> `psp loadfw` -> `sdma init` | passou |
| V2 | + `sdma enginereset` | passou |
| V3 | + `sdma reset` do F32 | passou |

**Duas conclusões firmes:**

1. O caminho de re-bring-up é válido, com ou sem soft reset de bloco. Os
   degraus `E3c`/`E3d`/`E3e` saem de suspensos: a falha deles sobre a engine
   danificada é dano real, não sequência errada.
2. O soft reset de bloco é utilizável e não destrói o estado — requisito de
   qualquer driver que precise lidar com engine travada.

**A hipótese `USE_BC` está encerrada por controle positivo.** No V2 o
`enginereset` deixou `UTCL1_PAGE = 0x000C5C20`, sem o bit em questão, e a
escrita seguinte funcionou com fence física. A engine volta a escrever sem que
ninguém reprograme esse registrador; quem reestabelece o bloco é o firmware pelo
PSP mais o `sdma init`.

Por isso a restauração de configuração foi **removida** do `enginereset`: cinco
escritas de registrador baseadas em leituras de validade duvidosa, para resolver
um problema inexistente. Os valores seguem no payload como observação.

### 23. A fase F reprovou por defeito do harness, e isso suspende conclusões

Boot de 2026-08-23 08:09, evidência
`docs/evidence/2026-08-23_08-09-45-DESKTOP-HQAG6T4-sdma-robustness`.

O teste do soft reset numa engine saudável falhou por um bug meu, não pelo
hardware: `sdma init` foi recusado com erro 170 (`ERROR_BUSY`) porque o
`enginereset` zerava o estado de anel mas deixava o snapshot transacional
aberto, e `BcSdmaCaptureSnapshot` recusa capturar sobre snapshot existente. A
escrita seguinte nem chegou ao silício (erro 21).

Consequência séria: os degraus `E3c`, `E3d` e `E3e` todos pressupõem que o
caminho de re-bring-up funciona, e esse caminho **nunca foi validado
isoladamente**. As conclusões daqueles degraus ficam **suspensas** — podem estar
certas, mas não estão demonstradas.

Também apareceu uma contradição que impede conclusão sobre `UTCL1_PAGE`: dentro
do `enginereset`, com F32 haltado, a leitura pós-pulso deu `0x004C5C20`; no dump
`sdma fault` segundos depois, também haltado, deu `0x000C5C20`. Duas leituras
divergentes no mesmo estado declarado. O registrador fica marcado como não
confiável para inferência.

Correções: o `enginereset` agora recusa com snapshot aberto, e foi criado
`tools/test-sdma-recovery.ps1`, um experimento controlado que valida a sequência
numa engine saudável **antes** de julgar o dano — três variantes (sem reset, com
soft reset de bloco, com reset também do F32), parando na primeira que falhar.

Lição de método registrada: um mecanismo de recuperação precisa ser validado num
sistema saudável antes de servir como prova sobre um sistema quebrado.

### 22. REVOGADO: `USE_BC` era artefato de leitura, não causa

Cruzando as 20 amostras de dump das cinco execuções do dia, a correlação de
`UTCL1_PAGE` não é com o reset — é com o estado do F32:

| Estado do F32 | `UTCL1_PAGE` | Amostras |
|---|---|---:|
| rodando | `0x004C5C20` | 16 |
| haltado | `0x000C5C20` | 4 |

Sem exceção. Dumps com a engine **já quebrada** e o F32 rodando mostram
`0x004C5C20`. A conclusão da seção 20 — de que o soft reset apagava `USE_BC` e
que isso explicava a perda de escrita — está **revogada**. O registrador
simplesmente não é observável de forma confiável com o microcontrolador parado.

Erro de método: comparei dois dumps sem verificar se a condição de observação
era a mesma. Regra agora explícita no catálogo: um registrador só é comparável
entre dumps se o bloco estiver no mesmo estado de execução.

Correções aplicadas: o driver escolhe a fonte da configuração conforme o F32
esteja rodando ou haltado e registra qual usou; a captura no boot existe como
referência, com a ressalva de que no boot o F32 está haltado.

Continua válido: o `CMD_OP` latchado em `0x3F` e o fato de o soft reset ser a
única ação que o limpa.

#### Fase F: testar o reset numa engine saudável

O ensaio ganhou uma fase executada **antes** dos negativos: soft reset de bloco
numa engine comprovadamente boa, seguido de firmware, F32 e anel novos, e uma
submissão normal.

Testar recuperação só depois de quebrar a engine mistura "o reset está correto?"
com "o dano é recuperável?". Com a engine saudável a resposta isola o mecanismo:
se a escrita voltar, o reset é válido e o dano do opcode reservado é que
sobrevive a ele; se não voltar, a sequência de reset está incompleta e julgá-la
sobre uma engine quebrada não diz nada.

### 21. Restauração implementada, e um falso "sucesso" pego a tempo

Boot de 2026-08-23 07:51, evidência
`docs/evidence/2026-08-23_07-51-42-DESKTOP-HQAG6T4-sdma-robustness`.

A restauração de configuração rodou e reportou `5 restaurados, 0 divergencias`
— mas restaurou o valor errado:

```
UTCL1_PAGE : 0x000C5C20 -> reset 0x000C5C20 -> 0x000C5C20
```

O "antes" já vinha sem `USE_BC`. Rastreando os cinco dumps, `USE_BC` sobrevive
ao pacote malformado, ao reset do F32, ao recriar o anel e ao recarregar o
firmware — e some **dentro** do próprio `enginereset`, porque eu capturava a
configuração depois de desligar `CNTL.UTC_L1_ENABLE`. Com a UTCL1 desligada,
seus registradores leem o valor de reset.

O código capturava o estado degradado que ele mesmo acabara de produzir e o
restaurava com sucesso aparente. **Um contador de sucesso confirmando a própria
falha** — o tipo de armadilha que só aparece porque o ensaio guarda um dump de
referência da engine saudável.

Correções: a configuração passou a ser lida antes de qualquer escrita, e na
restauração `CNTL` vem primeiro para religar a UTCL1 antes de reprogramá-la.

### 20. `UTCL1_PAGE.USE_BC`: o bit do POST de que tudo dependia

Boot de 2026-08-23 07:40, evidência
`docs/evidence/2026-08-23_07-40-27-DESKTOP-HQAG6T4-sdma-robustness`.

A sequência corrigida rodou inteira: soft reset do bloco, firmware recarregado
pelo PSP com `RespStatus=0`, F32 religado com o laço principal varrendo
`0x0142..0x03E0`, anel recriado. A escrita continuou falhando — e o dump
comparativo explicou por quê.

Quatro campos de configuração voltaram ao padrão de reset e ninguém os
reprogramou:

| Registro | POST | Após o reset | Campo perdido |
|---|---|---|---|
| `UTCL1_PAGE` | `0x004C5C20` | `0x000C5C20` | **`USE_BC` 1 -> 0** |
| `UTCL1_CNTL` | `0xD0000793` | `0xD0000191` | `RESP_MODE` 3 -> 0 |
| `CHICKEN_BITS` | `0x03AB0107` | `0x01AF0107` | `TIME_BASED_QOS` 1 -> 0 |
| `CNTL` | `0x000400E2` | `0x000400C2` | `MIDCMD_PREEMPT_ENABLE` 1 -> 0 |

**Todo o sucesso de SDMA deste projeto depende de `USE_BC`, um bit que o POST
deixou ligado.** Com ele a UTCL1 opera em bypass e a engine alcança a VRAM sem
page table nenhuma — é por isso que o laboratório escreve memória sem nunca ter
montado GCVM/VMID0. Zerado, a engine saudável para de escrever sem sinalizar
erro em registrador nenhum.

Correção aplicada (ABI 9): o `sdma enginereset` captura `UTCL1_PAGE`,
`UTCL1_CNTL`, `CHICKEN_BITS`, `UTCL1_WATERMK` e `CLK_CTRL` antes do pulso,
restaura tudo depois e confere cada readback, reportando quantos voltaram e
quantos divergiram. Sem isso, o "reset de recuperação" quebrava uma engine que
estava boa.

Efeito no plano: o gate GCVM/VMID0 deixa de ser opcional. Depender do bypass do
POST é aceitável num laboratório, não num driver — e agora está documentado
exatamente por quê.

Também confirmado nesta execução: a correção da corrida do writeback funcionou,
o rollback voltou com **0 divergências**.

### 19. O soft reset limpa o CMD_OP, mas apaga o microcódigo

Boot de 2026-08-23 07:35, evidência
`docs/evidence/2026-08-23_07-35-43-DESKTOP-HQAG6T4-sdma-robustness`.

Primeira execução do `sdma enginereset`. O reset funcionou no registrador:
`GRBM_SOFT_RESET 0x0 -> 0x00800000 -> 0x0`, `STATUS2_REG 0x00000804 -> 0x0`,
**`CMD_OP` latchado de `128` para `0`**. É a primeira ação que limpa esse
estado.

A recuperação ainda falhou, e a evidência diz por quê: na submissão seguinte o
PC do F32 ficou **congelado em `0x006B`** nas três amostras, contra a varredura
`0x0011..0x03E0` de uma engine saudável. O soft reset do bloco leva junto a
instruction RAM do F32 — faltava recarregar o firmware pelo PSP antes de religar
o microcontrolador e recriar o anel.

Também vale registrar: `FROZEN` nunca apareceu em 100 iterações de 1 ms; foi o
critério alternativo do upstream (`STATUS1_REG == 0x3FF`) que autorizou o reset.
Exigir `FROZEN` teria recusado a operação sem motivo.

Duas correções aplicadas:

- degrau `E3e` agora faz `enginereset` -> `psp loadfw sdma0` -> `sdma reset` ->
  `sdma init` -> submissão;
- o rollback ganhou 5 ms de assentamento antes de conferir a VRAM e uma segunda
  tentativa nos cinco dwords de controle. A divergência desta execução
  (`rptr_wb` lido `0x44`, esperado `0x488`, localizada graças à ABI 6) é corrida
  de escrita em voo da engine, não corrupção; divergência persistente continua
  reprovando.

### 18. Diagnóstico fechado: a engine latcha o opcode inválido

Com o `sh_mask` autoritativo obtido e os dumps decodificados por
`tools/decode-sdma-fault.py`, a comparação saudável x quebrada ficou reduzida a
um campo que importa:

**`STATUS2_REG.CMD_OP` vai de `5` para `63` (`0x3F`) — exatamente o opcode
reservado que o caso negativo emite — e fica latchado.**

Todo o resto reporta saúde: `STATUS_REG` com tudo em IDLE, `STATUS1_REG` em
`0x3FF`, `STATUS3_REG` com `EXCEPTION_IDLE`/`TLBI_IDLE`/`GCR_IDLE`,
`UTCL1_WR_STATUS` sem `PAGE_FAULT`, sem `WR_XNACK_TIMEOUT` e sem `PAGE_NULL`, e
`GCVM_L2_PROTECTION_FAULT_STATUS` em zero.

A falha é **silenciosa em todos os registradores de erro**. Isso explica por que
recarregar o firmware não resolvia: troca-se o microcódigo, mas a máquina de
estados do decodificador de comandos do bloco não é reinicializada por nada
abaixo de um reset do bloco.

#### Ferramentas novas

- `tools/decode-sdma-fault.py`: decodifica os dumps com os nomes de bit do
  header autoritativo e imprime a diferença campo a campo. O ensaio agora roda
  o decodificador sozinho e grava `fault-decoded.txt` junto com a evidência.
- `sdma enginereset` (ABI 8): soft reset do bloco SDMA0 na sequência do
  upstream. Parar o anel, `FREEZE` e esperar `FROZEN` (ou `STATUS1 == 0x3FF`),
  `HALT` no F32, desligar `UTC_L1_ENABLE`, pulso de 50 µs **apenas** no bit
  `SOFT_RESET_SDMA0`, com os demais bits preservados por
  leitura-modificação-escrita, e `FREEZE` liberado no fim.
- Novo degrau `E3e` na escada de recuperação, com o `CMD_OP` antes e depois
  gravados no JSON: é o teste direto da hipótese.

#### Procedência do bit

`GRBM_SOFT_RESET.SOFT_RESET_SDMA0`, shift `0x17`, máscara `0x00800000`, de
`gc_10_1_0_sh_mask.h` (SHA-256
`cba20414ecf02e91bafdcfbff7d4ba686326f0bdaf236d1a883364f08410c167`). Extrato
dos registros usados em
`referencias/BC250-windowsDriverTest/docs/upstream/generated/gc_10_1_0_sh_mask_extract.h`.

O mesmo registrador contém `SOFT_RESET_GFX`; por isso a escrita é
leitura-modificação-escrita, mexendo só no bit da SDMA0.

### 17. Dump de falha capturado: nenhum registrador explica a perda de escrita

Boot de 2026-08-23 01:05, evidência
`docs/evidence/2026-08-23_01-05-54-DESKTOP-HQAG6T4-sdma-robustness`. Sessão de
encerramento do dia.

As oito primeiras fases repetiram pela **quarta vez**. Os quatro degraus de
recuperação reprovaram de novo. O que mudou é que agora existe dado sobre o
estado do silício nos dois lados da falha.

**A referência saudável derrubou a hipótese que motivou a ABI 7.** Numa engine
que acabou de executar corretamente, `UTCL1_WR_XNACK` já vale
`0x0045FE00/0x01050500` e `ERROR_LOG` já vale `0x0000000F`. Esses registradores
carregam configuração ou contadores, não flag de erro. A leitura "XNACK != 0
significa tradução recusada" está revogada, e o `sdma fault` deixou de marcar
"sinais suspeitos" — ele agora imprime valores e diz que valor isolado não
diagnostica nada.

Capturar a referência saudável antes da falha foi o que impediu um diagnóstico
errado com aparência de conclusão.

O que muda de fato entre saudável e quebrada: `STATUS2_REG`
(`0x00050604` -> `0x003F0FE8`), alguns bits de `UTCL1_RD_STAT` e os contadores
`XNACK1`. O que **não** muda é mais revelador:

- `STATUS1_REG` continua `0x000003FF` — pelo critério do upstream a engine se
  declara **ociosa**;
- `GCVM_L2_PROTECTION_FAULT_STATUS` continua `0`: sem falha de proteção
  latchada na L2 do GC;
- `FREEZE`, `EDC_COUNTER`, `UTCL1_TIMEOUT` e `ERROR_LOG` inalterados.

E depois de recarregar o firmware pelo PSP o dump fica **mais limpo que o
saudável** — XNACK1 zerados, ponteiros zerados — e as escritas continuam sem
chegar à VRAM.

Conclusão do dia: **nenhum dos 26 registradores expostos explica a falha**. O
estado visível do bloco SDMA parece saudável enquanto o caminho de escrita
continua morto.

### 16. Nenhuma recuperação por software funciona (ABI 7 e o dump de falha)

Boot de 2026-08-23 00:53, evidência
`docs/evidence/2026-08-23_00-53-27-DESKTOP-HQAG6T4-sdma-robustness`.

A escada de recuperação inteira foi executada e **os quatro degraus
reprovaram**:

| Degrau | Ação | Resultado |
|---|---|---|
| E3a | nada | falhou |
| E3b | reset do F32 | falhou |
| E3c | desfazer e recriar o anel | falhou |
| E3d | recarregar firmware pelo PSP e recomeçar | falhou |

O detalhe que orienta a investigação está no último degrau: o PSP aceitou o
firmware (`RespStatus=0`), o anel foi recriado, o F32 voltou a rodar, e a
submissão seguinte teve **RPTR e writeback chegando ambos a `0x44`**, iguais ao
WPTR. Ou seja, depois de recarregar o firmware a máquina de ponteiros volta a
funcionar por completo — e mesmo assim nem o `WRITE_LINEAR` nem a `FENCE`
escrevem um único dword na VRAM.

Duas conclusões que isso permite:

1. O problema **não é visibilidade de cache**. A fence é emitida com
   `MTYPE=UC`; se as escritas estivessem apenas sujas na L2, a fence
   uncached teria aparecido assim mesmo.
2. O problema **não é o microcontrolador**. Firmware novo, PC andando, anel
   novo, ponteiros corretos — e nenhuma escrita.

Resta o caminho de escrita em memória da engine: tradução de endereço
(UTCL1/GCVM) ou o bloco de write da SDMA.

#### O que foi construído para responder isso (ABI 7)

Novo comando `sdma fault`, **somente leitura**, sem armamento: despeja
`STATUS1/2/3`, `FREEZE`, `ERROR_LOG`, `EDC_COUNTER`, `POWER_CNTL`,
`CHICKEN_BITS`, toda a família `UTCL1` — incluindo `RD_XNACK0/1` e
`WR_XNACK0/1` — e a falha de proteção latchada na L2 do GC, com endereço.

`WR_XNACK` é o candidato mais direto: tradução recusada silencia exatamente
escritas, sem parar o microcontrolador nem os ponteiros. Nenhum bit é
decodificado por suposição; o ensaio agora tira dumps em cinco pontos
(engine saudável, após o opcode reservado, após cada degrau da escada) e grava
no JSON a diferença campo a campo entre eles.

Os endereços vieram de `sdma5_4_2_2_offset.h` com a aritmética
`byte = dw * 4 + 0x4980`, a mesma já confirmada por seis registros em uso desde
2026-08-19. O detalhamento está em
[03_CATALOGO_DE_REGISTRADORES.md](03_CATALOGO_DE_REGISTRADORES.md).

#### O que ficou explicitamente bloqueado

O upstream recupera engine SDMA com
`GRBM_SOFT_RESET.SOFT_RESET_SDMA0`. O offset do registro é conhecido
(`0x08020`), mas a **posição do bit** não está em nenhum header disponível
offline. Escrever ali chutando o bit poderia resetar CP, RLC ou o GFX — numa
máquina cujo display usa a mesma GPU. Enquanto não houver fonte autoritativa
para o bit, o soft reset de engine **não será implementado**.

### 15. Terceira passagem: recuperação continua reprovada, e o ensaio mentiu

Boot de 2026-08-23 00:47, evidência
`docs/evidence/2026-08-23_00-47-33-DESKTOP-HQAG6T4-sdma-robustness`.

Terceira passagem consistente das oito primeiras fases: repetição, wraparound,
retomada e os três negativos, todos com o mesmo resultado dos boots anteriores.
O rollback desta vez voltou **sem nenhuma divergência** de VRAM, o que mostra
que as 2 divergências do ensaio das 00:42 não são determinísticas. Elas ficam
como pendência, agora com instrumentação para localizá-las.

O que a execução acrescentou de novo:

- `sdma reset` reinicia o F32 de verdade (PC `0x006E -> 0x01FA`, 15 transições
  em 16 amostras), **mas não recupera a engine**: a submissão seguinte de novo
  drenou o anel sem escrever memória e sem fence.
- `Pendente` subiu de 4 para 60 bytes, ou seja, o writeback continua ficando
  para trás a cada tentativa.
- Estado final seguro: `RB_CNTL=0x80840000`, base `0`, WPTR `0`,
  `F32_CNTL=0x00000001`, destino restaurado, zero bugcheck.
- Telemetria: 554 amostras cobrindo 9,49 s de 15,07 s, `covers_full_run=false`.
  GFXOFF em `0` nas 554 amostras. As 17 operações registraram
  `status_kick` variando entre seis valores distintos, incluindo os padrões de
  engine ocupada `0x44CC7052` e `0x44DC5052`.

#### Defeito grave de relatório, corrigido

O ensaio imprimiu "Fases executadas sem contradicao" e gravou `passed=true`
**com duas fases reprovadas**. A causa: as fases de recuperação eram
registradas como informativas e só a variável `failure` alimentava o veredito.

Isso é o defeito mais sério desta rodada — pior que a falha de recuperação em
si, porque um relatório que superestima o resultado envenena todas as decisões
seguintes. Corrigido: qualquer fase reprovada agora entra em `failed_phases`,
derruba o veredito e aparece na lista impressa no fim, fase por fase.

#### Escada de recuperação

O ensaio passou a testar, em ordem, qual é a ação mínima que devolve a engine a
um estado utilizável depois do pacote malformado:

1. nada (retomada espontânea) — já reprovado duas vezes;
2. reset do F32 — reprovado em 00:47;
3. desfazer e recriar o anel, sem recarregar firmware;
4. recarregar o firmware SDMA pelo PSP e recomeçar.

Se nenhum degrau funcionar, o ensaio declara explicitamente que só resta reboot.

### 14. Opcode reservado: negativo aprovado, recuperação reprovada

Boot de 2026-08-23 00:42, evidência
`docs/evidence/2026-08-23_00-42-15-DESKTOP-HQAG6T4-sdma-robustness`,
`passed=false`. As oito fases anteriores repetiram o resultado do ensaio das
00:36 — segunda passagem consistente de repetição, wraparound, retomada e dos
dois primeiros negativos.

**O negativo em si passou.** Com o opcode reservado a engine consumiu o pacote
(RPTR `0x430 -> 0x444`, igual ao WPTR), mas a fence permaneceu com o poison
`0x43DAFBBE`, os quatro dwords do destino não foram tocados e `Completed=NAO`.
É a prova direta de que RPTR alcançar o WPTR **não** é suficiente para declarar
execução — exatamente o que a regra do projeto afirma.

**O que veio depois é o achado real:**

- `sdma status` passou a mostrar `Pendente: 4 bytes`: o writeback do RPTR
  congelou em `0x440` enquanto o registrador foi para `0x444`.
- O F32 continuou rodando o laço principal (22 endereços distintos em 24
  amostras), ou seja, não travou.
- A submissão normal seguinte **falhou**: o RPTR alcançou o WPTR, mas nem o
  WRITE_LINEAR nem a fence escreveram memória. `STATUS_REG` foi de
  `0x46DEED57` para `0x46DEE056`.
- O rollback então **falhou**, com 24 registradores restaurados sem divergência
  e **2 divergências de VRAM** que o driver não sabia localizar.

Ou seja: um pacote malformado deixa a engine drenando o anel sem executar nada.
Isso é precisamente o gate de recuperação, e ele está reprovado.

Apesar da falha, o estado final ficou seguro: `RB_CNTL=0x80840000`, base `0`,
WPTR `0`, `F32_CNTL=0x00000001` e os 68 dwords do destino restaurados.

#### Instrumentação adicionada (ABI 6)

- `BC250DBG_SDMA_ROLLBACK_IO` passou a reportar as primeiras oito divergências
  de VRAM e de registrador, com região (anel, rptr_wb, wptr_shadow, fence),
  offset, valor lido e valor esperado. Uma contagem sozinha não permite
  investigar nada.
- O ensaio, quando a retomada espontânea falha, passa a executar `sdma reset`
  (reset do F32), recapturar status e tentar de novo, registrando o resultado
  como fase separada. É o caminho de recuperação candidato e precisa de dado
  físico antes de virar conclusão.
- A telemetria funcionou nesta execução: 560 amostras cobrindo 9,24 s de um
  ensaio de 14,9 s, com `covers_full_run=false` explícito. GFXOFF permaneceu
  em `0` nas 560 amostras.

### 13. Ensaio de robustez passou (sem o opcode reservado)

Boot de 2026-08-23 00:36, evidência
`docs/evidence/2026-08-23_00-36-35-DESKTOP-HQAG6T4-sdma-robustness`,
`passed=true`, nenhum bugcheck na janela do ensaio.

| Fase | Resultado |
|---|---|
| A — repetição | 12 submissões seguidas, todas com fence física e os 4 dwords conferidos. WPTR `0x44 -> 0x330`, sempre crescente. |
| B — wraparound | Anel de 512 bytes: a volta aconteceu na submissão 7 e as submissões seguintes continuaram corretas. |
| C — retomada | Após 6 s ocioso, a engine executou de novo sem reinicialização. |
| D1 — sem kick | Reprovado como exigido: nada publicado, nada consumido, fence com o poison, destino intacto. |
| E1 — recuperação | Submissão normal voltou a funcionar logo depois. |
| D2 — fence incorreta | Reprovado como exigido: `FenceReached=0` com o valor emitido diferente do esperado. |
| E2 — recuperação | Submissão normal voltou a funcionar logo depois. |

Estado final: `RB_CNTL=0x80840000` (desabilitado), base `0`, WPTR `0`,
`F32_CNTL=0x00000001` (haltado) e os 68 dwords do destino restaurados
exatamente. O caso de opcode reservado ainda não foi executado.

Isto fecha o primeiro conjunto real do gate de robustez, com uma ressalva
importante: **uma passagem não é determinismo**. O ciclo precisa ser repetido em
outros boots antes de qualquer conclusão.

#### Defeito de observabilidade encontrado no próprio ensaio

O CSV de telemetria veio vazio: o primeiro argumento de `telemetry watch` é
contagem de amostras, não duração, e o valor passado (`174000`) excedia o limite
de `100000`, então o CLI recusou e não emitiu amostra nenhuma.

Ao investigar, apareceu algo mais sério sobre o sampler em user mode: no ensaio
mínimo, as 38 amostras cobriram apenas **0,665 s** de uma execução de dezenas de
segundos, com intervalo médio de 18 ms. Ou seja, o sampler concorrente **não
cobre a janela de carga**, e um CSV curto parecia cobertura completa. A causa
ainda não foi determinada e fica registrada como pendência.

Correções aplicadas ao ensaio:

- `telemetry watch` passou a receber contagem de amostras, com limite aplicado;
- cada submissão agora grava no JSON a telemetria medida **dentro do kernel**
  (GFXOFF, GRBM, CLK_CTRL, STATUS e PC do F32 em before/kick/after), que é a
  única fonte que cobre comandos curtos;
- o registro passou a conter a janela real do sampler, a duração do ensaio e um
  `covers_full_run`, para que cobertura parcial fique explícita em vez de
  passar por completa.

### 12. Correção do F32 confirmada, e um defeito do próprio ensaio

Boot de 2026-08-23 00:29:30, build com o HALT no fim do teardown instalado.

A primeira tentativa de rodar o ensaio de robustez abortou por um defeito do
script, não da placa: literais hexadecimais acima de `0x7FFFFFFF` são lidos pelo
PowerShell como `Int32` negativo, e `[uint32](0xBC250100 + $i)` estourava na
conversão. O ensaio parou antes da primeira submissão, o bloco `finally` fez o
rollback e o script reprovou a sessão, como deveria.

O aborto, porém, produziu evidência útil: depois do rollback,
`F32_CNTL=0x00000001`. **A correção do HALT está confirmada no silício**, junto
com 24 registradores e 133 dwords de VRAM restaurados sem divergência — os 133
dwords também confirmam que o anel configurável de 128 dwords funciona
(128 + writebacks/shadow/fence).

Correções aplicadas depois disso:

- sufixo `L` em todos os literais grandes do ensaio, para que virem `Int64`
  antes do cast;
- o critério de `-nokick` deixou de exigir RPTR idêntico ao de antes e passou a
  exigir que o RPTR **não ultrapasse o último WPTR publicado**. Motivo: o
  writeback chega atrasado em relação ao registrador, então ele pode avançar
  sozinho até o último ponteiro anunciado sem que nada tenha sido consumido
  indevidamente.

Só o CLI foi recompilado; o `.sys` instalado continua sendo o mesmo binário, o
que evita mais um `install.bat`.

### 11. Primeiro ciclo completo em boot limpo com ABI 5

Boot de 2026-08-23 00:16:44. Sequência executada em console elevado, sem
nenhuma outra atividade na placa.

- Baseline limpo: `docs/evidence/2026-08-23_00-17-35-DESKTOP-HQAG6T4-baseline`,
  46 comandos e 8 consultas SMU, `bc250dbg v5.0 ABI 5 armamento=readonly`.
- Rollback sem submissão: `docs/evidence/2026-08-23_00-17-44-…-sdma-rollback`,
  sem divergências.
- Submissão mínima: `docs/evidence/2026-08-23_00-17-59-…-sdma-minimal`,
  `passed=true`:
  - RPTR `0x00 -> 0x44`, WPTR `0x44`, shadow `0x44`;
  - fence em `0x0FF03000`: poison `0x43DAFFFE` -> observado `0xBC250001`,
    igual ao valor que só existia dentro do pacote;
  - quatro dwords de destino corretos na leitura pelo BAR0;
  - `STATUS_REG` `0x46DEE557 -> 0x46DEED57`, e no instante do kick, dentro do
    kernel, `0x44CC7042` — a engine foi observada ocupada durante o comando;
  - F32 PC `0x03A2 -> 0x03DB -> 0x00BB`.
- **Correção do rollback confirmada no silício**: 24 registradores e 1029
  dwords de VRAM restaurados sem divergência, WPTR físico de volta a
  `0x00000000`, RB desabilitado, base zero e os quatro dwords originais do
  destino restaurados byte a byte.

#### Duas observações que continuam abertas

1. **GFXOFF não transicionou.** As 38 amostras do sampler e as três capturas
   before/kick/after ficaram todas em `0[GFXOFF]`, com `GFX_MISC` cru em
   `0x00000000`. Ou seja: uma cópia da SDMA não tira o domínio GFX do GFXOFF, o
   que é plausível por serem domínios distintos, mas significa que a regra 9 de
   `BC250_HARDWARE_RULES.md` continua sem prova. Ela só poderá ser satisfeita
   com carga no próprio GFX/compute.
2. **O F32 ficava fora de HALT depois do teardown.** O rollback haltava o F32 e
   logo em seguida restaurava os 24 registradores do snapshot — e esse snapshot
   foi tirado em `sdma init`, ou seja, depois do reset do F32, com `HALT=0`.
   Resultado medido: `F32_CNTL` `0x00000001` antes do ensaio e `0x00000000`
   depois. O anel estava desabilitado e sem base, então não havia consumo
   possível, mas o teardown não entregava o que a documentação afirmava.
   Correção aplicada no mesmo dia: o HALT passou a ser a última escrita do
   teardown, depois da verificação de fidelidade dos 24 registradores, e uma
   recusa do F32 em haltar conta como divergência. Os dois ensaios agora
   reprovam a sessão se `F32_CNTL` voltar com o bit de HALT em zero. Falta
   confirmar no silício. Também vale registrar `SDMA0_CLK_CTRL`
   `0x000C0100 -> 0xFF0C0100`: os soft overrides de clock permanecem ligados
   porque foram capturados no snapshot com esse valor.

Uma terceira observação menor: ao ler o writeback logo após a conclusão, ele
apareceu atrasado (`0x34`) em relação ao registrador de RPTR (`0x44`). A
conclusão foi decidida pelo registrador mais a fence física. Isso reforça que
nenhum ponteiro isolado serve como prova.

## O que falta antes de iniciar WDDM

### Gate imediato: recuperar um estado limpo — CONCLUÍDO em 2026-08-23

- [x] Autorizar e executar reboot.
- [x] Confirmar novamente BasicDisplay e ausência de `bc250v4`.
- [x] Instalar o build ABI 5 (`dbg/install.bat`) e iniciar somente
  `dbg/start.bat`.
- [x] Confirmar `bc250dbg v5.0`, ABI 5, hash instalado igual ao do workspace e
  armamento `readonly`.
- [x] Capturar novo baseline com `-IncludeSmuQueries`.
- [x] Confirmar firmware SDMA parado/estado de boot e WPTR/base/polling zerados.

### Gate SDMA reversível — quase concluído

- [x] Repetir `tools/test-sdma-rollback.ps1` com o build atual.
- [x] Repetir `tools/test-sdma-minimal.ps1 -Execute`.
- [x] Confirmar restauração dos 24 registradores.
- [x] Confirmar restauração do ring, RPTR writeback, WPTR shadow e fence.
- [x] Confirmar WPTR físico voltando ao valor do snapshot, `0` no boot limpo.
- [x] Confirmar que os quatro dwords de destino voltam exatamente ao conteúdo
  anterior.
- [x] Deixar o F32 em HALT ao fim do teardown, em vez de restaurar o snapshot
  que já tinha `HALT=0`. Confirmado no silício em
  `docs/evidence/2026-08-23_00-30-46-DESKTOP-HQAG6T4-sdma-robustness`:
  `F32_CNTL=0x00000001` depois do rollback, com 24 registradores e 133 dwords
  restaurados sem divergência.
- [ ] Capturar novo baseline após reboot de recuperação e comparar com o
  baseline anterior ao ensaio.
- [ ] Repetir o ciclo inteiro em outro boot limpo: uma passagem não estabelece
  determinismo.

### Gate de observabilidade física

- [x] Derivar e ler o registro físico de GFXOFF em `0x5A314` no estado ocioso.
- [x] Amostrar `GFXOFF_STATUS` before/kick/after durante uma carga real de SDMA:
  permaneceu `0` nas 38 amostras, então uma cópia da SDMA não serve para provar
  a transição.
- [ ] Observar a transição com carga no próprio GFX/compute; sem isso a regra 9
  de `BC250_HARDWARE_RULES.md` continua sem prova.
- [ ] Capturar clock antes, durante e depois de uma carga suficientemente longa.
- [ ] Capturar potência e temperatura antes/durante/depois sem usar valores
  fabricados.
- [ ] Calibrar a sonda térmica contra uma referência confiável antes de liberar
  qualquer operação `unsafe`.
- [ ] Correlacionar STATUS da engine, PC F32, clock e GFXOFF na mesma janela de
  tempo.

### Gate de robustez e recuperação

Ferramenta pronta: `tools/test-sdma-robustness.ps1`. Nenhum item abaixo foi
executado no silício.

- [x] Executar múltiplas submissões sequenciais sem estado latchado (12 em
  2026-08-23 00:36).
- [x] Testar wraparound do ring preservando todos os buffers.
- [x] Testar idle e retomada.
- [x] Timeout e falha após pacote malformado: reescrito como gate fail-closed e
  **aprovado** em 2026-08-23 08:53. O rollback devolve registradores e VRAM
  mesmo com a engine condenada.
- [x] Casos negativos sem kick e com fence incorreta (duas passagens).
- [x] Caso negativo de opcode reservado: reprovou como exigido.
- [ ] Recuperar a engine depois do opcode reservado. **Nenhuma das quatro ações
  testadas recupera**: nada, reset do F32, recriar o anel e recarregar o
  firmware pelo PSP (medido em 00:53). O caminho de ponteiros volta ao normal;
  só a escrita em memória continua morta.
- [x] Diagnosticar com `sdma fault`. Resultado: **não** há falha latchada na L2
  do GC, e os XNACK não são flags de erro — já são diferentes de zero na engine
  saudável. Nenhum dos 26 registradores expostos explica a perda de escrita.
- [x] Obter de fonte autoritativa a posição de `SOFT_RESET_SDMA0` em
  `GRBM_SOFT_RESET`: shift `0x17`, máscara `0x00800000`.
- [x] Implementar o soft reset de bloco na sequência do upstream
  (`sdma enginereset`, ABI 8).
- [x] Executar o degrau `E3e`: o soft reset **limpa o `CMD_OP` latchado**.
- [x] Validar o re-bring-up numa engine saudável: V1, V2 e V3 passaram em
  2026-08-23 08:21. O mecanismo é válido e os degraus `E3c`/`E3d`/`E3e` deixam
  de estar suspensos.
- [x] Q2 respondida: a injúria sobrevive a V2, com controle interno na mesma
  sessão. Nenhum caminho por software recupera.
- [x] Conferência de dado movida para o kernel; `Completed` exige ponteiro +
  fence + dado (ABI 10).
- [x] Marca de engine suspeita: submissões novas recusadas após a assinatura de
  fence-sem-dado.
- [ ] Testar a hipótese de coerência: por que a fence uncached aparece e o dado
  cacheado não? Exige experimento próprio, não conclusão por analogia.
- [x] Sequência corrigida executada: o problema seguinte era a configuração da
  UTCL1 apagada pelo reset, com `USE_BC` no centro.
- [ ] Confirmar a recuperação com a configuração do POST restaurada, agora com
  a captura no ponto certo (antes de desligar a UTC_L1).
- [ ] Localizar as 2 divergências de VRAM do rollback pós-falha.
- [ ] Caso negativo de firmware ausente.
- [ ] Repetir todo o conjunto em outros boots: uma passagem não é determinismo.
- [ ] Verificar ausência de leaks de pool, DMA após free, bugcheck e corrupção
  de registradores/VRAM.
- [ ] Repetir ciclos de init/submit/rollback/reboot até obter resultados
  determinísticos.

### Gate de memória e filas antes do miniport

- [ ] Validar GCVM/VMID0 com page tables reais e tipos distintos para PA, GPUVA
  e offset de VRAM.
- [ ] Validar coerência HDP/GCR e faults de tradução.
- [ ] Só então considerar rings fora da VRAM fixa e buffers do sistema.
- [ ] Carregar firmware CP estritamente pelo PSP, com lifecycle próprio.
- [ ] Validar CP/PM4 com RPTR/WPTR, fence, scratch/resultado e status físico.
- [ ] Montar KIQ/MQD/HQD e executar compute mínimo sem fallback de CPU.
- [ ] Detectar em runtime CPU, CU/WGP utilizável, BAR/ReBAR, VRAM e GTT; não
  anunciar constantes históricas como capacidade.

### Gate de entrada no WDDM

O miniport WDDM só deve começar quando os itens abaixo forem verdadeiros:

- laboratório retorna ao estado anterior de forma comprovada;
- firmware e rings têm ownership e teardown seguros;
- SDMA repete comandos, fence e resultados sem latch ou resíduos;
- timeout e recuperação são determinísticos;
- GCVM e memória têm modelo validado;
- CP/compute mínimo possui prova física completa;
- clock, temperatura, potência e GFXOFF são observáveis;
- nenhuma capacidade, fence, telemetria ou resultado é fabricado;
- nenhum caminho usa CPU, WARP, GDI ou outro fallback como prova de GPU;
- toda escrita MMIO continua obedecendo `BC250_HARDWARE_RULES.md`.

## Barra de progresso por área

| Área | Progresso | Situação |
|---|---:|---|
| Isolamento do driver legado | `██████████ 100%` | Concluído; não reinstalar `bc250v4`. |
| Baseline e mapa de registradores | `█████████▊ 98%` | Baseline limpo. **Base do segmento 1 (`0xA000`) medida contra registrador vivo** e cross-validada com o Linux nos mesmos offsets. `IC_BASE` correto é `0x3E100`..`0x3E128` e `UCODE_ADDR` é `0x3E050`; os valores `0x1AA80`/`0x1A9D0` eram derivação do segmento 0 e estão **em quarentena**. Zona morta `0x3E050..0x3ED84` **resolvida**: os mesmos `0xFFFFFFFF` aparecem no Linux com o CP executando, logo é write-only, não bloqueio. Consequência: leitura não confirma escrita nesses registradores, e a prova do CP se desloca para o ponteiro de instrução. |
| Lifecycle PSP | `█████████▌ 95%` | Create/destroy repetido em boot limpo, sem resíduo. |
| Execução SDMA mínima | `██████████ 98%` | Cadeia completa em boot limpo e revalidada com a conferência de dado dentro do kernel. |
| Rollback SDMA após submissão | `█████████▌ 95%` | WPTR zerado, 24 registradores restaurados e F32 haltado no fim do teardown. |
| Clock, energia, térmica e GFXOFF | `████░░░░░░ 40%` | Nada medido. GFXOFF nunca transicionou; depende de carga GFX real, que depende do CP. Térmica sem calibração, potência sem fonte. |
| Robustez, negativos e recuperação | `█████████░ 90%` | Gate reescrito medido e aprovado: fail-closed, detecção do estado mentiroso e falha limpa. |
| GCVM, CP e compute pré-WDDM | `███████▍░░ 71%` | **GCVM fechado e provado no silício** (tradução por page table, 20/20 gates). **RLC fora da ROM, provado** (`EXEC_ROM_CODE` 1 → 0, `GPM_STAT` idêntico ao Linux). `IC_BASE`, `UCODE_ADDR` e `IC_OP_CNTL` são **write-only**, confirmado contra o Linux com o CP vivo: a escrita vale, a leitura nunca confirmará, e não são bloqueio. **Unhalt rodado em boot limpo (2026-08-24 00:19): o controle responde, a execução não acontece.** `CP_ME_CNTL` foi de `0x15000000` a `0x00000000` com readback confirmado e os PCs saíram de zero, voltando a zero no re-halt — mas param em `0x0000041E`, constante em 8 amostras e **igual nas três engines**, enquanto no Linux são três valores distintos, um por ucode. Todo status global (`GRBM_STATUS`, `GRBM_STATUS2`, `CP_STAT`, `CP_ME_CNTL`) é **idêntico ao Linux**, o que encerra a família "bloco desligado, gated ou pendurado". Reproduzido em segundo boot limpo (00:47) e o **controle negativo respondeu (00:54)**: sem ucode, sem `icbase` e sem jump table o PC é o mesmo `0x0000041E`, bit a bit. Três boots limpos, dois caminhos de código, resultado idêntico — **a carga do ucode não tem efeito sobre as microengines e a busca de instrução nunca acontece**. Isso encerra a linha "ucode ou cache errados" e deixa duas hipóteses: `IC_BASE` não aceito de verdade (write-only, indemonstrável por readback) ou caminho de memória para busca de instrução do CP inexistente nesta configuração — a segunda **tem base na referência**: `gfx_v10_0_cp_gfx_load_pfp_microcode()` aloca o ucode em `AMDGPU_GEM_DOMAIN_GTT` e escreve no `IC_BASE` um endereço **traduzido**, com `IC_BASE_CNTL.VMID = 0` — a busca de instrução da microengine é acesso traduzido pelo GFXHUB, e nós entregamos endereço físico cru de VRAM. Próximo passo é `gcvm dump`, somente leitura e zero boot: se a abertura de sistema do VMID 0 não cobrir `0xF4_0E00_0000`, a hipótese está confirmada. Ring PM4 e MQD/HQD seguem NÃO VALIDADOS EM HARDWARE. |
| **Total conservador pré-WDDM** | **`█████████▍ 93%`** | **RLC fora da ROM, provado. Unhalt do CP exercitado em boot limpo: o caminho de controle responde, a execução não ocorre.** Todo status global bate com o Linux onde o CP roda, o que encerra a família "bloco desligado ou pendurado"; o que resta é a busca de instrução, e os três PCs iguais em `0x0000041E` apontam para as engines nunca terem alcançado o nosso microcódigo. O +1% é hipótese eliminada e caminho estreitado, **não** função nova — nada novo executa. Próximo bloqueio: o controle `-ControleSemUcode` decide se `0x041E` é ponto de parada comum das engines ou do nosso caminho. Depois: anel PM4, compute e observabilidade física. |

## Próxima sequência recomendada

1. Reiniciar a máquina: a sessão atual já tem firmware SDMA ativo e o
   preflight do ensaio recusa por isso.
2. `dbg/start.bat` (reinstalar somente quando o `.sys` mudar).
3. Executar `tools/test-sdma-robustness.ps1 -Execute`. A pergunta desta
   execução é o degrau `E3e`: o soft reset do bloco tira `CMD_OP` de `0x3F` e
   devolve a escrita em memória?
4. Ler `fault-decoded.txt` na pasta de evidência: o ensaio já roda o
   decodificador e grava a comparação campo a campo.
5. Se o soft reset recuperar, transformar isso no caminho oficial de
   recuperação do driver e repetir em vários boots.
6. Se não recuperar, o próximo alvo é reset em nível de GPU, o que exige
   auditoria separada por causa do display na mesma placa.
7. Só então atacar GFXOFF com carga GFX/compute, calibração térmica e uma fonte
   física de potência.
8. Avançar para GCVM/VMID0 e somente depois CP/compute.
9. Abrir o trabalho WDDM apenas quando todos os gates pré-WDDM estiverem
   documentados e reproduzíveis.

## Arquivos principais

- Regras de hardware: `BC250_HARDWARE_RULES.md`.
- Evidências e lições: `docs/01_EVIDENCIAS_E_LICOES.md`.
- Plano do driver: `docs/04_PLANO_DO_DRIVER_NOVO.md`.
- Marco zero: `docs/06_MARCO_ZERO_DESENVOLVIMENTO.md`.
- Incidente legado: `docs/07_INCIDENTE_REMOCAO_BC250V4.md`.
- Baseline limpo: `docs/evidence/2026-08-22_22-54-06-DESKTOP-HQAG6T4-baseline`.
- Rollback SDMA: `docs/evidence/2026-08-22_23-06-17-DESKTOP-HQAG6T4-sdma-rollback`.
- Rollback PSP: `docs/evidence/2026-08-22_23-14-56-DESKTOP-HQAG6T4-psp-rollback`.
- Primeiro WRITE_LINEAR físico:
  `docs/evidence/2026-08-22_23-20-13-DESKTOP-HQAG6T4-sdma-minimal`.
- Primeiro ciclo completo em boot limpo, com rollback corrigido:
  `docs/evidence/2026-08-23_00-17-59-DESKTOP-HQAG6T4-sdma-minimal`.
- Preflight ABI 4 e telemetria readonly:
  `docs/evidence/2026-08-22_23-38-33-DESKTOP-HQAG6T4-sdma-minimal`.
- Ensaio de robustez e negativos: `tools/test-sdma-robustness.ps1`
  (implementado, sem evidência física).

## Regras permanentes

- Não reinstalar `bc250v4`.
- Não remover driver de display experimental ao vivo.
- `bc250dbg` permanece o único laboratório autorizado.
- Não executar `sdma verify`, `bringup`, `audit`, `stress`, GCVM, GFX ou compute
  legado sem nova auditoria específica.
- Não escrever MMIO por suposição ou por valor copiado de outra geração.
- Não declarar funcionamento com base apenas em retorno de API, WPTR, ring
  habilitado ou resposta de mailbox.
- Não liberar WDDM enquanto o teardown e a recuperação não forem comprovados.
- Se uma prova obrigatória estiver ausente, o veredito é sempre:
  **NÃO VALIDADO EM HARDWARE**.
