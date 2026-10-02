# 546 — Corrida 188: tabela de objetos do KMD cheia, AppContainer derrubado pela I.1b, e candidato 189 (T4.7 + I.1c)

> Data: 30/09/2026. Continua o [doc 545](545_INVENTARIO_UMD_DWM_E_SHELL_CORRIDAS_181_186.md) §7.
> Tudo o que está abaixo da §3 é **offline**. **NÃO VALIDADO EM HARDWARE.**

## 1. Corrida 188 — relato (30/09 16:18, pacote 3.333.1.0)

- **Pasta:** `docs/evidence/2026-09-30_16-18-07-DESKTOP-HQAG6T4-lab-binding/`.
- **Candidato:**
  - SYS `2BBD88C8…`, o mesmo das 182–187;
  - UMD `9AF0291D…` (T4.6 + I.1b);
  - watchdog de 600 s.
- **Roteiro:** o Daniel **não abriu o menu Iniciar**. Por isso a T4.6 (recopia por quadro da superfície compartilhada) continua **sem resposta**.

1. **Houve avanço?** Sim, de diagnóstico. Não houve avanço funcional. Pela primeira vez a RADV disse **por que** uma alocação falhou.
2. **Sucessos:**
   - **A I.1b funcionou nos processos comuns (P0):**
     - existem arquivos `-RADV` do dwm.exe (1272), do Explorer (6264) e de 10316, com até 12,8 MB;
     - o binder copiou todos sem abortar, ao contrário da 187.
   - **Estabilidade (P0):** zero `TDR_QUIESCE`, zero `IB_TRIPOD TIMEOUT` e zero BugCheck no log do KMD (`display-log-this-bind-FINAL.txt`, 86.812 linhas).
3. **Falhas:**
   - **Cadeia completa de uma textura perdida, medida (P0):**
     - o KMD registrou `CREATE_ALLOC_RECUSADO status=0xC000009A motivo=REGISTRO_DO_OBJETO_DE_ALOCACAO_FALHOU ... ObjStatus=3` duas vezes, nas linhas 48.598 e 48.599;
     - `ObjStatus=3` é `NO_SPACE`, a tabela `BC250_MCDM_OBJECT_REGISTRY` cheia;
     - a primeira recusa tem `Bytes=65536 PrivSize=40`, a mesma assinatura da linha da RADV no Explorer: `CreateAllocation2 failed 0xC000009A (is_bc250=1, PrivateDriverDataSize=40)`;
     - a RADV devolve isso como `VK_ERROR_OUT_OF_DEVICE_MEMORY`. O log da UMD do Explorer tem 1 OOM e 3 falhas de alocação;
     - a segunda recusa (`Bytes=4096 PrivSize=56`, token de classe `RES`) é a alocação WDDM da própria UMD.
   - **A I.1b derrubou três processos AppContainer (P0):**
     - Pesquisa, ShellExperienceHost e Client.CBS saíram com `0xC0000409`;
     - os `-RADV` deles têm 0 bytes;
     - a falha foi reproduzida offline (§2).
   - A Pesquisa continuou com `vkCreateDevice -3` em massa. O motivo não chegou ao log, justamente pela falha acima.
4. **Veredito:** **parcial** frente ao critério da §7.3 do doc 545.
   - O **desfecho A** ocorreu para o Explorer: a etapa nomeada é `CreateAllocation2`, e o KMD nomeia a causa.
   - O **desfecho B** não ocorreu: a Pesquisa ficou sem motivo.
   - A T4.6 não foi exercitada.
5. **Recuperação e pendências:**
   - rollback do watchdog, com BasicDisplay ao final;
   - **NÃO VALIDADOS EM HARDWARE:** T4.6 no menu, motivo do `vkCreateDevice -3` e trava do FILL.

### 1.1 Correção de registro: §7.1 do doc 545

Escrevi que a RADV usa 4 KiB para BO pequena de VRAM e que os 64 KiB não eram o mecanismo. **A medição desmente a primeira parte:** a recusa do KMD mostra que a RADV pediu `Bytes=65536` para a memória da imagem. Os requisitos de imagem registrados pela I.1 também dizem 65.536.

O mecanismo que sobra é outro e está medido:

- cada textura D3D ocupa **dois** objetos na tabela do KMD (a alocação WDDM da UMD e a memória Vulkan da RADV);
- a tabela tem capacidade fixa;
- quando ela enche, a próxima alocação é recusada com `NO_SPACE`, **não** por falta de VRAM.

