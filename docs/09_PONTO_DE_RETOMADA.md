# Ponto de retomada

Escrito em 2026-08-23, ao encerrar a sessão de trabalho. Este arquivo é
auto-contido: dá para retomar o projeto lendo só ele, e depois aprofundar nos
outros documentos.

Veredito global do projeto: **NÃO VALIDADO EM HARDWARE**.

Progresso conservador pré-WDDM: `██████████████████▋░ 93%`

---

## 1. O que a máquina é, agora

| Item | Estado |
|---|---|
| Placa | AMD BC-250, `1002:13FE`, revisão `00` |
| Binding de vídeo | `BasicDisplay` (Microsoft Basic Display Adapter) |
| Driver legado `bc250v4` | removido offline; **não reinstalar** |
| Laboratório | `bc250dbg`, ABI 20, `DEMAND_START`, armamento `readonly` por padrão |
| VRAM: base de GPU | `0xF4_0000_0000`, **confirmada pelo hardware** (`FB_LOCATION_BASE/TOP`) |
| VRAM: abertura | 512 MB no FB, 256 MB visíveis pelo BAR0 |
| Tradução de endereço | desligada em repouso, mas **provada funcionando** (2026-08-23 13:49) |
| Como a SDMA alcança memória | bypass do POST: `MX_L1_TLB_CNTL.SYSTEM_ACCESS_MODE = 0` |
| L2 do GC | desabilitada (`L2_CNTL.ENABLE_L2_CACHE = 0`) |
| Potência | indisponível; nenhum valor é estimado |
| Térmica | sem calibração; `unsafe` bloqueado |

Regra operacional que custou caro aprender: **firmware SDMA carregado só sai com
reboot.** Cada ensaio que carrega firmware custa um ciclo de reinicialização, e
o preflight dos ensaios recusa sessão suja.

---

## 2. O que está provado no silício

Cada item abaixo tem pasta de evidência em `docs/evidence/`.

### SDMA — execução

- Firmware `cyan_skillfish2_sdma.bin` aceito pelo PSP (`RespStatus=0`), F32
  saindo de HALT e varrendo o laço principal.
- `WRITE_LINEAR` com **fence física escrita pela GPU** (poison trocado por valor
  que só existia dentro do pacote) e dwords conferidos.
- 12 submissões sequenciais, wraparound de anel, retomada após ocioso — mesmo
  resultado em quatro boots.
- Rollback transacional: 24 registradores, ring, writeback, shadow e fence
  restaurados; WPTR de volta a zero; F32 haltado no fim.

### SDMA — negativos, todos reprovando como exigido

| Caso | Comportamento medido |
|---|---|
| sem kick | nada publicado, nada consumido, fence com poison, destino intacto |
| fence incorreta | GPU escreve, valor difere do esperado, `FenceReached = 0` |
| opcode reservado | engine consome o pacote, RPTR alcança WPTR, **nada é executado** |

### O achado mais importante do projeto

**Fence física escrita pela GPU não prova que o trabalho foi feito.** Medido em
2026-08-23 08:31: após a injúria de opcode reservado, uma submissão teve RPTR
alcançando WPTR, fence escrita com o valor correto, STATUS mostrando engine
ocupada — e os dwords de destino inalterados. O driver havia declarado
`Completed = 1`.

Correção estrutural: a conferência do dado passou para dentro do kernel, e
`Completed` agora exige **ponteiro + fence + dado**.

### Gate de recuperação (reescrito e aprovado)

A injúria de opcode reservado **não tem caminho de volta por software**. Testado
em seis boots: nada, reset do F32, recriar o anel, recarregar firmware pelo PSP,
soft reset do bloco via `GRBM_SOFT_RESET.SOFT_RESET_SDMA0`. Nenhum recupera.

Controle positivo (2026-08-23 08:21) provou que a sequência de re-bring-up é
válida numa engine saudável — inclusive com soft reset de bloco. Portanto a
falha sobre engine danificada é dano real, não sequência errada.

O gate foi reescrito em requisitos verificáveis e **aprovado em 08:53**, dez
gates: fronteira fail-closed com nove entradas inválidas recusadas e WPTR imóvel,
detecção do estado mentiroso com recusa de submissão pelo próprio driver, e falha
limpa com rollback íntegro sobre engine condenada.

### GCVM — gate de memória

- Estado de tradução medido e decodificado; escopo de VMID 0 quantificado em
  **11 divergências** contra `gfxhub_v2_0.c`.
- **Passo 1 aprovado** (09:46): 512 PTEs construídas na VRAM e conferidas
  entrada por entrada; `PTE[0] = 0x000300045FD00071`, batendo com cálculo
  independente; região restaurada dword a dword; nenhum registrador de tradução
  tocado.
- **Passo 2 aprovado** (10:12), nove de nove: snapshot dos registradores de
  configuração, `restore` aceito por todos com readback conferido, dump idêntico
  antes e depois, e **submissão SDMA com fence física funcionando antes e depois
  das escritas**.
- **GATE DE MEMÓRIA FECHADO** (13:49), 20 de 20 gates. Uma submissão SDMA com
  endereço `0x0000_0800_0000_1000` no pacote escreveu na VRAM em `0x0FD01000` —
  offset que não aparece em lugar nenhum do pacote. Só a page table liga os dois.
  Fence física, dado conferido no kernel, leitura independente pelo BAR0.

  Quatro controles na mesma execução: destino envenenado antes (`0x5A5A0001..4`),
  tradução comprovadamente ligada (`ENABLE_CONTEXT=1`, `SYSTEM_ACCESS_MODE=3`, L2
  habilitada), abertura de framebuffer funcionando com tradução ligada, e o
  offset baixo `0x00001000` intocado.

  Invalidação pela engine 17 com ACK na primeira leitura. Rollback dos 35
  registradores sem divergência e volta ao bypass comprovada.

  **Não prova:** uma página de 512 exercitada, um boot, VMID 0 apenas, SDMA
  apenas.
- **Passo 3 aprovado nos quatro estágios inertes** (12:59), 17 de 18 gates:
  **24 valores novos escritos em registradores de tradução**, todos confirmados
  por readback; submissão SDMA com dado conferido depois deles; `restore` dos 33
  registradores sem divergência; dump de volta ao inicial; submissão funcionando
  de novo. Os dois controles negativos recusaram sem escrever nada.
  `PT_BASE = 0x45FF10000` bate com aritmética independente, e a divergência de
  `SYS_APERTURE_HIGH` estava prevista por escrito antes da medição.
  O único gate reprovado foi parser de ensaio, já corrigido.

