# 567 — 4c completa em 5 etapas (uma corrida cada) e o critério da Corrida 205

**Data:** 02/10/2026 · **Tipo:** desenho + implementação da etapa 1, offline
**Decisão do Daniel (02/10):** fazer a **4c completa** (tabelas de página e invalidação da TLB pelo SDMA, como o Linux), **separada em 5 corridas**, implementando aos poucos.
**Antecede:** [doc 566](566_4C_DESENHO_PTE_PELA_GPU_COMO_O_LINUX_E_O_TESTE_DE_ORDEM.md) (desenho da 4c), [doc 565](565_CORRIDA204_4B_VALIDO_TRAVOU_LEITURA_DA_CPU_NAO_BASTA.md) (Corrida 204).

---

## 1. O que existe hoje (P0, leitura do fonte)

| Peça | Hoje | Onde |
|---|---|---|
| Escrita de PTE | CPU, pela BAR, **no build** do paging buffer (`CPU_VIRTUAL`) | `Bc250McdmBuildCpuPageTableUpdate` → handler ativo |
| Invalidação da TLB | CPU, por MMIO, **no build**: flush de HDP e uma transação por VMID (fixas 7/8/9 + as do pool), engine 17, até 1000 polls cada, sob spinlock | `Bc250ActiveFlushTlbHandler` → `Bc250ActiveFlushMappedGfxhubTlb` → `Bc250RunGfxhubTlbTransaction` |
| Caminhada da tabela pela CPU | em submit de paginação e no TDR (`[PT_WALK]`) | `bc250_active_miniport.c` |
| Invalidação **por pacote** que já existe | o anel **gráfico** já invalida por PM4, em ordem, no engine 0, a cada troca de VMID (DX.32/DX.37) | `Bc250Pm4EmitGfxVmSwitch` |

A última linha importa: invalidar por pacote, em ordem com a GPU, **já roda** no nosso driver, só que no anel gráfico. O SDMA ainda não faz.

## 2. A referência do Linux (P1)

Cópias locais em `referencias/linux_upstream_2026-09-29/` (pasta fora do git; método e SHA-256 abaixo):

| Arquivo | SHA-256 | O que dá |
|---|---|---|
| `navi10_sdma_pkt_open.h` (baixado em 02/10) | `ff093a7c…0e01d` | `SDMA_OP_SRBM_WRITE = 14`, `BYTE_EN` nos bits 31:28, `ADDR` de 18 bits em dwords; campos do `POLL_REGMEM` |
| `amdgpu_gmc.c` (baixado em 02/10) | `833e5ed7…923bc1` | um engine de invalidação por anel (bitmap `0x1FFF3`), 17 para o flush de GART pela CPU |
| `sdma_v5_0.c` | (doc 563) | `vm_write_pte` (`WRITE_LINEAR`), `emit_wreg`, `emit_reg_wait`, `emit_reg_write_reg_wait` |
| `gmc_v10_0.c` | (doc 563) | `emit_flush_gpu_tlb`; **no GFXHUB o Linux não usa o semáforo** |
| `gfxhub_v2_0.c` (`inc/linux_ref`) | — | `program_invalidation`: faixa `0xFFFFFFFF/0x1F` nos 18 engines |

Sequência de invalidação do Linux no SDMA (15 dwords): `SRBM_WRITE(REQ, pedido)` → `POLL_REGMEM(REQ, ref 0, mask 0)` ("um ciclo para zerar o ACK") → `POLL_REGMEM(ACK, bit, bit)`.

## 3. As 5 etapas

Uma variável por corrida. Se a trava acabar numa etapa, a seguinte **repete** a mesma (n=2) antes de continuar.

