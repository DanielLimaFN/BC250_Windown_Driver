# 242 — Auditoria independente: Marco 1 (RADV/ACO/WDDM — compute Vulkan) na corrida 11/09 09:18

> **Escopo:** auditoria cética do dossiê do Marco 1, contra o repositório, a pasta de evidência
> `docs/evidence/2026-09-11_09-18-11-DESKTOP-HQAG6T4-lab-binding/` e o log vivo
> `C:\BC250_DISPLAY_LOG.txt` (54.070.786 bytes no arquivo pré-rollback).
> **Método:** cada afirmação do dossiê foi reconferida em P0 (log/JSON/arquivos) ou P1 (código/fonte externa).
> **Resultado:** veredito em §7.

---

## 0. Resumo do veredito (uma linha)

**Marco 1 CONFIRMADO NO SILÍCIO — com cinco ressalvas que não derrubam o marco, mas derrubam três
afirmações específicas do dossiê (o arithmetic do anel, a completude do IB arquivado e um check
fail-open) e exigem uma repetição antes de se falar em "consolidado".**

---

## 1. O que sustenta o marco (P0 — medido, não inferido)

A cadeia completa está no log vivo, linhas **607.484 → 607.516**:

```text
607484 [McdmSubmitCommandVirtual] Node=1, FenceId=1, DmaSize=192
607485 [SubmitVirtual Node1] GATE ABERTO ... publicando IB de 192 bytes em VA=0x200044000 no anel persistente MEC
607494 [MEC_VMID_RETARGET] Status=0x00000000 ... RPTR=0 WPTR=0
607505 [MEC_PRE_SUBMIT]  ... RPTR=0 WPTR=0
607507 [SubmissionEngineSubmit] SUCESSO: NewWptr=12 Fence=18 Pad=0 Accepted=18 Kicks=18
607508 [MEC_POST_DOORBELL] ... RPTR=12 WPTR=12 Error=00000000 HqStatus=40008040 Vmid=00000900
607513 [SubmitVirtual Node1] SUBMIT SUCESSO: AssignedFence=18 RingWptr=12 WptrMmio=12
607514 [SubmitVirtual Node1] DESCRIPTOR: Raw=C0023F00,00044000,00000002,09800030
607515 [SubmitVirtual Node1] FENCE COMMIT: Token=1 RingFence=18 committed=1 accepted=1 status=0x00000000
```

Leitura dos fatos (opcodes confirmados em P1 por `mesa-radv-wddm/src/amd/.../amd_cp_packets_gfx11.h`:
`PKT3_INDIRECT_BUFFER=0x3F`, `PKT3_RELEASE_MEM=0x49`, `PKT3_DISPATCH_DIRECT=0x15`, `PKT3_SET_SH_REG=0x76`,
`PKT3_SET_UCONFIG_REG=0x79`, `PKT3_ACQUIRE_MEM=0x58`):

1. **A submissão veio do ICD RADV e chegou ao nosso KMD**: `Node=1, DmaSize=192` — o `D3DKMTSubmitCommand`
   do winsys `wddm2` (`radv_wddm2_cs.c:817`, `.CommandBuffer = first_ib.va`), com `HwQueueSupported=false`
   no caminho BC-250. Não há Vulkan Loader envolvido (carga explícita, `icd_identity_probe.matched=true`).
2. **O KMD não reimplementou nada**: o descritor publicado é `C0023F00` = `PKT3_INDIRECT_BUFFER`,
   base `0x00000002_00044000` = **VA 0x200044000 (o próprio DMA buffer)**, 4º dword `0x09800030`
   → `IB_SIZE = 0x30 = 48 dwords` (= 192 bytes ✔), bit 23 (`INDIRECT_BUFFER_VALID`) ligado, `VMID = 9`.
   Ou seja: o MEC **salta para o command buffer do RADV dentro do espaço GPUVA do processo**.
