# Corrida 73: itens 1, 2 e 3 (offline) e critério da Corrida 74 (rotação com conteúdo Vulkan)

> **22/09/2026.** Evidência da Corrida 73: `docs/evidence/2026-09-22_21-49-38-DESKTOP-HQAG6T4-lab-binding/`.
> A Parte 3 é o critério da Corrida 74, **escrito antes do ensaio** (Leis 9 e 10).
> **Estado da faixa DX.28: NÃO VALIDADO EM HARDWARE.**

---

## Parte 1 — Item 1: TDR de 240 ondas

**O IB travado não é draw.** O TDR 1 aconteceu na FenceId 108, IB 0x240030000, 280 DW, do harness D3D11. O IB contém um único `DISPATCH_DIRECT 160×90`, uma cópia imagem→buffer por compute (1280/8 × 720/8), que é o readback do alvo 1280×720 do harness. O mesmo tipo de IB do DWM (`DISPATCH 320×135`, 2560×1080) retirou 8 de 8 vezes. O estado de registrador dos dois é idêntico: só mudam o endereço do shader, um ponteiro e a largura.

**Fault latchado no TDR 1 (P0):**
- `Status=0x00101281` → **Vmid=1**, **Cid=9** (SQC, busca de instrução), `PermissionFaults=0x8`, `Rw=0`;
- página **0x2003C0000**.

**Cruzamento (P0 nos fatos, H no mecanismo):**
- O VMID 1 pertencia ao Ctx 3BB (DWM), raiz 0x45FDE0000. A arena de shaders desse DWM fica em 0x2016000xx.
- O shader do dispatch travado (harness, VMID 4) fica em `COMPUTE_PGM_LO=0x02003C0B` → **0x2003C0B00**, que é a página do fault.
- Cada processo tem a arena numa VA diferente: autoteste em 0x2000400xx, outro processo em 0x200100xx, harness em 0x2003C0xx, DWM em 0x2016000xx. O VMID 1 não tinha motivo próprio para buscar código ali.
- No espaço do VMID 1, essa VA pertence à alocação PRIMARY do DWM, com PTE `0x61`, sem o bit de execução (uma página de código aparece com `0x71`).
- **Leitura (H forte):** o código do harness foi buscado **com a tabela do DWM**. O fault de execução manda a busca para a página dummy, e as ondas executam lixo e nunca retiram, o que dá as 240 ondas.

**Mecanismos candidatos, não provados:**
- (a) A raiz de cada VMID é instalada **por MMIO da CPU no instante do submit**, fora da ordem do anel. O pool reatribui VMIDs o tempo todo (14 alocados e 12 liberados até o TDR). A raiz do harness, 0x45FD63000, já tinha sido dona do VMID 01 pelo Ctx 1D6, antes do DWM.
- (b) Diferença frente ao upstream: a cada job no anel gráfico o upstream emite pipeline sync, flush de VM **dentro do anel** (base da tabela + invalidação + `PFP_SYNC_ME`) e `ACQUIRE_MEM` antes do IB. Nós emitimos só IB + fence.

**Limite:** o latch guarda o **primeiro** fault desde o último TDR, então o instante não é conhecido. O TDR 2 tem outro fault latchado (VMID 2, escrita), menos conclusivo.

**Instrumento novo (somente leitura):** `[GFX_FAULT_SENTINELA]`. A cada fence gráfica aposentada, o KMD lê `GCVM_L2_PROTECTION_FAULT_STATUS` e registra só a transição, com a faixa de fences, o último IB, o VMID e o endereço. Isso identifica em que submissão o fault nasce, sem escrever nada. O dreno continua exclusivo do caminho de TDR.

## Parte 2 — Item 2: os quadrados do desktop (causa encontrada)

**P0, `bc250_umd_log_5928-FINAL.txt` (dwm.exe):**
- Às 21:50:48.20, depois do draw #21, o alvo `A7F5770` estava com **86400/86400** amostras não-nulas.
- Em seguida o DWM fez 4 `ResourceCopyRegion` do buffer anterior (`A7F3B70`) para o alvo, copiando as regiões não alteradas, e apresentou.
- A grade do Present, **logo depois das cópias, deu 0/86400**.
- Depois vêm `RotateResourceIdentities` e o quadro seguinte, que reconstrói só a região suja (o rodapé).
- Mais tarde a grade mostra só caixas pequenas. Uma delas, de 24 px, anda por (1088,736) → (1104,744): tem cara de cursor do mouse. O resto é zero.

**Causa (P0 por leitura de código e P0 pelo log):** `Bc250DxgiRotateResourceIdentities` girava `Allocation` e `pCpuShadow`, mas **não** a `VkImage`/`VkDeviceMemory`, que é onde o conteúdo renderizado vive. O "buffer anterior" que o DWM copia era uma imagem Vulkan que ficou para trás, com zeros ou com um quadro velho. O modelo flip do DWM redesenha só a região suja e confia que o buffer guarda o resto. Daí os quadrados que aparecem e somem.

**Correção DX.28 (única variável comportamental da 74, UMD):**
- a rotação leva junto `VkImg`, `VkMem`, `VkMemSize`, `VkLayout`, `VkBuf`, `VkResourceCreated` e `WddmAllocationUploaded`;
- `Bc250UmdVkRtvFollowResource` refaz `VkView`/`VkFb` quando a imagem do recurso não é mais a da criação. Antes de destruir o framebuffer antigo, fecha o render pass e faz flush;
- chamada no Draw e no Clear, com recusa nomeada `RTV_NAO_SEGUIU_ROTACAO`.