Que as centenas de OOM do Explorer nas Corridas 183–186 tenham essa mesma causa é **H**: naquelas corridas o motivo não era registrado.

## 2. I.1c — sem redirecionamento do stderr em AppContainer

- **Reprodução offline** (scratchpad `stderrtest`: `aclaunch.exe` cria um AppContainer, e `host_ac.exe` carrega uma DLL com CRT dinâmico que escreve em `stderr`, como a RADV):

  | Variante | Resultado |
  |---|---|
  | I.1b | `EXIT 0xC0000409` |
  | I.1c em AppContainer | `EXIT 7`: sobreviveu e se reconheceu como AppContainer |
  | I.1c em processo comum | `EXIT 0` |

- **Mudança** (`driver/umd/bc250_umd_vulkan_bridge.c`):
  - `Bc250UmdVkProcessIsAppContainer` lê `TokenIsAppContainer`. Se a leitura falhar, o processo **conta como AppContainer** (falha fechada);
  - em AppContainer a UMD registra `I.1c ... NAO redirecionado` e não toca no `stderr`;
  - nos demais processos, o redirecionamento roda sob um manipulador de parâmetro inválido vazio, que depois é restaurado.
- **Custo declarado:** o motivo do `vkCreateDevice -3` da Pesquisa **continua invisível**. Medi-lo exige outro canal, porque o stderr não serve em AppContainer. Não é objetivo desta corrida.
- **Portão:** `audit_radv_stderr_reaches_evidence` foi estendido para exigir três coisas: a verificação de AppContainer antes do redirecionamento, a falha fechada e o manipulador. Mutação verificada.

## 3. T4.7 — capacidade da tabela de objetos: 4.096 → 16.384

- **Onde:** `driver/core/bc250_mcdm_object_contract.h`, `BC250_MCDM_MAX_OBJECTS 16384u`.
- **Verificado antes de mudar:**
  - o handle x64 carrega o índice da vaga em 32 bits, então há folga;
  - só a tabela usa a constante;
  - os registros dos testes são `static`, então não há estouro de pilha.
- **Trava:** `driver/tests/mcdm_object_contract_test.c` ganhou um `typedef` com índice negativo se a capacidade for menor que 16.384. Na mutação (voltar para 4.096), o build falha com **C2118**. O header foi restaurado.
- **É paliativo, e está declarado assim.** Uma tabela 4× maior adia o `NO_SPACE`, mas não prova que ele não volta. Se houver vazamento de objetos (alocação não fechada no ciclo de vida do processo), a tabela enche mais tarde. A correção definitiva é uma destas duas:
  - tabela dinâmica;
  - um objeto por textura, o que é a Frente B (aliasing, como no Linux).
- **Único efeito no SYS:** esta faixa só muda a constante.

## 4. Candidato 3.334.1.0

| Artefato | SHA-256 |
|---|---|
| SYS assinado | `8000EE83C493F36AA562221F51FA1890D9DFA7DB7D796AEF2A7BC315C3A80416` |
| INF | `8A2F753E12A7FBA4C2E2B219B688A3D37B64A6BF91A2175F8FA6383FE7B2E5F0` |
| CAT | `33F31B4509FD1A920E574A2F14858604648A2C43BAEA11FDCC1C713CAD04FC85` |
| UMD | `15F667C4A264557521FD78F7223ABC52C37F9C0726A7C9C594C5171EC52F5B5D` |
| RADV (inalterada) | `FCAA11FE…` |

- **Perfil e flags:** `DISPLAY_RENDER_LAB`, WDDM 2.7, persistent submission, `GfxWaveProbe=1`.
- **Builds:**
  - build da UMD: 0 erros, 0 avisos, todas as suítes PASS;
  - `build-contract` com EXIT 0; os avisos são das ferramentas antigas em `tools/*.c`, não do driver;
  - SYS de laboratório reconstruído **antes** do empacotamento, já que o empacotador só copia.
- **Auditorias:** matriz de DDI, segurança, no-op e pacote de laboratório, todas PASS.
- **Strings conferidas na UMD:** `I.1c`, `NAO redirecionado`, `T4.6 RECOPIA` e `T4.4 TOKEN_SEQUENCE`.
- **Preflight:** `tools/prepare-corrida189-t47i1c.ps1` com EXIT 0.

## 5. Critério da Corrida 189 (escrito antes, Lei 10)

**Procedimento:**
1. Boot frio.
2. Rodar `tools\EXECUTAR_CORRIDA189_T47I1C_600S.cmd`.
3. Abrir o menu Iniciar cerca de 5 vezes ao longo da corrida. Em cada abertura, anotar a hora e se os ícones e textos aparecem (e em quanto tempo).
4. Uma vez, digitar algumas letras na Pesquisa.

