#include "bc250_active_miniport.h"
#include "bc250_cp_firmware_manifest.h"
#include "../core/bc250_firmware_staging.h"
#include "../core/bc250_psp_load_fw_transaction.h"
#include "../core/bc250_pm4_packet_builder.h"
#include "bc250_vram_cpu_path.h"
#include "bc250_display_ddi.h"

#define BC250_PFP_FILE_BYTES 263424u
#define BC250_PFP_PAYLOAD_BYTES_LOCAL 263168u
#define BC250_PFP_JT_OFFSET_DWORDS 65536u
#define BC250_PFP_JT_DWORDS 128u
#define BC250_PFP_UCODE_VERSION 0x94u

#define BC250_ME_FILE_BYTES 263424u
#define BC250_ME_PAYLOAD_BYTES_LOCAL 263168u
#define BC250_ME_JT_OFFSET_DWORDS 65536u
#define BC250_ME_JT_DWORDS 128u
#define BC250_ME_UCODE_VERSION 0x63u

#define BC250_CE_FILE_BYTES 263296u
#define BC250_CE_PAYLOAD_BYTES_LOCAL 263040u
#define BC250_CE_JT_OFFSET_DWORDS 65536u
#define BC250_CE_JT_DWORDS 96u
#define BC250_CE_UCODE_VERSION 0x25u

#define BC250_MEC_FILE_BYTES 268592u
#define BC250_MEC_PAYLOAD_BYTES_LOCAL 268336u
#define BC250_MEC_JT_OFFSET_DWORDS 66860u
#define BC250_MEC_JT_DWORDS 224u
#define BC250_MEC_UCODE_VERSION 0x90u

#define BC250_PSP_TMR_BYTES 0x00400000u /* 4 MiB TMR */
#define BC250_CP_QUAD_ALLOCATION_BYTES 0x00110000u
#define BC250_CP_PFP_SLICE_OFFSET 0x00000000u
#define BC250_CP_ME_SLICE_OFFSET  0x00041000u
#define BC250_CP_CE_SLICE_OFFSET  0x00082000u
#define BC250_CP_MEC_SLICE_OFFSET 0x000C3000u
#define BC250_CP_RB0_RING_OFFSET  0x00105000u
#define BC250_CP_RB0_RING_BYTES   0x00001000u
#define BC250_CP_RB0_RPTR_WB_OFFSET 0x00106000u
#define BC250_CP_RB0_WPTR_POLL_OFFSET 0x00106100u
#define BC250_CP_RB0_FENCE_OFFSET 0x00106200u
/* Buffer de destino para a conferência byte a byte — a terceira perna do tripé
 * de CLAUDE.md, que este caminho ainda não fazia. O SDMA já compara
 * (ComparedDwords/MismatchCount); o CP Ring 0 só conferia fence e SCRATCH. */
#define BC250_CP_RB0_DEST_OFFSET  0x00106300u
#define BC250_CP_RB0_DEST_DWORDS  4u
#define BC250_CP_RB0_DEST_POISON  0xDEADC000u
#define BC250_CP_RB0_DEST_PATTERN 0xBC250000u
#define BC250_CP_PT_SLICE_OFFSET  0x00107000u
#define BC250_CP_PT_SLICE_BYTES   0x00001000u

#define BC250_CP_RB0_FENCE_POISON 0x43DAFFFEu
#define BC250_CP_RB0_FENCE_VALUE  0xBC250123u

#define BC250_PFP_POOL_TAG 'pPfB'
#define BC250_PFP_HALT 0x04000000u
#define BC250_ME_HALT  0x10000000u
#define BC250_CE_HALT  0x01000000u

#define R_CP_STAT                   0x00008680u
#define R_CP_ME_CNTL                0x000086D8u
#define R_CP_MEC_CNTL               0x00008234u
#define R_CP_PFP_PC                 0x00008694u
#define R_CP_ME_PC                  0x00008698u
#define R_CP_CE_PC                  0x0000869Cu
#define R_CP_MEC1_PC                0x000086A0u
#define R_CP_PFP_JT_STAT            0x0000C4CCu
#define R_CP_CE_JT_STAT             0x0000C4D0u
#define R_CP_MEC_JT_STAT            0x0000C4D4u
#define R_CP_PFP_IC_BASE_LO         0x0003E100u
#define R_CP_PFP_IC_BASE_HI         0x0003E104u
#define R_CP_PFP_IC_BASE_CNTL       0x0003E108u
#define R_CP_PFP_UCODE_ADDR         0x0003E050u
#define R_CP_PFP_UCODE_DATA         0x0003E054u
#define R_CP_ME_IC_BASE_LO          0x0003E110u
#define R_CP_ME_IC_BASE_HI          0x0003E114u
#define R_CP_ME_IC_BASE_CNTL        0x0003E118u
#define R_CP_ME_UCODE_ADDR          0x0003E058u
#define R_CP_ME_UCODE_DATA          0x0003E05Cu
#define R_CP_CE_IC_BASE_LO          0x0003E120u
#define R_CP_CE_IC_BASE_HI          0x0003E124u
#define R_CP_CE_IC_BASE_CNTL        0x0003E128u
#define R_CP_CE_UCODE_ADDR          0x0003E060u
#define R_CP_CE_UCODE_DATA          0x0003E064u
#define R_CP_CPC_IC_BASE_LO         0x0003E130u
#define R_CP_CPC_IC_BASE_HI         0x0003E134u
#define R_CP_CPC_IC_BASE_CNTL       0x0003E138u
#define R_CP_MEC_ME1_UCODE_ADDR     0x0003E068u
#define R_CP_MEC_ME1_UCODE_DATA     0x0003E06Cu

#define R_CP_PFP_IC_OP_CNTL         0x0003E10Cu
#define R_CP_ME_IC_OP_CNTL          0x0003E11Cu
#define R_CP_CE_IC_OP_CNTL          0x0003E12Cu
#define R_CP_CPC_IC_OP_CNTL         0x0003E13Cu

/* Campos de CP_*_IC_OP_CNTL, conferidos em inc/linux_ref/gc_10_1_0_sh_mask.h:39510.
 * INVALIDATE_CACHE e PRIME_ICACHE sao comandos; os bits COMPLETE/PRIMED sao o
 * status que o hardware sobe quando a operacao termina. Escrever o comando sem
 * aguardar o status deixa um fill de cache em voo, e destravar o microengine
 * nesse estado congela o PFP. */
#define BC250_IC_OP_INVALIDATE_CACHE          0x00000001u
#define BC250_IC_OP_INVALIDATE_CACHE_COMPLETE 0x00000002u
#define BC250_IC_OP_PRIME_ICACHE              0x00000010u
#define BC250_IC_OP_ICACHE_PRIMED             0x00000020u
#define BC250_IC_OP_POLL_ITERATIONS           1000u

/* Bits da mascara de evidencia por engine, na ordem PFP/ME/CE/CPC. */
#define BC250_IC_ENGINE_PFP                   0x00000001u
#define BC250_IC_ENGINE_ME                    0x00000002u
#define BC250_IC_ENGINE_CE                    0x00000004u
#define BC250_IC_ENGINE_CPC                   0x00000008u

#define R_GCVM_CONTEXT0_CNTL        0x0000A200u
#define R_GCVM_CONTEXT0_PT_BASE_LO  0x0000A3ACu
#define R_GCVM_CONTEXT0_PT_BASE_HI  0x0000A3B0u
#define R_GCVM_CONTEXT0_PT_START_LO 0x0000A42Cu
#define R_GCVM_CONTEXT0_PT_END_LO   0x0000A4ACu

#define R_FAULT_STATUS              0x0000A130u
#define R_FAULT_ADDR_LO             0x0000A134u
#define R_FAULT_ADDR_HI             0x0000A138u
#define R_RLC_CNTL                  0x0003B000u
#define R_GCVM_L2_CNTL2             0x0000A104u
/* Bloco SEM vem imediatamente antes do bloco REQ: 0x0A244 + 18*4 = 0x0A28C.
 * A adjacencia exata confirma o stride de 4 bytes por engine. */
#define R_GCVM_INVALIDATE_ENG0_SEM  0x0000A244u
#define R_GCVM_INVALIDATE_ENG0_REQ  0x0000A28Cu
#define R_GCVM_INVALIDATE_ENG0_ACK  0x0000A2D4u

/*
 * Engine de invalidacao usada para flush de TLB/L2 durante a carga do CP.
 *
 * O Engine 0 NAO pode ser usado: no BC-250 ele e' tambem o Display Engine
 * (VMID 0), e invalida-lo desliga o scanout imediatamente. Essa e' a causa-raiz
 * no. 2 da tela preta, documentada em
 * docs/21_PONTO_DE_RETOMADA_WDDM_DISPLAY.md e em STATUS_E_ROADMAP_CONSOLIDADO.md
 * secao 2.0.1.
 *
 * Aqui a invalidacao tem proposito de coerencia de cache, nao semantico: ela
 * acompanha o flush de HDP para que o CP enxergue o firmware recem-escrito.
 * Qualquer engine != 0 serve.
 *
 * Stride confirmado contra docs/03_CATALOGO_DE_REGISTRADORES.md:277-278, que
 * cataloga ENG0 e ENG17:
 *
 *     REQ_ENGn = 0x0000A28C + n*4     ENG0=0x0A28C   ENG17=0x0A2D0
 *     ACK_ENGn = 0x0000A2D4 + n*4     ENG0=0x0A2D4   ENG17=0x0A318
 *
 * Se a engine escolhida nao responder, o poll de ACK esgota e o fluxo segue sem
 * invalidacao. Esse modo de falha e' aceitavel: o caminho do bc250compute fecha
 * o tripe fisico do SDMA0 com o display ativo usando APENAS o flush de HDP, sem
 * nenhuma invalidacao de GCVM (STATUS_E_ROADMAP_CONSOLIDADO.md secao 2.0).
 */
/* A engine deixou de ser constante de compilacao: vem de
 * Context->GcvmInvalidateEngine, alimentado pelo tunable de registro
 * GcvmInvalidateEngine (default 17). Trocar de engine passa a custar um
 * valor de registro e um ciclo de PnP, nao build + assinatura + instalacao. */
#define BC250_GCVM_INVALIDATE_REQ_OF(eng) \
    (R_GCVM_INVALIDATE_ENG0_REQ + ((eng) * 4u))
#define BC250_GCVM_INVALIDATE_ACK_OF(eng) \
    (R_GCVM_INVALIDATE_ENG0_ACK + ((eng) * 4u))
#define BC250_GCVM_INVALIDATE_SEM_OF(eng) \
    (R_GCVM_INVALIDATE_ENG0_SEM + ((eng) * 4u))

/* Bit 16 de ACK e' o estado do semaforo, e e' LEITURA PURA. Ler o proprio
 * registrador SEM *adquire* o semaforo — docs/03_CATALOGO_DE_REGISTRADORES.md
 * secao "AVISO", classe P0: um dump de laboratorio ja' reteve o semaforo ate' o
 * reboot por ler esse registrador achando que observava. */
#define BC250_GCVM_ACK_SEMAPHORE_BIT 0x00010000u
#define R_VGT_NUM_INSTANCES         0x00030934u
#define R_PA_SC_LINE_STIPPLE_STATE  0x00030A04u
#define R_CGTT_CPF_CLK_CTRL         0x0003C2C4u
#define R_GE_FAST_CLKS              0x00008920u
#define R_CP_SD_CNTL                0x0000C6DCu
#define R_GRBM_GFX_INDEX            0x00030800u
#define R_GRBM_GFX_CNTL             0x00008088u
#define R_GRBM_STATUS2              0x00008008u
#define R_GRBM_STATUS               0x00008010u
#define R_GRBM_SOFT_RESET           0x00008020u