| Etapa | Corrida | O que muda | O que a corrida responde |
|---|---|---|---|
| **4c.1** | **205** | **Ensaio no boot, sem mudar o runtime.** O SDMA0 escreve 16 dwords "tipo PTE" e invalida a TLB por pacote, primeiro no **anel** (VMID 8) e depois **dentro de um IB** (VMID 9), no engine 1. A CPU confere fence, RPTR, dados, REQ e ACK | Se os pacotes funcionam no silício, e se `SRBM_WRITE` é aceito **dentro de IB** (o Linux só o usa no anel; em runtime os nossos pacotes moram no paging buffer, que é um IB) |
| **4c.2** | 206 | **`FlushTlb` por pacote no paging buffer.** O MMIO de invalidação sai do caminho de paginação. PTE continua pela CPU | Se a invalidação da TLB fora de ordem, pela CPU, é a causa |
| **4c.3** | 207 | **Gates de submit e `[PT_WALK]` sem ler a tabela pela CPU** (validação pelo journal de software). Pré-requisito da 4c.4 | Se a leitura das tabelas pela BAR durante a carga pesa na trava. Também é a condição para a GPU poder escrever as tabelas sem a CPU ler pela metade |
| **4c.4** | 208 | **`GPU_VIRTUAL`:** `UpdatePageTable` vira `WRITE_LINEAR` no paging buffer. As tabelas do próprio processo de paginação seguem pela CPU, como o WDDM exige. Tunable para voltar a `CPU_VIRTUAL` sem reinstalar | Se a CPU tocando tabela de página é a causa. É o estado do Linux |
| **4c.5** | 209 | **Zero MMIO de TLB em runtime** (instalação de VMID e caminhos restantes por pacote) **+ repetição** da 4c.4 | Confirmação (n=2) e limpeza |

**Pontos que cada etapa vai enfrentar (declarados agora):**
- **4c.2:** se `SRBM_WRITE` não for aceito dentro de IB (resposta da 205), o flush passa para o **anel**, entre IBs: o KMD divide a execução do paging buffer no ponto do `FlushTlb`. É mais código, mas não muda a ideia.
- **4c.2:** o `ACK` fica aceso até o próximo `REQ` no mesmo engine; um `SRBM_WRITE` ignorado faria o `POLL` passar sem invalidar. Por isso a 205 prova o `REQ` pela leitura do registrador.
- **4c.4:** com `GPU_VIRTUAL` o VidMm passa a mapear as tabelas de cada processo no espaço do processo de paginação, caminho que nunca rodou com o nosso driver.

## 4. Etapa 1 implementada (offline)

**Código:**
- `driver/core/bc250_sdma_packet_builder.{c,h}`: `Bc250SdmaEmitSrbmWrite`, `Bc250SdmaEmitRegWait` (sobre o `POLL_REGMEM` existente), `Bc250SdmaEmitVmInvalidate` (15 dwords, recusa VMID 0, engine 17 e endereço acima de 18 bits), `Bc250GcvmInvalidateRequestValue`. Engine do SDMA0 = **1** (17 é da CPU, 0 é do anel gráfico).
- `driver/wdk/bc250_sdma_present.{c,h}`: `Bc250SdmaPresentRunVmFlushProbe(UseIndirect, Engine, Vmid)`. Veneno no rascunho (`Work+0xA00`, 16 dwords) e na fence, corpo no anel ou num IB em `Work+0xC00` (VMID 0), GCR + fence, tripé. **Timeout → quiesce + restart do TDR** (o par provado em 07/09 e 17/09).
- `driver/wdk/bc250_active_miniport.c`: `Bc250ActiveArmSdmaInvalidateEngine` (faixa do engine 1 em `0xFFFFFFFF/0x1F` com snapshot, readback e restauração, como `gfxhub_v2_0_program_invalidation`) e `Bc250ActiveRunSdmaVmFlushProbe` (duas passadas, leitura de `REQ`/`ACK` do engine 1 antes e depois). Chamado no `StartDevice` logo depois da MMU global, **antes** de o VidMm paginar.
- `driver/core/bc250_gcvm_global_mmu.h` + `bc250_mmio_topology_transport.c`: offsets do engine 1. `REQ`/`ACK` só em **leitura** (quem escreve o `REQ` é o SDMA); faixa em escrita. O `SEM` não entra (ler adquire o semáforo, docs/03).

**Testes:**
- `sdma_packet_builder_test.c`: gabarito de 15 dwords montado à mão das macros do Linux (`0xF000000E, 0x28A4, 0x00F80100, 0x30000008, 0xA290, 0, 0, 0, 0x0FFF000A, 0x30000008, 0xA2D8, 0, 0x100, 0x100, 0x0FFF000A`), mais as recusas.
- `build-contract` EXIT=0 (suíte, auditorias de DDI e de segurança).
- **Mutação verificada:** `BYTE_EN 0xF → 0xE` no encoder → `TESTE FALHOU (sdma_packet_builder_test)`; fonte restaurada.
- A auditoria de segurança pegou a minha primeira versão: a cópia para o anel não tinha a migalha `VRAM_RING_WRITE` (regra X2a). Corrigido.

