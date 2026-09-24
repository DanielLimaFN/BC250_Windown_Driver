# Prompt de auditoria externa — BC-250 WDDM, Corridas 93 a 98

> **Para quem recebe este documento:** você é uma IA convidada a **auditar** o
> trabalho recente neste repositório. Não é para implementar nada; é para dizer
> se estamos no caminho certo, o que esquecemos, o que está errado e como
> avançar. Escreva em **português**. Leia este documento inteiro antes de abrir
> qualquer arquivo.
>
> Preparado em 24/09/2026. Repositório: `C:\Users\Daniel\Desktop\NamixWDDM`,
> branch `p6-rendernode-wddm`. Nada desta sequência foi commitado; o estado está
> na árvore de trabalho (`git status`, `git diff`).

---

## 1. O projeto em uma página

**Objetivo final:**
- Windows 10 22H2 rodando jogos na **AMD BC-250** (`1002:13FE`, GC 10.1.3
  "Cyan Skillfish", 16 GB GDDR6), com o monitor no DisplayPort da própria placa;
- aceleração por GPU via D3D11/D3D12/Vulkan;
- **sem WARP e sem fallback de CPU.**

**Peças:**

| Peça | O que é | Onde |
|---|---|---|
| KMD | `bc250mcdm.sys`, miniport WDDM 2.7 (display + render). Display herdado do GOP do UEFI; escrita em registrador de DCN é proibida. | `driver/wdk/`, `driver/core/` |
| UMD | `bc250umd.dll`, UMD D3D10/11 que traduz para Vulkan (ponte interna) | `driver/umd/` |
| RADV | fork do Mesa/RADV com winsys WDDM (`wddm2`), usado pela ponte Vulkan da UMD | `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm` (fora do repo; patch em `external/radv-wddm/`) |
| Ferramentas | binder de laboratório com guardião/rollback, auditorias Python com testes de mutação, pré-voos | `tools/` |

**Engines:**
- **Node 0 = SDMA:** cópia, paginação e composição do DWM;
- **Node 1 = GFX:** anel gráfico, RADV;
- **VMIDs:** pool próprio no KMD.

**Normas:** leia `CLAUDE.md` (leis 1 a 10) e `rules.md`. As que mais importam
para a auditoria:
- **Lei 1, sinceridade:** o que não foi medido é `NÃO VALIDADO EM HARDWARE`.
- **Lei 2, tripé no silício:** RPTR==WPTR, fence escrita pela GPU sobre poison,
  e dados conferidos.
- **Lei 4, zero fakes:**
  - nenhuma DDI devolve sucesso sem backend;
  - nenhuma recusa muda;
  - nenhum fallback de software.
- **Lei 8, vocabulário de confiança:**
  - **P0** medido no silício;
  - **P1** derivado do upstream (Linux amdgpu/Mesa);
  - **P2** parcialmente observado;
  - **H** hipótese.
- **Lei 9:** corridas físicas são caras. O dono do projeto (Daniel) executa;
  a IA compila e analisa.
- **Lei 10, formato do critério:**
  - critério escrito **antes** de cada corrida;
  - assinatura e implicação em campos separados;
  - desfecho "E — fora do mapa";
  - desfecho parcial não é desfecho.
- **Uma variável comportamental por corrida.** Todo portão novo de auditoria é
  verificado por mutação.

---

## 2. Fonte da verdade e documentos a ler

1. **`BASE_DE_AVANCO.md`:** entradas mais recentes no topo. Leia as entradas de
   23/09 e 24/09.
2. **Documentos desta sequência, em ordem:**
   - `docs/386` (DX.48 e critério da 93);
   - `docs/387`, resultado da 93: primeiro desktop interativo, ~0,6 FPS;
   - `docs/388`, DX.49 em 4 partes: staging persistente, lote de draws, trace
     leve, dirty rects;
   - `docs/389`, Corrida 94 (tem uma **correção** no topo);
   - `docs/390` (DX.49b, log durável);
   - `docs/391` (Corrida 95);
   - `docs/392` (DX.50, dirty rects desligados);
   - `docs/393` (Corrida 96, DX.51 e o **adendo §4**);
   - `docs/394` (Corrida 97, DX.52 e critério da 98).
3. **Anteriores, se precisar de contexto:**
   - `docs/370`–`385` (DX.42 a DX.47: VMID lag, anel GFX em tamanho cheio,
     preempção);
   - `docs/164_ROADMAP_WDDM_JOGOS.md`.