#define R_CP_MAX_CONTEXT            0x0000C2B8u
#define R_CP_DEVICE_ID              0x0000C12Cu

#define R_CP_RB0_BASE               0x0000C100u
#define R_CP_RB0_BASE_HI            0x0000C2C4u
#define R_CP_RB0_CNTL               0x0000C104u
#define R_CP_RB0_RPTR               0x00008700u
#define R_CP_RB0_WPTR               0x0000C150u
#define R_CP_RB0_WPTR_HI            0x0000C154u
#define R_CP_RB0_RPTR_ADDR          0x0000C10Cu
#define R_CP_RB0_RPTR_ADDR_HI       0x0000C110u
#define R_CP_RB_WPTR_POLL_ADDR_LO   0x0000C3ACu
#define R_CP_RB_WPTR_POLL_ADDR_HI   0x0000C3B0u
#define R_CP_RB0_ACTIVE             0x0000C680u
#define R_CP_RB_VMID                0x0000C144u
#define R_CP_RB_WPTR_DELAY          0x00008704u
#define R_CP_RB_WPTR_POLL_CNTL      0x00008708u
#define R_CP_RB_DOORBELL_CONTROL    0x0000C3B4u
/*
 * HDP.1 (28/09, doc 519): chamava-se R_HDP_MEM_COHERENCY_FLUSH_CNTL, mas 0x3FC4
 * e' a INVALIDACAO de leitura do HDP. So' o nome mudou aqui: este caminho roda
 * no StartDevice e ja funciona (P0). DIVIDA DECLARADA: o firmware escrito pela
 * CPU nao recebe flush de escrita real antes de o CP le-lo.
 */
#define R_HDP_READ_CACHE_INVALIDATE 0x00003FC4u

#define R_CP_SCRATCH_REG0           0x00030100u

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
static uint32_t Bc250PfpRead(
    BC250_DXGK_ACTIVE_CONTEXT *Context, uint32_t Offset, uint32_t *Value)
{
    return Bc250MmioTopologyRead32(
        &Context->Core.MmioTransport, Offset, Value);
}

static uint32_t Bc250PfpWrite(
    BC250_DXGK_ACTIVE_CONTEXT *Context, uint32_t Offset, uint32_t Value)
{
    return Bc250MmioTopologyWrite32(
        &Context->Core.MmioTransport, Offset, Value);
}

/* Dispara uma operacao de I-Cache num CP_*_IC_OP_CNTL e aguarda o bit de
 * conclusao correspondente. Retorna 1 somente se o hardware confirmou. */
_Check_return_
static uint32_t Bc250PfpIcacheOp(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ uint32_t OpRegister,
    _In_ uint32_t OpMask,
    _In_ uint32_t CompleteMask)
{
    uint32_t value = 0u;
    uint32_t i;

    if (!Bc250PfpRead(Context, OpRegister, &value))
        return 0u;

    /* Formula de bits SOC15: preserva o restante do registrador. */
    value = (value & ~OpMask) | OpMask;
    if (!Bc250PfpWrite(Context, OpRegister, value))
        return 0u;

    for (i = 0u; i < BC250_IC_OP_POLL_ITERATIONS; ++i) {
        if (Bc250PfpRead(Context, OpRegister, &value) &&
            (value & CompleteMask) != 0u) {
            return 1u;
        }
        KeStallExecutionProcessor(1u);
    }

    return 0u;
}

/* Aplica a mesma operacao aos quatro microengines e devolve a mascara das que
 * confirmaram. Mascara diferente de 0x0F e evidencia de cache nao tratada. */
static uint32_t Bc250PfpIcacheOpAll(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ uint32_t OpMask,
    _In_ uint32_t CompleteMask)
{
    uint32_t mask = 0u;

    if (Bc250PfpIcacheOp(Context, R_CP_PFP_IC_OP_CNTL, OpMask, CompleteMask))
        mask |= BC250_IC_ENGINE_PFP;
    if (Bc250PfpIcacheOp(Context, R_CP_ME_IC_OP_CNTL, OpMask, CompleteMask))
        mask |= BC250_IC_ENGINE_ME;
    if (Bc250PfpIcacheOp(Context, R_CP_CE_IC_OP_CNTL, OpMask, CompleteMask))
        mask |= BC250_IC_ENGINE_CE;
    if (Bc250PfpIcacheOp(Context, R_CP_CPC_IC_OP_CNTL, OpMask, CompleteMask))
        mask |= BC250_IC_ENGINE_CPC;

    return mask;
}

/* Invalidacao de GCVM seguindo a ordem do upstream (gmc_v10_0_flush_gpu_tlb):
 * adquire o semaforo, escreve o REQ, aguarda o ACK, libera o semaforo.
 *
 * O patch 0001 de referencias/bc250-gfx1013-fix forca use_semaphore no caminho
 * GFXHUB do gfx1013 justamente porque o BC-250 precisa dele; este carregador
 * nunca tocou no semaforo. O protocolo fica atras de UseSemaphore para poder ser
 * ligado e desligado por tunable, sem rebuild.
 *
 * A liberacao acontece em TODOS os caminhos de saida: reter o semaforo o prende
 * ate' o reboot.
 *
 * AckRaw devolve o valor cru do ACK — o bit 16 e' o estado do semaforo e e'
 * leitura segura, ao contrario do registrador SEM. */
static uint32_t Bc250PfpGcvmInvalidate(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ uint32_t Engine,
    _In_ uint32_t Request,
    _In_ uint32_t UseSemaphore,
    _Out_ uint32_t *AckRawBefore,
    _Out_ uint32_t *AckRawAfter,
    _Out_ uint32_t *Polls,
    _Out_ uint32_t *SemaphoreAcquired)
{
    const uint32_t req = BC250_GCVM_INVALIDATE_REQ_OF(Engine);
    const uint32_t ack = BC250_GCVM_INVALIDATE_ACK_OF(Engine);
    const uint32_t sem = BC250_GCVM_INVALIDATE_SEM_OF(Engine);
    uint32_t value = 0u;
    uint32_t acknowledged = 0u;
    uint32_t i;

    *AckRawBefore = 0u;
    *AckRawAfter = 0u;
    *Polls = 0u;
    *SemaphoreAcquired = 0u;

    if (Bc250PfpRead(Context, ack, &value)) {
        *AckRawBefore = value;
    }

    if (UseSemaphore != 0u) {
        for (i = 0u; i < 1000u; ++i) {
            /* Ler SEM e' a operacao de aquisicao; 1 significa adquirido. */
            if (Bc250PfpRead(Context, sem, &value) && (value & 1u) != 0u) {
                *SemaphoreAcquired = 1u;
                break;
            }
            KeStallExecutionProcessor(1u);
        }
    }

    (void)Bc250PfpWrite(Context, req, Request);

    for (i = 0u; i < 100u; ++i) {
        if (Bc250PfpRead(Context, ack, &value) && (value & 1u) != 0u) {
            acknowledged = 1u;
            break;
        }
        KeStallExecutionProcessor(1u);
    }
    *Polls = i;

    if (Bc250PfpRead(Context, ack, &value)) {
        *AckRawAfter = value;
    }

    if (*SemaphoreAcquired != 0u) {
        /* Escrever 0 libera. Sem isto o semaforo fica retido ate' o reboot. */
        (void)Bc250PfpWrite(Context, sem, 0u);
    }

    return acknowledged;
}

static void Bc250PfpCopyFromDevice(
    uint32_t *Destination, volatile uint32_t *Source, uint32_t Dwords)
{
    uint32_t i;
    for (i = 0u; i < Dwords; ++i)
        Destination[i] = READ_REGISTER_ULONG((volatile ULONG *)&Source[i]);
}

static void Bc250PfpCopyToDevice(
    volatile uint32_t *Destination, const uint32_t *Source, uint32_t Dwords)
{
    uint32_t i;
    for (i = 0u; i < Dwords; ++i)
        WRITE_REGISTER_ULONG((volatile ULONG *)&Destination[i], Source[i]);
    KeMemoryBarrier();
}