| Desfecho | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| **A** | Zero `ObjStatus=3` no log do KMD **e** zero `CreateAllocation2 failed 0xC000009A` nos `-RADV` **e** o menu foi aberto | A capacidade era o limite na carga medida. A T4.7 fica como paliativo e a correção definitiva (tabela dinâmica ou Frente B) volta a ser prioridade, não urgência |
| **B** | `ObjStatus=3` reaparece | Há vazamento de objetos ou a demanda passa de 16.384. Próximo passo: medir a ocupação ao longo do tempo (contagem viva) antes de mexer de novo |
| **C** | Algum AppContainer (Pesquisa, ShellExperienceHost, StartMenuExperienceHost, TextInputHost) sai com `0xC0000409` **e** o log dele tem `I.1c` | A I.1c não cobre o caso real. Rever |
| **D** | Menu aberto, ícones e textos aparecem, e o cruzamento `tools/cross-shared-surface-timeline.py` mostra `T4.6 RECOPIA` depois do último draw do menu | A T4.6 é a correção do menu com blocos cinza |
| **D′** | Menu aberto, ícones faltando, e existe `T4.6 RECOPIA` depois do último draw | A recopia roda mas não resolve. O defeito está no conteúdo ou no tempo da origem, não na falta de recopia |
| **E** | Algo fora do mapa | Descrever antes de julgar |

Os desfechos A/B, C e D/D′ são **eixos independentes** e cada um é julgado por si. Se o menu não for aberto, D e D′ ficam sem resposta, como na 188.

**Não esperar:** o fim da lentidão do menu. O custo por quadro não foi tocado nesta faixa.

## 6. Corrida 189 (30/09 17:50, 3.334.1.0): trava dura às 17:52, com o binder copiando 590 MB de logs antigos

- **Pasta:** `docs/evidence/2026-09-30_17-50-12-DESKTOP-HQAG6T4-lab-binding/`.
- **Trava** relatada pelo Daniel às 17:52, cerca de 2 min depois do bind.
- **Log do KMD recuperado** do arquivo cumulativo: `bc250_display_log-tail-recovered.txt`, 3.632.751 bytes, SHA-256 `A8C64B5B…`.

1. **Houve avanço?** Não, nem funcional nem de diagnóstico sobre a trava. Houve um **aprendizado de método**: a coleta do binder era carga pesada em cima de toda corrida (abaixo).
2. **Sucessos:**
   - zero `ObjStatus=3` e zero `CREATE_ALLOC_RECUSADO`, num trecho de apenas ~2 min;
   - 929 IBs SDMA executados;
   - zero TDR, zero timeout e zero `GFXHUB_FAULT`.
3. **Falhas:**
   - **Trava dura.** O último FILL registrado (11.075.584 bytes, a superfície de tela cheia) **concluiu** (`IB EXECUTADO ... FenceId=927`).
   - Depois dele vêm FLUSH_TLBs, um TRANSFER e um IB gráfico concluídos.
   - O log termina **no meio de uma linha** de um submit gráfico (`FenceId=336`). A cauda bufferizada se perdeu, então **não dá para dizer** se a trava terminou num FILL como as 10 anteriores.
   - Os logs da UMD desta corrida não estão na pasta: a cópia morreu com a trava, e os arquivos do dwm.exe ficaram em `C:\Windows\Temp`.
4. **Veredito:** **inconclusivo.** Nenhum eixo do critério da §5 foi respondido. Não se sabe se o menu chegou a ser aberto antes da trava.
5. **Recuperação:**
   - o watchdog restaurou o BasicDisplay às 18:00 (Código 0);
   - T4.6, T4.7 e I.1c continuam **NÃO VALIDADAS EM HARDWARE**.

### 6.1 Achado: a coleta do binder era carga pesada dentro da janela de ensaio (P0)

- **Desde 29/09 18:10, cada bind fazia isto nos primeiros ~5 min da janela física:**
  - copiava **e** concatenava, por `Get-Content | Add-Content`, **todos** os `BC250_UMD_LOG_*` das pastas Temp;
  - eram até 903 arquivos, a maioria das suítes offline e de corridas antigas;
  - o agregado chegava a **1,1–1,7 GB**.
- **Exemplos medidos pelos horários dos arquivos:**
  - na 188, de 16:18:18 a 16:23:52;
  - na 182, até 07:58:31;
  - na 189, a cópia estava em andamento às 17:52:30, o último instante gravado no log do KMD.