---

## 3. O que sabemos que não funciona, e por quê

| Fato | Consequência |
|---|---|
| Opcode reservado mata a execução da engine, sem sinal em registrador de erro | driver nunca pode emitir pacote malformado; assinatura detectada é fence-sem-dado |
| Nada em software recupera essa engine | recuperação real exige reset em nível de dispositivo — **não auditado**, display na mesma GPU |
| GFXOFF nunca transicionou | carga de SDMA não tira o GFX do GFXOFF; regra 9 só cai com carga GFX/compute |
| Sampler de telemetria em user mode cobre ~60% da execução | a fonte confiável é a captura before/kick/after feita dentro do kernel |
| Não há page table nenhuma | todo acesso atual depende do bypass do POST; ligar tradução substitui esse bypass |

---

## 4. Erros meus que viraram regra

Registrados porque cada um custou tempo e todos são repetíveis.

1. **`USE_BC` como causa** — revogado. Era artefato de leitura: `UTCL1_PAGE` só
   lê corretamente com o F32 rodando. Regra: *um registrador só é comparável
   entre dumps se o bloco estiver no mesmo estado de execução.*
2. **Ensaio imprimindo sucesso com fases reprovadas** — o veredito lia uma
   variável paralela em vez das fases. Regra: *o veredito deriva das fases,
   nunca de variável separada.*
3. **Testar recuperação só depois de quebrar a engine** — cinco execuções sem o
   controle positivo. Regra: *mecanismo de recuperação é validado em sistema
   saudável antes de servir como prova sobre sistema quebrado.*
4. **Dump "somente leitura" que adquiria semáforo** — ler
   `GCVM_INVALIDATE_ENG0_SEM` é aquisição, não observação. Regra: *antes de
   colocar registrador num dump, saber se a leitura tem efeito.*
5. **Patch que abortou no meio sem sinalizar** — três edições de CLI perdidas.
   Regra: *conferir por marcador no arquivo, não pela mensagem do patch.*
6. **Cálculo de endereço feito de cabeça** — errei um nibble numa mensagem.
   Regra: *endereço não vai para documento sem passar por ferramenta.*
7. **Gate avaliado sem ter sido exercitado** — preflight recusado ainda
   imprimia FALHA. Regra: *gate não exercitado não reprova; ele não existe
   naquela execução.*
8. **Parser reusado em saída de outro comando** — o `ptbuild` imprime o número
   antes da palavra (`conferencia : 0 divergencias em 512 entradas`) e o helper
   genérico procurava o oposto. Regra: *helper de parse é acoplado ao formato de
   quem imprime; reusar é suposição, não economia.*
9. **Readback exigido de bit que é gatilho** — `GCVM_L2_CNTL2` bits 0 e 1 são
   disparos de invalidação, limpos pelo hardware depois de agir. Regra: *antes de
   exigir readback, saber se a escrita configura ou age.* Correção por máscara de
   bits declarada na saída, nunca por exceção silenciosa.
10. **Estágios de risco aplicados antes de conferir os anteriores** — o `cache`
    falhou no meio e o `context0` ligou tradução sobre configuração incompleta.
    Regra: *em sequência de risco, cada passo aborta na hora; não existe "confere
    tudo no fim".*
11. **`(if ...)` como expressão em PowerShell** — é sintaxe de PS 7+; esta
    máquina roda 5.1, onde falha em **tempo de execução**, e o
    `Parser::ParseFile` **não acusa**. Regra: *checagem de sintaxe não substitui
    execução; construção que só existe em versão mais nova entra por função
    auxiliar explícita.* Custaria um boot descobrir isso no meio de um ensaio.
12. **`BASE_IDX` esquecido no endereço** — `RLC_CNTL` e `RLC_SAFE_MODE` são
    segmento 1 e foram calculados com a base do segmento 0. Os valores
    relatados como RLC eram de outro endereço qualquer, e o gate de
    legibilidade passou lendo a coisa errada. Regra: *anotar `mmREG` sem
    `BASE_IDX` é anotar metade do endereço.* Todo registrador declara o
    segmento, no catálogo e na tabela do ensaio.
    **Reapareceu duas vezes depois disso**: nos defines de `IC_BASE`
    (`0x1AA80` em vez de `0x3E100`) e na receita do RLC (`0x17980` em vez de
    `0x3B000`). É a lição mais reincidente do projeto.
13. **Esperar por bit de status sem checar se o registrador responde** —
    `INVALIDATE_CACHE_COMPLETE` é o bit 1, e `0xFFFFFFFF` o satisfaz. Duas
    sessões declararam "o hardware confirmou por COMPLETE" com o registrador
    lendo todos-os-bits-em-um do início ao fim. Regra: *todos-em-um não é um
    valor, é endereço que não decodifica; antes de esperar por um bit, conferir
    que o registrador responde.*
14. **Declarar passo aprovado sobre resultado instável** — a mesma escrita nos
    mesmos endereços colou numa sessão e foi descartada em duas outras. Regra:
    *comportamento que muda entre sessões sem explicação é não medido, não
    aprovado.*
15. **Read-modify-write sobre registrador que não responde** — ler `0xFFFFFFFF`,
    limpar só os campos nomeados e escrever o resto produziu `0xFC7FFFF0`, com
    todos os bits reservados ligados, chamado de configuração. Regra: *antes de
    ler-modificar-escrever, conferir que a leitura vale.* Irmã da lição 13: o
    defeito era duplo — esperar por bit num registrador morto **e** derivar valor
    dele.
16. **Literal hexadecimal acima de `0x7FFFFFFF` sem sufixo `L`** — `0x80000000`
    como chave de hashtable virou `Int32` negativo e abortou o fim de uma
    execução. Terceira aparição do mesmo hazard no projeto. Regra: *acima de
    `0x7FFFFFFF`, sufixo `L` e máscara `[uint32]` explícita; e testar o decode em
    execução, porque o parse não pega esta classe.*
17. **Controle negativo que recusa pelo motivo errado** — o negativo do RLC
    rodava antes do `arm write`, então o driver recusou por armamento (erro 5) e
    a guarda de snapshot nunca foi exercitada. Pior que a lição 7: ali o gate não
    existia, aqui ele rodou e mediu outra coisa. Regra: *controle negativo tem de
    isolar a guarda que pretende testar; se há duas no caminho, a de fora precisa
    estar satisfeita e a mensagem exigida tem de distinguir as duas.*
