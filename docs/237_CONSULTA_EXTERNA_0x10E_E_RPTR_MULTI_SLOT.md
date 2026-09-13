# 237 — Consulta Técnica Externa: Corrida das 16:56, BugCheck 0x10E Arg1=0x4 e Inanição na Fila de Retirada SDMA

> **Destinatários:** Claude, DeepSeek, Sparke e revisores de arquitetura WDDM.  
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Base desta análise:** Medição física direta no silício na corrida de 10/09/2026 16:56.  
> **Evidências arquivadas:**  
> - Log exato da corrida: `docs/evidence/2026-09-10_16-56-21-DESKTOP-HQAG6T4-lab-binding/run_log_exact.txt` (6.438 linhas).  
> - Dump de kernel: `C:\Windows\MEMORY.DMP` (1.10 GB, gerado às 16:58:20).  
> - Log do Sistema Windows: Evento 1001 WER — BugCheck `0x0000010E` `(0x4, 0x0, 0xA, 0x0)`.  
> - Fusível anti-bootloop: `C:\BC250_BOOT_FUSE.dat` disparado (`State=2`, `Boots=2`, `LastStatus=0xC0000001`).  

---

## 1. O que foi feito no pacote testado (versão 3.132.1.0)

Em resposta à recomendação de arquitetura do `docs/236` (eliminar a espera síncrona em `DISPATCH_LEVEL` e o back-pressure de slot único que causavam `0x133` e `0x119`), implementamos quatro mudanças:

1. **Alinhamento da Primária UMD em 64 KiB:**
   - Em `driver/umd/bc250_d3d_umd.c`, alinhamos `totalSize` para `alignment = 65536u`, resultando em alocações de `11.075.584 bytes` para 2560×1080 (pitch 10240), garantindo conformidade com os requisitos de alinhamento de tiles do GFX10.
   - Medição no log: `[GetStdAllocData] Filled: Token=0xA110000400000026, Size=11075584, Align=65536`.

2. **Fila Circular Multi-Slot In-Flight (32 slots) para SDMA:**
   - Substituímos a estrutura de slot único `InFlight` por `InFlightQueue[32]` com ponteiros circulares `InFlightHead`, `InFlightTail`, `InFlightCount`.
   - Cada slot possui seu próprio endereço de fence em memória física (`WorkGpuAddress + slotFenceOffset`), com offset independente `BC250_SDMA_PRESENT_FENCE_OFFSET + slotIndex * 4`.

3. **Despacho Assíncrono sem Espera Síncrona:**
   - Removemos o loop de 2000 polls com `YieldProcessor()` de dentro de `Bc250McdmSubmitCommandVirtual` (que roda em `DISPATCH_LEVEL`).
   - A submissão apenas enfileira o pacote no anel SDMA, atualiza o `WPTR` MMIO, agenda uma DPC de retirada (`context->SdmaRetirementDpc`) e retorna `STATUS_SUCCESS` imediatamente.
   - Um timer de 1 ms (`context->SdmaRetirementTimer`) e um poll oportunista no início do submit garantem que a fila seja processada sem prender a CPU.

4. **Discriminador de Layout / Tiling (TILE_PROBE):**
   - Inserimos amostragem somente-leitura dos offsets `0`, `64` (+16 DW) e `10240` (+2560 DW) nos 3 primeiros quadros do `DxgkDdiPresent` para discriminar rasterização linear de micro-tiling Morton 8×8.

---

## 2. O que a medição física no silício revelou

A máquina submeteu, a tela permaneceu preta, após ~10-15 segundos ocorreram travamentos intermitentes e o sistema reiniciou com BSOD.

### A. O SDMA NÃO travou (Hardware 100% Saudável e Ocioso)
Durante a corrida, o SDMA executou **808 submissões**. Nos três momentos em que o sistema de TDR do Windows interveio, a leitura de hardware foi:

- **TDR #1 (linha 3518 do log):**
  `RPTR=29188, WPTR=516`  
  `29188 % 4096 = 516 == WPTR!`  
  `STATUS1=0x000003FF` (todos os 10 bits de status do SDMA em idle!)  
  `ERR_LOG=0x0000000F`  