- **Não é causa provada da trava:** as Corridas 182–185 e 188 passaram pela mesma carga sem travar, e as travas da 156–172 são anteriores a ela.
- **Mas é carga de CPU, disco e memória no mesmo controlador de GDDR6** que trava (H, ligada ao W1). Também viola a regra de atividade mínima durante o ensaio.
- **Correção (offline):**
  - `Get-Bc250BindUmdLogs` só coleta arquivos gravados **depois** do início do bind (`$bindUmdLogCutoffUtc`);
  - `Add-Bc250LogToAggregate` concatena por fluxo, sem carregar o arquivo em memória;
  - as três coletas (o snapshot e as duas FINAL) passam por ela.
- **Portão** `audit_umd_log_collection_is_bounded_to_the_bind`: mutação (filtro removido) reprova.
- **Pacote inalterado** (3.334.1.0); o binder não faz parte do pacote assinado.

### 6.2 Próxima corrida

Repetir a 189 com o mesmo pacote e o mesmo critério da §5. A única mudança é a coleta enxuta; abrir o menu desde cedo.

Se não travar, isso **não** prova que a cópia causava a trava: a taxa de base é de ~2 em 13 corridas, e um ensaio isolado não separa.

### 6.3 Logs da UMD copiados depois (`final-elevado/`): o menu FOI aberto, e a cadeia até a trava está medida (P0)

O Daniel copiou os logs com PowerShell elevado: dwm.exe (1264), Explorer (6208) e os respectivos `-RADV`. A cadeia, pelos horários dos logs:

| Hora | O que aconteceu | Fonte |
|---|---|---|
| 17:52:21 | Começa a interação: o log do dwm.exe sobe de ~126 para ~1.700 linhas/s | log do dwm.exe |
| 17:52:26.780 | O DWM abre a superfície do menu (640x640, 1.638.400 bytes): `OpenResource T4.2: Map=0 ... hr=0` | idem |
| 17:52:27.397 | Abre uma segunda superfície compartilhada, 2592x1088 (11.419.648 bytes) | idem |
| 17:52:27.619 | Ao materializar o SRV dela, a RADV pede 12 MB para a imagem Vulkan. `CreateAllocation2` OK, `MapGpuVirtualAddress` OK, **`MakeResident: status=0xC0000017` (STATUS_NO_MEMORY)** → `vkAllocateMemory res=-2`, duas vezes | `-RADV` do dwm.exe |
| 17:52:27.637 | `DrawIndexed: RECUSADO -- SRV_RECURSO_SEM_VKIMAGE` | log do dwm.exe |
| 17:52:28.125 | O DWM **destrói o device inteiro**; às 17:52:28.462 cria outro | idem |
| 17:52:29 | Recria as três primárias e os alvos de 11 MB cada. No KMD aparece o FILL de 11.075.584 bytes, que concluiu | dwm.exe e KMD |
| 17:52:30.37 | Primeiro draw do device novo. A partir daí, nada mais: trava dura | todos |

**Leitura:**
- **Causa medida (P0):** o segmento local de ~257 MB **encheu**. O VidMm não tem para onde despejar desde a T3.9 (`EvictionSegmentSet = 0`), e o `MakeResident` falhou. Confirma, para o dwm.exe, a hipótese que o doc 545 §7.1 deixara aberta.
- **Contagem:** dwm.exe com 2 `NO_MEMORY` em 92 `MakeResident`; Explorer com 2 em 413.
- **Mesmo padrão das travas anteriores:** a falta de memória faz o DWM recriar o device, o que dispara uma rajada de alocações grandes e FILLs. A trava vem uns 3 s depois. Era o que se via na 181 ("recriar as primárias") e na 186 (clique no menu).
- **O que continua H:** que essa rajada **cause** a trava. A rajada é o gatilho mais frequente; o mecanismo físico continua o do doc 507.
- **Por que enche:** cada textura D3D ocupa **duas** vezes a memória (a alocação WDDM da UMD e a imagem da RADV), e uma superfície aberta ganha uma imagem **nova** em vez de usar a memória que já existe.
- **Consequência para o plano:** a Frente B (a imagem Vulkan usar a própria alocação, sem duplicar) deixa de ser só a correção da cópia do menu. Passa a ser a alavanca contra a falta de memória que precede as travas.
- **Fato de leitura:** a RADV já tem caminho de importação por NT handle (`radv_wddm2_bo.c:816`, `OpenResourceFromNtHandle`). Se ele serve para alocações da UMD sem handle compartilhado é a primeira pergunta da frente.