18. **Atribuir efeito a uma escrita sem olhar o valor anterior** — o `rlc start`
    leu `0x00000001` antes de escrever `0x00000001`. Quem habilitou o F32 foi o
    PSP; o `start` foi no-op. Regra: *o `antes` de cada escrita entra no
    registro, senão o crédito vai para quem não fez.*

19. **Readback fabricado — o pior defeito já encontrado nesta árvore.** No modo
    `icbase`, o driver escrevia no hardware e depois preenchia o registro assim:
    `WrBefore = 0xFFFFFFFF` de constante e **`WrAfter` recebendo o valor
    querido**, sem uma única leitura. A saída então mostrava
    `0xFFFFFFFF -> <querido> (queria <querido>)`, indistinguível de um readback
    exato vindo do silício. **Isto explica a anomalia das 16:19**, que sobreviveu
    a dias de hipóteses sobre estado de GPU: a "confirmação" daquela execução não
    veio do hardware, veio deste campo. Nenhuma das hipóteses precisava existir.
    Regra: *`antes` e `depois` são sempre leituras do hardware, sem exceção. Um
    campo de evidência nunca recebe o valor que se queria ver — se a confirmação
    não existe, o registro diz que não existe.* Corrigido em `BcCpWriteEx()`, que
    lê os dois lados e marca `WrWriteOnly` quando a leitura não vale.
20. **Guarda de segurança recusando o caminho que funciona.** A guarda
    `BcRegResponds()`, criada para matar o falso positivo da lição 13, passou a
    recusar `icbase` e `jt` porque `IC_BASE_*` e `UCODE_ADDR` leem `0xFFFFFFFF`.
    Só que o dump do Linux mostra **os mesmos `0xFFFFFFFF` na máquina onde o CP
    funciona**: são registradores *write-only*, não blocos mortos. A guarda
    estava certa sobre "não dá para confirmar" e errada sobre "então não
    escreva". Regra: *`0xFFFFFFFF` tem duas causas — endereço que não decodifica
    e registrador write-only — e elas exigem tratamentos opostos: a primeira
    manda parar, a segunda manda escrever e mover a prova para outro lugar.
    Distinguir as duas é trabalho de referência externa, não de dedução.* Aqui a
    prova foi deslocada inteira para o ponteiro de instrução da microengine
    andar.

21. **Ensaio que carrega firmware sem saber o que ele mesmo custa.** O
    `test-cp-unhalt.ps1` passou a carregar o RLC pelo PSP — logo queima o boot,
    como todo ensaio de firmware deste projeto — mas foi entregue **sem guarda de
    boot limpo e sem declarar `reboot_required`**. Em 23:58 ele rodou no mesmo
    boot das 23:26 que o bring-up das 23:32 já havia gasto, não recusou, e a
    execução foi perdida. Regra: *quando um ensaio ganha um passo novo, ele herda
    o custo desse passo — e o preflight e o `reboot_required` fazem parte do
    passo, não são enfeite.* O `reboot_required` é o lado que fecha o ciclo: sem
    declarar, o preflight não detecta a repetição que ele próprio causa.
22. **`throw` destruindo a evidência.** O `throw` do gate reprovado estava dentro
    de um `try` cujo `finally` só fazia teardown, e o relatório vinha **depois**
    do bloco. A exceção propagou e a pasta de evidência ficou **vazia**: a
    execução foi gasta e o valor que reprovou o gate não foi registrado — não se
    sabe quanto lia `RLC_EXEC_ROM_CODE`. Regra: *abortar é decisão do ensaio;
    perder a medição não é.* O `try/finally` passou a ficar dentro de um
    `try/catch`, o relatório sai sempre, e declara `ABORTADO` com o motivo e
    quantos gates foram medidos de quantos previstos. Corolário, irmão da lição
    7: gate não alcançado não reprova — mas tem de aparecer como não alcançado, e
    não como ausente.

23. **Argumento que virou string e apagou a evidência.**
    `[pscustomobject]@{...}` passado a uma função **sem parênteses**, depois de
    continuação com crase, é lido em modo de argumento como bareword: o parâmetro
    recebe a string `"[pscustomobject]@"`. O booleano do gate é outro argumento,
    então **o gate passa, o console fica idêntico, e só a evidência desaparece.**
    Seis gates de uma execução perderam os dados assim — incluindo o
    `CP_ME_CNTL` depois do unhalt. Só foi recuperável porque o log de eventos
    guarda a saída crua dos comandos. Regra: *dado estruturado passado a função
    vai entre parênteses, sempre; e a varredura por
    `^\s+\[pscustomobject\]@\{` precedido de crase entra na validação de
    ensaio, junto com o parse e o grep de `(if ...)`.* Achou dez ocorrências em
    `test-cp-ring-pm4.ps1` de tabela. Irmã da lição 19: instrumento que produz
    saída perfeita e conteúdo vazio.

24. **Switch que não existe vira modo silencioso.** `-ControleSemUcode` foi
    passado na linha de comando, o `param` não declarava a variável (defeito do
    meu script de patch: a lista de linhas foi capturada antes das substituições
    e o `join` final descartou a edição do `param`), o PowerShell resolveu para
    `$null`, e o ensaio rodou em **modo normal sem avisar** — 14 gates em vez de
    10, um boot gasto. Regra: *modo de execução se declara na saída antes de o
    ensaio começar, e variável não inicializada tem de ser erro, não `$null`.*
    Corrigido com o `param`, `Set-StrictMode -Version 1.0` e eco do modo na
    primeira linha, verificado sem `-Execute` para não gastar boot. Irmã das
    lições 19 e 23: o instrumento fez algo diferente do que a saída sugeria.

25. **Veredito derivado da contagem de gates em vez do que foi medido.** O modo
    controle passou todos os 10 gates — porque o controle funcionou — e o ensaio
    imprimiu `SUCESSO: Passo 3 do CP concluido com 100% dos gates aprovados!`
    exatamente na execução que **demonstrou que o passo 3 não acontece**. Regra:
    *um modo cujo objetivo é não executar nada não pode compartilhar a mensagem
    de veredito com o modo que tenta executar; a saída declara o que foi medido,
    não quantos gates passaram.* É a lição 2 na sua pior forma, agora dentro de um
    ensaio que eu mesmo escrevi para ser cético.