- **TDR #2 (linha 4580 do log):**
  `RPTR=8324, WPTR=132`  
  `8324 % 4096 = 132 == WPTR!`  
  `STATUS1=0x000003FF` (idle!)  
- **TDR #3 (linha 5529 do log):**
  `RPTR=8452, WPTR=260`  
  `8452 % 4096 = 260 == WPTR!`  
  `STATUS1=0x000003FF` (idle!)  
- **GFXHUB Fault:**  
  `STATUS=00000000 ADDR=00000000:00000000` (zero faltas de paginação da GPU).

**Conclusão inequívoca de silício:** A GPU consumiu todos os pacotes do anel até o último byte, escreveu todas as fences e entrou em estado de repouso perfeito.

---

### B. O BugCheck Real: `0x0000010E` Arg1=`0x4`
O Windows System Event Log registrou:
```text
BugCheck: 0x0000010e (0x0000000000000004, 0x0000000000000000, 0x000000000000000a, 0x0000000000000000)
Arquivo: C:\Windows\MEMORY.DMP (1.10 GB)
```

**Definição oficial da Microsoft para BugCheck 0x10E Arg1 = 0x4 (`VIDEO_MEMORY_MANAGEMENT_INTERNAL`):**
> *"A rotation in a must-succeed path failed."*  
> (Uma rotação de alocação de memória de vídeo pelo VidMm falhou em um caminho crítico que não pode falhar.)

O VidMm tenta rotacionar/evictar alocações de vídeo durante o processo de recuperação de TDR. Para isso, ele depende de que o motor de paginação (Node 0 SDMA) processe e reporte a conclusão das submissões de paginação pendentes via callback de sincronização (`DxgkCbSynchronizeExecution`).

---

### C. A Causa Raiz: Inanição da Cabeça da Fila (*Head-of-Line Starvation*) por Igualdade Estrita (`==`) no RPTR

Ao analisar a contagem de submissões versus retiradas:
- **Submissões assíncronas despachadas:** 288
- **Submissões retiradas e notificadas ao VidSch:** **Apenas 4!** (FenceId=61, 196, 393, 636).
- **Submissões retidas na fila:** 284.

Por que apenas 4 submissões foram retiradas em 288?

Em `driver/wdk/bc250_sdma_present.c`:
```c
static BOOLEAN Bc250SdmaRptrMatches(uint32_t RptrReg, uint32_t RptrWb, uint32_t TargetWptr)
{
    uint32_t target = TargetWptr % BC250_SDMA_PRESENT_RING_BYTES;
    return ((RptrReg % BC250_SDMA_PRESENT_RING_BYTES) == target ||
            (RptrWb % BC250_SDMA_PRESENT_RING_BYTES) == target) ? TRUE : FALSE;
}
```

E no loop de aposentadoria:
```c
headSlot = g_Bc250SdmaPresent.InFlightHead;
head = &g_Bc250SdmaPresent.InFlightQueue[headSlot];
...
if (fenceObserved == nextFence) {
    uint32_t rptrReg = Bc250SdmaRegRead(...);
    uint32_t rptrWbRaw = g_Bc250SdmaPresent.Work[...];

    if (Bc250SdmaRptrMatches(rptrReg, rptrWbRaw, targetWptrBytes)) {
        // Aposenta a submissao e avanca InFlightHead
        g_Bc250SdmaPresent.InFlightHead = (headSlot + 1u) % 32;
        g_Bc250SdmaPresent.InFlightCount--;
    }
}
```

#### O Mecanismo da Falha:
1. Em um sistema assíncrono multi-slot, o VidSch despacha várias submissões seguidas:
   - Submissão #1: `TargetWptr = 64`
   - Submissão #2: `TargetWptr = 128`
   - Submissão #3: `TargetWptr = 192`
2. O hardware SDMA executa em velocidade de silício (nanossegundos/microssegundos). Ele processa a #1, a #2 e a #3 quase instantaneamente.
3. Quando o DPC ou o poll da CPU é executado para verificar a Submissão #1 (na cabeça da fila `InFlightHead = 0`):
   - A GPU já terminou tudo! O registrador `RPTR` já está em `192`!
   - A função `Bc250SdmaRptrMatches` compara: `(192 % 4096) == (64 % 4096)`.
   - **`192 == 64` é FALSO!**
