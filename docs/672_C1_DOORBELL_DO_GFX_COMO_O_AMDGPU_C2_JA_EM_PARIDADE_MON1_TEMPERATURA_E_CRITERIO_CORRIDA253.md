# 672 — C1: doorbell do anel GFX como o `amdgpu`; C2 já em paridade; MON.1 (temperatura e uso no monitor); critério da Corrida 253

**Data:** 09/10/2026. **Natureza:** offline, sem corrida. Pedido do Daniel: C1 e C2 na mesma corrida, e o monitor com clock e temperatura da GPU.

## 1. C2: nada a mudar (P0)

Probes desta própria placa, gravados durante o bind da 252 (`probe-registry-at-bind.json`, 08:51:49):
- `CP_PQ_WPTR_POLL_CNTL`: lê **0**.
  - O `EN` escrito pelo teste de bring-up não fixa (`HqdActiveWptrPollCntlEnLatched=0`).
  - O registrador nem retém o período (`Writable=0`).
- `CP_PQ_WPTR_POLL_CNTL1` (máscara de filas): **0** em todas as fotos de regime (`P1SnapActWptrPollCntl1`, `P1SnapReWptrPollCntl1`, `P1SnapSubWptrPollCntl1`). O bit só é ligado durante o teste de bring-up e restaurado depois (`RestorePassed=1`).

O `amdgpu` desliga o `EN` (`gfx_v10_0.c:6630`) e não usa a máscara para as filas do kernel. **Estamos iguais.** Na 253 a C2 é só verificação: os mesmos probes têm de continuar em 0.

## 2. C1: o que mudou

| | Até a 252 | 253 (= `amdgpu`, P1) |
|---|---|---|
| Doorbell do CP_RB0 | `CP_RB_DOORBELL_CONTROL = 0` (fechada); faixa do RB `0x0..0x108` | `RANGE_LOWER = 0x116<<2 = 0x458`, `RANGE_UPPER = 0x0FFFFFFC`, `CONTROL = OFFSET(0x116)\|EN = 0x40000458` (`gfx_v10_0_cp_gfx_set_doorbell`; índice `AMDGPU_NAVI10_DOORBELL_GFX_RING0 << 1`) |
| Publicação do WPTR por submissão | escrita do `GRBM_GFX_INDEX` em broadcast + leituras, `CP_RB0_WPTR`/`_HI` por MMIO + releitura (e, se divergisse, mais duas sequências) | shadow de 64 bits na RAM + `WDOORBELL64` com o WPTR em dwords (`gfx_v10_0_ring_set_wptr_gfx`); **nenhum MMIO** |
| `CP_RB_WPTR_POLL_CNTL` | `0x00400004` (`POLL_FREQUENCY=4`, escolha nossa, D0.4, H) | não é escrito: fica `0x00400000`, como a placa deixa (o `amdgpu` só o escreve no caminho de CG, desligado neste chip, `nv.c` `cg_flags=0`) |

**Layout das doorbells resultante:** MEC até 0x450 (MEC.1), GFX 0x458, SDMA0 0x800 (P2), IH 0xBC0 (P3.3 desligada). É o mesmo do `amdgpu`.

**Transação (Lei 5)** em `Bc250ActiveC1GfxDoorbellArm`, chamada no fim do ONLINE (anel drenado pelo preâmbulo) e depois da prova do restart do TDR:
1. **Preflight:** abertura do BIF ligada (`RCC_DOORBELL_APER_EN`) e `CP_MEC_DOORBELL_RANGE_UPPER < 0x458` (MEC.1 armada).
2. **Snapshot:** `CONTROL` e `RANGE_LOWER/UPPER`.
3. **Apply e readback:** escreve os três e relê.
4. **Prova:** 4 NOPs publicados **só** pela doorbell; o `RPTR` do CP_RB0 tem de chegar ao cursor. Só então `DoorbellActive=1`.
5. **Rollback:** sem prova, restaura os três registradores, publica o mesmo cursor por MMIO e segue no caminho antigo.

**Desarme:**
- no quiesce do TDR, rearmado com nova prova depois do restart;
- no OFFLINE, antes do rollback do anel.

**Allowlist:** `CP_RB_DOORBELL_RANGE_LOWER/UPPER` (`0xC168/0xC16C`, `gc_10_1_0_offset.h` 0x1DFA/0x1DFB) entram na lista de escrita, com o arme/desarme da C1 como únicos escritores.

**Fica igual:** a sonda de onda (`GfxWaveProbe`) lê `SQ_DEBUG_STS_GLOBAL` **só em troca de VMID** (~129 por corrida). É uma diferença pequena com o Linux, declarada e fora desta corrida.

## 3. MON.1: monitor

| Informação | Fonte | Toca a placa? |
|---|---|---|
| Temperatura (Tctl) atual e máxima | amostra de plataforma do **KMD** (par SMN 0x38/0x3C sob o lock do KMD), 1 por segundo, de volta no ramo da P1 atrás de `BC250_MON1_TEMPERATURA` | sim, a mesma leitura das corridas 199–236 |
| Uso da GPU por engine (3D, Copy) | Windows: `\GPU Engine(*)\Utilization Percentage`, só do nosso adaptador (o de memória dedicada em uso, para não misturar com o Basic Render) | não |
| Submissões GFX/s e IBs SDMA/s | log do KMD | não |
| Estado da doorbell do GFX (C1) | log do KMD | não |
| **Clock da GPU** | **indisponível nesta corrida** | — |