Reincidência da lição 8, na mesma sessão: a primeira versão do preflight do item
21 procurava só por `evidence.json` e devolveu **zero**, porque 44 pastas gravam
em `result.json` e só 8 em `evidence.json`. Filtrar por nome de arquivo era
acoplar o leitor ao formato de um produtor. Passou a olhar a **propriedade**
`reboot_required`, em qualquer `.json` — e foi testado contra o estado real da
máquina antes de ser declarado pronto, o que é o único motivo de o erro ter
aparecido.

Padrão comum aos casos 2, 5 e 7: **falha de instrumento se parece com achado de
hardware.** O que salvou foi a saída trazer sempre os valores crus — `0 PTEs`,
`FB_OFFSET 0x00000000`, `snapshot AUSENTE` são impossíveis como medição.

A lição 19 é a exceção grave a esse padrão, e vale destacá-la: ali o valor cru
**foi impresso**, e ainda assim mentiu, porque não era cru — era o valor desejado
copiado para o campo de evidência. Nenhuma leitura de saída detectaria isso. Só a
auditoria do código que produz a saída detectou. Regra derivada: *o instrumento
que gera a evidência precisa ser auditado com o mesmo rigor que o hardware
medido.*

---

## 5. Ferramentas disponíveis

| Ensaio | Precisa boot limpo? | Contamina? |
|---|---|---|
| `capture-baseline.ps1` | não | não |
| `capture-gcvm-baseline.ps1` | não | não (somente leitura) |
| `test-gcvm-pagetable.ps1` | não | escreve VRAM, com rollback |
| `test-sdma-rollback.ps1` | sim | não carrega firmware |
| `test-sdma-minimal.ps1` | sim | sim, firmware |
| `test-sdma-robustness.ps1` | sim | sim, e condena a engine |
| `test-sdma-recovery.ps1` | sim | sim |
| `test-sdma-failclosed.ps1` | sim | sim, condena a engine |
| `test-gcvm-transaction.ps1` | sim | sim, firmware |
| `test-gcvm-program.ps1` | sim | sim, firmware; escreve valores novos, com rollback |
| `test-gcvm-translate.ps1` | sim | sim, firmware; **liga a tradução**, exige `-IAcceptTheRisk`. Aprovado 20/20 |
| `test-cp-firmware.ps1` | sim | sim, firmware; não escreve registrador do CP. Aprovado 6/6 |
| `test-mmio-segment1.ps1` | **não** | **não contamina**. Aprovado 7/7: base do segmento 1 medida |
| `test-cp-directload.ps1` | **não** | **não contamina**. Rodado: ucode na VRAM ok, `IC_BASE` recusado — a recusa estava errada, `IC_BASE_*` é write-only e não bloco morto |
| `test-rlc-bringup.ps1` | sim | sim, firmware pelo PSP. **Aprovado: RLC fora da ROM** |
| `test-cp-unhalt.ps1` | sim | sim, firmware pelo PSP (RLC) + ucode do CP na VRAM, com rollback completo. **Rodado 3x em boots limpos (00:19, 00:47, 00:54): controle vivo, execução ausente, PC sempre `0x0000041E`. O controle `-ControleSemUcode` provou que a carga do ucode não tem efeito.** |
| `test-mmio-responsividade.ps1` | não, mas **use boot limpo** | **somente leitura**, nem arma o driver. Rodado 23:07 em boot limpo |
| `collect-linux-cp-info.sh` | roda no **Linux** | somente leitura; referência com o CP funcionando |

Decodificadores: `decode-sdma-fault.py` e `decode-gcvm.py`, ambos usando o
extrato de bits do header autoritativo do kernel
(`gc_10_1_0_sh_mask_extract.h`, SHA-256 do original registrado no cabeçalho).

Comandos de CLI que valem lembrar: `sdma fault`, `sdma opgate`,
`sdma enginereset`, `gcvm dump`, `gcvm txn`, `gcvm ptbuild`/`ptrollback`,
`gcvm semrelease`, `telemetry sample`/`watch`.

---

## 6. Para onde vamos

### Concluído: mapa em boot limpo, duas hipóteses derrubadas

O mapa já foi rodado (19:36) e deu uma resposta que **derruba a hipótese do GFX
desligado**: `RLC_CNTL` e `GRBM_GFX_INDEX` são registradores de GFX no segmento 1
e **respondem**. A não-resposta é localizada, começa em dw `0x5814` e cobre só a
vizinhança `CP_*_UCODE_*` / `CP_*_IC_*`.

Mas o mapa foi tirado com a **GPU contaminada**:

| Registrador | Repouso | No mapa | Bits novos |
|---|---|---|---|
| `GRBM_STATUS` | `0x00003028` | `0xA0003028` | `GUI_ACTIVE`, `CP_BUSY` |
| `GRBM_STATUS2` | `0x00000008` | `0x51000008` | `RLC_BUSY`, `CPF_BUSY`, `CPG_BUSY` |
| `CP_STAT` | `0x00000000` | `0x84008000` | `CP_BUSY`, `CE_BUSY`, `PFP_BUSY` |
| `CP_RB0_CNTL` | — | `0x00000709` | anel ainda configurado |

E com uma contradição: `CP_ME_CNTL = 0x15000000` diz as três engines haltadas,
`CP_STAT` diz `PFP_BUSY` e `CE_BUSY`. Engine haltada reportando ocupada é estado
pendurado.

A linha do tempo aponta a causa:

| Hora | O que rodou | `IC_BASE` |
|---|---|---|
| 16:19 | direct load, **antes de existir anel** | escrita **aceita** |
| 16:29–16:59 | ensaios de anel PM4 | deixam o CP travado |
| 18:15, 18:58, 19:36 | direct load / mapa | `0xFFFFFFFF` |

Rodado em boot limpo às 19:43. **A GPU estava comprovadamente em repouso** —
`GRBM_STATUS 0x00003028`, `GRBM_STATUS2 0x00000008`, `CP_STAT 0x00000000`, e
`CP_RB0_CNTL` de volta ao default `0x00A00000`, confirmando que o `0x00000709`
era resíduo.

**E a vizinhança do CP continuou em `0xFFFFFFFF`.** Hipótese do estado pendurado
falsificada também.

Duas hipóteses, duas quedas, custo próximo de zero cada:

| Hipótese | Como caiu |
|---|---|
| Bloco de GFX sem energia | `RLC_CNTL` e `GRBM_GFX_INDEX`, GFX no segmento 1, respondem |
| Estado pendurado de sessão anterior | Boot limpo, GPU em repouso, mesma não-resposta |

