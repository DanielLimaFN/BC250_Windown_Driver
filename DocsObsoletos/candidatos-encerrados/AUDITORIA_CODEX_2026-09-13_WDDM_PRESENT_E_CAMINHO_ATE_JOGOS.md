# Auditoria independente — Present, contratos reais e caminho até jogos

Data: 13/09/2026. Alvo: BC-250, Windows 10 22H2. Escopo: revisão offline do checkout existente, BASE_DE_AVANCO.md, dossiê 268, fontes UMD/KMD e snapshots de três corridas. Nenhum bind, registro, pacote ou fonte do driver foi alterado. Há numerosas alterações preexistentes no checkout; os achados de código se referem a este estado, não automaticamente a todos os binários históricos.

**Conclusão:** existe progresso físico real em SDMA, paginação e compute. O bloqueio atual tem defeitos concretos de contrato antes de ser uma incógnita de silício. O melhor candidato novo para investigar o E_FAIL é `SetDisplayModeDXGI` devolver sucesso sem executar o callback obrigatório, chamado repetidamente pelo DWM. Isso NÃO estabelece a causa única do E_FAIL. Mesmo corrigido, restam apresentação coerente com o escalonador e rasterização real.

P0 = medição em evidência física arquivada; P1 = contrato Microsoft; P2 = código observado ou cadeia apenas parcialmente demonstrada; H = hipótese causal. Código lido é marcado P2, não P0. Todas as correções propostas: **NÃO VALIDADO EM HARDWARE**.

## 1. Achados prioritários

### A1 — SetDisplayMode devolve S_OK sem estabelecer o modo/primária

**P2, defeito demonstrável; P1, obrigação documentada; H forte, explicação do E_FAIL.**

`driver/umd/bc250_d3d_umd.c:3428`: valida somente o ponteiro, escreve log e retorna `S_OK`. Não traduz recurso/subrecurso para alocação nem chama `pfnSetDisplayModeCb`.

A Microsoft determina essa tradução e a chamada ao callback, que inicia o caminho de `CommitVidPn`. A declaração da alocação como Primary é uma condição anterior, não substitui essa transição. Fonte: [DXGI1_1_DDI_BASE_FUNCTIONS — SetDisplayMode](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgi1_1_ddi_base_functions).

**P0:** no arquivo `docs/evidence/2026-09-13_18-03-43-DESKTOP-HQAG6T4-lab-binding/bc250_umd_log_1216-FINAL.txt`, linhas 3033 e 3058, há SetDisplayMode às 18:03:49.310 e .324. O primeiro Present falha às .515. São **182 SetDisplayMode e 91 Present/E_FAIL** nesse snapshot por PID. O snapshot agregado tem 192 e 96. Esse callback não é código morto.

O teste `driver/tests/bc250_d3d_umd_test.c:617` cria argumento zerado, sem device/recurso válido, e exige S_OK. **O teste congela o defeito.**

Correção proposta: validar device, recurso, subrecurso e alocação; montar `D3DDDICB_SETDISPLAYMODE`; chamar o callback pelo handle de runtime correto; propagar seu HRESULT; registrar entrada/retorno e identidade do recurso. Tratar o eventual retorno de reallocation conforme o WDK, não generalizar tudo para sucesso. Não introduzir escrita DCN nessa correção UMD.

Limite: isso pode abrir um caminho que hoje está bloqueado, mas o KMD ainda precisa honrar o contrato de apresentação. Um callback bem encaminhado não torna uma primária arbitrária fisicamente escaneável.

### A2 — A arquitetura promete flip MMIO, mas só mantém um scanout fixo

**P2 + P1; defeito arquitetural, causalidade do E_FAIL ainda H.**

`driver/core/bc250_driver_caps_contract.c:43` declara `SupportPerEngineTDR=1` e `FlipOnVSyncMmIo=1`. Em `driver/wdk/bc250_display_ddi.c:2420`, SetVidPnSourceAddress apenas armazena endereço/segmento/handle e devolve sucesso. Não muda o endereço físico que o monitor escaneia.

O contrato de `FlipOnVSyncMmIo` inclui alteração efetiva do scanout em SetVidPnSourceAddress e notificação do endereço efetivo no VSync. **Salvar um endereço em uma estrutura não implementa flip.** Fonte: [DXGK_FLIPCAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_flipcaps).

