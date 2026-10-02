# 509 — Auditoria interna de 28/09/2026 (quatro frentes + conferência)

> Offline, só leitura. Quatro frentes em paralelo:
> 1. memória virtual da GPU;
> 2. acessos da CPU e SDMA;
> 3. concorrência e Lei 4;
> 4. `referencias/` e web.
>
> Legenda: **V** = conferido por mim no código ou no log depois do relato; **R** = relatado pela frente e ainda não conferido. Confiança pela Lei 8.

## 1. Candidato principal para a trava dura: toggle de `HDP_MISC` em tempo de execução — V

**O código (P1):**
- `bc250_sdma_present.c:112-138`, `Bc250SdmaPresentHdp`, faz a cada chamada:
  1. `HDP_FLUSH(0x4A80)=0` + leitura;
  2. `HDP_READ_CACHE_INVALIDATE(0x3FC4)=1` + leitura;
  3. **`HDP_MISC(0x3FCC)=misc|1`** + leitura;
  4. **`HDP_MISC=misc&~1`** + leitura.
- Chamada em 10 pontos: submit, fill, blit, e **em cada verificação de conclusão sem fence visível** (`PollRetirement`, :2302).
- Com a FPS.3b, essas verificações passaram a rodar a cada ~1 ms enquanto há IB em voo.

**O upstream (P1, `drivers/gpu/drm/amd/amdgpu/hdp_v5_0.c`):**
- `HDP_MISC_CNTL.FLUSH_INVALIDATE_CACHE=1` é escrito **uma vez, na inicialização** (`hdp_v5_0_init_registers`);
- a invalidação em tempo de execução é só `HDP_READ_CACHE_INVALIDATE=1` + leitura;
- o nosso código **nunca é o estado estável do upstream**: ele termina com o bit **desligado**.

**Evidência própria contra o toggle:** `bc250_submission_engine.c:560-561` registra que `HDP_MISC` **não** é tocado durante submissão ativa no MEC "para evitar bloqueio da interface RCIU no MEC2 (`CP_CPC_STALLED_STAT1 = 0x00040000`)". Um bloqueio causado por esse registrador já foi visto neste chip.

**Casa com o quadro da trava:**
- o HDP é o caminho entre a CPU e a memória da GPU;
- a taxa de toggle cresce com a paginação e cresceu com a FPS.3b (a 155 travou aos ~77 s);
- nas três travas em que as migalhas sobreviveram (152, 154, 155), o **último acesso a registrador da máquina** foi `SDMA_REG_READ 0xFCC` = leitura de `HDP_MISC`, concluída, no CPU00 (P0).

**Ressalva que manda:** essa leitura encerra toda sequência de HDP e roda o tempo todo, então ser a última é esperado por frequência. Vários processadores têm `0xFCC` como último acesso no dump da 152. O dado **não discrimina sozinho**: é H de alta prioridade, não causa.

**Teste que separa (proposta DX.96-HDP):** alinhar ao upstream e mudar só isso:
- `FLUSH_INVALIDATE_CACHE=1` uma vez no StartDevice, com leitura de volta;
- em tempo de execução, só flush (`0x4A80`) + invalidação (`0x3FC4`) com leitura, sem tocar em `HDP_MISC`.

Critério: travas somem em corridas seguidas **e** o tripé do SDMA continua fechando (fence sobre poison). Se a fence deixar de ficar visível, o toggle estava mascarando um problema de coerência, que também precisa de nome.

## 2. Defeitos confirmados, fora da trava

| # | Achado | Onde | Conf. | Estado | Impacto |
|---|---|---|---|---|---|
| 2.1 | `Bc250DisplayLog` dentro do callback em DIRQL (`Bc250SynchronizedVsyncCallback`, a cada 60 vsyncs) → `Bc250LogRingAppend` → `KeAcquireSpinLock` acima de DISPATCH | `bc250_display_ddi.c:1482`, `:1226` | P1 | V | Viola o contrato do spinlock; em retail ele **baixa** a IRQL dentro da região sincronizada. Não explica a trava (seria 0x133), mas é defeito. |
| 2.2 | Mapeamento de `BAR0 + VramOffset` **sem limite** quando o offset passa de 256 MB; o offset vem do conteúdo de uma PDE | `bc250_active_miniport.c`, `Bc250ActiveReadVramU64` | P1 | V | Uma PDE errada faria a CPU ler um endereço físico arbitrário (inclusive registradores). **Não rodou** nas 5 corridas recentes (0 `[PT_READ]`, P2 forte). Fechar. |
| 2.3 | A falha do FLUSH_TLB vira `STATUS_SUCCESS` antes do VidMm | `bc250_mcdm_callbacks.c:1578-1580` | P1 | V | O VidMm acredita que a TLB foi limpa. A doc oficial do `DxgkDdiBuildPagingBuffer` só prevê SUCCESS, ALLOCATION_BUSY e INSUFFICIENT_DMA_BUFFER. Devolver erro seria fatal; convertê-lo esconde divergência entre hardware e vista de software. |
| 2.4 | O dreno de fences retorna `FALSE` depois de já ter tirado `count` entradas do rastreador, quando uma geração diverge | `bc250_active_miniport.c:11067-11073` | P1 | V | Entradas nunca notificadas → fence que não retira → TDR com a engine ociosa (assinatura já vista). |
| 2.5 | FENCE de 64 bits num único pacote de 5 dwords (bit 31), em vez de dois FENCE | `bc250_sdma_packet_builder.c`, `Bc250SdmaEmitFence64` | P1 (relato do upstream) | V no código | Latente: com valor < 2³² o 5º dword vale 0 (NOP). Com valor ≥ 2³² vira COPY_LINEAR com parâmetros lixo. |