Fato consolidado: dw `0x5814` a `0x5848` no segmento 1 **não decodificam**, em
boot limpo, GPU em repouso, com endereços conferidos. Tudo até dw `0x4c05` no
mesmo segmento decodifica.

### Concluído: fronteira localizada, e o diff com o Linux resolveu a questão

O mapa foi ampliado com sondas de fronteira e de clock gating, todas somente
leitura. **Um defeito de PowerShell que abortava o fim da execução foi
corrigido** (`0x80000000` como chave de hashtable virava `Int32` negativo —
terceira aparição do hazard de literal hexadecimal; agora com sufixo `L` e
máscara `[uint32]`, e o decode foi testado em execução, não só no parse).

```
dbg\start.bat
powershell -File tools\test-mmio-responsividade.ps1 -Execute
```

Rodado às 23:07 e comparado com o Linux (amdgpu, CP executando). **A zona morta
é idêntica nos dois sistemas**, e o CP funciona no Linux. Portanto ela não é o
bloqueio: esses endereços não são o caminho que o hardware usa, ou são
write-only.

O diff apontou a causa real — `RLC_GPM_STAT.RLC_EXEC_ROM_CODE` é **1 no Windows**
e **0 no Linux**. O RLC do nosso lado está rodando código da ROM, sem ucode.

### Concluído: o RLC saiu da ROM (2026-08-23 23:32)

`RLC_GPM_STAT` de `0x00F40016` para **`0x00B40016`** — `EXEC_ROM_CODE` 1 → 0, e
**bit a bit idêntico ao Linux** com o CP executando. O gate passou.

O crédito é do **PSP**: o `start` leu `0x00000001` antes de escrever, ou seja o
`ENABLE_F32` já estava ligado depois da carga do ucode (tipo 8, 6272 dwords,
`RespStatus = 0`). O `start` foi no-op.

Dois achados de brinde:

- o RLC mexeu no próprio estado depois de subir: `RLC_PG_CNTL` 0 → `0x00000008`,
  `RLC_SRM_CNTL` 0 → `0x00000002`, sem escrita nossa;
- os `SOFT_OVERRIDE_*` do `CGTT_CPF_CLK_CTRL` **não** aparecem sozinhos
  (`0x00000100` antes e depois, contra `0xF8000100` no Linux). Minha hipótese de
  que seriam consequência do RLC está errada.

### Imediato: unhalt do CP com o RLC vivo

Agora existe a condição que nunca existiu: RLC fora da ROM. O caminho do CP pode
ser reavaliado, e nesta ordem:

| Passo | O que medir | Já sabemos |
|---|---|---|
| 1 | ~~com o RLC vivo, `CP_*_IC_*` continua `0xFFFFFFFF`?~~ **RESPONDIDO** | são write-only: `0xFFFFFFFF` também no Linux com o CP executando. Não é bloqueio, e nunca vai confirmar escrita. Medir de novo não acrescenta |
| 2 | `CP_ME_CNTL` aceita limpar os três HALT? | Windows `0x15000000`, Linux `0x00000000` |
| 3 | os PCs das microengines andam? | Linux: `0x172E` / `0xE19` / `0x5CF` / `0x447` |
| 4 | onde o amdgpu escreve `CGTT_CPF_CLK_CTRL = 0xF8000100`? | não mapeado |

O passo 3 é o gate: PC de microengine saindo de zero e variando entre amostras.
Valor constante não é execução — foi assim que um passo do CP foi declarado
aprovado sobre `0x0000041E` parado.

Ordem de operação: `psp loadfw rlc` (o PSP habilita o F32 sozinho), depois o
unhalt. Não é preciso `rlc start`.

**Isto já está embutido no ensaio** como gate `B2_rlc_fora_da_rom`, com a
sequência que funcionou às 23:32 — snapshot do RLC, `stop`, `cgcg`, `pg`,
`psp loadfw rlc` — e sem `start`. Se `RLC_EXEC_ROM_CODE` não for a 0, o ensaio
aborta antes de tocar no CP, porque medir o unhalt sobre pré-requisito ausente
não produz informação.

### Resultado: unhalt funciona, CP não executa (2026-08-24 00:19)

Boot limpo, 13 de 14 gates. O reprovado é o que decide: `I`.

`CP_ME_CNTL` — que **responde**, ao contrário do bloco `IC_*` — foi de
`0x15000000` a `0x00000000` com readback confirmado, e os PCs saíram de zero para
`0x0000041E`, voltando a zero no re-halt. O controle está vivo.

Mas o valor é **constante em 8 amostras** e **igual nas três engines**. Não é
execução — é exatamente o `0x0000041E` sobre o qual um passo do CP já foi
declarado aprovado com o critério antigo.

Todo status global bate com o Linux onde o CP roda (`GRBM_STATUS 0x00003028`,
`GRBM_STATUS2 0x00000008`, `CP_STAT 0x00000000`, `CP_ME_CNTL 0x00000000`). Só os
ponteiros diferem, e no Linux eles são **três valores distintos**
(`0x172E`/`0x0E19`/`0x05CF`) porque cada engine está dentro do próprio ucode. Os
nossos três iguais dizem que elas não chegaram ao nosso microcódigo.

Isso derruba a família "bloco desligado, gated ou pendurado": não há registrador
em estado anômalo.

Reproduzido em segundo boot limpo às 00:47, bit a bit igual: `CP_ME_CNTL`
`0x00000000`, PCs `041E`/`041E`/`041E`, sem variação. Pela lição 14, isso tira o
`0x0000041E` do terreno de "resultado instável" e o coloca em "medido".

(Aquela execução era para ser o controle e rodou em modo normal: o switch não
estava no `param` — lição 24. Agora engata, e o modo sai impresso na primeira
linha.)

### O controle respondeu (2026-08-24 00:54)

Sem ucode, sem `icbase`, sem jump table: PC em `0x0000041E`, os três iguais,
constante em 8 amostras. **Bit a bit igual às execuções com ucode.**

Três boots limpos, dois caminhos de código completamente diferentes, mesmo
resultado. Conclusão estabelecida: **a carga do ucode não tem efeito algum sobre
as microengines, e a busca de instrução nunca acontece.**

Isso encerra a linha "o ucode ou a configuração da cache estão errados". Podem
estar, e não importa — as engines não chegam lá.

**O espaço de hipóteses que sobrou, e ele é pequeno:**

