# Auditoria offline do estado WDDM — 09/09/2026

**Parecer:** continuar o desenvolvimento, mas não aprovar o driver como pronto nem promover a UMD atual para novo ensaio de desktop. Existe execução física de comandos WDDM, porém backend gráfico, conclusão integral do scheduler e recuperação do MEC ainda exigem trabalho. Desktop acelerado e jogos: **NÃO VALIDADO EM HARDWARE**.

Auditoria da árvore de trabalho existente, com numerosas alterações ainda não commitadas. Foram lidos BASE_DE_AVANCO.md, AGENTS.md, rules.md, roadmap 164, handoffs recentes, fontes dos caminhos críticos e evidências da corrida 19:56:09. Não é certificação exaustiva de todo o driver. Nenhum bind, alteração de registro, instalação, reset ou ensaio físico foi executado. Código e scripts operacionais não foram alterados nesta auditoria.

## 1. O avanço real e seu limite

A corrida 3.119.1.0 contém evidência física relevante:

- `bc250_display_log-full.txt:84377`: preview do IB com `DW0=C0073702`, `DW1=00000500`, destino `0x21000` e canários imediatos.
- Mesmo log, linha 84382: `HqdValid=0x7F`, `RPTR=12`, `WPTR=12`, VMID9.
- `B7_UMD_NODE1_TRIPOD_2026-09-09_19-56-15-346/umd-log.txt:26`: fence `0xBC250F01` sobre poison, cinco canários conferidos e cleanup marcado 1.

Esses arquivos estão em `docs/evidence/2026-09-09_19-56-09-DESKTOP-HQAG6T4-lab-binding/`. O log integral contém corridas anteriores: não somar todas as ocorrências como se fossem do último bind.

**Conclusão restrita:** o IB de prova submetido pela UMD/WDDM executou comandos PM4 de escrita e fence sob VMID9. Esse avanço deve ser preservado.

**Não demonstra shader:** `Bc250UmdBuildNode1ProofCommand`, em `driver/umd/bc250_d3d_umd.c:1752`, emite `Bc250Pm4EmitWriteData` e `Bc250Pm4EmitReleaseMemEop32`. Os valores esperados já estão dentro do IB. Não existe dispatch de shader nesse teste. O bit SHADER_TYPE_COMPUTE não substitui um dispatch. A expressão “executou o shader” em BASE e doc 229 excede a prova. Isso não revoga provas anteriores de compute direto no laboratório; limita o que foi provado pela nova rota WDDM/UMD.

## 2. Achados acionáveis

### A1 — Alto: rollback executa sem os parâmetros de autorização

`tools/rollback-bc250mcdm-lab.ps1:7` declara Execute e IAcceptDisplayInterruptionRisk, mas nenhum deles é consultado no restante do arquivo. Uma chamada elevada sem parâmetros chega a desabilitar dispositivo, desinstalar pacote e apagar serviço.

**Correção necessária:** condicionar todas as mutações ao modo efetivo de execução; prévia deve apenas descrever ações. Teste offline com comandos externos interceptados deve exigir zero mutações sem parâmetros e com somente um deles. Não executar o script real para testar essa propriedade.

### A2 — Alto: a nova desabilitação não garante rollback gracioso no Windows 10

Na linha 50, o script usa `/disable-device ... /force`, descarta a saída e não verifica o código de retorno nem confirma que o dispositivo parou antes de `/delete-driver ... /uninstall /force`.

