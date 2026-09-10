# P6 — Gate de tradução por caminhada de page table na CPU (candidato 3.82)

> **Data:** 2026-09-05 · **Classe:** offline
> **Origem:** corridas de 04/09 22:27 e 23:23, e dump `0x119` Arg1=0xD

---

## 1. O que a corrida de 23:23 mediu

| | 22:27 | 23:23 |
|---|---|---|
| `IbSizeDW` no `INDIRECT` | 16 | **15** |
| `RPTR` | 96 (DW 24, fim do `INDIRECT`) | **132 == WPTR** |
| Fence | poison | **poison** |
| `UTCL1_DELTA` | (sem baseline) | **não-zero** |
| GFXHUB | STATUS=0 | STATUS=0 |

**Ganho P0:** a SDMA0 parou de travar. Com `IB_SIZE=16` a engine entrava no IB e não
voltava; com `IB_SIZE=15` o anel drena por completo. O NOP de cauda **sendo buscado**
era o que travava.

**Ganho P0:** o `IB_DUMP` provou o encoder byte a byte —
`4194304 + 4194304 + 2686976 = 11.075.584`, destinos encadeando de `0x260000`.
"Pacote malformado" está descartado.

**Não resolvido:** a fence do anel não foi gravada. Como a fence é executada sob o VMID
do anel (físico, sem tradução), sua ausência indica que a engine **drenou sem executar**.

> Corolário importante sobre 22:16: o `IB_TRIPOD PASS` daquela corrida provou apenas que
> a engine passou do `INDIRECT` e executou a fence **do anel**. Ele nunca provou que os
> `CONST_FILL` do IB rodaram. Não usar 22:16 como prova de tradução.

## 2. O reboot — P0, provado por dump

```
bc250mcdm!Bc250McdmSubmitCommandVirtual
  -> dxgkrnl!DpSynchronizeExecution -> nt!KeSynchronizeExecution
    -> dxgkrnl!DxgNotifyInterruptCB
      -> dxgmms2!VidSchDdiNotifyInterrupt
        -> dxgmms2!VidSchDdiNotifyInterruptWorker
          -> watchdog!WdCriticalError
BugCheck 0x119  Arg1=0xD  Arg2=0  Arg3=0  Arg4=1 (o FenceId)
Submissão originada em dxgmms2!VidSchiSubmitPagingCommand
```

`DXGK_INTERRUPT_DMA_FAULTED` **não** evita o BugCheck nesta fila. Trocou `0x119 Arg1=2`
por `0x119 Arg1=0xD`. **Revertido** neste candidato; o motivo está registrado no ponto de
falha para não ser reintroduzido.

## 3. Bug de endereço encontrado por leitura de fonte

`Bc250McdmSetRootPageTable` entregava à GCVM:

```c
ptPhysical = vramBaseOffset + Address;          /* ERRADO */
```

`Address` é o offset **dentro do segmento 1**, e o segmento 1 não começa em zero — ele
pula a reserva herdada do scanout do GOP. Medido na corrida:

```
[McdmSetRootPageTable] SegId=1, Offset=0x0, NumEntries=512
[Vmid8Install] readback OK: PT_BASE=450000000
[McdmQAI] QUERYSEGMENT4: VRAM=0x1F570000
```

`0x20000000 - 0x1F570000 = 0xA90000`, que é exatamente `2560*1080*4` arredondado para
64 KiB — a reserva do scanout. O ponteiro de root apontava **`0xA90000` bytes antes da
tabela real**, dentro do framebuffer reservado. O codificador de PTE
(`Bc250McdmEncodeVidMmPteWithLevel`) já somava `LocalSegmentBaseOffset`; só a instalação
da root estava fora de fase.

**Corrigido:** `ptPhysical = vramBaseOffset + LocalSegmentBaseOffset + Address`, com
todos os termos logados em `ROOT_MC`. O readback do VMID8 nunca pegaria isso — ele prova
que o registrador contém o que escrevemos, não que o endereço está certo.

## 4. A pergunta que a próxima corrida decide

Há duas interpretações concorrentes para o campo de endereço de uma entrada:

| | Significado | Quem usa |
|---|---|---|
| **MC** | já inclui a base de VRAM em espaço MC | upstream (`amdgpu` soma `vram_base_offset` ao montar PTEs de VRAM); a instalação da root |
| **RAW** | offset cru dentro da VRAM | `Bc250McdmEncodeVidMmPteWithLevel` hoje: `LocalSegmentBaseOffset + pageOffsetInSegment`, **sem base MC** |

Há também um desacordo de base no próprio repositório: `BC250_SDMA_VRAM_MC_BASE` é
`0xF400000000` (e os blts provados por byte proof rodaram em `0xF40FF00000`), enquanto
`VramBaseOffset` é `0x450000000`.

`Bc250ActiveWalkGpuVa` caminha root → PDE → PTE **pela CPU**, lendo as mesmas estruturas
que a GPU leria, e tenta as duas interpretações em cada nível, registrando qual fechou.
Divergência entre níveis é reportada como `DIVERGENCIA DE ESPACO DE ENDERECO`.

**Gate fail-closed:** se a caminhada não resolve a GPUVA do IB, a submissão é recusada e
o silício **não é tocado**. Uma engine travada custa uma corrida e um reboot; uma recusa
custa só a linha de log.

## 5. Como ler a corrida

| Observado | Significado |
|---|---|
| `PT_WALK IB RESOLVIDO ... (root=MC, folha=MC)` | tradução coerente; se a IB ainda não retirar, o problema é outro |
| `DIVERGENCIA DE ESPACO DE ENDERECO: root=MC folha=RAW` | confirmado: PTEs sem base MC. Correção = somar a base no codificador |
| `FALHA L1 ... PDE=0000...` | a root aponta memória que não contém PDEs; revisitar `ROOT_MC` |
| `PT_WALK RECUSOU a submissao` | fail-closed funcionou; o `0x119` que vier é da recusa, não de engine travada |

**Abandono:** se a máquina reiniciar antes de qualquer linha `PT_WALK`, parar e analisar
o dump.

## 6. Testes

`driver\wdk\build-contract.bat` — **TODOS OS TESTES APROVADOS (0 falhas)** ·
`audit-mcdm-ddi-matrix.py` PASS · `audit-bc250mcdm-safety.py` PASS ·
`audit-ddi-noop-callbacks.py` APROVADO · `audit-bc250mcdm-package.ps1` PASS.

```text
DriverVer:          3.81.0.0
Source SYS SHA256:  FBDFCCC78BE4862BD36E03228AA04D7FA415943ED50F923D7F65EF9C3AFC72D3
Signed SYS SHA256:  A80F27F7920E831941D528196B86FB3F3782487B0D238DD38B98606DEABFB8AA
CAT SHA256:         EFB59FD0B7A725E67F2500317B38A75FAD1166394812845BFF18F439F6FF7A7D
```

## 7. Estado honesto

Tradução sob VMID8, byte proof do `FillVirtual`, composição do desktop, UMD 3D e jogos
acelerados — **NÃO VALIDADO EM HARDWARE**.
