# Revisão das auditorias externas (ChatGPT 6 SOL e Gemini) sobre as Corridas 93–98

> 24/09/2026. Prompt: [doc 395](395_PROMPT_AUDITORIA_EXTERNA_CORRIDAS_93_A_98.md).
> Cada afirmação que mudaria o plano foi conferida no código ou nos logs antes
> de entrar aqui.
> **Regra da casa (F2.5, 13/09):** achado externo não entra sem verificação.

## 1. Confirmado

**VMID devolvido ao pool antes do desarme no silício** (ChatGPT, prioridade 1).
**P0 de leitura de código; H como causa do travamento.**
- Em `Bc250ActiveDestroyContext…` (`bc250_active_miniport.c:4406-4449`),
  `Bc250VmidPoolRelease` roda sob `VmidPoolLock`; o lock é **solto**; e só
  depois `Bc250GcvmUninstallVmid(liberado)` escreve MMIO e invalida a TLB.
- O `Bc250VmidPoolAcquire` roda no SubmitCommandVirtual GFX de **outro**
  contexto (`:12913`).
- **Janela:** um contexto novo pega o mesmo VMID, instala a raiz dele, e o
  desarme atrasado **desliga o contexto GCVM do novo dono**. O IB seguinte roda
  sob um VMID desarmado.
- **Frequência:** a janela se repete a cada destruição de contexto que já usou
  GFX. Com o laço do Explorer (~290 contextos/min nas 94–97, contra ~100 na 93)
  a chance sobe junto. Isso casa com "trava mais quanto mais rápido".
- **Referência Linux (P1):** o amdgpu só reusa um VMID depois das fences
  ativas (`amdgpu_ids.c`, ex-`amdgpu_vmid.c`).

**Polling sob spinlock** em SDMA (`RingLock`) e TLB (`TlbLock`), e o custo do log
síncrono do KMD. **P2:** a janela existe no código; a duração e a participação
no travamento **não** foram medidas.

**A última linha do log do KMD não localiza o travamento.** Em IRQL > PASSIVE ou
durante paginação, as linhas vão para um anel em RAM, e o anel morre com a
máquina (Gemini, ChatGPT). **P0 de leitura** de `bc250_display_ddi.c`. A frase
"GPU ociosa no travamento" passa a ser "últimas fences registradas fechadas".

**Staging como fonte da vazão** rebaixado para **P2** (ChatGPT). A 97 mostra que
o ganho **persistiu sem o lote**, mas nenhuma corrida isolou só o staging.

## 2. O achado que muda a leitura do laço do Explorer

**P0, conferido agora:** para o **mesmo token**, o KMD **aceitou**:

| Lado | Registro |
|---|---|
| UMD (Explorer), log `BC250_UMD_LOG_6504.txt` linha 41428, 00:14:46.771 | `Allocate ENTER: Token=0x524553001968005D Size=3407872 … hRTResource=00000000129B6EF0` → `Allocate LEAVE: hr=0x80070057` |
| KMD, log resgatado da 97, linhas 22719–22721 | `[CreateAllocation] Render resource … Token=0x524553001968005D, Size=3407872, CpuVisible=1` → `[McdmCreateAllocation] Result=0x00000000` → `[ActiveOpenAlloc] LEAVE: result=0x00000000` |

**Consequências:**
- **A recusa acontece no dxgkrnl depois do nosso CreateAllocation/OpenAllocation**,
  provavelmente ao validar o que o KMD devolveu em `DXGK_ALLOCATIONINFO` ou ao
  criar o backing no VidMm. Não é uma recusa muda do KMD.
- **DX.52:** a parte do KMD (`[CREATE_ALLOC_RECUSADO]`) provavelmente **não vai
  disparar** para essa classe. A parte da UMD (`CreateResource RECUSADO desc:`)
  continua útil: dá dimensões, usage, bind/misc e acesso de CPU do recurso.
- **ChatGPT disse** "a alocação aceita pelo KMD é outro pedido". **Errado:** o
  token é o mesmo.

## 3. Contestado ou refutado (Gemini)

- **"`hResource` deve ser NULL; passar `hRTResource` viola o contrato e causa o
  E_INVALIDARG"**: **refutado (P0).** No mesmo log do Explorer da 97, 1106
  chamadas passaram `hRTResource` não nulo e **928 voltaram `hr=0`**. Além
  disso, `hResource` é o que faz o dxgkrnl ligar `Flags.Resource` (d3dumddi.h,
  faixa de 13/09), e as primárias do DWM dependem disso.
- **Citações de log inexistentes:**
  - linhas no formato `[KMD:hh:mm:ss] … pfnAllocateCb FAILED`: zero ocorrências;
  - `[DX51] Draw batching ring DESLIGADO via registro`: zero ocorrências. O
    interruptor é constante de compilação.
  - As linhas de KMD que o Gemini atribuiu ao log da UMD estão, de fato, no log
    do KMD e em outro formato.
  - A **conclusão** delas (KMD aceitou) coincide com a medição; as **citações**,
    não.
- **Outras afirmações falsas ou sem evidência:**
  - "DX.52 reduziu o anel para 1 MB": **falso**, a DX.52 é só instrumentação.
  - "superfície 1024×832 da barra de tarefas": **sem evidência** no log.
  - "246 presents em 120 s": não confere com a contagem por minuto.
- **`EvictionSegmentSet = 0`:** **existe** (`bc250_active_miniport.c:5118`). A
  consequência ("VidMm rejeita alocações") é **H**, e não explica por que 928
  alocações com a mesma configuração passam.

## 4. Proposta de próximo passo

**DX.53, uma variável comportamental:** desarmar o VMID **antes** de devolvê-lo
ao pool.
1. O slot continua pertencendo ao contexto morto durante o desarme, então o
   `Acquire` de outro contexto não o recebe.
2. Com o desarme confirmado (readback e ACK do flush), o slot é liberado.
3. Se o desarme falhar, o slot **não** volta ao pool: fica em quarentena,
   declarada no log.

**Mais, só medição:** um contador de "VMID liberado com fence GFX do contexto
ainda não aposentada", para medir a outra janela (IB em voo) apontada pelo
ChatGPT.

**Por que antes de corrigir o laço do Explorer:**
- o laço gera a carga que abre a janela. Manter a carga (~290 contextos/min) e
  fechar a janela é o ensaio que **separa** "defeito do VMID" de "carga em si";
- a DX.52 (instrumentação) vai junto e já descreve o recurso recusado pelo
  dxgkrnl.

**Critério a escrever antes:**
- **A:** 300 s sem travamento com a mesma taxa de contextos → a janela do VMID
  era a causa (P0 de corrida).
- **B:** trava com a mesma taxa → não era, ou não era a única; próximo corte:
  staging ou log.
- **C:** a taxa de contextos cai muito → inconclusivo.
- **E:** fora do mapa.
