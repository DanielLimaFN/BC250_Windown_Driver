# 587 — D2 por GART da VMID 0, como o Linux, e critério da Corrida 215

**Data:** 04/10/2026 · **Tipo:** implementação offline e critério, zero corridas · **Decisão:** Daniel ("D2 por GART, como o Linux faz neste mesmo chip"), depois do [doc 586](586_CORRIDA214_LOG1_FUNCIONOU_AGP_TRAVA_O_SDMA0_NA_PRIMEIRA_LEITURA.md)
**Candidato:** lab **3.363.1.0** = 3.362.1.0 com a D2 refeita por GART e a AGP desligada. A LOG.1 continua. UMD e RADV idênticas.

## 1. Desenho (P1 onde citado)

**Linux no BC-250 (P0, coleta L-B):** `GART: 512M 0x0 - 0x1FFFFFFF`, tabela em VRAM, sem AGP. Anéis e writeback em GTT, alcançados pela VMID 0.

**Programação do contexto 0** (`gfxhub_v2_0_init_gart_aperture_regs` e `enable_system_domain`, `inc/linux_ref/gfxhub_v2_0.c:120-148,254-264`):

| Registrador | Byte (Lei 7, `gc_10_1_0_offset.h`, conferido) | Valor |
|---|---|---|
| `CONTEXT0_PAGE_TABLE_BASE_LO/HI` | `0xA3AC`/`0xA3B0` | PA da tabela \| VALID (`pd_addr`) |
| `CONTEXT0_PAGE_TABLE_START_LO/HI` | `0xA42C`/`0xA430` | 0 |
| `CONTEXT0_PAGE_TABLE_END_LO/HI` | `0xA4AC`/`0xA4B0` | `0x1FF` (2 MB − 1, >> 12) |
| `CONTEXT0_CNTL` | `0xA200` | ENABLE=1, DEPTH=0, RETRY=0, resto do POST (Lei 6), **por último** |

- **Diferença declarada:** a faixa é de 2 MB (tabela de 4 KB) em vez de 512 MB. A mecânica é a mesma.
- **Invalidação:** VMID 0 pela engine **17** (`gmc_v10_0_flush_gpu_tlb`: "Use register 17 for GART"), requisição `0x00F80001`.
  - É o plano novo `Bc250PagingBuildGartTlbFlushPlan`, com a chave `GartVmid0`.
  - O validador da transação só deixa a VMID 0 passar com essa chave. O construtor genérico continua recusando a VMID 0, porque fora da GART ela é do MEC.
- **Tabela:** 512 PTEs em VRAM privada `0xFF02000`, colada no bloco fixo do anel legado (`0xFF00000`+8 KB) e fora de tudo que os alocadores entregaram na 212.
  - Entrada 0 e as não usadas apontam para a dummy da VRAM (`VALID|R|W`).
  - Entradas 1 e 2 apontam para o anel e o Work na RAM (`VALID|SYSTEM|SNOOPED|EXE|R|W` = `0x77`, a mesma PTE do segmento 2 da M1b).
  - Anel em MC `0x1000`, Work em `0x2000`. MC 0 nunca vira endereço de anel.
- **RAM:** 8 KB contíguos e **não cacheados** (`Bc250SdmaPresentAllocateSystemRingWork`).
- **AGP:** desligada (`config.AgpBytes = 0`). O plano puro e os testes ficam, sem uso.

## 2. Ordem no StartDevice (Lei 5)

`Bc250ActiveRunD2Gart`, depois da MMU global e antes da sonda de flush do boot:

1. **PREFLIGHT:**
   - MMU global pronta, com a AGP desligada;
   - dummy alocada;
   - tripé de boot do SDMA0 provado;
   - MMIO mapeado;
   - a BAR da VRAM cobre `0xFF02000`.
2. Aloca a RAM e monta a tabela.
3. Copia a tabela para a VRAM e faz **readback** das 512 PTEs.
4. Drena o **HDP**.
5. **SNAPSHOT** dos 7 registradores do contexto 0 e **APPLY** (o CNTL por último).
6. **READBACK** dos 7 registradores e invalidação da VMID 0. Se falhar, o próprio contrato desfaz, desligando o CNTL primeiro.
7. **Relocação do anel** pelo restart do TDR (soft reset, reprogramação, readback e NOP com fence sobre poison na RAM).
8. **Se a prova falhar:** o gancho desliga a GART **antes** de o anel voltar para a VRAM, e o anel é provado de novo na VRAM.
   - Em 29/08, com o contexto 0 ligado e a MMU no modo do POST, o SDMA0 falhou até no anel da VRAM. Por isso a GART nunca fica ligada durante o rollback.
9. A sonda de flush roda no anel novo: 16 dwords escritos pela GPU na RAM, conferidos pela CPU. É a terceira perna.
10. **Teardown:** a engine solta a RAM (anel, writeback e poll desligados, endereços de volta à VRAM, com readback), o gancho desliga a GART, e só então a RAM é liberada. Na dúvida, fica um vazamento declarado.

