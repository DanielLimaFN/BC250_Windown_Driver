# 583 — D2: anel e Work do SDMA0 na RAM pela abertura AGP, e critério da Corrida 213

**Data:** 04/10/2026 · **Tipo:** implementação offline e critério, zero corridas · **Decisão:** Daniel ("pode começar a D2"), depois do [doc 582](582_MINERACAO_DAS_TRAVAS_256MB_GPU_OCIOSA_MAQUINA_MORRE_ENTRE_DUAS_OBSERVACOES_E_O_QUE_O_LINUX_FAZ_DIFERENTE.md)
**Candidato:** lab **3.361.1.0** = 3.360.1.0 (Corrida 212) + D2. UMD e RADV idênticas às da 212.

## 0. Correção de um erro meu antes de tudo

No doc 582 escrevi que a AGP "é o mesmo mecanismo que o Linux usa para buffers de kernel em GTT". **Isso é falso no BC-250.**

- **P0, coleta L-B** (`docs/evidence/2026-10-01_15-01-LINUX-REFERENCIA-UMA256/.../dmesg-amdgpu.txt`): `GART: 512M 0x0 - 0x1FFFFFFF` e `PCIE GART of 512M enabled (table at 0xF40FE00000)`. Não há nenhuma linha `AGP:`.
- **P1:** `gmc_v10_0_vram_gtt_location` só liga a AGP com `amdgpu_agp == 1` (`gmc_v10_0.c:659`), e o padrão é `-1` (`amdgpu_drv.c:242`).
- **Conclusão:** o Linux põe o anel e o writeback em GTT e chega neles pela GART da VMID 0. A D2 usa a AGP, que é menor: três registradores mais a abertura de sistema, contra tabela de página, contexto 0 e invalidação. O que se testa é o mesmo: **onde a CPU consulta** o anel e as fences (RAM em vez da BAR). O caminho da GPU até a RAM é diferente do Linux, e isso fica declarado como diferença restante.

## 1. O que muda (uma variável)

| | Até a 212 | D2 (3.361.1.0) |
|---|---|---|
| Anel do SDMA0 (4 KB) | VRAM `0xFF00000`, CPU escreve pela BAR (UC) | RAM contígua **não cacheada**, CPU escreve na RAM |
| Work (fences dos slots, writeback do RPTR, poll do WPTR) | VRAM pela BAR | mesma RAM |
| Polling de 1 ms, HDP, MMIO de registrador | iguais | **iguais** (não mudam nesta etapa) |
| Abertura AGP | desligada (`BOT=0xFFFFFF > TOP=0`) | `[0xF800000000, 0xFFFFFFFFFF]` (32 GB), `MC = 0xF800000000 + PA` |
| Abertura de sistema | só a FB | `min(FB, AGP)` até `max(FB, AGP)` |

**Geometria (P1):**
- O início vem de `amdgpu_gmc_agp_location`, no ramo "depois da FB": `ALIGN(fb_end + 1, 16 GB)` (`amdgpu_gmc.c:358`).
- A tradução vem de `amdgpu_gmc_agp_addr` (`amdgpu_gmc.c:188`), com `AGP_BASE = 0`.
- Os registradores seguem `gfxhub_v2_0_init_system_aperture_regs` (`inc/linux_ref/gfxhub_v2_0.c:157-165`).
- Offsets catalogados em `docs/03`: `0x0A608`/`0x0A60C`/`0x0A610`.
- Com a FB em `0xF400000000`, a AGP começa em `0xF800000000`, fora de qualquer VA de usuário (34 bits).

## 2. Como foi feito

**Core** (`bc250_gcvm_global_mmu.{h,c}`):
- `Bc250GcvmPlanAgpAfterFb` e `Bc250GcvmAgpSystemPaToMc`, funções puras.
- `Config.AgpBytes` (0 = como antes).
- O plano é validado **antes** de qualquer escrita (`FailureStep=7`).
- A evidência publica `AgpEnabled`, `AgpStart` e `AgpEnd`.
- O restore devolve o snapshot do POST, como antes.