3. **O anel avançou 0 → 12 dwords** = `INDIRECT_BUFFER` (4 dw) + `RELEASE_MEM` (8 dw) — exatamente o
   payload documentado (`bc250_submission_engine.c:1400`, `:1416`; `docs/206`, `docs/207`).
4. **Drenagem física**: `MEC_POST_DOORBELL` com `RPTR=12 == WPTR=12`, com `Error=0` e
   `HqStatus=0x40008040` (ativo). No `StopDevice`: `WPTR=12 RPTR=12`, `Active=00000001`.
5. **Fence física escrita pela GPU**: o slot em `+0x2200` é zerado pela CPU **apenas no init**
   (`bc250_submission_engine.c:447-453`); o valor 18 vem do `RELEASE_MEM` emitido no anel.
   `P1LastFenceObserved=18`, `P1LastRetiredFence=18`, `FenceNow=18`, `RetiredTotal=18`.
6. **Sem falta de tradução**: `NODE1_STOP_GCVM_FAULT STATUS=0x00000000 ADDR=0x0` (contra `STATUS=0x00900B32
   ADDR=0x0F400AA0` no bind anterior, das 09:07 — que **falhou**, `Sub=18 Ret=17 FenceNow=0`).
7. **Prova de dados**: o harness escreve `0x43DAFFFE` e lê `0xBC250C01` no mesmo ponteiro mapeado.
   `BC250_RESULT` **não é escrito em lugar nenhum por CPU** (verificado em
   `driver/tests/bc250_radv_compute_gate.c`: só aparece na comparação) e **não existe no UMD**
   (grep `BC250` em toda a árvore do Mesa: só caminhos legítimos de alocação/WDDM).

**Tripé (Lei 2) — as três pernas, na mesma submissão:**

| Perna | Evidência | Nível |
|---|---|---|
| `RPTR == WPTR` | `MEC_POST_DOORBELL RPTR=12 WPTR=12` + `MEC_STOP_QUEUE WPTR=12 RPTR=12` | **P0** |
| Fence física sobre poison | slot zerado no init; 18 lido após o `RELEASE_MEM`; `Retired=18` | **P0** |
| Dados byte a byte | `0x43DAFFFE` → `0xBC250C01` no buffer mapeado | **P0** |

---

## 2. Resposta às cinco perguntas do auditor

**Q1 — Há desvio para emulação por CPU (WARP/llvmpipe/memcpy)?**
**Não, e a exclusão é estrutural, não retórica.** (a) Carga explícita de `vulkan_radeon.dll`
(ICD interface 7), sem loader global; (b) `vendor=0x1002 device=0x13FE` e `device_instance_id`
= `PCI\VEN_1002&DEV_13FE...`; (c) a submissão aparece **no nosso KMD** (`McdmSubmitCommandVirtual Node=1`)
com o tamanho exato do command buffer (192 bytes); (d) o anel MEC e o RPTR são **registradores MMIO**
da HQD lidos do silício; (e) nenhum `memcpy`/shadow escreve o valor esperado — o winsys usa
`D3DDDI_MAPGPUVIRTUALADDRESS` real (`radv_wddm2_bo.c:394/617/832`). WARP seria um `dwmapi`/D3D device,
não um `Node=1` do nosso miniport.
**Ressalva:** o valor poderia, em tese, ter sido escrito por CPU *dentro* do UMD — isso está excluído
por grep (nenhuma ocorrência de `BC250C01` fora do harness).

**Q2 — O byte proof é prova matemática?**
Não é "prova matemática" (nenhuma medição isolada é), mas é **prova física suficiente**: o valor esperado
é um padrão que só o shader produz, o endereço é um GPUVA traduzido pela nossa MMU sob VMID 9, e a
escrita só pode ocorrer depois de o MECbuscar o IB. Some-se a drenagem do anel + fence + fault zero e
a hipótese alternativa ("alguém escreveu por CPU") fica sem candidato.
**Fraqueza real:** o harness **não relê o poison logo antes do dispatch** nem prova que o valor é
diferente do que estava; confia na escrita do poison. Recomendo ler e imprimir `antes=` também.