**Pacote (assinado, auditado, NÃO INSTALADO):** lab **3.352.1.0**, DISPLAY_RENDER_LAB, WDDM 2.7, P1 PERSISTENT SUBMISSION, GfxWaveProbe=1.
- SYS `4B1894A7…3533F`, INF `6D79EF36…2A873`, CAT `76107FBE…B271F`.
- UMD `6654865D…F2F1` e RADV `5447A17D…DE82`: **idênticas às da 204** (fonte da UMD `73E7400F…22FF`, sem rebuild).
- Strings conferidas no SYS assinado: `[VMFLUSH_PROBE] VEREDITO`, `ENG1 faixa`, `SRBM_WRITE_IGNORADO`, `recuperacao do anel`.
- Lab package audit: PASS.

**Uma variável contra a 204:** o ensaio no boot. O runtime (X2b, 4b, CG.1, PTE e flush pela CPU) é o mesmo.

## 5. Critério da Corrida 205 (escrito antes, Lei 10)

**Roteiro:** recuperação da 204 (`tools\BC250_RECOVERY.cmd`, reset do fusível), boot frio, CMOS em 256 MB, `tools\EXECUTAR_CORRIDA205_4C1_PACOTES_VM_NO_BOOT_600S.cmd`, mesmo uso das 201–204. Se travar: botão de reset e `tools\coletar-pos-trava.ps1` primeiro.

**Linhas que decidem:** as duas `[VMFLUSH_PROBE] VEREDITO ANEL:` e `VEREDITO IB:`, no começo do log do KMD (antes do desktop).

| Desfecho | Assinatura (o que o log mostra) | Implicação (para onde vai) |
|---|---|---|
| **A** | `VEREDITO ANEL: PROVADO` **e** `VEREDITO IB: PROVADO` | Os pacotes funcionam no anel e dentro de IB. A 4c.2 põe o flush dentro do paging buffer, como desenhado |
| **B** | `ANEL: PROVADO` e `IB: SRBM_WRITE_IGNORADO` (dados 16/16 no IB, `REQ` não mudou) | O SDMA ignora escrita de registrador dentro de IB. A 4c.2 põe o flush no **anel**, dividindo a execução do paging buffer |
| **C** | `ANEL: PROVADO` e `IB: TIMEOUT` ou `FALHA_DADOS_OU_RPTR` | O IB em VMID 0 ou o pacote dentro do IB para o SDMA. Olhar `ERR_LOG`/`STATUS1` e `Recovered`; a 4c.2 também vai para o anel |
| **D** | `ANEL:` diferente de `PROVADO` (`TIMEOUT`, `SEM_ACK`, `SRBM_WRITE_IGNORADO`, `FALHA_DADOS_OU_RPTR`) | A invalidação por pacote do SDMA não funciona nem no anel. Antes da 4c.2: rever engine, faixa e protocolo contra o Linux, offline |
| **E** | Algo fora do mapa: `NAO EXECUTADO`, `ENG1 faixa: READBACK divergiu`, `COMPATIVEL_NAO_CONCLUSIVO`, BugCheck ou trava no boot | Descrever antes de julgar |

**Regras:**
- Desfecho parcial não conta. `PROVADO` exige as cinco provas juntas: fence pela GPU sobre veneno, `RPTR==WPTR`, 16/16 dwords, `REQ` com o valor que só o pacote escreve (e diferente antes) e o bit da VMID aceso no `ACK` (apagado antes).
- **A trava dura não faz parte deste critério.** O runtime é o da 204, então travar ou não travar depois do boot **não** diz nada sobre a 4c. Relatar como dado e seguir.
- **Risco declarado:** um `POLL_REGMEM` que nunca satisfaz para o SDMA0 no boot. O ensaio tem teto de ~1 s e faz quiesce + restart. Se o restart falhar, o SDMA0 fica desligado e a corrida não terá desktop; o resultado do ensaio vale do mesmo jeito.

**NÃO VALIDADO EM HARDWARE.**
