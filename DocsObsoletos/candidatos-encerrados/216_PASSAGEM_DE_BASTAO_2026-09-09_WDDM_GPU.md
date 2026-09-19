# Passagem de bastão — WDDM acelerado na BC-250

**Data:** 09/09/2026. **Workspace:** `C:\Users\Daniel\Desktop\NamixWDDM`.
**Destinatária:** próxima IA responsável por continuar o desenvolvimento.

## 1. Missão e ponto de retomada

Daniel autorizou corrigir o necessário para chegar a WDDM acelerado pela GPU. A meta é desktop e jogos no Windows 10 22H2 x64, na AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2), monitor conectado ao DisplayPort da própria placa, D3D12/Vulkan, **sem WARP ou fallback de software**.

**Não entregamos essa meta ainda.** O Node0/SDMA reproduz execução real; o Node1/MEC aceita o envio, mas não comprova conclusão. A UMD continua sem backend D3D10/11 habilitado para o DWM. Corrigir o Node1 é o próximo marco de execução, não equivale a implementar renderização ou obter desktop visível.

**Ponto exato de retomada:** candidato **3.106.1.0 assinado, auditado e não instalado nesta sessão**, preparado para obter a evidência que faltou sobre o Node1. Não há processo de build ou ferramenta aguardando continuação. Não foi criado commit nem branch nesta sessão. Há numerosas alterações anteriores no workspace: preservar o trabalho existente, não fazer reset/clean nem atribuir todo o diff a esta sessão.

O último estado do adaptador consultado foi **BasicDisplay / Código 0**, após rollback. Isso é observação datada, não garantia do estado quando você assumir. Confira o último binding antes de agir; se Daniel já rodou o 3.106.1.0, analise essa corrida primeiro.

## 2. Leitura inicial e regras

1. [AGENTS.md](../AGENTS.md), seguido de [BASE_DE_AVANCO.md](../BASE_DE_AVANCO.md): fonte única de verdade, tabelas de prova e prioridades.
2. Este documento, que consolida a sessão, e [handoff 215](215_HANDOFF_2026-09-09_NODE1_PRIMEIRO_POLL.md), que especifica o candidato atual.
3. Análises das duas corridas abaixo; depois [handoff 214](214_HANDOFF_2026-09-08_MEC_TLB_MMIO.md) para os detalhes da correção anterior.
4. Roadmap [164](164_ROADMAP_WDDM_JOGOS.md) e checklist [189](189_CHECKLIST_ATE_JOGOS_BC250.md) para seguir além do Node1.

Arquitetura vigente: display herdado do UEFI GOP, VidPn fixo, scanout em VRAM, Present blt via SDMA, VSync lido do OTG. Não escrever DCN fora de P9. Node0 usa VMID8; Node1 usa VMID9; bootstrap físico usa VMID0. Não trocar registradores, VMIDs ou comandos por tentativa sem procedência.

Não simular fence, preencher dados pela CPU para fabricar prova, converter erro em SUCCESS ou ligar `BC250_D3D10_11_NATIVE_READY` sem backend. Separar medição física, resultado offline e hipótese. Toda alteração não exercitada recebe **NÃO VALIDADO EM HARDWARE**.

AGENTS.md, regra 10: a corrida física é executada por Daniel, com boot frio entre binds, teto e critério de abandono. A autorização para corrigir não elimina essa regra específica. Nesta sessão não executamos bind, reset de fusível ou nova corrida por conta própria. Não repetir hang TDR induzido para tentar comprovar recuperação MEC: o backend TDR atual recupera SDMA.

## 3. De onde partimos

O texto recebido apontava para `docs/213_HANDOFF_2026-09-08_CANDIDATO_3.104.1.0_TLB_SPINLOCK.md`. Esse trabalho anterior introduziu `TlbLock` no adaptador, protegeu `Bc250ActiveFlushVmidTlb` e isolou flush de VMID8 no Node0 após um 0x119 com falha de transação TLB.

Não refizemos a análise daquele dump nesta sessão. Tampouco adotamos como garantia universal frases históricas como “100% eliminado”: uma corrida sem repetição não prova ausência de toda uma classe de falhas.

Daniel relatou tela preta e atuação do watchdog. Investigamos duas corridas, implementamos o 3.105.1.0, analisamos sua execução por Daniel e preparamos o 3.106.1.0.