Consequência: se A1 passar a funcionar, não concluir que a próxima chamada a SetVidPnSourceAddress significa que o monitor mudou de buffer. O endereço real do GOP continua sendo uma autoridade separada.

Para o escopo GOP+SDMA, documentar e implementar de ponta a ponta o caminho de cópia por GPU que o runtime aceita. Não trocar arbitrariamente Flags.Flip por Blt em Present, não ativar DirectFlip sem backend e não chamar um blt imediato de flip sincronizado. A compatibilidade completa Full Graphics tem requisitos que o escopo GOP fixo ainda não demonstrou satisfazer. O documento local `referencias/windows-driver-docs/windows-driver-docs-pr/display/direct-flip-of-video-memory.md` marca DirectFlip obrigatório; isso é uma dívida real da meta “100%”, mesmo que não seja a causa deste E_FAIL. A restrição atual contra escrita DCN permanece respeitada.

### A3 — BIND_PRESENT, primária solicitada e capacidade de scanout foram confundidos

**P2 + P1.**

`driver/umd/bc250_d3d_umd.c:860` usa `BIND_PRESENT || pPrimaryDesc != NULL` para marcar primária. Não consome ModeDesc/Flags do descritor nem devolve uma decisão de scanout. A origem do VidPnSourceId é substituída por zero em `:885`.

O WDK distingue recurso apresentável de primária para scanout: BIND_PRESENT pode existir com pPrimaryDesc nulo. `DXGI_DDI_PRIMARY_DESC.DriverFlags` é saída; `NO_SCANOUT` informa apresentação por cópia. Fontes: [CreateResource](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/ns-d3d10umddi-d3d10ddiarg_createresource), [PRIMARY_DESC](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgi_ddi_primary_desc).

Correção proposta: política separando (a) recurso apresentável, (b) primária solicitada pelo runtime, (c) superfície que o backend realmente consegue escanear. Validar formato, dimensão, amostragem, rotação e VidPnSourceId. Preservar o vínculo hRTResource legítimo já corrigido.

`NO_SCANOUT` é negociação documentada de capacidade, não emulação por CPU. **Não é recomendação de ativar uma flag às cegas:** primeiro medir pPrimaryDesc, inclusive OPTIONAL, e determinar se a primária requerida pelo DWM admite o caminho de cópia. Não há prova de que o DWM desta configuração vá aceitar essa alternativa. Para primárias obrigatórias, não prometer suporte inexistente.

### A4 — Present do KMD pode dar sucesso sem apresentar, inclusive para preto legítimo

**P2, defeito de código; P0, um ramo exercitado.**

`driver/wdk/bc250_display_ddi.c:3059` suprime a cópia quando as amostras são zero e retorna sucesso. `:3153` retorna STATUS_SUCCESS mesmo quando sdmaDone é falso, inclusive após falha de prova. `:3175` emite somente um NOP no DMA buffer.

Problemas distintos:

- Uma amostra 0/256 não prova uma superfície integralmente zero.
- Preto é conteúdo válido. Depois de um quadro colorido, apresentar preto precisa apagar o quadro anterior.
- Mesmo uma prova inconclusiva não autoriza fingir que a operação solicitada aconteceu.
- O comentário “present reprovado” não corresponde ao retorno de sucesso.

**P0:** Present #3 na corrida 17:18, linha 9442 do KMD FINAL, diz `Blt preservado`; não houve cópia nesse Present. O dossiê afirma seis blts, mas há cinco SDMA=YES.

Correção: separar execução, conteúdo e força da prova. Usar poison e padrões conhecidos em ensaio; não inferir sucesso/falha operacional da cor do frame. Projetar o tratamento de falha junto com o mecanismo de recuperação. **Não substituir simplesmente o S_OK por STATUS_IO_DEVICE_ERROR no caminho quente**, pois isso reabre a classe conhecida de BugCheck sem resolver o contrato.

### A5 — O blt real é disparado durante a preparação do Present; o scheduler recebe NOP

**P2 + P1; risco de ordenação, não causa provada do E_FAIL anterior ao KMD.**