## 3. Relatados pelas frentes, ainda não conferidos (R)

| Achado | Frente | Conf. relatada |
|---|---|---|
| As mesmas páginas de VRAM mapeadas **write-combined** (framebuffer inteiro, 256 MB) e **uncached** (migalhas, anel SDMA, anel GFX): aliasing de tipo de memória, que Intel e AMD declaram não suportado | 2 | P0 aliasing, H impacto |
| `SDMA0_UTCL1_CNTL.FORCE_INVALIDATION` ligado por RMW em todo FLUSH_TLB e nunca desligado | 1 | P1 bit, H |
| `GCVM_L2` inteiro invalidado em cada publicação de WPTR do MEC (upstream: só na init) | 1 | P1 divergência |
| `GCVM_L2_CNTL` escrito como constante `0x00080E01`, zerando `EFFECTIVE_L2_QUEUE_SIZE` e outros campos que o upstream preserva | 1 | P1 divergência |
| Sem semáforo no ENG17 do GFXHUB (o patch gfx1013 v33 do Linux adiciona o semáforo para este chip) | 1 + 4 | P1 |
| O caminho do FLUSH_TLB escreve `0x3FC4` (invalidação de leitura) e não `0x4A80` (flush de escrita) depois de a CPU escrever tabelas de página pela BAR | 1 | P1/P2 |
| Leitura do registro de objeto sem lock no submit em DISPATCH (registro rasgado → raiz errada) | 3 | P1 |
| Abort do journal fora do `ObjectLock` | 3 | P1 |
| E/S de disco de log com o `ObjectLock` exclusivo em DDIs de aplicação (CreateAllocation etc.) | 3 | P1 mecanismo |
| Laços longos com spinlock em DISPATCH (blt até 10.000 polls; SyncNop/Indirect até ~1 s) | 3 | P1 |
| StopDevice: `ExCancelTimer` sem espera; `TimerStart` sem checar `IsActive` | 3 | P1, baixa |
| Troca de raiz do VMID7 com IB de outro contexto em voo | 3 | P1 código, H efeito |

## 4. Referências e web (frente 4)

- **Patch gfx1013 v33 (Linux):** para o 13FE, invalidação por MMIO, tipo 0 e **com semáforo** no GFXHUB. Nós temos MMIO e tipo 0, e não temos o semáforo.
- **Patch "compute gfxoff guard":** no Linux o GFXOFF é tratado como real neste chip. O outro driver Windows (Keshas-dev) registra "GFXOFF ligado no boot (bit 2 de 0x3D)". Nós marcamos o guard como aprovado sem medir (`bc250_active_miniport.c:2869-2886`). **Medir `RLC_PG_CNTL`/`RLC_GPM_STAT` (somente leitura)** antes de concluir.
- **Outro driver Windows:** trata a faixa BAR5 `0x3400–0x8100` como "freeze zone" para **escrita**. Os nossos `0x3FC4`, `0x3FCC` e `0x4A80` caem nessa faixa: o próprio HDP. É um relato externo (P2) que reforça a §1.
- **`bc250_memcfg`:** tREF (A4h) e tRFC (A8h) ficam na CMOS. Se estiverem fora do padrão, podem explicar decaimento de memória. **Ler** a CMOS (ou perguntar ao Daniel se já mexeu) para descartar.
- **Comunidade:** o reset de GPU não se recupera neste chip; a GDDR6 é Micron de baixo bin e sem sensor, o ponto mais quente da placa; há placas que travam a 60–65 °C com refrigeração ruim.

## 5. Ordem proposta
1. **DX.96-HDP** (§1): uma variável, alinhada ao upstream, com critério escrito antes.
2. Na mesma faixa ou logo depois, só leitura:
   - `RLC_PG_CNTL`/`RLC_GPM_STAT` no StartDevice e a cada minuto (GFXOFF);
   - leitura de volta de `UTCL1_CNTL`.
3. Correções que não mudam o comportamento de hardware: 2.1 (tirar o log do DIRQL), 2.2 (limitar à BAR), 2.4 (não perder notificação), 2.5 (dois FENCE).
4. Depois, os itens R da §3, conferindo um a um.