4. **Evidência bruta:** `docs/evidence/2026-09-23_*` e `2026-09-24_*`
   (Corridas 93–97). Em cada pasta:
   - **log do KMD:** `display-log-this-bind-FINAL.txt` ou
     `-RECUPERADO.txt`, resgatado do log cumulativo depois de travamento;
   - **logs da UMD por processo:** `bc250_umd_log_<pid>*.txt`, mais
     `umd-logs-pos-reboot/` e `umd-logs-residuais-do-bind-anterior/`;
   - **saída do teste D3D11:** `d3d11-native-conformance-console.txt`;
   - **guardião:** `safe-bind-watchdog-journal.jsonl`.

---

## 3. O que aconteceu, em resumo (confira tudo contra a evidência)

| Corrida | Candidato | Mudança | Resultado medido |
|---|---|---|---|
| 93 | 3.253.1.0 | DX.48: log do ciclo de vida do adaptador | **Desktop visível e interativo**, 35–38 Present/min (~0,6 FPS), sem travamento, 60/60, zero TDR |
| 94 | 3.254.1.0 | DX.49, 4 partes juntas, a pedido do dono | Travou ~35 s depois do bind, tela preta. Lote e dirty rects ativos no KMD |
| 95 | 3.255.1.0 | DX.49b: log da UMD durável | ~109–134 Present/min (3×), defeito visual, **travou** |
| 96 | 3.256.1.0 | DX.50: dirty rects desligados | Tela piscando, **travou** |
| 97 | 3.257.1.0 | DX.51: lote desligado (staging e trace mantidos) | Desktop normal, sem piscar, 123 Present/min, **travou** |
| 98 | 3.258.1.0 | DX.52: só instrumentação das recusas de CreateAllocation | **Preparada, não executada** |

**Conclusões atuais, com a classe declarada pela própria equipe. Audite cada
uma:**
- **P0:**
  - os 4 travamentos (94–97) são duros: Kernel-Power 41, sem BugCheck;
  - o log do KMD termina no meio de uma submissão SDMA de paginação, com a GPU
    ociosa e o último tripé fechado;
  - zero TDR, fault ou timeout.
- **P0:** o ganho de vazão veio do **staging persistente** (a 97 manteve 123
  Present/min sem o lote).
- **H forte:** o **pisca-pisca** vem do anel de 16 MB **por device** do lote.
  - O segmento de VRAM tem só 257 MB; o VidMm despeja os buffers do swapchain
    do DWM.
  - Resultado: a ponte Vulkan→WDDM (escape `COMPOSITE_BLT`) falha com
    `SpaceResolveAddress status=9`.
  - Contagens: 89 falhas na 95, 438 na 96, 0 na 97.
- **H:** o travamento acompanha o **ritmo de criação e destruição de
  contextos/VMIDs**: ~100/min na 93 contra ~290–390/min nas 94–97.
  - Esse ritmo vem de um **laço do Explorer**: alocação de 3.407.872 bytes →
    `E_INVALIDARG` → destrói o device inteiro → recomeça a cada ~1,5 s.
  - A causa da recusa é desconhecida, porque as saídas de erro do
    `CreateAllocation` do KMD eram mudas (corrigido na DX.52).
- **H concorrente:** o próprio staging persistente (4–11 MB por device, mapeado
  permanentemente) causa o travamento.
- **Correção feita pela equipe:** a Corrida 94 afirmou "nenhum log da UMD
  sobreviveu". Era falso: a sessão não tinha permissão de leitura em
  `C:\Windows\Temp`.

---

## 4. O que queremos da auditoria

Responda às perguntas abaixo **com evidência**: arquivo, linha ou trecho de log,
e a classe P0/P1/P2/H de cada afirmação sua. Se discordar de uma conclusão
nossa, diga qual e por quê.

### A. Estamos no caminho certo?

1. A estratégia de **bissecção uma variável por vez** (DX.50 → DX.51 → DX.52)
   está bem conduzida? Houve algum passo que não separava as hipóteses e que
   poderíamos ter evitado?
2. A decisão de juntar 4 mudanças na DX.49 custou quantas corridas? O que
   deveríamos ter feito diferente?
3. A leitura "o travamento não vem do lote nem dos dirty rects" se sustenta nos
   logs da 96 e da 97?
4. A Corrida 98 como está (só instrumentação) é o melhor uso da próxima corrida?
   Proponha alternativa se houver uma que responda mais com o mesmo custo.

### B. O que esquecemos?