**Q3 — Os registradores comprovam drenagem física pelo MEC/MQD?**
**Sim, com uma correção:** `P1SubmitAccepted/Retired/FenceObserved = 18` é coerente; `WPTR=RPTR=12` e
`HqStatus/Active` coerentes. Mas **a afirmação "o anel avançou de 204 e deu wrap publicando 12" está
errada**: no log, imediatamente antes da submissão, `RPTR=0 WPTR=0` (linhas 607494/607505). O "204" do
`probe-before` é estado de submissão anterior/bind anterior, não uma volta de anel. O delta real é
**0 → 12 dwords**, que é o payload correto. A conclusão ("o anel moveu") sobrevive; a aritmética, não.

**Q4 — A tela preta invalida o compute?**
**Não.** O harness é processo isolado, sem DWM, sem D3D11; a recusa da UMD D3D11 (`E_NOTIMPL`, faixa C1)
explica a tela preta e é independente. O watchdog apenas fez rollback — e, aliás, **o rollback é
evidência de que o teste terminou antes do rollback** (o harness rodou 09:18:18→09:18:24; rollback 09:21:19).

**Q5 — O Marco 1 pode ser declarado concluído?**
**Sim, no escopo estrito "compute Vulkan (RADV+ACO+WDDM) executado no silício com tripé completo" —
com as cinco ressalvas abaixo.** Fora desse escopo, nada mudou: **não** há rasterização, **não** há
pipeline gráfico, **não** há apresentação, **não** há jogo.

---

## 3. As cinco ressalvas (o que o dossiê não diz)

1. **O dispatch não está na janela arquivada.** `NODE1_IB_CPU_ARCHIVE RequestedBytes=192 CapturedDwords=32`
   → o KMD arquiva **32 de 48 dwords** (teto fixo). Os 32 arquivados contêm **apenas**
   `SET_UCONFIG_REG (0x79)` e `SET_SH_REG (0x76)` (offsets 0x7B, 0x380, 0x20D, 0x216, 0x219, 0x224,
   0x27D, 0x204) — **nenhum `PKT3_DISPATCH_DIRECT (0x15)` e nenhum `RELEASE_MEM (0x49)` visível**.
   Os 16 dwords que faltam (64 bytes) são exatamente onde o dispatch estaria. **A execução do shader
   está provada pelo efeito (byte proof), não pelo conteúdo do IB** — e é assim que deve ser declarado.
   *Correção barata:* arquivar o IB inteiro (ou a cauda) antes da próxima corrida.
2. **`gpuva_gate_operational` é um check fail-open.** `GpuvaGateBlockMask` saiu de **0 → 2048**
   (`BC250_GPUVA_GATE_BLOCK_NO_LEAF_COMMIT`, `bc250_active_miniport.h:47`) e `GpuvaGateEligible` caiu
   **1 → 0**, mas `GpuvaGateOperational` ficou em 1 (sticky) — e é esse campo que o check lê. Nesta
   corrida não há dano (o gate estava 0/elegível **antes** da submissão), mas o check aprova um gate
   bloqueado. Exigir `BlockMask == 0 && Eligible == 1` no gate de saída.
3. **Reprodutibilidade = 1 de 2 nesta sessão.** O bind imediatamente anterior (09:07) **falhou com
   falta de tradução real**: `[NODE1_STOP_GCVM_FAULT] STATUS=0x00900B32 ADDR=0x0F400AA0`,
   `Sub=18 Ret=17 FenceNow=0 PollFailures=9663`. O sucesso é real, mas o mecanismo é frágil: declarar
   "consolidado" exige repetição (critério: 3/3 com o mesmo build).
4. **O nome do adaptador precisa de explicação.** `Microsoft Basic Display Adapter (RADV GFX1013)` com
   `vendor=0x1002 device=0x13FE`. Nosso INF declara `%DeviceName%=BC250Mcdm_Device` (linha 32) com
   `ProviderName="AMD BC-250 Driver Project"` — portanto o nome provavelmente vem da descrição do
   devnode no SO, não de adapter errado. Ainda assim: **provar** (imprimir o `DeviceDesc` do devnode),
   porque confundir adaptador seria catastrófico e o ID do dispositivo é a única âncora hoje.