| # | Hipótese | Como discriminar |
|---|---|---|
| 1 | O `IC_BASE` não é aceito de verdade (write-only, nenhum readback desmente) | escrever um valor absurdo e ver se algo muda; ou achar no amdgpu um caminho alternativo de carga |
| 2 | O caminho de memória para busca de instrução do CP não existe nesta configuração | a tradução está desligada e a SDMA só alcança memória pelo bypass do POST; o CP busca por outro caminho, que este laboratório **nunca configurou** |

A hipótese 2 é a mais promissora e a mais barata de investigar **sem gastar
boot**: o gate de memória já está fechado e provado (tradução por page table,
20/20), então há um caminho de tradução funcionando que o CP ainda não usa. O que
falta é descobrir o que o amdgpu configura para a busca de instrução do CP —
GFXHUB/UTCL1 do GC, apertures de MC para o bloco do CP — e comparar com o que
temos.

### O rastreamento foi feito, e a hipótese 2 tem base (2026-08-24)

`gfx_v10_0_cp_gfx_load_pfp_microcode()` aloca o ucode em
**`AMDGPU_GEM_DOMAIN_GTT`** e escreve no `IC_BASE_LO/HI` o
`pfp_fw_gpu_addr` — um endereço **de GPU traduzido** — com
`IC_BASE_CNTL.VMID = 0`.

**A busca de instrução da microengine é um acesso traduzido pelo GFXHUB, com
VMID 0.** Nós entregamos `BC250_VRAM_GPU_BASE + offset` = `0xF4_0E00_0000`, um
endereço físico cru, a um consumidor que traduz. Com a tradução desligada em
repouso e o VMID 0 do GFXHUB nunca configurado aqui, a busca não resolve e a
engine para no bootloader — o que casa com as três execuções, onde ucode,
`IC_BASE` e jump table foram todos irrelevantes.

Dois candidatos caíram no mesmo rastreamento, por conferência e não por teste:

- `mmCP_HYP_PFP_UCODE_ADDR` = `0x5814` = `mmCP_PFP_UCODE_ADDR`. Mesmo
  registrador; nosso `0x3E050` está certo.
- `IC_BASE_CNTL` do upstream: `VMID=0`, `CACHE_POLICY=0`, `EXE_DISABLE=0`,
  `ADDRESS_CLAMP=1`. É exatamente o que nosso driver monta.

**Ressalva registrada:** a etiqueta "write-only" de `IC_BASE_*` vem de um coletor
em espaço de usuário, e o `gfx_v10_0.c` faz read-modify-write nesse registrador —
o que sugere que o kernel lê algo útil. Não muda decisão nenhuma hoje, mas está
apoiada em um único método de leitura.

### Próximo passo: somente leitura, zero boot

```
studio\output\bc250ctl.exe gcvm dump
```

O que olhar — em GFXHUB v2, acesso de VMID 0 dentro de
`[SYSTEM_APERTURE_LOW, HIGH]` resolve direto para VRAM via `FB_LOCATION`, sem page
table:

| Registrador | Endereço |
|---|---|
| `GCMC_VM_FB_LOCATION_BASE` / `TOP` | `0x0A600` / `0x0A604` |
| `GCMC_VM_SYSTEM_APERTURE_LOW_ADDR` / `HIGH` | `0x0A614` / `0x0A618` |
| `GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB` / `MSB` | `0x0A5B0` / `0x0A5B4` |

- abertura **não** cobre `0xF4_0E00_0000` → hipótese confirmada; a correção é
  apontar o `IC_BASE` para endereço que o VMID 0 resolva;
- abertura **cobre** → hipótese cai, e o foco volta para o `IC_BASE` não ser
  aceito.

A favor: o gate de memória já está fechado e provado (tradução por page table,
20/20, VA `0x800000001000` → VRAM `0x0FD01000`). Há tradução funcionando aqui que
o CP não usa; falta ligar uma na outra, não inventar outra.

**Veredito do passo 3: NÃO VALIDADO EM HARDWARE.**

Unhalt sem ucode, sem `icbase`, sem jump table, 10 gates. Se o PC ainda for
`0x0000041E`, a carga não teve efeito sobre as engines e o problema está antes da
execução — a busca de instrução nunca aconteceu, e o `IC_BASE` write-only volta
para a mesa como suspeito. Se for outro valor, a carga mudou o comportamento e
`0x041E` é do nosso caminho.

### Primeira tentativa perdida por sessão suja (2026-08-23 23:58)

Rodou e abortou no `B2_rlc_fora_da_rom`. Medido depois: o boot das 23:26:11
serviu **as duas** execuções, e o bring-up das 23:32 já havia gasto esse boot
carregando o RLC pelo PSP. O ensaio não recusou porque não tinha guarda de boot
limpo — lição 21 — e o `throw` destruiu o relatório antes de gravar o valor que
reprovou o gate — lição 22. **Não se sabe quanto lia `RLC_EXEC_ROM_CODE`**, e
por isso não está estabelecido que a sessão suja seja a causa; é o candidato
principal, não um fato.

Registro em
`docs/evidence/2026-08-23_23-58-52-DESKTOP-HQAG6T4-cp-unhalt/LEIA-ME_execucao_sem_evidencia.md`.

Corrigido: preflight de boot limpo (testado contra o estado real, detecta a
execução das 23:32 e recusa), evidência gravada mesmo em aborto, `B2`
distinguindo "medi 1" de "não consegui medir", e o ensaio declarando
`reboot_required`.

### Como rodar, agora

**Precisa de reboot antes.** O ensaio carrega firmware pelo PSP e recusa sessão
suja por conta própria.

```
dbg\install.bat
dbg\start.bat
powershell -File tools\test-cp-unhalt.ps1 -Execute
```

O `.sys` mudou (ABI 20), então instalar é obrigatório: o CLI recusa driver com
ABI diferente do header contra o qual foi compilado.

Quatorze gates. O que olhar, em ordem de importância:

- `I_ponteiros_de_instrucao_ativos` — **o gate.** Exige que algum PC **varie
  entre amostras**. Sair de zero foi rebaixado a observação, com a nota de que
  uma engine sem ucode válido para num valor fixo do bootloader (medido:
  `0x0000041E`). Foi sobre esse valor parado que um passo do CP já foi declarado
  aprovado.
- `B2_rlc_fora_da_rom` — pré-requisito; aborta o ensaio se falhar.
- `E_tres_caches_invalidacao_pedida` — diz "pedida", não "confirmada". O gate
  antigo exigia `INVALIDATE_CACHE_COMPLETE`, que é o bit 1 e é satisfeito por
  `0xFFFFFFFF`: exigia prova impossível.
