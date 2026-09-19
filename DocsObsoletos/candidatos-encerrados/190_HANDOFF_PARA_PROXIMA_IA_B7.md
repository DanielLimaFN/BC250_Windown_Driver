# 190 — Handoff para a próxima IA: iniciar B7 sem perder o estado físico

Copie o texto abaixo como mensagem inicial para a próxima IA.

---

Você está assumindo o projeto **NamixWDDM**, um driver Windows 10 22H2 para a AMD BC-250
(`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2). A meta final é executar jogos no monitor
DisplayPort da própria placa, com D3D12/Vulkan, sem WARP, llvmpipe ou fallback de software.

Antes de alterar qualquer arquivo, leia integralmente, nesta ordem:

1. `AGENTS.md` — regras obrigatórias do repositório;
2. `BASE_DE_AVANCO.md` — fonte normativa do estado;
3. `docs/189_CHECKLIST_ATE_JOGOS_BC250.md` — checklist consolidado e caminho crítico;
4. `docs/188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md` — resultado físico mais recente;
5. `docs/183_HANDOFF_2026-09-07_B3_B4_SUBMISSAO.md` — estado do Node 1 e submissão;
6. `docs/173_DIMENSIONAMENTO_D3D11_PARA_DWM.md` — dimensão real do backend D3D11;
7. `docs/170_RADV_WDDM_FAIL_CLOSED_BRIDGE.md` e
   `docs/169_DWM_D3D11_ROOT_CAUSE_AND_NATIVE_WINSYS.md` — arquitetura do B7;
8. `docs/172_HANDOFF_2026-09-06_20-34.md` — ponto em que o RADV parou em `vkCreateDevice`.

## Estado verdadeiro que não pode ser regredido

- O miniport WDDM já chegou a **Código 0** com MMU global, VidPn, primary e Present.
- O IB real de paginação entregue pelo `dxgkrnl` executou sob VMID8 e fechou o tripé:
  `RPTR==WPTR`, fence física sobre poison e cinco canários conferidos. O B1 está fechado.
- Os caminhos Plano A não têm precedência por default. O caminho normal é `INDIRECT`.
- A SDMA0 recuperou de um hang induzido real: LiveKernelDump `0x117`, quiesce, soft reset,
  reconstrução do ring, NOP/fence física e **40 submissões reais readmitidas**. Isso está
  **VALIDADO EM HARDWARE** no doc 188.
- O item 25 ainda não fecha inteiro porque a perna “display visível sobrevive” não pôde ser
  medida: a tela já estava preta antes do TDR.
- `QueryEngineStatus` foi corrigida offline para medir RPTR/WPTR e falhar fechada, mas não
  foi chamada na rota TDR observada: **NÃO VALIDADO EM HARDWARE**.
- O DWM falha com `0xE0464645/0x887A0004` porque a UMD não possui backend D3D10/11.
- A negociação real chegou a `D3D11DDICAPS_3DPIPELINESUPPORT`. Não existe valor honesto
  para anunciar “nenhum pipeline”: declarar suporte antes de implementá-lo é falso sucesso.
- O ICD RADV explícito carrega, enumera e entra em `vkCreateDevice`, mas a chamada não
  retorna. Foram criados dois contextos WDDM Node 1 e medidos **zero submits Node 1**.
- Enumeração, DLL carregada, `vulkaninfo`, janela ou retorno `STATUS_SUCCESS` não provam
  aceleração. Toda execução precisa do tripé físico.

## Próximo objetivo

O trabalho ativo é **B7**, começando pela fundação compartilhada, não por caps falsos:

1. auditar e congelar `BC250_ALLOCATION_PRIVATE_V1`, incluindo versionamento, ownership,
   lifetime, erros e teardown de allocation/resource/queue/sync;
2. completar `driver/umd/bc250_wddm_winsys.*` para usar essa ABI;
3. localizar exatamente por que `vkCreateDevice` não retorna;
4. fazer a primeira submissão UMD/RADV originada pelo `dxgkrnl` chegar ao Node 1;
5. fechar o tripé no Node 1: ring drenado, fence GPU sobre poison e resultado byte a byte;
6. somente depois avançar por memória/sync, Vulkan compute, graphics e WSI;
7. reutilizar o mesmo backend para DXBC → SPIR-V → RADV/ACO e implementar as DDIs reais
   D3D10/11 exigidas pelo DWM.

A primeira entrega do B7 não é uma tela: é **um submit Node 1 da UMD com tripé completo**.
A primeira entrega visível é o **DWM funcionando por 10 minutos** no DisplayPort da BC-250.

## Restrições obrigatórias

- Não repetir agora o ensaio TDR. Ele só volta a produzir informação nova depois que o
  B7 fornecer imagem visível; então uma corrida fecha a perna final do item 25.
- Não escrever registradores DCN. P9 é opcional e está fora do caminho crítico.
- Não reabrir render-only, `MiscCaps.ComputeOnly`, MCDM como destino ou cross-adapter.
- Não completar fence pela CPU, não simular execução, não criar delay como GPU fake e não
  publicar caps sem backend.
- Toda hipótese deve ser rotulada `H`; toda ausência de prova física deve ser escrita como
  **NÃO VALIDADO EM HARDWARE**.
- Daniel executa as corridas físicas. Primeiro esgote leitura de fonte, testes offline e
  mineração de `docs/evidence/`. Antes de propor uma corrida, declare o que ela distingue,
  o teto de corridas, PASS, FAIL, rollback e critério de abandono.
- O worktree está deliberadamente sujo e contém trabalho do projeto. Não descarte, reverta
  ou sobrescreva mudanças existentes. Nunca use `git reset --hard` ou `git checkout --`.
- Use `apply_patch` para editar arquivos e `rg` para busca.

## Método esperado para esta sessão

1. Inspecione o código e os testes atuais do ABI/winsys/Node 1.
2. Compare a implementação com os handoffs e identifique o primeiro contrato incompleto
   por evidência de fonte, sem adivinhar.
3. Faça uma mudança pequena e fail-closed.
4. Acrescente teste que falhe por mutação do comportamento novo.
5. Execute as suites offline relevantes e a auditoria de segurança.
6. Atualize `docs/189_CHECKLIST_ATE_JOGOS_BC250.md` e a base apenas com o que foi realmente
   demonstrado.
7. Se chegar a um candidato físico, pare e entregue a Daniel um comando/runbook único,
   transacional, com evidência arquivada em `docs/evidence/` e rollback automático.

Comece informando, com referências de arquivo e linha, qual é o primeiro bloqueio concreto
entre o estado atual e o submit Node 1. Depois implemente a próxima unidade offline segura.
Não declare B7, Vulkan, D3D11, desktop ou jogos prontos sem prova física correspondente.

---

## Nota para Daniel

O handoff acima aponta deliberadamente a próxima IA para **Node 1 + winsys**, porque esse é
o primeiro elo comum ao Vulkan e ao backend D3D10/11. Ele também impede que a IA gaste uma
nova corrida repetindo o TDR antes de existir imagem visível.