**SDMA** (`bc250_sdma_present.c`): `Bc250SdmaPresentRelocateRingToSystemMemory(AgpStart, AgpEnd)`. Segue a Lei 5:
- **PREFLIGHT:** anel provado, `InFlightCount = 0`, ainda na VRAM.
- **Alocação:** `MmAllocateContiguousMemorySpecifyCache(8 KB, teto = tamanho da AGP, MmNonCached)`, o mesmo padrão dos buffers do PSP. Como é não cacheada, a coerência não depende de snoop.
- **SNAPSHOT:** os endereços da VRAM ficam guardados.
- **APPLY, READBACK e PROVA:** troca Ring/Work e os endereços de GPU, e chama o **restart do TDR**, que já foi provado no silício (07/09 21:49): soft reset do SDMA0, reprogramação integral, readback de `RB_BASE`, `RPTR_ADDR` e `WPTR_POLL_ADDR` nos endereços novos, e NOP com fence sobre poison **na RAM**.
- **ROLLBACK:** se a prova falhar, volta para a VRAM e prova de novo.
  - Rollback provado: a corrida segue **sem** a D2 (desfecho C abaixo).
  - Rollback que falha: o StartDevice recusa (Código 43), e a RAM **não** é liberada, porque a engine pode apontar para ela.

**Teardown:** a RAM só volta ao Windows se todas as condições abaixo valerem. Na dúvida, fica um vazamento declarado de 8 KB, nunca uma escrita da GPU em memória devolvida.
- o anel está desligado;
- o writeback do RPTR e o poll do WPTR estão desligados;
- os endereços foram devolvidos à VRAM, com readback;
- a engine está ociosa;
- `IRQL <= APC_LEVEL`.

**Momento:**
- No StartDevice, logo depois da MMU global e **antes** da sonda de flush do boot. Por isso a sonda (WRITE_LINEAR de 16 dwords no Work + invalidação) roda já no anel novo.
- Ela fecha a **terceira perna** da Lei 2: dados escritos pela GPU na RAM, conferidos byte a byte. Ela também mantém a porta do `RING_TLB_FLUSH` da 4c.
- Chave única: `BC250_D2_SDMA_RING_IN_SYSTEM_MEMORY`.

## 3. Provas offline

- **`gcvm_global_mmu_test`:**
  - geometria `0xF800000000`/`0xFFFFFFFFFF`;
  - FB já alinhada;
  - recusas (granulo, FB invertida, além de 2^48, tamanho zero) e o maior caso que cabe (fim = 2^48 − 1);
  - `PaToMc` dentro e fora da abertura;
  - registradores programados (`BOT=0xF800`, `TOP=0xFFFF`, `SYS=[0x3D0000, 0x3FFFFF]`), restore, e recusa antes de qualquer escrita.
  - Um caso meu estava errado (o "além de 2^48" ainda cabia) e a suíte o pegou. Corrigi o caso, não o código.
- **Mutação:** com `AGP_BOT = agpStart >> 18`, a suíte reprova (linha 250).
- **Portão novo** `audit_d2_sdma_ring_in_system_memory`: geometria, RAM não cacheada, prova e rollback pelo restart, teardown seguro, ordem antes da sonda. Duas mutações reprovam: `MmCached`, e rollback sem prova.
- `build-contract` EXIT=0, build de laboratório EXIT=0 ("TODOS OS TESTES APROVADOS"), auditoria de segurança PASS, auditoria do pacote **PASS**.

**Pacote 3.361.1.0** (assinado, auditado, **não instalado**):

| Arquivo | SHA-256 |
|---|---|
| SYS | `B7FA41ACF312ACA412DBAB0D0DBC005C38CE53D64FEF2062956A6FC43DE8209F` (`D2_ANEL_SISTEMA` 12×, `D2 abertura AGP` presente) |
| INF | `4C72F906226D2092830665356F75F228E44763D78F008C7DFE90AB13B4D002C9` |
| CAT | `29174B818C9521B177C0A2425E74F7B7405786C363E79593ED4AEE8A96DA225B` |
| UMD | `8782A5E5…E468`, igual à 212 |
| RADV | `80EBB248…7043`, igual à 212 |