## 4. Corridas analisadas e resultados físicos

| Corrida | Resultados medidos | Limite do resultado |
|---|---|---|
| 08/09 23:15, 3.104.1.0 | 11 transações TLB status=0; cinco tripés SDMA completos; VMID9 instalado; Node1 Accepted 17→18, Retired 17→17, WPTR 204→216, RPTR 204→204 | Node1 não concluiu. TDR recuperou SDMA, não comprovou recuperação MEC. StopDevice falhou ao restaurar MMU porque MMIO já estava vazio |
| 09/09 08:26, 3.105.1.0 | Cinco tripés SDMA; 13 transações TLB status=0; restauração MMU com Regs=26; tradução CPU do IB válida | Node1 continuou pendente. A captura MEC planejada no 3.105 não apareceu; causa exata da não conclusão permanece aberta |

### Evidência da corrida 23:15

Diretório: [2026-09-08_23-15-50-DESKTOP-HQAG6T4-lab-binding](evidence/2026-09-08_23-15-50-DESKTOP-HQAG6T4-lab-binding/).

- [ANALISE_POS_CORRIDA.md](evidence/2026-09-08_23-15-50-DESKTOP-HQAG6T4-lab-binding/ANALISE_POS_CORRIDA.md) contém a atualização final: rollback confirmado às 23:18:53, BasicDisplay/Código 0 às 23:19:11. A observação intermediária de Código 43 foi superada.
- TDR ocorreu às 23:16:12, antes do watchdog de 180 segundos. Não confundir os dois eventos.
- `display-log-post-run.txt` foi recortado do log cumulativo a partir do offset 2928377 do baseline. Inclui eventos posteriores ao arquivo inicialmente coletado. SHA-256: `CD32DD233F8C66BEA45668E4C8BEC16E778107A6225E53E4FE3975B86DD86CBE`.
- `post-run-inspection.json` conserva consulta de eventos e dispositivo. O MEMORY.DMP encontrado ainda era da corrida anterior das 22:21, não um dump novo desta corrida.

### Evidência da corrida 08:26

Diretório: [2026-09-09_08-26-06-DESKTOP-HQAG6T4-lab-binding](evidence/2026-09-09_08-26-06-DESKTOP-HQAG6T4-lab-binding/).

- [ANALISE_POS_CORRIDA.md](evidence/2026-09-09_08-26-06-DESKTOP-HQAG6T4-lab-binding/ANALISE_POS_CORRIDA.md) e `post-run-inspection.json` documentam os resultados.
- SYS instalado identificado como `19F5D13030D7B7BEF08D2D8A935F8F0A62D783AF1A11531C901A455E809DFBC2` (3.105.1.0).
- `Global GFXHUB MMU restored to snapshot: Regs=26` exercitou com sucesso a correção de ciclo de vida do MMIO. Não prova todos os caminhos de erro do encerramento.
- Root VMID9 `0x45FFB0000`; PDE `0x45FFAF001`; PTE `0x451FCC071`; VA `0x11000` resolveu para PA `0x451FCC000`. É tradução lida pela CPU, não prova de conteúdo ou busca GPU.
- Evento Display/4101 às 08:26:23; watchdog ROLLED_BACK às 08:29:09, exit=0; BasicDisplay/Código 0 confirmado. Consulta registrada não encontrou novo BugCheck/boot não limpo no intervalo.
- `display-log-post-run.txt` parte do offset 3130810, SHA-256 `352E94A5D48598FB927376387ADF82186CD0DDE467941581E4BA34B95B622E87`.
- Zero `MEC_PENDING_ARCHIVE` e `MEC_IB_DESCRIPTOR_ARCHIVE`. Os contadores do registro em `probe-registry-at-bind` antecedem o submit Node1: não sustentam a conclusão de que o timer não rodou.

## 5. O que corrigimos no 3.105.1.0

Detalhes e hashes históricos: [handoff 214](214_HANDOFF_2026-09-08_MEC_TLB_MMIO.md). Evidência offline em `docs/evidence/2026-09-08_MEC_TLB_MMIO_OFFLINE/`.