5. **Travamento duro sem BugCheck** e com a GPU ociosa: que causas conhecidas em
   GPUs AMD/WDDM produzem isso? Considere, entre outras:
   - acesso a MMIO de bloco desligado;
   - DMA da GPU para endereço de host inválido travando o barramento PCIe;
   - interrupção em tempestade;
   - deadlock com spinlock em DISPATCH_LEVEL;
   - programação de VMID/GCVM com trabalho em voo;
   - paginação do VidMm.

   Para cada causa, diga que evidência a confirmaria ou refutaria nos nossos
   logs.
6. O **desarme e rearme de VMID** no pool
   (`[VMID_POOL] LIBERADO`/`DESARMADO`, `FLUSH_POOL_VMIDS`, em
   `driver/wdk/bc250_active_miniport.c`) é seguro enquanto há IBs em voo para
   aquele VMID? Compare com o Linux amdgpu (`amdgpu_vmid.c`, `gmc_v10_0.c`).
7. O caminho de **paginação** (`BuildPagingBuffer`, submissões SDMA do Node 0 com
   `PT_WALK`/prova de canário) segura spinlock ou faz polling longo em
   DISPATCH_LEVEL? Isso poderia congelar a máquina sob paginação intensa?
8. O **log síncrono do KMD** (`Bc250DisplayLog`, gravando em arquivo a cada linha
   em PASSIVE; ~200 mil linhas por corrida) pode, por si só, causar ou mascarar
   o travamento?
9. Há **recusas mudas** (Lei 4) que ainda não vimos? Procure `return
   STATUS_INVALID_PARAMETER`, `STATUS_NOT_SUPPORTED` e semelhantes sem log antes,
   nas DDIs do KMD e nos callbacks da UMD.
10. A **ponte Vulkan→WDDM** faz uma cópia de 11 MB por SDMA a cada Present, lendo
    uma VA de memória da RADV e escrevendo numa alocação WDDM. Há risco de a VA
    de origem ter sido liberada ou movida entre a resolução e a cópia (corrida
    com o VidMm)?
11. O **laço do Explorer:** por que `pfnAllocateCb` recusaria uma alocação de
    3.407.872 bytes com `hRTResource`? Olhe o formato da ABI privada
    (`driver/core/bc250_allocation_abi_contract.c`, `Bc250AllocationAbiParse`)
    e a UMD (`Bc250WinsysAllocateForResource`, `Bc250Umd10CreateResource`).
    Tente achar a causa **sem corrida**.

### C. Como avançar?

12. Proponha a sequência das próximas **3 corridas**, cada uma com:
    - uma variável;
    - critério no formato da Lei 10 (assinatura e implicação separadas, e
      desfecho E).
13. Como recuperar a **vazão** sem a pressão de memória? Considere:
    - anel compartilhado por processo em vez de por device;
    - anel menor;
    - memória de sistema em vez do segmento de 257 MB;
    - aumentar o segmento.
14. Como recuperar os **dirty rects** com segurança? O scanout recebe escrita por
    dois caminhos: a cópia do KMD no Present e o `radv_bc250_present_scanout`
    direto da UMD. Isso é um problema?
15. O que falta, em ordem, para o **desktop acelerado minimamente funcional**
    (visível, interativo, estável por minutos)? E depois, para rodar um jogo
    D3D11 simples?

---

## 5. Regras para o auditor

- **Não invente medição.** Se um número não estiver nos logs, diga que não está.
  Toda afirmação técnica leva a classe P0/P1/P2/H.
- **Offsets de registrador só com proveniência:**
  - `docs/03_CATALOGO_DE_REGISTRADORES.md`;
  - `inc/linux_ref/`.
- **Não proponha:**
  - WARP, renderização por CPU ou fallback de software;
  - "trocar a meta por algo mais fácil" (compute, LLM, render-only).
- **Não proponha escrita em registrador de DCN.**
- Se sugerir código, diga **qual teste offline** o trava e **qual portão de
  auditoria** o protege, com uma mutação que o faça reprovar.
- **Seja direto.** Se algo que fizemos estiver errado, diga que está errado.

---

## 6. Formato da resposta

Entregue um documento com estas seções:

1. **Veredito geral:** estamos no caminho certo? Sim/parcial/não, em até 5
   linhas.
2. **Conclusões nossas que você confirma:** com evidência.
3. **Conclusões nossas que você contesta:** com evidência e alternativa.
4. **Riscos e esquecimentos:** lista priorizada, do mais provável de causar o
   travamento ao menos provável.
5. **Respostas às perguntas 1–15:** numeradas.
6. **Plano das próximas 3 corridas:** com critérios no formato da Lei 10.
7. **Lacunas desta auditoria:** o que você não conseguiu verificar e por quê.