**Scripts:** `tools/prepare-corrida213-d2-anel-sdma-na-ram.ps1` e `tools/EXECUTAR_CORRIDA213_D2_ANEL_SDMA_NA_RAM_600S.cmd`. O preflight confere hashes e manifesto; no ensaio a seco, só parou na leitura da CMOS, que o `.cmd` faz antes.

**O que ficou de fora, de propósito:** o contador passivo de acessos pela BAR que propus no doc 582. Ele exigiria instrumentar vários caminhos de MMIO e mudaria o tempo do experimento. A D2 vai sozinha.

## 4. Critério da Corrida 213 (Lei 10)

**Pré-condições:** recuperação, fusível, boot frio, CMOS em 256 MB, mesmo roteiro de uso da 201–212, `coletar-pos-trava.ps1` **antes** da recuperação se travar.

### A — D2 ativa e sem trava dura
- **Assinatura:**
  - `[...] D2 abertura AGP: LIGADA [0xF800000000,0xFFFFFFFFFF]`;
  - `[D2_ANEL_SISTEMA] ATIVO` com `writeback na RAM confere`;
  - `[VMFLUSH_PROBE] VEREDITO ANEL: PROVADO` com `Dados 0/16 divergentes`;
  - **e** a janela de 600 s até o rollback programado, sem trava dura, sem BugCheck, com rollback limpo.
- **Implicação:** a H-BAR do anel e das fences ganha força, mas **não está provada com n=1**. A 201 também passou 591 s sem trava e a 202 travou com a mesma configuração. Exige repetição (a Corrida 214 com o mesmo pacote) antes de qualquer conclusão.

### B — D2 ativa e trava dura
- **Assinatura:** os três sinais de ativação de A **e** a trava dura (tela preta e reinício), em qualquer momento.
- **Implicação:** consultar o anel e as fences pela BAR não é causa suficiente. Próximo passo: D1 (conclusão por interrupção, sem polling de 1 ms) ou GART como o Linux. A escolha sai da mineração dessa corrida.

### C — a D2 não ligou
- **Assinatura:**
  - `[D2_ANEL_SISTEMA] NAO LIGOU` (rollback provado), `RECUSADO no preflight` ou `NAO TENTADO`;
  - **ou** `D2 abertura AGP: desligada`.
- **Implicação:** a corrida não testa a D2. O motivo da linha diz o que corrigir. A AGP pode não rotear como a P1 descreve neste chip: a linha `SYNC_NOP TIMEOUT` do restart mostra `FenceObs`.

### D — defeito de host
- **Assinatura:**
  - `[D2_ANEL_SISTEMA] ROLLBACK FALHOU` seguido de Código 43;
  - **ou** tela azul ou falha atribuível ao nosso código novo (pilha ou linha do log);
  - **ou** o adaptador não chega ao desktop por causa da D2.
- **Implicação:** é defeito meu. A corrida não informa sobre a trava.

### E — algo fora do mapa
Descrever o observado antes de julgar. Exemplos: a D2 ativa com uma mudança de tempo (FPS, latência de IB, duração) que não estava prevista; a trava em outro lugar; um TDR novo.

**Desfecho parcial não é desfecho.** "ATIVO" sem a sonda `PROVADO` **não** conta como D2 ativa: falta a terceira perna.

## 5. O que o Daniel anota

O mesmo da 212:
- hora do desktop;
- abertura do menu e da Pesquisa;
- memória dedicada e compartilhada no Gerenciador;
- hora de qualquer tela preta ou trava.

## 6. Pendências

- Recuperação e boot frio antes do bind.
- **NÃO VALIDADO EM HARDWARE:** a AGP neste chip, o anel do SDMA0 na RAM, 600 s sem trava com 256 MB, e a causa da trava.