1. **MMIO liberado cedo demais:** `Bc250SubmissionEngineReleaseResources` desmapeava `Core.MmioTransport` antes de StopDevice restaurar MMU. Retiramos esse unmap do consumidor MEC. `Bc250DxgkDdiStopDeviceReadonly` mantém a responsabilidade pelo unmap final. **Exercitado na corrida 08:26.**
2. **Encerramento:** MEC offline antes de restaurar VMID9; depois MMU global e encerramento readonly. O timer/DPC é drenado antes de liberar recursos.
3. **TLB compartilhada:** o caminho MEC `Bc250SubmissionApplyCoherency` ainda acessava REQ/ACK compartilhados sem a trava nova do adaptador. Criamos wrapper com `TlbLock`, com implementação `Bc250SubmissionApplyCoherencyLocked`. Ordem engine Lock → TlbLock. A transação devolve todos os resultados depois de soltar a trava. A concorrência possível é verificável no código; atribuí-la ao hang Node1 permanece hipótese.
4. **Primeira telemetria MEC:** descritor real de quatro dwords lido do anel, captura de seis campos da fila após 100 ms de fence pendente, máscara de leituras válidas e arquivamento passivo no StopDevice. A captura não aconteceu na corrida seguinte: reconhecemos essa lacuna, não a tratamos como prova de timer parado.
5. **Page-table walk CPU do IB Node1:** marcador `PT_WALK NODE1_IB`, efetivamente observado na corrida 08:26.
6. **Auditoria e mutações:** `audit_mec_shared_mmio_lifetime` e `tools/test-mec-mmio-safety.py` passaram a impedir regressões de unmap, ordem de encerramento, trava e arquivamento.

## 6. O que fizemos no 3.106.1.0, ainda offline

Detalhes: [handoff 215](215_HANDOFF_2026-09-09_NODE1_PRIMEIRO_POLL.md).

O gate de 100 ms podia perder a primeira falha: `Bc250PersistentRingRetire` pode colocar a engine em FAULTED antes da captura. O teste reproduz esse caminho com fence regredida. **Não medimos essa sequência no hardware; não declarar que ela causou a corrida 08:26.**

- Removido o prazo da captura no polling. A observação pendente é preservada antes do retirement potencialmente falhar. Não é veredito de hang.
- Nova `Bc250SubmissionEngineArchiveDiagnostics`, chamada incondicionalmente no StopDevice após drenar o DPC e antes do Offline. Não exige ONLINE, VMID9, fence pendente ou tempo. Engine sem inicialização gera UNAVAILABLE.
- `MEC_STOP_STATE`: estado, fences submetida/retirada, polls, falhas, DPCs, último submit e fence observada. `MEC_STOP_QUEUE`: oito campos da fila, com máscara de leituras válidas. `MEC_LAST_DESCRIPTOR`: quatro dwords publicados.
- CPU lê prefixo físico do IB antes da publicação: no máximo 32 dwords, dentro da primeira página e da janela VRAM permanentemente mapeada, sem presumir continuidade entre PTEs. Arquiva no StopDevice como `NODE1_IB_CPU_ARCHIVE` e `NODE1_IB_CPU_WORD`.
- Teste executável cobre primeiro polling pendente, falha por fence regredida e retenção da captura após FAULTED. Novos gates/mutações verificam presença e ordem da captura independente e arquivamento do IB.
- INF fonte atualizado para 3.106.0.0; empacotador derivou laboratório 3.106.1.0. BASE_DE_AVANCO recebeu os resultados das corridas e o candidato atual.

Arquivos de código/testes alterados nesta sequência:

| Arquivo | Responsabilidade |
|---|---|
| `driver/wdk/bc250_submission_engine.c` | MMIO emprestado, wrapper TLB, captura no polling e fotografia StopDevice |
| `driver/wdk/bc250_submission_engine.h` | Campos de observação e declaração da função de arquivo |
| `driver/wdk/bc250_active_miniport.c` | Ordem StopDevice, chamada da fotografia, walker e prefixo físico do IB |
| `driver/wdk/bc250_active_miniport.h` | Armazenamento limitado do prefixo IB no contexto |
| `driver/tests/active_miniport_test.c` | Regressão executável do primeiro polling/falha |
| `tools/audit-bc250mcdm-safety.py` | Gates de MMIO, trava e observabilidade |
| `tools/test-mec-mmio-safety.py` | Mutações de fonte em memória, sem alterar arquivos reais |
| `driver/wdk/package/bc250mcdm.inf` | Versão fonte |