`driver/wdk/bc250_display_ddi.c:3077` chama Bc250SdmaPresentBlt diretamente; `:3178` escreve NOP. A cópia e o DMA agendado são operações diferentes. A fence do NOP não demonstra que o DMA buffer continha a transferência solicitada.

O papel documentado da preparação de DMA deve ser distinguido da submissão do scheduler: [Submitting a Command Buffer](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/submitting-a-command-buffer).

Com rasterização real em outro contexto, surge o risco de copiar antes de a produção do frame terminar, usar residência que já mudou ou concluir o objeto errado. Correção estrutural: codificar a transferência e suas referências no trabalho que será agendado, preservar recursos até a conclusão, resolver dependências produtor→cópia→apresentação e notificar a conclusão correspondente. Se um escape continuar como harness diagnóstico, seu sucesso permanece prova do harness, não da integração WSI de jogos.

### A6 — Ainda há operações D3D feitas em shadow de CPU, não na GPU

**P2, caminhos concretos. Não alego execução desses ramos em toda corrida.**

- ClearRenderTargetView (`driver/umd/bc250_d3d_umd.c:1281`) pinta somente pCpuShadow, sem comando GPU ou upload nesse handler. O render target real pode continuar zerado.
- ResourceCopy (`:1651`) tenta o blt GPU, ignora seu HRESULT e depois copia o shadow da origem por CPU e chama UploadAllocation. Se os shadows estiverem desatualizados, pode sobrescrever o resultado correto da GPU. Se o blt não for elegível, o ramo CPU ainda pode executar.
- ResourceCopyRegion (`:1698`) copia shadows, ignora índices de subrecurso/DstZ e não tem backend GPU nesse handler.
- UploadAllocation (`driver/umd/bc250_wddm_winsys.c:719`) efetivamente usa memcpy no mapeamento retornado pelo lock.

Isso não torna qualquer upload CPU proibido: dados iniciais e Map/Update de aplicação naturalmente começam na CPU. O problema é implementar a semântica de **copy/clear de recurso GPU** por shadow ou usá-lo como fallback depois de falha de GPU.

Testes `bc250_d3d_umd_test.c:1137`, `:1319` e `:1334` exigem pixels no shadow, não execução GPU. A retirada de memcpy do Draw foi boa, mas não eliminou essa classe da UMD inteira.

Correção: clear/copy GPU com contrato explícito de formatos/subrecursos; shadow apenas para os usos CPU permitidos e com coerência conhecida; falhas propagadas pela DDI. Um teste de clear preto/vermelho na alocação GPU é um degrau útil de apresentação, mas continua sem provar rasterização.

### A7 — CreateResource publica recurso criado depois de falhas descartadas

**P2.** Em `bc250_d3d_umd.c:889`, MapAllocation e MakeResident têm resultados descartados; Created recebe TRUE após apenas Allocate dar certo. Falha em Allocate só produz log; não há SetErrorCb nesse handler. pInitialDataUP não é consumido.

O tamanho (`:748`) considera apenas mip zero e usa produtos UINT sem overflow verificado. MipLevels é armazenado mas não entra na soma. Isso vai bloquear texturas mipadas, arrays/MSAA/layouts e recursos grandes. A fonte consultada descreve a lista de mip levels e dados iniciais: [D3D10DDIARG_CREATERESOURCE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/ns-d3d10umddi-d3d10ddiarg_createresource).

Correção: criação transacional, distinguir mapping válido de residência pendente e consumir a paging fence antes do uso; erro reportado por SetErrorCb com semântica apropriada; rollback dos objetos parciais. Sucesso de Allocate não equivale a recurso utilizável. Fonte sobre erro na criação e responsabilidade de cleanup: [PFND3D10DDI_CREATERESOURCE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/nc-d3d10umddi-pfnd3d10ddi_createresource).

### A8 — Outros contratos permanecem vazios ou inconsistentes

**P2; prioridade conforme alcance.**