## 3. Provas offline

- **`gcvm_global_mmu_test`:**
  - tabela (entrada 0 na dummy, 1 e 2 na RAM com `0x77`, resto na dummy, 6 recusas);
  - contexto 0 (`BASE=0x6FF02001`/`0x4`, `END=0x1FF`, `CNTL 0x007FFE80 → 0x007FFE01`);
  - restore;
  - rollback por readback divergente e por flush que falha;
  - recusa de tabela desalinhada.
- **`gfxhub_tlb_transaction_test`:** o plano da GART (`0xA2D0`, `0x00F80001`) passa; o mesmo plano sem a chave é recusado sem escrever; o construtor genérico recusa a VMID 0.
- **Portão novo** `audit_d2_sdma_ring_in_system_memory` (versão GART), com seis invariantes. Duas mutações reprovam:
  - o gancho da GART depois do rollback na VRAM;
  - o validador aceitando a VMID 0 sem a chave.
- `build-contract` EXIT=0, build de laboratório "TODOS OS TESTES APROVADOS", auditoria de segurança PASS, auditoria do pacote PASS.

| Arquivo | SHA-256 |
|---|---|
| SYS | `50C50E851A607C31CCF985978139E010F64F74844FC130F1E46B785A7BA2CB25` (`D2_GART` presente) |
| INF | `79959F8C6456002F1FCDF17B23F73A526E5F72020DDE04652F4239414FB9B0B5` |
| CAT | `F89E8E82CD6CC8EC9ED148EBB276C6FBB7AD2159F65D5C42AA3169240AB12019` |
| UMD e RADV | iguais |

**Scripts:** `tools/prepare-corrida215-d2-por-gart.ps1` e `tools/EXECUTAR_CORRIDA215_D2_POR_GART_600S.cmd`.

## 4. Riscos declarados

- **Se a busca pela GART travar o motor como a AGP travou, o rollback não salva.** A 214 mostrou que o soft reset do SDMA0 não desfaz isso. O desfecho é Código 43 em segundos, sem tela azul, e boot frio depois.
- **Se o contexto 0 ligado quebrar o acesso à FB** (como em 29/08 com a MMU do POST), o anel da VRAM também falha. Hoje a MMU global usa `SYSTEM_ACCESS_MODE=3`, em que a FB passa por fora da tabela, como no Linux. **H.**
- A carga direta do PFP lê e restaura o contexto 0. Como restaura o próprio snapshot, regrava os mesmos valores da GART, ou seja, é inofensiva. **P1, leitura de código.**

## 5. Critério da Corrida 215 (Lei 10)

**Pré-condições:** boot frio (o SDMA0 da 214 ficou preso), CMOS em 256 MB, mesmo roteiro de uso da 201–212, `coletar-pos-trava.ps1` antes da recuperação se travar.

- **A — D2 ativa e 600 s sem trava dura.**
  - *Assinatura:*
    - `[D2_GART] TABELA ... divergentes=0`;
    - `[D2_GART] CONTEXTO0: status=0 ... readback=1 flushVmid0=1`;
    - `[D2_ANEL_SISTEMA] ATIVO` com `writeback na RAM confere`;
    - `[VMFLUSH_PROBE] VEREDITO ANEL: PROVADO` com `Dados 0/16`;
    - e a janela inteira sem trava, sem tela azul, com rollback limpo.
  - *Implicação:* a H-BAR ganha força, mas n=1 não basta (a 201 também passou). Exige a Corrida 216 com o mesmo pacote.
- **B — D2 ativa e trava dura.**
  - *Assinatura:* os quatro sinais de ativação de A e a trava dura.
  - *Implicação:* consultar o anel e as fences pela BAR não é causa suficiente. Próximo passo: D1 (interrupção).
- **C — a D2 não ligou, com rollback provado.**
  - *Assinatura:* `[D2_ANEL_SISTEMA] NAO LIGOU`, ou `[D2_GART] NAO TENTADO`, ou `CONTEXTO0: status≠0`. O desktop sobe com o anel na VRAM.
  - *Implicação:* a linha nomeia o ponto. O `SYNC_NOP TIMEOUT` (RPTR/WPTR) separa leitura de escrita pela GART.
- **D — rollback falhou.**
  - *Assinatura:* `[D2_ANEL_SISTEMA] ROLLBACK FALHOU`, Código 43.
  - *Implicação:* a GART, como está, também trava o motor. Comparar o RPTR com o da 214. A D2 vai para a gaveta e o próximo passo é a D1.
- **E — fora do mapa.** Descrever antes de julgar.

**Desfecho parcial não é desfecho:** "ATIVO" sem a sonda `PROVADO` não é D2 ativa.

## 6. Pendências

**NÃO VALIDADO EM HARDWARE:** a GART da VMID 0 no nosso driver, o anel do SDMA0 na RAM, a H-BAR, e 600 s sem trava com 256 MB.