Não alteramos a implementação da UMD, formato PM4, VMIDs, controles de fault ou DCN nesta sequência. O SYS foi recompilado; a UMD assinada do pacote conserva o mesmo hash.

## 7. Identidade do candidato e validação

Pacote: `driver/wdk/package/output_lab`. Manifesto: `driver/wdk/package/output_lab.manifest.json`.

| Artefato | SHA-256 |
|---|---|
| SYS fonte, não assinado | `0C0487A8877DC34B91C19ACFF984F69CD063BF978ED7DA03487AC909627132EE` |
| SYS assinado | `C188B2DCA7F63350DFF71E4E91EC9ADC80B4D5C4D7CB447EFAAAE9CB73AAC1E6` |
| UMD assinada | `35FBD78186D300D84F25DB76D1AF1C3025C7FECA3211CA422D35619B116AF214` |
| INF | `230C1CEC7284A163E2AA028C137225F167E84FA17D9EE8E9254FEABCA6C18208` |
| CAT | `DD8078792EFB083E316EEFA684EB4412834ECEED25A69CA23F5E42205F1CAD33` |

Manifesto: DriverVer `09/09/2026,3.106.1.0`, DISPLAY_RENDER_LAB, RenderNode=1, WDDM2_7, persistent submission e shader dispatch habilitados. GpuvaLabPromotionEnabled=false e Node1TripodDefaultArmed=false por padrão; o comando de ensaio solicita os gates explicitamente. D3D10_11NativeReady=false e HardwareValidated=false.

Toolchain: VS2026, compilador 19.51.36256.0, SDK/WDK layout 10.0.28000.0, conforme manifesto. Usar scripts canônicos, não recompilar apenas um objeto e presumir compatibilidade dos demais.

Executado com sucesso: build contratual completo (inclui execução de active_miniport_test), build lab, auditoria de segurança, auditoria UMD Node1, empacotamento/assinatura e auditoria de pacote. Implementação atual aceita e **nove mutações recusadas**. Build contém avisos em ferramentas auxiliares; não foi declarado livre de warnings. **NÃO VALIDADO EM HARDWARE.**

Evidência consolidada: [2026-09-09_NODE1_PRIMEIRO_POLL_OFFLINE](evidence/2026-09-09_NODE1_PRIMEIRO_POLL_OFFLINE/), com logs completos, mutations.log, manifesto e source-hashes.json. Os arquivos `work_mec_3106_*.log` na raiz são cópias de trabalho, não a única prova.