**Por que sem clock:** só o SMU sabe o clock. Perguntar a ele é mensagem nova ao SMU em regime.
- A mailbox que temos (`C2PMSG_66/67/82`, msg `0x37`, do serviço de diagnóstico) não tem proveniência local contra o `amdgpu`.
- A comunidade registra filas de mensagem que travam o SMU até o reboot.

Fica como etapa própria (SMU.1), depois de trazer o código do `amdgpu` para `inc/linux_ref`.

**Correção minha:** a temperatura estava "sem amostra" desde a P1 (219), porque o sampler de plataforma foi desligado junto com a instrumentação.

## 4. Prova offline

- `build-contract` **EXIT=0**.
- Auditoria de segurança **PASS**:
  - portão novo `audit_c1_gfx_doorbell_like_amdgpu`;
  - os portões MEC.1, P1 e I.PLAT foram atualizados para a regra nova, sem afrouxar o resto;
  - **7 mutações, todas reprovadas**: ativa antes da prova, submit com MMIO sempre, `POLL_CNTL` escrito de novo, sem rollback na falha, sem desarme no TDR, índice errado, temperatura sem chave.
- Auditoria do pacote **PASS**.
- No SYS assinado: strings `[C1_GFX_DOORBELL] ATIVA/PROVA FALHOU/DESARMADA` presentes e o imediato `0x40000458` presente.

**Pacote lab 3.399.1.0:**

| | |
|---|---|
| SYS | `F4156E65A9208D333DDE2566A5911816806CD1EBE68F4FA6F27A148B8B5C4058` |
| INF | `4853C8DD…` |
| CAT | `1F23A8A2…` |
| UMD | `B73F9BDA` (igual) |
| RADV | `B4AE2E14` (igual) |
| Backup | `driver/wdk/package/output_lab_3.399.1.0_253` |
| Lançador | `tools/EXECUTAR_CORRIDA253_C1_DOORBELL_GFX_MENU_600S.cmd` (abre o monitor) |

## 5. Critério da Corrida 253 (escrito antes)

**Antes de rodar (Daniel):** a placa está em Código 43.
1. `tools\BC250_RECOVERY.cmd`, elevado.
2. `tools\inspect-boot-fuse.ps1 -Reset`.
3. Desligar (boot frio).
4. Rodar o lançador. O roteiro é só o menu, como na 252.

**Pré-condição** (todas):
- **C1-a:** `[C1_GFX_DOORBELL] ATIVA (ONLINE)` com `CONTROL=0x40000458` (HIT pode estar 0 ou 1), `RANGE=0x00000458..0x0FFFFFFC` e `MEC_UPPER=0x00000450`.
- **C1-b:** `WPTR_CANAIS ... PollCntlPedido=0 ... DoorbellGfx(C1)=1`.
- **C1-c:** zero `WPTR_READBACK PASS` e zero `SUBMIT RECUSA: WPTR_*` depois do ATIVA. O caminho por MMIO não é usado.
- **C1-d:** GFX funcionando: `GfxRetire` acompanha `SubmitVirtual Node1 GFX] ENTER`, desktop visível.
- **C2:** `P1Snap*WptrPollCntl1 = 0` e `HqdActiveWptrPollCntlReadback = 0` (os probes do bind).
- **MON.1:** o monitor mostra Tctl (não "sem amostra") e o uso 3D/Copy.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Pré-condição completa, **600 s sem trava**, rollback limpo | O WPTR do GFX por MMIO/polling era o mecanismo provável. **Repetição obrigatória**, com o mesmo pacote |
| **B** | Pré-condição completa e trava dura de sempre (GPU ociosa, sem BugCheck) | O canal do WPTR do GFX sai das causas. A C1 fica (paridade, menos MMIO). Próxima da lista: C3 (VRAM do Windows pela DRAM) |
| **C** | `[C1_GFX_DOORBELL] PROVA FALHOU` ou `RECUSADO` (a corrida cai no MMIO) | A doorbell do GFX não chega: investigar offline (faixa, BIF, índice). A corrida não responde à pergunta |
| **D** | TDR do GFX em série, GFX parado depois do ATIVA (submissões sem retire) ou tela preta com o GFX ativo | A publicação por doorbell perde WPTR: defeito de implementação. Voltar a chave para 0 |
| **E** | Fora do mapa | Descrever antes de julgar |

**Régua** (FILLs, doc 667 §6): 252 = 831 FILLs em ~124 s; 251b = 412 em ~53 s; 250 = 1.754 em ~213 s.

**Teto:** 2 corridas para a C1 (253 e a repetição, ou uma correção de C/D).

**Risco declarado:** a doorbell do GFX nunca foi usada nesta placa. Se a prova passar e uma submissão posterior perder o WPTR, o sintoma é TDR do GFX (desfecho D), não trava silenciosa. O TDR já desarma e rearma com prova.

**NÃO VALIDADO EM HARDWARE:** C1 e MON.1.
