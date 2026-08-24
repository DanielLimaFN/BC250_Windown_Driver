/* log.c — ring de log do bc250dbg.
 *
 * Desenho (ver roadmap secao 7.3):
 *   - buffer circular em non-paged pool, entradas de tamanho fixo;
 *   - produtor lock-free: reivindica a sequencia com InterlockedIncrement64,
 *     escreve o corpo e so entao publica escrevendo Seq (barreira antes);
 *   - consumidor le o mesmo buffer mapeado read-only em modo usuario, sem
 *     um IOCTL por linha — o objetivo e capturar sequencia de init inteira
 *     sem que o proprio ato de logar mude o timing.
 *
 * Regra do caminho quente: nada de alocacao, nada de lock, nada de
 * formatacao de string. Evento vira id + quatro argumentos; o texto e
 * resolvido no user mode. BcLogText existe so para caminhos frios.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bc250dbg.h"

/* O consumidor em user mode indexa o ring por aritmetica: se o tamanho da
 * entrada divergir da ABI, ele le lixo silenciosamente. */
C_ASSERT(sizeof(BC250DBG_LOG_ENTRY) == BC250DBG_LOG_ENTRY_SIZE);
C_ASSERT(sizeof(BC250DBG_LOG_HEADER) <= BC250DBG_LOG_HEADER_SIZE);

/* Arredonda para a potencia de dois <= v (o ring exige mascara). */
static ULONG BcpFloorPow2(ULONG v)
{
    ULONG p = 1;
    while ((p << 1) != 0 && (p << 1) <= v) {
        p <<= 1;
    }
    return p;
}

NTSTATUS BcLogInit(ULONG EntryCount)
{
    ULONG count = BcpFloorPow2(EntryCount ? EntryCount : BC250DBG_LOG_DEFAULT_COUNT);
    ULONG total;

    if (count < 256) {
        count = 256;
    }

    total = BC250DBG_LOG_HEADER_SIZE + count * BC250DBG_LOG_ENTRY_SIZE;

    g_Dev.LogBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, total, BC250DBG_POOL_TAG);
    if (g_Dev.LogBuffer == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    g_Dev.LogTotalBytes = total;
    g_Dev.LogEntryCount = count;
    g_Dev.LogEntryMask  = count - 1;
    g_Dev.LogHeader     = (BC250DBG_LOG_HEADER *)g_Dev.LogBuffer;
    g_Dev.LogEntries    = (BC250DBG_LOG_ENTRY *)((PUCHAR)g_Dev.LogBuffer +
                                                 BC250DBG_LOG_HEADER_SIZE);

    KeQueryPerformanceCounter(&g_Dev.QpcFrequency);

    g_Dev.LogHeader->Magic        = BC250DBG_LOG_MAGIC;
    g_Dev.LogHeader->Version      = BC250DBG_ABI_VERSION;
    g_Dev.LogHeader->EntrySize    = BC250DBG_LOG_ENTRY_SIZE;
    g_Dev.LogHeader->EntryCount   = count;
    g_Dev.LogHeader->Head         = 0;
    g_Dev.LogHeader->Dropped      = 0;
    g_Dev.LogHeader->QpcFrequency = (UINT64)g_Dev.QpcFrequency.QuadPart;
    g_Dev.LogHeader->EntryOffset  = BC250DBG_LOG_HEADER_SIZE;

    /* MDL construida uma vez; cada processo que mapear usa a mesma. */
    g_Dev.LogMdl = IoAllocateMdl(g_Dev.LogBuffer, total, FALSE, FALSE, NULL);
    if (g_Dev.LogMdl == NULL) {
        ExFreePoolWithTag(g_Dev.LogBuffer, BC250DBG_POOL_TAG);
        g_Dev.LogBuffer = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    MmBuildMdlForNonPagedPool(g_Dev.LogMdl);

    InterlockedExchange(&g_Dev.LogLevelMask, BC250DBG_LVL_ERROR |
                                             BC250DBG_LVL_WARN  |
                                             BC250DBG_LVL_INFO);
    InterlockedExchange(&g_Dev.LogSubsysMask, BC250DBG_SUB_ALL);

    return STATUS_SUCCESS;
}

VOID BcLogFree(VOID)
{
    if (g_Dev.LogMdl != NULL) {
        IoFreeMdl(g_Dev.LogMdl);
        g_Dev.LogMdl = NULL;
    }
    if (g_Dev.LogBuffer != NULL) {
        ExFreePoolWithTag(g_Dev.LogBuffer, BC250DBG_POOL_TAG);
        g_Dev.LogBuffer  = NULL;
        g_Dev.LogHeader  = NULL;
        g_Dev.LogEntries = NULL;
    }
}

/* Reivindica um slot e devolve a entrada ja zerada, com Seq pendente.
 * Devolve NULL se o log esta desligado para este nivel/subsistema. */
static BC250DBG_LOG_ENTRY *BcpClaim(ULONG Level, ULONG Subsys, INT64 *SeqOut)
{
    INT64 seq;
    BC250DBG_LOG_ENTRY *e;

    if (g_Dev.LogEntries == NULL) {
        return NULL;
    }
    if ((Level & (ULONG)g_Dev.LogLevelMask) == 0) {
        return NULL;
    }
    if ((Subsys & (ULONG)g_Dev.LogSubsysMask) == 0) {
        return NULL;
    }

    seq = InterlockedIncrement64((volatile LONG64 *)&g_Dev.LogHeader->Head);
    e = &g_Dev.LogEntries[(seq - 1) & g_Dev.LogEntryMask];

    /* Invalida o slot antes de reescrever: se o consumidor estiver lendo
     * exatamente esta posicao, ele ve Seq incoerente e trata como perda,
     * em vez de ler um registro meio antigo meio novo. */
    InterlockedExchange64((volatile LONG64 *)&e->Seq, 0);

    *SeqOut = seq;
    return e;
}

static VOID BcpPublish(BC250DBG_LOG_ENTRY *e, INT64 Seq)
{
    KeMemoryBarrier();
    InterlockedExchange64((volatile LONG64 *)&e->Seq, Seq);
}

VOID BcLogEvent(ULONG Level, ULONG Subsys, ULONG EventId,
                UINT64 A0, UINT64 A1, UINT64 A2, UINT64 A3)
{
    INT64 seq;
    BC250DBG_LOG_ENTRY *e = BcpClaim(Level, Subsys, &seq);
    LARGE_INTEGER qpc;

    if (e == NULL) {
        return;
    }

    qpc = KeQueryPerformanceCounter(NULL);

    e->Qpc     = (UINT64)qpc.QuadPart;
    e->Level   = Level;
    e->Subsys  = Subsys;
    e->EventId = EventId;
    e->Irql    = (ULONG)KeGetCurrentIrql();
    e->A[0]    = A0;
    e->A[1]    = A1;
    e->A[2]    = A2;
    e->A[3]    = A3;
    e->Text[0] = '\0';

    BcpPublish(e, seq);
}

VOID BcLogText(ULONG Level, ULONG Subsys, PCSTR Format, ...)
{
    INT64 seq;
    BC250DBG_LOG_ENTRY *e;
    LARGE_INTEGER qpc;
    va_list args;
    char buf[BC250DBG_LOG_TEXT_LEN];

    /* Formatacao so e legal em IRQL baixo; caminhos quentes usam BcLogEvent. */
    if (KeGetCurrentIrql() > DISPATCH_LEVEL) {
        return;
    }

    e = BcpClaim(Level, Subsys, &seq);
    if (e == NULL) {
        return;
    }

    va_start(args, Format);
    if (!NT_SUCCESS(RtlStringCbVPrintfA(buf, sizeof(buf), Format, args))) {
        buf[0] = '\0';
    }
    va_end(args);

    qpc = KeQueryPerformanceCounter(NULL);

    e->Qpc     = (UINT64)qpc.QuadPart;
    e->Level   = Level;
    e->Subsys  = Subsys;
    e->EventId = BC250DBG_EV_TEXT;
    e->Irql    = (ULONG)KeGetCurrentIrql();
    e->A[0] = e->A[1] = e->A[2] = e->A[3] = 0;
    RtlCopyMemory(e->Text, buf, sizeof(e->Text));
    e->Text[BC250DBG_LOG_TEXT_LEN - 1] = '\0';

    BcpPublish(e, seq);

    /* Espelho para o depurador: o ring vive em RAM e morre no bugcheck.
     * Quem sobrevive a travamento e o KDNET/DebugView. */
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "bc250dbg: %s\n", buf);
}