O /force **dessa operação de disable** é documentado a partir do Windows 11 22H2; o alvo é Windows 10 22H2. O /force de delete-driver tem outro histórico e outra semântica. Fonte local: `referencias/windows-driver-docs/windows-driver-docs-pr/devtest/pnputil-command-syntax.md:91`; conferência externa: [Microsoft — PnPUtil](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax#disable-device).

**Correção necessária:** operação compatível com o sistema alvo, captura do resultado e confirmação do estado antes de remover pacote. Falha na desabilitação não pode cair silenciosamente na remoção forçada. A correção relatada pelo doc 229 continua **NÃO VALIDADO EM HARDWARE**. A atribuição causal exata do último 0x119 à remoção surpresa não foi revalidada por dump nesta auditoria; deve permanecer distinguida da sequência temporal relatada.

### A3 — Alto: a UMD anuncia pipeline 3D que não executa

`driver/umd/bc250_d3d_umd.c:3386` responde Type 130 com níveis 10_0/10_1 e S_OK, sem condicionar a resposta à existência do backend. BC250_D3D10_11_NATIVE_READY continua 0 em `bc250_d3d_umd.h:17`. Todas as variantes Draw chamam Bc250UmdRejectUnbackedDraw (`:1536`) e sinalizam E_NOTIMPL.

D3D11LabPromotion permite contornar o gate de criação do device (`:3046`), e a leitura aceita qualquer DWORD não zero (`:3220`). Esse caminho publica a tabela sem render implementado. Não foi inferido que a chave esteja armada atualmente; o defeito é a capacidade do caminho de anunciar suporte inexistente.

**Correção necessária:** preservar a recusa honesta de capacidades até haver backend; teste deve chamar GetCaps com buffer válido e verificar recusa enquanto Draw não possuir executor. Inspecionar semanticamente a promoção de laboratório. Preencher ponteiros não implementa um feature level. A árvore atual difere da UMD registrada na corrida, cujo log ainda recusava Type 130; não atribuir a mudança atual ao pacote antigo.

### A4 — Alto: DXGI devolve sucesso sem efetivar operações na GPU

No mesmo arquivo:

- Bc250DxgiPresent (`:2855`) devolve S_OK se o callback de apresentação estiver ausente.
- Bc250DxgiBlt (`:2935`) termina com S_OK, inclusive sem recursos válidos; chama ResourceCopy, que apenas faz memcpy entre pCpuShadow (`:1446`), sem submissão GPU nesse caminho. Retângulos/rotação são registrados, mas não aplicados por essa cópia integral.
- Bc250DxgiQueryResourceResidency (`:2908`) marca os recursos como plenamente residentes sem consultar seu estado real.

**Consequência:** promover essa tabela pode fazer o runtime acreditar que houve apresentação, cópia ou residência sem o efeito correspondente. O KMD SDMA já provado não torna esses caminhos UMD automaticamente corretos.

**Correção necessária:** backend real e propagação de falhas; recusar o que não existe. Verificar ausência de callback, recursos inválidos, cópia com região e recurso não residente, além do sucesso físico posterior.

### A5 — Alto para fechamento da etapa: veredito automático Node1 contradiz o handoff

O `b7-umd-node1-tripod.json` da corrida 19:56:09 declara:

```
harness_callback_legs=false
retired=false
ring_drained=false
hardware_validated=false
before.P1RetiredTotal=17
after.P1RetiredTotal=17
after.P1LastWptrMmioReadback=12
after.P1LastRptrMmio=0
```

O stdout do harness contém PASS e o MMIO direto mostra 12/12. Há prova física favorável e um coletor que não fechou seus critérios. `tools/test-b7-umd-node1-tripod.ps1:158` coleta ExitCode após WaitForExit; linhas 180–183 usam esse código e snapshots de registro para decidir. O JSON não arquiva o ExitCode bruto, dificultando explicar o primeiro false.

**Conclusão:** não há base para apagar a reprovação, nem para negar os canários medidos. A fence monitorada da UMD (0xBC250F01) e a fence interna de retirement (18) são distintas; “FENCE COMMIT” não prova retirement. Causa da divergência, conclusão pelo scheduler e repetibilidade: **NÃO VALIDADO EM HARDWARE** nesta auditoria.

**Próximo trabalho offline:** reconciliar a linha temporal, registrar ExitCode bruto e motivo de falha, verificar retirement e atualização dos probes. Reprocessar a evidência sem forçar PASS. Não gastar nova corrida apenas para repetir o teste inconclusivo.

### A6 — Alto antes de carga gráfica: TDR não recupera integralmente o MEC

Bc250ActiveTdrQuiesceLifecycle (`driver/wdk/bc250_active_miniport.c:3035`) chama o quiesce SDMA e coleta telemetria MEC. Bc250ActiveTdrRestartLifecycle (`:3119`) retorna exclusivamente Bc250SdmaPresentRestartAfterTdr(). O probe seguinte também consulta SDMA.

A prova TDR histórica não fecha recuperação de hang MEC/Node1. Snapshot do MEC não é seu reset. Recuperação do MEC e sobrevivência do display composto permanecem **NÃO VALIDADO EM HARDWARE**. Não repetir ensaio TDR SDMA para declarar esse item concluído.

### A7 — Médio: fontes de status divergem

BASE abre com “Frente 1 100%”, mas conserva ações antigas para o IB já executado, pacote lab estagnado de 07/09 e um “handoff mais recente” apontando ao doc 174. AGENTS ainda apresenta o fracasso inicial Node1. Doc 229 generaliza escrita PM4 para execução de shader. Roadmap 164 é referência arquitetural, não inventário atualizado de capacidades entregues.

**Correção necessária:** separar estado atual de histórico, corrigir o escopo da prova e manter BASE/AGENTS/CLAUDE/GEMINI/.cursorrules coerentes quando a barra for revisada. Os percentuais existentes não possuem nesta auditoria uma métrica verificável de esforço restante; não usar 78%, 90% ou “100%” para prever proximidade dos jogos.

## 3. Verificações executadas

Logs novos em `docs/evidence/2026-09-09_AUDITORIA_ESTADO_OFFLINE/`:

| Verificação | Resultado |
|---|---|
| driver/wdk/build-contract.bat | EXIT=0 |
| audit-bc250mcdm-safety.py | EXIT=0 |
| audit-umd-node1-submission.py | EXIT=0 |
| audit-ddi-noop-callbacks.py | EXIT=0; 98 DDIs anunciadas, 3 dívidas aceitas e 1 aviso |
| audit-radv-wddm-safety.py com --mesa-root | EXIT=0 |

Mesa auditado no caminho declarado no runner: `C:/Users/Daniel/Desktop/bc250-win/bc250-win/mesa-radv-wddm`. A primeira invocação sem --mesa-root recusou por argumento obrigatório; foi corrigida. Não foi falha do backend.

O auditor de DDIs tolera explicitamente três stubs KMDOD. Os auditores estáticos não detectaram os falsos sucessos UMD acima. Verde prova os critérios desses testes, não correção total ou execução no silício. Não foi recompilado/assinado/instalado um candidato novo.

## 4. Etapa atual e sequência recomendada

| Camada | Leitura da evidência |
|---|---|
| Boot, firmware, compute direto de laboratório | Provas históricas existentes |
| Miniport WDDM, VidPn, primária e paginação SDMA | Caminhos reais exercitados em hardware |
| UMD → WDDM → IB PM4 sob VMID9 | Escritas/fence/canários medidos; coletor e retirement pendentes |
| Shader real pela rota UMD/WDDM/RADV | Não demonstrado pelo teste de canários de 17 DW |
| Backend D3D10/11 do compositor | Incompleto; Draw recusado e DXGI sem semântica completa |
| Vulkan gráfico, desktop acelerado e jogos | NÃO VALIDADO EM HARDWARE |
| TDR MEC e display sobrevivendo | NÃO VALIDADO EM HARDWARE |

**Decisão:** seguir para implementação real da UMD gráfica, preservando GOP/SDMA e a rota de shader definida pelo projeto. A evidência não justifica mudar a meta nem reabrir DCN no caminho crítico. Também não justifica tratar o restante como simples ativação de uma flag.

Ordem proposta, com testes a nomear nos próximos candidatos (nomes abaixo são propostas, não testes já existentes):

1. ROLLBACK_OFFLINE_GUARDS: corrigir A1/A2 e testar mutações sem hardware; UMD_CAPS_FAIL_CLOSED: corrigir A3/A4. Zero corridas físicas nesta fase.
2. Reconciliar B7_UMD_NODE1_TRIPOD e retirement usando arquivos arquivados. Nova corrida somente após mudança material e critério de abandono escrito.
3. WDDM_SHADER_TRIPOD: shader realmente despachado, resultado dependente de entradas, fence sobre poison e comparação integral. Pré-requisito de jogos; não substitui vídeo.
4. D3D_DWM_GPU_FRAME: recursos residentes, shaders e operações gráficas reais, Present com conteúdo não nulo, pixels conferidos e DWM estável na saída da placa.
5. VULKAN_GAME_FRAME: render Vulkan real e jogo visível sem fallback; depois TDR_MEC_DISPLAY_SURVIVES, com hang detectado, reset, novas submissões e display sobrevivendo.

Arquivar futuras provas em `docs/evidence/<data>_<nome-do-teste>/`, associando hashes do pacote, log delimitado, resultados e rejeições. Teto inicial: zero novas corridas até resolver os defeitos offline; depois, no máximo uma por hipótese material, com resultado binário e abandono definidos antes do bind.

**Conclusão:** existe base real para continuar. O projeto está em integração WDDM/UMD e construção do backend gráfico, não em acabamento de um driver de jogos. Desktop acelerado e jogos: **NÃO VALIDADO EM HARDWARE**.