- `J_halt_restaurado_e_parado`, `K`, `L` — o caminho de volta. VRAM das três
  engines e registradores do CP e do RLC devolvidos.
- `M_sem_bugcheck` — agora consulta `Get-WinEvent` de verdade; estava fixado em
  `$true`.

O que a saída vai mostrar e **não** é defeito: `[WRITE-ONLY: readback nao
confirma]` em cada escrita de `IC_BASE_*` e `UCODE_ADDR`. É o estado honesto
desses registradores, confirmado contra o Linux.

Sequência derivada de `gfx_v10_0_rlc_resume()`, com endereços já conferidos
contra registrador vivo e diferenças medidas nos dois sistemas:

| Passo | Ação | Endereço | Windows hoje | Linux |
|---|---|---|---:|---:|
| 1 | RLC pelo PSP | — | aceito (`RespStatus = 0`) | carregado |
| 2 | `RLC_CGCG_CGLS_CTRL = 0` | `0x3B124` | `0x0001003C` | `0x00000000` |
| 3 | `RLC_PG_CNTL = 0` | `0x3B10C` | `0x00000000` | `0x00000000` |
| 4 | `RLC_CNTL.RLC_ENABLE_F32 = 1` | `0x3B000` | `0x00000000` | `0x00000001` |
| 5 | **conferir `RLC_EXEC_ROM_CODE` 1 → 0** | `0x3B9B8` | `1` | `0` |

**O passo 5 é o gate.** Sem ele, "RLC habilitado" seria afirmação sobre escrita
aceita — e este projeto já sabe quanto vale isso.

O RLC **não** é carregado por `RLC_GPM_UCODE_ADDR/DATA`: esses não respondem nem
no Linux. É carregado pelo PSP, que já aceita o `cyan_skillfish2_rlc.bin`
(`RespStatus = 0`, medido às 14:21), e o `amdgpu_firmware_info` do Linux confirma
a mesma versão `0x0000000d`.

Nada disso está implementado ainda. E vale a ordem: com o RLC rodando, os cinco
`SOFT_OVERRIDE_*` do `CGTT_CPF_CLK_CTRL` devem aparecer sozinhos — eles são
consequência do RLC, não algo a escrever à mão.

### Anomalia das 16:19: RESOLVIDA (2026-08-24)

Não era hardware. O driver preenchia o campo de evidência com o valor **querido**
sem ler o hardware:

```c
Io->WrBefore[0] = 0xFFFFFFFF;   /* constante */
Io->WrAfter[0]  = cntl;         /* o valor querido, nunca lido */
```

A saída imprimia `0xFFFFFFFF -> 0x00000001 (queria 0x00000001)`, indistinguível
de um readback exato do silício. O "aceite" das 16:19 veio deste campo. As
sessões de 18:15 e 18:58 passavam por caminho de código diferente, e por isso
"descartaram" — não havia instabilidade nenhuma.

Nenhuma das hipóteses de estado de GPU, clock gating residual ou hang remanescente
precisava existir. Registrado como lição 19; corrigido em `BcCpWriteEx()`, que lê
o hardware nos dois lados e marca `WrWriteOnly` quando a leitura não vale.

Com isso, **não há mais observação do dia sem hipótese que a cubra.**

### Depois: rodar o direct load do CP

O `.sys` mudou (ABI 20), então a instalação é obrigatória:

```
dbg\install.bat
dbg\start.bat
powershell -File tools\test-cp-directload.ps1 -Execute
```

**Não exige boot limpo e não contamina a sessão.** Nenhum firmware pelo PSP, o
ucode volta da VRAM e os dez registradores do CP voltam do snapshot.

Doze gates. O que olhar:

- `C_recusa_sem_snapshot` — sem caminho de volta provado, o driver tem de recusar
  escrever em registrador do CP.
- `E_ucode_na_vram_conferido` — 65792 dwords por engine, conferidos dword a
  dword.
- `F_cache_invalidada_com_confirmacao` — `INVALIDATE_CACHE_COMPLETE` aceso pelo
  hardware, não só a escrita feita.
- `J_cp_continua_parado_e_haltado` — **o mais importante.** Nada ali tira engine
  do HALT; se um ponteiro de instrução se mover, alguma coisa começou a executar
  sem ser liberada, e isso precisa de explicação antes do próximo passo.

Limitação conhecida: o driver mantém um snapshot de ucode único, então as três
engines foram encenadas uma por vez. Encenar as três simultaneamente é requisito
do passo seguinte, o que tira do HALT.

### Concluído: base do segmento 1 medida

Antes de escrever no CP existe uma suposição no caminho: **a base do segmento 1
do MMIO (`0xA000` dwords) nunca foi conferida contra registrador vivo.** Está no
catálogo como classe P1, derivada. E todo o direct load do CP mora nesse
segmento.

Escrever `CP_PFP_IC_BASE_LO` com a base não verificada seria escrever MMIO por
suposição. Em 2026-08-23 esse risco já se materializou em forma leve: a primeira
versão de `test-cp-firmware.ps1` calculou `RLC_CNTL` com a base do segmento 0 e
relatou como RLC valores de outro endereço qualquer.

```
dbg\start.bat
powershell -File tools\test-mmio-segment1.ps1 -Execute
```

**Não exige boot limpo e não contamina a sessão.** Nenhum firmware, nenhum
registrador do CP, e os oito valores originais são devolvidos. Pode rodar agora.

O alvo é `SCRATCH_REG0..7` (`0x30100`..`0x3011C`), com controle de leitura em
`TCP_WATCH0_ADDR_H` (`0x0CA80`) — o mesmo dword `0x2040` no segmento 0. Dois
padrões complementares, oito valores distintos, e o controle tem de ficar
intacto.

### Depois: direct load do CP

Medido em 2026-08-23 14:21: **o PSP desta placa recusa as microengines do CP**
(`0x80000203` para pfp, me, ce, mec) e aceita o RLC (`RespStatus = 0`). Coerente
com o upstream — `psp_v11_0_8` não tem `load_ip_fw` para o CP.

A rota é **direct load**, e o mecanismo do GFX10 não é streaming de ucode por
registrador. É a microengine buscar o próprio ucode da memória:

1. ucode em memória alcançável pela GPU;
2. invalidar a cache de instruções (`CP_PFP_IC_OP_CNTL.INVALIDATE_CACHE`,
   esperar `INVALIDATE_CACHE_COMPLETE`);