A SRV não precisa de ajuste, porque cria views por lote de draw.

## Parte 3 — Item 3: `PREAMBLE_CNTL` do fork

O Mesa upstream **não** emite `PREAMBLE_CNTL` em `radv_emit_graphics`; a inserção com payload 0/1 é só do fork. A correção fiel é removê-la. Ela está pronta em `external/radv-wddm/pending/DX29-remover-preamble-cntl-do-fork.patch` (`git apply --check` limpo), **não aplicada**. Uma variável por corrida: o executor copia o ICD do `build\` do fork, então recompilar agora poria duas variáveis na 74. Ela fica para a 75.

## Verificação

- **Builds:** UMD EXIT=0 com o teste da rotação estendido (a `VkImg` precisa girar e voltar em 3 voltas), `build-contract` EXIT=0 e SYS de laboratório compilado.
- **Auditorias PASS:** segurança do MCDM com o portão DX.28 novo, matriz de DDI, callbacks no-op e RADV.
- **Mutações que reprovam:**
  - 5 na auditoria: rotação sem `VkImg`, RTV sem registrar a imagem, Draw sem seguir, teste sem conferir a `VkImg`, sentinela escrevendo;
  - 1 no teste: rotação sem `VkImg` derruba o `bc250_d3d_umd_test` na linha 832.
- **Furo no meu portão, pego pela mutação M2:** a linha exigida também existia dentro da `FollowResource`. O portão foi restrito ao corpo da `CreateRtv`.
- **Pacote lab 3.235.1.0** assinado, com `Lab package audit: PASS`, NÃO INSTALADO:

| Artefato | SHA-256 |
|---|---|
| SYS assinado | `E395E3F2...` (fonte `A60F64C2...`) |
| UMD | `5D66F9B3...` |
| INF / CAT | `6066D2AA...` / `9643AD6D...` |
| ICD | `57D60ACB...` (igual à 72 e à 73) |

Strings conferidas: `RTV_SEGUE_RECURSO` e `DX.28: imagem Vulkan gira junto` na UMD; `GFX_FAULT_SENTINELA` no SYS.

---

## Parte 4 — Critério da Corrida 74

### 4.1 Perguntas
1. A rotação passa a levar a imagem Vulkan, e a RTV a segue?
2. O quadro apresentado deixa de zerar depois das cópias do buffer anterior, isto é, o desktop aparece inteiro e estável?
3. Em que submissão nasce o fault que precede os TDRs (sentinela)?

### 4.2 Sinais
- **ROT-OK:** no log da UMD do dwm.exe, `RotateResourceIdentities LEAVE ... VkImg=X (antes Y) -- DX.28` com X≠Y, **e** pelo menos uma linha `RTV_SEGUE_RECURSO:`.
- **QUADRO-OK:** entre as grades do Present no dwm.exe (a `INSPECAO LINEAR_BUF GRADE` logo depois de `DXGI Present: Vulkan Destile`), depois da primeira com NonZero > 80000, **nenhuma** volta a 0/86400 enquanto o mesmo DWM vive, e pelo menos 10 grades de Present com NonZero > 0.
- **KMD:** pelo menos 5 `BYTE_PROOF PASS`.
- **Controle:** autoteste com 5/5 PASS nas três variantes.
- **Visual (Daniel):** desktop inteiro no monitor, e não blocos alternando com preto.
- **Sentinela:** linhas `[GFX_FAULT_SENTINELA]`. Informativa; não decide desfecho.

### 4.3 Desfechos
**A — A rotação era a causa dos quadrados**
- Assinatura: ROT-OK **e** QUADRO-OK **e** autoteste 5/5.
- Implicação: o conteúdo passa a persistir entre quadros. O desktop estável depende agora dos TDRs, e a sentinela diz onde atacar. Próximo: o mecanismo do fault (Parte 1) e o patch DX29 na 75.

**B — A rotação gira, mas o quadro ainda zera depois das cópias**
- Assinatura: ROT-OK, e alguma grade de Present volta a 0/86400 logo depois de `ResourceCopyRegion`.
- Implicação: a rotação não era a única causa. Próximo: o caminho de cópia (`oldLayout` fixo em `COLOR_ATTACHMENT_OPTIMAL` nas barreiras da UMD, e layout real da origem).

**C — A correção falhou no próprio mecanismo**
- Assinatura: `RTV_SEGUE_RECURSO RECUSADO` ou `RTV_NAO_SEGUIU_ROTACAO`, ou ROT-OK ausente com o DWM apresentando.
- Implicação: consertar o seguimento da RTV antes de concluir qualquer coisa sobre o quadro.

**D — Regressão**
- Assinatura: autoteste abaixo de 5/5, ou TDR novo associado ao flush feito dentro de `FollowResource`.
- Implicação: descrever e reverter a parte que regrediu.

**G — Fora do mapa**
- Descrever antes de julgar: BugCheck, desktop que aparece sem ROT-OK, fault GCVM novo etc.

Desfecho parcial não é desfecho (Lei 10.3). Os TDRs não decidem A ou B; a sentinela é insumo da faixa seguinte.

### 4.4 Execução
Boot frio (marcador armado) e depois `tools\EXECUTAR_D3D11_NATIVO.cmd -Force`. Anotar o que aparecer no monitor antes do rollback.