Comandos de reprodução offline, a partir da raiz:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
python tools/test-mec-mmio-safety.py
python tools/audit-bc250mcdm-safety.py
python tools/audit-umd-node1-submission.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode
powershell -NoProfile -ExecutionPolicy Bypass -File tools/audit-bc250mcdm-lab-package.ps1
```

Não é preciso reconstruir um pacote idêntico para iniciar a análise. Se mudar código, versionar novo candidato e registrar hashes; não substituir silenciosamente a identidade já documentada.

## 8. Próxima ação e interpretação da próxima corrida

Primeiro ler `tools/_last_lab_binding.json` e identificar se já existe corrida posterior. Conferir versão/hash e o baseline do log. `C:\BC250_DISPLAY_LOG.txt` é cumulativo: recortar pelo offset da corrida, não misturar ocorrências antigas. Se watchdog ainda estiver trabalhando, distinguir observação intermediária do resultado final.

Se não houver corrida nova, o ensaio preparado tem **teto de uma corrida**, executada por Daniel depois de boot frio e preflights. PowerShell elevado, na raiz:

```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod -WatchdogTimeoutSeconds 180
```

Não burlar preflight/fusível nem armar TDR induzido ou fallback. A tela preta continua possível/esperada sem D3D11; esta corrida mede Node1. Não pedir repetição idêntica se falhar.

Critério de sucesso: **anel drenado + fence exata produzida pela GPU sobre poison + dados conferidos byte a byte**. Aceitação do submit e avanço de WPTR não bastam. Confirmar restauração MMU e rollback BasicDisplay separadamente.

Se falhar, analisar nesta ordem:

1. **Conteúdo do IB:** CapturedDwords deve cobrir os 17 dwords do ensaio de 68 bytes, quando acessíveis. Comparar palavras com o IB real construído pela UMD (WRITE_DATA + RELEASE_MEM), endereços e valores esperados daquele ensaio. CapturedDwords=0 significa não capturado; prefixo ausente não prova zeros. Se divergir, investigar upload/Lock/Unlock/paginação antes de alterar o MEC.
2. **Descritor publicado:** comparar MEC_LAST_DESCRIPTOR/MEC_IB_DESCRIPTOR_ARCHIVE com o endereço, tamanho e VMID realmente enviados. O descriptor PM4 já foi comparado com gfx_v10_0_ring_emit_ib_compute do AMD/Linux sem divergência que justificasse alterá-lo; não reabrir por palpite.
3. **Retirement:** consultar MEC_PENDING_ARCHIVE e MEC_STOP_STATE, engine State, fence física, LastSubmit, polls/falhas e DpcRuns. Falha de fence regredida só vira diagnóstico físico se aparecer na evidência; hoje é cenário de teste.
4. **Fila física:** respeitar ValidReads de MEC_STOP_QUEUE. Não interpretar registrador zero cuja leitura falhou. A fotografia StopDevice pode ser posterior ao TDR; não atribuir automaticamente seu estado ao instante original do hang.
5. Se bytes e descritor estiverem corretos mas não houver conclusão, investigar fetch/tradução/estado HQD sob VMID9 com base nos valores capturados. Bytes CPU corretos não provam busca GPU, nem FAULT_STATUS=0 exclui sozinho todos os defeitos de execução.
6. Se a coleta ainda faltar, investigar sua própria elegibilidade e o encerramento. Não concluir timer parado ou engine morta apenas pela ausência de linha. Em crash, usar dump novo com data/hash, não o MEMORY.DMP antigo.

## 9. Dívidas e cuidados técnicos para não perder o avanço

- `Bc250DisplayLog` faz log em arquivo apenas em PASSIVE_LEVEL e fora de paging ativo; mensagem emitida no DPC pode não aparecer no arquivo. Por isso guardamos observações no contexto e arquivamos após drenar o DPC. Não mover I/O de arquivo para dentro de spinlock.
- A máscara de validade dos registradores é parte da evidência. Não eliminar essa distinção por conveniência do parser.
- `Bc250ActiveTdrQuiesceLifecycle`/`Bc250ActiveTdrRestartLifecycle` chamam o backend SDMA. Não declarar recuperação completa do adaptador/MEC a partir do NOP SDMA.
- Falhas de desativação HQD e liberação de recursos ainda requerem revisão; o sucesso da restauração MMU em uma corrida não valida todo teardown com trabalho pendente.
- TlbLock protege transações compartilhadas; manter ordem de locks e soltura em todos os retornos. Não reinserir o unmap MMIO no engine consumidor.
- A CPU snapshot é limitada à janela existente e à primeira página. Ampliar arbitrariamente o mapeamento ou assumir continuidade física pode introduzir defeito novo no diagnóstico.
- O default Node0 continua INDIRECT real. Não reintroduzir Plano A como sucesso equivalente à execução do DMA buffer WDDM.
- O pacote 3.106 não contém uma correção comprovada da causa Node1. Ele corrige uma lacuna de medição que impedia localizar essa causa. Ser explícito com Daniel.

## 10. Onde queremos chegar depois deste bloqueador

**Marco imediato:** identificar e corrigir a causa real do Node1 pendente, fechando o tripé no silício. A seguir, repetir apenas quando houver critério de robustez que justifique a corrida.

**Marco de desktop:** implementar backend D3D10/11 nativo suficiente para o DWM, com recursos, shaders/comandos e sincronização reais; depois comprovar pixels produzidos pela GPU e apresentados pelo SDMA enquanto nosso driver permanece instalado. Apenas publicar caps ou trocar a flag de readiness seria falso avanço.

**Marco de produto final:** estabilidade, recuperação que cubra as filas usadas e display, D3D12/Vulkan e jogos nativos na BC-250. Seguir a base e o roadmap, sem usar percentuais ou build PASS como substituto dos testes físicos de saída.

**Resumo para a próxima IA:** preserve o que já funciona; comece pela evidência mais recente; não gaste outra corrida sem uma pergunta discriminante. A meta é WDDM realmente acelerado, e o próximo fato necessário é saber por que o IB Node1 de 68 bytes é aceito, mas não conclui.