4. Apesar de a GPU já ter ultrapassado e concluído a Submissão #1 há muito tempo, a comparação por **igualdade estrita** falha!
5. Como `Bc250SdmaRptrMatches` retorna `FALSE`, a Submissão #1 **NÃO é aposentada**.
6. `InFlightHead` permanece apontando para o slot 0.
7. Como a fila é FIFO e a cabeça nunca avança, **as Submissões #2, #3, #4... NUNCA são avaliadas nem aposentadas**.
8. A Submissão #1 só se aposenta se, por pura coincidência de timing, o `RPTR` for lido exatamente no instante em que for igual a 64 (o que só aconteceu 4 vezes em 288 submissões).
9. O VidSch espera pela notificação de fence da Submissão #1 (ou subsequentes). Após 2 segundos, dispara o TDR #1.
10. O TDR reseta o SDMA, tenta reiniciar. Novas submissões entram na fila e sofrem a mesmíssima inanição.
11. Após 3 TDRs consecutivos, o VidMm tenta rotacionar alocações em caminho de recuperação (*must-succeed*), falha por falta de resposta do motor de paginação, e derruba a máquina com **BugCheck `0x10E` Arg1=`0x4`**.

---

## 3. As Perguntas para os Especialistas (Claude / DeepSeek / Revisores)

### Pergunta 1: Qual o critério correto de completude para a fila multi-slot SDMA?
Em pacotes SDMA, emitimos explicitamente um `SDMA_OP_FENCE` que grava um valor único `nextFence` no endereço `WorkGpuAddress + slotFenceOffset`:
- Se a GPU já gravou `fenceObserved == nextFence` sobre o poison (`0x43DAFFFE`), isso já é uma garantia física de que o hardware executou todas as instruções precedentes daquele pacote e realizou o flush?
- Faz sentido subordinar a aposentadoria da submissão à leitura do `RPTR` do anel quando já temos a fence de memória escrita pela própria GPU para aquele slot específico?
- Se a conferência de anel for mantida, qual a formulação matemática padrão para determinar se `RPTR` avançou além de `TargetWptr` em um anel circular com wrap de 4096 bytes?
  Seria:
  `((uint32_t)(Rptr - StartWptr) >= (uint32_t)(TargetWptr - StartWptr))`?

### Pergunta 2: Aposentadoria Fora de Ordem vs Em Ordem (FIFO)
O VidSch espera que as notificações de DMA buffer de paginação (`DxgkCbNotifyInterrupt` com `SubmissionFenceId`) sejam entregues em ordem estritamente crescente de `SubmissionFenceId`?
- Se o slot 0 estiver avaliando uma condição e o slot 1 já tiver sua fence gravada, podemos avançar a fila com base na maior fence observada (marca d'água / *high-water mark*), ou o VidSch exige que cada `SubmissionFenceId` individual gere sua notificação em sequência?

### Pergunta 3: Gestão de DPC e Timer
Atualmente temos:
- Um poll oportunista em `SubmitCommandVirtual` (rodando em `DISPATCH_LEVEL`).
- `KeInsertQueueDpc(&context->SdmaRetirementDpc)` agendado no submit.
- Um timer de 1 ms (`KeSetTimerEx`) para re-agendar a DPC caso a fila ainda contenha itens pendentes.
Essa arquitetura assíncrona é adequada para satisfazer o VidSch do Windows 10 WDDM 2.7, ou há armadilhas de latência/IRQL que devemos antecipar para evitar novos TDRs?

---

## 4. Próximos Passos Imediatos
1. Antes de qualquer novo teste, o fusível de boot deve ser resetado com:
   `tools\inspect-boot-fuse.ps1 -Reset`
2. Corrigir o critério de avanço de `InFlightHead` para não depender de igualdade pontual de `RPTR`.
3. Validar a correção via testes contratuais offline antes de gravar novo pacote.