5. **O laço de aposentadoria continua girando solto.** `MEC_STOP_STATE Polls=18670 DpcRuns=18664` para
   **uma** submissão (e `Polls=3 PollFailures=9663` no bind que falhou). É a mesma classe que produziu
   `0x133` no caminho SDMA. Não invalida o marco, mas é dívida declarada antes de qualquer carga real.
   *Também:* `RPTR=12` aparece já no `POST_DOORBELL`, imediatamente após o doorbell — amostrar uma
   segunda vez com atraso para mostrar progressão em vez de um único casamento instantâneo.

---

## 4. O que **não** é prova (Lei 5) e por isso não entra no veredito

Compilar o Mesa, assinar, carregar `vulkan_radeon.dll`, enumerar o adaptador, criar device/pipeline,
`elapsed_ms=5758`, `process_exit_code=0`, os 15 checks do JSON e o manifesto ICD **não provam nada
isoladamente**. O que prova são os três itens da tabela do §1 (anel, fence, bytes) lidos do silício.
Tudo o mais é contexto.

---

## 5. Recomendações antes de declarar "consolidado" (ordem de custo)

1. **Repetir com o mesmo build** — critério escrito antes: 3/3 corridas com tripé completo;
   critério de abandono: 2 falhas consecutivas com `GCVM_FAULT != 0` → voltar à análise de GPUVA.
2. **Arquivar o IB inteiro** (48 dwords, ou cauda de 32) e decodificar o `PKT3_DISPATCH_DIRECT` com o
   opcode table do próprio Mesa — isso transforma "o efeito apareceu" em "o dispatch estava lá".
3. **Endurecer o gate**: `gpuva_gate_operational` deve exigir `BlockMask==0 && Eligible==1`;
   adicionar leitura do poison **antes** do dispatch no harness (`antes=`, `depois=`).
4. **Explicar o nome do adaptador** no relatório (uma linha com o `DeviceDesc` do devnode basta).
5. **Atacar o giro do poll do Nó 1** (18670 polls/submissão) antes de qualquer carga real — o caminho
   SDMA já pagou `0x133` por isso.

---

## 6. O que falsificaria este veredito

- `0xBC250C01` aparecendo sem `P1LastFenceObserved == 18` (efeito sem fence) → escrita por CPU.
- Repetição em que `RPTR != WPTR` com o byte proof passando → a prova de dados não vem do anel.
- `GCVM_FAULT != 0` no mesmo build → a tradução sob VMID 9 não é confiável; o marco volta para H.
- Descoberta de qualquer escrita de `BC250_RESULT` fora do shader (UMD/KMD/harness) → Lei 4, marco anulado.

---

## 7. Veredito

> **MARCO 1 — compute Vulkan (Mesa RADV + ACO + WDDM 2.0+) executado no silício BC-250:
> CONFIRMADO (P0) em 11/09/2026 09:18, com tripé completo (anel drenado + fence física + dados
> byte a byte) e laço UMD→KMD→MEC→CUs fechado numa única submissão.**
>
> **Ressalvas que ficam registradas:** (1) o `PKT3_DISPATCH_DIRECT` não está na janela arquivada
> (32/48 dwords) — a execução está provada pelo efeito, não pelo conteúdo do IB; (2) a aritmética
> "204 → 12 com wrap" do dossiê está errada (o delta real é 0 → 12); (3) `gpuva_gate_operational`
> é fail-open frente a `BlockMask=2048`; (4) reprodutibilidade 1/2 nesta sessão (o bind anterior
> faleceu com `GCVM_FAULT=0x00900B32`); (5) o nome do adaptador exige explicação.
>
> **O que este marco NÃO é:** não é desktop, não é rasterização, não é apresentação, não é jogo.
> A tela preta permanece o estado esperado e correto, e a parede do D3D11/DWM continua de pé.