| Código | Defeito | Consequência / ação |
|---|---|---|
| UMD :3471 RotateResourceIdentities | S_OK sem rotação | Rotacionar identidades de backing/views preservando handles RT; testar A,B,C→B,C,A. Zero chamadas nos snapshots por PID examinados: não atribuir o primeiro E_FAIL a isso. |
| UMD :3438 SetResourcePriority | S_OK sem callback | Implementar prioridade real; teste :623 aceita estrutura vazia. |
| UMD :3449 QueryResourceResidency | Consulta booleano local; sempre S_OK | Consultar runtime e devolver status agregado correto; o teste :629 congela consulta local. |
| UMD :907 OpenResource | Created=TRUE sem reconstruir alocações/layout | Implementar importação de recurso compartilhado e ownership antes de desktop com múltiplos processos. Zero OpenResource observado nos snapshots DWM examinados. |
| KMD callbacks :2424 ResetEngine | Sempre NOT_SUPPORTED | Não corresponde ao SupportPerEngineTDR=1. Reset global SDMA provado não prova reset por engine/MEC. |
| UMD :1773 RejectUnbackedDraw | Com substituição armada, omite draw sem SetErrorCb | Log é honesto sobre ausência, mas aplicação não recebe a operação. Regime não pode promover capacidade de produção. |

Para rotação, prioridade e residência, conferir [DXGI_DDI_BASE_FUNCTIONS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgi_ddi_base_functions). A norma exige rotação das identidades de kernel preservando handles RT, callback de prioridade e consulta de residência ao runtime.

## 2. Correções de interpretação do dossiê

1. **187 não são 187 eventos independentes.** Na 18:03, arquivo por PID tem 91 ENTER/E_FAIL; agregado tem 96; as 91 linhas ENTER do PID aparecem literalmente no agregado. A união é 96. A soma 91+96 reproduz exatamente 187. O E_FAIL sistemático permanece, mas o denominador estava duplicado.
2. **Seis DDIs não são seis blts.** Há cinco linhas SDMA=YES, uma supressão. Os cinco têm amostra de sub-retângulo não nula. “Todos com origem toda zerada” excede a medição e contradiz essas amostras. Não prova composição correta nem rasterização.
3. **E_FAIL síncrono não nomeia uma validação específica.** Pode vir da camada runtime, preparação ou kernel. Mesmo HRESULT não garante mesmo motivo em duas variantes. A mensagem UMD :3404 que transforma qualquer hr!=0 em “dxgkrnl recusou antes da DDI” é mais forte que a prova; existem HRESULTs de sucesso não zero.
4. **Ausência de DdiPresent não é localizador universal para Flip.** A rota MMIO tem contrato diferente de DMA blt e precisa ser observada também em SetVidPnSourceAddress/VSync. Os seis Presents GDI não demonstram conversão dos Presents da UMD.
5. **“Contexto eliminado” é forte demais.** Foi eliminada a hipótese de que mudar somente Node1→Node0 solucionaria o erro. Não foram demonstrados todos os requisitos de contexto, dependências, ownership e modelo de submissão.
6. **Node1 rotulado 3D não prova anel gráfico.** GetNodeMetadata é declaração do próprio driver. A prova compute MEC permanece distinta de comandos gráficos PFP/ME.
7. **“Primária correta” não está encerrado.** hResource+Primary corrigem uma parte. SetDisplayMode, descritor, capacidade física, importação e ciclo de vida continuam com lacunas.
8. **A rota DXVK não evita o anel gráfico.** Pode evitar escrever uma UMD D3D completa para o jogo, mas DXVK/Vulkan graphics ainda exigem rasterização. BASE_DE_AVANCO.md, seção “Desktop e jogos”, sugere vantagem de ordem que precisa dessa ressalva. O backend comum GFX beneficia as duas frentes.

## 3. Respostas diretas a Q1–Q9