NTSTATUS BcLogMapToUser(PBC250DBG_FILE_CONTEXT Ctx, BC250DBG_LOG_MAP_OUT *Out)
{
    PVOID va = NULL;

    if (g_Dev.LogMdl == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (Ctx->LogUserVa != NULL) {
        return STATUS_ALREADY_COMMITTED;   /* um mapeamento por handle */
    }

    /* MmMapLockedPagesSpecifyCache levanta excecao em falha. O mapeamento e
     * read-only de proposito: o consumidor nunca escreve no ring. */
    __try {
        va = MmMapLockedPagesSpecifyCache(g_Dev.LogMdl, UserMode, MmCached,
                                          NULL, FALSE, NormalPagePriority);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        va = NULL;
    }

    if (va == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Ctx->LogUserVa  = va;
    Ctx->LogProcess = PsGetCurrentProcess();

    Out->Size        = sizeof(*Out);
    Out->EntryCount  = g_Dev.LogEntryCount;
    Out->EntrySize   = BC250DBG_LOG_ENTRY_SIZE;
    Out->TotalBytes  = g_Dev.LogTotalBytes;
    Out->UserAddress = (UINT64)(ULONG_PTR)va;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE,
              "log ring mapeado (%u entradas)", g_Dev.LogEntryCount);
    return STATUS_SUCCESS;
}

VOID BcLogUnmapFromUser(PBC250DBG_FILE_CONTEXT Ctx)
{
    /* Precisa rodar no contexto do processo que mapeou — por isso o unmap
     * fica no IRP_MJ_CLEANUP, entregue nesse contexto. */
    if (Ctx->LogUserVa != NULL && g_Dev.LogMdl != NULL) {
        __try {
            MmUnmapLockedPages(Ctx->LogUserVa, g_Dev.LogMdl);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            /* nada a fazer: processo morrendo */
        }
        Ctx->LogUserVa  = NULL;
        Ctx->LogProcess = NULL;
    }
}