3. `CP_PFP_IC_BASE_CNTL` com `VMID = 0`, `CACHE_POLICY = 0`, `EXE_DISABLE = 0`,
   `ADDRESS_CLAMP = 1`;
4. `CP_PFP_IC_BASE_LO/HI` apontando para o ucode;
5. jump table por `CP_PFP_UCODE_ADDR`/`DATA`, terminando com a versão no `ADDR`.

Endereços, todos no **segmento 1** e já no catálogo:
`CP_PFP_IC_BASE_LO 0x3E100`, `CP_PFP_IC_BASE_CNTL 0x3E108`,
`CP_PFP_IC_OP_CNTL 0x3E10C`, `CP_PFP_UCODE_ADDR 0x3E050`.

**O gate de memória é pré-requisito disto, não trabalho paralelo.**
`CP_PFP_IC_BASE_CNTL` tem campo `VMID`. Se a busca de ucode do CP passa pelo
VMID 0 do GFXHUB, a tradução provada às 13:49 é dependência direta.

### Estado do bring-up do Command Processor (CP / PM4)

Uma sessão anterior registrou os passos 2, 3 e 4 como aprovados. **A auditoria
das evidências não sustenta.** O status abaixo é o que os arquivos de evidência
mostram.

| Passo | O que é | Status | O que a evidência mostra |
|---|---|:---:|---|
| 1. Header & ucode audit | Conferir `gfx_firmware_header_v1_0` | **APROVADO** | Parsing de arquivo, v1.0 / IP v10.1 / jump table dentro do payload nas três engines |
| 2. Ucode na VRAM | Cópia do ucode e conferência | **APROVADO** | 65.792 dwords por engine, conferidos dword a dword pela CPU |
| 2b. `IC_BASE` e invalidação | Apontar a cache para o ucode | **NÃO PROVADO** | Os registradores leem `0xFFFFFFFF` e descartam escrita. O gate de invalidação era falso positivo |
| 3. CP unhalt | Liberar do HALT e ver o PC andar | **NÃO SUSTENTADO** | Sem `IC_BASE` válido, o PFP foi liberado sem ucode. PC parado em valor constante não é execução |
| 4. Graphics Ring 0 / PM4 | Anel na VRAM e submissão | **NÃO INTERPRETÁVEL** | Sem 2b e 3 firmes, `RPTR` não avançar não tem leitura |

#### O fato que reorganiza tudo

Os registradores da cache de instruções do CP, **nos endereços corretos do
segmento 1**, leem `0xFFFFFFFF` e descartam escrita:

```
0x3E108 : 0xFFFFFFFF -> 0xFFFFFFFF  (queria 0xFC7FFFF0)  DIVERGENCIA
0x3E100 : 0xFFFFFFFF -> 0xFFFFFFFF  (queria 0x0E000000)  DIVERGENCIA
```

Todos-os-bits-em-um não é um valor: é endereço que não está decodificando. E não
é erro de endereço — no mesmo segmento 1 o banco de rascunho responde, e no
segmento 0 `CP_ME_CNTL` lê `0x15000000` estável.

Três coisas foram corrigidas no código: os endereços, que haviam sido trocados
para a derivação do segmento 0 (`0x1AA80`); a guarda que recusa operar sobre
registrador que lê `0xFFFFFFFF`; e o falso positivo do `INVALIDATE_CACHE_COMPLETE`
— bit 1, que `0xFFFFFFFF` satisfaz trivialmente.

#### A hipótese do RLC: medida e derrubada na forma original

O mapa de 19:36 respondeu: **`RLC_CNTL` em `0x3B000` responde** (`0x00000002` =
`FORCE_RETRY`), e `GRBM_GFX_INDEX` no segmento 1 também. Portanto **o bloco de
GFX não está desligado** — se estivesse, esses dois devolveriam `0xFFFFFFFF`.

A não-resposta é localizada em dw `0x5814` e acima, só na vizinhança
`CP_*_UCODE_*` / `CP_*_IC_*`.

E a receita original usava `0x17980`/`0x17994`, derivações do segmento 0 dos
mesmos registradores `BASE_IDX 1`. Corretos: `0x3B000` e `0x3B014`.

Duas correções de decodificação, de passagem:

- `CP_STAT = 0x84008000` foi lido como `ROQ_RING_BUSY | PFP_BUSY | ME_BUSY`. Pelo
  header autoritativo é `CP_BUSY | CE_BUSY | PFP_BUSY`.
- `RLC_CNTL = 0x00000002` é `FORCE_RETRY`, não `RLC_ENABLE_F32`. O bit 0 está
  apagado, o que é consistente com querer escrever `0x1` — mas no endereço certo.

#### O que bloqueia o WDDM agora

1. **Alcançar o bloco do CP.** Enquanto os registradores lerem `0xFFFFFFFF`, não
   há passo 2b, 3 nem 4.
2. **Observabilidade física.** Transição de GFXOFF e telemetria sob carga real do
   GFX — e agora se sabe que isso e o item 1 podem ser o mesmo problema.
3. **MQD/HQD para compute.** Filas assíncronas do MEC.

### Barra de progresso: **88%**

```text
[█████████████████▋░░] 88%
```

Voltou de 95% para 88% porque 95% contava passos que a evidência não sustenta.
A barra mede prova física; retirar uma prova que não existia não é retrocesso de
engenharia, é correção de contabilidade.

---

## 7. Como retomar na prática

```
dbg\install.bat          (só se o .sys do workspace divergir do instalado)
dbg\start.bat
```

Depois, o ensaio que interessa. Se for algum que exige boot limpo, reinicie
antes — o preflight recusa sozinho, mas o reboot é inevitável.

Ordem de leitura dos documentos: este arquivo, depois
[08_STATUS_PRE_WDDM.md](08_STATUS_PRE_WDDM.md) para o estado por área,
[01_EVIDENCIAS_E_LICOES.md](01_EVIDENCIAS_E_LICOES.md) para o histórico de
medições e revogações, [03_CATALOGO_DE_REGISTRADORES.md](03_CATALOGO_DE_REGISTRADORES.md)
para endereços e semântica de bit, e [04_PLANO_DO_DRIVER_NOVO.md](04_PLANO_DO_DRIVER_NOVO.md)
para o plano de VMID 0 e os requisitos que a medição impôs ao driver.

As regras superiores continuam em
[`BC250_HARDWARE_RULES.md`](../BC250_HARDWARE_RULES.md). Em caso de divergência,
elas prevalecem.