static NTSTATUS Bc250LoadFirmwareFile(
    const WCHAR *FilePath,
    uint32_t ExpectedFileBytes,
    uint32_t ExpectedPayloadBytes,
    const unsigned char *ExpectedSha256,
    PVOID *OutBuffer)
{
    UNICODE_STRING uPath;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE handle = NULL;
    FILE_STANDARD_INFORMATION fsi;
    NTSTATUS st;
    PVOID buf = NULL;
    unsigned char fileHash[32];
    unsigned char payloadHash[32];

    if (OutBuffer == NULL) return STATUS_INVALID_PARAMETER;
    *OutBuffer = NULL;

    RtlInitUnicodeString(&uPath, FilePath);
    InitializeObjectAttributes(
        &oa, &uPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    st = ZwOpenFile(&handle, FILE_GENERIC_READ | SYNCHRONIZE, &oa, &iosb,
        FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(st)) return st;

    st = ZwQueryInformationFile(
        handle, &iosb, &fsi, sizeof(fsi), FileStandardInformation);
    if (!NT_SUCCESS(st) || fsi.EndOfFile.QuadPart != (LONGLONG)ExpectedFileBytes) {
        ZwClose(handle);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)ExpectedFileBytes, BC250_PFP_POOL_TAG);
    if (buf == NULL) {
        ZwClose(handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    st = ZwReadFile(
        handle, NULL, NULL, NULL, &iosb, buf, ExpectedFileBytes, NULL, NULL);
    ZwClose(handle);
    if (!NT_SUCCESS(st) || iosb.Information != ExpectedFileBytes) {
        ExFreePoolWithTag(buf, BC250_PFP_POOL_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Bc250FirmwareSha256(buf, ExpectedFileBytes, fileHash);
    Bc250FirmwareSha256((const PUCHAR)buf + 0x100u, ExpectedPayloadBytes, payloadHash);

    if (ExpectedSha256 != NULL && !Bc250FirmwareDigestEqual(payloadHash, ExpectedSha256)) {
        ExFreePoolWithTag(buf, BC250_PFP_POOL_TAG);
        return STATUS_IMAGE_CHECKSUM_MISMATCH;
    }

    *OutBuffer = buf;
    return STATUS_SUCCESS;
}

BC250_PFP_DIRECT_LOAD_STATUS Bc250ActiveRunPfpDirectLoad(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PFP_DIRECT_LOAD_EVIDENCE *Evidence)
{
    BC250_PFP_DIRECT_LOAD_STATUS status = BC250_PFP_DIRECT_LOAD_OK;
    uint32_t invalidateEngine;
    uint32_t invalidateUseSemaphore;
    uint32_t invalidateReq;
    NTSTATUS ntStatus;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_VRAM_ALLOCATOR *allocator;
    const BC250_VRAM_ADDRESS_SPACE *addressSpace;
    BC250_VRAM_TOKEN token = {0};
    BC250_VRAM_REGION region = {0};
    BC250_VRAM_TOKEN tmrToken = {0};
    BC250_VRAM_REGION tmrRegion = {0};
    BC250_VRAM_STATUS vramStatus;
    const BC250_RESOLVED_BAR_RESOURCE *bar = NULL;
    PHYSICAL_ADDRESS physical;
    volatile uint32_t *mapped = NULL;
    volatile uint32_t *pfpMapped = NULL;
    volatile uint32_t *meMapped = NULL;
    volatile uint32_t *ceMapped = NULL;
    volatile uint32_t *mecMapped = NULL;
    volatile uint32_t *pRing = NULL;
    volatile uint32_t *pRptrWb = NULL;
    volatile uint32_t *pWptrPoll = NULL;
    volatile uint32_t *pFence = NULL;
    volatile uint32_t *pDest = NULL;
    uint32_t *backup = NULL;
    uint32_t mappedDwords = 0u;
    uint32_t pfpPayloadDwords = 0u;
    uint32_t mePayloadDwords  = 0u;
    uint32_t cePayloadDwords  = 0u;
    uint32_t mecPayloadDwords = 0u;
    uint32_t i, matches = 0u;
    uint32_t value = 0u;
    uint32_t scratchAfter = 0u;
    uint32_t previousPc = 0u;
    uint32_t cursor = 0u;
    uint32_t rbCntl = 0u;
    uint64_t ringMc = 0ull;
    uint64_t rptrWbMc = 0ull;
    uint64_t wptrPollMc = 0ull;
    uint64_t fenceMc = 0ull;
    uint64_t destMc = 0ull;
    uint32_t destPoll = 0u;
    uint64_t tmrMc = 0ull;
    uint64_t tmrPa = 0ull;
    uint32_t snapshotValid = 0u;
    uint32_t registersTouched = 0u;
    uint32_t memoryPrepared = 0u;
    uint32_t mappedValid = 0u;
    uint32_t allocated = 0u;
    uint32_t tmrAllocated = 0u;
    uint32_t pspRingActive = 0u;

    PHYSICAL_ADDRESS pspLow, pspHigh, pspBoundary;
    PVOID pspRing = NULL, pspCmd = NULL, pspFence = NULL, pspFw = NULL;
    PHYSICAL_ADDRESS pspRingPa = {0}, pspCmdPa = {0}, pspFencePa = {0}, pspFwPa = {0};

    uint32_t origRb0Active = 0u;
    uint32_t origRb0Cntl = 0u;
    uint32_t origRb0Base = 0u;
    uint32_t origRb0BaseHi = 0u;
    uint32_t origRb0Rptr = 0u;
    uint32_t origRb0Wptr = 0u;
    uint32_t origRb0WptrHi = 0u;
    uint32_t origRb0RptrAddr = 0u;
    uint32_t origRb0RptrAddrHi = 0u;
    uint32_t origRbWptrPollAddrLo = 0u;
    uint32_t origRbWptrPollAddrHi = 0u;
    uint32_t origRbVmid = 0u;
    uint32_t origRbWptrDelay = 0u;
    uint32_t origRbWptrPollCntl = 0u;
    uint32_t origRbDoorbellCntl = 0u;
    uint32_t origCpMecCntl = 0u;
    uint32_t origGcvmL2Cntl2 = 0u;
    uint32_t origVgtNumInstances = 0u;
    uint32_t origPaScLineStippleState = 0u;
    uint32_t origCgttCpfClkCtrl = 0u;
    uint32_t origGeFastClks = 0u;
    uint32_t origCpSdCntl = 0u;
    uint32_t origGrbmGfxIndex = 0u;
    uint32_t origGrbmGfxCntl = 0u;
    uint32_t origCpMaxContext = 0u;
    uint32_t origScratchReg0 = 0u;
    uint32_t origCtx0Cntl = 0u;
    uint32_t origCtx0PtBaseLo = 0u;
    uint32_t origCtx0PtBaseHi = 0u;
    uint32_t origCtx0PtStartLo = 0u;
    uint32_t origCtx0PtEndLo = 0u;
    uint64_t firmwarePa = 0ull;

    PVOID pfpFileBuffer = NULL;
    PVOID meFileBuffer = NULL;
    PVOID ceFileBuffer = NULL;
    PVOID mecFileBuffer = NULL;

    const unsigned char *pfpExpected;
    const unsigned char *meExpected;
    const unsigned char *ceExpected;

    if (Context == NULL || Evidence == NULL)
        return BC250_PFP_DIRECT_LOAD_BAD_ARGUMENT;

    RtlZeroMemory(Evidence, sizeof(*Evidence));

    invalidateEngine = Context->GcvmInvalidateEngine;
    if (invalidateEngine > BC250_GCVM_INVALIDATE_ENGINE_MAX) {
        invalidateEngine = BC250_GCVM_INVALIDATE_ENGINE_DEFAULT;
    }
    invalidateUseSemaphore = Context->GcvmInvalidateSemaphore;
    invalidateReq = BC250_GCVM_INVALIDATE_REQ_OF(invalidateEngine);
    Evidence->InvalidateEngine = invalidateEngine;
    Evidence->InvalidateUseSemaphore = invalidateUseSemaphore;
    Evidence->FirmwareBytes = BC250_PFP_PAYLOAD_BYTES_LOCAL;
    Evidence->JumpTableDwords = BC250_PFP_JT_DWORDS;
    Evidence->UcodeVersion = BC250_PFP_UCODE_VERSION;

    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;
    addressSpace = &Context->Core.TopologyEvidence.AddressSpace;
    if (addressSpace->TotalBytes == 0u ||
        addressSpace->VramMcStart == 0u) return BC250_PFP_DIRECT_LOAD_ALLOCATION_FAILED;

    pfpExpected = Bc250GetCpFirmwarePayloadSha256(BC250_PSP_FW_CP_PFP);
    meExpected  = Bc250GetCpFirmwarePayloadSha256(BC250_PSP_FW_CP_ME);
    ceExpected  = Bc250GetCpFirmwarePayloadSha256(BC250_PSP_FW_CP_CE);
    if (pfpExpected == NULL || meExpected == NULL || ceExpected == NULL)
        return BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;

    /* 1. Carrega os quatro firmwares da quadra do CP */
    ntStatus = Bc250LoadFirmwareFile(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_pfp.bin",
        BC250_PFP_FILE_BYTES, BC250_PFP_PAYLOAD_BYTES_LOCAL,
        pfpExpected, &pfpFileBuffer);
    if (!NT_SUCCESS(ntStatus)) return BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;

    ntStatus = Bc250LoadFirmwareFile(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_me.bin",
        BC250_ME_FILE_BYTES, BC250_ME_PAYLOAD_BYTES_LOCAL,
        meExpected, &meFileBuffer);
    if (!NT_SUCCESS(ntStatus)) {
        ExFreePoolWithTag(pfpFileBuffer, BC250_PFP_POOL_TAG);
        return BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;
    }

    ntStatus = Bc250LoadFirmwareFile(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_ce.bin",
        BC250_CE_FILE_BYTES, BC250_CE_PAYLOAD_BYTES_LOCAL,
        ceExpected, &ceFileBuffer);
    if (!NT_SUCCESS(ntStatus)) {
        ExFreePoolWithTag(meFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(pfpFileBuffer, BC250_PFP_POOL_TAG);
        return BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;
    }

    ntStatus = Bc250LoadFirmwareFile(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_mec.bin",
        BC250_MEC_FILE_BYTES, BC250_MEC_PAYLOAD_BYTES_LOCAL,
        NULL, &mecFileBuffer);
    if (!NT_SUCCESS(ntStatus)) {
        ExFreePoolWithTag(ceFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(meFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(pfpFileBuffer, BC250_PFP_POOL_TAG);
        return BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED;
    }

    mappedDwords = BC250_CP_QUAD_ALLOCATION_BYTES / sizeof(uint32_t);
    pfpPayloadDwords = BC250_PFP_PAYLOAD_BYTES_LOCAL / sizeof(uint32_t);
    mePayloadDwords  = BC250_ME_PAYLOAD_BYTES_LOCAL  / sizeof(uint32_t);
    cePayloadDwords  = BC250_CE_PAYLOAD_BYTES_LOCAL  / sizeof(uint32_t);
    mecPayloadDwords = BC250_MEC_PAYLOAD_BYTES_LOCAL / sizeof(uint32_t);

    backup = (uint32_t *)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, (SIZE_T)BC250_CP_QUAD_ALLOCATION_BYTES, BC250_PFP_POOL_TAG);
    if (backup == NULL) {
        ExFreePoolWithTag(mecFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(ceFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(meFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(pfpFileBuffer, BC250_PFP_POOL_TAG);
        return BC250_PFP_DIRECT_LOAD_ALLOCATION_FAILED;
    }

    /*
     * 2a. TMR.1 (doc 658, auditoria 653-TMR): o TMR do PSP e' memoria que o
     * PSP continua usando depois do SETUP_TMR (o CP busca o microcodigo nele),
     * entao fica reservado ate o fim, como o amdgpu (psp_tmr_init/sw_fini;
     * na mesma placa com 256 MB: "reserve 0x400000 from 0xf40f800000 for PSP
     * TMR", doc 552). Ate a 248b este bloco alocava um TMR proprio e o
     * devolvia ao alocador no Cleanup: a faixa voltava ao segmento do VidMm.
     * Primeiro reusa o TMR persistente que o bootstrap do CP ja reservou
     * (papel TMR); sem ele, aloca um persistente. Nunca e' liberado.
     */
    {
        uint32_t r;
        for (r = 0u; r < allocator->RegionCount && r < BC250_VRAM_MAX_REGIONS; ++r) {
            const BC250_VRAM_REGION *cand = &allocator->Regions[r];
            if (cand->InUse != 0u && cand->Role == BC250_VRAM_ROLE_TMR &&
                (cand->Flags & BC250_VRAM_REGION_PERSISTENT) != 0u &&
                cand->Length >= BC250_PSP_TMR_BYTES &&
                (cand->Offset & (BC250_PSP_TMR_BYTES - 1ull)) == 0ull) {
                tmrRegion = *cand;
                tmrAllocated = 2u; /* do bootstrap: nao e' nosso */
                break;
            }
        }
    }
    if (tmrAllocated == 0u) {
        vramStatus = Bc250VramAllocate(allocator, BC250_PSP_TMR_BYTES,
            BC250_PSP_TMR_BYTES, BC250_VRAM_ROLE_TMR,
            BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_REGION_PERSISTENT, &tmrToken);
        if (vramStatus == BC250_VRAM_OK &&
            Bc250VramGetRegion(allocator, tmrToken, &tmrRegion) == BC250_VRAM_OK) {
            tmrAllocated = 1u;
        }
    }
    if (tmrAllocated == 0u) {
        Bc250DisplayLog("[TMR1] RECUSADO: sem TMR persistente nem como alocar 4 MiB; sem fallback (o endereco fixo antigo cai fora da VRAM com 256 MB)\n");
        ExFreePoolWithTag(mecFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(ceFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(meFileBuffer, BC250_PFP_POOL_TAG);
        ExFreePoolWithTag(pfpFileBuffer, BC250_PFP_POOL_TAG);
        return BC250_PFP_DIRECT_LOAD_ALLOCATION_FAILED;
    }

    /* 2b. Reserva de regiao contigua de 1.1 MiB para Ring 0 em VRAM owned */
    vramStatus = Bc250VramAllocate(allocator, BC250_CP_QUAD_ALLOCATION_BYTES,
        PAGE_SIZE, BC250_VRAM_ROLE_FIRMWARE,
        BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE |
            BC250_VRAM_REGION_PERSISTENT, &token);
    if (vramStatus != BC250_VRAM_OK) {
        status = BC250_PFP_DIRECT_LOAD_ALLOCATION_FAILED;
        goto Cleanup;
    }
    allocated = 1u;
    if (Bc250VramGetRegion(allocator, token, &region) != BC250_VRAM_OK ||
        region.Length != BC250_CP_QUAD_ALLOCATION_BYTES) {
        status = BC250_PFP_DIRECT_LOAD_REGION_FAILED;
        goto Cleanup;
    }
    for (i = 0u; i < Context->Core.ResolvedBarCount; ++i) {
        if (Context->Core.ResolvedBars[i].Index ==
            BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            bar = &Context->Core.ResolvedBars[i];
            ++matches;
        }
    }
    if (matches != 1u || bar == NULL ||
        bar->Length < BC250_CP_QUAD_ALLOCATION_BYTES ||
        region.Offset > bar->Length - BC250_CP_QUAD_ALLOCATION_BYTES ||
        addressSpace->VramMcStart > MAXULONGLONG - region.Offset) {
        status = BC250_PFP_DIRECT_LOAD_APERTURE_INVALID;
        goto Cleanup;
    }

    Evidence->FirmwareOffset = region.Offset;
    Evidence->FirmwareMcAddress = addressSpace->VramMcStart + region.Offset;
    firmwarePa = bar->Start + region.Offset;

    /*
     * TMR.1: o PSP recebe a PA da DRAM (GCMC_VM_FB_OFFSET<<24 + offset), como
     * amdgpu_gmc_vram_pa no psp_tmr_load. Ate a 248b ia a PA da BAR PCI
     * (bar->Start + offset). Sem a base lida da placa, recusa.
     */
    if (addressSpace->VramBaseOffset == 0ull) {
        Bc250DisplayLog("[TMR1] RECUSADO: base PA da VRAM (GCMC_VM_FB_OFFSET<<24) nao lida; nao se adivinha\n");
        status = BC250_PFP_DIRECT_LOAD_ALLOCATION_FAILED;
        goto Cleanup;
    }
    tmrMc = addressSpace->VramMcStart + tmrRegion.Offset;
    tmrPa = addressSpace->VramBaseOffset + tmrRegion.Offset;
    Bc250DisplayLog("[TMR1] TMR persistente: VRAM [0x%llX,+0x%llX) origem=%s MC=0x%llX PA_DRAM=0x%llX (PA da BAR, como ate a 248b, seria 0x%llX); quad do CP persistente em [0x%llX,+0x%llX)\n",
        (unsigned long long)tmrRegion.Offset, (unsigned long long)tmrRegion.Length,
        tmrAllocated == 2u ? "bootstrap" : "alocado aqui",
        (unsigned long long)tmrMc, (unsigned long long)tmrPa,
        (unsigned long long)(bar->Start + tmrRegion.Offset),
        (unsigned long long)region.Offset, (unsigned long long)region.Length);

    physical.QuadPart = (LONGLONG)(bar->Start + region.Offset);
    ntStatus = Bc250VramCpuMapMemory(&Context->Core.DxgkInterface,
        (ULONGLONG)physical.QuadPart, BC250_CP_QUAD_ALLOCATION_BYTES,
        MmNonCached, (PVOID *)&mapped);
    if (!NT_SUCCESS(ntStatus) || mapped == NULL) {
        status = BC250_PFP_DIRECT_LOAD_MAP_FAILED;
        goto Cleanup;
    }
    mappedValid = 1u;
    Bc250LogUcVramMapping("PFP_DIRECT_LOAD", region.Offset,
        BC250_CP_QUAD_ALLOCATION_BYTES);
    Bc250PfpCopyFromDevice(backup, mapped, mappedDwords);

    /* 3. Mapeamento MMIO e Snapshot */
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        status = BC250_PFP_DIRECT_LOAD_MMIO_FAILED;
        goto Cleanup;
    }

    (void)Bc250PfpWrite(Context, R_GRBM_GFX_INDEX, 0u);
    (void)Bc250PfpWrite(Context, R_GRBM_GFX_CNTL, 0u);

    if (!Bc250PfpRead(Context, R_CP_ME_CNTL, &Evidence->CpMeCntlBefore) ||
        !Bc250PfpRead(Context, R_CP_MEC_CNTL, &origCpMecCntl) ||
        !Bc250PfpRead(Context, R_RLC_CNTL, &Evidence->RlcCntlBefore) ||
        !Bc250PfpRead(Context, R_CP_PFP_PC, &Evidence->PfpPcBefore) ||
        !Bc250PfpRead(Context, R_CP_ME_PC, &Evidence->MePcBefore) ||
        !Bc250PfpRead(Context, R_CP_CE_PC, &Evidence->CePcBefore) ||
        !Bc250PfpRead(Context, R_CP_RB0_ACTIVE, &origRb0Active) ||
        !Bc250PfpRead(Context, R_CP_RB0_CNTL, &origRb0Cntl) ||
        !Bc250PfpRead(Context, R_CP_RB0_BASE, &origRb0Base) ||
        !Bc250PfpRead(Context, R_CP_RB0_BASE_HI, &origRb0BaseHi) ||
        !Bc250PfpRead(Context, R_CP_RB0_RPTR, &origRb0Rptr) ||
        !Bc250PfpRead(Context, R_CP_RB0_WPTR, &origRb0Wptr) ||
        !Bc250PfpRead(Context, R_CP_RB0_WPTR_HI, &origRb0WptrHi) ||
        !Bc250PfpRead(Context, R_CP_RB0_RPTR_ADDR, &origRb0RptrAddr) ||
        !Bc250PfpRead(Context, R_CP_RB0_RPTR_ADDR_HI, &origRb0RptrAddrHi) ||
        !Bc250PfpRead(Context, R_CP_RB_WPTR_POLL_ADDR_LO, &origRbWptrPollAddrLo) ||
        !Bc250PfpRead(Context, R_CP_RB_WPTR_POLL_ADDR_HI, &origRbWptrPollAddrHi) ||
        !Bc250PfpRead(Context, R_CP_RB_VMID, &origRbVmid) ||
        !Bc250PfpRead(Context, R_CP_RB_WPTR_DELAY, &origRbWptrDelay) ||
        !Bc250PfpRead(Context, R_CP_RB_WPTR_POLL_CNTL, &origRbWptrPollCntl) ||
        !Bc250PfpRead(Context, R_CP_RB_DOORBELL_CONTROL, &origRbDoorbellCntl) ||
        !Bc250PfpRead(Context, R_GCVM_L2_CNTL2, &origGcvmL2Cntl2) ||
        !Bc250PfpRead(Context, R_VGT_NUM_INSTANCES, &origVgtNumInstances) ||
        !Bc250PfpRead(Context, R_PA_SC_LINE_STIPPLE_STATE, &origPaScLineStippleState) ||
        !Bc250PfpRead(Context, R_CGTT_CPF_CLK_CTRL, &origCgttCpfClkCtrl) ||
        !Bc250PfpRead(Context, R_GE_FAST_CLKS, &origGeFastClks) ||
        !Bc250PfpRead(Context, R_CP_SD_CNTL, &origCpSdCntl) ||
        !Bc250PfpRead(Context, R_GRBM_GFX_INDEX, &origGrbmGfxIndex) ||
        !Bc250PfpRead(Context, R_GRBM_GFX_CNTL, &origGrbmGfxCntl) ||
        !Bc250PfpRead(Context, R_CP_MAX_CONTEXT, &origCpMaxContext) ||
        !Bc250PfpRead(Context, R_CP_SCRATCH_REG0, &origScratchReg0) ||
        !Bc250PfpRead(Context, R_GCVM_CONTEXT0_CNTL, &origCtx0Cntl) ||
        !Bc250PfpRead(Context, R_GCVM_CONTEXT0_PT_BASE_LO, &origCtx0PtBaseLo) ||
        !Bc250PfpRead(Context, R_GCVM_CONTEXT0_PT_BASE_HI, &origCtx0PtBaseHi) ||
        !Bc250PfpRead(Context, R_GCVM_CONTEXT0_PT_START_LO, &origCtx0PtStartLo) ||
        !Bc250PfpRead(Context, R_GCVM_CONTEXT0_PT_END_LO, &origCtx0PtEndLo) ||
        !Bc250PfpRead(Context, R_FAULT_STATUS, &Evidence->FaultStatusBefore)) {
        status = BC250_PFP_DIRECT_LOAD_MMIO_FAILED;
        goto UnmapMmio;
    }
    Evidence->Rb0ActiveBefore = origRb0Active;
    Evidence->Rb0RptrBefore = origRb0Rptr;

    snapshotValid = 1u;
    /* Coloca os microengines em HALT e invalida os caches de instrução para evitar loop residual (F4d) */
    (void)Bc250PfpWrite(Context, R_CP_ME_CNTL, (BC250_PFP_HALT | BC250_ME_HALT | BC250_CE_HALT));
    (void)Bc250PfpWrite(Context, R_CP_MEC_CNTL, 0x00000003u);
    KeMemoryBarrier();
    KeStallExecutionProcessor(50u);

    /* Aqui so' invalida. Primar a cache antes do staging encheria a I-Cache com
     * o conteudo antigo; o PRIME correto acontece depois, no sitio 1b. */
    Evidence->IcacheInvalidateEntryMask = Bc250PfpIcacheOpAll(
        Context, BC250_IC_OP_INVALIDATE_CACHE, BC250_IC_OP_INVALIDATE_CACHE_COMPLETE);

    /* 4a. Staging dos 4 payloads na VRAM para conferência */
    pfpMapped = mapped + (BC250_CP_PFP_SLICE_OFFSET / sizeof(uint32_t));
    meMapped  = mapped + (BC250_CP_ME_SLICE_OFFSET  / sizeof(uint32_t));
    ceMapped  = mapped + (BC250_CP_CE_SLICE_OFFSET  / sizeof(uint32_t));
    mecMapped = mapped + (BC250_CP_MEC_SLICE_OFFSET / sizeof(uint32_t));

    Bc250PfpCopyToDevice(pfpMapped,
        (const uint32_t *)((PUCHAR)pfpFileBuffer + 0x100u), pfpPayloadDwords);
    Bc250PfpCopyToDevice(meMapped,
        (const uint32_t *)((PUCHAR)meFileBuffer + 0x100u), mePayloadDwords);
    Bc250PfpCopyToDevice(ceMapped,
        (const uint32_t *)((PUCHAR)ceFileBuffer + 0x100u), cePayloadDwords);
    Bc250PfpCopyToDevice(mecMapped,
        (const uint32_t *)((PUCHAR)mecFileBuffer + 0x100u), mecPayloadDwords);
    KeMemoryBarrier();
    memoryPrepared = 1u;
    Evidence->StagedDwords = pfpPayloadDwords;

    for (i = 0u; i < pfpPayloadDwords; ++i) {
        if (READ_REGISTER_ULONG((volatile ULONG *)&pfpMapped[i]) !=
            ((uint32_t *)((PUCHAR)pfpFileBuffer + 0x100u))[i])
            ++Evidence->StagingMismatches;
    }
    for (i = 0u; i < mePayloadDwords; ++i) {
        if (READ_REGISTER_ULONG((volatile ULONG *)&meMapped[i]) !=
            ((uint32_t *)((PUCHAR)meFileBuffer + 0x100u))[i])
            ++Evidence->StagingMismatches;
    }
    for (i = 0u; i < cePayloadDwords; ++i) {
        if (READ_REGISTER_ULONG((volatile ULONG *)&ceMapped[i]) !=
            ((uint32_t *)((PUCHAR)ceFileBuffer + 0x100u))[i])
            ++Evidence->StagingMismatches;
    }
    for (i = 0u; i < mecPayloadDwords; ++i) {
        if (READ_REGISTER_ULONG((volatile ULONG *)&mecMapped[i]) !=
            ((uint32_t *)((PUCHAR)mecFileBuffer + 0x100u))[i])
            ++Evidence->StagingMismatches;
    }
    if (Evidence->StagingMismatches != 0u) {
        status = BC250_PFP_DIRECT_LOAD_STAGING_FAILED;
        goto Restore;
    }

    /* 4b. SESSAO UNIFICADA DO PSP: SETUP_TMR + 5x LOAD_IP_FW (MANTIDA ATIVA) */
    {
        uint32_t pspRingWptr = 0u;
        uint32_t pspFenceValue = 0u;
        uint32_t pspAck = 0u;
        uint32_t cpPspLoadedCount = 0u;
        uint32_t tmrFenceOk = 0u;
        uint32_t tmrResp = 0xFFFFFFFFu;
        uint32_t loadResp[5] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
        uint32_t loadFenceOk[5] = { 0u, 0u, 0u, 0u, 0u };
        volatile uint32_t *pRingBuf;
        uint32_t *pCmdBuf;
        volatile uint32_t *pFenceBuf;
        uint32_t pollIndex;
        uint32_t k;

        const uint32_t fwTypes[5] = {
            BC250_PSP_LOAD_FW_CP_ME_TYPE,
            BC250_PSP_LOAD_FW_CP_PFP_TYPE,
            BC250_PSP_LOAD_FW_CP_CE_TYPE,
            BC250_PSP_LOAD_FW_CP_MEC_TYPE,
            BC250_PSP_LOAD_FW_CP_MEC_JT1_TYPE
        };
        const uint32_t fwBytes[5] = {
            BC250_ME_PAYLOAD_BYTES_LOCAL,
            BC250_PFP_PAYLOAD_BYTES_LOCAL,
            BC250_CE_PAYLOAD_BYTES_LOCAL,
            BC250_CP_MEC_PAYLOAD_BYTES,
            BC250_CP_MEC_JT1_PAYLOAD_BYTES
        };
        const void *fwSources[5] = {
            (const PUCHAR)meFileBuffer + 0x100u,
            (const PUCHAR)pfpFileBuffer + 0x100u,
            (const PUCHAR)ceFileBuffer + 0x100u,
            (const PUCHAR)mecFileBuffer + 0x100u,
            (const PUCHAR)mecFileBuffer + 0x100u + BC250_CP_MEC_PAYLOAD_BYTES
        };

        pspLow.QuadPart = 0;
        pspHigh.QuadPart = 0xFFFFFFFFFFull;
        pspBoundary.QuadPart = 0;
        pspRing = MmAllocateContiguousMemorySpecifyCache(
            BC250_PSP_RING_BYTES, pspLow, pspHigh, pspBoundary, MmNonCached);
        pspCmd = MmAllocateContiguousMemorySpecifyCache(
            BC250_PSP_LOAD_FW_COMMAND_BYTES, pspLow, pspHigh, pspBoundary, MmNonCached);
        pspFence = MmAllocateContiguousMemorySpecifyCache(
            PAGE_SIZE, pspLow, pspHigh, pspBoundary, MmNonCached);
        pspFw = MmAllocateContiguousMemorySpecifyCache(
            270336u, pspLow, pspHigh, pspBoundary, MmNonCached);

        if (pspRing != NULL && pspCmd != NULL && pspFence != NULL && pspFw != NULL) {
            pspRingPa = MmGetPhysicalAddress(pspRing);
            pspCmdPa  = MmGetPhysicalAddress(pspCmd);
            pspFencePa = MmGetPhysicalAddress(pspFence);
            pspFwPa   = MmGetPhysicalAddress(pspFw);
            pRingBuf  = (volatile uint32_t *)pspRing;
            pCmdBuf   = (uint32_t *)pspCmd;
            pFenceBuf = (volatile uint32_t *)pspFence;

            RtlZeroMemory(pspRing, BC250_PSP_RING_BYTES);
            RtlZeroMemory(pspCmd, BC250_PSP_LOAD_FW_COMMAND_BYTES);
            *pFenceBuf = 0u;

            /* 1. Criar anel GPCOM */
            (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_69, (uint32_t)pspRingPa.QuadPart);
            (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_70, (uint32_t)(pspRingPa.QuadPart >> 32));
            (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_71, BC250_PSP_RING_BYTES);
            (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_64, BC250_PSP_RING_CREATE_KM);
            KeStallExecutionProcessor(20000u);

            for (pollIndex = 0u; pollIndex < 500u; ++pollIndex) {
                if (Bc250PfpRead(Context, BC250_PSP_RING_C2PMSG_64, &pspAck) &&
                    (pspAck & BC250_PSP_RING_ACK_MASK) == BC250_PSP_RING_ACK_VALUE) break;
                KeStallExecutionProcessor(1000u);
            }

            if ((pspAck & BC250_PSP_RING_ACK_MASK) == BC250_PSP_RING_ACK_VALUE) {
                pspRingActive = 1u;

                /* 2. Submeter SETUP_TMR (ID = 5, 4 MiB) */
                RtlZeroMemory(pCmdBuf, BC250_PSP_LOAD_FW_COMMAND_BYTES);
                pCmdBuf[0] = 0x400u;
                pCmdBuf[1] = 1u;
                pCmdBuf[2] = 5u; /* GFX_CMD_ID_SETUP_TMR */
                pCmdBuf[7] = (uint32_t)(tmrMc & 0xFFFFFFFFull);
                pCmdBuf[8] = (uint32_t)(tmrMc >> 32);
                pCmdBuf[9] = BC250_PSP_TMR_BYTES;
                pCmdBuf[10] = 2u; /* flags: virt_phy_addr=1 */
                pCmdBuf[11] = (uint32_t)(tmrPa & 0xFFFFFFFFull);
                pCmdBuf[12] = (uint32_t)(tmrPa >> 32);

                ++pspFenceValue;
                *pFenceBuf = 0u;
                pRingBuf[pspRingWptr + 0] = (uint32_t)pspCmdPa.QuadPart;
                pRingBuf[pspRingWptr + 1] = (uint32_t)(pspCmdPa.QuadPart >> 32);
                pRingBuf[pspRingWptr + 2] = 0x400u;
                pRingBuf[pspRingWptr + 3] = (uint32_t)pspFencePa.QuadPart;
                pRingBuf[pspRingWptr + 4] = (uint32_t)(pspFencePa.QuadPart >> 32);
                pRingBuf[pspRingWptr + 5] = pspFenceValue;
                KeMemoryBarrier();

                pspRingWptr = (pspRingWptr + 16u) % (BC250_PSP_RING_BYTES / sizeof(uint32_t));
                (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_67, pspRingWptr);

                for (pollIndex = 0u; pollIndex < 500u; ++pollIndex) {
                    if (*pFenceBuf == pspFenceValue) break;
                    KeStallExecutionProcessor(1000u);
                }

                tmrFenceOk = (*pFenceBuf == pspFenceValue) ? 1u : 0u;
                tmrResp = pCmdBuf[864u / 4u];
                if (*pFenceBuf == pspFenceValue && pCmdBuf[864u / 4u] == 0u) {
                    ++cpPspLoadedCount;

                    /* 3. Submeter sucessivamente LOAD_IP_FW para os 5 firmwares do CP */
                    for (k = 0u; k < 5u; ++k) {
                        KeStallExecutionProcessor(10000u);
                        RtlCopyMemory(pspFw, fwSources[k], fwBytes[k]);
                        KeMemoryBarrier();

                        RtlZeroMemory(pCmdBuf, BC250_PSP_LOAD_FW_COMMAND_BYTES);
                        pCmdBuf[0] = 0x400u;
                        pCmdBuf[1] = 1u;
                        pCmdBuf[2] = 6u; /* GFX_CMD_ID_LOAD_IP_FW */
                        pCmdBuf[7] = (uint32_t)(pspFwPa.QuadPart & 0xFFFFFFFFull);
                        pCmdBuf[8] = (uint32_t)(pspFwPa.QuadPart >> 32);
                        pCmdBuf[9] = fwBytes[k];
                        pCmdBuf[10] = fwTypes[k];

                        ++pspFenceValue;
                        *pFenceBuf = 0u;
                        pRingBuf[pspRingWptr + 0] = (uint32_t)pspCmdPa.QuadPart;
                        pRingBuf[pspRingWptr + 1] = (uint32_t)(pspCmdPa.QuadPart >> 32);
                        pRingBuf[pspRingWptr + 2] = 0x400u;
                        pRingBuf[pspRingWptr + 3] = (uint32_t)pspFencePa.QuadPart;
                        pRingBuf[pspRingWptr + 4] = (uint32_t)(pspFencePa.QuadPart >> 32);
                        pRingBuf[pspRingWptr + 5] = pspFenceValue;
                        KeMemoryBarrier();

                        pspRingWptr = (pspRingWptr + 16u) % (BC250_PSP_RING_BYTES / sizeof(uint32_t));
                        (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_67, pspRingWptr);

                        for (pollIndex = 0u; pollIndex < 500u; ++pollIndex) {
                            if (*pFenceBuf == pspFenceValue) break;
                            KeStallExecutionProcessor(1000u);
                        }

                        loadFenceOk[k] = (*pFenceBuf == pspFenceValue) ? 1u : 0u;
                        loadResp[k] = pCmdBuf[864u / 4u];
                        if (*pFenceBuf == pspFenceValue && pCmdBuf[864u / 4u] == 0u) {
                            ++cpPspLoadedCount;
                        }
                    }
                }
            }
        }

        Evidence->FaultAddressLow = cpPspLoadedCount;
        /* TMR.1: a contagem acima e' sobrescrita pelo scratch mais adiante;
         * as respostas do PSP ficam aqui, em linha propria. */
        Bc250DisplayLog("[TMR1] PSP: anel=%u SETUP_TMR fence=%u resp=0x%08X | LOAD_IP_FW ME %u/0x%08X PFP %u/0x%08X CE %u/0x%08X MEC %u/0x%08X MEC_JT1 %u/0x%08X | aceitos=%u/6\n",
            pspRingActive, tmrFenceOk, tmrResp,
            loadFenceOk[0], loadResp[0], loadFenceOk[1], loadResp[1],
            loadFenceOk[2], loadResp[2], loadFenceOk[3], loadResp[3],
            loadFenceOk[4], loadResp[4], cpPspLoadedCount);
    }

    /* Slice 5: Ring buffer, RPTR writeback, WPTR poll e Fence de silicio */
    pRing = mapped + (BC250_CP_RB0_RING_OFFSET / sizeof(uint32_t));
    pRptrWb = mapped + (BC250_CP_RB0_RPTR_WB_OFFSET / sizeof(uint32_t));
    pWptrPoll = mapped + (BC250_CP_RB0_WPTR_POLL_OFFSET / sizeof(uint32_t));
    pFence = mapped + (BC250_CP_RB0_FENCE_OFFSET / sizeof(uint32_t));
    pDest = mapped + (BC250_CP_RB0_DEST_OFFSET / sizeof(uint32_t));

    RtlZeroMemory((void *)pRing, BC250_CP_RB0_RING_BYTES);
    *pRptrWb = 0u;
    *pWptrPoll = 0u;
    *pFence = BC250_CP_RB0_FENCE_POISON;
    Evidence->Rb0FenceBefore = *pFence;
    /* Envenena o destino: sem veneno, um buffer zerado que "bate" nao prova nada. */
    for (i = 0u; i < BC250_CP_RB0_DEST_DWORDS; ++i)
        pDest[i] = BC250_CP_RB0_DEST_POISON + i;
    KeMemoryBarrier();

    /* Enderecos FISICOS DE MEMORIA (MC) para Ring 0, RPTR WB, WPTR Poll e Fence */
    ringMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_RING_OFFSET;
    rptrWbMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_RPTR_WB_OFFSET;
    wptrPollMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_WPTR_POLL_OFFSET;
    fenceMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_FENCE_OFFSET;
    destMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_DEST_OFFSET;

    /* Sequencia PM4 canonica: WRITE_DATA SCRATCH + WRITE_DATA FENCE MC + WRITE_DATA FENCE GPUVA */
    cursor = 0u;
    {
        uint64_t gpuvaFence = BC250_CP_RB0_FENCE_OFFSET;
        (void)gpuvaFence;

        /* ORDEM INVERTIDA DE PROPOSITO.
         *
         * Com o destino no fim: RPTR=36/36 (anel inteiro consumido), fence
         * gravada, e o destino permanecendo com o veneno apos 1000 polls — o
         * mesmo formato de pacote que funciona para a fence nao escreve aqui.
         * Sobram duas explicacoes: o endereco do destino, ou o CP parar de
         * EXECUTAR depois de alguns pacotes enquanto o RPTR continua avancando.
         *
         * Pondo o destino PRIMEIRO, as duas se separam:
         *   destino grava e fence nao  -> o CP so' executa os primeiros pacotes
         *   destino nao grava e fence grava -> o problema e' o endereco
         *   os dois gravam -> era ordem/tempo
         */
        /* 4. Gabarito no destino: QUATRO WRITE_DATA de um dword cada.
         *
         * A primeira tentativa usava um unico WRITE_DATA com payload de 4 dwords
         * (COUNT=6). O CP consumiu o anel inteiro — RPTR chegou a 24/24 — e
         * mesmo assim o destino permaneceu com o veneno 0xDEADC000 apos 1000
         * polls com flush de HDP: o pacote foi aceito e a escrita nao aconteceu.
         *
         * Aqui o formato e' exatamente o dos tres pacotes que ja' funcionam
         * nesta mesma sequencia (5 dwords, um dado). Repetir a forma provada
         * elimina a variavel do payload multiplo em vez de supor por que ela
         * falha. */
        for (i = 0u; i < BC250_CP_RB0_DEST_DWORDS; ++i) {
            uint64_t slotMc = destMc + (i * sizeof(uint32_t));
            pRing[cursor++] = 0xC0033700u; /* PACKET3_WRITE_DATA, count=3 */
            pRing[cursor++] = 0x00100500u; /* WR_CONFIRM=1, DST_SEL=5 (Memory) */
            pRing[cursor++] = (uint32_t)(slotMc & 0xFFFFFFFFull);
            pRing[cursor++] = (uint32_t)(slotMc >> 32);
            pRing[cursor++] = BC250_CP_RB0_DEST_PATTERN + i;
        }

        /* 1. WRITE_DATA: SCRATCH_REG0 (0x2040) = 0x22222222 (5 dwords) */
        pRing[cursor++] = 0xC0033700u; /* PACKET3_WRITE_DATA, count=3 */
        pRing[cursor++] = 0x00100000u; /* WR_CONFIRM=1, DST_SEL=0 (Register) */
        pRing[cursor++] = 0x00002040u; /* mmSCRATCH_REG0 */
        pRing[cursor++] = 0x00000000u;
        pRing[cursor++] = 0x22222222u;

        /* 2. WRITE_DATA: fence via MC physical aperture (5 dwords) */
        pRing[cursor++] = 0xC0033700u; /* PACKET3_WRITE_DATA, count=3 */
        pRing[cursor++] = 0x00100500u; /* WR_CONFIRM=1, DST_SEL=5 (Memory direct) */
        pRing[cursor++] = (uint32_t)(fenceMc & 0xFFFFFFFFull);
        pRing[cursor++] = (uint32_t)(fenceMc >> 32);
        pRing[cursor++] = BC250_CP_RB0_FENCE_VALUE;

        /* 3. WRITE_DATA: fence via GPUVA — SOMENTE com a page table do VMID 0.
         *
         * Este pacote endereca gpuvaFence como VIRTUAL. Sem CONTEXT0 programado
         * (Vmid0PageTable=0, que e' o padrao e o modo comprovadamente correto),
         * o VMID 0 nao tem page table nossa e a traducao falha.
         *
         * Evidencia de 29/08 20:01 vs 20:04: com este pacote ANTES dos writes de
         * destino, o destino nunca era escrito, apesar de RPTR=36/36 — o CP
         * busca o anel inteiro mas para de EXECUTAR depois do pacote que falha.
         * Movendo o destino para antes dele, os dados passaram a bater (tripé
         * completo 3/3). Emitir um pacote que falha por desenho e' desperdicio e
         * arrasta tudo que vier depois, entao ele so' entra quando faz sentido. */
        if (Context->CpVmid0PageTable != 0u) {
            pRing[cursor++] = 0xC0033700u; /* PACKET3_WRITE_DATA, count=3 */
            pRing[cursor++] = 0x00100500u; /* WR_CONFIRM=1, DST_SEL=5 (Memory) */
            pRing[cursor++] = (uint32_t)(gpuvaFence & 0xFFFFFFFFull);
            pRing[cursor++] = (uint32_t)(gpuvaFence >> 32);
            pRing[cursor++] = BC250_CP_RB0_FENCE_VALUE;
        }

        /* 4. Pad 1 NOP para alinhar a 16 dwords (64 bytes / cacheline) */
        /* Padding de UM dword.
         *
         * O valor anterior aqui era 0xC0001000 — um PACKET3 NOP. Num cabeçalho
         * type-3 o campo COUNT (bits 29:16) vale n-2, então COUNT=0 declara um
         * pacote de DOIS dwords. Só escrevíamos um. O CP se recusa, com razão, a
         * consumir um pacote incompleto e estaciona o RPTR no dword anterior:
         * é a origem determinística do RPTR=15 contra WPTR=16 observado em 40/40
         * ciclos, com a fence física correta e os três WRITE_DATA executados.
         *
         * PACKET2 (type 2, 0x80000000) é o NOP de um dword — a forma canônica de
         * alinhar sem declarar payload que não existe. */
        pRing[cursor++] = 0x80000000u; /* PACKET2 NOP, 1 dword */
    }
    KeMemoryBarrier();

    /* 5. Inicializacao canônica seguindo exatamente o driver Linux */
    registersTouched = 1u;
    Evidence->WriteOnlyPortsProgrammed = 1u;
    Evidence->RlcCntlApplied = Evidence->RlcCntlBefore | 1u;
    Evidence->CpMeCntlApplied = Evidence->CpMeCntlBefore & ~(BC250_PFP_HALT | BC250_ME_HALT | BC250_CE_HALT);

    /* Desgatar clocks */
    {
        uint32_t cpfClk = (origCgttCpfClkCtrl & ~0xFCFF8FFFu) | (0xF8000100u & 0xFCFF8FFFu);
        uint32_t geFast = (origGeFastClks & ~0x3FFFFFFFu) | (0x0000493Eu & 0x3FFFFFFFu);
        uint32_t cpSd = (origCpSdCntl & ~0x800007FFu) | (0x000005FFu & 0x800007FFu);
        (void)Bc250PfpWrite(Context, R_CGTT_CPF_CLK_CTRL, cpfClk);
        (void)Bc250PfpWrite(Context, R_GE_FAST_CLKS, geFast);
        (void)Bc250PfpWrite(Context, R_CP_SD_CNTL, cpSd);
    }

    /* Broadcast ativo para todos os SEs, SHs e instâncias para portas indexadas do CP */
    (void)Bc250PfpWrite(Context, R_GRBM_GFX_INDEX, 0xE0000000u);
    (void)Bc250PfpWrite(Context, R_GRBM_GFX_CNTL, 0u);

    /* Flush HDP e L2 */
    (void)Bc250PfpWrite(Context, R_HDP_READ_CACHE_INVALIDATE, 1u);
    (void)Bc250PfpWrite(Context, R_GCVM_L2_CNTL2, 0x00000003u);
    /* O ACK desta invalidacao nunca foi registrado. O commit 7e926c0 trocou a
     * engine 0 pela 17 sob a hipotese "qualquer engine != 0 serve", declarada
     * NAO VALIDADA EM HARDWARE no proprio commit, e previu explicitamente que um
     * ENG que nao responda faz o poll esgotar e o fluxo seguir SEM invalidacao.
     * Sem esta evidencia nao se distingue "invalidou" de "esgotou em silencio". */
    Evidence->InvalidateAckStaging = Bc250PfpGcvmInvalidate(
        Context, invalidateEngine, 0x00000001u, invalidateUseSemaphore,
        &Evidence->InvalidateAckBeforeStaging,
        &Evidence->InvalidateAckRawStaging,
        &Evidence->InvalidatePollsStaging,
        &Evidence->InvalidateSemAcquiredStaging);

    /* RLC e CP setup */
    (void)Bc250PfpWrite(Context, R_RLC_CNTL, Evidence->RlcCntlApplied);
    (void)Bc250PfpWrite(Context, R_CP_MAX_CONTEXT, 7u);
    (void)Bc250PfpWrite(Context, R_CP_DEVICE_ID, 1u);

    /* Prepara Page Table VMID 0 na VRAM: mapeia 2 MiB (512 PTEs) comecando em GPUVA 0 com Host Physical Address */
    {
        volatile uint64_t *pPt = (volatile uint64_t *)(mapped + (BC250_CP_PT_SLICE_OFFSET / sizeof(uint32_t)));
        uint32_t pteIdx;
        for (pteIdx = 0u; pteIdx < 512u; ++pteIdx) {
            uint64_t pagePhys = firmwarePa + (pteIdx * 4096ull);
            pPt[pteIdx] = (pagePhys & 0x0000FFFFFFFFF000ull) | 0xE1ull;
        }
        KeMemoryBarrier();
    }

    /* 4b-2. Ativação de GCVM Context 0 (VMID 0).
     *
     * Este bloco existia em 4df3484 — o estado sob o qual as 109 capturas de
     * sucesso foram produzidas — e foi REMOVIDO junto com o soft reset do CP no
     * trabalho de preservação do display. Desde a remoção não houve um único
     * sucesso do CP.
     *
     * O anel é programado com endereço físico de MC e CP_RB_VMID = 0. Sem uma
     * page table válida no contexto 0, o CP não traduz o que precisa buscar.
     *
     * MEDIDO EM 2026-08-29 18:42, E O RESULTADO INVERTE A HIPÓTESE ACIMA:
     * com Vmid0PageTable=0 e CpSoftReset=1 o CP assenta em 0x284 e fecha com
     * fence física. **Este bloco não é necessário.** Dos dois blocos removidos
     * em 4df3484, só o soft reset do CP fazia falta.
     *
     * E ele é ativamente nocivo: ligá-lo produziu XNACK na UTCL1 do SDMA
     * (SdmaRingUtcl1WriteXnack0=1, ReadXnack0=0x0045FA00, ErrorLog=0xF) e
     * timeout do anel do SDMA, que por sua vez bloqueia o carregador do CP. O
     * SDMA endereça o próprio anel por VMID 0 com endereço físico de MC
     * (0xF40FBB9000); a page table aqui mapeia 2 MiB a partir de firmwarePa, e
     * todo acesso VMID 0 fora dessa janela falha a tradução.
     *
     * Mantido atrás do tunable, default 0, apenas para reprodução do ensaio.
     * NÃO LIGAR sem antes resolver a cobertura do VMID 0.
     * Todos os cinco registradores CONTEXT0_* são restaurados no rótulo Restore. */
    (void)Bc250PfpRead(Context, R_GCVM_CONTEXT0_CNTL, &value);
    if (Context->CpVmid0PageTable != 0u) {
        uint64_t ptPa = firmwarePa + BC250_CP_PT_SLICE_OFFSET;
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_BASE_LO, (uint32_t)(ptPa | 1u));
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_BASE_HI, (uint32_t)(ptPa >> 32));
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_START_LO, 0u);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_END_LO, 511u);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_CNTL, 0x007FFE01u);
        Evidence->Vmid0PageTableApplied = 1u;
        /* Invalidação da TLB do VMID 0, com a engine e o protocolo correntes. */
        Evidence->Vmid0InvalidateAck = Bc250PfpGcvmInvalidate(
            Context, invalidateEngine, 0x00F80001u, invalidateUseSemaphore,
            &Evidence->Vmid0AckBefore, &Evidence->Vmid0AckAfter,
            &Evidence->Vmid0Polls, &Evidence->Vmid0SemAcquired);
    }

    /* 4c. Inicialização pós-PSP canônica (Linux gfx_v10_0) sem GRBM soft reset */
    {
        uint32_t cpStat = 0u, grbmStat = 0u, grbmStat2 = 0u;

        /* Soft reset do CP (GRBM_SOFT_RESET bit 0 = SOFT_RESET_CP).
         *
         * Também presente em 4df3484 e removido no mesmo trabalho. Um PFP que
         * é destravado sem passar por reset pode assentar num laço de espera —
         * que é exatamente a assinatura 0x218..0x21B observada desde então.
         *
         * Atrás de CpSoftReset (default 0): resetar o CP com o display servido
         * pelo mesmo dispositivo é a segunda operação proibida do arranque. */
        if (Context->CpSoftReset != 0u) {
            (void)Bc250PfpWrite(Context, R_GRBM_SOFT_RESET, 1u);
            KeStallExecutionProcessor(50u);
            (void)Bc250PfpWrite(Context, R_GRBM_SOFT_RESET, 0u);
            KeStallExecutionProcessor(50u);
            Evidence->CpSoftResetApplied = 1u;
        }

        /* Inicialização canônica pós-PSP */
        (void)Bc250PfpWrite(Context, R_CP_MAX_CONTEXT, 7u);
        (void)Bc250PfpWrite(Context, R_CP_DEVICE_ID, 1u);

        /* Status inicial pós-reset */
        (void)Bc250PfpRead(Context, R_CP_STAT, &cpStat);
        (void)Bc250PfpRead(Context, R_GRBM_STATUS, &grbmStat);
        (void)Bc250PfpRead(Context, R_GRBM_STATUS2, &grbmStat2);
        Evidence->FaultAddressHigh = ((cpStat & 0xFFu) << 24) | ((grbmStat2 & 0xFFu) << 16) | (grbmStat & 0xFFFFu);
        Evidence->WriteOnlyPortsProgrammed = 1u;
    }

    /* 1. Configura registradores do anel sob endereço físico MC e VMID 0 */
    rbCntl = 0x00A00709u; /* BUFSZ=9, BLKSZ=7, MIN_AVAIL=0x00A00000 */
    (void)Bc250PfpWrite(Context, R_CP_RB_DOORBELL_CONTROL, 0u);
    (void)Bc250PfpWrite(Context, R_CP_RB0_BASE, (uint32_t)(ringMc >> 8));
    (void)Bc250PfpWrite(Context, R_CP_RB0_BASE_HI, (uint32_t)((ringMc >> 8) >> 32));
    (void)Bc250PfpWrite(Context, R_CP_RB0_RPTR_ADDR, (uint32_t)rptrWbMc);
    (void)Bc250PfpWrite(Context, R_CP_RB0_RPTR_ADDR_HI, (uint32_t)(rptrWbMc >> 32));
    (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_POLL_ADDR_LO, (uint32_t)wptrPollMc);
    (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_POLL_ADDR_HI, (uint32_t)(wptrPollMc >> 32));
    (void)Bc250PfpWrite(Context, R_CP_RB_VMID, 0u);
    (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_DELAY, 0u);
    (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_POLL_CNTL, 0x00403004u); /* Polling ativo 64 clocks */

    (void)Bc250PfpWrite(Context, R_CP_RB0_CNTL, rbCntl);
    (void)Bc250PfpWrite(Context, R_CP_RB0_RPTR, 0u);
    (void)Bc250PfpWrite(Context, R_CP_RB0_WPTR, 0u);
    (void)Bc250PfpWrite(Context, R_CP_RB0_WPTR_HI, 0u);

    (void)Bc250PfpWrite(Context, R_CP_SCRATCH_REG0, 0x11111111u);

    (void)Bc250PfpWrite(Context, R_VGT_NUM_INSTANCES, 1u);
    (void)Bc250PfpWrite(Context, R_PA_SC_LINE_STIPPLE_STATE, 0u);

    /* 1b. Invalidação estrita de I-Cache antes do UNHALT */
    Evidence->IcacheInvalidateUnhaltMask = Bc250PfpIcacheOpAll(
        Context, BC250_IC_OP_INVALIDATE_CACHE, BC250_IC_OP_INVALIDATE_CACHE_COMPLETE);
    Evidence->IcachePrimeUnhaltMask = Bc250PfpIcacheOpAll(
        Context, BC250_IC_OP_PRIME_ICACHE, BC250_IC_OP_ICACHE_PRIMED);
    KeMemoryBarrier();
    KeStallExecutionProcessor(50u);

    /* 2. UNHALT CP e MEC */
    (void)Bc250PfpWrite(Context, R_CP_MEC_CNTL, 0x00000000u);
    if (!Bc250PfpWrite(Context, R_CP_ME_CNTL, Evidence->CpMeCntlApplied)) {
        status = BC250_PFP_DIRECT_LOAD_MMIO_FAILED;
        goto Restore;
    }
    KeMemoryBarrier();

    /* 2b. Traco do PC na janela cega entre o UNHALT e a ativacao do anel.
     * Nos 109 sucessos o PFP assenta antes do primeiro laco de amostragem, o
     * que torna PcFirst/PcLast cegos justamente onde a decisao acontece.
     * Leituras consecutivas sem stall: o espacamento e' a latencia do MMIO. */
    for (i = 0u; i < BC250_PFP_PC_TRACE_SAMPLES; ++i) {
        (void)Bc250PfpRead(Context, R_CP_PFP_PC, &Evidence->PfpPcTrace[i]);
    }

    KeStallExecutionProcessor(50u);

    /* 3. Aguarda CP idle / pronto */
    for (i = 0u; i < 100u; ++i) {
        if (Bc250PfpRead(Context, R_CP_STAT, &value) && (value & 1u) == 0u) break;
        KeStallExecutionProcessor(10u);
    }

    /* 4. Ativa o Ring 0 */
    if (!Bc250PfpWrite(Context, R_CP_RB0_ACTIVE, 1u)) {
        status = BC250_PFP_DIRECT_LOAD_MMIO_FAILED;
        goto Restore;
    }
    Evidence->Rb0ActiveApplied = 1u;
    KeStallExecutionProcessor(50u);

    /* 5. Atualiza WPTR na memoria e dispara trigger 64-bit SOC15 */
    *pWptrPoll = cursor;
    KeMemoryBarrier();

    (void)Bc250PfpWrite(Context, R_HDP_READ_CACHE_INVALIDATE, 1u);
    (void)Bc250PfpWrite(Context, R_GCVM_L2_CNTL2, 0x00000003u);
    /* Este sitio disparava a invalidacao e seguia em frente sem esperar nada. */
    Evidence->InvalidateAckPreWptr = Bc250PfpGcvmInvalidate(
        Context, invalidateEngine, 0x00000001u, invalidateUseSemaphore,
        &Evidence->InvalidateAckBeforePreWptr,
        &Evidence->InvalidateAckRawPreWptr,
        &Evidence->InvalidatePollsPreWptr,
        &Evidence->InvalidateSemAcquiredPreWptr);

    (void)Bc250PfpWrite(Context, R_CP_RB0_WPTR, cursor);
    (void)Bc250PfpWrite(Context, R_CP_RB0_WPTR_HI, 0u);
    Evidence->Rb0WptrApplied = cursor;
    KeMemoryBarrier();

    /* 6. Amostragem adaptativa de RPTR, fence, PCs e Scratch */
    for (i = 0u; i < 100u; ++i) {
        uint32_t rptrMmio = 0u;
        uint32_t rptrWb = *pRptrWb;

        (void)Bc250PfpRead(Context, R_CP_RB0_RPTR, &rptrMmio);
        (void)Bc250PfpRead(Context, R_CP_SCRATCH_REG0, &scratchAfter);
        Evidence->FaultAddressLow = scratchAfter;

        if (*pFence != BC250_CP_RB0_FENCE_POISON) {
            Evidence->Rb0FenceAfter = *pFence;
        } else if (scratchAfter == 0x22222222u) {
            Evidence->Rb0FenceAfter = scratchAfter;
        } else {
            Evidence->Rb0FenceAfter = *pFence;
        }

        if (rptrMmio != 0u) {
            Evidence->Rb0RptrAfter = rptrMmio;
        } else {
            Evidence->Rb0RptrAfter = rptrWb;
        }

        (void)Bc250PfpRead(Context, R_CP_PFP_PC, &value);
        (void)Bc250PfpRead(Context, R_CP_ME_PC, &Evidence->MePcLast);
        (void)Bc250PfpRead(Context, R_CP_CE_PC, &Evidence->CePcLast);
        uint32_t mec1Pc = 0u;
        (void)Bc250PfpRead(Context, R_CP_MEC1_PC, &mec1Pc);
        {
            uint32_t jtStatPfp = 0u, jtStatCe = 0u, jtStatMec = 0u;
            (void)Bc250PfpRead(Context, R_CP_PFP_JT_STAT, &jtStatPfp);
            (void)Bc250PfpRead(Context, R_CP_CE_JT_STAT, &jtStatCe);
            (void)Bc250PfpRead(Context, R_CP_MEC_JT_STAT, &jtStatMec);
            /* Campos proprios: FaultAddressHigh tambem carrega CP_STAT/GRBM na
             * fase 4c, e comparar capturas por ele mistura dois significados. */
            Evidence->JtStatPfp = jtStatPfp;
            Evidence->JtStatCe = jtStatCe;
            Evidence->JtStatMec = jtStatMec;
            if (jtStatPfp != 0u || jtStatCe != 0u || jtStatMec != 0u || i == 0u) {
                Evidence->FaultAddressHigh = ((jtStatMec & 0xFFu) << 16) | ((jtStatCe & 0xFFu) << 8) | (jtStatPfp & 0xFFu);
            }
        }
        if (i == 0u) Evidence->PfpPcFirst = value;
        Evidence->PfpPcLast = value;
        if (value != previousPc) ++Evidence->PfpPcTransitions;
        previousPc = value;

        if (Evidence->Rb0RptrAfter >= cursor &&
            (Evidence->Rb0FenceAfter == BC250_CP_RB0_FENCE_VALUE ||
             Evidence->Rb0FenceAfter == 0x22222222u ||
             scratchAfter == 0x22222222u)) {
            break;
        }
        KeStallExecutionProcessor(200u);
    }
    (void)Bc250PfpRead(Context, R_CP_RB0_RPTR, &Evidence->Rb0RptrAfter);
    Evidence->PfpPcSamples = (i >= 100u) ? 100u : (i + 1u);

    /* Conferencia byte a byte do destino contra o gabarito.
     *
     * O laco de amostragem sai assim que a fence aparece, e a fence e' escrita
     * por um pacote ANTERIOR ao do destino. Comparar imediatamente confunde
     * "a GPU nao escreveu" com "a GPU ainda nao escreveu". Aqui espera-se o
     * gabarito com flush de HDP a cada iteracao, e registra-se quantas iteracoes
     * foram necessarias e o primeiro dword observado — se ele continuar sendo o
     * veneno, a escrita nunca aconteceu; se for outro valor, o endereco e' que
     * esta errado. */
    Evidence->Rb0ComparedDwords = BC250_CP_RB0_DEST_DWORDS;
    for (destPoll = 0u; destPoll < 1000u; ++destPoll) {
        uint32_t mismatches = 0u;
        (void)Bc250PfpWrite(Context, R_HDP_READ_CACHE_INVALIDATE, 1u);
        for (i = 0u; i < BC250_CP_RB0_DEST_DWORDS; ++i) {
            if (READ_REGISTER_ULONG((volatile ULONG *)&pDest[i]) !=
                (BC250_CP_RB0_DEST_PATTERN + i)) {
                ++mismatches;
            }
        }
        Evidence->Rb0MismatchCount = mismatches;
        if (mismatches == 0u) break;
        KeStallExecutionProcessor(1u);
    }
    Evidence->Rb0DestPolls = destPoll;
    Evidence->Rb0DestObserved =
        READ_REGISTER_ULONG((volatile ULONG *)&pDest[0]);

    (void)Bc250PfpWrite(Context, R_CP_RB0_ACTIVE, 0u);
    if (Context->GfxRingBringupLevel == 0u) {
        (void)Bc250PfpWrite(Context, R_CP_ME_CNTL, Evidence->CpMeCntlBefore);
        (void)Bc250PfpWrite(Context, R_CP_MEC_CNTL, origCpMecCntl);
        KeMemoryBarrier();
        KeStallExecutionProcessor(50u);
    }

    (void)Bc250PfpRead(Context, R_FAULT_STATUS,
        &Evidence->FaultStatusAfter);

    /* Gate estrito, conforme o tripé de CLAUDE.md — substitui o afrouxamento
     * auditado na seção 4 do STATUS (`RPTR >= cursor-1`, sem checagem de dados).
     * O `-1` existia por causa de um PACKET3 NOP de um dword mal formado; com o
     * PACKET2 o RPTR alcanca o WPTR e a igualdade estrita e' alcançável. */
    status = (Evidence->Rb0RptrAfter == cursor &&
              Evidence->Rb0WptrApplied == cursor &&
              Evidence->Rb0MismatchCount == 0u &&
              /* A fence tem de ser a fence. O `|| scratch` anterior aceitava um
               * registrador escrito pelo mesmo pacote como substituto da fence
               * fisica em VRAM — sao provas diferentes. */
              Evidence->Rb0FenceAfter == BC250_CP_RB0_FENCE_VALUE) ?
              BC250_PFP_DIRECT_LOAD_OK :
              BC250_PFP_DIRECT_LOAD_PM4_NOT_CONSUMED;

    if (status == BC250_PFP_DIRECT_LOAD_OK) {
        Evidence->PhysicalReady = 1u;
    }

Restore:
    if (pspRingActive != 0u) {
        uint32_t pspAck = 0u;
        uint32_t pollIndex;
        (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_64, BC250_PSP_RING_DESTROY);
        KeStallExecutionProcessor(20000u);
        for (pollIndex = 0u; pollIndex < 500u; ++pollIndex) {
            if (Bc250PfpRead(Context, BC250_PSP_RING_C2PMSG_64, &pspAck) &&
                (pspAck & BC250_PSP_RING_ACK_MASK) == BC250_PSP_RING_ACK_VALUE) break;
            KeStallExecutionProcessor(1000u);
        }
        (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_67, 0u);
        (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_69, 0u);
        (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_70, 0u);
        (void)Bc250PfpWrite(Context, BC250_PSP_RING_C2PMSG_71, 0u);
        pspRingActive = 0u;
    }

    if (registersTouched != 0u && snapshotValid != 0u) {
        (void)Bc250PfpWrite(Context, R_CP_RB0_ACTIVE, origRb0Active);
        (void)Bc250PfpWrite(Context, R_CP_RB0_CNTL, origRb0Cntl);
        (void)Bc250PfpWrite(Context, R_CP_RB0_BASE, origRb0Base);
        (void)Bc250PfpWrite(Context, R_CP_RB0_BASE_HI, origRb0BaseHi);
        (void)Bc250PfpWrite(Context, R_CP_RB0_RPTR, origRb0Rptr);
        (void)Bc250PfpWrite(Context, R_CP_RB0_WPTR, origRb0Wptr);
        (void)Bc250PfpWrite(Context, R_CP_RB0_WPTR_HI, origRb0WptrHi);
        (void)Bc250PfpWrite(Context, R_CP_RB0_RPTR_ADDR, origRb0RptrAddr);
        (void)Bc250PfpWrite(Context, R_CP_RB0_RPTR_ADDR_HI, origRb0RptrAddrHi);
        (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_POLL_ADDR_LO, origRbWptrPollAddrLo);
        (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_POLL_ADDR_HI, origRbWptrPollAddrHi);
        (void)Bc250PfpWrite(Context, R_CP_RB_VMID, origRbVmid);
        (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_DELAY, origRbWptrDelay);
        (void)Bc250PfpWrite(Context, R_CP_RB_WPTR_POLL_CNTL, origRbWptrPollCntl);
        (void)Bc250PfpWrite(Context, R_CP_RB_DOORBELL_CONTROL, origRbDoorbellCntl);
        (void)Bc250PfpWrite(Context, R_CGTT_CPF_CLK_CTRL, origCgttCpfClkCtrl);
        (void)Bc250PfpWrite(Context, R_GE_FAST_CLKS, origGeFastClks);
        (void)Bc250PfpWrite(Context, R_CP_SD_CNTL, origCpSdCntl);
        (void)Bc250PfpWrite(Context, R_GRBM_GFX_INDEX, 0u);
        (void)Bc250PfpWrite(Context, R_GRBM_GFX_CNTL, 0u);
        (void)Bc250PfpWrite(Context, R_CP_MAX_CONTEXT, origCpMaxContext);
        (void)Bc250PfpWrite(Context, R_CP_SCRATCH_REG0, origScratchReg0);
        if (Context->GfxRingBringupLevel == 0u || status != BC250_PFP_DIRECT_LOAD_OK) {
            /* Restaurando com os engines em HALT: invalidar basta, primar nao. */
            (void)Bc250PfpIcacheOpAll(Context,
                BC250_IC_OP_INVALIDATE_CACHE, BC250_IC_OP_INVALIDATE_CACHE_COMPLETE);
            (void)Bc250PfpWrite(Context, R_CP_MEC_CNTL, origCpMecCntl);
            (void)Bc250PfpWrite(Context, R_CP_ME_CNTL, Evidence->CpMeCntlBefore);
        }
        (void)Bc250PfpWrite(Context, R_RLC_CNTL, Evidence->RlcCntlBefore);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_CNTL, origCtx0Cntl);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_END_LO, origCtx0PtEndLo);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_START_LO, origCtx0PtStartLo);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_BASE_HI, origCtx0PtBaseHi);
        (void)Bc250PfpWrite(Context, R_GCVM_CONTEXT0_PT_BASE_LO, origCtx0PtBaseLo);
        /*
         * Restore: os registradores GCVM_CONTEXT0_* acima sao reescritos com os
         * valores ORIGINAIS lidos no preflight - o contexto do display nunca e'
         * modificado. Logo nao ha' nada a invalidar em ENG0, e faze-lo apenas
         * derrubaria o scanout. Usa a mesma engine != 0 do caminho de carga.
         */
        (void)Bc250PfpWrite(Context, invalidateReq, 0x00F80001u);
        KeStallExecutionProcessor(50u);
        (void)Bc250PfpRead(Context, R_CP_RB0_ACTIVE, &Evidence->Rb0ActiveAfter);
        {
            uint32_t expectedMe = (Context->GfxRingBringupLevel > 0u && status == BC250_PFP_DIRECT_LOAD_OK)
                ? Evidence->CpMeCntlApplied
                : Evidence->CpMeCntlBefore;
            if (Bc250PfpRead(Context, R_CP_ME_CNTL, &Evidence->CpMeCntlAfter) &&
                Evidence->CpMeCntlAfter == expectedMe &&
                Bc250PfpRead(Context, R_RLC_CNTL, &value) &&
                value == Evidence->RlcCntlBefore)
                Evidence->RegisterRestorePassed = 1u;
        }
    } else if (registersTouched == 0u && snapshotValid != 0u) {
        Evidence->RegisterRestorePassed = 1u;
        Evidence->CpMeCntlAfter = Evidence->CpMeCntlBefore;
    }

    if (mappedValid != 0u) {
        if (memoryPrepared != 0u) {
            Bc250PfpCopyToDevice(mapped, backup, mappedDwords);
            for (i = 0u; i < mappedDwords; ++i) {
                if (READ_REGISTER_ULONG((volatile ULONG *)&mapped[i]) != backup[i]) {
                    Evidence->MemoryRestorePassed = 0u;
                    if (status == BC250_PFP_DIRECT_LOAD_OK)
                        status = BC250_PFP_DIRECT_LOAD_RESTORE_FAILED;
                    break;
                }
            }
            if (i == mappedDwords) Evidence->MemoryRestorePassed = 1u;
        } else {
            Evidence->MemoryRestorePassed = 1u;
        }
        (void)Bc250VramCpuUnmapMemory(&Context->Core.DxgkInterface,
            (PVOID)mapped);
    }

    if (status == BC250_PFP_DIRECT_LOAD_OK)
        Evidence->PhysicalReady = 1u;
    if ((registersTouched != 0u && Evidence->RegisterRestorePassed == 0u) ||
        (memoryPrepared != 0u && Evidence->MemoryRestorePassed == 0u)) {
        status = BC250_PFP_DIRECT_LOAD_RESTORE_FAILED;
        Evidence->PhysicalReady = 0u;
    }

UnmapMmio:
    (void)Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);

Cleanup:
    if (pspFw != NULL) MmFreeContiguousMemory(pspFw);
    if (pspFence != NULL) MmFreeContiguousMemory(pspFence);
    if (pspCmd != NULL) MmFreeContiguousMemory(pspCmd);
    if (pspRing != NULL) MmFreeContiguousMemory(pspRing);
    /* TMR.1: TMR e quad do CP ficam reservados (persistentes) ate o fim do
     * dispositivo; devolve-los punha tabelas de pagina do VidMm em cima do
     * TMR vivo do PSP (doc 653-TMR). */
    UNREFERENCED_PARAMETER(token);
    UNREFERENCED_PARAMETER(tmrToken);
    (void)allocated;
    if (backup != NULL)
        ExFreePoolWithTag(backup, BC250_PFP_POOL_TAG);
    if (mecFileBuffer != NULL)
        ExFreePoolWithTag(mecFileBuffer, BC250_PFP_POOL_TAG);
    if (ceFileBuffer != NULL)
        ExFreePoolWithTag(ceFileBuffer, BC250_PFP_POOL_TAG);
    if (meFileBuffer != NULL)
        ExFreePoolWithTag(meFileBuffer, BC250_PFP_POOL_TAG);
    if (pfpFileBuffer != NULL)
        ExFreePoolWithTag(pfpFileBuffer, BC250_PFP_POOL_TAG);

    return status;
}
#else
BC250_PFP_DIRECT_LOAD_STATUS Bc250ActiveRunPfpDirectLoad(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PFP_DIRECT_LOAD_EVIDENCE *Evidence)
{
    (void)Context;
    if (Evidence != NULL)
        memset(Evidence, 0, sizeof(*Evidence));
    return BC250_PFP_DIRECT_LOAD_BAD_ARGUMENT;
}
#endif