| Pergunta | Resposta |
|---|---|
| Q1 — causa do E_FAIL | Não determinada. A1 é o novo candidato mais fundamentado: obrigação ausente e chamada medida antes da falha. A2/A3 impedem tratar apresentação como resolvida mesmo após A1. |
| Q2 — cadeia de flip | Não encontrei exigência de agrupar todos os backbuffers em um único hResource. Não inventar isso nem compartilhar handles RT distintos. O WDK prevê recursos separados e rotação de identidades. Investigar descritor e transição de primária primeiro. |
| Q3 — DirectFlip=0 versus Flip | São conceitos distintos. DirectFlip é otimização de composição; seu zero não solicita conversão global para blt. O contrato por recurso está em PRIMARY_DESC. Seu suporte obrigatório na meta completa continua dívida. |
| Q4 — nó COPY/3D | Não encontrei na documentação consultada regra universal exigindo Node0 ou proibindo contexto COPY. O nome de engine não substitui capacidades e contrato do contexto. A mudança isolada não resolveu. |
| Q5 — estrutura do callback | hDstAllocation=0 é permitido; zero contextos adicionais é coerente sem broadcast; payload privado zero é possível sem ABI privada; SyncIntervalOverrideValid=0 significa sem override. Não há evidência de que esses campos sejam a causa. pDXGIContext é corretamente repassado. Faltam guardas claras para recurso/handle inválido e subrecurso não suportado. |
| Q6 — alocação | Não basta conferir bits extras ao acaso. Corrigir classificação/descritor, A1, mapping e residência. Não alego que Overlay, HintedBank ou MaximumRenamingListLength sejam obrigatórios para este caso. |
| Q7 — gates | Sim: SetDisplayMode, RotateResourceIdentities, prioridade, consulta local de residência e testes de shadow. O teste de caps exige PerEngineTDR=1 apesar de ResetEngine recusar sempre. |
| Q8 — omissões | A1–A8, duplicação de logs, amostra zero tratada como frame inteiro e fence do NOP confundida com transferência. |
| Q9 — ordem | Corrigir contratos demonstráveis de apresentação primeiro, com prazo definido; desenvolver o backend gráfico comum sem esperar ensaios especulativos de flags. Não trocar o objetivo por compute. |

Fonte dos campos de Q5: [DXGIDDICB_PRESENT](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgiddicb_present). Fonte conceitual de Q3: [Direct flip of video memory](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/direct-flip-of-video-memory).

## 4. Plano executável com testes e critérios falsificáveis

Os nomes abaixo são **testes propostos**, não executados nem arquivos existentes. Nenhum candidato foi preparado nesta auditoria.

| Etapa | Correção / teste offline | Prova física exigida |
|---|---|---|
| 1. Transição de primária | `TestDxgiSetDisplayModeContract`: callback captura hRTDevice/hPrimaryAllocation; sucesso/falha preservados; inválidos recusados; cleanup/reallocation explicitados | `PRESENT_MODE_HANDSHAKE`: SetDisplayMode ENTER/LEAVE correlacionados com Commit e identidade. Se callback passar e E_FAIL persistir, refutar A1 como causa suficiente. Se falhar, registrar HRESULT e parar nesse contrato. |
| 2. Contrato de scanout | `TestPrimaryDescriptorPolicy`: BIND_PRESENT com/sem desc, OPTIONAL, modo suportado/recusado, saída DriverFlags e SourceId; nunca prometer flip sem executor | `PRESENT_GPU_COPY_CONTRACT`: verificar a rota negociada, sem assumir que DWM aceitará NO_SCANOUT. Cópia efetiva precisa terminar no GOP por GPU, com origem conhecida. Rejeição de primária obrigatória é resultado, não autorização para falsificar caps. |
| 3. Present real | `TestPresentBlackFrameAndFailure`: preparar DMA não toca GPU; payload contém cópia; falha não vira NOP de sucesso; frame zero não suprimido | `PRESENT_RED_BLACK_PATTERN`: GPU produz vermelho→preto→padrão esparso; bytes exatos no destino, fences e ordem; zero escrita CPU no scanout. Preto tem de substituir vermelho. |
| 4. Recursos | `TestCreateResourceFailureMatrix`, `TestResourceLayouts`, `TestGpuClearCopy`: falhas em Allocate/Map/Resident/Upload, mips e overflow, import/export, dados iniciais; shadow divergente não pode sobrescrever GPU | Rodar sequência de clear/copy conhecida, sem Draw substituído; pressão de memória e desalocação após fence. Faltas de residência não podem publicar recurso pronto. |
| 5. GFX comum | RADV/ACO e winsys com seleção correta da engine; contrato de IB gráfico, descriptors, render target, cache/sincronização. NOP GFX testa fila, não rasterização | `GFX_TRIANGLE_TRIPOD`: triângulo rasterizado pela engine gráfica em VRAM com validação geométrica, depois SDMA ao monitor. Compute desenhando imagem não fecha este marco. |
| 6. DWM e jogos | Backend D3D11 estado→pipeline Vulkan, recursos compartilhados, alpha/blend/scissor, queries, bindings; WSI para jogos | Desktop com movimento/redimensionamento/transparência, depois jogo Vulkan, DXVK e VKD3D com frames atribuídos à GPU. Sem promoção por CreateDevice ou shader traduzido. |
| 7. Robustez | `TestEngineResetCapabilityCoherence`, recursos vivos durante TDR, dependências entre filas, limites de GPUVA e teardown | Hang induzido por engine com reset/readmissão/display sobrevivendo. Não repetir TDR global já provado antes de ter pixel real. |

**Orçamento recomendado para o E_FAIL:** no máximo duas corridas direcionadas após testes offline. Primeira mede A1 com demais variáveis controladas; segunda somente se a primeira trouxer um discriminante concreto. Se persistir o HRESULT genérico, interromper a troca de flags e capturar trace ETW de DxgKrnl/DXGI com PID/TID/identidades e, se necessário, pilha de retorno em reprodução controlada. ETW pode não revelar o motivo interno; não prometer que o fará. A segurança e execução do ensaio continuam com Daniel e watchdog.

Não juntar implementação de NO_SCANOUT, mudança de nó e SetDisplayMode em um único ensaio causal. Telemetria adicional pode acompanhar desde que não mude a semântica.

## 5. Melhorias no processo que evitam novas corridas perdidas

- Manter uma tabela curta de estado atual separada do histórico. A BASE conserva afirmações refutadas abaixo de correções e números antigos; o dossiê é mais recente que seu cabeçalho. Percentuais sem denominador funcional não representam distância até jogos.
- Cada cap precisa de três links: obrigação normativa, executor real e teste físico. “Mandatory” é trabalho a implementar, não motivo para declarar TRUE antes do backend.
- Testar comportamento capturando argumentos, ownership, transições e falhas. Regex de fonte é defesa auxiliar; não prova equivalência ao WDK.
- Deduplicar evidência por PID/TID/sequência. O arquivo agregado e os arquivos por processo são visões sobrepostas; registrar também janela temporal do snapshot e de qual arquivo veio cada contagem.
- Telemetria de execução não decide conteúdo. Zero, shader preto e recorte sem alteração são estados legítimos. A prova de ensaio usa padrão conhecido/poison, não um limiar NonZero como política de produto.
- Limitar o volume do log quente com contadores por evento e resumo final, preservando erros e primeiras ocorrências. O snapshot DWM :18:03 tem cerca de 33 MB; impressão excessiva altera latência e atrapalha correlação.

## 6. O que foi verificado e o que não foi

Executei `tools/audit-bc250mcdm-safety.py`: **PASS, exit 0**. O próprio resultado imprime “operational DDIs without hardware backing: fail-closed”, apesar dos contraexemplos acima. Isso demonstra limite da cobertura; não invalida os invariantes específicos que ele efetivamente testa. Não executei build nem suíte binária, pois não houve mudança de fonte e a tarefa é auditoria do estado atual.

Mineração reproduzível em `docs/evidence/auditoria-codex-2026-09-13/minerar.py`; resultado `medicoes.json`, com SHA-256 das fontes-chave e logs e interseção literal das entradas Present. A união por linha exata serve a estes snapshots; para novas corridas, usar IDs de evento. A 16:28 não tinha ENTER/LEAVE instrumentados: zero dessa string não significa zero Present UMD.

Não determinei a instrução interna de DXGI/dxgkrnl que produz E_FAIL. Não provei que o DWM aceita o caminho NO_SCANOUT deste backend. Não fiz auditoria exaustiva de todo registrador, pacote PM4, todo RADV externo ou toda concorrência do KMD. Não validei novos comportamentos no silício. Não alterei os documentos de status normativos nem reclassifiquei seus marcos automaticamente.

**Decisão recomendada:** corrigir o handshake SetDisplayMode e a semântica de apresentação; remover sucessos sem efeito e shadow como backend de operações GPU; construir o caminho gráfico comum RADV/ACO→GFX. Há trabalho concreto suficiente para avançar sem gastar outra corrida em suposições sobre um hResource compartilhado ou um nó “mágico”.
