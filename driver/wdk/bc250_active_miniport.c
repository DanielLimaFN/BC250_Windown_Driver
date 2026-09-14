#include "bc250_active_miniport.h"
#include "bc250_mcdm_callbacks.h"
#include "bc250_display_ddi.h"
#include "bc250_probe_telemetry.h"
#include "bc250_sdma_present.h"
#include "bc250_cp_firmware_manifest.h"
#include "../core/bc250_adapter_info_contract.h"
#include "../core/bc250_allocation_abi_contract.h"
#include "../core/bc250_mcdm_debug_contract.h"
#include "../core/bc250_firmware_staging.h"
#include "../core/bc250_sdma_queue_state_contract.h"
#include "../core/bc250_escape_contract.h"
#include "../core/bc250_segment_address_contract.h"

DRIVER_INITIALIZATION_DATA g_Bc250ActiveInitializationData;

#define BC250_ACTIVE_MAX_ADAPTERS 8u
#define BC250_ACTIVE_GCVM_FAULT_STATUS   0x0A130u
#define BC250_ACTIVE_GCVM_FAULT_ADDR_LO  0x0A134u
#define BC250_ACTIVE_GCVM_FAULT_ADDR_HI  0x0A138u
static EX_PUSH_LOCK g_Bc250AdapterDirectoryLock;
static BC250_GPUVA_JOURNAL_STATUS Bc250ActiveInitializeJournalSet(
    BC250_DXGK_ACTIVE_CONTEXT *Context);

static BC250_DXGK_ACTIVE_CONTEXT *
    g_Bc250AdapterDirectory[BC250_ACTIVE_MAX_ADAPTERS];

static uint32_t Bc250ActiveExecuteGcvmBacking(
    void *Context, uint64_t PageTablePhysical);
static BC250_PSP_RING_STATUS Bc250ActiveProbePspRing(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PSP_RING_EVIDENCE *Evidence);
static BC250_PSP_LOAD_FW_STATUS Bc250ActiveLoadSdmaFirmware(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PSP_LOAD_FW_EVIDENCE *Evidence);
static BC250_PSP_LOAD_FW_STATUS Bc250ActiveLoadRlcFirmware(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PSP_LOAD_FW_EVIDENCE *Evidence,
    BC250_RLC_LOAD_EVIDENCE *RlcEvidence);
static void Bc250ActiveTlbStall(void *Context, uint32_t Microseconds);
static void Bc250ActivePspBarrier(void *Context);
static uint32_t Bc250ActiveVmidRead32(
    void *Context, uint32_t ByteOffset, uint32_t *Value);
static uint32_t Bc250ActiveVmidWrite32(
    void *Context, uint32_t ByteOffset, uint32_t Value);
static uint32_t Bc250ActiveFlushMappedGfxhubTlb(void *Context);
static uint32_t Bc250ActiveFlushComputeTlb(void *Context);
static uint32_t Bc250ActiveFlushVmid8Only(void *Context);
static uint32_t Bc250ActiveFlushUserVmidOnly(void *Context);
static NTSTATUS Bc250ActiveEnsureGlobalMmu(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    const char *Origin);

/*
 * Marcador de proveniencia do bring-up inseguro.
 *
 * Existe no binario SOMENTE quando BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP
 * foi definido.  Serve para que o empacotador seguro possa PROVAR, por
 * conteudo do .sys, que esta' assinando a imagem de producao — e nao a de
 * laboratorio com outro INF.
 *
 * Nao usar nomes de tunable para essa prova: eles aparecem no .rdata como
 * nomes de campo de telemetria mesmo na imagem de producao, e a verificacao
 * daria falso positivo.
 */
#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)
const WCHAR g_Bc250UnsafeBringupMarker[] =
    L"BC250-UNSAFE-STARTDEVICE-BRINGUP-COMPILED";
#define BC250_UNSAFE_BRINGUP_COMPILED 1u
#else
#define BC250_UNSAFE_BRINGUP_COMPILED 0u
#endif

static ULONG Bc250ActiveReadTunable(PCWSTR ValueName, ULONG DefaultValue)
{
#if defined(BC250_ENABLE_PERSISTENT_TELEMETRY)
    RTL_QUERY_REGISTRY_TABLE table[2];
    ULONG val = DefaultValue;
    NTSTATUS status;

    RtlZeroMemory(table, sizeof(table));
    table[0].Flags         = RTL_QUERY_REGISTRY_DIRECT;
    table[0].Name          = (PWSTR)ValueName;
    table[0].EntryContext  = &val;
    table[0].DefaultType   = REG_DWORD;
    table[0].DefaultData   = &DefaultValue;
    table[0].DefaultLength = sizeof(ULONG);

    status = RtlQueryRegistryValues(
        RTL_REGISTRY_SERVICES,
        L"bc250mcdm\\Parameters",
        table,
        NULL,
        NULL);

    return NT_SUCCESS(status) ? val : DefaultValue;
#else
    UNREFERENCED_PARAMETER(ValueName);
    return DefaultValue;
#endif
}

/* Le um tunable e informa se ele existe de fato.
 *
 * Bc250ActiveReadTunable devolve o default tanto quando o valor vale o default
 * quanto quando a leitura falha — e RtlQueryRegistryValues exige PASSIVE_LEVEL,
 * entao "falhou" e' um resultado possivel dependendo de onde e' chamado. Ler
 * duas vezes com defaults diferentes separa os dois casos: se os resultados
 * batem, o valor existe; se divergem, cada chamada devolveu o proprio default e
 * nao ha' valor legivel. */
static ULONG Bc250ActiveReadTunableChecked(
    PCWSTR ValueName, ULONG DefaultValue, ULONG *Present)
{
    ULONG asZero = Bc250ActiveReadTunable(ValueName, 0u);
    ULONG asOne = Bc250ActiveReadTunable(ValueName, 1u);

    if (asZero == asOne) {
        if (Present != NULL) *Present = 1u;
        return asZero;
    }

    if (Present != NULL) *Present = 0u;
    return DefaultValue;
}

static BC250_HQD_PREFLIGHT_STATUS Bc250ActiveProbeHqdReadonly(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_READONLY_SNAPSHOT *Snapshot,
    uint32_t *ReadCount)
{
    static const uint32_t offsets[BC250_HQD_PREFLIGHT_REGISTER_COUNT] = {
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
        BC250_HQD_MMIO_MQD_BASE_LO,
        BC250_HQD_MMIO_MQD_BASE_HI,
        BC250_HQD_MMIO_ACTIVE,
        BC250_HQD_MMIO_VMID,
        BC250_HQD_MMIO_PQ_BASE_LO,
        BC250_HQD_MMIO_PQ_BASE_HI,
        BC250_HQD_MMIO_PQ_RPTR,
        BC250_HQD_MMIO_DOORBELL_CONTROL,
        BC250_HQD_MMIO_PQ_CONTROL,
        BC250_HQD_MMIO_PQ_WPTR_LO,
        BC250_HQD_MMIO_PQ_WPTR_HI,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET
    };
    uint32_t pass1[BC250_HQD_PREFLIGHT_REGISTER_COUNT];
    uint32_t pass2[BC250_HQD_PREFLIGHT_REGISTER_COUNT];
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_PREFLIGHT_STATUS status =
        BC250_HQD_PREFLIGHT_TRANSPORT_FAILED;
    uint32_t reads = 0u, writes = 0u, index;

    if (Context == NULL || Snapshot == NULL || ReadCount == NULL) {
        return BC250_HQD_PREFLIGHT_BAD_ARGUMENT;
    }
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    *ReadCount = 0u;
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) return status;

    for (index = 0u; index < BC250_HQD_PREFLIGHT_REGISTER_COUNT; ++index) {
        if (!Bc250MmioTopologyRead32(
                &Context->Core.MmioTransport, offsets[index], &pass1[index])) {
            goto Unmap;
        }
    }
    KeMemoryBarrier();
    for (index = 0u; index < BC250_HQD_PREFLIGHT_REGISTER_COUNT; ++index) {
        if (!Bc250MmioTopologyRead32(
                &Context->Core.MmioTransport, offsets[index], &pass2[index])) {
            goto Unmap;
        }
    }
    status = Bc250ComputeHqdReadonlyPreflight(pass1, pass2, Snapshot);

Unmap:
    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    *ReadCount = reads;
    if (writes != 0u) return BC250_HQD_PREFLIGHT_WRITE_DETECTED;
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK)
        return BC250_HQD_PREFLIGHT_TRANSPORT_FAILED;
    return status;
}

static BC250_HQD_SELECTOR_STATUS Bc250ActiveProbeMecHqdSelected(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_SELECTOR_EVIDENCE *Evidence)
{
    BC250_HQD_SELECTOR_INTERFACE interfaceData;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_SELECTOR_STATUS status;
    uint32_t reads, writes;

    if (Context == NULL || Evidence == NULL)
        return BC250_HQD_SELECTOR_BAD_ARGUMENT;
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK)
        return BC250_HQD_SELECTOR_PREFLIGHT_FAILED;
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = &Context->Core.MmioTransport;
    status = Bc250ComputeProbeMecHqdSelected(&interfaceData, Evidence);
    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK ||
        reads != Evidence->ReadCalls || writes != Evidence->WriteCalls) {
        Evidence->PhysicalReady = 0u;
        return BC250_HQD_SELECTOR_RESTORE_FAILED;
    }
    return status;
}

/*
 * B.1 -- MEDICAO SOMENTE-LEITURA DO ANEL GRAFICO (CP_RB0).
 *
 * Este projeto nunca mediu o anel grafico. O que existe e' o valor de
 * CP_RB0_CNTL observado uma vez e anotado no catalogo; tudo o mais e'
 * suposicao. Aqui ele vira fotografia: o que o POST/GOP deixou em
 * CNTL, BASE, RPTR, WPTR, POLL_CNTL e ME_CNTL.
 *
 * DUAS SALVAGUARDAS, e as duas importam:
 *
 * 1. O contrato promete nao escrever, e o TRANSPORTE confirma. Comparamos os
 *    contadores do transporte (fonte independente) com os da evidencia. Se o
 *    transporte registrar UMA escrita, a medicao e' descartada -- a promessa
 *    de "somente leitura" nao pode depender so' de quem faz a promessa.
 *    Mesmo padrao ja usado em Bc250ActiveProbeMecHqdSelected.
 *
 * 2. Plan == NULL: medicao pura. Sem plano nao ha CntlPlanned, e o APPLY
 *    recusa por construcao. Medir nao pode escorregar para escrever.
 *
 * NAO altera o veredito do StartDevice em desfecho algum.
 */
static BC250_GFX_RING_TXN_STATUS Bc250ActiveProbeGfxRingReadonly(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_GFX_RING_TXN_EVIDENCE *Evidence)
{
    BC250_GFX_RING_INTERFACE interfaceData;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_GFX_RING_TXN_STATUS status;
    uint32_t reads;
    uint32_t writes;

    if (Context == NULL || Evidence == NULL)
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    RtlZeroMemory(Evidence, sizeof(*Evidence));

    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        Bc250DisplayLog("[GfxRing] PREFLIGHT nao executou: mapa do transporte falhou (%s)\n",
            Bc250MmioTopologyStatusName(mmioStatus));
        return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
    }

    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Stall = NULL;
    interfaceData.Context = &Context->Core.MmioTransport;
    interfaceData.Identity = BC250_GFX_RING_TXN_IDENTITY;

    status = Bc250GfxRingTxnPreflight(&interfaceData, NULL, Evidence);

    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    Context->GfxRingPreflightTransportWrites = writes;

    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        Bc250DisplayLog("[GfxRing] PREFLIGHT: unmap do transporte falhou (%s) -- medicao DESCARTADA\n",
            Bc250MmioTopologyStatusName(mmioStatus));
        return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
    }
    if (writes != 0u || reads != Evidence->Reads) {
        Bc250DisplayLog("[GfxRing] PREFLIGHT DESCARTADO: transporte contou Reads=%u Writes=%u contra Evidencia Reads=%u Writes=%u -- somente leitura NAO comprovada\n",
            reads, writes, Evidence->Reads, Evidence->Writes);
        Evidence->SnapshotTaken = 0u;
        return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
    }
    return status;
}

/*
 * B.1 -- ENSAIO DO ANEL GRAFICO (CP_RB0), sob a Lei 5.
 *
 * ESCALONADO DE PROPOSITO, porque a corrida e' o recurso escasso e a Lei 9
 * manda mover uma variavel por vez:
 *
 *   Level 1  configura o anel (BASE/CNTL/ponteiros) e confere por READBACK.
 *            Nao liga nada. As micro-engines continuam HALTED, entao nenhum
 *            pacote pode ser buscado -- e' escrita em registrador de uma
 *            engine desligada, com rollback provado.
 *
 *   Level 2  acrescenta desalt de CE/PFP/ME e a partida (CP_RB0_ACTIVE),
 *            cada um provado por readback. Exige firmware carregado, e por
 *            isso roda depois do PFP direct load.
 *
 * O ROLLBACK E' SEMPRE EXECUTADO. Este e' um ensaio de medicao, nao a criacao
 * de um anel permanente: o StartDevice termina com a placa no estado em que a
 * encontrou, e o proprio rollback e' conferido por readback.
 *
 * NAO altera o veredito do StartDevice em desfecho algum.
 */
static BC250_GFX_RING_TXN_STATUS Bc250ActiveRunGfxRingBringup(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    uint32_t Level,
    BC250_GFX_RING_TXN_EVIDENCE *Evidence)
{
    BC250_VRAM_ALLOCATOR *allocator;
    const BC250_VRAM_ADDRESS_SPACE *addressSpace;
    BC250_VRAM_TOKEN token;
    BC250_VRAM_REGION region;
    BC250_VRAM_STATUS vramStatus;
    BC250_GFX_RING_INTERFACE interfaceData;
    BC250_GFX_RING_PLAN plan;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_GFX_RING_TXN_STATUS status;
    BC250_GFX_RING_TXN_STATUS rollbackStatus;
    const uint64_t ringBytes = 8192ull;
    /* anel + dword de writeback de RPTR + dword de shadow de WPTR, com folga
     * de alinhamento para os dois ficarem fora do anel */
    const uint64_t totalBytes = ringBytes + 0x1000ull;
    uint64_t ringMc;
    uint32_t mappedByUs;

    if (Context == NULL || Evidence == NULL || Level == 0u) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    RtlZeroMemory(&plan, sizeof(plan));

    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;
    addressSpace = &Context->Core.TopologyEvidence.AddressSpace;
    if (addressSpace->TotalBytes == 0u || addressSpace->VramMcStart == 0u) {
        Bc250DisplayLog("[GfxRing] BRINGUP nao executou: janela de VRAM indisponivel\n");
        return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
    }

    vramStatus = Bc250VramAllocate(allocator, totalBytes, 65536ull,
        BC250_VRAM_ROLE_RING, BC250_VRAM_ALLOC_TOP_DOWN, &token);
    if (vramStatus != BC250_VRAM_OK) {
        Bc250DisplayLog("[GfxRing] BRINGUP nao executou: VRAM negada (%s)\n",
            Bc250VramStatusName(vramStatus));
        return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
    }
    if (Bc250VramGetRegion(allocator, token, &region) != BC250_VRAM_OK) {
        (void)Bc250VramRelease(allocator, token);
        return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
    }

    ringMc = addressSpace->VramMcStart + region.Offset;
    plan.VramWindowBase = addressSpace->VramMcStart;
    plan.VramWindowBytes = addressSpace->TotalBytes;
    plan.RingGpuVa = ringMc;
    plan.RingBytes = (uint32_t)ringBytes;
    /* writeback e shadow DEPOIS do anel, sem sobreposicao -- o contrato recusa
     * se encostarem, e essa recusa tem teste proprio */
    plan.RptrWritebackGpuVa = ringMc + ringBytes;
    plan.WptrPollGpuVa = ringMc + ringBytes + 0x10ull;
    plan.RbExe = 1u;
    plan.CeHqdNeqRbHqd = 1u;
    if (Level >= 2u) {
        plan.UnhaltMicroEngines = 1u;
        plan.MicroEngineFirmwareLoaded = 1u;
    }

    Bc250DisplayLog("[GfxRing] BRINGUP level=%u: RingMc=0x%llX Bytes=%llu RptrWb=0x%llX WptrShadow=0x%llX\n",
        Level, (unsigned long long)plan.RingGpuVa, (unsigned long long)ringBytes,
        (unsigned long long)plan.RptrWritebackGpuVa,
        (unsigned long long)plan.WptrPollGpuVa);

    /*
     * ESTADO DO TRANSPORTE -- corrigido pela corrida 14/09 13:18.
     *
     * Aquela corrida perdeu o ensaio com "mapa do transporte falhou
     * (wrong_state)": com -EnableShaderDispatch o caminho do PFP deixa
     * Core.MmioTransport MAPEADO, e Bc250MmioTopologyMapReadonly recusa
     * transporte nao-vazio. No nivel 1, sem aquele caminho, o transporte
     * estava livre e o ensaio rodou -- por isso o defeito so' apareceu aqui.
     *
     * E' a classe A16, que ja custou a corrida de 08/09 11:48.
     *
     * A correcao NAO e' desmapear o mapeamento alheio: quem mapeou pode ainda
     * precisar dele. Reusamos o que existe e desmapeamos SOMENTE o que nos
     * mesmos mapearmos.
     */
    mappedByUs = 0u;
    if (Context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
        Bc250DisplayLog("[GfxRing] BRINGUP: transporte JA MAPEADO por estagio anterior -- reusando, sem desmapear no fim\n");
    } else {
        mmioStatus = Bc250MmioTopologyMapReadonly(
            &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
            Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
            BC250_PCI_MMIO_BAR_INDEX);
        if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
            (void)Bc250VramRelease(allocator, token);
            Bc250DisplayLog("[GfxRing] BRINGUP nao executou: mapa do transporte falhou (%s)\n",
                Bc250MmioTopologyStatusName(mmioStatus));
            return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
        }
        mappedByUs = 1u;
    }

    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Stall = Bc250ActiveTlbStall;
    interfaceData.Context = &Context->Core.MmioTransport;
    interfaceData.Identity = BC250_GFX_RING_TXN_IDENTITY;

    status = Bc250GfxRingTxnPreflight(&interfaceData, &plan, Evidence);
    if (status == BC250_GFX_RING_TXN_OK) {
        status = Bc250GfxRingTxnApply(&interfaceData, &plan, Evidence);
    }

    /*
     * ROLLBACK SEMPRE, inclusive quando o APPLY falhou no meio: e' justamente
     * o caso em que a placa pode ter ficado parcialmente escrita.
     */
    rollbackStatus = Bc250GfxRingTxnRollback(&interfaceData, Evidence);

    /* Desmapeia SOMENTE se fomos nos que mapeamos -- ver o bloco de estado
     * acima. Desmapear o mapeamento alheio quebraria o estagio que o criou. */
    if (mappedByUs == 1u) {
        (void)Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    }
    (void)Bc250VramRelease(allocator, token);

    if (rollbackStatus != BC250_GFX_RING_TXN_OK) {
        Bc250DisplayLog("[GfxRing] BRINGUP: ROLLBACK FALHOU (%s) -- a placa NAO voltou ao estado inicial\n",
            Bc250GfxRingTxnStatusName(rollbackStatus));
        return rollbackStatus;
    }
    return status;
}

static BC250_HQD_BANK_PROBE_STATUS Bc250ActiveProbeMecHqdBankIsolation(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_BANK_PROBE_EVIDENCE *Evidence)
{
    BC250_HQD_SELECTOR_INTERFACE interfaceData;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_BANK_PROBE_STATUS status;
    uint32_t reads, writes;

    if (Context == NULL || Evidence == NULL)
        return BC250_HQD_BANK_PROBE_BAD_ARGUMENT;
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK)
        return BC250_HQD_BANK_PROBE_PREFLIGHT_FAILED;
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = &Context->Core.MmioTransport;
    status = Bc250ComputeProbeMecHqdBankIsolation(
        &interfaceData, Evidence);
    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK ||
        reads != Evidence->ReadCalls || writes != Evidence->WriteCalls) {
        Evidence->PhysicalReady = 0u;
        return BC250_HQD_BANK_PROBE_RESTORE_FAILED;
    }
    return status;
}

static BC250_HQD_WPTR_DIAGNOSTIC_STATUS Bc250ActiveProbeMecHqdWptrBehavior(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_WPTR_DIAGNOSTIC_EVIDENCE *Evidence)
{
    BC250_HQD_SELECTOR_INTERFACE interfaceData;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_WPTR_DIAGNOSTIC_STATUS status;
    uint32_t reads, writes;

    if (Context == NULL || Evidence == NULL)
        return BC250_HQD_WPTR_DIAG_BAD_ARGUMENT;
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK)
        return BC250_HQD_WPTR_DIAG_PREFLIGHT_FAILED;
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = &Context->Core.MmioTransport;
    status = Bc250ComputeProbeMecHqdWptrBehavior(&interfaceData, Evidence);
    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK ||
        reads != Evidence->ReadCalls || writes != Evidence->WriteCalls) {
        Evidence->PhysicalReady = 0u;
        return BC250_HQD_WPTR_DIAG_RESTORE_FAILED;
    }
    return status;
}

BC250_HQD_PROGRAM_STATUS Bc250ActiveRunInactiveHqdProgramTransaction(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_PROGRAM_EVIDENCE *Evidence)
{
    BC250_HQD_SELECTOR_INTERFACE interfaceData;
    BC250_HQD_MC_BUFFERS buffers;
    BC250_COMPUTE_MQD_PROPERTIES props;
    BC250_GFX10_COMPUTE_MQD mqd;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_PROGRAM_STATUS status;
    BC250_VRAM_ALLOCATOR *allocator;
    const BC250_VRAM_ADDRESS_SPACE *addressSpace;
    BC250_VRAM_TOKEN token = {0};
    BC250_VRAM_REGION region = {0};
    BC250_VRAM_STATUS vramStatus;
    const BC250_RESOLVED_BAR_RESOURCE *bar = NULL;
    PHYSICAL_ADDRESS physical;
    volatile uint32_t *mapped = NULL;
    NTSTATUS ntStatus;
    uint32_t totalBytes = 73728u; /* 72 KiB */
    uint32_t reads, writes;
    uint32_t i;
    size_t matchCount = 0;

    if (Context == NULL || Evidence == NULL)
        return BC250_HQD_PROGRAM_BAD_ARGUMENT;

    RtlZeroMemory(Evidence, sizeof(*Evidence));
    RtlZeroMemory(&buffers, sizeof(buffers));
    RtlZeroMemory(&props, sizeof(props));
    RtlZeroMemory(&mqd, sizeof(mqd));

    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;
    addressSpace = &Context->Core.TopologyEvidence.AddressSpace;
    if (addressSpace->TotalBytes == 0u || addressSpace->VramMcStart == 0u) {
        return BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    }

    for (i = 0; i < Context->Core.ResolvedBarCount; ++i) {
        if (Context->Core.ResolvedBars[i].Index == BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            bar = &Context->Core.ResolvedBars[i];
            ++matchCount;
        }
    }
    if (matchCount != 1 || bar == NULL) {
        return BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    }

    vramStatus = Bc250VramAllocate(
        allocator, totalBytes, 256u,
        BC250_VRAM_ROLE_MQD,
        BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE,
        &token);
    if (vramStatus != BC250_VRAM_OK) {
        return BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    }

    if (Bc250VramGetRegion(allocator, token, &region) != BC250_VRAM_OK) {
        (void)Bc250VramRelease(allocator, token);
        return BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    }

    physical.QuadPart = (LONGLONG)(bar->Start + region.Offset);
    ntStatus = Context->Core.DxgkInterface.DxgkCbMapMemory(
        Context->Core.DxgkInterface.DeviceHandle,
        physical,
        (ULONG)totalBytes,
        FALSE,
        FALSE,
        MmNonCached,
        (PVOID *)&mapped);
    if (!NT_SUCCESS(ntStatus) || mapped == NULL) {
        (void)Bc250VramRelease(allocator, token);
        return BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    }

    buffers.MqdVirtual = (volatile void *)((PUCHAR)mapped + 0x0000);
    buffers.MqdGpuAddress = addressSpace->VramMcStart + region.Offset + 0x0000;
    buffers.MqdSizeBytes = BC250_MQD_SIZE_BYTES;

    buffers.RingVirtual = (volatile void *)((PUCHAR)mapped + 0x0200);
    buffers.RingGpuAddress = addressSpace->VramMcStart + region.Offset + 0x0200;
    buffers.RingSizeBytes = 65536u;

    buffers.EopVirtual = (volatile void *)((PUCHAR)mapped + 0x10200);
    buffers.EopGpuAddress = addressSpace->VramMcStart + region.Offset + 0x10200;
    buffers.EopSizeBytes = 4096u;

    buffers.RptrVirtual = (volatile void *)((PUCHAR)mapped + 0x11200);
    buffers.RptrGpuAddress = addressSpace->VramMcStart + region.Offset + 0x11200;
    buffers.RptrSizeBytes = 8u;

    buffers.WptrVirtual = (volatile void *)((PUCHAR)mapped + 0x11300);
    buffers.WptrGpuAddress = addressSpace->VramMcStart + region.Offset + 0x11300;
    buffers.WptrSizeBytes = 8u;

    buffers.FenceVirtual = (volatile void *)((PUCHAR)mapped + 0x11400);
    buffers.FenceGpuAddress = addressSpace->VramMcStart + region.Offset + 0x11400;
    buffers.FenceSizeBytes = 8u;

    props.MqdGpuAddress = buffers.MqdGpuAddress;
    props.HqdBaseGpuAddress = buffers.RingGpuAddress;
    props.QueueSizeDwords = 16384u;
    props.EopGpuAddress = buffers.EopGpuAddress;
    props.EopSizeBytes = 4096u;
    props.RptrGpuAddress = buffers.RptrGpuAddress;
    props.WptrGpuAddress = buffers.WptrGpuAddress;
    props.UseDoorbell = 0u;
    props.DoorbellIndex = 0u;
    /* Cyan Skillfish 2: a fila de compute validada usa MEC2/Pipe0/Queue0.
     * O contrato rejeita SchedulerMe == 0 antes de qualquer acesso ao HW. */
    props.SchedulerMe = 2u;
    props.SchedulerPipe = 0u;
    props.SchedulerQueue = 0u;
    props.PipePriority = 0u;
    props.QueuePriority = 0u;
    props.HqdActive = 0u;

    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
            Context->Core.DxgkInterface.DeviceHandle, (PVOID)mapped);
        (void)Bc250VramRelease(allocator, token);
        return BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    }

    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = &Context->Core.MmioTransport;

    status = Bc250ComputeRunInactiveHqdProgramTransaction(
        &interfaceData, &buffers, &props, &mqd, Evidence);

    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);

    (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
        Context->Core.DxgkInterface.DeviceHandle, (PVOID)mapped);
    (void)Bc250VramRelease(allocator, token);

    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK ||
        reads != Evidence->ReadCalls || writes != Evidence->WriteCalls) {
        Evidence->PhysicalReady = 0u;
        return BC250_HQD_PROGRAM_RESTORE_FAILED;
    }

    return status;
}

static BC250_HQD_ACTIVE_STATUS Bc250ActiveRunActiveHqdSubmissionTransaction(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *Evidence)
{
    BC250_HQD_SELECTOR_INTERFACE interfaceData;
    BC250_HQD_MC_BUFFERS buffers;
    BC250_COMPUTE_MQD_PROPERTIES props;
    BC250_GFX10_COMPUTE_MQD mqd;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_ACTIVE_STATUS status;
    BC250_VRAM_ALLOCATOR *allocator;
    const BC250_VRAM_ADDRESS_SPACE *addressSpace;
    BC250_VRAM_TOKEN token = {0};
    BC250_VRAM_REGION region = {0};
    BC250_VRAM_STATUS vramStatus;
    const BC250_RESOLVED_BAR_RESOURCE *bar = NULL;
    PHYSICAL_ADDRESS physical;
    volatile uint32_t *mapped = NULL;
    NTSTATUS ntStatus;
    uint32_t totalBytes = 131072u; /* 128 KiB para garantir alinhamento de 64 KiB no anel */
    uint32_t reads, writes;
    uint32_t i;
    size_t matchCount = 0;

    if (Context == NULL || Evidence == NULL)
        return BC250_HQD_ACTIVE_BAD_ARGUMENT;

    RtlZeroMemory(Evidence, sizeof(*Evidence));
    Evidence->GfxOffApplicable = 0u;
    Evidence->GfxOffGuardPassed = 1u;
    Evidence->GfxOffRestorePassed = 1u;
    RtlZeroMemory(&buffers, sizeof(buffers));
    RtlZeroMemory(&props, sizeof(props));
    RtlZeroMemory(&mqd, sizeof(mqd));

    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;
    addressSpace = &Context->Core.TopologyEvidence.AddressSpace;
    if (addressSpace->TotalBytes == 0u || addressSpace->VramMcStart == 0u) {
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }

    for (i = 0; i < Context->Core.ResolvedBarCount; ++i) {
        if (Context->Core.ResolvedBars[i].Index == BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            bar = &Context->Core.ResolvedBars[i];
            ++matchCount;
        }
    }
    if (matchCount != 1 || bar == NULL) {
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }

    vramStatus = Bc250VramAllocate(
        allocator, totalBytes, 65536u,
        BC250_VRAM_ROLE_MQD,
        BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE,
        &token);
    if (vramStatus != BC250_VRAM_OK) {
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }

    if (Bc250VramGetRegion(allocator, token, &region) != BC250_VRAM_OK) {
        (void)Bc250VramRelease(allocator, token);
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }

    physical.QuadPart = (LONGLONG)(bar->Start + region.Offset);
    ntStatus = Context->Core.DxgkInterface.DxgkCbMapMemory(
        Context->Core.DxgkInterface.DeviceHandle,
        physical,
        (ULONG)totalBytes,
        FALSE,
        FALSE,
        MmNonCached,
        (PVOID *)&mapped);
    if (!NT_SUCCESS(ntStatus) || mapped == NULL) {
        (void)Bc250VramRelease(allocator, token);
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }

    {
        uint64_t basePhys = (addressSpace->VramMcStart + region.Offset);

        /* Enderecos MC fisicos em VRAM para o microengine MEC (VMID 0) */
        buffers.MqdVirtual = (volatile void *)((PUCHAR)mapped + 0x0000);
        buffers.MqdGpuAddress = basePhys + 0x0000ull;
        buffers.MqdSizeBytes = BC250_MQD_SIZE_BYTES;

        buffers.EopVirtual = (volatile void *)((PUCHAR)mapped + 0x1000);
        buffers.EopGpuAddress = basePhys + 0x1000ull;
        buffers.EopSizeBytes = 4096u;

        buffers.RptrVirtual = (volatile void *)((PUCHAR)mapped + 0x2000);
        buffers.RptrGpuAddress = basePhys + 0x2000ull;
        buffers.RptrSizeBytes = 256u;

        buffers.WptrVirtual = (volatile void *)((PUCHAR)mapped + 0x2100);
        buffers.WptrGpuAddress = basePhys + 0x2100ull;
        buffers.WptrSizeBytes = 256u;

        buffers.FenceVirtual = (volatile void *)((PUCHAR)mapped + 0x2200);
        buffers.FenceGpuAddress = basePhys + 0x2200ull;
        buffers.FenceSizeBytes = 256u;

        /* GPUVA 0x4000/0x5000 map to these owned physical pages. */
        buffers.ShaderVirtual = (volatile void *)((PUCHAR)mapped + 0x4000);
        buffers.ShaderGpuAddress = basePhys + 0x4000ull;
        buffers.ShaderSizeBytes = 256u;

        buffers.ResultVirtual = (volatile void *)((PUCHAR)mapped + 0x5000);
        buffers.ResultGpuAddress = basePhys + 0x5000ull;
        buffers.ResultSizeBytes = 256u;

        /* Page Table Context 0 (VMID 0) em 0x3000 (4 KiB = 512 PTEs) */
        buffers.PtVirtual = (volatile void *)((PUCHAR)mapped + 0x3000);
        buffers.PtGpuAddress = basePhys + 0x3000ull;
        buffers.PtSizeBytes = 4096u;

        {
            volatile uint64_t *pPt = (volatile uint64_t *)buffers.PtVirtual;
            uint32_t pteIdx;
            for (pteIdx = 0u; pteIdx < 512u; ++pteIdx) {
                uint64_t pagePhys = basePhys + ((uint64_t)pteIdx * 4096ull);
                pPt[pteIdx] = (pagePhys & 0x0000FFFFFFFFF000ull) | 0xE1ull;
            }
            KeMemoryBarrier();
        }

        /* Ring buffer em 0x10000 (64 KiB) perfeitamente alinhado a 64 KiB */
        buffers.RingVirtual = (volatile void *)((PUCHAR)mapped + 0x10000);
        buffers.RingGpuAddress = basePhys + 0x10000ull;
        buffers.RingSizeBytes = 65536u;
    }

    props.MqdGpuAddress = buffers.MqdGpuAddress;
    /* No modo Host PA (Vmid0PageTable=0), CP_HQD_PQ_BASE aponta para a VRAM física. */
    props.HqdBaseGpuAddress = buffers.RingGpuAddress;
    props.QueueSizeDwords = 16384u;
    props.EopGpuAddress = buffers.EopGpuAddress;
    props.EopSizeBytes = 4096u;
    props.RptrGpuAddress = buffers.RptrGpuAddress;
    props.WptrGpuAddress = buffers.WptrGpuAddress;
    volatile void *doorbellMapped = NULL;
    NTSTATUS dbStatus = STATUS_SUCCESS;

    props.UseDoorbell = 0u;
    props.DoorbellIndex = 0u;
    props.SchedulerMe = 2u;
    props.SchedulerPipe = 0u;
    props.SchedulerQueue = 0u;
    props.PipePriority = 0u;
    props.QueuePriority = 0u;
    props.HqdActive = 1u;

    if (Context->Core.ResolvedBarCount > BC250_ADAPTER_DOORBELL_BAR_INDEX &&
        Context->Core.ResolvedBars[BC250_ADAPTER_DOORBELL_BAR_INDEX].Length >= 4096) {
        PHYSICAL_ADDRESS dbPhysical;
        dbPhysical.QuadPart = (LONGLONG)Context->Core.ResolvedBars[BC250_ADAPTER_DOORBELL_BAR_INDEX].Start;
        dbStatus = Context->Core.DxgkInterface.DxgkCbMapMemory(
            Context->Core.DxgkInterface.DeviceHandle,
            dbPhysical,
            (ULONG)Context->Core.ResolvedBars[BC250_ADAPTER_DOORBELL_BAR_INDEX].Length,
            FALSE,
            FALSE,
            MmNonCached,
            (PVOID *)&doorbellMapped);
        if (NT_SUCCESS(dbStatus) && doorbellMapped != NULL) {
            buffers.DoorbellVirtual = doorbellMapped;
            buffers.DoorbellSizeBytes = (uint32_t)Context->Core.ResolvedBars[BC250_ADAPTER_DOORBELL_BAR_INDEX].Length;
            props.UseDoorbell = 1u;
            /* A transacao deriva o indice KIQ da faixa MEC fisica. */
            props.DoorbellIndex = 0u;
        }
    }

    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        if (doorbellMapped != NULL) {
            (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
                Context->Core.DxgkInterface.DeviceHandle, (PVOID)doorbellMapped);
        }
        (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
            Context->Core.DxgkInterface.DeviceHandle, (PVOID)mapped);
        (void)Bc250VramRelease(allocator, token);
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }

    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = &Context->Core.MmioTransport;

    /*
     * Cyan Skillfish2 (PCI 1002:13FE) usa SMU 11.8. O ABI oficial nao
     * possui AllowGfxOff/DisallowGfxOff e o caminho upstream configura
     * clock/power gating como nao aplicavel. Nao emita mensagens Van Gogh
     * neste ASIC. A protecao real da submissao permanece nos preflights de
     * firmware/MEC, coerencia HDP/L2, readback e tripé fisico.
     */
    if (Context->ShaderDispatch != 0u) {
        status = Bc250ComputeRunObservableShaderDispatchTransaction(
            &interfaceData, &buffers, &props, &mqd, Evidence,
            &Context->HqdShaderDispatchEvidence);
    } else {
        status = Bc250ComputeRunActiveHqdSubmissionTransaction(
            &interfaceData, &buffers, &props, &mqd, Evidence);
    }
    Evidence->GfxOffApplicable = 0u;
    Evidence->GfxOffGuardPassed = 1u;
    Evidence->GfxOffRestorePassed = 1u;

    reads = Context->Core.MmioTransport.ReadCount;
    writes = Context->Core.MmioTransport.WriteCount;
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);

    if (doorbellMapped != NULL) {
        (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
            Context->Core.DxgkInterface.DeviceHandle, (PVOID)doorbellMapped);
    }
    (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
        Context->Core.DxgkInterface.DeviceHandle, (PVOID)mapped);
    (void)Bc250VramRelease(allocator, token);

    if (status == BC250_HQD_ACTIVE_OK &&
        mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        Evidence->PhysicalReady = 0u;
        return BC250_HQD_ACTIVE_RESTORE_FAILED;
    }

    return status;
}

static void Bc250ActiveRecordHqdSubmission(
    const BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    const BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *e;

    if (Context == NULL) return;
    e = &Context->HqdActiveEvidence;
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_STATUS,
        (ULONG)Context->HqdActiveStatus);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SELECTED_SELECTOR,
        e->SelectedSelector);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BEFORE, e->ActiveBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_APPLIED, e->ActiveApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_AFTER, e->ActiveAfter);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_BEFORE, e->RptrBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_AFTER, e->RptrAfter);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_APPLIED, e->WptrApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FENCE_BEFORE, e->FenceBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FENCE_AFTER, e->FenceAfter);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SCRATCH_BEFORE, e->ScratchBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SCRATCH_AFTER, e->ScratchAfter);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_POLL_ITERATIONS, e->PollIterations);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_TRIAD_PASSED, e->TriadPassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RESTORE_PASSED, e->RestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEM_RESTORE_PASSED,
        e->MemoryRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PHYSICAL_READY, e->PhysicalReady);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_RUNG, e->DoorbellRung);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_INDEX, e->DoorbellIndex);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_OFFSET, e->DoorbellOffset);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_VALUE_APPLIED,
        e->DoorbellValueApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_BEFORE,
        e->DoorbellRangeLowerBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_APPLIED,
        e->DoorbellRangeLowerApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_READBACK,
        e->DoorbellRangeLowerReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_BEFORE,
        e->DoorbellRangeUpperBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_APPLIED,
        e->DoorbellRangeUpperApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_READBACK,
        e->DoorbellRangeUpperReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_BEFORE,
        e->CpPqStatusBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_APPLIED,
        e->CpPqStatusApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_READBACK,
        e->CpPqStatusReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_BEFORE,
        e->WptrPollCntlBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_APPLIED,
        e->WptrPollCntlApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_READBACK,
        e->WptrPollCntlReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SELECTOR_RELEASED_BEFORE_KICK,
        e->SelectorReleasedBeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_AFTER_KICK,
        e->CpPqStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_CONTROL_AFTER_KICK,
        e->DoorbellControlAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_AFTER_KICK,
        e->WptrAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_AFTER_KICK,
        e->RptrAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_INITIAL_WPTR_LO,
        e->InitialWptrLo);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_INITIAL_WPTR_HI,
        e->InitialWptrHi);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_APPLIED,
        e->BaselineWptrApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_READBACK,
        e->BaselineWptrReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORED,
        e->BaselineWptrRestored);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORE_POLLS,
        e->BaselineWptrRestorePolls);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORE_PASSED,
        e->BaselineWptrRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_KICK_APPLIED,
        e->DirectWptrKickApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_BEFORE,
        e->DirectWptrBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_READBACK,
        e->DirectWptrReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_RPTR_AFTER_KICK,
        e->DirectRptrAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORED,
        e->DirectWptrRestored);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORE_POLLS,
        e->DirectWptrRestorePolls);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORE_PASSED,
        e->DirectWptrRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_MMIO_LAST,
        e->RptrMmioLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_MMIO_LAST,
        e->WptrMmioLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_POLL_LAST,
        e->HqStatus0PollLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQD_ERROR_POLL_LAST,
        e->HqdErrorPollLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_BEFORE,
        e->RlcCpSchedulersBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_APPLIED,
        e->RlcCpSchedulersApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_READBACK,
        e->RlcCpSchedulersReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_AFTER_KICK,
        e->RlcCpSchedulersAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_RESTORED,
        e->RlcCpSchedulersRestored);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_RESTORE_PASSED,
        e->RlcCpSchedulersRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_AFTER_KICK,
        e->HqStatus0AfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_CONTROL0_AFTER_KICK,
        e->HqControl0AfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQD_ERROR_AFTER_KICK,
        e->HqdErrorAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_UTCL1_ERROR_AFTER_KICK,
        e->CpcUtcl1ErrorAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_UTCL1_STATUS_AFTER_KICK,
        e->CpcUtcl1StatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_UTCL1_STATUS_AFTER_KICK,
        e->CpfUtcl1StatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_INT_STATUS_AFTER_KICK,
        e->CpcIntStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_ME1_PIPE0_INT_STATUS_AFTER_KICK,
        e->Me1Pipe0IntStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_ME2_PIPE1_INT_STATUS_AFTER_KICK,
        e->Me2Pipe1IntStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_ME2_PIPE0_INT_STATUS_AFTER_KICK,
        e->Me2Pipe0IntStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_BASE_LO, e->PqBaseLoApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_BASE_HI, e->PqBaseHiApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_ADDR_LO,
        e->WptrPollAddrLoApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_ADDR_HI,
        e->WptrPollAddrHiApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_SHADOW, e->WptrShadowApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_MEC_CNTL_READBACK,
        e->CpMecCntlReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_FIRST, e->MecPcFirst);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_LAST, e->MecPcLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_TRANSITIONS,
        e->MecPcTransitions);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_SAMPLES, e->MecPcSamples);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_CONTROL_OBSERVED,
        e->PqControlObserved);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_BEFORE,
        e->EopRptrBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_APPLIED,
        e->EopRptrApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_READBACK,
        e->EopRptrReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_RESTORED,
        e->EopRptrRestored);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_RESTORE_PASSED,
        e->EopRptrRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_BEFORE,
        e->WptrPollCntl1Before);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_APPLIED,
        e->WptrPollCntl1Applied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_READBACK,
        e->WptrPollCntl1Readback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_RESTORED,
        e->WptrPollCntl1Restored);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_RESTORE_PASSED,
        e->WptrPollCntl1RestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_QUEUE_MASK_BIT,
        e->QueueMaskBit);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_APPLIED,
        e->PreActiveWptrApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_READBACK,
        e->PreActiveWptrReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_PUBLISHED,
        e->PreActiveWptrPublished);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_STAGE,
        e->FirstRestoreFailureStage);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_OFFSET,
        e->FirstRestoreFailureOffset);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_EXPECTED,
        e->FirstRestoreFailureExpected);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_OBSERVED,
        e->FirstRestoreFailureObserved);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_TRANSPORT,
        e->FirstRestoreFailureTransport);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RESTORE_FAILURE_COUNT,
        e->RestoreFailureCount);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_PROBE_APPLIED,
        e->WptrPollCntlProbeApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_PROBE_READBACK,
        e->WptrPollCntlProbeReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_WRITABLE,
        e->WptrPollCntlWritable);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_EN_LATCHED,
        e->WptrPollCntlEnLatched);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_POST_ACTIVE_WPTR_OBSERVED,
        e->PostActiveWptrObserved);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_DIRTY_ENTRY,
        e->WptrPollCntl1DirtyEntry);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_CLEAR_ATTEMPTS,
        e->WptrPollCntl1ClearAttempts);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_CONTROL_FIRST_SAMPLE,
        e->PqControlFirstSample);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_FIRST_SAMPLE,
        e->HqStatus0FirstSample);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_POLL_LAST,
        e->DequeueRequestPollLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_VMID_POLL_LAST,
        e->VmidPollLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PERSISTENT_STATE_POLL_LAST,
        e->PersistentStatePollLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_REPORT_MEM_FIRST,
        e->RptrReportMemFirst);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_REPORT_MEM_LAST,
        e->RptrReportMemLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_NON_ZERO_SAMPLES,
        e->RptrNonZeroSamples);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_FIRST,
        e->CpcStatusFirst);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_LAST,
        e->CpcStatusLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_BUSY_STAT_LAST,
        e->CpcBusyStatLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_STAT_LAST,
        e->CpfBusyStatLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STALLED_STAT1_LAST,
        e->CpcStalledStat1Last);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_FIRST,
        e->CpStatFirst);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_LAST,
        e->CpStatLast);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STALLED_STAT1_AFTER,
        e->CpStalledStat1After);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP0,
        e->MecMe1HeaderDump[0]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP1,
        e->MecMe1HeaderDump[1]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP2,
        e->MecMe1HeaderDump[2]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP3,
        e->MecMe1HeaderDump[3]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP0,
        e->MecMe2HeaderDump[0]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP1,
        e->MecMe2HeaderDump[1]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP2,
        e->MecMe2HeaderDump[2]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP3,
        e->MecMe2HeaderDump[3]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE0,
        e->MecMe2HeaderDumpBeforeActive[0]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE1,
        e->MecMe2HeaderDumpBeforeActive[1]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE2,
        e->MecMe2HeaderDumpBeforeActive[2]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE3,
        e->MecMe2HeaderDumpBeforeActive[3]);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_BEFORE_KICK,
        e->CpcStatusBeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_BUSY_BEFORE_KICK,
        e->CpcBusyStatBeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_KICK,
        e->CpfBusyStatBeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STALLED_BEFORE_KICK,
        e->CpcStalledStat1BeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_BEFORE_KICK,
        e->CpStatBeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STALLED_BEFORE_KICK,
        e->CpStalledStat1BeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_AFTER_KICK,
        e->CpcStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_BUSY_AFTER_KICK,
        e->CpcBusyStatAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_KICK,
        e->CpfBusyStatAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_ACTIVE,
        e->CpfBusyStatBeforeActive);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_ACTIVE,
        e->CpfBusyStatAfterActive);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_WPTR,
        e->CpfBusyStatBeforeWptr);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_WPTR,
        e->CpfBusyStatAfterWptr);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_BEFORE_ACTIVE,
        e->CpfStalledStat1BeforeActive);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_AFTER_ACTIVE,
        e->CpfStalledStat1AfterActive);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_BEFORE_KICK,
        e->CpfStalledStat1BeforeKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_AFTER_KICK,
        e->CpfStalledStat1AfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_LAST,
        e->CpfStalledStat1Last);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STATUS_AFTER_ACTIVE,
        e->CpfStatusAfterActive);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STATUS_AFTER_KICK,
        e->CpfStatusAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_APPLIED,
        e->DequeueRequestApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_READBACK,
        e->DequeueRequestReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_RESTORE_PASSED,
        e->DequeueRequestRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STALLED_AFTER_KICK,
        e->CpcStalledStat1AfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_AFTER_KICK,
        e->CpStatAfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STALLED_AFTER_KICK,
        e->CpStalledStat1AfterKick);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HDP_INVALIDATE_APPLIED,
        e->HdpInvalidateApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_L2_BEFORE,
        e->GcvmL2Cntl2Before);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_L2_APPLIED,
        e->GcvmL2Cntl2Applied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_L2_READBACK,
        e->GcvmL2Cntl2Readback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_REQ_APPLIED,
        e->GcvmInvalidateRequestApplied);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_REQ_READBACK,
        e->GcvmInvalidateRequestReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_ACK_BEFORE,
        e->GcvmInvalidateAckBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_ACK_AFTER,
        e->GcvmInvalidateAckAfter);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_POLLS,
        e->GcvmInvalidatePolls);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_COHERENCY_PASSED,
        e->CoherencyPassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_APPLICABLE,
        e->GfxOffApplicable);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_BEFORE,
        e->GfxOffStateBefore);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_GUARDED,
        e->GfxOffStateGuarded);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_AFTER,
        e->GfxOffStateAfter);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_DISALLOW_RESPONSE,
        e->GfxOffDisallowResponse);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_ALLOW_RESPONSE,
        e->GfxOffAllowResponse);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_GUARD_PASSED,
        e->GfxOffGuardPassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_RESTORE_PASSED,
        e->GfxOffRestorePassed);
}

static void Bc250ActiveRecordShaderDispatch(
    const BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    const BC250_HQD_SHADER_DISPATCH_EVIDENCE *e;

    if (Context == NULL) return;
    e = &Context->HqdShaderDispatchEvidence;
    Bc250ProbeRecord(BC250_PROBE_SHADER_DISPATCH_ENABLED,
        Context->ShaderDispatch != 0u ? 1u : 0u);
    Bc250ProbeRecord(BC250_PROBE_SHADER_DISPATCH_DWORDS,
        e->Plan.SubmittedDwords);
    Bc250ProbeRecord(BC250_PROBE_SHADER_DISPATCH_PACKET_INDEX,
        e->Plan.DispatchPacketIndex);
    Bc250ProbeRecord(BC250_PROBE_SHADER_FENCE_PACKET_INDEX,
        e->Plan.FencePacketIndex);
    Bc250ProbeRecord(BC250_PROBE_SHADER_FENCE_AFTER_DISPATCH,
        e->Plan.FenceIsAfterDispatch);
    Bc250ProbeRecord(BC250_PROBE_SHADER_CC_ARRAY_PASS1,
        e->CcShaderArrayPass1);
    Bc250ProbeRecord(BC250_PROBE_SHADER_CC_ARRAY_PASS2,
        e->CcShaderArrayPass2);
    Bc250ProbeRecord(BC250_PROBE_SHADER_SPI_WGP_MASK_PASS1,
        e->SpiStaticWgpMaskPass1);
    Bc250ProbeRecord(BC250_PROBE_SHADER_SPI_WGP_MASK_PASS2,
        e->SpiStaticWgpMaskPass2);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RLC_AON_WGP_PASS1,
        e->RlcAlwaysOnWgpMaskPass1);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RLC_AON_WGP_PASS2,
        e->RlcAlwaysOnWgpMaskPass2);
    Bc250ProbeRecord(BC250_PROBE_SHADER_NON_HARVESTED_WGP,
        e->NonHarvestedWgpMask);
    Bc250ProbeRecord(BC250_PROBE_SHADER_AON_NON_HARVESTED_WGP,
        e->AlwaysOnNonHarvestedWgpMask);
    Bc250ProbeRecord(BC250_PROBE_SHADER_WGP_PREFLIGHT_PASSED,
        e->WgpPreflightPassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_GPUVA_LOW,
        e->RingGpuVaLow);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_GPUVA_HIGH,
        e->RingGpuVaHigh);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_PHYSICAL_LOW,
        e->RingPhysicalLow);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_PHYSICAL_HIGH,
        e->RingPhysicalHigh);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_PTE_LOW,
        e->RingPteLow);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_PTE_HIGH,
        e->RingPteHigh);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_TRANSLATION_PASSED,
        e->RingTranslationPassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_READBACK_PASSED,
        e->RingReadbackPassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_READBACK_MISMATCHES,
        e->RingReadbackMismatchCount);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD0, e->RingDword[0]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD1, e->RingDword[1]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD2, e->RingDword[2]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD3, e->RingDword[3]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD4, e->RingDword[4]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD5, e->RingDword[5]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD6, e->RingDword[6]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RING_DWORD7, e->RingDword[7]);
    Bc250ProbeRecord(BC250_PROBE_SHADER_READBACK_PASSED,
        e->ShaderReadbackPassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_READBACK_MISMATCHES,
        e->ShaderReadbackMismatchCount);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RESULT_BEFORE,
        e->Observation.ResultBefore);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RESULT_AFTER,
        e->Observation.ResultAfter);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RESULT_MISMATCHES,
        e->Observation.ResultMismatchCount);
    Bc250ProbeRecord(BC250_PROBE_SHADER_FENCE_BEFORE,
        e->Observation.FenceBefore);
    Bc250ProbeRecord(BC250_PROBE_SHADER_FENCE_AFTER,
        e->Observation.FenceAfter);
    Bc250ProbeRecord(BC250_PROBE_SHADER_RPTR_AFTER,
        e->Observation.RptrAfter);
    Bc250ProbeRecord(BC250_PROBE_SHADER_WPTR_AFTER,
        e->Observation.WptrAfter);
    Bc250ProbeRecord(BC250_PROBE_SHADER_REG_RESTORE,
        e->Observation.RegistersRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_MEM_RESTORE,
        e->Observation.MemoryRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_TRIAD_PASSED,
        e->Verdict.TriadPassed);
    Bc250ProbeRecord(BC250_PROBE_SHADER_PHYSICAL_READY,
        e->PhysicalReady);
}

static void Bc250ActiveRecordHqdWptrDiag(
    const BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    const BC250_HQD_WPTR_DIAGNOSTIC_EVIDENCE *e;

    if (Context == NULL) return;
    e = &Context->HqdWptrDiagEvidence;
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_STATUS,
        (ULONG)Context->HqdWptrDiagStatus);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_ORIGINAL_SELECTOR,
        e->OriginalSelector);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_TARGET_SELECTOR,
        e->TargetSelector);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_ACTIVE_BEFORE,
        e->ActiveBeforePass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_RPTR_BEFORE,
        e->RptrBeforePass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_WPTR_LO_BEFORE,
        e->WptrLoBeforePass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_WPTR_HI_BEFORE,
        e->WptrHiBeforePass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_PQ_CONTROL_BEFORE,
        e->PqControlBeforePass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_STABLE_PREFLIGHT_PASSED,
        e->StablePreflightPassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_TEST_VALUE_WRITTEN,
        e->TestValueWritten);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_FIRST_READBACK,
        e->FirstReadback);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_SERIES_MATCH_INDEX,
        e->SeriesMatchIndex);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_SELECTOR_UNMODIFIED,
        e->SelectorRemainedUnmodified);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_RESTORED_VALUE_OBSERVED,
        e->RestoredValueObserved);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_RESTORE_POLLS,
        e->RestorePolls);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_RESTORE_PASSED,
        e->RestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_ACTIVE_AFTER,
        e->ActiveAfterPass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_WPTR_LO_AFTER,
        e->WptrLoAfterPass1);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_SELECTOR_RESTORE_PASSED,
        e->SelectorRestorePassed);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_READ_CALLS,
        e->ReadCalls);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_WRITE_CALLS,
        e->WriteCalls);
    Bc250ProbeRecord(BC250_PROBE_HQD_WPTR_DIAG_PHYSICAL_READY,
        e->PhysicalReady);
}

static NTSTATUS Bc250ActiveQueryRuntimeCaps(
    HANDLE AdapterContext,
    BC250_MCDM_RUNTIME_CAPS *Caps)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext;
    uint32_t i;
    uint64_t localStart = 0u;
    uint64_t localEnd;
    uint64_t cpuVisibleBytes;
    if (Caps == NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Caps, sizeof(*Caps));
    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }

    if (context->RuntimeInventoryStatus == BC250_RUNTIME_INVENTORY_OK &&
        context->VramProbeStatus == BC250_VRAM_PROBE_OK &&
        context->SecondPreflightStage == BC250_DXGK_STAGE_PREFLIGHT_READY &&
        context->Core.StartDevice.Preflight.FirmwareCount ==
            BC250_CP_BOOTSTRAP_FIRMWARE_COUNT) {
        cpuVisibleBytes = context->RuntimeInventoryReport.CpuVisibleBytes;
        localEnd = context->Core.StartDevice.Preflight.FirmwarePlans[0]
            .Binding.Offset;
        if (localEnd == 0u || localEnd > cpuVisibleBytes) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        /*
         * MEDIDO NA CORRIDA 08/09 12:36 (Codigo 43, RuntimeCapsStatus=0xC0000182).
         *
         * Este laco tratava TODA reserva como PREFIXO a pular, empurrando
         * localStart para depois do fim dela. A premissa vale para o scanout,
         * que nasce no offset 0 -- e e' FALSA para qualquer reserva alocada
         * TOP_DOWN. A dummy page da MMU global e' exatamente isso: ela cai em
         * 268.431.360 e termina em 268.435.456, o topo da janela de CPU, e o
         * laco levava localStart ao topo, acima de localEnd (268.161.024, o
         * offset do plano de firmware). Resultado: segmento vazio e recusa.
         *
         * O defeito e' ANTERIOR a faixa A16: este ramo exige
         * SecondPreflightStage == PREFLIGHT_READY e, ate a A16 destravar o
         * segundo preflight, ele NUNCA tinha executado num bind real.
         *
         * Regra correta, e que preserva a intencao original de entregar um
         * segmento CONTIGUO e sem buracos:
         *   - reserva que toca o INICIO empurra localStart (prefixo);
         *   - reserva ACIMA do inicio trunca o FIM (sufixo).
         * O passe de prefixo repete ate estabilizar porque a lista nao e'
         * ordenada: uma reserva pode virar prefixo depois que outra avanca
         * localStart. O teto de passes e' o proprio numero de reservas.
         */
        {
            uint32_t pass;
            for (pass = 0u;
                 pass < context->RuntimeInventoryReport.ReservationCount;
                 ++pass) {
                uint64_t startBefore = localStart;
                for (i = 0u;
                     i < context->RuntimeInventoryReport.ReservationCount;
                     ++i) {
                    const BC250_INHERITED_VRAM_RESERVATION *reservation =
                        &context->RuntimeInventoryReport.Reservations[i];
                    uint64_t reservationEnd;
                    if (reservation->Offset >= cpuVisibleBytes) continue;
                    if (reservation->Offset > localStart) continue;
                    reservationEnd = reservation->Length >
                        cpuVisibleBytes - reservation->Offset ?
                        cpuVisibleBytes :
                        reservation->Offset + reservation->Length;
                    if (reservationEnd > localStart) {
                        localStart = reservationEnd;
                    }
                }
                if (localStart == startBefore) break;
            }
            for (i = 0u;
                 i < context->RuntimeInventoryReport.ReservationCount;
                 ++i) {
                const BC250_INHERITED_VRAM_RESERVATION *reservation =
                    &context->RuntimeInventoryReport.Reservations[i];
                if (reservation->Offset >= cpuVisibleBytes) continue;
                if (reservation->Offset <= localStart) continue;
                if (reservation->Offset < localEnd) {
                    localEnd = reservation->Offset;
                }
            }
        }
        if (localStart >= localEnd) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    } else if (context->Core.ResolvedBarCount != 0u &&
               context->Core.TopologyEvidence.AddressSpace.TotalBytes != 0u) {
        uint64_t totalBytes = context->Core.TopologyEvidence.AddressSpace.TotalBytes;
        cpuVisibleBytes = context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes;
        if (cpuVisibleBytes == 0u) cpuVisibleBytes = totalBytes;
        if (context->PostDisplayReservation.Length != 0u) {
            localStart = (context->PostDisplayReservation.Offset +
                          context->PostDisplayReservation.Length + 0xFFFFull) & ~0xFFFFull;
        } else {
            localStart = 0x01000000ull;
        }
        /*
         * O BAR 0 físico de VRAM possui 256 MB (RebarAssignedMiB=256).
         * localEnd DEVE ser restrito a cpuVisibleBytes para que todo o segmento 1
         * (e especialmente as Page Tables alocadas pelo VidMm no topo do segmento)
         * resida dentro da abertura física do BAR 0, sem cair em espaço fantasma (0xFF).
         */
        localEnd = (cpuVisibleBytes != 0u && cpuVisibleBytes < totalBytes) ?
            cpuVisibleBytes : totalBytes;
        if (localStart >= localEnd) {
            localStart = 0u;
        }
    } else {
        return STATUS_DEVICE_NOT_READY;
    }

    for (i = 0u; i < context->Core.ResolvedBarCount; ++i) {
        if (context->Core.ResolvedBars[i].Index ==
            BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            Caps->CpuPhysicalStart =
                context->Core.ResolvedBars[i].Start + localStart;
            break;
        }
    }
    if (i == context->Core.ResolvedBarCount || Caps->CpuPhysicalStart == 0u) {
        RtlZeroMemory(Caps, sizeof(*Caps));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    /*
     * Isolamento rigoroso de estruturas internas do driver no topo da VRAM:
     * O topo da abertura de CPU e' reservado para o SDMA ring (0x0FF00000),
     * motor de submissao persistente MEC (top-down) e dummy page da MMU.
     * Capping localEnd em 0x0FE00000 (254 MB) garante um airgap fisico absoluto:
     * VidMm NUNCA recebera memoria onde vivem os aneis e writebacks da GPU.
     */
    if (localEnd > 0x0FE00000ull) {
        localEnd = 0x0FE00000ull;
    }
    if (context->GlobalMmuDummyAllocated != FALSE &&
        context->GlobalMmuDummyOffset < localEnd) {
        localEnd = context->GlobalMmuDummyOffset;
    }
    if (context->SubmissionEngine.TokenValid != 0u) {
        BC250_VRAM_REGION engReg;
        if (Bc250VramGetRegion(&context->Core.StartDevice.Owned.Bootstrap.Allocator,
                               context->SubmissionEngine.Token, &engReg) == BC250_VRAM_OK) {
            if (engReg.Offset < localEnd) {
                localEnd = engReg.Offset;
            }
        }
    }
    if (localStart >= localEnd) {
        RtlZeroMemory(Caps, sizeof(*Caps));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Caps->LocalSegmentBaseOffset = localStart;
    Caps->LocalSegmentBytes = localEnd - localStart;
    /*
     * C3.6: ponto UNICO de publicacao da geometria do segmento para o caminho de
     * display. Fica aqui, e nao num sitio de chamada, porque assim toda resolucao
     * bem-sucedida de caps atualiza os dois consumidores de uma vez -- e' a
     * lição das quatro divergencias de fonte unica (A1, A11, ContextAbi, C3.4).
     * DxgkDdiPresent nao tem o contexto do adaptador em maos e precisa deste dado
     * para converter offset de segmento; sem ele supunha offset cru, o que
     * produziu o BugCheck 0x119 da corrida 13:11.
     */
    Bc250SdmaPresentSetSegmentGeometry(Caps->LocalSegmentBaseOffset,
                                       Caps->LocalSegmentBytes);
    /*
     * Deliberadamente NAO usamos
     * Bootstrap.AddressSpace.VramBaseOffset aqui. Ver o comentario de
     * BC250_GCVM_VRAM_MC_BASE_FALLBACK: aquele valor (0x450000000) nunca fez
     * um acesso funcionar, enquanto esta base tem prova byte a byte. O valor
     * do leitor de topologia continua sendo logado em ROOT_MC para comparacao.
     */
    Caps->VramMcBase = BC250_GCVM_VRAM_MC_BASE_FALLBACK;
    Caps->NodeCount = BC250_ADAPTER_NODE_COUNT;
    Caps->Ready = 1u;
    Caps->FullMcdmReady = 1u;
    Caps->TdrReady = 1u;
    Caps->PreemptionReady = 1u;
    Caps->SubmitReady = 1u;
    Caps->HistoryBufferReady = 1u;

    /*
     * Geometria da page table. Descritiva, nao e' declaracao de prontidao:
     * pode ser preenchida sempre, porque so' e' consumida quando GpuVaReady
     * estiver ligado. Constantes com proveniencia em
     * bc250_gpuva_space_contract.h (PAGE_TABLE_DEPTH=0 do catalogo).
     */
    Caps->GpuVaBitCount = BC250_GPUVA_PT_VA_BITS;
    Caps->PageTableLevelCount = BC250_GPUVA_PT_LEVEL_COUNT;
    Caps->PageTableIndexBitCount = BC250_GPUVA_PT_INDEX_BITS;
    Caps->RootPageTableIndexBitCount = BC250_GPUVA_PT_ROOT_INDEX_BITS;
    Caps->PageTableSizeBytes = BC250_GPUVA_PT_SIZE_BYTES;
    Caps->PageTableAlignmentBytes = BC250_GPUVA_PT_ALIGN_BYTES;

    /*
     * GpuVaReady/PagingReady eram os unicos campos da struct que ninguem
     * atribuia -- por OMISSAO, nao por gate. Isso derrubava
     * MemoryManagementCaps.GpuMmuSupported para 0, e sem modelo de memoria o
     * dxgkrnl nunca chama CreateAllocation, nunca cria a primary allocation e
     * entrega ao CommitVidPn um VidPN em branco. Cadeia medida na corrida
     * 15:10:53 de 03/09.
     *
     * Agora o zero e' DELIBERADO e reversivel por tunable. Fail-closed por
     * default: sem GpuMmuDeclare=1 o adaptador se comporta exatamente como
     * na corrida medida. A promocao para 1 e' o ensaio do item 35 (map/unmap
     * com invalidacao de TLB e readback), e nao pode ser feita as cegas.
     */
    /*
     * A condicao de TlbTransactionEvidence.PhysicalReady foi REMOVIDA: essa
     * evidencia so' e' produzida pelo bring-up de laboratorio, e o caminho de
     * producao exclui transacoes GCVM de proposito. Exigi-la tornava o declare
     * inalcancavel por construcao no unico perfil que roda de verdade.
     *
     * Declarar GpuMmuSupported afirma que as DDIs do modelo estao
     * implementadas -- e estao: UPDATE_PAGE_TABLE, FLUSH_TLB, VIRTUAL_TRANSFER
     * e VIRTUAL_FILL tem handler real. A PROVA de que funcionam e' a corrida,
     * nao um pre-requisito dela. O gate honesto e' o tunable: default 0,
     * explicito no INF, auditavel, e sem ele nada muda.
     */
    if (context->GpuMmuDeclarePresent == 1u &&
        context->GpuMmuDeclare == 1u) {
        Caps->GpuVaReady = 1u;
        Caps->PagingReady = 1u;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ActiveEnsureGlobalMmu(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    const char *Origin)
{
    BC250_GCVM_GLOBAL_MMU_CONFIG config;
    BC250_GCVM_GLOBAL_MMU_INTERFACE io;
    BC250_VRAM_ALLOCATOR *allocator;
    BC250_VRAM_REGION region;
    BC250_VRAM_STATUS vramStatus;
    BC250_VRAM_TOKEN inheritedToken;
    uint64_t topologyBase;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Context->GlobalMmuReady) return STATUS_SUCCESS;
    if (Context->Core.MmioTransport.State != BC250_MMIO_TOPOLOGY_MAPPED) {
        Bc250DisplayLog("[%s] Global GFXHUB MMU RECUSADA: transporte MMIO state=%u\n",
            Origin, Context->Core.MmioTransport.State);
        return STATUS_DEVICE_NOT_READY;
    }

    /*
     * A primeira auditoria readonly termina em RUNTIME_MISSING e ainda nao
     * criou o allocator do bootstrap. A corrida 15:16 mediu exatamente isso:
     * TotalBytes=0 e Bc250VramAllocate devolveu NO_SPACE. A dummy global tem
     * ownership proprio; o allocator dedicado nasce da topologia capturada e
     * reserva o scanout herdado antes da busca top-down.
     */
    allocator = &Context->GlobalMmuAllocator;
    if (allocator->TotalBytes == 0ull) {
        vramStatus = Bc250VramAllocatorInit(allocator,
            Context->Core.TopologyEvidence.AddressSpace.TotalBytes,
            Context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes);
        if (vramStatus != BC250_VRAM_OK) {
            Bc250DisplayLog("[%s] Global GFXHUB MMU RECUSADA: allocator init status=%u total=0x%llX cpu=0x%llX\n",
                Origin, (uint32_t)vramStatus,
                (unsigned long long)Context->Core.TopologyEvidence.AddressSpace.TotalBytes,
                (unsigned long long)Context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        if (Context->PostDisplayStatus == BC250_POST_DISPLAY_OK &&
            Context->PostDisplayReservation.Length != 0ull) {
            RtlZeroMemory(&inheritedToken, sizeof(inheritedToken));
            vramStatus = Bc250VramReserveFixed(allocator,
                Context->PostDisplayReservation.Offset,
                Context->PostDisplayReservation.Length,
                Context->PostDisplayReservation.Alignment,
                BC250_VRAM_ROLE_SCANOUT,
                BC250_VRAM_ALLOC_CPU_VISIBLE |
                    BC250_VRAM_REGION_PERSISTENT,
                &inheritedToken);
            if (vramStatus != BC250_VRAM_OK) {
                RtlZeroMemory(allocator, sizeof(*allocator));
                Bc250DisplayLog("[%s] Global GFXHUB MMU RECUSADA: reserva do scanout status=%u off=0x%llX len=0x%llX\n",
                    Origin, (uint32_t)vramStatus,
                    (unsigned long long)Context->PostDisplayReservation.Offset,
                    (unsigned long long)Context->PostDisplayReservation.Length);
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
        }
    }
    if (!Context->GlobalMmuDummyAllocated) {
        RtlZeroMemory(&Context->GlobalMmuDummyToken,
            sizeof(Context->GlobalMmuDummyToken));
        vramStatus = Bc250VramAllocate(allocator, 0x1000ull, 0x1000ull,
            BC250_VRAM_ROLE_PAGE_TABLE,
            BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE,
            &Context->GlobalMmuDummyToken);
        if (vramStatus != BC250_VRAM_OK ||
            Bc250VramGetRegion(allocator, Context->GlobalMmuDummyToken,
                &region) != BC250_VRAM_OK ||
            region.Length != 0x1000ull ||
            (region.Offset & 0xFFFull) != 0ull ||
            allocator->CpuVisibleBytes < 0x1000ull ||
            region.Offset > allocator->CpuVisibleBytes - 0x1000ull) {
            if (vramStatus == BC250_VRAM_OK) {
                (void)Bc250VramRelease(allocator,
                    Context->GlobalMmuDummyToken);
            }
            RtlZeroMemory(&Context->GlobalMmuDummyToken,
                sizeof(Context->GlobalMmuDummyToken));
            Bc250DisplayLog("[%s] Global GFXHUB MMU RECUSADA: dummy page sem reserva valida (vramStatus=%u)\n",
                Origin, (uint32_t)vramStatus);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Context->GlobalMmuDummyOffset = region.Offset;
        Context->GlobalMmuDummyAllocated = TRUE;
    }

    topologyBase =
        Context->Core.TopologyEvidence.AddressSpace.VramBaseOffset;
    if (topologyBase != 0ull &&
        topologyBase != BC250_GCVM_VRAM_MC_BASE_FALLBACK) {
        Bc250DisplayLog("[%s] Global GFXHUB MMU RECUSADA: base MC medida 0x%llX diverge de 0x%llX\n",
            Origin, (unsigned long long)topologyBase,
            (unsigned long long)BC250_GCVM_VRAM_MC_BASE_FALLBACK);
        (void)Bc250VramRelease(allocator, Context->GlobalMmuDummyToken);
        Context->GlobalMmuDummyAllocated = FALSE;
        Context->GlobalMmuDummyOffset = 0ull;
        RtlZeroMemory(&Context->GlobalMmuDummyToken,
            sizeof(Context->GlobalMmuDummyToken));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    RtlZeroMemory(&config, sizeof(config));
    config.DummyPagePhysical = BC250_GCVM_VRAM_MC_BASE_FALLBACK +
        Context->GlobalMmuDummyOffset;
    RtlZeroMemory(&io, sizeof(io));
    io.Read32 = Bc250ActiveVmidRead32;
    io.Write32 = Bc250ActiveVmidWrite32;
    io.Flush = Bc250ActiveFlushMappedGfxhubTlb;
    io.Context = Context;
    io.Identity = BC250_GCVM_GLOBAL_MMU_IDENTITY;

    RtlZeroMemory(&Context->GlobalMmuEvidence,
        sizeof(Context->GlobalMmuEvidence));
    Context->GlobalMmuStatus = Bc250GcvmGlobalMmuProgram(
        &config, &io, &Context->GlobalMmuEvidence);
    if (Context->GlobalMmuStatus != BC250_GCVM_GLOBAL_MMU_OK) {
        Bc250DisplayLog("[%s] Global GFXHUB MMU FALHOU: status=%u step=%u reg=0x%05X val=0x%08X rollback=%u/%u\n",
            Origin, (uint32_t)Context->GlobalMmuStatus,
            Context->GlobalMmuEvidence.FailureStep,
            Context->GlobalMmuEvidence.FailureRegister,
            Context->GlobalMmuEvidence.FailureValue,
            Context->GlobalMmuEvidence.RollbackPassed,
            Context->GlobalMmuEvidence.RollbackFailureCount);
        (void)Bc250VramRelease(allocator, Context->GlobalMmuDummyToken);
        Context->GlobalMmuDummyAllocated = FALSE;
        Context->GlobalMmuDummyOffset = 0ull;
        RtlZeroMemory(&Context->GlobalMmuDummyToken,
            sizeof(Context->GlobalMmuDummyToken));
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    Context->GlobalMmuReady = TRUE;
    Bc250DisplayLog("[%s] Global GFXHUB MMU initialized: Regs=%u TLB=0x%08X L2=0x%08X DummyPA=0x%llX\n",
        Origin, Context->GlobalMmuEvidence.RegisterCount,
        BC250_GCVM_GLOBAL_TLB_CNTL_TARGET,
        BC250_GCVM_GLOBAL_L2_CNTL_TARGET,
        (unsigned long long)config.DummyPagePhysical);
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ActiveRegisterAdapter(
    BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    uint32_t index;
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        if (g_Bc250AdapterDirectory[index] == NULL) {
            g_Bc250AdapterDirectory[index] = Context;
            status = STATUS_SUCCESS;
            break;
        }
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return status;
}

static BC250_PSP_LOAD_FW_STATUS Bc250ActiveLoadSdmaFirmware(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PSP_LOAD_FW_EVIDENCE *Evidence)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    static const unsigned char expectedDigest[BC250_FIRMWARE_SHA256_BYTES] = {
        0x15,0xD0,0xD3,0x62,0x6D,0xA7,0xF2,0x51,
        0x3F,0x03,0xB1,0x3B,0xB7,0xE0,0x2E,0xEE,
        0xEC,0xCA,0xB2,0x76,0x27,0x6A,0xE2,0x7D,
        0x0B,0x60,0x67,0xB5,0xF2,0x5E,0x9E,0x95
    };
    static const unsigned char expectedPayloadDigest[BC250_FIRMWARE_SHA256_BYTES] = {
        0xAC,0xBA,0x51,0xD3,0xD7,0x34,0x63,0x7F,
        0x5A,0xCB,0xBA,0xBF,0x3A,0x7D,0xA5,0xCE,
        0xCD,0xC4,0x90,0x96,0x28,0xD7,0x9A,0x40,
        0xF2,0x9F,0x5A,0x39,0xEB,0x13,0x38,0xFE
    };
    UNICODE_STRING path = RTL_CONSTANT_STRING(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_sdma.bin");
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK ioStatus;
    FILE_STANDARD_INFORMATION fileInfo;
    HANDLE file = NULL;
    PHYSICAL_ADDRESS low, high, boundary;
    PVOID ring = NULL, command = NULL, fence = NULL, firmware = NULL;
    PHYSICAL_ADDRESS ringPa, commandPa, fencePa, firmwarePa;
    unsigned char digest[BC250_FIRMWARE_SHA256_BYTES];
    BC250_PSP_LOAD_FW_INTERFACE interfaceData;
    BC250_SDMA_F32_INTERFACE f32Interface;
    BC250_PSP_LOAD_FW_STATUS status = BC250_PSP_LOAD_FW_BAD_ARGUMENT;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    NTSTATUS ntStatus;
    uint32_t safeToFree = 1u;

    if (Context == NULL || Evidence == NULL) return status;
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    InitializeObjectAttributes(&attributes, &path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    ntStatus = ZwCreateFile(&file, GENERIC_READ | SYNCHRONIZE, &attributes,
        &ioStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0u);
    if (!NT_SUCCESS(ntStatus)) {
        Bc250DisplayLog("[SdmaPresent] ZwCreateFile failed: 0x%08X\n", ntStatus);
        return status;
    }
    ntStatus = ZwQueryInformationFile(file, &ioStatus, &fileInfo,
        sizeof(fileInfo), FileStandardInformation);
    if (!NT_SUCCESS(ntStatus) || fileInfo.EndOfFile.QuadPart != 33792) {
        Bc250DisplayLog("[SdmaPresent] ZwQueryInformationFile failed: 0x%08X, size=%lld\n",
            ntStatus, fileInfo.EndOfFile.QuadPart);
        ZwClose(file);
        return status;
    }
    low.QuadPart = 0;
    high.QuadPart = 0xFFFFFFFFFFull;
    boundary.QuadPart = 0;
    ring = MmAllocateContiguousMemorySpecifyCache(
        BC250_PSP_RING_BYTES, low, high, boundary, MmNonCached);
    command = MmAllocateContiguousMemorySpecifyCache(
        BC250_PSP_LOAD_FW_COMMAND_BYTES, low, high, boundary, MmNonCached);
    fence = MmAllocateContiguousMemorySpecifyCache(
        PAGE_SIZE, low, high, boundary, MmNonCached);
    firmware = MmAllocateContiguousMemorySpecifyCache(
        33792u, low, high, boundary, MmNonCached);
    if (ring == NULL || command == NULL || fence == NULL || firmware == NULL) {
        Bc250DisplayLog("[SdmaPresent] Contiguous memory alloc failed for PSP FW buffers\n");
        goto Cleanup;
    }
    RtlZeroMemory(ring, BC250_PSP_RING_BYTES);
    RtlZeroMemory(command, BC250_PSP_LOAD_FW_COMMAND_BYTES);
    RtlZeroMemory(fence, PAGE_SIZE);
    RtlZeroMemory(firmware, 33792u);
    ntStatus = ZwReadFile(file, NULL, NULL, NULL, &ioStatus, firmware,
        33792u, NULL, NULL);
    if (!NT_SUCCESS(ntStatus) || ioStatus.Information != 33792u) {
        Bc250DisplayLog("[SdmaPresent] ZwReadFile failed: 0x%08X, bytes=%llu\n",
            ntStatus, (ULONGLONG)ioStatus.Information);
        goto Cleanup;
    }
    Bc250FirmwareSha256(firmware, 33792u, digest);
    if (!Bc250FirmwareDigestEqual(digest, expectedDigest)) {
        Bc250DisplayLog("[SdmaPresent] Firmware header SHA-256 mismatch!\n");
        goto Cleanup;
    }
    RtlMoveMemory(firmware, (PUCHAR)firmware + 0x100u, 33536u);
    RtlZeroMemory((PUCHAR)firmware + 33536u, 0x100u);
    Bc250FirmwareSha256(firmware, 33536u, digest);
    if (!Bc250FirmwareDigestEqual(digest, expectedPayloadDigest)) {
        Bc250DisplayLog("[SdmaPresent] Firmware payload SHA-256 mismatch!\n");
        goto Cleanup;
    }
    ringPa = MmGetPhysicalAddress(ring);
    commandPa = MmGetPhysicalAddress(command);
    fencePa = MmGetPhysicalAddress(fence);
    firmwarePa = MmGetPhysicalAddress(firmware);
    if (ringPa.QuadPart == 0 || commandPa.QuadPart == 0 ||
        fencePa.QuadPart == 0 || firmwarePa.QuadPart == 0) goto Cleanup;
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        status = BC250_PSP_LOAD_FW_READ_FAILED;
        goto Cleanup;
    }
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250ActiveVmidRead32;
    interfaceData.Write32 = Bc250ActiveVmidWrite32;
    interfaceData.Stall = Bc250ActiveTlbStall;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = Context;
    interfaceData.Identity = BC250_PSP_LOAD_FW_IDENTITY;
    status = Bc250RunPspLoadFirmwareTransaction(
        (uint64_t)ringPa.QuadPart, (uint32_t *)ring,
        (uint64_t)commandPa.QuadPart, (uint32_t *)command,
        (uint64_t)fencePa.QuadPart, (volatile uint32_t *)fence,
        (uint64_t)firmwarePa.QuadPart, 33536u,
        BC250_PSP_LOAD_FW_SDMA0_TYPE, &interfaceData, 500u, Evidence);
    Bc250DisplayLog("[SdmaPresent] Bc250RunPspLoadFirmwareTransaction status=%u, PhysReady=%u\n",
        (uint32_t)status, Evidence->PhysicalReady);
    if (status == BC250_PSP_LOAD_FW_OK && Evidence->PhysicalReady != 0u) {
        RtlZeroMemory(&f32Interface, sizeof(f32Interface));
        f32Interface.Read32 = Bc250ActiveVmidRead32;
        f32Interface.Write32 = Bc250ActiveVmidWrite32;
        f32Interface.Stall = Bc250ActiveTlbStall;
        f32Interface.Context = Context;
        f32Interface.Identity = BC250_SDMA_F32_IDENTITY;
        Context->SdmaF32Status = Bc250RunSdmaF32Transaction(
            &f32Interface, &Context->SdmaF32Evidence);
        Bc250DisplayLog("[SdmaPresent] F32Status=%u (PhysReady=%u)\n",
            (uint32_t)Context->SdmaF32Status,
            Context->SdmaF32Evidence.PhysicalReady);
        if (Context->SdmaF32Status == BC250_SDMA_F32_OK &&
            Context->SdmaF32Evidence.PhysicalReady != 0u) {
            Context->SdmaRingWorkStatus = Bc250ActiveRunSdmaRingWork(
                Context, &Context->SdmaRingWorkEvidence);
            /*
             * A corrida 09:04 devolveu RingWorkStatus=2 (ALLOCATION_FAILED) e
             * o log nao dizia qual das duas tentativas falhou: a reserva fixa
             * em BC250_SDMA_LEGACY_RING_OFFSET ou o fallback TOP_DOWN
             * CPU_VISIBLE.  Sem esses dois campos a causa nao e' decidivel a
             * partir da evidencia arquivada.
             */
            Bc250DisplayLog("[SdmaPresent] RingWorkStatus=%u (FixedReservation=%u, AllocationMode=%u, RingMc=0x%llX, WorkMc=0x%llX)\n",
                (uint32_t)Context->SdmaRingWorkStatus,
                Context->SdmaRingWorkEvidence.FixedReservationStatus,
                Context->SdmaRingWorkEvidence.AllocationMode,
                (unsigned long long)Context->SdmaRingWorkEvidence.RingMcAddress,
                (unsigned long long)Context->SdmaRingWorkEvidence.WorkMcAddress);
        }
    }
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK &&
        status == BC250_PSP_LOAD_FW_OK) {
        status = BC250_PSP_LOAD_FW_RESTORE_FAILED;
        Evidence->PhysicalReady = 0u;
    }
    if (Evidence->CreatePassed != 0u && Evidence->DestroyPassed == 0u)
        safeToFree = 0u;
Cleanup:
    if (file != NULL) ZwClose(file);
    if (safeToFree != 0u) {
        if (firmware != NULL) MmFreeContiguousMemory(firmware);
        if (fence != NULL) MmFreeContiguousMemory(fence);
        if (command != NULL) MmFreeContiguousMemory(command);
        if (ring != NULL) MmFreeContiguousMemory(ring);
    } else {
        Context->DeferredPspLoadRing = ring;
        Context->DeferredPspLoadCommand = command;
        Context->DeferredPspLoadFence = fence;
        Context->DeferredPspLoadFirmware = firmware;
    }
    return status;
#else
    UNREFERENCED_PARAMETER(Context);
    if (Evidence != NULL) RtlZeroMemory(Evidence, sizeof(*Evidence));
    return BC250_PSP_LOAD_FW_BAD_ARGUMENT;
#endif
}

static BC250_PSP_LOAD_FW_STATUS Bc250ActiveLoadRlcFirmware(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PSP_LOAD_FW_EVIDENCE *Evidence,
    BC250_RLC_LOAD_EVIDENCE *RlcEvidence)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    static const unsigned char expectedDigest[BC250_FIRMWARE_SHA256_BYTES] = {
        0x20,0xAC,0xEF,0xDB,0x61,0x28,0xA3,0x62,
        0x75,0xF4,0x38,0x24,0x25,0xA1,0x09,0xA7,
        0xC9,0x92,0x7F,0x60,0x53,0xEA,0xB6,0x85,
        0xBB,0x51,0x16,0x4F,0xF1,0xD1,0x8C,0xFB
    };
    static const unsigned char expectedPayloadDigest[BC250_FIRMWARE_SHA256_BYTES] = {
        0x16,0x79,0x4B,0x48,0x17,0xCC,0xB8,0xE8,
        0x2E,0x45,0xFE,0x35,0x24,0xAE,0xC7,0xA8,
        0xA0,0xE5,0x8D,0x03,0x2E,0x76,0xAC,0x1C,
        0x33,0x85,0xC4,0x17,0xA1,0x45,0xBE,0x48
    };
    UNICODE_STRING path = RTL_CONSTANT_STRING(
        L"\\SystemRoot\\System32\\drivers\\cyan_skillfish2_rlc.bin");
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK ioStatus;
    FILE_STANDARD_INFORMATION fileInfo;
    HANDLE file = NULL;
    PHYSICAL_ADDRESS low, high, boundary;
    PVOID ring = NULL, command = NULL, fence = NULL, firmware = NULL;
    PHYSICAL_ADDRESS ringPa, commandPa, fencePa, firmwarePa;
    unsigned char digest[BC250_FIRMWARE_SHA256_BYTES];
    BC250_PSP_LOAD_FW_INTERFACE interfaceData;
    BC250_PSP_LOAD_FW_STATUS status = BC250_PSP_LOAD_FW_BAD_ARGUMENT;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    NTSTATUS ntStatus;
    uint32_t cntlReadback = 0u, safeToFree = 1u;
    const uint32_t rlcCntl = 0x0003B000u;
    const uint32_t rlcGpmStat = 0x0003B9B8u;
    const uint32_t execRomMask = 0x00400000u;

    if (Context == NULL || Evidence == NULL || RlcEvidence == NULL)
        return status;
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    RtlZeroMemory(RlcEvidence, sizeof(*RlcEvidence));
    InitializeObjectAttributes(&attributes, &path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    ntStatus = ZwCreateFile(&file, GENERIC_READ | SYNCHRONIZE, &attributes,
        &ioStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0u);
    if (!NT_SUCCESS(ntStatus)) return status;
    ntStatus = ZwQueryInformationFile(file, &ioStatus, &fileInfo,
        sizeof(fileInfo), FileStandardInformation);
    if (!NT_SUCCESS(ntStatus) || fileInfo.EndOfFile.QuadPart != 25344) {
        ZwClose(file);
        return status;
    }
    low.QuadPart = 0;
    high.QuadPart = 0xFFFFFFFFFFull;
    boundary.QuadPart = 0;
    ring = MmAllocateContiguousMemorySpecifyCache(
        BC250_PSP_RING_BYTES, low, high, boundary, MmNonCached);
    command = MmAllocateContiguousMemorySpecifyCache(
        BC250_PSP_LOAD_FW_COMMAND_BYTES, low, high, boundary, MmNonCached);
    fence = MmAllocateContiguousMemorySpecifyCache(
        PAGE_SIZE, low, high, boundary, MmNonCached);
    firmware = MmAllocateContiguousMemorySpecifyCache(
        25344u, low, high, boundary, MmNonCached);
    if (ring == NULL || command == NULL || fence == NULL || firmware == NULL)
        goto Cleanup;
    RtlZeroMemory(ring, BC250_PSP_RING_BYTES);
    RtlZeroMemory(command, BC250_PSP_LOAD_FW_COMMAND_BYTES);
    RtlZeroMemory(fence, PAGE_SIZE);
    RtlZeroMemory(firmware, 25344u);
    ntStatus = ZwReadFile(file, NULL, NULL, NULL, &ioStatus, firmware,
        25344u, NULL, NULL);
    if (!NT_SUCCESS(ntStatus) || ioStatus.Information != 25344u) goto Cleanup;
    Bc250FirmwareSha256(firmware, 25344u, digest);
    if (!Bc250FirmwareDigestEqual(digest, expectedDigest)) goto Cleanup;
    RtlMoveMemory(firmware, (PUCHAR)firmware + 0x100u, 25088u);
    RtlZeroMemory((PUCHAR)firmware + 25088u, 0x100u);
    Bc250FirmwareSha256(firmware, 25088u, digest);
    if (!Bc250FirmwareDigestEqual(digest, expectedPayloadDigest)) goto Cleanup;
    ringPa = MmGetPhysicalAddress(ring);
    commandPa = MmGetPhysicalAddress(command);
    fencePa = MmGetPhysicalAddress(fence);
    firmwarePa = MmGetPhysicalAddress(firmware);
    if (ringPa.QuadPart == 0 || commandPa.QuadPart == 0 ||
        fencePa.QuadPart == 0 || firmwarePa.QuadPart == 0) goto Cleanup;
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        status = BC250_PSP_LOAD_FW_READ_FAILED;
        goto Cleanup;
    }
    if (!Bc250ActiveVmidRead32(Context, rlcCntl, &RlcEvidence->CntlBefore) ||
        !Bc250ActiveVmidRead32(Context, rlcGpmStat,
            &RlcEvidence->GpmStatBefore)) {
        status = BC250_PSP_LOAD_FW_READ_FAILED;
        goto Unmap;
    }
    RlcEvidence->ExecRomBefore =
        (RlcEvidence->GpmStatBefore & execRomMask) != 0u;
    if (RlcEvidence->ExecRomBefore == 0u) {
        RlcEvidence->AlreadyActive = 1u;
        status = BC250_PSP_LOAD_FW_OK;
        Evidence->FirmwareType = BC250_PSP_LOAD_FW_RLC_TYPE;
        if (Bc250ActiveVmidWrite32(Context, rlcCntl,
                RlcEvidence->CntlBefore | 1u) &&
            Bc250ActiveVmidRead32(Context, rlcCntl,
                &RlcEvidence->CntlAfter) &&
            Bc250ActiveVmidRead32(Context, rlcGpmStat,
                &RlcEvidence->GpmStatAfter)) {
            RlcEvidence->ExecRomAfter =
                (RlcEvidence->GpmStatAfter & execRomMask) != 0u;
            RlcEvidence->EnableF32After = RlcEvidence->CntlAfter & 1u;
            if (RlcEvidence->ExecRomAfter == 0u &&
                RlcEvidence->EnableF32After != 0u) {
                RlcEvidence->PhysicalReady = 1u;
                Evidence->PhysicalReady = 1u;
            }
        }
        goto RestoreRlc;
    }
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250ActiveVmidRead32;
    interfaceData.Write32 = Bc250ActiveVmidWrite32;
    interfaceData.Stall = Bc250ActiveTlbStall;
    interfaceData.Barrier = Bc250ActivePspBarrier;
    interfaceData.Context = Context;
    interfaceData.Identity = BC250_PSP_LOAD_FW_IDENTITY;
    status = Bc250RunPspLoadFirmwareTransaction(
        (uint64_t)ringPa.QuadPart, (uint32_t *)ring,
        (uint64_t)commandPa.QuadPart, (uint32_t *)command,
        (uint64_t)fencePa.QuadPart, (volatile uint32_t *)fence,
        (uint64_t)firmwarePa.QuadPart, 25088u,
        BC250_PSP_LOAD_FW_RLC_TYPE, &interfaceData, 500u, Evidence);
    if (status == BC250_PSP_LOAD_FW_OK && Evidence->PhysicalReady != 0u &&
        Bc250ActiveVmidWrite32(Context, rlcCntl,
            RlcEvidence->CntlBefore | 1u)) {
        KeStallExecutionProcessor(1000u);
    }
    if (status == BC250_PSP_LOAD_FW_OK && Evidence->PhysicalReady != 0u &&
        Bc250ActiveVmidRead32(Context, rlcCntl, &RlcEvidence->CntlAfter) &&
        Bc250ActiveVmidRead32(Context, rlcGpmStat,
            &RlcEvidence->GpmStatAfter)) {
        RlcEvidence->ExecRomAfter =
            (RlcEvidence->GpmStatAfter & execRomMask) != 0u;
        RlcEvidence->EnableF32After = RlcEvidence->CntlAfter & 1u;
        if (RlcEvidence->ExecRomBefore != 0u &&
            RlcEvidence->ExecRomAfter == 0u &&
            RlcEvidence->EnableF32After != 0u)
            RlcEvidence->PhysicalReady = 1u;
    }
RestoreRlc:
    if (Bc250ActiveVmidWrite32(Context, rlcCntl, RlcEvidence->CntlBefore) &&
        Bc250ActiveVmidRead32(Context, rlcCntl, &cntlReadback) &&
        cntlReadback == RlcEvidence->CntlBefore)
        RlcEvidence->CntlRestorePassed = 1u;
Unmap:
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK &&
        status == BC250_PSP_LOAD_FW_OK) {
        status = BC250_PSP_LOAD_FW_RESTORE_FAILED;
        Evidence->PhysicalReady = 0u;
        RlcEvidence->PhysicalReady = 0u;
    }
    if (Evidence->CreatePassed != 0u && Evidence->DestroyPassed == 0u)
        safeToFree = 0u;
Cleanup:
    if (file != NULL) ZwClose(file);
    if (safeToFree != 0u) {
        if (firmware != NULL) MmFreeContiguousMemory(firmware);
        if (fence != NULL) MmFreeContiguousMemory(fence);
        if (command != NULL) MmFreeContiguousMemory(command);
        if (ring != NULL) MmFreeContiguousMemory(ring);
    } else {
        Context->DeferredPspLoadRing = ring;
        Context->DeferredPspLoadCommand = command;
        Context->DeferredPspLoadFence = fence;
        Context->DeferredPspLoadFirmware = firmware;
    }
    return status;
#else
    UNREFERENCED_PARAMETER(Context);
    if (Evidence != NULL) RtlZeroMemory(Evidence, sizeof(*Evidence));
    if (RlcEvidence != NULL) RtlZeroMemory(RlcEvidence, sizeof(*RlcEvidence));
    return BC250_PSP_LOAD_FW_BAD_ARGUMENT;
#endif
}

static VOID Bc250ActiveUnregisterAdapter(
    BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    uint32_t index;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        if (g_Bc250AdapterDirectory[index] == Context) {
            g_Bc250AdapterDirectory[index] = NULL;
            break;
        }
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
}

static NTSTATUS Bc250ObjectStatusToNtStatus(
    BC250_MCDM_OBJECT_STATUS Status)
{
    switch (Status) {
    case BC250_MCDM_OBJECT_OK:
        return STATUS_SUCCESS;
    case BC250_MCDM_OBJECT_NO_SPACE:
        return STATUS_INSUFFICIENT_RESOURCES;
    case BC250_MCDM_OBJECT_DUPLICATE_OWNER:
        return STATUS_OBJECT_NAME_COLLISION;
    case BC250_MCDM_OBJECT_STALE_HANDLE:
    case BC250_MCDM_OBJECT_INVALID_HANDLE:
    case BC250_MCDM_OBJECT_TYPE_MISMATCH:
    case BC250_MCDM_OBJECT_OWNER_MISMATCH:
        return STATUS_INVALID_HANDLE;
    case BC250_MCDM_OBJECT_NODE_MISMATCH:
        return STATUS_INVALID_PARAMETER;
    case BC250_MCDM_OBJECT_NO_ROOT_PAGE_TABLE:
    case BC250_MCDM_OBJECT_IN_TEARDOWN:
        return STATUS_DEVICE_NOT_READY;
    case BC250_MCDM_OBJECT_CHILDREN_LIVE:
    case BC250_MCDM_OBJECT_IN_FLIGHT_SUBMISSIONS:
        return STATUS_DEVICE_BUSY;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS Bc250ActiveCreateProcessObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    const void *ExternalOwner,
    BC250_MCDM_HANDLE *Handle)
{
    BC250_MCDM_OBJECT_STATUS status;

    if (Handle != NULL) *Handle = NULL;
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        ExternalOwner == NULL || Handle == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Context->ObjectLock);
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        ExReleasePushLockExclusive(&Context->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_DEVICE_NOT_READY;
    }
    status = Bc250McdmObjectCreateProcess(
        &Context->Objects, ExternalOwner, Handle);
    ExReleasePushLockExclusive(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    return Bc250ObjectStatusToNtStatus(status);
}

static NTSTATUS Bc250ActiveCreateProcessLifecycle(
    HANDLE AdapterContext,
    const void *ExternalOwner,
    HANDLE *ProcessHandle)
{
    return Bc250ActiveCreateProcessObject(
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext,
        ExternalOwner,
        (BC250_MCDM_HANDLE *)ProcessHandle);
}

static NTSTATUS Bc250ActiveDestroyProcessLifecycle(
    HANDLE AdapterContext,
    HANDLE ProcessHandle)
{
    return Bc250ActiveDestroyProcessObject(
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext,
        (BC250_MCDM_HANDLE)ProcessHandle);
}

NTSTATUS Bc250ActiveDestroyProcessObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE Handle)
{
    BC250_MCDM_OBJECT_STATUS status;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        Handle == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Context->ObjectLock);
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        ExReleasePushLockExclusive(&Context->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_DEVICE_NOT_READY;
    }
    if (InterlockedCompareExchange(&Context->GpuvaSdmaBrokerReady, 1, 1) == 1) {
        (void)Bc250GpuvaSdmaBrokerRevokeForObject(&Context->GpuvaSdmaBroker,
            Handle, NULL, NULL);
    }
    /*
     * A14 (07/09) -- MEDIDO na corrida 17:13. O DestroyContext ja invalidava as
     * faixas de GPUVA do contexto, mas o DestroyProcess NAO invalidava as do
     * processo. Quando o primeiro dwm.exe morreu no fail-fast de D3D11, as
     * faixas dele continuaram MAPPED sob o dono morto, e o dwm.exe seguinte
     * passou a receber:
     *
     *   [ResolveGpuVa] SpaceResolveAddress failed: status=10 (VA=0x30000)
     *
     * status 10 e' BC250_GPUVA_OWNER_MISMATCH: a faixa existe, mas pertence a
     * outro processo -- um que nao existe mais. Vazamento de ciclo de vida, e
     * a resposta ao novo processo virava "ocupado por terceiro" em vez de
     * "nao mapeado".
     *
     * Passar (Handle, NULL, NULL) invalida TODAS as faixas do processo, que e'
     * a mesma semantica que o caminho de contexto usa para as dele.
     */
    (void)Bc250GpuvaSpaceInvalidateForObject(&Context->GpuvaSpace,
        Handle, NULL, NULL);
    Bc250DisplayLog("[DestroyProcess] GPUVA: faixas do processo %p invalidadas (Epoch=%u LiveCount=%u)\n",
        Handle,
        Context->GpuvaSpace.PageTableEpoch,
        Context->GpuvaSpace.LiveCount);
    status = Bc250McdmObjectDestroy(&Context->Objects, Handle,
        BC250_MCDM_OBJECT_PROCESS, Handle);
    ExReleasePushLockExclusive(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    return Bc250ObjectStatusToNtStatus(status);
}

NTSTATUS Bc250ActiveCreateDeviceObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE ProcessHandle,
    BC250_MCDM_HANDLE *DeviceHandle)
{
    BC250_MCDM_OBJECT_STATUS status;

    if (DeviceHandle != NULL) *DeviceHandle = NULL;
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        ProcessHandle == NULL || DeviceHandle == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Context->ObjectLock);
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        status = BC250_MCDM_OBJECT_BAD_REGISTRY;
    } else {
        status = Bc250McdmObjectCreateChild(&Context->Objects,
            BC250_MCDM_OBJECT_DEVICE, ProcessHandle, ProcessHandle,
            DeviceHandle);
    }
    ExReleasePushLockExclusive(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    if (status == BC250_MCDM_OBJECT_BAD_REGISTRY) {
        return STATUS_DEVICE_NOT_READY;
    }
    return Bc250ObjectStatusToNtStatus(status);
}

NTSTATUS Bc250ActiveDestroyDeviceObject(
    BC250_MCDM_HANDLE DeviceHandle)
{
    uint32_t index;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (DeviceHandle == NULL) return STATUS_INVALID_PARAMETER;
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[index];
        BC250_MCDM_OBJECT_RECORD record;
        BC250_MCDM_OBJECT_STATUS status;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        status = Bc250McdmObjectLookup(&context->Objects, DeviceHandle,
            BC250_MCDM_OBJECT_DEVICE, NULL, &record);
        if (status != BC250_MCDM_OBJECT_INVALID_HANDLE) {
            if (status == BC250_MCDM_OBJECT_OK &&
                InterlockedCompareExchange(&context->IsActive, 1, 1) != 1) {
                result = STATUS_DEVICE_NOT_READY;
            } else if (status == BC250_MCDM_OBJECT_OK) {
                status = Bc250McdmObjectDestroy(&context->Objects,
                    DeviceHandle, BC250_MCDM_OBJECT_DEVICE,
                    record.OwnerProcess);
                result = Bc250ObjectStatusToNtStatus(status);
            } else {
                result = Bc250ObjectStatusToNtStatus(status);
            }
            ExReleasePushLockExclusive(&context->ObjectLock);
            break;
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveCreateDeviceLifecycle(
    HANDLE AdapterContext,
    HANDLE ProcessHandle,
    HANDLE *DeviceHandle)
{
    return Bc250ActiveCreateDeviceObject(
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext,
        (BC250_MCDM_HANDLE)ProcessHandle,
        (BC250_MCDM_HANDLE *)DeviceHandle);
}

static NTSTATUS Bc250ActiveDestroyDeviceLifecycle(HANDLE DeviceHandle)
{
    return Bc250ActiveDestroyDeviceObject((BC250_MCDM_HANDLE)DeviceHandle);
}

/*
 * FONTE UNICA da ABI de contexto.
 *
 * MEDIDO na corrida 08/09 14:06, com a instrumentacao nova:
 *   [McdmCreateContext] OK: Node=0, hContext=0x..., DmaBuf=0x0, SegSet=0x0
 *
 * Um contexto com DmaBufferSize=0 e DmaBufferSegmentSet=0 nao serve para nada:
 * o dxgkrnl nao tem como alocar DMA buffer para ele. E o valor saia zerado
 * porque Bc250ActiveCreateContextObject faz "*ContextInfo = context->ContextAbi"
 * -- sobrescrevendo os defaults que Bc250McdmCreateContext acabara de escrever
 * -- e ContextAbi so' era populado DENTRO do bloco
 * "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)", isto e', apenas na
 * imagem de PRODUCAO. Na imagem de laboratorio ContextAbiReady ia a 1 com
 * ContextAbi inteiramente zerado.
 *
 * MESMA CLASSE de defeito do GpuvaLabPromotion (B4) e do tunable de modo de
 * submissao da A13: campo lido ou escrito em UM SO' ramo do #if, invisivel no
 * outro -- e o gate que proibe o nome antigo daquele tunable acabou de pegar
 * este proprio comentario, o que e' o portao funcionando. Por isso a
 * populacao virou funcao unica, chamada nos dois caminhos ANTES de declarar
 * ContextAbiReady, e ha' gate estatico exigindo essa ordem.
 */
static void Bc250ActiveInitializeContextAbi(
    BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    if (Context == NULL) return;
    Context->ContextAbi.DmaBufferSize = 0x10000u;
    Context->ContextAbi.DmaBufferSegmentSet = (1u << (2u - 1u)); /* Segmento 2: GART */
    Context->ContextAbi.AllocationListSize = 32u;
    Context->ContextAbi.PatchLocationListSize = 0u;
    Context->ContextAbi.Caps.NoPatchingRequired = 1u;
    Context->ContextAbi.PagingCompanionNodeId = 0u;
}

NTSTATUS Bc250ActiveCreateContextObject(
    BC250_MCDM_HANDLE DeviceHandle,
    UINT NodeOrdinal,
    BC250_MCDM_HANDLE *ContextHandle,
    DXGK_CONTEXTINFO *ContextInfo)
{
    uint32_t index;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (ContextHandle != NULL) *ContextHandle = NULL;
    if (ContextInfo != NULL) RtlZeroMemory(ContextInfo, sizeof(*ContextInfo));
    if (DeviceHandle == NULL || ContextHandle == NULL || ContextInfo == NULL ||
        NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[index];
        BC250_MCDM_OBJECT_RECORD device;
        BC250_MCDM_OBJECT_STATUS status;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        status = Bc250McdmObjectLookup(&context->Objects, DeviceHandle,
            BC250_MCDM_OBJECT_DEVICE, NULL, &device);
        if (status != BC250_MCDM_OBJECT_INVALID_HANDLE) {
            if (status != BC250_MCDM_OBJECT_OK) {
                result = Bc250ObjectStatusToNtStatus(status);
            } else if (InterlockedCompareExchange(
                    &context->IsActive, 1, 1) != 1 ||
                InterlockedCompareExchange(
                    &context->ContextAbiReady, 1, 1) != 1) {
                result = STATUS_DEVICE_NOT_READY;
            } else {
                status = Bc250McdmObjectCreateContext(&context->Objects,
                    context, DeviceHandle, device.OwnerProcess,
                    (uint32_t)NodeOrdinal, 0u, ContextHandle);
                result = Bc250ObjectStatusToNtStatus(status);
                if (NT_SUCCESS(result)) {
                    *ContextInfo = context->ContextAbi;
                    Bc250DisplayRegisterContextProcess(*ContextHandle, device.OwnerProcess);
                }
            }
            ExReleasePushLockExclusive(&context->ObjectLock);
            break;
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

NTSTATUS Bc250ActiveDestroyContextObject(
    BC250_MCDM_HANDLE ContextHandle)
{
    uint32_t index;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (ContextHandle == NULL) return STATUS_INVALID_PARAMETER;
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[index];
        BC250_MCDM_OBJECT_RECORD record;
        BC250_MCDM_OBJECT_STATUS status;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        status = Bc250McdmObjectLookup(&context->Objects, ContextHandle,
            BC250_MCDM_OBJECT_CONTEXT, NULL, &record);
        if (status != BC250_MCDM_OBJECT_INVALID_HANDLE) {
            if (status == BC250_MCDM_OBJECT_OK &&
                InterlockedCompareExchange(&context->IsActive, 1, 1) == 1) {
                (void)Bc250GpuvaSpaceInvalidateForObject(&context->GpuvaSpace,
                    record.OwnerProcess, ContextHandle, NULL);
                status = Bc250McdmObjectDestroy(&context->Objects,
                    ContextHandle, BC250_MCDM_OBJECT_CONTEXT,
                    record.OwnerProcess);
                result = Bc250ObjectStatusToNtStatus(status);
                Bc250DisplayUnregisterContextProcess(ContextHandle);
            } else if (status == BC250_MCDM_OBJECT_OK) {
                result = STATUS_DEVICE_NOT_READY;
            } else {
                result = Bc250ObjectStatusToNtStatus(status);
            }
            ExReleasePushLockExclusive(&context->ObjectLock);
            {
                uint32_t retiredNow = 0u;
                (void)Bc250SubmissionEnginePoll(context, &retiredNow);
                Bc250SubmissionEngineRecordTelemetry(context);
            }
            break;
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveCreateContextLifecycle(
    HANDLE DeviceHandle,
    UINT NodeOrdinal,
    HANDLE *ContextHandle,
    DXGK_CONTEXTINFO *ContextInfo)
{
    return Bc250ActiveCreateContextObject(
        (BC250_MCDM_HANDLE)DeviceHandle, NodeOrdinal,
        (BC250_MCDM_HANDLE *)ContextHandle, ContextInfo);
}

static NTSTATUS Bc250ActiveDestroyContextLifecycle(HANDLE ContextHandle)
{
    return Bc250ActiveDestroyContextObject(
        (BC250_MCDM_HANDLE)ContextHandle);
}

NTSTATUS Bc250ActiveSetRootPageTableObject(
    BC250_DXGK_ACTIVE_CONTEXT *context,
    BC250_MCDM_HANDLE ContextHandle,
    uint32_t SegmentId,
    uint64_t Address,
    uint32_t NumEntries,
    uint32_t MaxEntries)
{
    BC250_MCDM_OBJECT_STATUS status;

    Bc250DisplayLog("[ActiveSetRootPT] ENTER: Ctx=%p, SegId=%u, Addr=0x%llX, Num=%u, Max=%u (CtxMax=%u, Active=%d, AbiReady=%d)\n",
        ContextHandle, SegmentId, (unsigned long long)Address, NumEntries, MaxEntries,
        (context != NULL) ? context->RootPageTableMaxEntries : 0u,
        (context != NULL) ? context->IsActive : 0,
        (context != NULL) ? context->RootPageTableAbiReady : 0);

    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE ||
        ContextHandle == NULL || MaxEntries == 0u) {
        Bc250DisplayLog("[ActiveSetRootPT] FAIL: invalid params\n");
        return STATUS_INVALID_PARAMETER;
    }
    if (SegmentId > 1u) {
        Bc250DisplayLog("[ActiveSetRootPT] FAIL: invalid SegmentId=%u\n", SegmentId);
        return STATUS_INVALID_PARAMETER;
    }
    if (SegmentId == 1u) {
        if ((Address & 0xFFFull) != 0ull ||
            Address >= context->AllocationSegmentBytes ||
            NumEntries == 0u || NumEntries > MaxEntries ||
            (UINT64)NumEntries * sizeof(UINT64) >
              context->AllocationSegmentBytes - Address) {
            Bc250DisplayLog("[ActiveSetRootPT] FAIL: invalid segment 1 params (Addr=0x%llX, Num=%u, Max=%u, SegBytes=0x%llX)\n",
                (unsigned long long)Address, NumEntries, MaxEntries,
                (unsigned long long)context->AllocationSegmentBytes);
            return STATUS_INVALID_PARAMETER;
        }
    } else {
        if (NumEntries != 0u || Address != 0u) {
            Bc250DisplayLog("[ActiveSetRootPT] FAIL: invalid segment 0 unbind params (Addr=0x%llX, Num=%u)\n",
                (unsigned long long)Address, NumEntries);
            return STATUS_INVALID_PARAMETER;
        }
    }
    if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1 ||
        InterlockedCompareExchange(
            &context->RootPageTableAbiReady, 1, 1) != 1 ||
        context->RootPageTableMaxEntries != MaxEntries) {
        Bc250DisplayLog("[ActiveSetRootPT] FAIL: state or MaxEntries mismatch (Active=%d, AbiReady=%d, CtxMax=%u vs Max=%u)\n",
            context->IsActive, context->RootPageTableAbiReady,
            context->RootPageTableMaxEntries, MaxEntries);
        return STATUS_DEVICE_NOT_READY;
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&context->ObjectLock);
    if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1 ||
        InterlockedCompareExchange(
            &context->RootPageTableAbiReady, 1, 1) != 1) {
        ExReleasePushLockExclusive(&context->ObjectLock);
        KeLeaveCriticalRegion();
        return STATUS_DEVICE_NOT_READY;
    }
    if (SegmentId == 1u && !context->GlobalMmuReady) {
        NTSTATUS globalMmuStatus = Bc250ActiveEnsureGlobalMmu(
            context, "SetRootPageTable");
        if (!NT_SUCCESS(globalMmuStatus)) {
            ExReleasePushLockExclusive(&context->ObjectLock);
            KeLeaveCriticalRegion();
            Bc250DisplayLog("[SetRootPageTable] RECUSADO: MMU global indisponivel, status=0x%08X\n",
                (uint32_t)globalMmuStatus);
            return globalMmuStatus;
        }
    }
    status = Bc250McdmObjectSetRootPageTable(&context->Objects,
        (BC250_MCDM_HANDLE)ContextHandle, SegmentId, Address,
        NumEntries, MaxEntries);
    if (status != BC250_MCDM_OBJECT_OK) {
        ExReleasePushLockExclusive(&context->ObjectLock);
        KeLeaveCriticalRegion();
        return Bc250ObjectStatusToNtStatus(status);
    }

    /*
     * Instalação persistente do VMID 8 no silício (Passo 2 do handoff).
     * Quando MMIO estiver mapeado, programa os registradores GCVM do VMID 8
     * a partir da root page table real do processo com readback check estrito
     * e invalidação de TLB pela engine 17.
     * Fail-closed: se o readback divergir ou o flush falhar, aborta e retorna
     * STATUS_UNSUCCESSFUL sem deixar o VMID parcialmente programado.
     */
    {
        BC250_MCDM_OBJECT_RECORD rootContext;
        if (Bc250McdmObjectLookup(&context->Objects, ContextHandle,
                BC250_MCDM_OBJECT_CONTEXT, NULL, &rootContext) != BC250_MCDM_OBJECT_OK) {
            ExReleasePushLockExclusive(&context->ObjectLock);
            KeLeaveCriticalRegion();
            return STATUS_INVALID_PARAMETER;
        }
        if (rootContext.NodeOrdinal == BC250_SUBMISSION_NODE_COMPUTE) {
            Bc250DisplayLog("[SetRootPageTable] Node1: raiz registrada; VMID9 instalado no submit\n");
            ExReleasePushLockExclusive(&context->ObjectLock);
            KeLeaveCriticalRegion();
            return STATUS_SUCCESS;
        }
    }

    if (context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
        BC250_GCVM_VMID8_INTERFACE io;
        BC250_GCVM_VMID8_STATUS vmidStatus;

        RtlZeroMemory(&io, sizeof(io));
        io.Read32 = Bc250ActiveVmidRead32;
        io.Write32 = Bc250ActiveVmidWrite32;
        io.Flush = Bc250ActiveFlushVmid8Only;
        io.Context = context;
        io.Identity = BC250_GCVM_VMID8_IDENTITY;

        if (SegmentId == 1u && NumEntries != 0u) {
            /*
             * A base sai de rootCaps.VramMcBase, mais abaixo. Aqui ficava uma
             * inicializacao a partir do leitor de topologia com fallback para o
             * literal 0x450000000ull -- MORTA (era sobrescrita sem condicao) e uma
             * QUARTA grafia do mesmo numero, que ja tem nome:
             * BC250_GCVM_VRAM_MC_BASE_FALLBACK. Numero repetido a mao foi
             * exatamente como GpuVaBits divergiu (48 contra 34). Removida.
             */
            uint64_t vramBaseOffset;
            BC250_MCDM_RUNTIME_CAPS rootCaps;
            uint64_t ptPhysical;

            /*
             * Address e o offset DENTRO do segmento 1, nao o offset dentro da
             * VRAM. O segmento 1 nao comeca em zero: LocalSegmentBaseOffset
             * pula a reserva herdada do scanout do GOP. Sem esse termo o
             * ponteiro de root entregue a GCVM apontava 0xA90000 bytes antes
             * da tabela real -- exatamente para dentro do framebuffer
             * reservado --, e a GPU caminhava pixels como se fossem PDEs.
             *
             * O codificador de PTE ja usava LocalSegmentBaseOffset
             * (Bc250McdmEncodeVidMmPteWithLevel); so a instalacao da root
             * estava fora de fase com ele.
             */
            RtlZeroMemory(&rootCaps, sizeof(rootCaps));
            if (!NT_SUCCESS(Bc250ActiveQueryRuntimeCaps(context, &rootCaps)) ||
                rootCaps.Ready == 0u) {
                ExReleasePushLockExclusive(&context->ObjectLock);
                KeLeaveCriticalRegion();
                Bc250DisplayLog("[SetRootPageTable] [Vmid8Install] RECUSADO: runtime caps indisponiveis\n");
                return STATUS_DEVICE_NOT_READY;
            }

            /*
             * Base unica: a mesma que Bc250McdmEncodeVidMmPteWithLevel usa
             * para os enderecos de VRAM nas entradas. Root e entradas fora de
             * fase foi exatamente o que a corrida de 05/09 00:55 mediu.
             */
            vramBaseOffset = rootCaps.VramMcBase;
            ptPhysical = vramBaseOffset + rootCaps.LocalSegmentBaseOffset + Address;
            /*
             * A base usada e' constante (ver BC250_GCVM_VRAM_MC_BASE_FALLBACK) e o
             * leitor de topologia le GCMC_VM_FB_OFFSET do silicio. Enquanto os dois
             * concordam, a constante e' so' um atalho. Se DIVERGIREM, a constante
             * passa a ser valor obsoleto usado em silencio -- e isso tem de aparecer
             * no log, nao ficar para alguem diferir a olho numa corrida futura.
             */
            {
                CONST UINT64 topologyBase =
                    context->Core.StartDevice.Owned.Bootstrap.AddressSpace.VramBaseOffset;
                if (topologyBase != 0ull && topologyBase != rootCaps.VramMcBase) {
                    Bc250DisplayLog("[SetRootPageTable] ROOT_MC: DIVERGENCIA -- topologia leu "
                        "0x%llX do silicio, base em uso e 0x%llX. A constante pode estar "
                        "obsoleta para esta placa.\n",
                        (unsigned long long)topologyBase,
                        (unsigned long long)rootCaps.VramMcBase);
                } else {
                    Bc250DisplayLog("[SetRootPageTable] ROOT_MC: TopologyVramBase=0x%llX "
                        "(NAO usada; concorda com a base em uso)\n",
                        (unsigned long long)topologyBase);
                }
            }
            Bc250DisplayLog("[SetRootPageTable] ROOT_MC: VramBase=0x%llX + LocalSegBase=0x%llX + Offset=0x%llX -> PT=0x%llX\n",
                (unsigned long long)vramBaseOffset,
                (unsigned long long)rootCaps.LocalSegmentBaseOffset,
                (unsigned long long)Address,
                (unsigned long long)ptPhysical);
            RtlZeroMemory(&context->Vmid8InstallEvidence, sizeof(context->Vmid8InstallEvidence));
            vmidStatus = Bc250GcvmVmid8Install(ptPhysical, &io, &context->Vmid8InstallEvidence);
            context->Vmid8InstallStatus = vmidStatus;
            if (vmidStatus != BC250_GCVM_VMID8_OK) {
                context->Vmid8InstalledContext = NULL;
                context->Vmid8InstalledRootPhysical = 0ull;
                Bc250DisplayLog("[SetRootPageTable] [Vmid8Install] FALHA vmidStatus=%u step=%u reg=0x%05X val=0x%08X PT=%llx\n",
                    (uint32_t)vmidStatus,
                    context->Vmid8InstallEvidence.FailureStep,
                    context->Vmid8InstallEvidence.FailureRegister,
                    context->Vmid8InstallEvidence.FailureValue,
                    (unsigned long long)ptPhysical);
                ExReleasePushLockExclusive(&context->ObjectLock);
                KeLeaveCriticalRegion();
                return STATUS_UNSUCCESSFUL;
            }
            context->Vmid8InstalledContext = ContextHandle;
            context->Vmid8InstalledRootPhysical = ptPhysical;
            Bc250DisplayLog("[SetRootPageTable] [Vmid8Install] readback OK: PT_BASE=%llx CNTL=0x%08X (PhysReady=%u, Flush=%u)\n",
                (unsigned long long)context->Vmid8InstallEvidence.PageTablePhysical,
                context->Vmid8InstallEvidence.ContextControlProgrammed,
                context->Vmid8InstallEvidence.PhysicalReady,
                context->Vmid8InstallEvidence.TlbFlushPassed);
        } else {
            RtlZeroMemory(&context->Vmid8InstallEvidence, sizeof(context->Vmid8InstallEvidence));
            vmidStatus = Bc250GcvmVmid8Uninstall(&io, &context->Vmid8InstallEvidence);
            context->Vmid8InstallStatus = vmidStatus;
            context->Vmid8InstalledContext = NULL;
            context->Vmid8InstalledRootPhysical = 0ull;
            Bc250DisplayLog("[SetRootPageTable] [Vmid8Uninstall] vmidStatus=%u\n", (uint32_t)vmidStatus);
        }
    }

    ExReleasePushLockExclusive(&context->ObjectLock);
    KeLeaveCriticalRegion();
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ActiveSetRootPageTableLifecycle(
    HANDLE AdapterContext,
    HANDLE ContextHandle,
    UINT32 SegmentId,
    UINT64 Address,
    UINT32 NumEntries,
    UINT32 MaxEntries)
{
    return Bc250ActiveSetRootPageTableObject(
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext,
        (BC250_MCDM_HANDLE)ContextHandle,
        SegmentId, Address, NumEntries, MaxEntries);
}

NTSTATUS Bc250ActiveIssueGpuvaSdmaLease(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE OpenAllocation,
    uint64_t GpuVa,
    uint64_t ByteCount,
    uint64_t *LeaseId)
{
    BC250_GPUVA_SDMA_BROKER_STATUS status;

    if (LeaseId != NULL) *LeaseId = 0ull;
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        OwnerProcess == NULL || OpenAllocation == NULL || LeaseId == NULL ||
        InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1 ||
        InterlockedCompareExchange(&Context->GpuvaSdmaBrokerReady, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&Context->ObjectLock);
    status = Bc250GpuvaSdmaBrokerIssueLease(&Context->GpuvaSdmaBroker,
        &Context->Objects, OwnerProcess, OpenAllocation, GpuVa, ByteCount,
        LeaseId);
    ExReleasePushLockExclusive(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    if (status == BC250_GPUVA_SDMA_BROKER_OK) return STATUS_SUCCESS;
    if (status == BC250_GPUVA_SDMA_BROKER_MAPPING_NOT_READY) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (status == BC250_GPUVA_SDMA_BROKER_NO_SPACE) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS Bc250ActiveValidateGpuvaSdmaLeasePair(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    uint64_t SourceLeaseId,
    uint64_t DestinationLeaseId,
    uint64_t FenceGpuVa,
    uint64_t ByteCount)
{
    BC250_GPUVA_SDMA_BROKER_STATUS status;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1 ||
        InterlockedCompareExchange(&Context->GpuvaSdmaBrokerReady, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&Context->ObjectLock);
    status = Bc250GpuvaSdmaBrokerValidatePair(&Context->GpuvaSdmaBroker,
        SourceLeaseId, DestinationLeaseId, FenceGpuVa, ByteCount);
    ExReleasePushLockShared(&Context->ObjectLock);
    KeLeaveCriticalRegion();
    return status == BC250_GPUVA_SDMA_BROKER_OK ? STATUS_SUCCESS :
        STATUS_INVALID_PARAMETER;
}

/*
 * F2.1 (13/09) -- EXECUTOR DA DESCRICAO DE ALOCACAO.
 *
 * Responde `DxgkDdiDescribeAllocation` a partir da geometria que a UMD declarou
 * na criacao e que o registro de objetos guardou. Sem geometria declarada,
 * RECUSA -- nao ha caminho em que uma descricao seja inventada.
 *
 * `RefreshRate` sai do modo vigente, que e' onde ele realmente vive: e' uma
 * propriedade do scanout, nao da superficie. Quando nao ha modo publicado, sai
 * 0/1 -- "desconhecido" declarado, e nao um numero plausivel escolhido por nos.
 *
 * `Rotation` e' IDENTITY porque o driver declara SupportSmoothRotation=0 e nao
 * ha caminho rotacionado; declarar outra coisa seria cap sem backend.
 */
static NTSTATUS Bc250ActiveDescribeAllocationLifecycle(
    HANDLE AdapterContext,
    DXGKARG_DESCRIBEALLOCATION *DescribeAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (DescribeAllocation == NULL || DescribeAllocation->hAllocation == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t pitch = 0u;
        uint32_t format = 0u;
        BC250_MCDM_OBJECT_STATUS status;

        if (context != (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext) continue;

        ExAcquirePushLockShared(&context->ObjectLock);
        status = Bc250McdmObjectGetAllocationGeometry(&context->Objects,
            (BC250_MCDM_HANDLE)DescribeAllocation->hAllocation,
            &width, &height, &pitch, &format);
        ExReleasePushLockShared(&context->ObjectLock);

        if (status != BC250_MCDM_OBJECT_OK) {
            result = (status == BC250_MCDM_OBJECT_NO_GEOMETRY) ?
                STATUS_NOT_SUPPORTED : Bc250ObjectStatusToNtStatus(status);
            break;
        }

        DescribeAllocation->Width = width;
        DescribeAllocation->Height = height;
        DescribeAllocation->Format = (D3DDDIFORMAT)format;
        DescribeAllocation->MultisampleMethod.NumSamples = 1;
        DescribeAllocation->MultisampleMethod.NumQualityLevels = 0;
        DescribeAllocation->Rotation = D3DDDI_ROTATION_IDENTITY;
        DescribeAllocation->PrivateDriverFormatAttribute = 0u;
        DescribeAllocation->Flags.Value = 0u;
        if (g_Bc250DisplayState.CurrentMode.RefreshRateNumerator != 0u &&
            g_Bc250DisplayState.CurrentMode.RefreshRateDenominator != 0u) {
            DescribeAllocation->RefreshRate.Numerator =
                g_Bc250DisplayState.CurrentMode.RefreshRateNumerator;
            DescribeAllocation->RefreshRate.Denominator =
                g_Bc250DisplayState.CurrentMode.RefreshRateDenominator;
        } else {
            DescribeAllocation->RefreshRate.Numerator = 0u;
            DescribeAllocation->RefreshRate.Denominator = 1u;
        }
        /*
         * Pitch nao cabe em DXGKARG_DESCRIBEALLOCATION (d3dkmddi.h:4221); ele e'
         * publicado em DXGK_ALLOCATIONINFO.Pitch na criacao. Fica registrado
         * aqui para quem vier procurar, e para que a divergencia entre os dois
         * caminhos seja visivel no log.
         */
        Bc250DisplayLog("[DescribeAlloc] geometria do registro: hAlloc=%p %ux%u "
            "pitch=%u fmt=%u\n",
            DescribeAllocation->hAllocation, width, height, pitch, format);
        result = STATUS_SUCCESS;
        break;
    }
    ExReleasePushLockShared(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveCreateAllocationLifecycle(
    HANDLE AdapterContext,
    DXGKARG_CREATEALLOCATION *CreateAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    /*
     * Instrumentacao 3.82 (somente leitura, zero mudanca de comportamento).
     * Nas corridas 12:46, 13:32 e 14:25 esta DDI NAO apareceu uma unica vez no
     * log, e todo CommitVidPn chegou com hPrimaryAllocation=NULL. Precisamos
     * saber se o dxgkrnl chega a pedir alocacao e nos a recusamos, ou se ele
     * nunca pede. O log entra ANTES de qualquer gate para cobrir os dois casos.
     */
    if (CreateAllocation != NULL) {
        UINT logIndex;
        Bc250DisplayLog("[CreateAllocation] ENTER: NumAllocations=%u, Flags=0x%08X (Resource=%u), hResource=%p\n",
            CreateAllocation->NumAllocations,
            CreateAllocation->Flags.Value,
            CreateAllocation->Flags.Resource,
            CreateAllocation->hResource);
        for (logIndex = 0u;
             logIndex < CreateAllocation->NumAllocations && logIndex < 8u;
             ++logIndex) {
            CONST DXGK_ALLOCATIONINFO *logInfo =
                &CreateAllocation->pAllocationInfo[logIndex];
            Bc250DisplayLog("[CreateAllocation]   [%u] Size=%Iu, Alignment=%u, Flags=0x%08X, PrivDataSize=%u\n",
                logIndex, logInfo->Size, logInfo->Alignment,
                logInfo->Flags.Value, logInfo->PrivateDriverDataSize);
        }
    } else {
        Bc250DisplayLog("[CreateAllocation] ENTER: CreateAllocation=NULL\n");
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        UINT index;
        UINT created = 0u;
        if (context != (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1 ||
            InterlockedCompareExchange(
                &context->AllocationAbiReady, 1, 1) != 1) {
            result = STATUS_DEVICE_NOT_READY;
        } else {
            result = STATUS_SUCCESS;
            for (index = 0u; index < CreateAllocation->NumAllocations; ++index) {
                BC250_ALLOCATION_PRIVATE_V1 request;
                DXGK_ALLOCATIONINFO *info =
                    &CreateAllocation->pAllocationInfo[index];
                if (info->pPrivateDriverData != NULL && info->PrivateDriverDataSize != 0u) {
                    if (Bc250AllocationAbiParse(info->pPrivateDriverData,
                            info->PrivateDriverDataSize, &request) !=
                            BC250_ALLOCATION_ABI_OK ||
                        request.ByteSize > (uint64_t)(SIZE_T)~(SIZE_T)0 ||
                        context->AllocationSegmentSet != 1u ||
                        context->AllocationSegmentBytes == 0u ||
                        request.ByteSize > context->AllocationSegmentBytes) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                } else {
                    uint64_t allocSize = info->Size != 0u ? (uint64_t)info->Size : 4096ull;
                    if (allocSize > (uint64_t)(SIZE_T)~(SIZE_T)0 ||
                        context->AllocationSegmentSet != 1u ||
                        context->AllocationSegmentBytes == 0u ||
                        allocSize > context->AllocationSegmentBytes) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < CreateAllocation->NumAllocations;
                 ++index) {
                BC250_ALLOCATION_PRIVATE_V1 request;
                BC250_MCDM_HANDLE handle = NULL;
                BC250_MCDM_OBJECT_STATUS status;
                DXGK_ALLOCATIONINFO *info =
                    &CreateAllocation->pAllocationInfo[index];
                RtlZeroMemory(&request, sizeof(request));
                if (info->pPrivateDriverData != NULL && info->PrivateDriverDataSize != 0u) {
                    (void)Bc250AllocationAbiParse(info->pPrivateDriverData,
                        info->PrivateDriverDataSize, &request);
                } else {
                    static volatile LONG g_Bc250FallbackAllocSeq = 0;
                    LONG fbSeq = InterlockedIncrement(&g_Bc250FallbackAllocSeq);
                    request.AllocationToken = 0xA110000000000000ull | (UINT64)(ULONG)fbSeq;
                    request.ByteSize = info->Size != 0u ? (uint64_t)info->Size : 4096ull;
                    request.Alignment = info->Alignment != 0u ? (uint64_t)info->Alignment : 4096ull;
                    request.Flags = 0u;
                }
                if (request.ByteSize == 0u && info->Size != 0u) {
                    request.ByteSize = (uint64_t)info->Size;
                }
                if (request.Alignment == 0u) {
                    request.Alignment = info->Alignment != 0u ? (uint64_t)info->Alignment : 4096ull;
                }
                status = Bc250McdmObjectCreateAdapterAllocation(
                    &context->Objects, info, request.AllocationToken,
                    request.ByteSize,
                    request.Alignment, &handle);
                if (status == BC250_MCDM_OBJECT_DUPLICATE_OWNER) {
                    static volatile LONG g_Bc250RetryAllocSeq = 50000;
                    LONG rSeq = InterlockedIncrement(&g_Bc250RetryAllocSeq);
                    uint64_t retryToken = ((request.AllocationToken & 0xFFFFFFFF00000000ull) != 0ull) ?
                        (request.AllocationToken + (UINT64)(ULONG)rSeq) :
                        (((UINT64)request.AllocationToken << 32) | (UINT64)(ULONG)rSeq);
                    status = Bc250McdmObjectCreateAdapterAllocation(
                        &context->Objects, info, retryToken,
                        request.ByteSize,
                        request.Alignment, &handle);
                }
                result = Bc250ObjectStatusToNtStatus(status);
                if (!NT_SUCCESS(result)) break;

                /*
                 * F2.1 -- a geometria entra no registro de objetos AQUI, e e' o
                 * unico lugar. DxgkDdiDescribeAllocation le' dali por quadro.
                 * Falha no registro NAO derruba a criacao: a alocacao continua
                 * valida para uso geral; o que ela perde e' a capacidade de ser
                 * DESCRITA, e a DDI recusa honestamente nesse caso.
                 */
                if (Bc250AllocationHasGeometry(&request)) {
                    BC250_MCDM_OBJECT_STATUS geoStatus =
                        Bc250McdmObjectSetAllocationGeometry(&context->Objects,
                            handle,
                            request.Geometry.Width,
                            request.Geometry.Height,
                            request.Geometry.Pitch,
                            request.Geometry.Format);
                    if (geoStatus != BC250_MCDM_OBJECT_OK) {
                        Bc250DisplayLog("[CreateAllocation] FALHA ao registrar geometria: "
                            "Handle=%p %ux%u pitch=%u fmt=%u status=%d -- "
                            "DescribeAllocation vai RECUSAR esta alocacao\n",
                            handle, request.Geometry.Width, request.Geometry.Height,
                            request.Geometry.Pitch, request.Geometry.Format,
                            (int)geoStatus);
                    }
                }

                info->Size = (SIZE_T)request.ByteSize;
                info->PitchAlignedSize = (SIZE_T)request.ByteSize;
                info->Alignment = (UINT)request.Alignment;
                info->PreferredSegment.Value = 0u;
                info->PreferredSegment.SegmentId0 = 1u;

                /*
                 * F2.5 (13/09) -- ITEM CANCELADO. DOIS ACHADOS EXTERNOS FALSOS.
                 *
                 * Duas auditorias externas pediram, cada uma, um campo que NAO
                 * EXISTE no contrato:
                 *
                 *   "DXGK_ALLOCATIONINFO.Pitch nunca e' preenchido"
                 *      -> a struct (d3dkmddi.h:3993) nao tem membro `Pitch`.
                 *         Tem `PitchAlignedSize`, que este codigo JA escreve.
                 *         A observacao de que nao havia `->Pitch =` no arquivo
                 *         estava certa; a conclusao, nao.
                 *
                 *   "DXGK_ALLOCATIONINFO.Flags.Primary nunca e' escrito"
                 *      -> `DXGK_ALLOCATIONINFOFLAGS` (d3dkmddi.h:3875) nao tem
                 *         bit `Primary`. Zero ocorrencias no header inteiro.
                 *
                 * As duas tentativas de implementacao falharam na compilacao com
                 * C2039, e e' por isso que achado externo nao entra sem
                 * verificacao. A primariedade continua vivendo onde de fato vive:
                 * no token privado e em `Flags.Resource` do lado do dxgkrnl.
                 */
                info->SupportedReadSegmentSet =
                    context->AllocationSegmentSet;
                info->SupportedWriteSegmentSet =
                    context->AllocationSegmentSet;
                info->EvictionSegmentSet = 0u;
                BOOLEAN cpuVisible = FALSE;
                if (Bc250AllocationIsPrimary(&request) ||
                    Bc250AllocationRequiresCpuAccess(&request)) {
                    cpuVisible = TRUE;
                } else if (CreateAllocation->Flags.Resource == 0u &&
                           (info->FlagsWddm2.CpuVisible != 0u || info->Flags.CpuVisible != 0u)) {
                    cpuVisible = TRUE;
                }
                info->PhysicalAdapterIndex = 0u;
                info->FlagsWddm2.Value = 0u;
                info->FlagsWddm2.CpuVisible = cpuVisible ? 1u : 0u;
                if (CreateAllocation->Flags.Resource != 0u &&
                    Bc250AllocationIsPrimary(&request)) {
                    info->FlagsWddm2.AccessedPhysically = 1u;
                    /*
                     * 13/09 -- NAO CLOBBER. Ate esta faixa este ramo era
                     * inalcancavel para alocacao de UMD (Flags.Resource era
                     * sempre 0, medido na corrida 16:28:27), entao o unico
                     * escritor aqui era a alocacao padrao do dxgkrnl no bind --
                     * a primaria do GOP, que o CommitVidPn usa. Agora que a UMD
                     * passa hRTResource, as primarias do dwm.exe TAMBEM chegam
                     * aqui, e sobrescrever este campo trocaria a primaria do
                     * display por um backbuffer de composicao.
                     *
                     * O campo so' e' preenchido enquanto estiver vazio; as
                     * demais sao contadas e nomeadas, nunca silenciadas.
                     */
                    if (g_Bc250DisplayState.hPrimaryAllocation == NULL) {
                        g_Bc250DisplayState.hPrimaryAllocation = (HANDLE)handle;
                        Bc250DisplayLog("[CreateAllocation] Primary token confirmed (DISPLAY): Handle=%p, Size=%llu (CpuVisible=%u)\n",
                            handle, request.ByteSize, info->FlagsWddm2.CpuVisible);
                    } else {
                        static volatile LONG g_Bc250UmdPrimarySeq = 0;
                        LONG primarySeq = InterlockedIncrement(&g_Bc250UmdPrimarySeq);
                        if (primarySeq <= 8L || (primarySeq % 256L) == 0L) {
                            Bc250DisplayLog("[CreateAllocation] Primary token confirmed (UMD #%ld): Handle=%p, Token=0x%llX, Size=%llu, CpuVisible=%u "
                                "-- primaria de display ja definida (%p), NAO substituida\n",
                                primarySeq, handle,
                                (unsigned long long)request.AllocationToken,
                                request.ByteSize, info->FlagsWddm2.CpuVisible,
                                g_Bc250DisplayState.hPrimaryAllocation);
                        }
                    }
                } else if (CreateAllocation->Flags.Resource != 0u) {
                    Bc250DisplayLog("[CreateAllocation] Render resource, not display primary: Handle=%p, Token=0x%llX, Size=%llu, CpuVisible=%u\n",
                        handle, (unsigned long long)request.AllocationToken,
                        request.ByteSize, info->FlagsWddm2.CpuVisible);
                } else {
                    Bc250DisplayLog("[CreateAllocation] Non-resource allocation: Handle=%p, Token=0x%llX, Size=%llu, CpuVisible=%u\n",
                        handle, (unsigned long long)request.AllocationToken,
                        request.ByteSize, info->FlagsWddm2.CpuVisible);
                }
                info->hAllocation = handle;
                ++created;
            }
            if (!NT_SUCCESS(result)) {
                while (created != 0u) {
                    DXGK_ALLOCATIONINFO *info =
                        &CreateAllocation->pAllocationInfo[--created];
                    (void)Bc250McdmObjectDestroy(&context->Objects,
                        info->hAllocation, BC250_MCDM_OBJECT_ALLOCATION, NULL);
                    info->hAllocation = NULL;
                }
            }
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveDestroyAllocationLifecycle(
    HANDLE AdapterContext,
    const DXGKARG_DESTROYALLOCATION *DestroyAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        UINT index, other;
        if (context != (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1) {
            result = STATUS_DEVICE_NOT_READY;
        } else {
            result = STATUS_SUCCESS;
            for (index = 0u; index < DestroyAllocation->NumAllocations; ++index) {
                HANDLE handle = DestroyAllocation->pAllocationList[index];
                if (handle == NULL) {
                    result = STATUS_INVALID_PARAMETER;
                    break;
                }
                for (other = 0u; other < index; ++other) {
                    if (DestroyAllocation->pAllocationList[other] == handle) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
                if (!NT_SUCCESS(result)) break;
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < DestroyAllocation->NumAllocations;
                 ++index) {
                BC250_MCDM_OBJECT_RECORD record;
                BC250_MCDM_OBJECT_STATUS status;
                HANDLE handle = DestroyAllocation->pAllocationList[index];
                status = Bc250McdmObjectLookup(&context->Objects, handle,
                    BC250_MCDM_OBJECT_ALLOCATION, NULL, &record);
                result = Bc250ObjectStatusToNtStatus(status);
                if (!NT_SUCCESS(result)) break;
                if (record.ChildCount != 0u) {
                    result = STATUS_DEVICE_BUSY;
                    break;
                }
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < DestroyAllocation->NumAllocations;
                 ++index) {
                BC250_MCDM_OBJECT_STATUS status = Bc250McdmObjectDestroy(
                    &context->Objects,
                    DestroyAllocation->pAllocationList[index],
                    BC250_MCDM_OBJECT_ALLOCATION, NULL);
                result = Bc250ObjectStatusToNtStatus(status);
            }
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static NTSTATUS Bc250ActiveOpenAllocationLifecycle(
    HANDLE DeviceContext,
    const DXGKARG_OPENALLOCATION *OpenAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    Bc250DisplayLog("[ActiveOpenAlloc] ENTER: DevCtx=%p, NumAllocs=%u\n",
        DeviceContext, OpenAllocation ? OpenAllocation->NumAllocations : 0u);

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        BC250_MCDM_OBJECT_RECORD deviceRecord;
        BC250_MCDM_OBJECT_STATUS objectStatus;
        UINT index, other, opened = 0u;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        objectStatus = Bc250McdmObjectLookup(&context->Objects,
            DeviceContext, BC250_MCDM_OBJECT_DEVICE, NULL, &deviceRecord);
        if (objectStatus != BC250_MCDM_OBJECT_OK) {
            Bc250DisplayLog("[ActiveOpenAlloc] Device lookup failed for %p (status=%u)\n",
                DeviceContext, (uint32_t)objectStatus);
            ExReleasePushLockExclusive(&context->ObjectLock);
            continue;
        }
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1 ||
            InterlockedCompareExchange(
                &context->AllocationAbiReady, 1, 1) != 1) {
            Bc250DisplayLog("[ActiveOpenAlloc] Adapter inactive or AbiNotReady (Active=%ld, AbiReady=%ld)\n",
                context->IsActive, context->AllocationAbiReady);
            result = STATUS_DEVICE_NOT_READY;
        } else {
            result = STATUS_SUCCESS;
            for (index = 0u; index < OpenAllocation->NumAllocations; ++index) {
                BC250_ALLOCATION_PRIVATE_V1 request;
                DXGK_OPENALLOCATIONINFO *info =
                    &OpenAllocation->pOpenAllocation[index];
                if (info->hAllocation == 0u) {
                    Bc250DisplayLog("[ActiveOpenAlloc] Invalid hAllocation 0 at index %u\n", index);
                    result = STATUS_INVALID_PARAMETER;
                    break;
                }
                if (info->pPrivateDriverData != NULL && info->PrivateDriverDataSize != 0u) {
                    if (Bc250AllocationAbiParse(info->pPrivateDriverData,
                            info->PrivateDriverDataSize, &request) !=
                            BC250_ALLOCATION_ABI_OK) {
                        Bc250DisplayLog("[ActiveOpenAlloc] ABI parse failed at index %u\n", index);
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                    for (other = 0u; other < index; ++other) {
                        BC250_ALLOCATION_PRIVATE_V1 previous;
                        DXGK_OPENALLOCATIONINFO *prior =
                            &OpenAllocation->pOpenAllocation[other];
                        if (prior->pPrivateDriverData != NULL && prior->PrivateDriverDataSize != 0u) {
                            (void)Bc250AllocationAbiParse(prior->pPrivateDriverData,
                                prior->PrivateDriverDataSize, &previous);
                            if (prior->hAllocation == info->hAllocation ||
                                previous.AllocationToken == request.AllocationToken) {
                                Bc250DisplayLog("[ActiveOpenAlloc] Duplicate token at index %u\n", index);
                                result = STATUS_INVALID_PARAMETER;
                                break;
                            }
                        }
                    }
                }
                if (!NT_SUCCESS(result)) break;
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < OpenAllocation->NumAllocations;
                 ++index) {
                BC250_ALLOCATION_PRIVATE_V1 request;
                BC250_MCDM_HANDLE allocation = NULL, openHandle = NULL;
                DXGK_OPENALLOCATIONINFO *info =
                    &OpenAllocation->pOpenAllocation[index];
                BC250_MCDM_OBJECT_RECORD allocationRecord;
                BOOLEAN hasRecord = FALSE;
                RtlZeroMemory(&allocationRecord, sizeof(allocationRecord));
                if (info->hAllocation != 0) {
                    allocation = (BC250_MCDM_HANDLE)(ULONG_PTR)info->hAllocation;
                    objectStatus = Bc250McdmObjectLookup(&context->Objects,
                        allocation, BC250_MCDM_OBJECT_ALLOCATION, NULL, &allocationRecord);
                    if (objectStatus == BC250_MCDM_OBJECT_OK) {
                        hasRecord = TRUE;
                    }
                }
                if (!hasRecord && info->pPrivateDriverData != NULL && info->PrivateDriverDataSize != 0u) {
                    if (Bc250AllocationAbiParse(info->pPrivateDriverData,
                            info->PrivateDriverDataSize, &request) == BC250_ALLOCATION_ABI_OK) {
                        objectStatus = Bc250McdmObjectFindAllocationByToken(
                            &context->Objects, request.AllocationToken, &allocation);
                        if (objectStatus == BC250_MCDM_OBJECT_OK) {
                            objectStatus = Bc250McdmObjectLookup(&context->Objects,
                                allocation, BC250_MCDM_OBJECT_ALLOCATION, NULL,
                                &allocationRecord);
                            if (objectStatus == BC250_MCDM_OBJECT_OK) {
                                hasRecord = TRUE;
                            }
                        }
                    }
                }
                if (hasRecord && info->pPrivateDriverData != NULL && info->PrivateDriverDataSize != 0u) {
                    if (Bc250AllocationAbiParse(info->pPrivateDriverData,
                            info->PrivateDriverDataSize, &request) == BC250_ALLOCATION_ABI_OK) {
                        if (request.ByteSize != 0u &&
                            (allocationRecord.AllocationBytes != request.ByteSize ||
                             allocationRecord.AllocationAlignment != request.Alignment)) {
                            Bc250DisplayLog("[ActiveOpenAlloc] Size/Alignment mismatch at index %u: rec=%llu/%llu req=%llu/%llu\n",
                                index, allocationRecord.AllocationBytes, allocationRecord.AllocationAlignment,
                                request.ByteSize, request.Alignment);
                            result = STATUS_INVALID_PARAMETER;
                            break;
                        }
                    }
                }
                if (objectStatus == BC250_MCDM_OBJECT_OK) {
                    objectStatus = Bc250McdmObjectOpenAllocation(
                        &context->Objects, DeviceContext, allocation,
                        (const void *)(ULONG_PTR)info->hAllocation,
                        &openHandle);
                }
                result = Bc250ObjectStatusToNtStatus(objectStatus);
                Bc250DisplayLog("[ActiveOpenAlloc] Alloc[%u]: hAlloc=%p -> openHandle=%p (status=%u, nt=0x%08X)\n",
                    index, (PVOID)(ULONG_PTR)info->hAllocation, openHandle, (uint32_t)objectStatus, result);
                if (!NT_SUCCESS(result)) break;
                info->hDeviceSpecificAllocation = openHandle;
                ++opened;
            }
            if (!NT_SUCCESS(result)) {
                while (opened != 0u) {
                    DXGK_OPENALLOCATIONINFO *info =
                        &OpenAllocation->pOpenAllocation[--opened];
                    (void)Bc250McdmObjectDestroy(&context->Objects,
                        info->hDeviceSpecificAllocation,
                        BC250_MCDM_OBJECT_OPEN_ALLOCATION,
                        deviceRecord.OwnerProcess);
                    info->hDeviceSpecificAllocation = NULL;
                }
            }
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();

    Bc250DisplayLog("[ActiveOpenAlloc] LEAVE: result=0x%08X\n", result);
    return result;
}

static NTSTATUS Bc250ActiveCloseAllocationLifecycle(
    HANDLE DeviceContext,
    const DXGKARG_CLOSEALLOCATION *CloseAllocation)
{
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        BC250_MCDM_OBJECT_RECORD deviceRecord;
        BC250_MCDM_OBJECT_STATUS objectStatus;
        UINT index, other;
        if (context == NULL) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        objectStatus = Bc250McdmObjectLookup(&context->Objects,
            DeviceContext, BC250_MCDM_OBJECT_DEVICE, NULL, &deviceRecord);
        if (objectStatus != BC250_MCDM_OBJECT_OK) {
            ExReleasePushLockExclusive(&context->ObjectLock);
            continue;
        }
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1) {
            result = STATUS_DEVICE_NOT_READY;
        } else {
            result = STATUS_SUCCESS;
            for (index = 0u; index < CloseAllocation->NumAllocations; ++index) {
                BC250_MCDM_OBJECT_RECORD openRecord;
                HANDLE handle = CloseAllocation->pOpenHandleList[index];
                if (handle == NULL) {
                    result = STATUS_INVALID_PARAMETER;
                    break;
                }
                for (other = 0u; other < index; ++other) {
                    if (CloseAllocation->pOpenHandleList[other] == handle) {
                        result = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
                if (!NT_SUCCESS(result)) break;
                objectStatus = Bc250McdmObjectLookup(&context->Objects,
                    handle, BC250_MCDM_OBJECT_OPEN_ALLOCATION,
                    deviceRecord.OwnerProcess, &openRecord);
                result = Bc250ObjectStatusToNtStatus(objectStatus);
                if (!NT_SUCCESS(result)) break;
                if (openRecord.Parent != DeviceContext) {
                    result = STATUS_INVALID_HANDLE;
                    break;
                }
            }
            for (index = 0u;
                 NT_SUCCESS(result) && index < CloseAllocation->NumAllocations;
                 ++index) {
                if (InterlockedCompareExchange(
                        &context->GpuvaSdmaBrokerReady, 1, 1) == 1) {
                    (void)Bc250GpuvaSdmaBrokerRevokeForObject(
                        &context->GpuvaSdmaBroker, NULL,
                        CloseAllocation->pOpenHandleList[index], NULL);
                }
                objectStatus = Bc250McdmObjectDestroy(&context->Objects,
                    CloseAllocation->pOpenHandleList[index],
                    BC250_MCDM_OBJECT_OPEN_ALLOCATION,
                    deviceRecord.OwnerProcess);
                result = Bc250ObjectStatusToNtStatus(objectStatus);
            }
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

/*
 * B6 / item 25 (07/09): adaptadores do par de TDR. Existem para que
 * bc250_mcdm_callbacks.c nao dependa do modulo de SDMA em tempo de link -- e
 * o mesmo motivo dos outros executores instalados: as suites offline linkam os
 * callbacks sem o transporte fisico. Ponteiro ausente = DDI falha fechada.
 */
static BOOLEAN Bc250ActiveTdrQuiesceLifecycle(VOID)
{
    BC250_SDMA_TDR_SNAPSHOT snapshot;
    BOOLEAN result = Bc250SdmaPresentQuiesceForTdr(&snapshot);
    uint32_t dirIdx;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_Bc250AdapterDirectoryLock);
    for (dirIdx = 0u; dirIdx < BC250_ACTIVE_MAX_ADAPTERS; ++dirIdx) {
        BC250_DXGK_ACTIVE_CONTEXT *activeCtx = g_Bc250AdapterDirectory[dirIdx];
        if (activeCtx != NULL && activeCtx->Signature == BC250_ACTIVE_SIGNATURE) {
            uint32_t faultStatus = 0u, faultAddrLo = 0u, faultAddrHi = 0u;
            uint32_t faultValid = 0u;
            uint32_t selReadback = 0u, restReadback = 0u;
            uint32_t hqdValid = 0u, cpValid = 0u;
            uint32_t hqdActive = 0u, hqdRptr = 0u, hqdWptr = 0u, hqdError = 0u, hqStatus0 = 0u, ibCtrl = 0u, hqdVmid = 0u;
            uint32_t ibBaseLo = 0u, ibBaseHi = 0u, ibRptr = 0u, pqControl = 0u, dequeueReq = 0u;
            uint32_t cpfStatus = 0u, cpfBusy = 0u, cpfStalled = 0u;
            uint32_t cpcStatus = 0u, cpcBusy = 0u, cpcStalled = 0u;
            uint32_t mecMe1Hdr = 0u, mecMe2Hdr = 0u;
            uint32_t mec1InstrPntr = 0u, mec2InstrPntr = 0u;
            uint32_t cpStalled1 = 0u, cpStat = 0u;

            if (Bc250ActiveVmidRead32(activeCtx, BC250_ACTIVE_GCVM_FAULT_STATUS, &faultStatus)) faultValid |= (1u << 0);
            if (Bc250ActiveVmidRead32(activeCtx, BC250_ACTIVE_GCVM_FAULT_ADDR_LO, &faultAddrLo)) faultValid |= (1u << 1);
            if (Bc250ActiveVmidRead32(activeCtx, BC250_ACTIVE_GCVM_FAULT_ADDR_HI, &faultAddrHi)) faultValid |= (1u << 2);

            if (activeCtx->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
                (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                    BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
                (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                    BC250_GRBM_GFX_CNTL_BYTE_OFFSET, activeCtx->SubmissionEngine.SelectedSelector);
                (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                    BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &selReadback);

                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_ACTIVE, &hqdActive)) hqdValid |= (1u << 0);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_PQ_RPTR, &hqdRptr)) hqdValid |= (1u << 1);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_PQ_WPTR_LO, &hqdWptr)) hqdValid |= (1u << 2);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_HQD_ERROR, &hqdError)) hqdValid |= (1u << 3);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_HQ_STATUS0, &hqStatus0)) hqdValid |= (1u << 4);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_IB_CONTROL, &ibCtrl)) hqdValid |= (1u << 5);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_VMID, &hqdVmid)) hqdValid |= (1u << 6);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_IB_BASE_LO, &ibBaseLo)) hqdValid |= (1u << 7);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_IB_BASE_HI, &ibBaseHi)) hqdValid |= (1u << 8);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_IB_RPTR, &ibRptr)) hqdValid |= (1u << 9);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_PQ_CONTROL, &pqControl)) hqdValid |= (1u << 10);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_DEQUEUE_REQUEST, &dequeueReq)) hqdValid |= (1u << 11);

                if ((hqdActive & 1u) != 0u) {
                    uint32_t poll;
                    (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                        BC250_HQD_MMIO_DEQUEUE_REQUEST, 1u);
                    for (poll = 0u; poll < 2000u; ++poll) {
                        if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                                BC250_HQD_MMIO_ACTIVE, &hqdActive) && (hqdActive & 1u) == 0u) {
                            break;
                        }
                        KeStallExecutionProcessor(5u);
                    }
                    (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                        BC250_HQD_MMIO_DEQUEUE_REQUEST, 0u);
                    (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                        BC250_HQD_MMIO_ACTIVE, 0u);
                    Bc250DisplayLog("[TDR_MEC_QUIESCE] HQD dequeue drain: Polls=%u ActiveAfter=%u\n",
                        poll, hqdActive & 1u);
                }

                (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                    BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
                (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                    BC250_GRBM_GFX_CNTL_BYTE_OFFSET, activeCtx->SubmissionEngine.OriginalSelector);
                (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                    BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &restReadback);

                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_CPF_STATUS, &cpfStatus)) cpValid |= (1u << 0);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &cpfBusy)) cpValid |= (1u << 1);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_CPF_STALLED_STAT1, &cpfStalled)) cpValid |= (1u << 2);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_CPC_STATUS, &cpcStatus)) cpValid |= (1u << 3);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_CPC_BUSY_STAT, &cpcBusy)) cpValid |= (1u << 4);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_CPC_STALLED_STAT1, &cpcStalled)) cpValid |= (1u << 5);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_MEC_ME1_HEADER_DUMP, &mecMe1Hdr)) cpValid |= (1u << 6);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_MEC_ME2_HEADER_DUMP, &mecMe2Hdr)) cpValid |= (1u << 7);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_MEC1_INSTR_PNTR, &mec1InstrPntr)) cpValid |= (1u << 8);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_MEC2_INSTR_PNTR, &mec2InstrPntr)) cpValid |= (1u << 9);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_STALLED_STAT1, &cpStalled1)) cpValid |= (1u << 10);
                if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_STAT, &cpStat)) cpValid |= (1u << 11);
            }
            Bc250DisplayLog("[TDR_MEC_SNAPSHOT] Selector=0x%08X (Req=0x%08X) RestoredSel=0x%08X HqdValid=0x%X Active=%08X RPTR=%u WPTR=%u Error=%08X HqStatus=%08X IbCtrl=%08X Vmid=%08X FaultValid=0x%X FaultStatus=%08X FaultAddr=%08X:%08X\n",
                selReadback, activeCtx->SubmissionEngine.SelectedSelector, restReadback,
                hqdValid, hqdActive, hqdRptr, hqdWptr, hqdError, hqStatus0, ibCtrl, hqdVmid,
                faultValid, faultStatus, faultAddrHi, faultAddrLo);
            Bc250DisplayLog("[TDR_MEC_CPF_STAT] CpValid=0x%X CpfStat=%08X CpfBusy=%08X CpfStalled=%08X CpcStat=%08X CpcBusy=%08X CpcStalled=%08X CpStalled1=%08X CpStat=%08X\n",
                cpValid, cpfStatus, cpfBusy, cpfStalled, cpcStatus, cpcBusy, cpcStalled, cpStalled1, cpStat);
            Bc250DisplayLog("[TDR_MEC_EXEC_STAT] Mec1Hdr=%08X Mec2Hdr=%08X Mec1Pntr=%08X Mec2Pntr=%08X IbBase=%08X:%08X IbRptr=%08X PqCtrl=%08X Dequeue=%08X\n",
                mecMe1Hdr, mecMe2Hdr, mec1InstrPntr, mec2InstrPntr, ibBaseHi, ibBaseLo, ibRptr, pqControl, dequeueReq);
            break;
        }
    }
    ExReleasePushLockShared(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

static BOOLEAN Bc250ActiveTdrRestartLifecycle(VOID)
{
    BOOLEAN sdmaRestart = Bc250SdmaPresentRestartAfterTdr();
    BOOLEAN mecRestart = TRUE;
    uint32_t dirIdx;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_Bc250AdapterDirectoryLock);
    for (dirIdx = 0u; dirIdx < BC250_ACTIVE_MAX_ADAPTERS; ++dirIdx) {
        BC250_DXGK_ACTIVE_CONTEXT *activeCtx = g_Bc250AdapterDirectory[dirIdx];
        if (activeCtx != NULL && activeCtx->Signature == BC250_ACTIVE_SIGNATURE) {
            InterlockedExchange(&activeCtx->SdmaRetireBusy, 0);
        }
        if (activeCtx != NULL && activeCtx->Signature == BC250_ACTIVE_SIGNATURE &&
            activeCtx->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED &&
            activeCtx->SubmissionEngine.State == BC250_SUBMISSION_ENGINE_ONLINE) {
            uint32_t active = 0u, rptr = 0u, wptr = 0u, err = 0u, deq = 0u, stall = 0u;
            BC250_TDR_STATUS mecStatus;

            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, activeCtx->SubmissionEngine.SelectedSelector);

            /* Zera os registradores de ponteiro e dequeue */
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_HQD_MMIO_DEQUEUE_REQUEST, 0u);
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_HQD_MMIO_ACTIVE, 0u);
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_HQD_MMIO_PQ_RPTR, 0u);
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_HQD_MMIO_PQ_WPTR_LO, 0u);
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_HQD_MMIO_PQ_WPTR_HI, 0u);

            if (activeCtx->SubmissionEngine.ComputeWptrShadowVirtual != NULL) {
                activeCtx->SubmissionEngine.ComputeWptrShadowVirtual[0] = 0u;
                activeCtx->SubmissionEngine.ComputeWptrShadowVirtual[1] = 0u;
            }

            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_ACTIVE, &active);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_PQ_RPTR, &rptr);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_PQ_WPTR_LO, &wptr);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_DEQUEUE_REQUEST, &deq);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_HQD_ERROR, &err);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_CP_STALLED_STAT1, &stall);

            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, activeCtx->SubmissionEngine.OriginalSelector);

            mecStatus = Bc250TdrValidateMecRestartReadback(active, rptr, wptr, deq, err, stall);
            if (mecStatus != BC250_TDR_OK) {
                Bc250DisplayLog("[TDR_MEC_RESTART] Readback INVALIDO: status=%s Active=%u RPTR=%u WPTR=%u Deq=%u Error=0x%X Stall=0x%X\n",
                    Bc250TdrStatusName(mecStatus), active, rptr, wptr, deq, err, stall);
                mecRestart = FALSE;
            } else {
                Bc250DisplayLog("[TDR_MEC_RESTART] MEC queue rearmada e comprovada no silicio\n");
            }
            break;
        }
    }
    ExReleasePushLockShared(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();

    return (sdmaRestart && mecRestart) ? TRUE : FALSE;
}

static BOOLEAN Bc250ActiveMecProbeResponsive(_Out_ UINT32 *IsResponsive)
{
    uint32_t dirIdx;
    BOOLEAN measured = FALSE;

    if (IsResponsive == NULL) return FALSE;
    *IsResponsive = 0u;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_Bc250AdapterDirectoryLock);
    for (dirIdx = 0u; dirIdx < BC250_ACTIVE_MAX_ADAPTERS; ++dirIdx) {
        BC250_DXGK_ACTIVE_CONTEXT *activeCtx = g_Bc250AdapterDirectory[dirIdx];
        if (activeCtx != NULL && activeCtx->Signature == BC250_ACTIVE_SIGNATURE &&
            activeCtx->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED &&
            activeCtx->SubmissionEngine.State == BC250_SUBMISSION_ENGINE_ONLINE) {
            uint32_t active = 0u, rptr1 = 0u, wptr1 = 0u, rptr2 = 0u, wptr2 = 0u;
            uint32_t isHung = 0u;
            BC250_TDR_STATUS tdrStatus;

            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, activeCtx->SubmissionEngine.SelectedSelector);

            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_ACTIVE, &active);
            if ((active & 1u) == 0u) {
                /* Fila ociosa (idle) -> responsiva por definicao */
                (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                    BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
                (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                    BC250_GRBM_GFX_CNTL_BYTE_OFFSET, activeCtx->SubmissionEngine.OriginalSelector);
                *IsResponsive = 1u;
                measured = TRUE;
                break;
            }

            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_PQ_RPTR, &rptr1);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_PQ_WPTR_LO, &wptr1);

            KeStallExecutionProcessor(50u);

            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_PQ_RPTR, &rptr2);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_HQD_MMIO_PQ_WPTR_LO, &wptr2);

            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, activeCtx->SubmissionEngine.OriginalSelector);

            tdrStatus = Bc250TdrDetectHang(1u, rptr1, wptr1, rptr2, wptr2, &isHung);
            if (tdrStatus == BC250_TDR_OK) {
                *IsResponsive = (isHung == 0u) ? 1u : 0u;
                measured = TRUE;
                Bc250DisplayLog("[TDR_MEC_PROBE] Responsive=%u (RPTR %u->%u WPTR %u->%u)\n",
                    *IsResponsive, rptr1, rptr2, wptr1, wptr2);
            }
            break;
        }
    }
    ExReleasePushLockShared(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();

    return measured;
}

static BOOLEAN Bc250ActiveEngineResponsiveProbe(UINT32 NodeOrdinal, UINT32 *IsResponsive)
{
    if (NodeOrdinal == 0u) {
        return Bc250SdmaPresentProbeResponsive(IsResponsive);
    } else if (NodeOrdinal == 1u) {
        return Bc250ActiveMecProbeResponsive(IsResponsive);
    }
    if (IsResponsive != NULL) *IsResponsive = 0u;
    return FALSE;
}

static NTSTATUS Bc250ActiveCollectDebugLifecycle(
    HANDLE AdapterContext,
    const DXGKARG_COLLECTDBGINFO *CollectDebugInfo)
{
    BC250_MCDM_DEBUG_SNAPSHOT_V1 snapshot;
    uint32_t directoryIndex;
    NTSTATUS result = STATUS_INVALID_HANDLE;

    if (CollectDebugInfo->BufferSize < sizeof(snapshot)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    RtlZeroMemory(&snapshot, sizeof(snapshot));
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (directoryIndex = 0u;
         directoryIndex < BC250_ACTIVE_MAX_ADAPTERS;
         ++directoryIndex) {
        BC250_DXGK_ACTIVE_CONTEXT *context =
            g_Bc250AdapterDirectory[directoryIndex];
        BC250_PROBE_FIELD lastField = (BC250_PROBE_FIELD)0;
        ULONG lastValue = 0u;
        if (context != (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext) continue;
        ExAcquirePushLockExclusive(&context->ObjectLock);
        snapshot.Magic = BC250_MCDM_DEBUG_MAGIC;
        snapshot.Version = BC250_MCDM_DEBUG_VERSION;
        snapshot.StructSize = sizeof(snapshot);
        snapshot.Reason = CollectDebugInfo->Reason;
        snapshot.StateFlags = BC250_MCDM_DEBUG_ADAPTER_REGISTERED;
        if (InterlockedCompareExchange(&context->IsActive, 1, 1) == 1) {
            snapshot.StateFlags |= BC250_MCDM_DEBUG_ACTIVE;
        }
        if (InterlockedCompareExchange(&context->ContextAbiReady, 1, 1) == 1) {
            snapshot.StateFlags |= BC250_MCDM_DEBUG_CONTEXT_ABI_READY;
        }
        if (InterlockedCompareExchange(
                &context->AllocationAbiReady, 1, 1) == 1) {
            snapshot.StateFlags |= BC250_MCDM_DEBUG_ALLOCATION_READY;
        }
        snapshot.FirstFailureStatus =
            (uint32_t)Bc250ProbeGetFirstFailure();
        snapshot.ProbeSequence = Bc250ProbeGetOffline(&lastField, &lastValue);
        snapshot.LastProbeField = (uint32_t)lastField;
        snapshot.LastProbeValue = lastValue;
        snapshot.ObjectLiveCount = context->Objects.LiveCount;
        snapshot.ObjectNextSlot = context->Objects.NextSlot;
        snapshot.SubmissionsCount = context->SubmissionsCount;
        snapshot.ResetsCount = context->ResetsCount;
        RtlCopyMemory(CollectDebugInfo->pBuffer, &snapshot, sizeof(snapshot));
        if (CollectDebugInfo->pExtension != NULL) {
            RtlZeroMemory(CollectDebugInfo->pExtension,
                sizeof(*CollectDebugInfo->pExtension));
            CollectDebugInfo->pExtension->BucketingKey =
                BC250_MCDM_DEBUG_MAGIC;
        }
        ExReleasePushLockExclusive(&context->ObjectLock);
        result = STATUS_SUCCESS;
        break;
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

/*
 * Publica as tres flags de ABI como telemetria.
 *
 * Motivo (3.79, 2026-09-01): o ensaio provou P1/P2/HQD no silicio, terminou em
 * Codigo 43, e a auditoria pediu ContextAbiReady, AllocationAbiReady e
 * RootPageTableAbiReady.  Nenhuma das duas primeiras existia como campo de
 * telemetria — so' como membro volatile do contexto.  Um gate que nao e'
 * observavel nao pode ser auditado.
 *
 * Le sempre o valor REAL do contexto com InterlockedCompareExchange(x,1,1);
 * nunca grava literal.  Publicar um 1 constante aqui seria precisamente o
 * falso sucesso que o projeto proibe.
 *
 * AbiReadyPublishCount existe porque, sem ele, os tres zeros continuariam
 * ambiguos: "as ABIs nao ficaram prontas" e "este publicador nunca rodou"
 * produzem exatamente a mesma leitura.  E' a licao do Schema 133.
 *
 * Definida antes do AddDevice porque o AddDevice tambem publica a linha de
 * base.  NAO chamar no zeramento do StopDevice: apagaria a medicao do
 * StartDevice antes de o operador conseguir ler o registro.
 */
static void Bc250ActivePublishAbiReadyTelemetry(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    static volatile LONG s_publishCount;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    Bc250ProbeRecord(BC250_PROBE_V5_ABI_READY_CONTEXT,
        (ULONG)InterlockedCompareExchange(&Context->ContextAbiReady, 1, 1));
    Bc250ProbeRecord(BC250_PROBE_V5_ABI_READY_ALLOCATION,
        (ULONG)InterlockedCompareExchange(&Context->AllocationAbiReady, 1, 1));
    Bc250ProbeRecord(BC250_PROBE_V5_ABI_READY_ROOT_PAGE_TABLE,
        (ULONG)InterlockedCompareExchange(
            &Context->RootPageTableAbiReady, 1, 1));
    Bc250ProbeRecord(BC250_PROBE_V5_ABI_READY_PUBLISH_COUNT,
        (ULONG)InterlockedIncrement(&s_publishCount));
}

NTSTATUS APIENTRY Bc250DxgkDdiAddDeviceActive(
    CONST PDEVICE_OBJECT PhysicalDeviceObject,
    PVOID *MiniportDeviceContext)
{
    BC250_DXGK_ACTIVE_CONTEXT *context;
    NTSTATUS status;

    Bc250ProbeMarkDdi(BC250_V5_DDI_ADD_DEVICE);
    Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_ENTERED, 1);
    if (MiniportDeviceContext != NULL) *MiniportDeviceContext = NULL;
    if (PhysicalDeviceObject == NULL || MiniportDeviceContext == NULL) {
        Bc250ProbeRecord(
            BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    context = (BC250_DXGK_ACTIVE_CONTEXT *)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(*context), BC250_ACTIVE_POOL_TAG);
    if (context == NULL) {
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = Bc250DxgkReadonlyInitialize(&context->Core);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)status);
        return status;
    }

    context->Signature = BC250_ACTIVE_SIGNATURE;
    context->PhysicalDeviceObject = (PDEVICE_OBJECT)PhysicalDeviceObject;
    context->Core.PhysicalDeviceObject = (PDEVICE_OBJECT)PhysicalDeviceObject;
    RtlZeroMemory(&context->Display, sizeof(context->Display));
    ExInitializePushLock(&context->ObjectLock);
    KeInitializeSpinLock(&context->TlbLock);
    if (Bc250McdmObjectRegistryInitialize(&context->Objects) !=
        BC250_MCDM_OBJECT_OK) {
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (Bc250GpuvaSdmaBrokerInitialize(&context->GpuvaSdmaBroker,
            ((uint64_t)(KeQueryPerformanceCounter(NULL).QuadPart) | 1ull)) !=
        BC250_GPUVA_SDMA_BROKER_OK) {
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    InterlockedExchange(&context->GpuvaSdmaBrokerReady, 1);
    /*
     * O espaco de VA nao pode ser maior do que a page table consegue mapear.
     * Com o modelo plano (PAGE_TABLE_DEPTH=0) a cobertura e'
     * 2^BC250_GPUVA_PT_VA_BITS; declarar 48 bits aqui deixava o espaco aceitar
     * e rastrear ranges que o hardware nunca traduziria -- inconsistencia
     * silenciosa que so' apareceria como falha de traducao.
     */
    if (Bc250GpuvaSpaceInitialize(&context->GpuvaSpace,
            BC250_GPUVA_PT_VA_BITS) !=
        BC250_GPUVA_OK) {
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    InterlockedExchange(&context->GpuvaSpaceReady, 1);
    if (Bc250GpuvaSnapshotStoreInitialize(&context->GpuvaSnapshots, BC250_GPUVA_PT_VA_BITS) !=
            BC250_GPUVA_OK ||
        Bc250ActiveInitializeJournalSet(context) !=
            BC250_GPUVA_JOURNAL_OK) {
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS,
            (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    InterlockedExchange(&context->GpuvaJournalReady, 1);
    context->GpuvaHierarchyPending = 0u;
    context->GpuvaHierarchyProcess = NULL;
    context->GpuvaHierarchyRootPageTable = 0u;
    context->GpuvaHierarchyHighestLevel = 0u;
    InterlockedExchange(&context->GpuvaActivationEligible, 0);
    context->GpuvaLabPromotion = 0u;
    context->GpuvaLabPromotionPresent = 0u;
    context->GpuvaActivationBlockMask =
        BC250_GPUVA_GATE_BLOCK_ADAPTER_INACTIVE |
        BC250_GPUVA_GATE_BLOCK_CONTEXT_ABI |
        BC250_GPUVA_GATE_BLOCK_ALLOCATION_ABI |
        BC250_GPUVA_GATE_BLOCK_ROOT_ABI |
        BC250_GPUVA_GATE_BLOCK_ENGINE_OFFLINE |
        BC250_GPUVA_GATE_BLOCK_SNAPSHOT_EMPTY |
        BC250_GPUVA_GATE_BLOCK_SNAPSHOT_EPOCH;
    context->GpuvaActivationSnapshotGeneration = 0u;
    context->GpuvaActivationSnapshotEpoch = 0u;
    context->GpuvaActivationSnapshotLiveCount = 0u;
    InterlockedExchange(&context->IsActive, 0);
    RtlZeroMemory(&context->ContextAbi, sizeof(context->ContextAbi));
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    context->AllocationSegmentBytes = 0u;
    context->AllocationSegmentSet = 0u;
    context->RootPageTableMaxEntries = 0u;
    /*
     * Linha de base do AddDevice: fixa AbiReadyPublishCount=1 com as tres
     * flags em zero.  Se o registro terminar o ensaio com PublishCount=1, o
     * StartDevice nao chegou a nenhum de seus caminhos de retorno.
     */
    Bc250ActivePublishAbiReadyTelemetry(context);
    context->PostDisplayStatus = BC250_POST_DISPLAY_BAD_ARGUMENT;
    context->PostDisplayCallbackStatus = STATUS_NOT_SUPPORTED;
    RtlZeroMemory(&context->PostDisplayReservation,
                  sizeof(context->PostDisplayReservation));
    RtlZeroMemory(&context->PostDisplayEvidence,
                  sizeof(context->PostDisplayEvidence));
    context->RuntimeInventoryStatus = BC250_RUNTIME_INVENTORY_BAD_ARGUMENT;
    RtlZeroMemory(&context->RuntimeInventoryInput,
                  sizeof(context->RuntimeInventoryInput));
    RtlZeroMemory(&context->RuntimeInventoryReport,
                  sizeof(context->RuntimeInventoryReport));
    RtlZeroMemory(&context->SealedRuntime,
                  sizeof(context->SealedRuntime));
    context->RuntimeStageStatus = STATUS_NOT_SUPPORTED;
    context->SecondPreflightStage = BC250_DXGK_STAGE_EMPTY;
    context->VramProbeStatus = BC250_VRAM_PROBE_BAD_ARGUMENT;
    RtlZeroMemory(&context->VramProbeEvidence,
                  sizeof(context->VramProbeEvidence));
    context->TlbTransactionStatus = BC250_TLB_TRANSACTION_BAD_ARGUMENT;
    RtlZeroMemory(&context->TlbTransactionEvidence,
                  sizeof(context->TlbTransactionEvidence));
    context->GcvmBackingStatus = BC250_GCVM_BACKING_BAD_ARGUMENT;
    RtlZeroMemory(&context->GcvmBackingEvidence,
                  sizeof(context->GcvmBackingEvidence));
    context->GcvmVmid8Status = BC250_GCVM_VMID8_BAD_ARGUMENT;
    RtlZeroMemory(&context->GcvmVmid8Evidence,
                  sizeof(context->GcvmVmid8Evidence));
    context->PspRingStatus = BC250_PSP_RING_BAD_ARGUMENT;
    RtlZeroMemory(&context->PspRingEvidence,
                  sizeof(context->PspRingEvidence));
    context->DeferredPspRingVirtual = NULL;
    context->PspLoadFwStatus = BC250_PSP_LOAD_FW_BAD_ARGUMENT;
    RtlZeroMemory(&context->PspLoadFwEvidence,
                  sizeof(context->PspLoadFwEvidence));
    /* Fail-closed: so' a prova fisica do tripe de boot pode liga-lo. */
    context->SdmaBootTripodValidated = 0u;
    context->PspRlcLoadStatus = BC250_PSP_LOAD_FW_BAD_ARGUMENT;
    RtlZeroMemory(&context->PspRlcLoadEvidence,
                  sizeof(context->PspRlcLoadEvidence));
    RtlZeroMemory(&context->RlcLoadEvidence,
                  sizeof(context->RlcLoadEvidence));
    context->PfpDirectLoadStatus = BC250_PFP_DIRECT_LOAD_BAD_ARGUMENT;
    RtlZeroMemory(&context->PfpDirectLoadEvidence,
                  sizeof(context->PfpDirectLoadEvidence));
    context->HqdPreflightStatus = BC250_HQD_PREFLIGHT_BAD_ARGUMENT;
    RtlZeroMemory(&context->HqdPreflightSnapshot,
                  sizeof(context->HqdPreflightSnapshot));
    context->HqdPreflightReadCount = 0u;
    context->MecHqdSelectorStatus = BC250_HQD_SELECTOR_BAD_ARGUMENT;
    RtlZeroMemory(&context->MecHqdSelectorEvidence,
                  sizeof(context->MecHqdSelectorEvidence));
    context->MecHqdBankProbeStatus = BC250_HQD_BANK_PROBE_BAD_ARGUMENT;
    RtlZeroMemory(&context->MecHqdBankProbeEvidence,
                  sizeof(context->MecHqdBankProbeEvidence));
    context->HqdProgramStatus = BC250_HQD_PROGRAM_BAD_ARGUMENT;
    RtlZeroMemory(&context->HqdProgramEvidence,
                  sizeof(context->HqdProgramEvidence));
    context->HqdWptrDiagStatus = BC250_HQD_WPTR_DIAG_BAD_ARGUMENT;
    RtlZeroMemory(&context->HqdWptrDiagEvidence,
                  sizeof(context->HqdWptrDiagEvidence));
    context->HqdActiveStatus = BC250_HQD_ACTIVE_BAD_ARGUMENT;
    RtlZeroMemory(&context->HqdActiveEvidence,
                  sizeof(context->HqdActiveEvidence));
    RtlZeroMemory(&context->HqdShaderDispatchEvidence,
                  sizeof(context->HqdShaderDispatchEvidence));
    context->HqdActiveEvidence.GfxOffApplicable = 0u;
    context->HqdActiveEvidence.GfxOffGuardPassed = 1u;
    context->HqdActiveEvidence.GfxOffRestorePassed = 1u;
    context->SdmaF32Status = BC250_SDMA_F32_BAD_ARGUMENT;
    RtlZeroMemory(&context->SdmaF32Evidence,
                  sizeof(context->SdmaF32Evidence));
    context->SdmaRingWorkStatus = BC250_SDMA_RING_WORK_BAD_ARGUMENT;
    RtlZeroMemory(&context->SdmaRingWorkEvidence,
                  sizeof(context->SdmaRingWorkEvidence));
    context->DeferredPspLoadRing = NULL;
    context->DeferredPspLoadCommand = NULL;
    context->DeferredPspLoadFence = NULL;
    context->DeferredPspLoadFirmware = NULL;
    context->SubmissionsCount = 0;
    context->ResetsCount = 0;
    context->PersistentSubmission = 0u;
    context->TunablePersistentSubmissionPresent = 0u;
    RtlZeroMemory(&context->SubmissionEngine,
                  sizeof(context->SubmissionEngine));

    status = Bc250ActiveRegisterAdapter(context);
    if (!NT_SUCCESS(status)) {
        context->Signature = 0u;
        ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
        return status;
    }

    /*
     * Arma o portao de admissao do rastreador junto com a criacao do
     * contexto. Sao dois portoes independentes e em profundidade:
     *
     *   AcceptingWork  — propriedade do rastreador, armada aqui, fechada na
     *                    quiescencia do teardown.
     *   LifecycleState — propriedade do contexto, so vai a RUNNING quando o
     *                    StartDevice devolve sucesso; NOT_STARTED e' zero, de
     *                    modo que memoria zerada recusa trabalho.
     *
     * Nenhum dos dois sozinho autoriza submissao: SubmitCommandVirtual exige
     * IsActive == 1 e LifecycleState == RUNNING, e a reserva ainda passa pelo
     * AcceptingWork.
     */
    Bc250WddmFenceTrackerSetAcceptingWork(
        &context->SubmissionEngine.WddmFences, TRUE);
    InterlockedExchange(&context->LifecycleState,
        (LONG)BC250_ACTIVE_LIFECYCLE_NOT_STARTED);

    *MiniportDeviceContext = context;
    Bc250ProbeRecord(BC250_PROBE_ADD_DEVICE_STATUS, (ULONG)STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

/*
 * Arma o ciclo de vida junto com IsActive (3.67).
 *
 * O portao de admissao do rastreador e' fail-closed por construcao: um
 * rastreador zerado tem AcceptingWork == 0 e recusa toda reserva. Isso e'
 * deliberado — esquecer de armar produz um adaptador que nao aceita trabalho,
 * falha alta e imediata, em vez de um adaptador que aceita trabalho que o
 * teardown nao consegue contabilizar.
 *
 * O preco e' que TODO caminho que liga IsActive precisa armar tambem. Sao
 * tres: bring-up pulado cedo, bring-up pulado tarde e bring-up completo.
 * Concentrar aqui evita que um quarto caminho futuro esqueca metade do par.
 */
static void Bc250ActiveArmLifecycle(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    InterlockedExchange(&Context->SubmitInFlight, 0);
    Context->StopReason = BC250_ACTIVE_STOP_REASON_NONE;
    Context->StopSubmitInFlightAfterDrain = 0u;
    Context->StopDrainIterations = 0u;
    Context->StopSnapshotFailures = 0u;
    Bc250WddmFenceTrackerSetAcceptingWork(
        &Context->SubmissionEngine.WddmFences, TRUE);
    InterlockedExchange(&Context->LifecycleState,
        (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING);
}

NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceActive(
    CONST PVOID MiniportDeviceContext,
    PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface,
    PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    NTSTATUS preflightStatus;
#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)
    NTSTATUS persistentStatus = STATUS_NOT_SUPPORTED;
    NTSTATUS selfTestStatus = STATUS_NOT_SUPPORTED;
    NTSTATUS p2Status = STATUS_NOT_SUPPORTED;
    BOOLEAN persistentReady = FALSE;
    /*
     * Os tres NTSTATUS acima nascem em STATUS_NOT_SUPPORTED e SO' sao escritos
     * dentro do ensaio de submissao persistente. Sem esta marca, o rodapé
     * publicava o INICIALIZADOR de persistentStatus como se fosse o veredito do
     * StartDevice -- foi assim que a corrida 08/09 11:48 devolveu 0xC00000BB
     * escondendo a recusa real do segundo preflight.
     */
    BOOLEAN persistentAttempted = FALSE;
#endif

    Bc250ProbeRecord(BC250_PROBE_START_DEVICE_ENTERED, 1);
    if (context == NULL ||
        context->Signature != BC250_ACTIVE_SIGNATURE ||
        DxgkStartInfo == NULL ||
        DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL ||
        NumberOfChildren == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * P0: Avaliacao do fusivel anti-boot-loop persistente.
     * Se a tentativa anterior nao fechou limpa (crash, hang, reboot no meio do ensaio),
     * o fusivel dispara e recusa imediatamente o StartDevice, impedindo o boot loop
     * e devolvendo o BasicDisplay de forma automatica.
     */
    {
        BOOLEAN allowStart = FALSE;
        ULONG prevFuseState = 0;
        NTSTATUS fuseStatus = Bc250BootFuseEvaluateAndArm(&allowStart, &prevFuseState);
        if (!NT_SUCCESS(fuseStatus) || !allowStart) {
            Bc250DisplayLog(
                "[StartDevice] FUSIVEL ANTI-BOOT-LOOP DISPARADO: recusa StartDevice (fuseStatus=%08x, prevFuseState=%u)\n",
                fuseStatus, prevFuseState);
            Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_RETURN_STAGE,
                BC250_V5_START_RETURN_FUSE_TRIPPED);
            Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_STATUS,
                (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
            Bc250ProbeRecordFirstFailure(STATUS_DEVICE_CONFIGURATION_ERROR);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    /*
     * Executa somente a auditoria readonly. O contexto nasce sem runtime
     * staged: ownership exclusivo, inventario de VRAM, reservas herdadas e
     * manifesto precisam vir de produtores fisicos no lifecycle PnP. Ate
     * esses produtores existirem, StartDevice deve falhar fechado.
     */
    preflightStatus = Bc250DxgkDdiStartDeviceReadonly(
        &context->Core,
        DxgkStartInfo,
        DxgkInterface,
        NumberOfVideoPresentSources,
        NumberOfChildren);

    if (preflightStatus == STATUS_NOT_SUPPORTED &&
        context->Core.LastStage == BC250_DXGK_STAGE_RUNTIME_MISSING &&
        context->Core.ResolvedBarCount != 0u &&
        context->Core.TopologyEvidence.AddressSpace.SnapshotGeneration != 0u) {

        /*
         * Gate 1.5 — render-only.
         *
         * Devolve zero sources e zero children, e sai ANTES de adquirir
         * post-display ownership e antes de Bc250DisplayInit.  Isso e' o que
         * torna a corrida repetivel: o GOP continua dono do scanout, o modo
         * herdado nao e' tocado, e um FAIL nao custa tela preta nem boot
         * frio.  A pergunta que este perfil responde e' unicamente se a
         * tabela render/base sobrevive em Codigo 0.
         *
         * As tres flags de ABI ficam em zero, iguais ao caminho de display
         * de producao: promove-las aqui anunciaria objetos WDDM sem backend
         * fisico provado, que e' exatamente o falso sucesso que o projeto
         * proibe.  A promocao e' decisao de um gate posterior, com evidencia
         * propria.
         */
        if (Bc250ActiveGetAdapterProfile() ==
                BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
            Bc250ProbeRecord(BC250_PROBE_SKIP_HW_BRINGUP, 1u);
            /*
             * Marcador positivo: prova que ESTE caminho executou, em vez de
             * deduzi-lo da ausencia de outros campos. Um zero em
             * PostDisplayAttempted so' significa "nao tentou" se existir um
             * um em RenderOnlyEarlyReturn.
             */
            Bc250ProbeRecord(BC250_PROBE_V5_RENDER_ONLY_EARLY_RETURN, 1u);
            Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_RETURN_STAGE,
                BC250_V5_START_RETURN_RENDER_ONLY);
            *NumberOfVideoPresentSources = 0;
            *NumberOfChildren = 0;
#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)
            /*
             * Nao adquirir post-display ownership. O perfil render-only
             * segue diretamente para o bring-up fisico comum e so podera
             * retornar sucesso depois que RuntimeCaps/ABI/submission tiverem
             * backend real. P1/P2 continuam sujeitos aos mesmos gates e
             * provas fisicas do caminho display+render de laboratorio.
             */
            goto Bc250PhysicalRenderBringup;
#else
            /*
             * Uma imagem sem bring-up compilado nao possui backend de
             * render. Retornar sucesso aqui foi o falso positivo medido no
             * 3.76--3.78 (RuntimeMissing=1, todas as ABIs em zero, Codigo
             * 43). Falhar explicitamente e' o unico resultado honesto.
             */
            InterlockedExchange(&context->ContextAbiReady, 0);
            InterlockedExchange(&context->AllocationAbiReady, 0);
            InterlockedExchange(&context->RootPageTableAbiReady, 0);
            InterlockedExchange(&context->IsActive, 0);
            Bc250ActivePublishAbiReadyTelemetry(context);
            Bc250ProbeRecordFirstFailure(STATUS_DEVICE_NOT_READY);
            return STATUS_DEVICE_NOT_READY;
#endif
        }

        Bc250ProbeRecord(BC250_PROBE_V5_POST_DISPLAY_ATTEMPTED, 1u);
        context->PostDisplayStatus = Bc250AcquirePostDisplayReservation(
            &context->Core.DxgkInterface,
            context->Core.ResolvedBars,
            context->Core.ResolvedBarCount,
            BC250_PCI_VRAM_APERTURE_BAR_INDEX,
            context->Core.TopologyEvidence.AddressSpace.SnapshotGeneration,
            &context->PostDisplayReservation,
            &context->PostDisplayEvidence,
            &context->PostDisplayCallbackStatus);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_STATUS,
            (ULONG)context->PostDisplayStatus);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_CALLBACK_STATUS,
            (ULONG)context->PostDisplayCallbackStatus);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_USED_CALLBACK2,
            context->PostDisplayEvidence.UsedOwnershipCallback2);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_FRAMEBUFFER_STATE,
            context->PostDisplayEvidence.FrameBufferState);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_TARGET_ID,
            context->PostDisplayEvidence.TargetId);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_WIDTH,
            context->PostDisplayEvidence.Width);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_HEIGHT,
            context->PostDisplayEvidence.Height);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_PITCH,
            context->PostDisplayEvidence.Pitch);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_PHYSICAL_LOW32,
            (ULONG)context->PostDisplayEvidence.PhysicalStart);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_PHYSICAL_HIGH32,
            (ULONG)(context->PostDisplayEvidence.PhysicalStart >> 32));
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_BYTES,
            (ULONG)context->PostDisplayEvidence.Length);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_VRAM_OFFSET_LOW32,
            (ULONG)context->PostDisplayEvidence.VramOffset);
        Bc250ProbeRecord(BC250_PROBE_POST_DISPLAY_VRAM_OFFSET_HIGH32,
            (ULONG)(context->PostDisplayEvidence.VramOffset >> 32));
        {
            DXGK_DISPLAY_INFORMATION postDisplayInfo;
            RtlZeroMemory(&postDisplayInfo, sizeof(postDisplayInfo));
            postDisplayInfo.Width = context->PostDisplayEvidence.Width;
            postDisplayInfo.Height = context->PostDisplayEvidence.Height;
            postDisplayInfo.Pitch = context->PostDisplayEvidence.Pitch;
            postDisplayInfo.ColorFormat = D3DDDIFMT_A8R8G8B8;
            postDisplayInfo.PhysicAddress.QuadPart = (LONGLONG)context->PostDisplayEvidence.PhysicalStart;
            postDisplayInfo.TargetId = 0;
            postDisplayInfo.AcpiId = 0;
            Bc250ProbeRecord(BC250_PROBE_V5_DISPLAY_INIT_ENTERED, 1u);
            preflightStatus = Bc250DisplayInit(
                &context->Core.DxgkInterface, &postDisplayInfo);
            if (!NT_SUCCESS(preflightStatus)) {
                Bc250DisplayTeardown();
                Bc250ProbeRecordFirstFailure(preflightStatus);
                return preflightStatus;
            }

            {
                uint32_t bIdx;
                PHYSICAL_ADDRESS mmioPhys;
                PHYSICAL_ADDRESS vramPhys;
                mmioPhys.QuadPart = 0;
                vramPhys.QuadPart = 0;
                for (bIdx = 0; bIdx < context->Core.ResolvedBarCount; ++bIdx) {
                    if (context->Core.ResolvedBars[bIdx].Index == BC250_PCI_MMIO_BAR_INDEX) {
                        mmioPhys.QuadPart = (LONGLONG)context->Core.ResolvedBars[bIdx].Start;
                    } else if (context->Core.ResolvedBars[bIdx].Index == BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
                        vramPhys.QuadPart = (LONGLONG)context->Core.ResolvedBars[bIdx].Start;
                    }
                }
                if (mmioPhys.QuadPart != 0) {
                    Bc250DisplaySetMmioBase(mmioPhys);
                }

                /* Carga de Firmware SDMA0 via PSP (Fail-Closed) */
                context->PspRingStatus = Bc250ActiveProbePspRing(context, &context->PspRingEvidence);
                if (context->PspRingStatus == BC250_PSP_RING_OK &&
                    context->PspRingEvidence.PhysicalReady != 0u) {
                    context->PspLoadFwStatus = Bc250ActiveLoadSdmaFirmware(context, &context->PspLoadFwEvidence);
                    Bc250DisplayLog("[SdmaPresent] PSP Load SDMA0 FW: Status=%u, PhysicalReady=%u, F32Status=%u, RingWorkStatus=%u (WptrPub=%u, RptrAfter=%u, Fence=%08X, Mismatches=%u)\n",
                        (uint32_t)context->PspLoadFwStatus,
                        context->PspLoadFwEvidence.PhysicalReady,
                        (uint32_t)context->SdmaF32Status,
                        (uint32_t)context->SdmaRingWorkStatus,
                        context->SdmaRingWorkEvidence.WptrPublished,
                        context->SdmaRingWorkEvidence.RptrRegisterAfter,
                        context->SdmaRingWorkEvidence.FenceObserved,
                        context->SdmaRingWorkEvidence.MismatchCount);
                } else {
                    Bc250DisplayLog("[SdmaPresent] PSP Ring Probe: Status=%u, PhysicalReady=%u\n",
                        (uint32_t)context->PspRingStatus,
                        context->PspRingEvidence.PhysicalReady);
                }

                if (mmioPhys.QuadPart != 0 && vramPhys.QuadPart != 0) {
                    uint64_t vramMcStart = context->Core.TopologyEvidence.AddressSpace.VramMcStart;
                    uint64_t scanoutMc;
                    if (vramMcStart == 0) {
                        vramMcStart = BC250_SDMA_VRAM_MC_BASE;
                    }
                    scanoutMc = vramMcStart;
                    if (g_Bc250DisplayState.FrameBufferPhysical.QuadPart >= vramPhys.QuadPart) {
                        scanoutMc += (uint64_t)(g_Bc250DisplayState.FrameBufferPhysical.QuadPart - vramPhys.QuadPart);
                    }
                    (void)Bc250SdmaPresentInit(mmioPhys, vramPhys, vramMcStart, scanoutMc);
                    /*
                     * Congelar aqui a prova fisica do tripe de boot. E' ela, e
                     * nao um segundo Bc250ActiveLoadSdmaFirmware, que autoriza
                     * a cadeia de bring-up a seguir para o PFP direct load.
                     */
                    context->SdmaBootTripodValidated =
                        g_Bc250SdmaPresent.TripodValidated ? 1u : 0u;
                    Bc250DisplayLog("[SdmaPresent] BootTripodValidated=%u (porta da cadeia de bring-up)\n",
                        context->SdmaBootTripodValidated);
                }
            }
        }

        /*
         * A auditoria readonly pode devolver o transporte vazio. Mapeia-lo e'
         * parte do preflight da MMU; seguir sem ele repetiria a corrida 14:32,
         * que aceitou SetRootPageTable depois de SNAPSHOT_FAILED.
         */
        if (context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_EMPTY) {
            BC250_MMIO_TOPOLOGY_STATUS mmioMapStatus =
                Bc250MmioTopologyMapReadonly(
                    &context->Core.MmioTransport,
                    &context->Core.DxgkInterface,
                    context->Core.ResolvedBars,
                    context->Core.ResolvedBarCount,
                    BC250_PCI_MMIO_BAR_INDEX);
            Bc250DisplayLog("[StartDevice] Global MMU transport map status: %s (State=%u, Base=%p)\n",
                Bc250MmioTopologyStatusName(mmioMapStatus),
                context->Core.MmioTransport.State,
                context->Core.MmioTransport.VirtualBase);
        }
        {
            NTSTATUS globalMmuNtStatus = Bc250ActiveEnsureGlobalMmu(
                context, "StartDevice");
            if (!NT_SUCCESS(globalMmuNtStatus)) {
                InterlockedExchange(&context->IsActive, 0);
                InterlockedExchange(&context->ContextAbiReady, 0);
                InterlockedExchange(&context->AllocationAbiReady, 0);
                InterlockedExchange(&context->RootPageTableAbiReady, 0);
                Bc250DisplayTeardown();
                Bc250ProbeRecordFirstFailure(globalMmuNtStatus);
                Bc250DisplayLog("[StartDevice] RECUSADO fail-closed: MMU global status=0x%08X\n",
                    (uint32_t)globalMmuNtStatus);
                return globalMmuNtStatus;
            }
        }

        {
            /*
             * B.1 -- TUNABLE DO PREFLIGHT DO ANEL GRAFICO, EM FONTE UNICA.
             *
             * DELIBERADAMENTE FORA DO #if, e este comentario existe para que
             * ninguem o mova para dentro. E' a QUARTA vez que a classe aparece
             * neste projeto: A13 (tunable de submissao), B4 (GpuvaLabPromotion)
             * e ContextAbi ficaram num ramo de
             * BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP e eram invisiveis no
             * outro -- cada uma custou uma corrida. A PRIMEIRA VERSAO DESTE
             * BLOCO REPETIU O ERRO: leitura no ramo de producao, uso no ramo
             * de laboratorio, resultado sempre 0 e medicao que nunca rodaria.
             *
             * Fail-closed: valor ausente ou ilegivel mantem a medicao
             * DESLIGADA. A medicao nao escreve registrador algum e nao altera
             * o veredito do StartDevice em desfecho nenhum.
             */
            ULONG gfxRingPresent = 0u;
#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)
            const ULONG gfxRingCompiled = 1u;
#else
            const ULONG gfxRingCompiled = 0u;
#endif
            context->GfxRingPreflightArmed = Bc250ActiveReadTunableChecked(
                L"GfxRingPreflight", 0u, &gfxRingPresent);
            context->GfxRingPreflightPresent = gfxRingPresent;
            /*
             * "compiled" evita a ambiguidade que este projeto ja pagou caro:
             * tunable armado com o codigo ausente da imagem produz silencio
             * indistinguivel de recusa. Na imagem de producao o probe fisico
             * nao existe por desenho, e a linha diz isso.
             */
            Bc250DisplayLog("[StartDevice] GfxRingPreflight=%u (present=%u compiled=%u) -- 0 = sem medicao do anel grafico (default); 1 fotografa CP_RB0 SEM escrever\n",
                context->GfxRingPreflightArmed, context->GfxRingPreflightPresent,
                gfxRingCompiled);
            if (context->GfxRingPreflightArmed == 1u && gfxRingCompiled == 0u) {
                Bc250DisplayLog("[GfxRing] PREFLIGHT ARMADO mas AUSENTE desta imagem: o probe fisico so' existe no build de laboratorio. Nenhuma medicao sera feita.\n");
            }

            /*
             * B.1 -- ENSAIO DO ANEL GRAFICO, escalonado. Mesma regra do
             * tunable acima: lido FORA do #if, para que as duas imagens o
             * enxerguem (A13/B4/ContextAbi custaram uma corrida cada).
             *
             * 0 = nada (default). 1 = configura e confere por readback.
             * 2 = acrescenta desalt das micro-engines e a partida do anel.
             * O rollback e' SEMPRE executado: o ensaio mede, nao deixa anel
             * montado, e o StartDevice termina com a placa como a encontrou.
             */
            {
                ULONG gfxBringupPresent = 0u;
                context->GfxRingBringupLevel = Bc250ActiveReadTunableChecked(
                    L"GfxRingBringup", 0u, &gfxBringupPresent);
                context->GfxRingBringupPresent = gfxBringupPresent;
                if (context->GfxRingBringupLevel > 2u) {
                    Bc250DisplayLog("[GfxRing] BRINGUP RECUSADO: level=%u fora da faixa 0..2 -- tratado como 0\n",
                        context->GfxRingBringupLevel);
                    context->GfxRingBringupLevel = 0u;
                }
                Bc250DisplayLog("[StartDevice] GfxRingBringup=%u (present=%u compiled=%u) -- 0 = nenhum ensaio (default); 1 configura e confere; 2 acrescenta desalt e partida\n",
                    context->GfxRingBringupLevel, context->GfxRingBringupPresent,
                    gfxRingCompiled);
            }
        }

#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)
        /*
         * REGRESSAO MEDIDA NA CORRIDA 08/09 11:48 (Codigo 43, 0xC00000BB).
         *
         * O bloco acima deixa Core.MmioTransport MAPEADO. No caminho de
         * laboratorio ainda falta o SEGUNDO preflight readonly, e
         * Bc250DxgkDdiStartDeviceReadonly RECUSA com STATUS_INVALID_PARAMETER
         * quando MmioTransport.State != BC250_MMIO_TOPOLOGY_EMPTY -- a guarda
         * existe para que o preflight rode sobre transporte limpo, e esta
         * certa. Quem estava errado era a ordem.
         *
         * Efeito medido: SecondPreflightStage ficou em 3 (RUNTIME_MISSING,
         * residuo do PRIMEIRO preflight -- prova de que a segunda chamada saiu
         * antes de escrever LastStage) e SecondReadonlyGatePassed em 0, o que
         * pulou TODO o bring-up fisico. Contra o bind 09:50 do MESMO perfil,
         * que fechou com estagio 8 e Codigo 0.
         *
         * Desmapear aqui e' seguro e nao desfaz nada: os registradores da MMU
         * global JA foram programados (GlobalMmuReady=TRUE, e o sitio
         * preguicoso de SetRootPageTable so' reprograma quando ele e' FALSE), e
         * cada transacao do bring-up fisico mapeia e desmapeia o transporte por
         * conta propria -- Bc250MmioTopologyMapReadonly devolve WRONG_STATE se
         * encontrar o transporte ja mapeado, entao deixa-lo mapeado quebraria
         * o bring-up inteiro, e nao apenas o preflight.
         *
         * O caminho de producao NAO passa por aqui de proposito: ele mantem o
         * transporte mapeado durante toda a vida do dispositivo (ver o mapa de
         * runtime logo abaixo) e nao executa um segundo preflight.
         */
        if (context->Core.MmioTransport.State != BC250_MMIO_TOPOLOGY_EMPTY) {
            BC250_MMIO_TOPOLOGY_STATUS mmioUnmapStatus =
                Bc250MmioTopologyUnmap(&context->Core.MmioTransport);
            Bc250DisplayLog("[StartDevice] Transporte devolvido ao preflight: unmap=%s (State=%u)\n",
                Bc250MmioTopologyStatusName(mmioUnmapStatus),
                context->Core.MmioTransport.State);
            if (mmioUnmapStatus != BC250_MMIO_TOPOLOGY_OK ||
                context->Core.MmioTransport.State !=
                    BC250_MMIO_TOPOLOGY_EMPTY) {
                /*
                 * Falha fechada: seguir daqui garantiria a recusa do segundo
                 * preflight, isto e', repetiria a 11:48 sem dizer por que.
                 */
                InterlockedExchange(&context->IsActive, 0);
                InterlockedExchange(&context->ContextAbiReady, 0);
                InterlockedExchange(&context->AllocationAbiReady, 0);
                InterlockedExchange(&context->RootPageTableAbiReady, 0);
                Bc250DisplayTeardown();
                Bc250ProbeRecordFirstFailure(STATUS_DEVICE_CONFIGURATION_ERROR);
                Bc250DisplayLog("[StartDevice] RECUSADO fail-closed: transporte MMIO nao voltou a vazio antes do segundo preflight\n");
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
        }
#endif

        /*
         * C3.9 -- LIDO FORA DO #if DE PROPOSITO.
         *
         * A primeira versao desta leitura ficou dentro de
         * "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)", ou
         * seja, SO' na imagem de PRODUCAO -- e o perfil que roda os ensaios
         * e o de LABORATORIO. O tunable nunca seria lido justamente na
         * imagem que precisa dele. QUARTA ocorrencia da classe "campo escrito
         * em um so' ramo do #if": A13 (tunable de submissao), B4
         * (GpuvaLabPromotion) e ContextAbi (doc 202).
         *
         * Pego por conferencia de string no SYS assinado, nao por teste.
         */
            {
                /*
                 * C3.9b: granularidade do bloqueio por journal. Default 0
                 * (fail-closed) mantem o comportamento medido ate' aqui.
                 */
                ULONG perProcessPresent = 0u;
                context->Node1JournalGatePerProcess = Bc250ActiveReadTunableChecked(
                    L"Node1JournalGatePerProcess", 0u, &perProcessPresent);
                context->Node1JournalGatePerProcessPresent = perProcessPresent;
                Bc250DisplayLog("[StartDevice] Node1JournalGatePerProcess=%u (present=%u) -- 0 = bloqueio GLOBAL do journal (default, "
                    "comportamento da corrida 13:29); 1 = bloqueia so' se o PROCESSO QUE SUBMETE estiver pendente\n",
                    context->Node1JournalGatePerProcess,
                    context->Node1JournalGatePerProcessPresent);
            }

#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)
        /*
         * Production display image: StartDevice ends after inheriting the GOP
         * scanout.  Compute/GCVM/CP/HQD transactions are deliberately absent
         * from this compiled path; a registry value must never be able to turn
         * destructive laboratory bring-up on during PnP display ownership.
         */
        Bc250ProbeRecord(BC250_PROBE_SKIP_HW_BRINGUP, 1u);
        Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_RETURN_STAGE,
            BC250_V5_START_RETURN_DISPLAY_OK);
        *NumberOfVideoPresentSources = 1;
        *NumberOfChildren = 1;
        ULONG enableSyntheticEdid = Bc250ActiveReadTunable(L"EnableSyntheticEdid", 0u);
        ULONG gpuMmuDeclarePresent = 0u;
        Bc250DisplaySetSyntheticEdidEnabled(enableSyntheticEdid != 0u);
        /*
         * Este e' o UNICO caminho compilado no pacote de producao. O bloco que
         * le os demais tunables vive no #else de
         * BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP, isto e', no bring-up de
         * laboratorio -- por isso GpuvaLabPromotionPresent saiu 0 em TODAS as
         * corridas apesar de o INF mandar 1, e por isso a corrida 15:57:42
         * declarou Mem(VA=0, GpuMmu=0) mesmo com GpuMmuDeclare=1 no INF.
         * Ler aqui, antes de qualquer Bc250ActiveQueryRuntimeCaps.
         */
        context->GpuMmuDeclare = Bc250ActiveReadTunableChecked(
            L"GpuMmuDeclare", 0u, &gpuMmuDeclarePresent);
        context->GpuMmuDeclarePresent = gpuMmuDeclarePresent;
        Bc250DisplayLog("[StartDevice] GpuMmuDeclare=%u (present=%u)\n",
            context->GpuMmuDeclare, context->GpuMmuDeclarePresent);
        /*
         * A13: lido AQUI, no unico caminho compilado no pacote de producao --
         * o bloco de tunables de laboratorio vive no #else de
         * BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP e foi exatamente por isso
         * que GpuvaLabPromotion saiu present=0 em todas as corridas.
         */
        {
            /*
             * B3 (07/09): o default virou INDIRECT. SdmaPlanoAFallback=1
             * RESTAURA a precedencia dos PLANO A e existe apenas para
             * bisseccao -- valor ausente ou ilegivel mantem os PLANO A
             * DESLIGADOS.
             */
            ULONG planoAPresent = 0u;
            context->SdmaPlanoAFallback = Bc250ActiveReadTunableChecked(
                L"SdmaPlanoAFallback", 0u, &planoAPresent);
            context->SdmaPlanoAFallbackPresent = planoAPresent;
            Bc250DisplayLog("[StartDevice] SdmaPlanoAFallback=%u (present=%u) -- 0 = somente INDIRECT (default); 1 restaura a precedencia dos PLANO A\n",
                context->SdmaPlanoAFallback, context->SdmaPlanoAFallbackPresent);
        }
        {
            /*
             * B4 (07/09) -- o gate do Node 1 nunca podia abrir no pacote de
             * producao. GpuvaMappingsOperational so' vai a 1 quando
             * GpuvaLabPromotion esta PRESENTE e em 1 (ver ActiveFlushTlb), e
             * essa leitura existia SO' no #else de
             * BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP -- isto e', fora do
             * pacote ativo. Resultado medido: 2 contextos Node 1 criados e
             * ZERO submissoes, com recusa muda.
             *
             * Ler aqui NAO abre nada por si: o default e' 0, e o INF ativo
             * grava 0. Abrir e' decisao do dono do projeto, e a elegibilidade
             * (BlockMask) ainda tem de fechar.
             */
            ULONG promotionPresent = 0u;
            context->GpuvaLabPromotion = Bc250ActiveReadTunableChecked(
                L"GpuvaLabPromotion", 0u, &promotionPresent);
            context->GpuvaLabPromotionPresent = promotionPresent;
            Bc250ProbeRecord(BC250_PROBE_GPUVA_LAB_PROMOTION_REQUESTED,
                context->GpuvaLabPromotion);
            Bc250ProbeRecord(BC250_PROBE_GPUVA_LAB_PROMOTION_PRESENT,
                context->GpuvaLabPromotionPresent);
            Bc250DisplayLog("[StartDevice] GpuvaLabPromotion=%u (present=%u) -- gate de submissao Node 1; 0 = fechado (default)\n",
                context->GpuvaLabPromotion, context->GpuvaLabPromotionPresent);
        }
        {
            /*
             * item 25: hang induzido. Default 0 e fail-closed -- valor ausente
             * ou ilegivel NAO arma. Armar e' decisao do dono do projeto, e a
             * consequencia esperada e' TDR real; se o TDR nao funcionar, o
             * desfecho e' BugCheck -- que e' exatamente o que se quer medir.
             */
            ULONG induceHangPresent = 0u;
            context->TdrInduceHang = Bc250ActiveReadTunableChecked(
                L"TdrInduceHang", 0u, &induceHangPresent);
            context->TdrInduceHangPresent = induceHangPresent;
            InterlockedExchange(&context->TdrHangInduced, 0);
            Bc250DisplayLog("[StartDevice] TdrInduceHang=%u (present=%u) -- 1 arma UM hang de proposito no Node 0 para exercitar o TDR\n",
                context->TdrInduceHang, context->TdrInduceHangPresent);
        }
        Bc250DisplayLog("[StartDevice] Sources=1, Children=1, Profile=%d, SyntheticEdid=%u\n",
            (int)Bc250ActiveGetAdapterProfile(), enableSyntheticEdid);
        if (Bc250ActiveGetAdapterProfile() == BC250_ADAPTER_PROFILE_DISPLAY_RENDER) {
            Bc250ActiveInitializeContextAbi(context);
            context->AllocationSegmentSet = 1u;
            {
                BC250_MCDM_RUNTIME_CAPS caps;
                if (NT_SUCCESS(Bc250ActiveQueryRuntimeCaps(context, &caps))) {
                    context->AllocationSegmentBytes = caps.LocalSegmentBytes;
                }
            }
            /*
             * Numero de entradas da root page table do modelo plano.
             * Bc250McdmSetRootPageTable calcula 1 << PageTableIndexBitCount e
             * Bc250ActiveSetRootPageTableObject exige igualdade exata com este
             * campo. Ele so' era ZERADO -- mesma classe de omissao que
             * GpuVaReady --, entao toda DxgkDdiSetRootPageTable era recusada
             * com DEVICE_NOT_READY e o handler de UPDATE_PAGE_TABLE nunca
             * encontrava root page table para o processo.
             */
            context->RootPageTableMaxEntries =
                BC250_GPUVA_PT_ROOT_MAX_ENTRIES;
            InterlockedExchange(&context->ContextAbiReady, 1);
            InterlockedExchange(&context->AllocationAbiReady, 1);
            InterlockedExchange(&context->RootPageTableAbiReady, 1);
            /*
             * GpuvaMappingsOperational NAO e' ligado aqui.
             *
             * Estava sendo posto em 1 incondicionalmente no StartDevice: antes
             * de existir page table, antes de qualquer PTE escrita e antes de
             * qualquer ACK de flush -- atropelando o gate de promocao que o
             * proprio driver implementa no caminho de FLUSH_TLB
             * (GpuvaLabPromotion + GpuvaActivationEligible). Declarar
             * submissao operacional sem prova viola a Lei 4. O unico lugar
             * legitimo para liga-lo e' depois de um commit de flush aceito.
             */
        } else {
            InterlockedExchange(&context->ContextAbiReady, 0);
            InterlockedExchange(&context->AllocationAbiReady, 0);
            InterlockedExchange(&context->RootPageTableAbiReady, 0);
        }
        /*
         * Mantem MmioTransport mapeado durante o tempo de vida do dispositivo.
         * Operacoes em tempo de execucao (FLUSH_TLB do GFXHUB, submissoes de
         * comando, leitura/escrita de registradores) necessitam de acesso a MMIO
         * e nao podem chamar DxgkCbMapMemory em DISPATCH_LEVEL.
         */
        if (context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_EMPTY) {
            BC250_MMIO_TOPOLOGY_STATUS mmioMapStatus = Bc250MmioTopologyMapReadonly(
                &context->Core.MmioTransport, &context->Core.DxgkInterface,
                context->Core.ResolvedBars, context->Core.ResolvedBarCount,
                BC250_PCI_MMIO_BAR_INDEX);
            Bc250DisplayLog("[StartDevice] Runtime MmioTransport map status: %s (State=%u, Base=%p)\n",
                Bc250MmioTopologyStatusName(mmioMapStatus),
                context->Core.MmioTransport.State,
                context->Core.MmioTransport.VirtualBase);
        }

        InterlockedExchange(&context->IsActive, 1);
        Bc250ActiveArmLifecycle(context);
        Bc250ActivePublishAbiReadyTelemetry(context);
        return STATUS_SUCCESS;
    }

    InterlockedExchange(&context->IsActive, 0);
    Bc250ActiveRetirementTimerStop(context);
    Bc250ActiveSdmaRetirementTimerStop(context);

    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    Bc250ActivePublishAbiReadyTelemetry(context);
    Bc250ProbeRecordFirstFailure(preflightStatus);
    return preflightStatus;
#else
Bc250PhysicalRenderBringup:
        {
            ULONG skipHw = Bc250ActiveReadTunable(L"SkipHwBringup", 1);
            ULONG present = 0u;
            Bc250ProbeRecord(BC250_PROBE_SKIP_HW_BRINGUP, skipHw);
            /*
             * Marca a ENTRADA no bloco compilado com
             * BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP. Num pacote seguro
             * este campo tem de ficar zero: se vier 1, o binario instalado
             * nao e' o que o manifesto diz que e'.
             */
            /* A leitura do marcador garante que o linker o preserve. */
            Bc250ProbeRecord(BC250_PROBE_V5_UNSAFE_BRINGUP_ENTERED,
                g_Bc250UnsafeBringupMarker[0] != L'\0' ? 1u : 0u);
            Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_RETURN_STAGE,
                BC250_V5_START_RETURN_UNSAFE_BRINGUP);

            /* Os tunables do carregador do CP sao lidos AQUI, e nao no sitio de
             * uso: este ponto e' PASSIVE_LEVEL comprovado — e' onde
             * SkipHwBringup funciona desde sempre. Lidos no sitio de uso, os
             * tres voltavam sempre o default. */
            context->GcvmInvalidateEngine = Bc250ActiveReadTunableChecked(
                L"GcvmInvalidateEngine",
                BC250_GCVM_INVALIDATE_ENGINE_DEFAULT, &present);
            context->TunableEnginePresent = present;
            context->GcvmInvalidateSemaphore = Bc250ActiveReadTunableChecked(
                L"GcvmInvalidateSemaphore", 0u, &present);
            context->TunableSemaphorePresent = present;
            context->CpSoftReset = Bc250ActiveReadTunableChecked(
                L"CpSoftReset", 0u, &present);
            context->TunableSoftResetPresent = present;
            context->CpVmid0PageTable = Bc250ActiveReadTunableChecked(
                L"Vmid0PageTable", 0u, &present);
            context->TunableVmid0Present = present;
            context->ShaderDispatch = Bc250ActiveReadTunableChecked(
                L"ShaderDispatch", 0u, &present);
            context->TunableShaderDispatchPresent = present;
            context->PersistentSubmission = Bc250ActiveReadTunableChecked(
                L"PersistentSubmission", 0u, &present);
            context->TunablePersistentSubmissionPresent = present;
            context->GpuvaLabPromotion = Bc250ActiveReadTunableChecked(
                L"GpuvaLabPromotion", 0u, &present);
            context->GpuvaLabPromotionPresent = present;
            Bc250ProbeRecord(BC250_PROBE_GPUVA_LAB_PROMOTION_REQUESTED,
                context->GpuvaLabPromotion);
            Bc250ProbeRecord(BC250_PROBE_GPUVA_LAB_PROMOTION_PRESENT,
                context->GpuvaLabPromotionPresent);
            context->GpuMmuDeclare = Bc250ActiveReadTunableChecked(
                L"GpuMmuDeclare", 0u, &present);
            context->GpuMmuDeclarePresent = present;
            /* 1 preserva o comportamento atual (liga AUTO_CTXSW_ENABLE antes do
             * kick do anel). O upstream faz o contrario durante o bring-up. */
            context->SdmaAutoCtxSw =
                Bc250ActiveReadTunableChecked(L"SdmaAutoCtxSw", 1u, &present);
            context->SdmaWakeOnExIdle =
                Bc250ActiveReadTunableChecked(L"SdmaWakeOnExIdle", 0u, &present);
            context->SdmaEarlyClkUngate =
                Bc250ActiveReadTunableChecked(L"SdmaEarlyClkUngate", 0u, &present);
            Bc250ProbeRecord(BC250_PROBE_TUNABLE_READ_OK,
                (context->TunableEnginePresent << 0) |
                (context->TunableSemaphorePresent << 1) |
                (context->TunableSoftResetPresent << 2) |
                (context->TunableVmid0Present << 3) |
                (context->TunableShaderDispatchPresent << 4) |
                (context->TunablePersistentSubmissionPresent << 5));
            if (skipHw != 0) {
                if (Bc250ActiveGetAdapterProfile() ==
                        BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
                    InterlockedExchange(&context->IsActive, 0);
                    InterlockedExchange(&context->ContextAbiReady, 0);
                    InterlockedExchange(&context->AllocationAbiReady, 0);
                    InterlockedExchange(&context->RootPageTableAbiReady, 0);
                    Bc250ActivePublishAbiReadyTelemetry(context);
                    Bc250ProbeRecordFirstFailure(STATUS_DEVICE_NOT_READY);
                    return STATUS_DEVICE_NOT_READY;
                }
                if (NumberOfVideoPresentSources != NULL) *NumberOfVideoPresentSources = 1;
                if (NumberOfChildren != NULL) *NumberOfChildren = 1;
                if (context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_EMPTY) {
                    BC250_MMIO_TOPOLOGY_STATUS mmioMapStatus = Bc250MmioTopologyMapReadonly(
                        &context->Core.MmioTransport, &context->Core.DxgkInterface,
                        context->Core.ResolvedBars, context->Core.ResolvedBarCount,
                        BC250_PCI_MMIO_BAR_INDEX);
                    Bc250DisplayLog("[StartDevice] Runtime MmioTransport map status: %s (State=%u, Base=%p)\n",
                        Bc250MmioTopologyStatusName(mmioMapStatus),
                        context->Core.MmioTransport.State,
                        context->Core.MmioTransport.VirtualBase);
                }
                InterlockedExchange(&context->IsActive, 1);
                Bc250ActiveArmLifecycle(context);
                /*
                 * Fonte unica: sem isto o laboratorio publicava
                 * DmaBuf=0x0/SegSet=0x0 (corrida 14:06). Tem de vir ANTES
                 * de ContextAbiReady, e ha' gate estatico exigindo a ordem.
                 */
                Bc250ActiveInitializeContextAbi(context);
                context->RootPageTableMaxEntries =
                    BC250_GPUVA_PT_ROOT_MAX_ENTRIES;
                InterlockedExchange(&context->ContextAbiReady, 1);
                InterlockedExchange(&context->AllocationAbiReady, 1);
                InterlockedExchange(&context->RootPageTableAbiReady, 1);
                Bc250ActivePublishAbiReadyTelemetry(context);
                return STATUS_SUCCESS;
            }
        }

        context->RuntimeInventoryInput.EvidenceFlags =
            BC250_RUNTIME_EVIDENCE_SCANOUT_PROBED |
            BC250_RUNTIME_EVIDENCE_DISCOVERY_TMR_PROBED |
            BC250_RUNTIME_EVIDENCE_PLATFORM_CARVEOUTS_PROBED;
        if (context->PostDisplayStatus == BC250_POST_DISPLAY_OK) {
            context->RuntimeInventoryInput.HasScanout = 1u;
            context->RuntimeInventoryInput.ScanoutReservation =
                context->PostDisplayReservation;
        }
        if (context->Core.LastDiscoveryTmrStatus ==
            BC250_DISCOVERY_TMR_OK) {
            context->RuntimeInventoryInput.HasDiscoveryTmr = 1u;
            context->RuntimeInventoryInput.DiscoveryTmrEvidence =
                context->Core.DiscoveryTmrEvidence;
        }
        context->RuntimeInventoryInput.PlatformCarveoutCount = 0u;
        if (context->GlobalMmuDummyAllocated) {
            BC250_INHERITED_VRAM_RESERVATION *dummy =
                &context->RuntimeInventoryInput.PlatformCarveouts[
                    context->RuntimeInventoryInput.PlatformCarveoutCount++];
            dummy->Offset = context->GlobalMmuDummyOffset;
            dummy->Length = 0x1000ull;
            dummy->Alignment = 0x1000u;
            dummy->Role = BC250_VRAM_ROLE_PLATFORM_RESERVED;
            dummy->Flags = BC250_VRAM_REGION_PERSISTENT |
                BC250_VRAM_ALLOC_CPU_VISIBLE;
        }
        if (context->Core.TopologyEvidence.AddressSpace.TotalBytes >
            context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes) {
            BC250_INHERITED_VRAM_RESERVATION *opaque =
                &context->RuntimeInventoryInput.PlatformCarveouts[
                    context->RuntimeInventoryInput.PlatformCarveoutCount++];
            opaque->Offset =
                context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes;
            opaque->Length =
                context->Core.TopologyEvidence.AddressSpace.TotalBytes -
                context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes;
            opaque->Alignment = 0x1000u;
            opaque->Role = BC250_VRAM_ROLE_PLATFORM_RESERVED;
            opaque->Flags = BC250_VRAM_REGION_PERSISTENT;
        }
        context->RuntimeInventoryStatus = Bc250BuildRuntimeVramInventory(
            &context->RuntimeInventoryInput,
            context->Core.TopologyEvidence.AddressSpace.TotalBytes,
            context->Core.TopologyEvidence.AddressSpace.CpuVisibleBytes,
            context->Core.TopologyEvidence.AddressSpace.SnapshotGeneration,
            &context->RuntimeInventoryReport);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_STATUS,
            (ULONG)context->RuntimeInventoryStatus);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_EVIDENCE_FLAGS,
            context->RuntimeInventoryReport.EvidenceFlags);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_RESERVATION_COUNT,
            context->RuntimeInventoryReport.ReservationCount);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_RESERVED_LOW32,
            (ULONG)context->RuntimeInventoryReport.TotalReservedBytes);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_RESERVED_HIGH32,
            (ULONG)(context->RuntimeInventoryReport.TotalReservedBytes >> 32));
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_COMPLETE,
            context->RuntimeInventoryStatus == BC250_RUNTIME_INVENTORY_OK ?
                1u : 0u);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_FREE_LOW32,
            (ULONG)context->RuntimeInventoryReport.TotalFreeBytes);
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_FREE_HIGH32,
            (ULONG)(context->RuntimeInventoryReport.TotalFreeBytes >> 32));
        Bc250ProbeRecord(BC250_PROBE_RUNTIME_INVENTORY_CPU_FREE_LOW32,
            (ULONG)context->RuntimeInventoryReport.CpuVisibleFreeBytes);
        if (context->RuntimeInventoryStatus == BC250_RUNTIME_INVENTORY_OK) {
            RtlZeroMemory(&context->SealedRuntime,
                sizeof(context->SealedRuntime));
            context->SealedRuntime.ReservationCount =
                context->RuntimeInventoryReport.ReservationCount;
            RtlCopyMemory(context->SealedRuntime.Reservations,
                context->RuntimeInventoryReport.Reservations,
                sizeof(context->RuntimeInventoryReport.Reservations));
            context->SealedRuntime.VramApertureBarIndex =
                BC250_PCI_VRAM_APERTURE_BAR_INDEX;
            context->SealedRuntime.MmioBarIndex = BC250_PCI_MMIO_BAR_INDEX;
            context->SealedRuntime.TopologyReaderIdentityFlags =
                BC250_TOPOLOGY_READER_IDENTITY_REQUIRED;
            context->SealedRuntime.RuntimeOwnershipFlags =
                context->RuntimeInventoryReport.OwnershipEvidenceFlags;
            context->SealedRuntime.SnapshotGeneration =
                context->RuntimeInventoryReport.SnapshotGeneration;
            context->SealedRuntime.ManifestCount =
                BC250_CP_BOOTSTRAP_FIRMWARE_COUNT;
            Bc250GetCpFirmwareManifestContract(
                &context->SealedRuntime.Policy,
                context->SealedRuntime.Manifest);
            context->RuntimeStageStatus = Bc250DxgkReadonlyStageRuntime(
                &context->Core, &context->SealedRuntime);
            Bc250ProbeRecord(BC250_PROBE_RUNTIME_STAGE_STATUS,
                (ULONG)context->RuntimeStageStatus);
            if (NT_SUCCESS(context->RuntimeStageStatus)) {
                preflightStatus = Bc250DxgkDdiStartDeviceReadonly(
                    &context->Core, DxgkStartInfo, DxgkInterface,
                    NumberOfVideoPresentSources, NumberOfChildren);
                context->SecondPreflightStage = context->Core.LastStage;
                Bc250ProbeRecord(BC250_PROBE_SECOND_PREFLIGHT_STAGE,
                    context->SecondPreflightStage);
                Bc250ProbeRecord(BC250_PROBE_SECOND_READONLY_GATE_PASSED,
                    context->Core.ReadonlyGatePassed);
                /*
                 * O ESTAGIO SOZINHO E' AMBIGUO: quando a segunda chamada e'
                 * recusada na guarda de entrada, ela retorna ANTES de escrever
                 * LastStage, e o campo continua exibindo o estagio do PRIMEIRO
                 * preflight. Foi assim que a corrida 11:48 mostrou "3" para uma
                 * chamada que nunca chegou a avaliar RuntimeStaged. O status
                 * cru desambigua, e o estado do transporte nomeia a causa mais
                 * provavel de uma recusa de entrada.
                 */
                Bc250ProbeRecord(BC250_PROBE_SECOND_PREFLIGHT_STATUS,
                    (ULONG)preflightStatus);
                Bc250DisplayLog("[StartDevice] Segundo preflight: status=0x%08X, stage=%u, gate=%u, MmioState=%u\n",
                    (uint32_t)preflightStatus,
                    context->SecondPreflightStage,
                    context->Core.ReadonlyGatePassed,
                    context->Core.MmioTransport.State);
                if (context->Core.ReadonlyGatePassed == 1u &&
                    context->Core.LastStage == BC250_DXGK_STAGE_PREFLIGHT_READY) {
                    context->HqdPreflightStatus =
                        Bc250ActiveProbeHqdReadonly(
                            context, &context->HqdPreflightSnapshot,
                            &context->HqdPreflightReadCount);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PREFLIGHT_STATUS,
                        (ULONG)context->HqdPreflightStatus);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PREFLIGHT_READ_COUNT,
                        context->HqdPreflightReadCount);
                    Bc250ProbeRecord(BC250_PROBE_HQD_STABLE_PASSES,
                        context->HqdPreflightSnapshot.StablePasses);
                    Bc250ProbeRecord(BC250_PROBE_HQD_SELECTOR,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_SELECTOR_BEFORE]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_ACTIVE]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_MQD_BASE_LO,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_MQD_BASE_LO]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_MQD_BASE_HI,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_MQD_BASE_HI]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_VMID,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_VMID]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PQ_BASE_LO,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_BASE_LO]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PQ_BASE_HI,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_BASE_HI]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PQ_RPTR,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_RPTR]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_DOORBELL_CONTROL,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_DOORBELL_CONTROL]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PQ_CONTROL,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_CONTROL]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PQ_WPTR_LO,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_WPTR_LO]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PQ_WPTR_HI,
                        context->HqdPreflightSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_WPTR_HI]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_INACTIVE,
                        context->HqdPreflightSnapshot.HqdWasInactive);
                    if (context->HqdPreflightStatus ==
                            BC250_HQD_PREFLIGHT_SELECTOR_NOT_COMPUTE &&
                        context->HqdPreflightSnapshot.HqdWasInactive != 0u) {
                        context->MecHqdSelectorStatus =
                            Bc250ActiveProbeMecHqdSelected(
                                context, &context->MecHqdSelectorEvidence);
                    }
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_SELECTOR_STATUS,
                        (ULONG)context->MecHqdSelectorStatus);
                    /*
                     * B.1 -- fotografia do anel grafico. Ponto escolhido de
                     * proposito: o probe acima acabou de fazer map/unmap, logo
                     * o transporte esta VAZIO aqui. A16 custou uma corrida
                     * justamente por deixar o transporte mapeado.
                     */
                    if (context->GfxRingPreflightArmed == 1u &&
                        context->GfxRingPreflightPresent == 1u) {
                        context->GfxRingPreflightStatus =
                            Bc250ActiveProbeGfxRingReadonly(
                                context, &context->GfxRingPreflightEvidence);
                        if (context->GfxRingPreflightStatus == BC250_GFX_RING_TXN_OK &&
                            context->GfxRingPreflightEvidence.SnapshotTaken == 1u) {
                            Bc250DisplayLog("[GfxRing] PREFLIGHT OK: Reads=%u Writes=%u (transporte Writes=%u)\n",
                                context->GfxRingPreflightEvidence.Reads,
                                context->GfxRingPreflightEvidence.Writes,
                                context->GfxRingPreflightTransportWrites);
                            Bc250DisplayLog("[GfxRing] CP_RB0 COMO ENCONTRADO: CNTL=%08X BASE=%08X BASE_HI=%08X RPTR=%08X WPTR=%08X POLL_CNTL=%08X ME_CNTL=%08X\n",
                                context->GfxRingPreflightEvidence.CntlBefore,
                                context->GfxRingPreflightEvidence.BaseBefore,
                                context->GfxRingPreflightEvidence.BaseHiBefore,
                                context->GfxRingPreflightEvidence.RptrBefore,
                                context->GfxRingPreflightEvidence.WptrBefore,
                                context->GfxRingPreflightEvidence.PollCntlBefore,
                                context->GfxRingPreflightEvidence.MeCntlBefore);
                            Bc250DisplayLog("[GfxRing] CNTL DECODIFICADO: RB_BUFSZ=%u RB_BLKSZ=%u RB_EXE=%u CE_HQD_NEQ=%u CACHE_POLICY=%u RB_NO_UPDATE=%u RB_RPTR_WR_ENA=%u BUF_SWAP=%u MIN_AVAILSZ=%u MIN_IB_AVAILSZ=%u RB_VOLATILE=%u\n",
                                context->GfxRingPreflightEvidence.FieldsBefore.RbBufsz,
                                context->GfxRingPreflightEvidence.FieldsBefore.RbBlksz,
                                context->GfxRingPreflightEvidence.FieldsBefore.RbExe,
                                context->GfxRingPreflightEvidence.FieldsBefore.CeHqdNeqRbHqd,
                                context->GfxRingPreflightEvidence.FieldsBefore.CachePolicy,
                                context->GfxRingPreflightEvidence.FieldsBefore.RbNoUpdate,
                                context->GfxRingPreflightEvidence.FieldsBefore.RbRptrWrEna,
                                context->GfxRingPreflightEvidence.FieldsBefore.BufSwap,
                                context->GfxRingPreflightEvidence.FieldsBefore.MinAvailsz,
                                context->GfxRingPreflightEvidence.FieldsBefore.MinIbAvailsz,
                                context->GfxRingPreflightEvidence.FieldsBefore.RbVolatile);
                            /*
                             * O catalogo registra CP_RB0_CNTL=0x50000709 lido
                             * uma vez. Dizer se a leitura de AGORA confere e'
                             * informacao nos dois desfechos, e a divergencia
                             * NAO e' erro: significa que o estado do anel
                             * depende do caminho de boot, o que precisamos
                             * saber antes de escrever qualquer coisa nele.
                             */
                            /*
                             * MEDIDO em 14/09 11:43: as tres micro-engines
                             * graficas vem HALTED do boot (CP_ME_CNTL=15000000).
                             * Um anel configurado com elas paradas aceita toda
                             * a escrita, confere no readback e NAO executa
                             * pacote nenhum -- o falso sucesso que a Lei 4
                             * proibe. Por isso a linha existe e e' explicita.
                             */
                            Bc250DisplayLog("[GfxRing] MICRO-ENGINES: CE_HALT=%u PFP_HALT=%u ME_HALT=%u -> %s\n",
                                context->GfxRingPreflightEvidence.MeCntlFields.CeHalt,
                                context->GfxRingPreflightEvidence.MeCntlFields.PfpHalt,
                                context->GfxRingPreflightEvidence.MeCntlFields.MeHalt,
                                (context->GfxRingPreflightEvidence.MicroEnginesHalted != 0u)
                                    ? "PARADAS -- anel configurado NAO executaria"
                                    : "liberadas");
                            Bc250DisplayLog("[GfxRing] CONTRA A REFERENCIA: lido=%08X boot_medido=%08X ref_configurado=%08X -> %s\n",
                                context->GfxRingPreflightEvidence.CntlBefore,
                                BC250_GFX_RING_CNTL_MEASURED_BOOT,
                                BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE,
                                (context->GfxRingPreflightEvidence.CntlBefore ==
                                    BC250_GFX_RING_CNTL_MEASURED_BOOT) ? "IGUAL AO BOOT MEDIDO"
                                : (context->GfxRingPreflightEvidence.CntlBefore ==
                                    BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE) ? "ANEL JA CONFIGURADO"
                                : "ESTADO NOVO -- nem boot nem referencia");
                        } else {
                            Bc250DisplayLog("[GfxRing] PREFLIGHT RECUSADO: status=%s (%u), SnapshotTaken=%u -- nenhuma medicao do anel grafico nesta corrida\n",
                                Bc250GfxRingTxnStatusName(context->GfxRingPreflightStatus),
                                (ULONG)context->GfxRingPreflightStatus,
                                context->GfxRingPreflightEvidence.SnapshotTaken);
                        }
                    } else {
                        Bc250DisplayLog("[GfxRing] PREFLIGHT DESARMADO (armed=%u present=%u) -- anel grafico NAO medido nesta corrida\n",
                            context->GfxRingPreflightArmed,
                            context->GfxRingPreflightPresent);
                    }
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_ORIGINAL_SELECTOR,
                        context->MecHqdSelectorEvidence.OriginalSelector);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_APPLIED_SELECTOR,
                        context->MecHqdSelectorEvidence.AppliedSelector);
                    Bc250ProbeRecord(
                        BC250_PROBE_MEC_HQD_APPLIED_READBACK_FIRST,
                        context->MecHqdSelectorEvidence
                            .AppliedSelectorReadbackFirst);
                    Bc250ProbeRecord(
                        BC250_PROBE_MEC_HQD_APPLIED_READBACK_LAST,
                        context->MecHqdSelectorEvidence
                            .AppliedSelectorReadbackLast);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_RESTORED_SELECTOR,
                        context->MecHqdSelectorEvidence.RestoredSelector);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_READ_CALLS,
                        context->MecHqdSelectorEvidence.ReadCalls);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_WRITE_CALLS,
                        context->MecHqdSelectorEvidence.WriteCalls);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_APPLY_READBACK,
                        context->MecHqdSelectorEvidence.ApplyReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_RESTORE,
                        context->MecHqdSelectorEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_PHYSICAL_READY,
                        context->MecHqdSelectorEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_STATUS,
                        (ULONG)context->MecHqdSelectorEvidence.HqdStatus);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_STABLE_PASSES,
                        context->MecHqdSelectorEvidence.HqdSnapshot.StablePasses);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_ACTIVE,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_ACTIVE]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_MQD_BASE_LO,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_MQD_BASE_LO]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_MQD_BASE_HI,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_MQD_BASE_HI]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_PQ_BASE_LO,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_BASE_LO]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_PQ_BASE_HI,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_BASE_HI]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_PQ_RPTR,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_RPTR]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_PQ_WPTR_LO,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_WPTR_LO]);
                    Bc250ProbeRecord(BC250_PROBE_MEC_HQD_PQ_WPTR_HI,
                        context->MecHqdSelectorEvidence.HqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_WPTR_HI]);
                    if (context->MecHqdSelectorStatus ==
                            BC250_HQD_SELECTOR_READBACK_FAILED &&
                        context->MecHqdSelectorEvidence.RestorePassed != 0u) {
                        context->MecHqdBankProbeStatus =
                            Bc250ActiveProbeMecHqdBankIsolation(
                                context, &context->MecHqdBankProbeEvidence);
                    }
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_PROBE_STATUS,
                        (ULONG)context->MecHqdBankProbeStatus);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_ORIGINAL_SELECTOR,
                        context->MecHqdBankProbeEvidence.OriginalSelector);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK0_ACTIVE,
                        context->MecHqdBankProbeEvidence.Bank0Active);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK0_PRIORITY,
                        context->MecHqdBankProbeEvidence.Bank0Priority);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_CANDIDATE_ACTIVE,
                        context->MecHqdBankProbeEvidence.CandidateActive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_CANDIDATE_PRIORITY,
                        context->MecHqdBankProbeEvidence.CandidatePriority);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_POISON_PRIORITY,
                        context->MecHqdBankProbeEvidence.PoisonPriority);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_POISON_READBACK,
                        context->MecHqdBankProbeEvidence.PoisonReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK0_CROSS_READBACK,
                        context->MecHqdBankProbeEvidence.Bank0CrossReadback);
                    Bc250ProbeRecord(
                        BC250_PROBE_HQD_BANK_CANDIDATE_CROSS_READBACK,
                        context->MecHqdBankProbeEvidence
                            .CandidateCrossReadback);
                    Bc250ProbeRecord(
                        BC250_PROBE_HQD_BANK_CANDIDATE_RESTORE_READBACK,
                        context->MecHqdBankProbeEvidence
                            .CandidateRestoreReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK0_FINAL_READBACK,
                        context->MecHqdBankProbeEvidence.Bank0FinalReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_FINAL_SELECTOR,
                        context->MecHqdBankProbeEvidence.FinalSelector);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_READ_CALLS,
                        context->MecHqdBankProbeEvidence.ReadCalls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_WRITE_CALLS,
                        context->MecHqdBankProbeEvidence.WriteCalls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_POISON_PASSED,
                        context->MecHqdBankProbeEvidence.PoisonReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_ISOLATION_PASSED,
                        context->MecHqdBankProbeEvidence.BankIsolationPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_RESTORE_PASSED,
                        context->MecHqdBankProbeEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_PHYSICAL_READY,
                        context->MecHqdBankProbeEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_CAPTURE_PASSED,
                        context->MecHqdBankProbeEvidence.HqdCapturePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_STABLE_PASSES,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.StablePasses);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_INACTIVE,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.HqdWasInactive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_SELECTOR_OBSERVED,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_SELECTOR_BEFORE]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_ACTIVE,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_ACTIVE]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_MQD_BASE_LO,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_MQD_BASE_LO]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_MQD_BASE_HI,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_MQD_BASE_HI]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_VMID,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_VMID]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_PQ_BASE_LO,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_BASE_LO]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_PQ_BASE_HI,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_BASE_HI]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_PQ_RPTR,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_RPTR]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_DOORBELL_CONTROL,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_DOORBELL_CONTROL]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_PQ_CONTROL,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_CONTROL]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_PQ_WPTR_LO,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_WPTR_LO]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_BANK_HQD_PQ_WPTR_HI,
                        context->MecHqdBankProbeEvidence.CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_PQ_WPTR_HI]);
                    if (context->MecHqdBankProbeStatus ==
                            BC250_HQD_BANK_PROBE_OK &&
                        context->MecHqdBankProbeEvidence.PhysicalReady != 0u) {
                        context->HqdProgramStatus =
                            Bc250ActiveRunInactiveHqdProgramTransaction(
                                context, &context->HqdProgramEvidence);
                    }
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_STATUS,
                        (ULONG)context->HqdProgramStatus);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_ORIGINAL_SELECTOR,
                        context->HqdProgramEvidence.OriginalSelector);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_SELECTED_SELECTOR,
                        context->HqdProgramEvidence.SelectedSelector);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_RESTORED_SELECTOR,
                        context->HqdProgramEvidence.RestoredSelector);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READ_CALLS,
                        context->HqdProgramEvidence.ReadCalls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_WRITE_CALLS,
                        context->HqdProgramEvidence.WriteCalls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MEMORY_PASSED,
                        context->HqdProgramEvidence.MemoryVerificationPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READBACK_PASSED,
                        context->HqdProgramEvidence.ApplyReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_ACTIVE_ZERO,
                        context->HqdProgramEvidence.ActiveRemainedZero);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_RESTORE_PASSED,
                        context->HqdProgramEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MEM_RESTORE_PASSED,
                        context->HqdProgramEvidence.MemoryRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_PHYSICAL_READY,
                        context->HqdProgramEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MQD_BASE_LO,
                        context->HqdProgramEvidence.AppliedValues[0]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MQD_BASE_HI,
                        context->HqdProgramEvidence.AppliedValues[1]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_PQ_BASE_LO,
                        context->HqdProgramEvidence.AppliedValues[11]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_PQ_BASE_HI,
                        context->HqdProgramEvidence.AppliedValues[12]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_PQ_CONTROL,
                        context->HqdProgramEvidence.AppliedValues[14]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_ACTIVE_READBACK,
                        context->HqdProgramEvidence.ActiveReadbackPass1);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MISMATCH_MASK,
                        context->HqdProgramEvidence.MismatchMask);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MISMATCH_OFFSET,
                        context->HqdProgramEvidence.FirstMismatchOffset);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MISMATCH_WRITTEN,
                        context->HqdProgramEvidence.FirstMismatchWritten);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MISMATCH_READ1,
                        context->HqdProgramEvidence.FirstMismatchRead1);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_MISMATCH_READ2,
                        context->HqdProgramEvidence.FirstMismatchRead2);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READBACK_MQD_BASE_LO,
                        context->HqdProgramEvidence.ReadbackPass1[0]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READBACK_MQD_BASE_HI,
                        context->HqdProgramEvidence.ReadbackPass1[1]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READBACK_PQ_BASE_LO,
                        context->HqdProgramEvidence.ReadbackPass1[11]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READBACK_PQ_BASE_HI,
                        context->HqdProgramEvidence.ReadbackPass1[12]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_PROGRAM_READBACK_PQ_CONTROL,
                        context->HqdProgramEvidence.ReadbackPass1[14]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_STATUS,
                        (ULONG)context->HqdActiveStatus);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SELECTED_SELECTOR,
                        context->HqdActiveEvidence.SelectedSelector);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BEFORE,
                        context->HqdActiveEvidence.ActiveBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_APPLIED,
                        context->HqdActiveEvidence.ActiveApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_AFTER,
                        context->HqdActiveEvidence.ActiveAfter);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_BEFORE,
                        context->HqdActiveEvidence.RptrBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_AFTER,
                        context->HqdActiveEvidence.RptrAfter);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_APPLIED,
                        context->HqdActiveEvidence.WptrApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FENCE_BEFORE,
                        context->HqdActiveEvidence.FenceBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FENCE_AFTER,
                        context->HqdActiveEvidence.FenceAfter);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SCRATCH_BEFORE,
                        context->HqdActiveEvidence.ScratchBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SCRATCH_AFTER,
                        context->HqdActiveEvidence.ScratchAfter);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_POLL_ITERATIONS,
                        context->HqdActiveEvidence.PollIterations);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_TRIAD_PASSED,
                        context->HqdActiveEvidence.TriadPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RESTORE_PASSED,
                        context->HqdActiveEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEM_RESTORE_PASSED,
                        context->HqdActiveEvidence.MemoryRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PHYSICAL_READY,
                        context->HqdActiveEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_RUNG,
                        context->HqdActiveEvidence.DoorbellRung);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_INDEX,
                        context->HqdActiveEvidence.DoorbellIndex);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_OFFSET,
                        context->HqdActiveEvidence.DoorbellOffset);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_VALUE_APPLIED,
                        context->HqdActiveEvidence.DoorbellValueApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_BEFORE,
                        context->HqdActiveEvidence.DoorbellRangeLowerBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_APPLIED,
                        context->HqdActiveEvidence.DoorbellRangeLowerApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_READBACK,
                        context->HqdActiveEvidence.DoorbellRangeLowerReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_BEFORE,
                        context->HqdActiveEvidence.DoorbellRangeUpperBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_APPLIED,
                        context->HqdActiveEvidence.DoorbellRangeUpperApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_READBACK,
                        context->HqdActiveEvidence.DoorbellRangeUpperReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_BEFORE,
                        context->HqdActiveEvidence.CpPqStatusBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_APPLIED,
                        context->HqdActiveEvidence.CpPqStatusApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_READBACK,
                        context->HqdActiveEvidence.CpPqStatusReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_BEFORE,
                        context->HqdActiveEvidence.WptrPollCntlBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_APPLIED,
                        context->HqdActiveEvidence.WptrPollCntlApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_READBACK,
                        context->HqdActiveEvidence.WptrPollCntlReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_SELECTOR_RELEASED_BEFORE_KICK,
                        context->HqdActiveEvidence.SelectorReleasedBeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.CpPqStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DOORBELL_CONTROL_AFTER_KICK,
                        context->HqdActiveEvidence.DoorbellControlAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_AFTER_KICK,
                        context->HqdActiveEvidence.WptrAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_AFTER_KICK,
                        context->HqdActiveEvidence.RptrAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_INITIAL_WPTR_LO,
                        context->HqdActiveEvidence.InitialWptrLo);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_INITIAL_WPTR_HI,
                        context->HqdActiveEvidence.InitialWptrHi);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_APPLIED,
                        context->HqdActiveEvidence.BaselineWptrApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_READBACK,
                        context->HqdActiveEvidence.BaselineWptrReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORED,
                        context->HqdActiveEvidence.BaselineWptrRestored);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORE_POLLS,
                        context->HqdActiveEvidence.BaselineWptrRestorePolls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORE_PASSED,
                        context->HqdActiveEvidence.BaselineWptrRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_KICK_APPLIED,
                        context->HqdActiveEvidence.DirectWptrKickApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_BEFORE,
                        context->HqdActiveEvidence.DirectWptrBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_READBACK,
                        context->HqdActiveEvidence.DirectWptrReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_RPTR_AFTER_KICK,
                        context->HqdActiveEvidence.DirectRptrAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORED,
                        context->HqdActiveEvidence.DirectWptrRestored);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORE_POLLS,
                        context->HqdActiveEvidence.DirectWptrRestorePolls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORE_PASSED,
                        context->HqdActiveEvidence.DirectWptrRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_MMIO_LAST,
                        context->HqdActiveEvidence.RptrMmioLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_MMIO_LAST,
                        context->HqdActiveEvidence.WptrMmioLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_POLL_LAST,
                        context->HqdActiveEvidence.HqStatus0PollLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQD_ERROR_POLL_LAST,
                        context->HqdActiveEvidence.HqdErrorPollLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_BEFORE,
                        context->HqdActiveEvidence.RlcCpSchedulersBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_APPLIED,
                        context->HqdActiveEvidence.RlcCpSchedulersApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_READBACK,
                        context->HqdActiveEvidence.RlcCpSchedulersReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_AFTER_KICK,
                        context->HqdActiveEvidence.RlcCpSchedulersAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_RESTORED,
                        context->HqdActiveEvidence.RlcCpSchedulersRestored);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_RESTORE_PASSED,
                        context->HqdActiveEvidence.RlcCpSchedulersRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_AFTER_KICK,
                        context->HqdActiveEvidence.HqStatus0AfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_CONTROL0_AFTER_KICK,
                        context->HqdActiveEvidence.HqControl0AfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQD_ERROR_AFTER_KICK,
                        context->HqdActiveEvidence.HqdErrorAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_UTCL1_ERROR_AFTER_KICK,
                        context->HqdActiveEvidence.CpcUtcl1ErrorAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_UTCL1_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.CpcUtcl1StatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_UTCL1_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.CpfUtcl1StatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_INT_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.CpcIntStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_ME1_PIPE0_INT_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.Me1Pipe0IntStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_ME2_PIPE1_INT_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.Me2Pipe1IntStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_ME2_PIPE0_INT_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.Me2Pipe0IntStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_BASE_LO,
                        context->HqdActiveEvidence.PqBaseLoApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_BASE_HI,
                        context->HqdActiveEvidence.PqBaseHiApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_ADDR_LO,
                        context->HqdActiveEvidence.WptrPollAddrLoApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_ADDR_HI,
                        context->HqdActiveEvidence.WptrPollAddrHiApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_SHADOW,
                        context->HqdActiveEvidence.WptrShadowApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_MEC_CNTL_READBACK,
                        context->HqdActiveEvidence.CpMecCntlReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_FIRST,
                        context->HqdActiveEvidence.MecPcFirst);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_LAST,
                        context->HqdActiveEvidence.MecPcLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_TRANSITIONS,
                        context->HqdActiveEvidence.MecPcTransitions);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_PC_SAMPLES,
                        context->HqdActiveEvidence.MecPcSamples);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_CONTROL_OBSERVED,
                        context->HqdActiveEvidence.PqControlObserved);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_BEFORE,
                        context->HqdActiveEvidence.EopRptrBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_APPLIED,
                        context->HqdActiveEvidence.EopRptrApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_READBACK,
                        context->HqdActiveEvidence.EopRptrReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_RESTORED,
                        context->HqdActiveEvidence.EopRptrRestored);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_EOP_RPTR_RESTORE_PASSED,
                        context->HqdActiveEvidence.EopRptrRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_BEFORE,
                        context->HqdActiveEvidence.WptrPollCntl1Before);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_APPLIED,
                        context->HqdActiveEvidence.WptrPollCntl1Applied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_READBACK,
                        context->HqdActiveEvidence.WptrPollCntl1Readback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_RESTORED,
                        context->HqdActiveEvidence.WptrPollCntl1Restored);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_RESTORE_PASSED,
                        context->HqdActiveEvidence.WptrPollCntl1RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_QUEUE_MASK_BIT,
                        context->HqdActiveEvidence.QueueMaskBit);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_APPLIED,
                        context->HqdActiveEvidence.PreActiveWptrApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_READBACK,
                        context->HqdActiveEvidence.PreActiveWptrReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_PUBLISHED,
                        context->HqdActiveEvidence.PreActiveWptrPublished);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_STAGE,
                        context->HqdActiveEvidence.FirstRestoreFailureStage);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_OFFSET,
                        context->HqdActiveEvidence.FirstRestoreFailureOffset);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_EXPECTED,
                        context->HqdActiveEvidence.FirstRestoreFailureExpected);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_OBSERVED,
                        context->HqdActiveEvidence.FirstRestoreFailureObserved);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_TRANSPORT,
                        context->HqdActiveEvidence.FirstRestoreFailureTransport);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RESTORE_FAILURE_COUNT,
                        context->HqdActiveEvidence.RestoreFailureCount);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_PROBE_APPLIED,
                        context->HqdActiveEvidence.WptrPollCntlProbeApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_PROBE_READBACK,
                        context->HqdActiveEvidence.WptrPollCntlProbeReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_WRITABLE,
                        context->HqdActiveEvidence.WptrPollCntlWritable);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_EN_LATCHED,
                        context->HqdActiveEvidence.WptrPollCntlEnLatched);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_POST_ACTIVE_WPTR_OBSERVED,
                        context->HqdActiveEvidence.PostActiveWptrObserved);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_DIRTY_ENTRY,
                        context->HqdActiveEvidence.WptrPollCntl1DirtyEntry);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_CLEAR_ATTEMPTS,
                        context->HqdActiveEvidence.WptrPollCntl1ClearAttempts);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PQ_CONTROL_FIRST_SAMPLE,
                        context->HqdActiveEvidence.PqControlFirstSample);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_FIRST_SAMPLE,
                        context->HqdActiveEvidence.HqStatus0FirstSample);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_POLL_LAST,
                        context->HqdActiveEvidence.DequeueRequestPollLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_VMID_POLL_LAST,
                        context->HqdActiveEvidence.VmidPollLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_PERSISTENT_STATE_POLL_LAST,
                        context->HqdActiveEvidence.PersistentStatePollLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_REPORT_MEM_FIRST,
                        context->HqdActiveEvidence.RptrReportMemFirst);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_REPORT_MEM_LAST,
                        context->HqdActiveEvidence.RptrReportMemLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_RPTR_NON_ZERO_SAMPLES,
                        context->HqdActiveEvidence.RptrNonZeroSamples);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_FIRST,
                        context->HqdActiveEvidence.CpcStatusFirst);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_LAST,
                        context->HqdActiveEvidence.CpcStatusLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_BUSY_STAT_LAST,
                        context->HqdActiveEvidence.CpcBusyStatLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_STAT_LAST,
                        context->HqdActiveEvidence.CpfBusyStatLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STALLED_STAT1_LAST,
                        context->HqdActiveEvidence.CpcStalledStat1Last);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_FIRST,
                        context->HqdActiveEvidence.CpStatFirst);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_LAST,
                        context->HqdActiveEvidence.CpStatLast);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STALLED_STAT1_AFTER,
                        context->HqdActiveEvidence.CpStalledStat1After);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP0,
                        context->HqdActiveEvidence.MecMe1HeaderDump[0]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP1,
                        context->HqdActiveEvidence.MecMe1HeaderDump[1]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP2,
                        context->HqdActiveEvidence.MecMe1HeaderDump[2]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP3,
                        context->HqdActiveEvidence.MecMe1HeaderDump[3]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP0,
                        context->HqdActiveEvidence.MecMe2HeaderDump[0]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP1,
                        context->HqdActiveEvidence.MecMe2HeaderDump[1]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP2,
                        context->HqdActiveEvidence.MecMe2HeaderDump[2]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP3,
                        context->HqdActiveEvidence.MecMe2HeaderDump[3]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE0,
                        context->HqdActiveEvidence.MecMe2HeaderDumpBeforeActive[0]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE1,
                        context->HqdActiveEvidence.MecMe2HeaderDumpBeforeActive[1]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE2,
                        context->HqdActiveEvidence.MecMe2HeaderDumpBeforeActive[2]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE3,
                        context->HqdActiveEvidence.MecMe2HeaderDumpBeforeActive[3]);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_BEFORE_KICK,
                        context->HqdActiveEvidence.CpcStatusBeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_BUSY_BEFORE_KICK,
                        context->HqdActiveEvidence.CpcBusyStatBeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_KICK,
                        context->HqdActiveEvidence.CpfBusyStatBeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STALLED_BEFORE_KICK,
                        context->HqdActiveEvidence.CpcStalledStat1BeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_BEFORE_KICK,
                        context->HqdActiveEvidence.CpStatBeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STALLED_BEFORE_KICK,
                        context->HqdActiveEvidence.CpStalledStat1BeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.CpcStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_BUSY_AFTER_KICK,
                        context->HqdActiveEvidence.CpcBusyStatAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_KICK,
                        context->HqdActiveEvidence.CpfBusyStatAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_ACTIVE,
                        context->HqdActiveEvidence.CpfBusyStatBeforeActive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_ACTIVE,
                        context->HqdActiveEvidence.CpfBusyStatAfterActive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_WPTR,
                        context->HqdActiveEvidence.CpfBusyStatBeforeWptr);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_WPTR,
                        context->HqdActiveEvidence.CpfBusyStatAfterWptr);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_BEFORE_ACTIVE,
                        context->HqdActiveEvidence.CpfStalledStat1BeforeActive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_AFTER_ACTIVE,
                        context->HqdActiveEvidence.CpfStalledStat1AfterActive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_BEFORE_KICK,
                        context->HqdActiveEvidence.CpfStalledStat1BeforeKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_AFTER_KICK,
                        context->HqdActiveEvidence.CpfStalledStat1AfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STALLED_LAST,
                        context->HqdActiveEvidence.CpfStalledStat1Last);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STATUS_AFTER_ACTIVE,
                        context->HqdActiveEvidence.CpfStatusAfterActive);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPF_STATUS_AFTER_KICK,
                        context->HqdActiveEvidence.CpfStatusAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_APPLIED,
                        context->HqdActiveEvidence.DequeueRequestApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_READBACK,
                        context->HqdActiveEvidence.DequeueRequestReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_RESTORE_PASSED,
                        context->HqdActiveEvidence.DequeueRequestRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CPC_STALLED_AFTER_KICK,
                        context->HqdActiveEvidence.CpcStalledStat1AfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STAT_AFTER_KICK,
                        context->HqdActiveEvidence.CpStatAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_CP_STALLED_AFTER_KICK,
                        context->HqdActiveEvidence.CpStalledStat1AfterKick);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_HDP_INVALIDATE_APPLIED,
                        context->HqdActiveEvidence.HdpInvalidateApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_L2_BEFORE,
                        context->HqdActiveEvidence.GcvmL2Cntl2Before);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_L2_APPLIED,
                        context->HqdActiveEvidence.GcvmL2Cntl2Applied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_L2_READBACK,
                        context->HqdActiveEvidence.GcvmL2Cntl2Readback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_REQ_APPLIED,
                        context->HqdActiveEvidence.GcvmInvalidateRequestApplied);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_REQ_READBACK,
                        context->HqdActiveEvidence.GcvmInvalidateRequestReadback);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_ACK_BEFORE,
                        context->HqdActiveEvidence.GcvmInvalidateAckBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_ACK_AFTER,
                        context->HqdActiveEvidence.GcvmInvalidateAckAfter);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GCVM_POLLS,
                        context->HqdActiveEvidence.GcvmInvalidatePolls);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_COHERENCY_PASSED,
                        context->HqdActiveEvidence.CoherencyPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_APPLICABLE,
                        context->HqdActiveEvidence.GfxOffApplicable);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_BEFORE,
                        context->HqdActiveEvidence.GfxOffStateBefore);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_GUARDED,
                        context->HqdActiveEvidence.GfxOffStateGuarded);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_AFTER,
                        context->HqdActiveEvidence.GfxOffStateAfter);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_DISALLOW_RESPONSE,
                        context->HqdActiveEvidence.GfxOffDisallowResponse);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_ALLOW_RESPONSE,
                        context->HqdActiveEvidence.GfxOffAllowResponse);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_GUARD_PASSED,
                        context->HqdActiveEvidence.GfxOffGuardPassed);
                    Bc250ProbeRecord(BC250_PROBE_HQD_ACTIVE_GFXOFF_RESTORE_PASSED,
                        context->HqdActiveEvidence.GfxOffRestorePassed);
                    context->VramProbeStatus = Bc250RunVramProbeTransaction(
                        &context->Core.DxgkInterface,
                        context->Core.ResolvedBars,
                        context->Core.ResolvedBarCount,
                        BC250_PCI_VRAM_APERTURE_BAR_INDEX,
                        context->Core.StartDevice.Preflight.FirmwarePlans[0]
                            .Binding.Offset,
                        &context->VramProbeEvidence);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_STATUS,
                        (ULONG)context->VramProbeStatus);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_OFFSET_LOW32,
                        (ULONG)context->VramProbeEvidence.Offset);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_BYTES,
                        context->VramProbeEvidence.Bytes);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_READBACK_PASSED,
                        context->VramProbeEvidence.WriteReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_VRAM_PROBE_RESTORE_PASSED,
                        context->VramProbeEvidence.RestorePassed);
                    if (context->VramProbeStatus == BC250_VRAM_PROBE_OK &&
                        context->VramProbeEvidence.WriteReadbackPassed != 0u &&
                        context->VramProbeEvidence.RestorePassed != 0u) {
                        context->GcvmBackingStatus =
                            Bc250RunGcvmBackingTransaction(
                                &context->Core.StartDevice.Owned.Bootstrap
                                    .Allocator,
                                context->Core.StartDevice.Owned.Bootstrap
                                    .AddressSpace.VramBaseOffset,
                                &context->Core.DxgkInterface,
                                context->Core.ResolvedBars,
                                context->Core.ResolvedBarCount,
                                BC250_PCI_VRAM_APERTURE_BAR_INDEX,
                                Bc250ActiveExecuteGcvmBacking,
                                context,
                                &context->GcvmBackingEvidence);
                    }
                    Bc250ProbeRecord(BC250_PROBE_TLB_TRANSACTION_STATUS,
                        (ULONG)context->TlbTransactionStatus);
                    Bc250ProbeRecord(BC250_PROBE_TLB_VMID,
                        context->TlbTransactionEvidence.Vmid);
                    Bc250ProbeRecord(BC250_PROBE_TLB_ENGINE_ID,
                        context->TlbTransactionEvidence.EngineId);
                    Bc250ProbeRecord(BC250_PROBE_TLB_REQUEST_VALUE,
                        context->TlbTransactionEvidence.RequestValue);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_LOW_BEFORE,
                        context->TlbTransactionEvidence.RangeLowBefore);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_HIGH_BEFORE,
                        context->TlbTransactionEvidence.RangeHighBefore);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_LOW_PROGRAMMED,
                        context->TlbTransactionEvidence.RangeLowProgrammed);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_HIGH_PROGRAMMED,
                        context->TlbTransactionEvidence.RangeHighProgrammed);
                    Bc250ProbeRecord(BC250_PROBE_TLB_REQUEST_DUMMY_READ,
                        context->TlbTransactionEvidence.RequestDummyRead);
                    Bc250ProbeRecord(BC250_PROBE_TLB_ACK_BEFORE,
                        context->TlbTransactionEvidence.AckBefore);
                    Bc250ProbeRecord(BC250_PROBE_TLB_ACK_AFTER,
                        context->TlbTransactionEvidence.AckAfter);
                    Bc250ProbeRecord(BC250_PROBE_TLB_POLL_COUNT,
                        context->TlbTransactionEvidence.PollCount);
                    Bc250ProbeRecord(BC250_PROBE_TLB_REQUEST_WRITTEN,
                        context->TlbTransactionEvidence.RequestWritten);
                    Bc250ProbeRecord(BC250_PROBE_TLB_DUMMY_READ_COMPLETED,
                        context->TlbTransactionEvidence.DummyReadCompleted);
                    Bc250ProbeRecord(BC250_PROBE_TLB_ACK_OBSERVED,
                        context->TlbTransactionEvidence.AckObserved);
                    Bc250ProbeRecord(BC250_PROBE_TLB_RANGE_RESTORE_PASSED,
                        context->TlbTransactionEvidence.RangeRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_BACKING_STATUS,
                        (ULONG)context->GcvmBackingStatus);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_OFFSET_LOW32,
                        (ULONG)context->GcvmBackingEvidence.PageTableOffset);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_PHYSICAL_LOW32,
                        (ULONG)context->GcvmBackingEvidence.PageTablePhysical);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_PT_PHYSICAL_HIGH32,
                        (ULONG)(context->GcvmBackingEvidence.PageTablePhysical >> 32));
                    Bc250ProbeRecord(BC250_PROBE_GCVM_TARGET_OFFSET_LOW32,
                        (ULONG)context->GcvmBackingEvidence.TargetOffset);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_FIRST_PTE_LOW32,
                        (ULONG)context->GcvmBackingEvidence.FirstPte);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_FIRST_PTE_HIGH32,
                        (ULONG)(context->GcvmBackingEvidence.FirstPte >> 32));
                    Bc250ProbeRecord(BC250_PROBE_GCVM_BACKING_OWNED,
                        context->GcvmBackingEvidence.OwnershipPassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_BACKING_READBACK,
                        context->GcvmBackingEvidence.WriteReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_BACKING_RESTORE,
                        context->GcvmBackingEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_STATUS,
                        (ULONG)context->GcvmVmid8Status);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_CNTL_BEFORE,
                        context->GcvmVmid8Evidence.ContextControlBefore);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_READBACK,
                        context->GcvmVmid8Evidence.ProgramReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_TLB_PASSED,
                        context->GcvmVmid8Evidence.TlbFlushPassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_VMID8_RESTORE,
                        context->GcvmVmid8Evidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_GCVM_INERT_PHYSICAL_READY,
                        context->GcvmVmid8Evidence.PhysicalReady != 0u &&
                        context->GcvmBackingEvidence.ExecutePassed != 0u &&
                        context->GcvmBackingEvidence.RestorePassed != 0u);
                    if (context->GcvmBackingStatus == BC250_GCVM_BACKING_OK &&
                        context->GcvmVmid8Evidence.PhysicalReady != 0u) {
                        context->PspRingStatus = Bc250ActiveProbePspRing(
                            context, &context->PspRingEvidence);
                    }
                    if (context->PspRingStatus == BC250_PSP_RING_OK &&
                        context->PspRingEvidence.PhysicalReady != 0u) {
                        /*
                         * NAO recarregar o firmware da SDMA0 nem rearmar o anel
                         * aqui.  MEDIDO na corrida 06/09 11:53 (evidencia em
                         * docs\evidence\2026-09-06_11-53-58-*-lab-binding):
                         *
                         *   1. Bc250ActiveLoadSdmaFirmware ja rodou no caminho
                         *      de display, algumas linhas acima, e devolveu
                         *      PspLoadFwStatus=OK com RingWorkStatus=2
                         *      (ALLOCATION_FAILED, RingMc=0x0);
                         *   2. Bc250SdmaPresentInit entao armou o anel nos
                         *      enderecos proprios (0xF40FF00000) e PROVOU o
                         *      tripe no silicio: WPTR=68, RPTR=52,
                         *      Fence=0x00001001 sobre o poison, dados 4/4 OK;
                         *   3. uma segunda chamada aqui achava o anel ja armado
                         *      e devolvia RingWorkStatus=6 (INHERITED_ACTIVE) --
                         *      recusa correta, colisao nossa.
                         *
                         * Com isso a cadeia parava antes do PFP direct load, o
                         * motor P1 nunca subia e StartDevice devolvia
                         * STATUS_NOT_SUPPORTED, deixando o dispositivo em
                         * Codigo 43.  Na imagem de producao o bloco e compilado
                         * fora e a colisao nunca aparecia.
                         *
                         * A porta passa a ser a prova que de fato existe. O
                         * tripe de boot e evidencia MAIS FORTE do que a
                         * transacao de ring work: alem de programar o anel, ele
                         * exige RPTR avancado, fence gravada pela GPU sobre o
                         * poison e conferencia byte a byte dos 4 dwords.
                         */
                        if (context->PspLoadFwStatus ==
                                BC250_PSP_LOAD_FW_OK &&
                            context->SdmaBootTripodValidated != 0u) {
                            context->PfpDirectLoadStatus =
                                Bc250ActiveRunPfpDirectLoad(
                                    context,
                                    &context->PfpDirectLoadEvidence);
                            if (context->PfpDirectLoadStatus ==
                                    BC250_PFP_DIRECT_LOAD_OK &&
                                context->PfpDirectLoadEvidence.PhysicalReady != 0u) {
                                context->HqdActiveStatus =
                                    Bc250ActiveRunActiveHqdSubmissionTransaction(
                                        context, &context->HqdActiveEvidence);
                                Bc250ActiveRecordHqdSubmission(context);
                                Bc250ActiveRecordShaderDispatch(context);

                                /*
                                 * P1/P2 fazem parte do StartDevice somente por
                                 * opt-in exato. O gate one-shot precisa provar
                                 * o caminho antes que a HQD persistente fique
                                 * viva para as DDIs do Dxgkrnl.
                                 */
                                if (context->PersistentSubmission == 1u &&
                                    context->TunablePersistentSubmissionPresent == 1u &&
                                    context->HqdActiveStatus == BC250_HQD_ACTIVE_OK &&
                                    context->HqdActiveEvidence.PhysicalReady != 0u) {
                                    persistentAttempted = TRUE;
                                    persistentStatus =
                                        Bc250SubmissionEngineOnline(context);
                                    if (NT_SUCCESS(persistentStatus)) {
                                        selfTestStatus =
                                            Bc250SubmissionEngineSelfTest(context);
                                    }
                                    if (NT_SUCCESS(persistentStatus) &&
                                        NT_SUCCESS(selfTestStatus)) {
                                        p2Status =
                                            Bc250SubmissionEngineP2StreamTest(context);
                                    }
                                    if (NT_SUCCESS(persistentStatus) &&
                                        NT_SUCCESS(selfTestStatus) &&
                                        NT_SUCCESS(p2Status) &&
                                        context->SubmissionEngine.SelfTest.PhysicalReady != 0u &&
                                        context->SubmissionEngine.P2Stream.PhysicalReady != 0u &&
                                        InterlockedCompareExchange(
                                            &context->SubmissionEngine.State,
                                            BC250_SUBMISSION_ENGINE_ONLINE,
                                            BC250_SUBMISSION_ENGINE_ONLINE) ==
                                                BC250_SUBMISSION_ENGINE_ONLINE) {
                                        persistentReady = TRUE;
                                    } else if (NT_SUCCESS(persistentStatus)) {
                                        (void)Bc250SubmissionEngineOffline(context);
                                    }
                                } else if (context->PersistentSubmission == 1u) {
                                    persistentAttempted = TRUE;
                                    persistentStatus = STATUS_DEVICE_NOT_READY;
                                } else if (context->PersistentSubmission != 0u) {
                                    persistentAttempted = TRUE;
                                    persistentStatus =
                                        STATUS_DEVICE_CONFIGURATION_ERROR;
                                }
                                Bc250SubmissionEngineRecordTelemetry(context);
                            } else {
                                Bc250DisplayLog("[StartDevice] PfpDirectLoad falhou: status=%u, PhysicalReady=%u\n",
                                    context->PfpDirectLoadStatus,
                                    context->PfpDirectLoadEvidence.PhysicalReady);
                            }
                        }
                    }
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_STATUS,
                        (ULONG)context->PspRingStatus);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_PHYSICAL_LOW32,
                        (ULONG)context->PspRingEvidence.RingPhysical);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_C2PMSG64_BEFORE,
                        context->PspRingEvidence.C2pmsg64Before);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_CREATE_ACK,
                        context->PspRingEvidence.CreateAck);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_DESTROY_ACK,
                        context->PspRingEvidence.DestroyAck);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_CREATE_POLLS,
                        context->PspRingEvidence.CreatePolls);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_DESTROY_POLLS,
                        context->PspRingEvidence.DestroyPolls);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_CREATE_PASSED,
                        context->PspRingEvidence.CreatePassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_DESTROY_PASSED,
                        context->PspRingEvidence.DestroyPassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_RESTORE_PASSED,
                        context->PspRingEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RING_PHYSICAL_READY,
                        context->PspRingEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_STATUS,
                        (ULONG)context->PspLoadFwStatus);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_BYTES,
                        context->PspLoadFwEvidence.FirmwareBytes);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_TYPE,
                        context->PspLoadFwEvidence.FirmwareType);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_RING_LOW,
                        (ULONG)context->PspLoadFwEvidence.RingPhysical);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_RING_HIGH,
                        (ULONG)(context->PspLoadFwEvidence.RingPhysical >> 32));
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_COMMAND_LOW,
                        (ULONG)context->PspLoadFwEvidence.CommandPhysical);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_COMMAND_HIGH,
                        (ULONG)(context->PspLoadFwEvidence.CommandPhysical >> 32));
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_FENCE_LOW,
                        (ULONG)context->PspLoadFwEvidence.FencePhysical);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_FENCE_HIGH,
                        (ULONG)(context->PspLoadFwEvidence.FencePhysical >> 32));
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_PAYLOAD_LOW,
                        (ULONG)context->PspLoadFwEvidence.FirmwarePhysical);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_PAYLOAD_HIGH,
                        (ULONG)(context->PspLoadFwEvidence.FirmwarePhysical >> 32));
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_CREATE_ACK,
                        context->PspLoadFwEvidence.CreateAck);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_FENCE_EXPECTED,
                        context->PspLoadFwEvidence.FenceExpected);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_FENCE_OBSERVED,
                        context->PspLoadFwEvidence.FenceObserved);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_FENCE_POLLS,
                        context->PspLoadFwEvidence.FencePolls);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_RESPONSE_STATUS,
                        context->PspLoadFwEvidence.ResponseStatus);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_CHECKSUM_BEFORE,
                        context->PspLoadFwEvidence.ChecksumBefore);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_CHECKSUM_AFTER,
                        context->PspLoadFwEvidence.ChecksumAfter);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_DESTROY_ACK,
                        context->PspLoadFwEvidence.DestroyAck);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_FENCE_PASSED,
                        context->PspLoadFwEvidence.FencePassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_RESPONSE_PASSED,
                        context->PspLoadFwEvidence.ResponsePassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_CHECKSUM_PASSED,
                        context->PspLoadFwEvidence.ChecksumPassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_DESTROY_PASSED,
                        context->PspLoadFwEvidence.DestroyPassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_RESTORE_PASSED,
                        context->PspLoadFwEvidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_PSP_LOAD_FW_PHYSICAL_READY,
                        context->PspLoadFwEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RLC_LOAD_STATUS,
                        (ULONG)context->PspRlcLoadStatus);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RLC_LOAD_BYTES,
                        context->PspRlcLoadEvidence.FirmwareBytes);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RLC_LOAD_TYPE,
                        context->PspRlcLoadEvidence.FirmwareType);
                    Bc250ProbeRecord(
                        BC250_PROBE_PSP_RLC_LOAD_FENCE_EXPECTED,
                        context->PspRlcLoadEvidence.FenceExpected);
                    Bc250ProbeRecord(
                        BC250_PROBE_PSP_RLC_LOAD_FENCE_OBSERVED,
                        context->PspRlcLoadEvidence.FenceObserved);
                    Bc250ProbeRecord(BC250_PROBE_PSP_RLC_LOAD_FENCE_POLLS,
                        context->PspRlcLoadEvidence.FencePolls);
                    Bc250ProbeRecord(
                        BC250_PROBE_PSP_RLC_LOAD_RESPONSE_STATUS,
                        context->PspRlcLoadEvidence.ResponseStatus);
                    Bc250ProbeRecord(
                        BC250_PROBE_PSP_RLC_LOAD_DESTROY_PASSED,
                        context->PspRlcLoadEvidence.DestroyPassed);
                    Bc250ProbeRecord(
                        BC250_PROBE_PSP_RLC_LOAD_RESTORE_PASSED,
                        context->PspRlcLoadEvidence.RestorePassed);
                    Bc250ProbeRecord(
                        BC250_PROBE_PSP_RLC_LOAD_PHYSICAL_READY,
                        context->PspRlcLoadEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_RLC_CNTL_BEFORE,
                        context->RlcLoadEvidence.CntlBefore);
                    Bc250ProbeRecord(BC250_PROBE_RLC_GPM_BEFORE,
                        context->RlcLoadEvidence.GpmStatBefore);
                    Bc250ProbeRecord(BC250_PROBE_RLC_CNTL_AFTER,
                        context->RlcLoadEvidence.CntlAfter);
                    Bc250ProbeRecord(BC250_PROBE_RLC_GPM_AFTER,
                        context->RlcLoadEvidence.GpmStatAfter);
                    Bc250ProbeRecord(BC250_PROBE_RLC_EXEC_ROM_BEFORE,
                        context->RlcLoadEvidence.ExecRomBefore);
                    Bc250ProbeRecord(BC250_PROBE_RLC_EXEC_ROM_AFTER,
                        context->RlcLoadEvidence.ExecRomAfter);
                    Bc250ProbeRecord(BC250_PROBE_RLC_ENABLE_F32_AFTER,
                        context->RlcLoadEvidence.EnableF32After);
                    Bc250ProbeRecord(BC250_PROBE_RLC_CNTL_RESTORE,
                        context->RlcLoadEvidence.CntlRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_RLC_ALREADY_ACTIVE,
                        context->RlcLoadEvidence.AlreadyActive);
                    Bc250ProbeRecord(BC250_PROBE_RLC_PHYSICAL_READY,
                        context->RlcLoadEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_STATUS,
                        (ULONG)context->PfpDirectLoadStatus);

                    /*
                     * B.1 -- ENSAIO DO ANEL GRAFICO. Ponto escolhido: DEPOIS
                     * do PFP direct load, porque o level 2 desalta as
                     * micro-engines e desaltar sem ucode e' solta-las sobre
                     * memoria de instrucao nao inicializada.
                     *
                     * O nivel 2 exige que a carga de firmware tenha dado
                     * certo. Sem isso o ensaio cai para o nivel 1, que nao
                     * liga nada -- degradar e' melhor que desaltar as cegas.
                     */
                    if (context->GfxRingBringupLevel > 0u &&
                        context->GfxRingBringupPresent == 1u) {
                        uint32_t level = context->GfxRingBringupLevel;
                        if (level >= 2u &&
                            context->PfpDirectLoadEvidence.PhysicalReady == 0u) {
                            Bc250DisplayLog("[GfxRing] BRINGUP rebaixado de 2 para 1: PfpDirectLoad PhysicalReady=0, firmware NAO confirmado\n");
                            level = 1u;
                        }
                        context->GfxRingBringupStatus =
                            Bc250ActiveRunGfxRingBringup(
                                context, level, &context->GfxRingBringupEvidence);
                        {
                            const BC250_GFX_RING_TXN_EVIDENCE *ev =
                                &context->GfxRingBringupEvidence;
                            Bc250DisplayLog("[GfxRing] BRINGUP veredito: %s (level=%u) Reads=%u Writes=%u\n",
                                Bc250GfxRingTxnStatusName(context->GfxRingBringupStatus),
                                level, ev->Reads, ev->Writes);
                            Bc250DisplayLog("[GfxRing] BRINGUP antes: CNTL=%08X BASE=%08X ME_CNTL=%08X | planejado: CNTL=%08X\n",
                                ev->CntlBefore, ev->BaseBefore, ev->MeCntlBefore,
                                ev->CntlPlanned);
                            Bc250DisplayLog("[GfxRing] BRINGUP readback: CNTL=%08X BASE=%08X ConfigOk=%u | desalt tentado=%u ok=%u ME_CNTL=%08X\n",
                                ev->CntlReadback, ev->BaseReadback,
                                ev->ApplyReadbackPassed, ev->UnhaltAttempted,
                                ev->UnhaltPassed, ev->MeCntlAfter);
                            Bc250DisplayLog("[GfxRing] BRINGUP partida: tentada=%u ok=%u ACTIVE_readback=%08X | rollback tentado=%u ok=%u\n",
                                ev->RingActivateAttempted, ev->RingActivatePassed,
                                ev->RingActivateReadback, ev->RollbackAttempted,
                                ev->RollbackPassed);
                        }
                    } else if (context->GfxRingBringupLevel > 0u) {
                        Bc250DisplayLog("[GfxRing] BRINGUP DESARMADO: level=%u mas present=%u -- valor ausente no registro\n",
                            context->GfxRingBringupLevel,
                            context->GfxRingBringupPresent);
                    }
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_MC_LOW,
                        (ULONG)context->PfpDirectLoadEvidence.FirmwareMcAddress);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_MC_HIGH,
                        (ULONG)(context->PfpDirectLoadEvidence.FirmwareMcAddress >> 32));
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_BYTES,
                        context->PfpDirectLoadEvidence.FirmwareBytes);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_JT_DWORDS,
                        context->PfpDirectLoadEvidence.JumpTableDwords);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_VERSION,
                        context->PfpDirectLoadEvidence.UcodeVersion);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_STAGED_DWORDS,
                        context->PfpDirectLoadEvidence.StagedDwords);
                    Bc250ProbeRecord(
                        BC250_PROBE_PFP_DIRECT_STAGING_MISMATCHES,
                        context->PfpDirectLoadEvidence.StagingMismatches);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_ME_CNTL_BEFORE,
                        context->PfpDirectLoadEvidence.CpMeCntlBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_ME_CNTL_APPLIED,
                        context->PfpDirectLoadEvidence.CpMeCntlApplied);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_ME_CNTL_AFTER,
                        context->PfpDirectLoadEvidence.CpMeCntlAfter);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RLC_CNTL_BEFORE,
                        context->PfpDirectLoadEvidence.RlcCntlBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RLC_CNTL_APPLIED,
                        context->PfpDirectLoadEvidence.RlcCntlApplied);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PC_BEFORE,
                        context->PfpDirectLoadEvidence.PfpPcBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PC_FIRST,
                        context->PfpDirectLoadEvidence.PfpPcFirst);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PC_LAST,
                        context->PfpDirectLoadEvidence.PfpPcLast);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PC_TRANSITIONS,
                        context->PfpDirectLoadEvidence.PfpPcTransitions);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PC_SAMPLES,
                        context->PfpDirectLoadEvidence.PfpPcSamples);
                    Bc250ProbeRecord(
                        BC250_PROBE_PFP_DIRECT_IC_INV_ENTRY_MASK,
                        context->PfpDirectLoadEvidence.IcacheInvalidateEntryMask);
                    Bc250ProbeRecord(
                        BC250_PROBE_PFP_DIRECT_IC_INV_UNHALT_MASK,
                        context->PfpDirectLoadEvidence.IcacheInvalidateUnhaltMask);
                    Bc250ProbeRecord(
                        BC250_PROBE_PFP_DIRECT_IC_PRIME_UNHALT_MASK,
                        context->PfpDirectLoadEvidence.IcachePrimeUnhaltMask);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_COMPARED_DWORDS,
                        context->PfpDirectLoadEvidence.Rb0ComparedDwords);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_MISMATCHES,
                        context->PfpDirectLoadEvidence.Rb0MismatchCount);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_DEST_POLLS,
                        context->PfpDirectLoadEvidence.Rb0DestPolls);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_DEST_OBSERVED,
                        context->PfpDirectLoadEvidence.Rb0DestObserved);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_JT_STAT_PFP,
                        context->PfpDirectLoadEvidence.JtStatPfp);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_JT_STAT_CE,
                        context->PfpDirectLoadEvidence.JtStatCe);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_JT_STAT_MEC,
                        context->PfpDirectLoadEvidence.JtStatMec);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ENGINE,
                        context->PfpDirectLoadEvidence.InvalidateEngine);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ACK_BEFORE_STAGING,
                        context->PfpDirectLoadEvidence.InvalidateAckBeforeStaging);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ACK_STAGING,
                        context->PfpDirectLoadEvidence.InvalidateAckStaging);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ACK_BEFORE_PRE_WPTR,
                        context->PfpDirectLoadEvidence.InvalidateAckBeforePreWptr);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_USE_SEMAPHORE,
                        context->PfpDirectLoadEvidence.InvalidateUseSemaphore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ACK_RAW_STAGING,
                        context->PfpDirectLoadEvidence.InvalidateAckRawStaging);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ACK_RAW_PRE_WPTR,
                        context->PfpDirectLoadEvidence.InvalidateAckRawPreWptr);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_SEM_ACQ_STAGING,
                        context->PfpDirectLoadEvidence.InvalidateSemAcquiredStaging);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_SEM_ACQ_PRE_WPTR,
                        context->PfpDirectLoadEvidence.InvalidateSemAcquiredPreWptr);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_CP_SOFT_RESET,
                        context->PfpDirectLoadEvidence.CpSoftResetApplied);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_VMID0_PT,
                        context->PfpDirectLoadEvidence.Vmid0PageTableApplied);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_VMID0_INV_ACK,
                        context->PfpDirectLoadEvidence.Vmid0InvalidateAck);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_VMID0_ACK_AFTER,
                        context->PfpDirectLoadEvidence.Vmid0AckAfter);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_POLLS_STAGING,
                        context->PfpDirectLoadEvidence.InvalidatePollsStaging);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_ACK_PRE_WPTR,
                        context->PfpDirectLoadEvidence.InvalidateAckPreWptr);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_INV_POLLS_PRE_WPTR,
                        context->PfpDirectLoadEvidence.InvalidatePollsPreWptr);
                    {
                        uint32_t traceIndex;
                        for (traceIndex = 0u;
                             traceIndex < BC250_PFP_PC_TRACE_SAMPLES;
                             ++traceIndex) {
                            Bc250ProbeRecord(
                                (BC250_PROBE_FIELD)(
                                    BC250_PROBE_PFP_DIRECT_PC_TRACE_00 + traceIndex),
                                context->PfpDirectLoadEvidence.PfpPcTrace[traceIndex]);
                        }
                    }
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_ME_PC_BEFORE,
                        context->PfpDirectLoadEvidence.MePcBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_ME_PC_LAST,
                        context->PfpDirectLoadEvidence.MePcLast);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_CE_PC_BEFORE,
                        context->PfpDirectLoadEvidence.CePcBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_CE_PC_LAST,
                        context->PfpDirectLoadEvidence.CePcLast);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_FAULT_BEFORE,
                        context->PfpDirectLoadEvidence.FaultStatusBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_FAULT_AFTER,
                        context->PfpDirectLoadEvidence.FaultStatusAfter);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_FAULT_ADDR_LOW,
                        context->PfpDirectLoadEvidence.FaultAddressLow);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_FAULT_ADDR_HIGH,
                        context->PfpDirectLoadEvidence.FaultAddressHigh);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PORTS_PROGRAMMED,
                        context->PfpDirectLoadEvidence.WriteOnlyPortsProgrammed);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_ACTIVE_BEFORE,
                        context->PfpDirectLoadEvidence.Rb0ActiveBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_ACTIVE_APPLIED,
                        context->PfpDirectLoadEvidence.Rb0ActiveApplied);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_ACTIVE_AFTER,
                        context->PfpDirectLoadEvidence.Rb0ActiveAfter);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_RPTR_BEFORE,
                        context->PfpDirectLoadEvidence.Rb0RptrBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_RPTR_AFTER,
                        context->PfpDirectLoadEvidence.Rb0RptrAfter);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_WPTR_APPLIED,
                        context->PfpDirectLoadEvidence.Rb0WptrApplied);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_FENCE_BEFORE,
                        context->PfpDirectLoadEvidence.Rb0FenceBefore);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_RB0_FENCE_AFTER,
                        context->PfpDirectLoadEvidence.Rb0FenceAfter);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_REG_RESTORE,
                        context->PfpDirectLoadEvidence.RegisterRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_MEM_RESTORE,
                        context->PfpDirectLoadEvidence.MemoryRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_PFP_DIRECT_PHYSICAL_READY,
                        context->PfpDirectLoadEvidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_STATUS,
                        (ULONG)context->SdmaF32Status);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_CLOCK_BEFORE,
                        context->SdmaF32Evidence.ClockBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_CONTROL_BEFORE,
                        context->SdmaF32Evidence.ControlBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_CONTROL_APPLIED,
                        context->SdmaF32Evidence.ControlApplied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_F32_BEFORE,
                        context->SdmaF32Evidence.F32Before);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_F32_APPLIED,
                        context->SdmaF32Evidence.F32Applied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_PC_FIRST,
                        context->SdmaF32Evidence.PcFirst);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_PC_LAST,
                        context->SdmaF32Evidence.PcLast);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_PC_TRANSITIONS,
                        context->SdmaF32Evidence.PcTransitions);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_APPLY_READBACK,
                        context->SdmaF32Evidence.ApplyReadbackPassed);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_RESTORE,
                        context->SdmaF32Evidence.RestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_F32_PHYSICAL_READY,
                        context->SdmaF32Evidence.PhysicalReady);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WORK_STATUS,
                        (ULONG)context->SdmaRingWorkStatus);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_FIXED_RESERVATION_STATUS,
                        context->SdmaRingWorkEvidence.FixedReservationStatus);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_ALLOCATION_MODE,
                        context->SdmaRingWorkEvidence.AllocationMode);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RING_LOW,
                        (ULONG)context->SdmaRingWorkEvidence.RingMcAddress);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RING_HIGH,
                        (ULONG)(context->SdmaRingWorkEvidence.RingMcAddress >> 32));
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WORK_LOW,
                        (ULONG)context->SdmaRingWorkEvidence.WorkMcAddress);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WORK_HIGH,
                        (ULONG)(context->SdmaRingWorkEvidence.WorkMcAddress >> 32));
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RPTR_BEFORE,
                        context->SdmaRingWorkEvidence.RptrBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RPTR_AFTER,
                        context->SdmaRingWorkEvidence.RptrAfter);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WPTR,
                        context->SdmaRingWorkEvidence.WptrPublished);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WPTR_REGISTER,
                        context->SdmaRingWorkEvidence.WptrRegisterAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WPTR_SHADOW,
                        context->SdmaRingWorkEvidence.WptrShadowAfterKick);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RB_CNTL,
                        context->SdmaRingWorkEvidence.RbCntlApplied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_HEADER,
                        context->SdmaRingWorkEvidence.RingHeaderReadback);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RPTR_REGISTER,
                        context->SdmaRingWorkEvidence.RptrRegisterAfter);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_POLL_CNTL,
                        context->SdmaRingWorkEvidence.WptrPollCntlApplied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_FENCE_POISON,
                        context->SdmaRingWorkEvidence.FencePoison);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_FENCE_EXPECTED,
                        context->SdmaRingWorkEvidence.FenceExpected);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_FENCE_OBSERVED,
                        context->SdmaRingWorkEvidence.FenceObserved);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_POLLS,
                        context->SdmaRingWorkEvidence.Polls);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_COMPARED_DWORDS,
                        context->SdmaRingWorkEvidence.ComparedDwords);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_MISMATCHES,
                        context->SdmaRingWorkEvidence.MismatchCount);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_STATUS_BEFORE,
                        context->SdmaRingWorkEvidence.StatusBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_STATUS_KICK,
                        context->SdmaRingWorkEvidence.StatusKick);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_STATUS_AFTER,
                        context->SdmaRingWorkEvidence.StatusAfter);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_PC_KICK,
                        context->SdmaRingWorkEvidence.PcKick);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_STATUS1_AFTER,
                        context->SdmaRingWorkEvidence.Status1After);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_STATUS2_AFTER,
                        context->SdmaRingWorkEvidence.Status2After);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_STATUS3_AFTER,
                        context->SdmaRingWorkEvidence.Status3After);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_ERROR_LOG_AFTER,
                        context->SdmaRingWorkEvidence.ErrorLogAfter);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_ERROR_LOG_BEFORE,
                        context->SdmaRingWorkEvidence.ErrorLogBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_CNTL_BEFORE,
                        context->SdmaRingWorkEvidence.CntlBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_CNTL_APPLIED,
                        context->SdmaRingWorkEvidence.CntlApplied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_EX_IDLE_EXPIRED,
                        context->SdmaRingWorkEvidence.ExIdlePollExpiredBefore);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_EX_IDLE_AT_ENTRY,
                        context->SdmaRingWorkEvidence.ExIdleAtEntry);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WAKE_APPLIED,
                        context->SdmaRingWorkEvidence.WakeApplied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_EX_IDLE_AFTER_WAKE,
                        context->SdmaRingWorkEvidence.ExIdleAfterWake);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_EX_IDLE_CLEAR_POLLS,
                        context->SdmaRingWorkEvidence.ExIdleClearPolls);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_CLK_AT_ENTRY,
                        context->SdmaRingWorkEvidence.ClkCtrlAtEntry);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_EARLY_CLK_UNGATE,
                        context->SdmaRingWorkEvidence.EarlyClkUngateApplied);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_RD_XNACK0_BEFORE,
                        context->SdmaRingWorkEvidence.Utcl1ReadXnack0Before);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_WR_XNACK0_BEFORE,
                        context->SdmaRingWorkEvidence.Utcl1WriteXnack0Before);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_UTCL1_TIMEOUT_BEFORE,
                        context->SdmaRingWorkEvidence.Utcl1TimeoutBefore);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_RD_STATUS_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1ReadStatusAfter);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_WR_STATUS_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1WriteStatusAfter);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_RD_XNACK0_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1ReadXnack0After);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_RD_XNACK1_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1ReadXnack1After);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_WR_XNACK0_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1WriteXnack0After);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_WR_XNACK1_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1WriteXnack1After);
                    Bc250ProbeRecord(
                        BC250_PROBE_SDMA_RING_UTCL1_TIMEOUT_AFTER,
                        context->SdmaRingWorkEvidence.Utcl1TimeoutAfter);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_REG_RESTORE,
                        context->SdmaRingWorkEvidence.RegisterRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_MEM_RESTORE,
                        context->SdmaRingWorkEvidence.MemoryRestorePassed);
                    Bc250ProbeRecord(BC250_PROBE_SDMA_RING_PHYSICAL_READY,
                        context->SdmaRingWorkEvidence.PhysicalReady);
                    {
                        BC250_MCDM_RUNTIME_CAPS caps;
                        NTSTATUS capsStatus = Bc250ActiveQueryRuntimeCaps(
                            context, &caps);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_STATUS,
                            (ULONG)capsStatus);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_READY,
                            caps.Ready);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_BASE_LOW32,
                            (ULONG)caps.LocalSegmentBaseOffset);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_LOCAL_LOW32,
                            (ULONG)caps.LocalSegmentBytes);
                        Bc250ProbeRecord(BC250_PROBE_RUNTIME_CAPS_NODE_COUNT,
                            caps.NodeCount);
                        Bc250ProbeRecord(BC250_PROBE_MCDM_FULL_CAPS_READY,
                            caps.FullMcdmReady);
                        Bc250ProbeRecord(BC250_PROBE_MCDM_SEGMENT4_READY,
                            caps.Ready);
                        if (NT_SUCCESS(capsStatus) && caps.Ready != 0u) {
                            context->AllocationSegmentBytes =
                                caps.LocalSegmentBytes;
                            context->AllocationSegmentSet = 1u;
                        }
                        Bc250ProbeRecord(
                            BC250_PROBE_ALLOCATION_SEGMENT_SET,
                            context->AllocationSegmentSet);
                        Bc250ProbeRecord(
                            BC250_PROBE_ALLOCATION_MAX_LOW32,
                            (ULONG)context->AllocationSegmentBytes);
                        Bc250ProbeRecord(
                            BC250_PROBE_GPUVA_DESCRIPTOR_READY,
                            caps.GpuVaReady != 0u &&
                            caps.GpuVaBitCount != 0u &&
                            caps.PageTableLevelCount != 0u);
                        Bc250ProbeRecord(
                            BC250_PROBE_ROOT_PAGE_TABLE_ABI_READY,
                            (ULONG)InterlockedCompareExchange(
                                &context->RootPageTableAbiReady, 1, 1));
                        Bc250ProbeRecord(
                            BC250_PROBE_ROOT_PAGE_TABLE_MAX_ENTRIES,
                            context->RootPageTableMaxEntries);
                        Bc250ProbeRecord(
                            BC250_PROBE_PAGING_CPU_UPDATE_READY,
                            caps.GpuVaReady != 0u &&
                            caps.PagingReady != 0u);
                        Bc250ProbeRecord(
                            BC250_PROBE_TLB_FLUSH_PHYSICAL_READY,
                            context->TlbTransactionEvidence.PhysicalReady);
                    }
                    if (context->PspRingStatus == BC250_PSP_RING_OK &&
                        context->PspRingEvidence.PhysicalReady != 0u &&
                        context->PspLoadFwStatus == BC250_PSP_LOAD_FW_OK &&
                        /* Mesma porta da cadeia: o tripe de boot provado no
                         * silicio, e nao o ring work que colide com ele. */
                        context->SdmaBootTripodValidated != 0u &&
                        (context->ShaderDispatch == 0u ||
                            (context->ShaderDispatch == 1u &&
                             context->HqdActiveStatus == BC250_HQD_ACTIVE_OK &&
                             context->HqdActiveEvidence.PhysicalReady != 0u)) &&
                        (context->PersistentSubmission == 0u ||
                            (context->PersistentSubmission == 1u &&
                             persistentReady != FALSE))) {
                        if (Bc250ActiveGetAdapterProfile() ==
                                BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
                            if (NumberOfVideoPresentSources != NULL)
                                *NumberOfVideoPresentSources = 0;
                            if (NumberOfChildren != NULL) *NumberOfChildren = 0;
                        } else {
                            if (NumberOfVideoPresentSources != NULL)
                                *NumberOfVideoPresentSources = 1;
                            if (NumberOfChildren != NULL) *NumberOfChildren = 1;
                        }
                        if (context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_EMPTY) {
                            BC250_MMIO_TOPOLOGY_STATUS mmioMapStatus = Bc250MmioTopologyMapReadonly(
                                &context->Core.MmioTransport, &context->Core.DxgkInterface,
                                context->Core.ResolvedBars, context->Core.ResolvedBarCount,
                                BC250_PCI_MMIO_BAR_INDEX);
                            Bc250DisplayLog("[StartDevice] Runtime MmioTransport map status: %s (State=%u, Base=%p)\n",
                                Bc250MmioTopologyStatusName(mmioMapStatus),
                                context->Core.MmioTransport.State,
                                context->Core.MmioTransport.VirtualBase);
                        }
                        InterlockedExchange(&context->IsActive, 1);
                        Bc250ActiveArmLifecycle(context);
                        /*
                         * Fonte unica: sem isto o laboratorio publicava
                         * DmaBuf=0x0/SegSet=0x0 (corrida 14:06). Tem de vir ANTES
                         * de ContextAbiReady, e ha' gate estatico exigindo a ordem.
                         */
                        Bc250ActiveInitializeContextAbi(context);
                        context->RootPageTableMaxEntries =
                            BC250_GPUVA_PT_ROOT_MAX_ENTRIES;
                        InterlockedExchange(&context->ContextAbiReady, 1);
                        InterlockedExchange(&context->AllocationAbiReady, 1);
                        InterlockedExchange(&context->RootPageTableAbiReady, 1);
                        Bc250ActivePublishAbiReadyTelemetry(context);
                        return STATUS_SUCCESS;
                    }
                }
            }
        }
    }

    InterlockedExchange(&context->IsActive, 0);
    Bc250ActiveRetirementTimerStop(context);
    Bc250ActiveSdmaRetirementTimerStop(context);
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    Bc250ActivePublishAbiReadyTelemetry(context);
    /*
     * persistentAttempted E' OBRIGATORIO AQUI.
     *
     * Sem ele, um bring-up que nem CHEGOU ao ensaio de submissao persistente
     * publicava o inicializador de persistentStatus (STATUS_NOT_SUPPORTED) como
     * veredito do StartDevice, apagando a falha que de fato ocorreu. Foi
     * exatamente isso na corrida 08/09 11:48: o segundo preflight foi recusado
     * com STATUS_INVALID_PARAMETER, o bring-up inteiro foi pulado, e o
     * dispositivo reportou 0xC00000BB -- um valor que nenhuma medicao produziu.
     * Uma falha real ja carregada em preflightStatus manda sobre o ensaio que
     * nunca aconteceu.
     */
    if (context->PersistentSubmission == 1u &&
        persistentReady == FALSE &&
        persistentAttempted != FALSE) {
        preflightStatus = !NT_SUCCESS(persistentStatus) ?
            persistentStatus : (!NT_SUCCESS(selfTestStatus) ?
                selfTestStatus : p2Status);
        if (NT_SUCCESS(preflightStatus)) {
            preflightStatus = STATUS_DEVICE_HARDWARE_ERROR;
        }
    } else if (context->PersistentSubmission == 1u &&
               persistentReady == FALSE) {
        /*
         * Nao houve ensaio. preflightStatus permanece o status REAL do ponto em
         * que o bring-up parou; se por algum caminho ele vier em sucesso, a
         * resposta honesta e' falha de hardware -- nunca STATUS_SUCCESS, porque
         * a submissao persistente foi pedida e nao esta viva.
         */
        Bc250DisplayLog("[StartDevice] Submissao persistente pedida e NAO ENSAIADA: o bring-up parou antes. Status preservado=0x%08X\n",
            (uint32_t)preflightStatus);
        if (NT_SUCCESS(preflightStatus)) {
            preflightStatus = STATUS_DEVICE_HARDWARE_ERROR;
        }
    } else if (context->PersistentSubmission != 0u &&
               context->PersistentSubmission != 1u) {
        preflightStatus = STATUS_DEVICE_CONFIGURATION_ERROR;
    } else if (context->ShaderDispatch != 0u &&
               context->ShaderDispatch != 1u) {
        preflightStatus = STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Bc250ProbeRecordFirstFailure(preflightStatus);
    return preflightStatus;
#endif
}

static NTSTATUS Bc250ActiveQuiesceAndSnapshotTracker(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ uint32_t StopReason,
    _Out_ uint32_t *PendingCount,
    _Out_ uint32_t *ReservedCount);

static NTSTATUS Bc250DxgkDdiStopDeviceActiveReason(
    CONST PVOID MiniportDeviceContext,
    uint32_t StopReason)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    uint32_t destroyedObjects;
    uint32_t pendingCount = 0u;
    uint32_t reservedCount = 0u;
    BC250_MCDM_OBJECT_STATUS teardownStatus;
    NTSTATUS engineStatus = STATUS_SUCCESS;
    NTSTATUS mmuRestoreNtStatus = STATUS_SUCCESS;
    NTSTATUS quiesceStatus;
    NTSTATUS coreStatus;
    static LONG s_stopFirstStatusRecorded;
    /*
     * Publicar primeiro o rastro barato acumulado desde StartDevice. Fazer
     * isto antes de qualquer teardown preserva a ultima DDI que levou o
     * dxgkrnl a parar o adaptador, e agora tambem os GUIDs completos das
     * interfaces pedidas e o marco pos-StartDevice mais avancado.
     */
    Bc250ProbeSnapshotDdiTrace();
    Bc250ProbeSnapshotQueryInterfaces();
    /*
     * A marca de StopDevice vem DEPOIS do snapshot acima: marcar antes
     * empurraria a propria StopDevice para dentro do retrato e apagaria uma
     * posicao do rastro que interessa mais.
     */
    Bc250ProbeMarkDdi(BC250_V5_DDI_STOP_DEVICE);
    Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_ENTERED, 1);
    Bc250DisplayLog("[StopDevice] Entered. Reason=%u\n", StopReason);
    if (context == NULL ||
        context->Signature != BC250_ACTIVE_SIGNATURE) {
        Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS,
                         (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[StopDevice] WDDM submissions: Accepted=%ld, NotExecuted=%ld, VMID8Status=%u, InstalledCtx=%p, Root=0x%llX\n",
        context->SubmissionsCount,
        context->SubmissionsNotExecutedCount,
        (uint32_t)context->Vmid8InstallStatus,
        context->Vmid8InstalledContext,
        (unsigned long long)context->Vmid8InstalledRootPhysical);
    Bc250DisplayLog("[SDMA_RETIRE_ARCHIVE] DpcRuns=%u NotifiedTotal=%u BatchesNotified=%u WatermarkSkipped=%u NotifyFailed=%u BusyYields=%u LastNotifiedNode0=%u\n",
        context->SdmaRetirementDpcRuns,
        context->SdmaRetirementNotifiedTotal,
        context->SdmaRetirementBatchesNotifiedTotal,
        context->SdmaRetirementFencesSkippedWatermark,
        context->SdmaRetirementNotifyFailedTotal,
        context->SdmaRetirementBusyYields,
        context->SubmissionEngine.WddmFences.LastNotifiedFenceIdByNode[0]);
    Bc250DisplayLogSamplingSummary();

    /*
     * Quiescencia antes da fotografia. Nao desligar polling/engine enquanto
     * uma fence ainda possui pin GPUVA — mas tambem nao declarar ocupado por
     * causa de uma consulta que falhou.
     */
    quiesceStatus = Bc250ActiveQuiesceAndSnapshotTracker(
        context, StopReason, &pendingCount, &reservedCount);
    if (!NT_SUCCESS(quiesceStatus)) {
        if (StopReason == BC250_ACTIVE_STOP_REASON_REMOVE_DEVICE) {
            Bc250DisplayLog("[StopDevice] REMOVE_DEVICE quiesce status=0x%08X: forçando reset do tracker para teardown limpo\n",
                (uint32_t)quiesceStatus);
            Bc250WddmFenceTrackerReset(&context->SubmissionEngine.WddmFences);
            pendingCount = 0u;
            reservedCount = 0u;
        } else {
            NTSTATUS reported = (quiesceStatus == STATUS_DEVICE_BUSY) ?
                STATUS_DEVICE_BUSY : quiesceStatus;
            Bc250ProbeRecordFirstFailure(reported);
            Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS, (ULONG)reported);
            if (InterlockedCompareExchange(&s_stopFirstStatusRecorded, 1, 0) == 0) {
                Bc250ProbeRecord(BC250_PROBE_V5_STOP_FIRST_STATUS, (ULONG)reported);
            }
            /* STOPPING continua fechando novas admissoes, mas mantem retirement para STOP_DEVICE. */
            if (InterlockedCompareExchange(&context->IsActive, 1, 1) == 1) {
                Bc250ProbeRecord(BC250_PROBE_V5_STOP_TIMER_DRAINED, 0u);
                Bc250ActiveRetirementTimerStart(context);
            }
            return reported;
        }
    }
    if (pendingCount != 0u || reservedCount != 0u) {
        if (StopReason == BC250_ACTIVE_STOP_REASON_REMOVE_DEVICE) {
            Bc250DisplayLog("[StopDevice] REMOVE_DEVICE com pending=%u reserved=%u: forçando reset do tracker para teardown limpo\n",
                pendingCount, reservedCount);
            Bc250WddmFenceTrackerReset(&context->SubmissionEngine.WddmFences);
            pendingCount = 0u;
            reservedCount = 0u;
        } else {
            Bc250ProbeRecordFirstFailure(STATUS_DEVICE_BUSY);
            Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS,
                             (ULONG)STATUS_DEVICE_BUSY);
            if (InterlockedCompareExchange(&s_stopFirstStatusRecorded, 1, 0) == 0) {
                Bc250ProbeRecord(BC250_PROBE_V5_STOP_FIRST_STATUS,
                                 (ULONG)STATUS_DEVICE_BUSY);
            }
            /* Manter retirement para STOP_DEVICE sem reabrir admissoes. */
            if (InterlockedCompareExchange(&context->IsActive, 1, 1) == 1) {
                Bc250ProbeRecord(BC250_PROBE_V5_STOP_TIMER_DRAINED, 0u);
                Bc250ActiveRetirementTimerStart(context);
            }
            return STATUS_DEVICE_BUSY;
        }
    }
    if (InterlockedCompareExchange(&s_stopFirstStatusRecorded, 1, 0) == 0) {
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_FIRST_STATUS, (ULONG)STATUS_SUCCESS);
    }
    InterlockedExchange(&context->IsActive, 0);
    Bc250ActiveRetirementTimerStop(context);
    Bc250ActiveSdmaRetirementTimerStop(context);
    InterlockedExchange(&context->SdmaRetireBusy, 0);
    Bc250SubmissionEngineArchiveDiagnostics(context);
    Bc250SubmissionEngineRecordTelemetry(context);
    Bc250DisplayLog("[NODE1_IB_CPU_ARCHIVE] VA=0x%llX PA=0x%llX RequestedBytes=%u CapturedDwords=%u (CPU observation, not GPU execution)\n",
        (unsigned long long)context->Node1IbObservedVa,
        (unsigned long long)context->Node1IbObservedPhysical,
        context->Node1IbRequestedBytes, context->Node1IbObservedDwords);
    {
        uint32_t word;
        for (word = 0u; word < context->Node1IbObservedDwords; ++word) {
            Bc250DisplayLog("[NODE1_IB_CPU_WORD] DW=%u Value=%08X\n",
                word, context->Node1IbCpuWords[word]);
        }
    }
    {
        uint32_t stopFaultStatus = 0u, stopFaultAddrLo = 0u, stopFaultAddrHi = 0u;
        uint32_t stopFaultValid = 0u;
        if (Bc250ActiveVmidRead32(context, BC250_ACTIVE_GCVM_FAULT_STATUS, &stopFaultStatus)) stopFaultValid |= (1u << 0);
        if (Bc250ActiveVmidRead32(context, BC250_ACTIVE_GCVM_FAULT_ADDR_LO, &stopFaultAddrLo)) stopFaultValid |= (1u << 1);
        if (Bc250ActiveVmidRead32(context, BC250_ACTIVE_GCVM_FAULT_ADDR_HI, &stopFaultAddrHi)) stopFaultValid |= (1u << 2);
        Bc250DisplayLog("[NODE1_STOP_GCVM_FAULT] Valid=0x%X STATUS=0x%08X ADDR=0x%08X:%08X\n",
            stopFaultValid, stopFaultStatus, stopFaultAddrHi, stopFaultAddrLo);
    }
    /* Retirement DPC is drained. These observations were captured under
     * the engine lock; at PASSIVE_LEVEL they can now reach the disk log. */
    if (context->SubmissionEngine.DiagnosticPendingFence != 0ull) {
        const BC250_SUBMISSION_ENGINE *ev = &context->SubmissionEngine;
        Bc250DisplayLog("[MEC_PENDING_ARCHIVE] Fence=%llu Observed=%u RPTR=%u Writeback=%u ValidReads=0x%X Active=%08X Error=%08X HqStatus=%08X WPTR=%u Vmid=%08X IbControl=%08X\n",
            (unsigned long long)ev->DiagnosticPendingFence,
            ev->DiagnosticObservedFence, ev->DiagnosticRptr,
            ev->DiagnosticWriteback, ev->DiagnosticValidReads,
            ev->DiagnosticQueueRaw[0], ev->DiagnosticQueueRaw[1],
            ev->DiagnosticQueueRaw[2], ev->DiagnosticQueueRaw[3],
            ev->DiagnosticQueueRaw[4], ev->DiagnosticQueueRaw[5]);
        Bc250DisplayLog("[MEC_IB_DESCRIPTOR_ARCHIVE] Raw=%08X,%08X,%08X,%08X\n",
            ev->DiagnosticPendingDescriptor[0], ev->DiagnosticPendingDescriptor[1],
            ev->DiagnosticPendingDescriptor[2], ev->DiagnosticPendingDescriptor[3]);
    }
    InterlockedExchange(&context->ContextAbiReady, 0);
    InterlockedExchange(&context->AllocationAbiReady, 0);
    InterlockedExchange(&context->RootPageTableAbiReady, 0);
    /* Stop the consumer before restoring the address space it may still
     * reference. Offline borrows core MMIO; the mapping remains available. */
    engineStatus = Bc250SubmissionEngineOffline(context);
    Bc250ProbeRecordFirstFailure(engineStatus);
    if (context->ComputeVmidBinding.SnapshotValid) {
        BC250_GCVM_VMID8_INTERFACE computeIo;
        computeIo.Read32 = Bc250ActiveVmidRead32;
        computeIo.Write32 = Bc250ActiveVmidWrite32;
        computeIo.Flush = Bc250ActiveFlushComputeTlb;
        computeIo.Context = context;
        computeIo.Identity = BC250_GCVM_VMID8_IDENTITY;
        if (Bc250GcvmComputeRestore(&computeIo, &context->ComputeVmidBinding) != BC250_GCVM_VMID8_OK) {
            Bc250DisplayLog("[StopDevice] VMID9 RESTORE FAILED apos desligar MEC; MMIO do core ainda mapeado\n");
            return STATUS_DEVICE_NOT_READY;
        }
        Bc250DisplayLog("[StopDevice] VMID9 baseline restaurado com readback e ACK\n");
    }
    {
        BC250_GCVM_VMID8_INTERFACE vmid8Io;
        BC250_GCVM_VMID8_INSTALL_EVIDENCE cleanEv;
        RtlZeroMemory(&vmid8Io, sizeof(vmid8Io));
        vmid8Io.Read32 = Bc250ActiveVmidRead32;
        vmid8Io.Write32 = Bc250ActiveVmidWrite32;
        vmid8Io.Flush = Bc250ActiveFlushVmid8Only;
        vmid8Io.Context = context;
        vmid8Io.Identity = BC250_GCVM_VMID8_IDENTITY;
        RtlZeroMemory(&cleanEv, sizeof(cleanEv));
        (void)Bc250GcvmVmid8Uninstall(&vmid8Io, &cleanEv);
        context->Vmid8InstalledContext = NULL;
        context->Vmid8InstalledRootPhysical = 0ull;
        context->Vmid8InstallStatus = BC250_GCVM_VMID8_OK;
        Bc250DisplayLog("[StopDevice] VMID8 uninstalled e TLB invalidada\n");
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&context->ObjectLock);
    InterlockedExchange(&context->GpuvaMappingsOperational, 0);
    InterlockedExchange(&context->GpuvaActivationEligible, 0);
    context->GpuvaActivationBlockMask =
        BC250_GPUVA_GATE_BLOCK_ADAPTER_INACTIVE;
    InterlockedExchange(&context->GpuvaJournalReady, 0);
    {
        uint32_t journalSlot;
        for (journalSlot = 0u;
             journalSlot < BC250_ACTIVE_GPUVA_JOURNAL_SLOTS;
             ++journalSlot) {
            Bc250GpuvaJournalAbort(&context->GpuvaJournals[journalSlot]);
        }
    }
    context->GpuvaHierarchyPending = 0u;
    context->GpuvaHierarchyProcess = NULL;
    context->GpuvaHierarchyRootPageTable = 0u;
    context->GpuvaHierarchyHighestLevel = 0u;
    destroyedObjects = 0u;
    teardownStatus = Bc250McdmObjectRegistryTeardown(
        &context->Objects, &destroyedObjects);
    ExReleasePushLockExclusive(&context->ObjectLock);
    KeLeaveCriticalRegion();
    if (teardownStatus != BC250_MCDM_OBJECT_OK) {
        NTSTATUS status = Bc250ObjectStatusToNtStatus(teardownStatus);
        Bc250ProbeRecordFirstFailure(status);
        Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS, (ULONG)status);
        return status;
    }
    Bc250DisplayTeardown();
    if (context->GlobalMmuReady) {
        BC250_GCVM_GLOBAL_MMU_INTERFACE mmuIo;
        if (context->Core.MmioTransport.State !=
                BC250_MMIO_TOPOLOGY_MAPPED) {
            mmuRestoreNtStatus = STATUS_DEVICE_NOT_READY;
            Bc250DisplayLog("[StopDevice] Global GFXHUB MMU restore FALHOU: transporte MMIO state=%u\n",
                context->Core.MmioTransport.State);
        } else {
            BC250_GCVM_GLOBAL_MMU_STATUS mmuRestoreStatus;
            RtlZeroMemory(&mmuIo, sizeof(mmuIo));
            mmuIo.Read32 = Bc250ActiveVmidRead32;
            mmuIo.Write32 = Bc250ActiveVmidWrite32;
            mmuIo.Flush = Bc250ActiveFlushMappedGfxhubTlb;
            mmuIo.Context = context;
            mmuIo.Identity = BC250_GCVM_GLOBAL_MMU_IDENTITY;
            mmuRestoreStatus = Bc250GcvmGlobalMmuRestore(
                &mmuIo, &context->GlobalMmuEvidence);
            if (mmuRestoreStatus == BC250_GCVM_GLOBAL_MMU_OK &&
                context->GlobalMmuEvidence.RestorePassed != 0u) {
                Bc250DisplayLog("[StopDevice] Global GFXHUB MMU restored to snapshot: Regs=%u\n",
                    context->GlobalMmuEvidence.RegisterCount);
            } else {
                mmuRestoreNtStatus = STATUS_DEVICE_HARDWARE_ERROR;
                Bc250DisplayLog("[StopDevice] Global GFXHUB MMU restore FALHOU: status=%u step=%u reg=0x%05X val=0x%08X failures=%u\n",
                    (uint32_t)mmuRestoreStatus,
                    context->GlobalMmuEvidence.FailureStep,
                    context->GlobalMmuEvidence.FailureRegister,
                    context->GlobalMmuEvidence.FailureValue,
                    context->GlobalMmuEvidence.RestoreFailureCount);
            }
        }
        context->GlobalMmuReady = FALSE;
    }
    if (context->GlobalMmuDummyAllocated) {
        BC250_VRAM_STATUS releaseStatus = Bc250VramRelease(
            &context->GlobalMmuAllocator,
            context->GlobalMmuDummyToken);
        if (releaseStatus != BC250_VRAM_OK) {
            if (NT_SUCCESS(mmuRestoreNtStatus)) {
                mmuRestoreNtStatus = STATUS_DEVICE_HARDWARE_ERROR;
            }
            Bc250DisplayLog("[StopDevice] Global MMU dummy release FALHOU: status=%u offset=0x%llX\n",
                (uint32_t)releaseStatus,
                (unsigned long long)context->GlobalMmuDummyOffset);
        }
        context->GlobalMmuDummyAllocated = FALSE;
        context->GlobalMmuDummyOffset = 0ull;
        RtlZeroMemory(&context->GlobalMmuDummyToken,
            sizeof(context->GlobalMmuDummyToken));
        RtlZeroMemory(&context->GlobalMmuAllocator,
            sizeof(context->GlobalMmuAllocator));
    }
    if (!NT_SUCCESS(mmuRestoreNtStatus)) {
        Bc250ProbeRecordFirstFailure(mmuRestoreNtStatus);
        if (NT_SUCCESS(engineStatus)) engineStatus = mmuRestoreNtStatus;
    }
    coreStatus = Bc250DxgkDdiStopDeviceReadonly(&context->Core);
    Bc250ProbeRecordFirstFailure(coreStatus);
    if (!NT_SUCCESS(coreStatus)) {
        Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS, (ULONG)coreStatus);
        return coreStatus;
    }
    if (NT_SUCCESS(engineStatus)) {
        InterlockedExchange(&context->LifecycleState,
            (LONG)BC250_ACTIVE_LIFECYCLE_STOPPED);
        Bc250ProbeRecord(BC250_PROBE_V5_LIFECYCLE_STATE,
            BC250_ACTIVE_LIFECYCLE_STOPPED);
    }
    Bc250ProbeRecord(BC250_PROBE_STOP_DEVICE_STATUS, (ULONG)engineStatus);
    (void)Bc250BootFuseDisarmClean(engineStatus);
    return engineStatus;
}

NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceActive(
    CONST PVOID MiniportDeviceContext)
{
    return Bc250DxgkDdiStopDeviceActiveReason(
        MiniportDeviceContext, BC250_ACTIVE_STOP_REASON_STOP_DEVICE);
}

NTSTATUS APIENTRY Bc250DxgkDdiRemoveDeviceActive(
    CONST PVOID MiniportDeviceContext)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    NTSTATUS status = STATUS_SUCCESS;

    Bc250ProbeMarkDdi(BC250_V5_DDI_REMOVE_DEVICE);
    Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_ENTERED, 1);
    if (context == NULL ||
        context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    /*
     * O contrato PnP normalmente chama StopDevice antes de RemoveDevice.
     * Repetir o teardown readonly sobre o mesmo core devolvia
     * STATUS_DEVICE_BUSY e mascarava a causa original do Codigo 43. Ainda
     * suportamos RemoveDevice direto quando o contexto segue ativo.
     */
    if (InterlockedCompareExchange(&context->IsActive, 0, 0) != 0) {
        status = Bc250DxgkDdiStopDeviceActiveReason(
            context, BC250_ACTIVE_STOP_REASON_REMOVE_DEVICE);
        /*
         * Independentemente do resultado de StopDeviceActiveReason,
         * RemoveDevice DEVE prosseguir com a parada de timers, DPCs
         * e desmontagem do contexto. Falhar aqui deixaria timers ativos
         * em memoria prestes a ser descarregada pelo PnP (BugCheck 0xD1).
         */
    }
    Bc250ActiveRetirementTimerStop(context);
    Bc250ActiveSdmaRetirementTimerStop(context);
    Bc250DisplayTeardown();
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeFlushQueuedDpcs();
#endif
    Bc250ActiveUnregisterAdapter(context);
    context->Signature = 0;
    context->PhysicalDeviceObject = NULL;
    context->Core.PhysicalDeviceObject = NULL;
    ExFreePoolWithTag(context, BC250_ACTIVE_POOL_TAG);
    /*
     * Segundo retrato, agora com Stop e Remove ja' marcadas. So' e' seguro
     * republicar porque o anel do schema 134 tem 16 posicoes: com os tres
     * slots antigos, esta chamada apagaria justamente a historia anterior ao
     * teardown. Depois do ExFreePoolWithTag de proposito — o rastro vive em
     * estatica global, nao no contexto liberado.
     */
    Bc250ProbeSnapshotDdiTrace();
    Bc250ProbeRecord(BC250_PROBE_REMOVE_DEVICE_STATUS, (ULONG)status);
    return STATUS_SUCCESS;
}

VOID Bc250ActiveDriverUnloadCleanup(VOID)
{
    uint32_t index;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        BC250_DXGK_ACTIVE_CONTEXT *active = g_Bc250AdapterDirectory[index];
        if (active != NULL && active->Signature == BC250_ACTIVE_SIGNATURE) {
            Bc250ActiveRetirementTimerStop(active);
            Bc250ActiveSdmaRetirementTimerStop(active);
            InterlockedExchange(&active->IsActive, 0);
            g_Bc250AdapterDirectory[index] = NULL;
        }
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();

    Bc250DisplayTeardown();
    Bc250McdmInstallEscapeHandler(NULL);
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeFlushQueuedDpcs();
#endif
}

/* ==========================================================================
   Fase V2 / V5 — rastreador e retirada de fence para o dxgkrnl.
   ========================================================================== */

#define BC250_ACTIVE_FENCE_SYNC_PUSH    0u
#define BC250_ACTIVE_FENCE_SYNC_DRAIN   1u
#define BC250_ACTIVE_FENCE_SYNC_RESERVE 2u
#define BC250_ACTIVE_FENCE_SYNC_CANCEL  3u
#define BC250_ACTIVE_FENCE_SYNC_COMMIT  4u
#define BC250_ACTIVE_FENCE_SYNC_QUERY   5u
#define BC250_ACTIVE_FENCE_SYNC_PENDING 6u
#define BC250_ACTIVE_FENCE_SYNC_NOTIFY_SDMA 7u

#define BC250_ACTIVE_FENCE_NOTIFY_BATCH 16u

typedef struct BC250_ACTIVE_FENCE_SYNC {
    BC250_DXGK_ACTIVE_CONTEXT *Context;
    uint32_t Operation;

    /* PUSH */
    uint64_t RingFence;
    uint32_t SubmissionFenceId;
    uint32_t NodeOrdinal;
    uint32_t EngineOrdinal;
    BOOLEAN PushAccepted;

    /* RESERVE / CANCEL / COMMIT */
    uint32_t ReservationToken;
    BOOLEAN ReserveAccepted;
    BOOLEAN CommitAccepted;
    uint32_t GpuvaSlot;
    uint32_t GpuvaGeneration;
    BOOLEAN GpuvaPinValid;

    /* DRAIN */
    uint64_t RetiredRingFence;
    uint32_t Notified;
    uint32_t CurrentFence;
    uint32_t PendingCount;
    uint32_t ReservedCount;
} BC250_ACTIVE_FENCE_SYNC;

static BOOLEAN Bc250ActiveSynchronizedFenceCallback(
    _In_opt_ PVOID SynchronizeContext)
{
    BC250_ACTIVE_FENCE_SYNC *sync = (BC250_ACTIVE_FENCE_SYNC *)SynchronizeContext;
    BC250_DXGK_ACTIVE_CONTEXT *context;
    BC250_WDDM_FENCE_ENTRY completed[BC250_ACTIVE_FENCE_NOTIFY_BATCH];
    DXGKARGCB_NOTIFY_INTERRUPT_DATA data;
    uint32_t count;
    uint32_t i;

    if (sync == NULL || sync->Context == NULL) {
        return FALSE;
    }
    context = sync->Context;
    if (context->Signature != BC250_ACTIVE_SIGNATURE) {
        return FALSE;
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_RESERVE) {
        sync->ReserveAccepted = Bc250WddmFenceTrackerReserve(
            &context->SubmissionEngine.WddmFences,
            sync->SubmissionFenceId,
            sync->NodeOrdinal,
            sync->EngineOrdinal,
            &sync->ReservationToken);
        if (sync->ReserveAccepted && sync->GpuvaPinValid &&
            !Bc250WddmFenceTrackerAttachGpuvaPin(
                &context->SubmissionEngine.WddmFences,
                sync->ReservationToken, sync->GpuvaSlot,
                sync->GpuvaGeneration)) {
            (void)Bc250WddmFenceTrackerCancel(
                &context->SubmissionEngine.WddmFences,
                sync->ReservationToken);
            sync->ReserveAccepted = FALSE;
        }
        return sync->ReserveAccepted;
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_CANCEL) {
        return Bc250WddmFenceTrackerCancel(
            &context->SubmissionEngine.WddmFences,
            sync->ReservationToken);
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_COMMIT) {
        sync->CommitAccepted = Bc250WddmFenceTrackerCommit(
            &context->SubmissionEngine.WddmFences,
            sync->ReservationToken,
            sync->RingFence);
        return sync->CommitAccepted;
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_PUSH) {
        sync->PushAccepted = Bc250WddmFenceTrackerPush(
            &context->SubmissionEngine.WddmFences,
            sync->RingFence,
            sync->SubmissionFenceId,
            sync->NodeOrdinal,
            sync->EngineOrdinal);
        return sync->PushAccepted;
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_NOTIFY_SDMA) {
        if (context->Core.DxgkInterface.DxgkCbNotifyInterrupt != NULL &&
            context->Core.DxgkInterface.DeviceHandle != NULL) {
            if (sync->NodeOrdinal <= BC250_SUBMISSION_NODE_COMPUTE) {
                if (sync->SubmissionFenceId <= context->SubmissionEngine.WddmFences.LastNotifiedFenceIdByNode[sync->NodeOrdinal]) {
                    /*
                     * Marca d'agua monotonica estrita por no (Q1):
                     * A fence ja foi alcancada ou superada por notificacao anterior.
                     * No WDDM, reportar fence decrescente ou repetida e' violacao fatal
                     * de contrato (BugCheck 0x119 Arg1=1). Descarta silenciosamente com
                     * Notified=1 para o chamador saber que o lote foi consumido.
                     */
                    ++context->SdmaRetirementFencesSkippedWatermark;
                    sync->Notified = 1u;
                    return TRUE;
                }
            }
            RtlZeroMemory(&data, sizeof(data));
            data.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
            data.DmaCompleted.SubmissionFenceId = sync->SubmissionFenceId;
            data.DmaCompleted.NodeOrdinal = sync->NodeOrdinal;
            data.DmaCompleted.EngineOrdinal = sync->EngineOrdinal;
            context->Core.DxgkInterface.DxgkCbNotifyInterrupt(
                context->Core.DxgkInterface.DeviceHandle, &data);
            if (sync->NodeOrdinal <= BC250_SUBMISSION_NODE_COMPUTE) {
                context->SubmissionEngine.WddmFences.LastNotifiedFenceIdByNode[sync->NodeOrdinal] =
                    sync->SubmissionFenceId;
            }
            context->SubmissionEngine.WddmFences.LastNotifiedFenceId =
                sync->SubmissionFenceId;
            sync->Notified = 1u;
            InterlockedExchange(&context->SdmaDpcNotifyPending, 1);
            return TRUE;
        }
        return FALSE;
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_QUERY) {
        return Bc250WddmFenceTrackerQueryCurrent(
            &context->SubmissionEngine.WddmFences,
            sync->NodeOrdinal,
            &sync->CurrentFence);
    }

    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_PENDING) {
        return Bc250WddmFenceTrackerSnapshotCounts(
            &context->SubmissionEngine.WddmFences,
            &sync->PendingCount,
            &sync->ReservedCount);
    }

    if (context->Core.DxgkInterface.DxgkCbNotifyInterrupt == NULL ||
        context->Core.DxgkInterface.DeviceHandle == NULL) {
        return FALSE;
    }

    count = Bc250WddmFenceTrackerDrain(
        &context->SubmissionEngine.WddmFences,
        sync->RetiredRingFence,
        completed,
        BC250_ACTIVE_FENCE_NOTIFY_BATCH);
    if (count == 0u) {
        return FALSE;
    }

    for (i = 0u; i < count; ++i) {
        if (completed[i].GpuvaPinValid != 0u) {
            BC250_GPUVA_SNAPSHOT_READER reader;
            RtlZeroMemory(&reader, sizeof(reader));
            if (completed[i].GpuvaSlot > 1u ||
                (uint32_t)InterlockedCompareExchange(
                    &context->GpuvaSnapshots.SlotGeneration[
                        completed[i].GpuvaSlot], 0, 0) !=
                    completed[i].GpuvaGeneration) {
                InterlockedExchange(&context->GpuvaMappingsOperational, 0);
                return FALSE;
            }
            reader.Space = &context->GpuvaSnapshots.Slots[completed[i].GpuvaSlot];
            reader.Slot = completed[i].GpuvaSlot;
            reader.PublicationGeneration = completed[i].GpuvaGeneration;
            reader.Acquired = 1u;
            Bc250GpuvaSnapshotRelease(&context->GpuvaSnapshots, &reader);
        }
        RtlZeroMemory(&data, sizeof(data));
        data.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
        data.DmaCompleted.SubmissionFenceId = completed[i].SubmissionFenceId;
        data.DmaCompleted.NodeOrdinal = completed[i].NodeOrdinal;
        data.DmaCompleted.EngineOrdinal = completed[i].EngineOrdinal;
        context->Core.DxgkInterface.DxgkCbNotifyInterrupt(
            context->Core.DxgkInterface.DeviceHandle, &data);
    }

    sync->Notified = count;
    return TRUE;
}

/*
 * ==========================================================================
 * Quiescencia do rastreador antes do teardown (candidato 3.67).
 *
 * O que o 3.66 fazia: chamava DxgkCbSynchronizeExecution para ler Count e
 * ReservedCount, e tratava qualquer falha dessa chamada como "ocupado".  O
 * ensaio fisico de 2026-09-01 com RenderNode=1 mediu exatamente isso —
 * SyncStatus=0xC0000001 (STATUS_UNSUCCESSFUL), SyncResult=0, Pending=0,
 * Reserved=0 — e o adaptador terminou em STATUS_DEVICE_BUSY sem jamais ter
 * tido uma fence.  Evidencia:
 * docs/evidence/2026-09-01_11-34-36-DESKTOP-HQAG6T4-rendernode-inspect.
 *
 * O que este candidato NAO faz: ler os contadores direto.  Nao esta provado
 * que a interrupcao ja foi desconectada nesse ponto do ciclo PnP, e mesmo que
 * estivesse ainda restariam o timer de retirement (armado em 1 ms), o DPC,
 * submissoes concorrentes e cancelamentos de reserva.  Trocar uma leitura
 * errada por outra leitura errada nao e' correcao.
 *
 * O que ele faz, nesta ordem:
 *   1. LifecycleState = STOPPING e portao de admissao do rastreador fechado —
 *      nenhuma submissao ou reserva nova entra a partir daqui.
 *   2. Timer de retirement parado.
 *   3. Espera as submissoes em voo sairem e drena os DPCs ja enfileirados.
 *   4. Fotografa Count e ReservedCount sob o lock proprio do rastreador.
 *
 * O criterio fail-closed NAO foi afrouxado.  Contagem diferente de zero
 * continua devolvendo STATUS_DEVICE_BUSY; falha ao adquirir o lock ou dreno
 * incompleto tambem.  O que mudou foi o mecanismo de consulta, que deixou de
 * depender exclusivamente de uma chamada que pode falhar por motivos alheios
 * ao rastreador.
 * ==========================================================================
 */

/* Dreno limitado: 400 x 5 ms = 2 s. Estourar e' falha, nao sucesso. */
#define BC250_ACTIVE_STOP_DRAIN_MAX_ITERATIONS 400u
#define BC250_ACTIVE_STOP_DRAIN_INTERVAL_100NS (-50000LL) /* 5 ms relativos */

static void Bc250ActiveStopDrainDelay(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    LARGE_INTEGER interval;
    interval.QuadPart = BC250_ACTIVE_STOP_DRAIN_INTERVAL_100NS;
    (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
#endif
}

static void Bc250ActiveStopFlushDpcs(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeFlushQueuedDpcs();
#endif
}

/*
 * Torna o rastreador quiescente e fotografa as contagens.
 *
 * Devolve STATUS_SUCCESS somente quando a fotografia e' confiavel; nesse caso
 * *PendingCount e *ReservedCount valem. Qualquer outro retorno significa
 * "nao sei", e o chamador deve falhar fechado.
 */
static NTSTATUS Bc250ActiveQuiesceAndSnapshotTracker(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ uint32_t StopReason,
    _Out_ uint32_t *PendingCount,
    _Out_ uint32_t *ReservedCount)
{
    BC250_WDDM_FENCE_TRACKER *tracker;
    uint32_t iterations = 0u;
    LONG inFlight;

    *PendingCount = 0u;
    *ReservedCount = 0u;
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    tracker = &Context->SubmissionEngine.WddmFences;

    /* 1. STOPPING: fecha a admissao de trabalho novo. */
    InterlockedExchange(&Context->LifecycleState,
        (LONG)BC250_ACTIVE_LIFECYCLE_STOPPING);
    Context->StopReason = StopReason;
    Bc250WddmFenceTrackerSetAcceptingWork(tracker, FALSE);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_REASON, StopReason);
    Bc250ProbeRecord(BC250_PROBE_V5_LIFECYCLE_STATE,
        BC250_ACTIVE_LIFECYCLE_STOPPING);

    /* 2. Primeira parada do timer (ja inclui KeFlushQueuedDpcs). */
    Bc250ActiveRetirementTimerStop(Context);
    Bc250ActiveSdmaRetirementTimerStop(Context);

    /*
     * 3. Espera as submissoes que passaram pelo portao antes do fechamento
     *    terminarem. Elas ainda podem comitar ou cancelar reserva, e por isso
     *    fotografar antes delas sairem produziria numero instavel.
     */
    for (;;) {
        inFlight = InterlockedCompareExchange(&Context->SubmitInFlight, 0, 0);
        if (inFlight <= 0) {
            break;
        }
        if (iterations >= BC250_ACTIVE_STOP_DRAIN_MAX_ITERATIONS) {
            break;
        }
        ++iterations;
        Bc250ActiveStopDrainDelay();
    }
    /*
     * Uma submissao que ja tinha passado pelo portao podia estar entre sua
     * ultima verificacao de RUNNING e o rearme do timer. Parar novamente
     * depois de SubmitInFlight chegar a zero fecha essa janela. A propria
     * submissao tambem deixa de rearmar ao observar STOPPING, mas a segunda
     * parada e' a barreira que torna a corrida inofensiva.
     */
    Bc250ActiveRetirementTimerStop(Context);
    Bc250ActiveSdmaRetirementTimerStop(Context);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_TIMER_DRAINED, 1u);
    /* Um DPC pode ter sido enfileirado durante a espera acima. */
    Bc250ActiveStopFlushDpcs();
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_DPC_DRAINED, 1u);

    inFlight = InterlockedCompareExchange(&Context->SubmitInFlight, 0, 0);
    Context->StopSubmitInFlightAfterDrain =
        (inFlight < 0) ? 0u : (uint32_t)inFlight;
    Context->StopDrainIterations = iterations;
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_SUBMIT_INFLIGHT,
        Context->StopSubmitInFlightAfterDrain);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_DRAIN_ITERATIONS, iterations);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_REJECTED_WHILE_STOPPING,
        tracker->RejectedWhileStopping);

    if (inFlight > 0) {
        /* Dreno nao concluiu: e' ocupado de verdade, nao ruido de consulta. */
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_SNAPSHOT_OK, 0u);
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_QUERY_MODE, 3u);
        return STATUS_DEVICE_BUSY;
    }

    /* 4. Fotografia sob o lock proprio do rastreador. */
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_QUERY_MODE, 3u);
    if (!Bc250WddmFenceTrackerSnapshotCounts(
            tracker, PendingCount, ReservedCount)) {
        ++Context->StopSnapshotFailures;
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_SNAPSHOT_OK, 0u);
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_LOCK_FAILURES,
            (ULONG)tracker->Lock.AcquireFailures);
        return STATUS_UNSUCCESSFUL;
    }
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_SNAPSHOT_OK, 1u);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_LOCK_FAILURES,
        (ULONG)tracker->Lock.AcquireFailures);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_PENDING_COUNT, *PendingCount);
    Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_RESERVED_COUNT, *ReservedCount);

    /*
     * A consulta serializada continua sendo feita, mas agora e' apenas
     * observacao: o veredito ja saiu da fotografia sob lock. Assim o proximo
     * ensaio ainda mostra se DxgkCbSynchronizeExecution funciona no
     * StopDevice, sem que a resposta dele decida o teardown.
     */
    if (Context->Core.DxgkInterface.DxgkCbSynchronizeExecution != NULL &&
        Context->Core.DxgkInterface.DeviceHandle != NULL) {
        BC250_ACTIVE_FENCE_SYNC sync;
        BOOLEAN queried = FALSE;
        NTSTATUS syncStatus;

        RtlZeroMemory(&sync, sizeof(sync));
        sync.Context = Context;
        sync.Operation = BC250_ACTIVE_FENCE_SYNC_PENDING;
        syncStatus = Context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
            Context->Core.DxgkInterface.DeviceHandle,
            Bc250ActiveSynchronizedFenceCallback, &sync, 0, &queried);
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_SYNC_STATUS,
            (ULONG)syncStatus);
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_SYNC_RESULT,
            queried ? 1u : 0u);
    } else {
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_SYNC_STATUS,
            (ULONG)STATUS_NOT_SUPPORTED);
        Bc250ProbeRecord(BC250_PROBE_V5_STOP_FENCE_SYNC_RESULT, 0u);
    }
    return STATUS_SUCCESS;
}

static void Bc250ActiveRetireAndNotify(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    BC250_ACTIVE_FENCE_SYNC sync;
    BOOLEAN notified = FALSE;
    uint32_t retired = 0u;
    NTSTATUS status;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return;
    }
    if (Context->Core.DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
        Context->Core.DxgkInterface.DeviceHandle == NULL) {
        return;
    }

    status = Bc250SubmissionEnginePoll(Context, &retired);
    if (!NT_SUCCESS(status)) {
        ++Context->RetirementPollFailures;
        return;
    }

    RtlZeroMemory(&sync, sizeof(sync));
    sync.Context = Context;
    sync.Operation = BC250_ACTIVE_FENCE_SYNC_DRAIN;
    sync.RetiredRingFence = Context->SubmissionEngine.ComputeRing.LastRetiredFence;

    status = Context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
        Context->Core.DxgkInterface.DeviceHandle,
        Bc250ActiveSynchronizedFenceCallback,
        &sync,
        0,
        &notified);
    if (!NT_SUCCESS(status) || !notified || sync.Notified == 0u) {
        return;
    }

    ++Context->RetirementNotifyBatches;
    Context->RetirementNotifiedTotal += sync.Notified;

    if (Context->Core.DxgkInterface.DxgkCbNotifyDpc != NULL) {
        Context->Core.DxgkInterface.DxgkCbNotifyDpc(
            Context->Core.DxgkInterface.DeviceHandle);
    }
}

static VOID Bc250ActiveRetirementDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)DeferredContext;
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    ++context->RetirementDpcRuns;
    Bc250ActiveRetireAndNotify(context);
}

void Bc250ActiveRetirementTimerStart(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    LARGE_INTEGER due;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    if (InterlockedCompareExchange(
            &Context->RetirementTimerInitialized, 1, 0) == 0) {
        KeInitializeTimer(&Context->RetirementTimer);
        KeInitializeDpc(&Context->RetirementDpc,
            Bc250ActiveRetirementDpcRoutine, Context);
    }
    if (InterlockedCompareExchange(
            &Context->RetirementTimerRunning, 1, 0) != 0) {
        return;
    }

    due.QuadPart = -10000LL; /* 1 ms */
    KeSetTimerEx(&Context->RetirementTimer, due, 1, &Context->RetirementDpc);
}

void Bc250ActiveRetirementTimerStop(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    if (Context == NULL ||
        InterlockedCompareExchange(
            &Context->RetirementTimerInitialized, 1, 1) != 1) {
        return;
    }
    InterlockedExchange(&Context->RetirementTimerRunning, 0);
    KeCancelTimer(&Context->RetirementTimer);
    KeFlushQueuedDpcs();
}

static VOID Bc250ActiveSdmaRetirementEnsureScheduled(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return;
    }
    if (g_Bc250SdmaPresent.InFlightCount > 0u) {
        if (InterlockedExchange(&Context->SdmaRetirementTimerRunning, 1) == 0) {
            LARGE_INTEGER due;
            due.QuadPart = -10000LL; /* 1 ms */
            KeSetTimerEx(&Context->SdmaRetirementTimer, due, 0, &Context->SdmaRetirementDpc);
        }
    }
}

static VOID Bc250ActiveSdmaRetireAndNotify(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ BOOLEAN FromDpc)
{
    BC250_SDMA_RETIREMENT_RESULT result;
    BOOLEAN polled;
    BC250_ACTIVE_FENCE_SYNC sync;
    BOOLEAN notified = FALSE;
    NTSTATUS status;
    uint32_t highestSubmissionFenceId = 0u;
    uint32_t batchCount = 0u;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    if (InterlockedCompareExchange(&Context->IsActive, 1, 1) != 1) {
        return;
    }
    if (Context->Core.DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
        Context->Core.DxgkInterface.DeviceHandle == NULL) {
        return;
    }

    /*
     * Guarda atomica de serializacao (Q3):
     * Impede que a thread de submit e a DPC executem a rotina de retirement
     * em paralelo em nucleos diferentes, eliminando colisoes e reentrancias.
     *
     * INVARIANTE DE LIVENESS ANTI-0x133:
     * O loser da guarda NUNCA re-enfileira DPC imediatamente se veio de DPC.
     * Re-enfileirar KeInsertQueueDpc dentro de uma DPC em execucao causa loop
     * infinito de DPC em DISPATCH_LEVEL sem retorno a PASSIVE_LEVEL, estourando
     * o limite de ticks do DPC watchdog (BugCheck 0x133 Arg1=1).
     * O liveness e' garantido exclusivamente pelo timer de 1 ms via
     * Bc250ActiveSdmaRetirementEnsureScheduled, que desacopla temporalmente e
     * permite a CPU baixar o IRQL para PASSIVE_LEVEL entre as tentativas.
     */
    if (InterlockedCompareExchange(&Context->SdmaRetireBusy, 1, 0) != 0) {
        ++Context->SdmaRetirementBusyYields;
        Bc250DisplayLog("[SdmaRetire] BUSY_YIELD #%u: reagendando via timer (FromDpc=%u InFlight=%u)\n",
            Context->SdmaRetirementBusyYields, FromDpc ? 1u : 0u,
            g_Bc250SdmaPresent.InFlightCount);
        Bc250ActiveSdmaRetirementEnsureScheduled(Context);
        return;
    }

    BOOLEAN notifiedAny = FALSE;

    while ((polled = Bc250SdmaPresentPollRetirement(&result)) != FALSE) {
        if (result.Completed) {
            uint32_t fenceId = (uint32_t)result.SubmissionFenceId;
            if (fenceId > highestSubmissionFenceId) {
                highestSubmissionFenceId = fenceId;
            }
            batchCount++;

            BC250_SUBMIT_EXEC_PATH execPath = (BC250_SUBMIT_EXEC_PATH)result.ExecPath;
            if (Bc250SubmitPathExecutesDmaBuffer(execPath) != 0u) {
                Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB EXECUTADO (INDIRECT): a GPU leu o DMA buffer de %u bytes em VA=0x%llX; FenceId=%llu (batch #%u)\n",
                    result.DmaBufferSize,
                    (unsigned long long)result.DmaBufferVirtualAddress,
                    (unsigned long long)result.SubmissionFenceId,
                    batchCount);
            } else {
                Bc250DisplayLog("[SubmitVirtual Node0 SDMA] PLANO A CONCLUIDO (%s): operacao REIMPLEMENTADA no anel; o DMA buffer de %u bytes em VA=0x%llX NAO foi lido pela GPU. FenceId=%llu (batch #%u)\n",
                    Bc250SubmitPathName(execPath),
                    result.DmaBufferSize,
                    (unsigned long long)result.DmaBufferVirtualAddress,
                    (unsigned long long)result.SubmissionFenceId,
                    batchCount);
            }
            if (!result.TripodPassed) {
                Bc250DisplayLog("[SdmaRetire] Submissao #%llu concluida, mas telemetria do tripe NAO casou (canaries intactos ou RPTR avancado)\n",
                    (unsigned long long)result.SubmissionFenceId);
            }
        } else if (result.Timeout) {
            Bc250DisplayLog("[SdmaRetire] Submissao #%llu TIMEOUT no silicio!\n",
                (unsigned long long)result.SubmissionFenceId);
            Bc250ActiveSdmaRetirementEnsureScheduled(Context);
            break;
        } else {
            Bc250ActiveSdmaRetirementEnsureScheduled(Context);
            break;
        }
    }

    /*
     * Notificacao canonica em batch (Q2):
     * No WDDM, reportar DMA_COMPLETED com a fence N sinaliza que todos os
     * pacotes ate N naquele no completaram. Notificamos uma unica vez com a maior
     * fence do lote apos consumir todos os slots concluidos da FIFO.
     */
    if (highestSubmissionFenceId > 0u) {
        RtlZeroMemory(&sync, sizeof(sync));
        sync.Context = Context;
        sync.Operation = BC250_ACTIVE_FENCE_SYNC_NOTIFY_SDMA;
        sync.SubmissionFenceId = highestSubmissionFenceId;
        sync.NodeOrdinal = 0; /* Node 0 SDMA */
        sync.EngineOrdinal = 0;

        status = Context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
            Context->Core.DxgkInterface.DeviceHandle,
            Bc250ActiveSynchronizedFenceCallback,
            &sync,
            0,
            &notified);
        if (NT_SUCCESS(status) && notified && sync.Notified != 0u) {
            Context->SdmaRetirementNotifiedTotal += batchCount;
            Context->SdmaRetirementBatchesNotifiedTotal++;
            notifiedAny = TRUE;
            InterlockedExchange(&Context->SdmaDpcNotifyPending, 1);
            Bc250DisplayLog("[SdmaRetire] LOTE CONCLUIDO: %u submissoes aposentadas; DMA_COMPLETED notificado com a maior FenceId=%u\n",
                batchCount, highestSubmissionFenceId);
        } else {
            Context->SdmaRetirementNotifyFailedTotal += batchCount;
            Bc250DisplayLog("[SdmaRetire] FALHA NA NOTIFICACAO DO LOTE: MaiorFenceId=%u (%u submissoes) status=0x%08X notified=%u syncNotified=%u\n",
                highestSubmissionFenceId, batchCount, status, notified ? 1 : 0, sync.Notified);
        }
    }

    if (FromDpc) {
        if (InterlockedExchange(&Context->SdmaDpcNotifyPending, 0) != 0) {
            if (Context->Core.DxgkInterface.DxgkCbNotifyDpc != NULL) {
                Context->Core.DxgkInterface.DxgkCbNotifyDpc(
                    Context->Core.DxgkInterface.DeviceHandle);
            }
        }
    } else {
        /*
         * Em SubmitCommandVirtual (FromDpc == FALSE), o scheduler (VidSch)
         * ja esta na pilha segurando seus locks internos. Chamar DxgkCbNotifyDpc
         * aqui causa reentrancia recursiva no spinlock em fila (BugCheck 0x133).
         * Agendamos o DPC para que ele notifique o scheduler logo apos sair.
         */
        if (InterlockedCompareExchange(&Context->SdmaDpcNotifyPending, 0, 0) != 0) {
            KeInsertQueueDpc(&Context->SdmaRetirementDpc, NULL, NULL);
        }
    }

    InterlockedExchange(&Context->SdmaRetireBusy, 0);
    if (g_Bc250SdmaPresent.InFlightCount > 0u) {
        Bc250ActiveSdmaRetirementEnsureScheduled(Context);
    }
}

static VOID Bc250ActiveSdmaRetirementDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)DeferredContext;
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    InterlockedExchange(&context->SdmaRetirementTimerRunning, 0);
    ++context->SdmaRetirementDpcRuns;
    Bc250ActiveSdmaRetireAndNotify(context, TRUE);
    if (InterlockedExchange(&context->SdmaDpcNotifyPending, 0) != 0) {
        if (context->Core.DxgkInterface.DxgkCbNotifyDpc != NULL) {
            context->Core.DxgkInterface.DxgkCbNotifyDpc(
                context->Core.DxgkInterface.DeviceHandle);
        }
    }
    Bc250ActiveSdmaRetirementEnsureScheduled(context);
}

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
VOID Bc250ActiveSdmaRetirePendingOnAllAdapters(
    _In_ BOOLEAN FromDpc)
{
    uint32_t index;
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        BC250_DXGK_ACTIVE_CONTEXT *context = g_Bc250AdapterDirectory[index];
        if (context != NULL &&
            context->Signature == BC250_ACTIVE_SIGNATURE &&
            InterlockedCompareExchange(&context->IsActive, 1, 1) == 1) {
            Bc250ActiveSdmaRetireAndNotify(context, FromDpc);
        }
    }
}
#endif

void Bc250ActiveSdmaRetirementTimerStart(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    LARGE_INTEGER due;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }
    if (InterlockedCompareExchange(
            &Context->SdmaRetirementTimerInitialized, 1, 0) == 0) {
        KeInitializeTimer(&Context->SdmaRetirementTimer);
        KeInitializeDpc(&Context->SdmaRetirementDpc,
            Bc250ActiveSdmaRetirementDpcRoutine, Context);
    }
    if (InterlockedCompareExchange(
            &Context->SdmaRetirementTimerRunning, 1, 0) != 0) {
        return;
    }

    due.QuadPart = -10000LL; /* 1 ms */
    KeSetTimerEx(&Context->SdmaRetirementTimer, due, 0, &Context->SdmaRetirementDpc);
}

void Bc250ActiveSdmaRetirementTimerStop(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    if (Context == NULL ||
        InterlockedCompareExchange(
            &Context->SdmaRetirementTimerInitialized, 1, 1) != 1) {
        return;
    }
    InterlockedExchange(&Context->SdmaRetirementTimerRunning, 0);
    KeCancelTimer(&Context->SdmaRetirementTimer);
    KeFlushQueuedDpcs();
}

/*
 * ==========================================================================
 * Caminhada de page table pela CPU antes de tocar o silicio (candidato 3.82).
 *
 * Motivacao medida na corrida de 04/09 23:23: com IB_SIZE=15 a SDMA0 parou de
 * travar (RPTR alcancou WPTR=132), mas a fence do anel continuou no poison e o
 * UTCL1_DELTA acusou atividade de traducao. Isso e consistente com o fetch do
 * IB faultando sob VMID8 e a engine drenando o anel sem executar. Antes de
 * apostar outra corrida na engine, resolvemos a GPUVA aqui na CPU, lendo as
 * MESMAS estruturas que a GPU vai ler.
 *
 * Geometria (bc250_gpuva_space_contract.h, coerente com GPUMMUCAPS que
 * publicamos e com DXGKQAITYPE_PAGETABLELEVELDESC):
 *   VA[33:21] -> indice na root  (13 bits, 8192 entradas, tabela de 64 KiB)
 *   VA[20:12] -> indice na folha ( 9 bits,  512 entradas, tabela de  4 KiB)
 *   VA[11:0]  -> offset na pagina
 *
 * As leituras usam a janela de VRAM ja mapeada para o framebuffer. Fora dela
 * a caminhada NAO adivinha: reporta o limite e falha fechada.
 * ==========================================================================
 */

#define BC250_ACTIVE_PT_ENTRY_BYTES 8ull

static BOOLEAN Bc250ActiveReadVramU64(
    _In_ uint64_t VramOffset,
    _Out_ uint64_t *Value)
{
    *Value = 0ull;
    if (g_Bc250DisplayState.FrameBufferPhysical.QuadPart == 0) {
        return FALSE;
    }

    /* Leitura direta rapida se o offset estiver dentro da janela permanente do Framebuffer */
    if (g_Bc250DisplayState.FrameBufferVirtual != NULL &&
        (VramOffset + BC250_ACTIVE_PT_ENTRY_BYTES) <= (uint64_t)g_Bc250DisplayState.FrameBufferSize) {
        *Value = *(CONST volatile uint64_t *)
            ((CONST volatile UCHAR *)g_Bc250DisplayState.FrameBufferVirtual +
             VramOffset);
        return TRUE;
    }

    /*
     * Dupla verificacao e extensao segura:
     * Para offsets alem do framebuffer estatico (ex: Page Tables do DWM alocadas
     * pelo VidMm no topo da memoria local a ~511 MB), mapeia a pagina de 4 KB
     * correspondente sob demanda para leitura fisica segura.
     */
    {
        PHYSICAL_ADDRESS entryPhys;
        PHYSICAL_ADDRESS pagePhys;
        uint32_t pageOffset;
        PVOID mappedPage;

        entryPhys.QuadPart = g_Bc250DisplayState.FrameBufferPhysical.QuadPart + VramOffset;
        pagePhys.QuadPart = entryPhys.QuadPart & ~0xFFFull;
        pageOffset = (uint32_t)(entryPhys.QuadPart & 0xFFFull);

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
        mappedPage = MmMapIoSpace(pagePhys, 4096u, MmNonCached);
        if (mappedPage != NULL) {
            *Value = *(CONST volatile uint64_t *)((CONST volatile UCHAR *)mappedPage + pageOffset);
            MmUnmapIoSpace(mappedPage, 4096u);
            Bc250DisplayLog("[PT_READ] VRAM U64 sob demanda: VramOff=0x%llX Phys=0x%llX Val=0x%016llX\n",
                (unsigned long long)VramOffset, (unsigned long long)entryPhys.QuadPart, (unsigned long long)*Value);
            return TRUE;
        }
#else
        UNREFERENCED_PARAMETER(entryPhys);
        UNREFERENCED_PARAMETER(pagePhys);
        UNREFERENCED_PARAMETER(pageOffset);
        UNREFERENCED_PARAMETER(mappedPage);
#endif
    }
    return FALSE;
}

/*
 * Converte endereco MC para offset dentro da janela de VRAM mapeada.
 * Devolve FALSE quando o MC esta abaixo da base -- sinal de que a base usada
 * para programar a GCVM e a base usada para enderecar VRAM divergem.
 */
static BOOLEAN Bc250ActiveMcToVramOffset(
    _In_ uint64_t McAddress,
    _In_ uint64_t VramMcBase,
    _Out_ uint64_t *VramOffset)
{
    *VramOffset = 0ull;
    if (McAddress < VramMcBase) {
        return FALSE;
    }
    *VramOffset = McAddress - VramMcBase;
    return TRUE;
}

/*
 * Duas interpretacoes concorrentes para o campo de endereco de uma entrada,
 * e uma corrida so para decidir entre elas:
 *
 *   MC   -- o valor ja inclui a base de VRAM em espaco MC. E o que o upstream
 *           faz (amdgpu soma vram_base_offset ao montar PTEs de VRAM) e o que
 *           a instalacao da root passa para a GCVM.
 *   RAW  -- o valor e um offset cru dentro da VRAM. E o que
 *           Bc250McdmEncodeVidMmPteWithLevel produz hoje:
 *           LocalSegmentBaseOffset + pageOffsetInSegment, sem base MC.
 *
 * Se as duas divergirem, a root e as folhas estao em espacos de endereco
 * diferentes -- e a GPU, que so conhece a interpretacao MC, caminha memoria
 * errada. Reportamos qual das duas leu uma entrada plausivel.
 */
static BOOLEAN Bc250ActiveEntryToVramOffset(
    _In_ const char *Label,
    _In_ const char *Level,
    _In_ uint64_t EntryAddress,
    _In_ uint64_t VramMcBase,
    _Out_ uint64_t *VramOffset,
    _Out_ const char **Interpretation)
{
    *Interpretation = "NENHUMA";
    if (Bc250ActiveMcToVramOffset(EntryAddress, VramMcBase, VramOffset)) {
        *Interpretation = "MC";
        return TRUE;
    }
    if (EntryAddress != 0ull) {
        /*
         * Interpretacao RAW existe apenas para NOMEAR o defeito. Ela nao
         * autoriza submissao: a GPU so caminha em MC, e na corrida de 05/09
         * 00:55 tratar RAW como sucesso deixou a submissao seguir, travar a
         * engine e custar um reboot. Agora e diagnostico e recusa.
         */
        *VramOffset = EntryAddress;
        *Interpretation = "RAW";
        Bc250DisplayLog("[PT_WALK %s] %s: endereco 0x%llX esta ABAIXO da base MC 0x%llX -- entrada em espaco CRU, a GPU nao caminha isto\n",
            Label, Level, (unsigned long long)EntryAddress,
            (unsigned long long)VramMcBase);
        return FALSE;
    }
    *VramOffset = 0ull;
    return FALSE;
}

/*
 * Resolve GpuVa lendo root -> PDE -> PTE. Devolve TRUE somente quando as duas
 * entradas estao validas e ambas as leituras couberam na janela mapeada.
 * Toda etapa e logada: uma falha aqui nomeia o nivel exato que quebrou.
 */
static BOOLEAN Bc250ActiveWalkGpuVa(
    _In_ const char *Label,
    _In_ uint64_t GpuVa,
    _In_ uint64_t RootMcAddress,
    _In_ uint64_t VramMcBase,
    _Out_ uint64_t *ResolvedPhysical)
{
    uint64_t rootIndex;
    uint64_t leafIndex;
    uint64_t pdeVramOffset;
    uint64_t pteVramOffset;
    uint64_t pde = 0ull;
    uint64_t pte = 0ull;
    uint64_t leafMc = 0ull;
    uint64_t pageMc = 0ull;
    uint32_t pdeFlags = 0u;
    uint32_t pteFlags = 0u;
    uint32_t pdeReserved = 0u;
    const char *rootInterp = "NENHUMA";
    const char *leafInterp = "NENHUMA";

    *ResolvedPhysical = 0ull;

    if (GpuVa >= (1ull << BC250_GPUVA_PT_VA_BITS)) {
        Bc250DisplayLog("[PT_WALK %s] RECUSA: VA=0x%llX excede %u bits\n",
            Label, (unsigned long long)GpuVa, BC250_GPUVA_PT_VA_BITS);
        return FALSE;
    }

    rootIndex = (GpuVa >> (12u + BC250_GPUVA_PT_LEAF_INDEX_BITS)) &
        ((1ull << BC250_GPUVA_PT_ROOT_INDEX_BITS) - 1ull);
    leafIndex = (GpuVa >> 12u) &
        ((1ull << BC250_GPUVA_PT_LEAF_INDEX_BITS) - 1ull);

    if (!Bc250ActiveEntryToVramOffset(Label, "L1 root",
            RootMcAddress + rootIndex * BC250_ACTIVE_PT_ENTRY_BYTES,
            VramMcBase, &pdeVramOffset, &rootInterp)) {
        Bc250DisplayLog("[PT_WALK %s] FALHA L1: root MC=0x%llX nao mapeia (base VRAM MC=0x%llX)\n",
            Label, (unsigned long long)RootMcAddress,
            (unsigned long long)VramMcBase);
        return FALSE;
    }
    if (!Bc250ActiveReadVramU64(pdeVramOffset, &pde)) {
        Bc250DisplayLog("[PT_WALK %s] FALHA L1: Nao foi possivel ler PDE em VRAM+0x%llX (acesso fisico falhou)\n",
            Label, (unsigned long long)pdeVramOffset);
        return FALSE;
    }
    /*
     * DECODIFICAR COMO PDE, nao como PTE. Bc250GcvmDecodePte mascara com
     * 4 KiB e escondia os bits [11:0] -- foi assim que seis corridas seguidas
     * viram um PDE corrompido (0x61) como se fosse limpo.
     */
    (void)Bc250GcvmDecodePde(pde, &leafMc, &pdeFlags, &pdeReserved);
    Bc250DisplayLog("[PT_WALK %s] L1 idx=%llu VRAM+0x%llX PDE=%016llX -> leafPA=0x%llX flags=0x%X valid=%u reservado[5:3]=0x%X\n",
        Label, (unsigned long long)rootIndex,
        (unsigned long long)pdeVramOffset, (unsigned long long)pde,
        (unsigned long long)leafMc, pdeFlags,
        (pdeFlags & BC250_PTE_VALID) ? 1u : 0u, pdeReserved);
    if ((pdeFlags & BC250_PTE_VALID) == 0u) {
        return FALSE;
    }
    if (pdeReserved != 0u) {
        Bc250DisplayLog("[PT_WALK %s] FALHA L1: PDE com bits reservados [5:3] ligados; a GPU caminharia endereco deslocado\n",
            Label);
        return FALSE;
    }

    if (!Bc250ActiveEntryToVramOffset(Label, "L0 folha",
            leafMc + leafIndex * BC250_ACTIVE_PT_ENTRY_BYTES,
            VramMcBase, &pteVramOffset, &leafInterp)) {
        Bc250DisplayLog("[PT_WALK %s] FALHA L0: folha MC=0x%llX nao mapeia (base VRAM MC=0x%llX)\n",
            Label, (unsigned long long)leafMc,
            (unsigned long long)VramMcBase);
        return FALSE;
    }
    if (!Bc250ActiveReadVramU64(pteVramOffset, &pte)) {
        Bc250DisplayLog("[PT_WALK %s] FALHA L0: Nao foi possivel ler PTE em VRAM+0x%llX (acesso fisico falhou)\n",
            Label, (unsigned long long)pteVramOffset);
        return FALSE;
    }
    (void)Bc250GcvmDecodePte(pte, &pageMc, &pteFlags);
    Bc250DisplayLog("[PT_WALK %s] L0 idx=%llu VRAM+0x%llX PTE=%016llX -> pageMC=0x%llX flags=0x%X valid=%u\n",
        Label, (unsigned long long)leafIndex,
        (unsigned long long)pteVramOffset, (unsigned long long)pte,
        (unsigned long long)pageMc, pteFlags,
        (pteFlags & BC250_PTE_VALID) ? 1u : 0u);
    if ((pteFlags & BC250_PTE_VALID) == 0u) {
        return FALSE;
    }

    *ResolvedPhysical = pageMc + (GpuVa & 0xFFFull);
    Bc250DisplayLog("[PT_WALK %s] RESOLVIDO: VA=0x%llX -> 0x%llX (root=%s, folha=%s)\n",
        Label, (unsigned long long)GpuVa,
        (unsigned long long)*ResolvedPhysical, rootInterp, leafInterp);
    /*
     * Divergencia de interpretacao entre os niveis e o achado, nao um detalhe:
     * a GPU so sabe caminhar em espaco MC. Se a folha so fecha como offset
     * cru, a tabela que a CPU escreve nao e a tabela que a GPU consegue ler.
     */
    if (rootInterp[0] != leafInterp[0]) {
        Bc250DisplayLog("[PT_WALK %s] DIVERGENCIA DE ESPACO DE ENDERECO: root=%s folha=%s -- a GPU nao caminha isto\n",
            Label, rootInterp, leafInterp);
    }
    return TRUE;
}

/*
 * C3.16 (12/09) -- caminhada silenciosa e cobertura INTEGRAL.
 *
 * O gate anterior conferia somente primeira/meio/ultima pagina. A corrida
 * 18:45 terminou com um TransferVirtual de 3.072.000 bytes: os tres probes
 * passaram, o submit #8896 foi publicado e nunca apareceu a fence. Uma faixa
 * de 750 paginas nao pode ser declarada segura por tres amostras.
 *
 * A page table e a fonte fisica que a GPU vai ler. Percorremos cada pagina,
 * conferindo validade e permissao antes de tocar o SDMA. O caminho normal e'
 * silencioso; numa falha, o walker verboso e' chamado apenas para a pagina
 * exata que reprovou.
 */
static BOOLEAN Bc250ActiveWalkGpuVaQuiet(
    _In_ uint64_t GpuVa,
    _In_ uint64_t RootMcAddress,
    _In_ uint64_t VramMcBase,
    _In_ uint32_t RequiredPermissions,
    _Out_ uint64_t *ResolvedPhysical)
{
    uint64_t rootIndex;
    uint64_t leafIndex;
    uint64_t pdeVramOffset;
    uint64_t pteVramOffset;
    uint64_t pde = 0ull;
    uint64_t pte = 0ull;
    uint64_t leafMc = 0ull;
    uint64_t pageMc = 0ull;
    uint32_t pdeFlags = 0u;
    uint32_t pteFlags = 0u;
    uint32_t pdeReserved = 0u;

    if (ResolvedPhysical == NULL) return FALSE;
    *ResolvedPhysical = 0ull;
    if (GpuVa >= BC250_GPUVA_TRANSLATABLE_LIMIT) return FALSE;

    rootIndex = (GpuVa >> (12u + BC250_GPUVA_PT_LEAF_INDEX_BITS)) &
        ((1ull << BC250_GPUVA_PT_ROOT_INDEX_BITS) - 1ull);
    leafIndex = (GpuVa >> 12u) &
        ((1ull << BC250_GPUVA_PT_LEAF_INDEX_BITS) - 1ull);

    if (!Bc250ActiveMcToVramOffset(
            RootMcAddress + rootIndex * BC250_ACTIVE_PT_ENTRY_BYTES,
            VramMcBase, &pdeVramOffset) ||
        !Bc250ActiveReadVramU64(pdeVramOffset, &pde)) {
        return FALSE;
    }
    (void)Bc250GcvmDecodePde(pde, &leafMc, &pdeFlags, &pdeReserved);
    if ((pdeFlags & BC250_PTE_VALID) == 0u || pdeReserved != 0u) {
        return FALSE;
    }

    if (!Bc250ActiveMcToVramOffset(
            leafMc + leafIndex * BC250_ACTIVE_PT_ENTRY_BYTES,
            VramMcBase, &pteVramOffset) ||
        !Bc250ActiveReadVramU64(pteVramOffset, &pte)) {
        return FALSE;
    }
    (void)Bc250GcvmDecodePte(pte, &pageMc, &pteFlags);
    if ((pteFlags & BC250_PTE_VALID) == 0u ||
        (pteFlags & RequiredPermissions) != RequiredPermissions) {
        return FALSE;
    }

    *ResolvedPhysical = pageMc + (GpuVa & BC250_GPUVA_PAGE_MASK);
    return TRUE;
}

static BOOLEAN Bc250ActiveValidateGpuVaRange(
    _In_ const char *Label,
    _In_ uint64_t BaseGpuVa,
    _In_ uint64_t ByteCount,
    _In_ uint64_t RootMcAddress,
    _In_ uint64_t VramMcBase,
    _In_ uint32_t RequiredPermissions,
    _Out_opt_ uint64_t *FirstResolvedPhysical)
{
    uint64_t endGpuVa;
    uint64_t pageGpuVa;
    uint64_t lastPageGpuVa;
    uint64_t resolved = 0ull;
    uint64_t pages = 0ull;

    if (FirstResolvedPhysical != NULL) *FirstResolvedPhysical = 0ull;
    if (ByteCount == 0ull || BaseGpuVa > (uint64_t)~0ull - ByteCount) {
        Bc250DisplayLog("[PT_RANGE %s] RECUSA: intervalo invalido VA=0x%llX Bytes=%llu\n",
            Label, (unsigned long long)BaseGpuVa,
            (unsigned long long)ByteCount);
        return FALSE;
    }
    endGpuVa = BaseGpuVa + ByteCount;
    if (endGpuVa > BC250_GPUVA_TRANSLATABLE_LIMIT) {
        Bc250DisplayLog("[PT_RANGE %s] RECUSA: fim 0x%llX excede limite traduzivel 0x%llX\n",
            Label, (unsigned long long)endGpuVa,
            (unsigned long long)BC250_GPUVA_TRANSLATABLE_LIMIT);
        return FALSE;
    }

    pageGpuVa = BaseGpuVa & ~BC250_GPUVA_PAGE_MASK;
    lastPageGpuVa = (endGpuVa - 1ull) & ~BC250_GPUVA_PAGE_MASK;
    for (;;) {
        if (!Bc250ActiveWalkGpuVaQuiet(pageGpuVa, RootMcAddress,
                VramMcBase, RequiredPermissions, &resolved)) {
            uint64_t verboseResolved = 0ull;
            (void)Bc250ActiveWalkGpuVa(Label, pageGpuVa, RootMcAddress,
                VramMcBase, &verboseResolved);
            Bc250DisplayLog("[PT_RANGE %s] FALHA: pagina=%llu VA=0x%llX Perm=0x%X; SDMA NAO SERA TOCADO\n",
                Label, (unsigned long long)pages,
                (unsigned long long)pageGpuVa, RequiredPermissions);
            return FALSE;
        }
        if (pages == 0ull && FirstResolvedPhysical != NULL) {
            *FirstResolvedPhysical = resolved +
                (BaseGpuVa & BC250_GPUVA_PAGE_MASK);
        }
        ++pages;
        if (pageGpuVa == lastPageGpuVa) break;
        pageGpuVa += BC250_GPUVA_PAGE_BYTES;
    }

    Bc250DisplayLog("[PT_RANGE %s] PASS: VA=[0x%llX,0x%llX) Pages=%llu Perm=0x%X\n",
        Label, (unsigned long long)BaseGpuVa,
        (unsigned long long)endGpuVa,
        (unsigned long long)pages, RequiredPermissions);
    return TRUE;
}

/* Root installation and MEC publication (called at DISPATCH_LEVEL). Synchronized via SubmissionEngine.Lock. */
static NTSTATUS Bc250ActivePrepareComputeVmid(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    const BC250_CONTEXT_SUBMISSION_VIEW *View)
{
    BC250_MCDM_OBJECT_RECORD record;
    BC250_MCDM_RUNTIME_CAPS caps;
    BC250_GCVM_VMID8_INTERFACE io;
    BC250_GCVM_VMID8_STATUS result;
    uint64_t root, submitted, retired;
    uint32_t retiredNow = 0u;
    uint64_t rootOffset = 0ull;
    KIRQL irql;
    NTSTATUS status;

    if (Context == NULL || View == NULL) {
        Bc250DisplayLog("[SubmitVirtual Node1] RECUSA PrepareComputeVmid: Context ou View nulo\n");
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&record, sizeof(record));
    RtlZeroMemory(&caps, sizeof(caps));

    /* View ja consolida a raiz aprovada em Step 2 */
    if (View->RootPageTableSegmentId == 1u && View->RootPageTableEntries > 0u) {
        rootOffset = View->RootPageTableAddress;
    } else {
        /* Fallback defensivo no registro de objetos */
        if (Bc250McdmObjectLookup(&Context->Objects, View->ContextHandle,
                BC250_MCDM_OBJECT_CONTEXT, NULL, &record) == BC250_MCDM_OBJECT_OK &&
            record.RootPageTableSetCount > 0u && record.RootPageTableSegmentId == 1u) {
            rootOffset = record.RootPageTableAddress;
        } else if (Bc250McdmObjectLookup(&Context->Objects, View->OwnerProcess,
                BC250_MCDM_OBJECT_PROCESS, NULL, &record) == BC250_MCDM_OBJECT_OK &&
            record.RootPageTableSetCount > 0u && record.RootPageTableSegmentId == 1u) {
            rootOffset = record.RootPageTableAddress;
        } else {
            Bc250DisplayLog("[SubmitVirtual Node1] RECUSA PrepareComputeVmid: raiz nao localizada (ViewSeg=%u ViewEntries=%u Ctx=%p Proc=%p)\n",
                View->RootPageTableSegmentId, View->RootPageTableEntries,
                View->ContextHandle, View->OwnerProcess);
            return STATUS_DEVICE_NOT_READY;
        }
    }

    status = Bc250ActiveQueryRuntimeCaps(Context, &caps);
    if (!NT_SUCCESS(status) || !caps.Ready) {
        Bc250DisplayLog("[SubmitVirtual Node1] RECUSA PrepareComputeVmid: RuntimeCaps falhou (status=0x%08X Ready=%u)\n",
            (uint32_t)status, caps.Ready);
        return STATUS_DEVICE_NOT_READY;
    }

    root = caps.VramMcBase + caps.LocalSegmentBaseOffset + rootOffset;
    KeAcquireSpinLock(&Context->SubmissionEngine.Lock, &irql);
    submitted = Context->SubmissionEngine.ComputeRing.LastSubmittedFence;
    retired = Context->SubmissionEngine.ComputeRing.LastRetiredFence;
    KeReleaseSpinLock(&Context->SubmissionEngine.Lock, irql);

    Bc250DisplayLog("[SubmitVirtual Node1] PrepareComputeVmid: Root=0x%llX Sub=%llu Ret=%llu InFlight=%llu\n",
        (unsigned long long)root, (unsigned long long)submitted, (unsigned long long)retired,
        (unsigned long long)(submitted > retired ? submitted - retired : 0ull));

    if (submitted != retired) {
        status = Bc250SubmissionEnginePoll(Context, &retiredNow);
        KeAcquireSpinLock(&Context->SubmissionEngine.Lock, &irql);
        submitted = Context->SubmissionEngine.ComputeRing.LastSubmittedFence;
        retired = Context->SubmissionEngine.ComputeRing.LastRetiredFence;
        KeReleaseSpinLock(&Context->SubmissionEngine.Lock, irql);
        Bc250DisplayLog("[SubmitVirtual Node1] PrepareComputeVmid: PosPoll Sub=%llu Ret=%llu RetiredNow=%u status=0x%08X\n",
            (unsigned long long)submitted, (unsigned long long)retired, retiredNow, (uint32_t)status);
        if (submitted != retired) {
            Bc250DisplayLog("[SubmitVirtual Node1] RECUSA PrepareComputeVmid: anel compute ocupado (Sub=%llu Ret=%llu)\n",
                (unsigned long long)submitted, (unsigned long long)retired);
            return STATUS_GRAPHICS_ALLOCATION_BUSY;
        }
    }

    io.Read32 = Bc250ActiveVmidRead32;
    io.Write32 = Bc250ActiveVmidWrite32;
    io.Flush = Bc250ActiveFlushComputeTlb;
    io.Context = Context;
    io.Identity = BC250_GCVM_VMID8_IDENTITY;
    Bc250SubmissionEngineCaptureRciuStage(Context, "BEFORE_GCVM_INSTALL");
    result = Bc250GcvmComputeInstall(root, submitted, retired, &io, &Context->ComputeVmidBinding);
    Bc250DisplayLog("[SubmitVirtual Node1] VMID9_BIND: Root=0x%llX Submitted=%llu Retired=%llu Status=%u Ready=%u\n",
        (unsigned long long)root, (unsigned long long)submitted,
        (unsigned long long)retired, (uint32_t)result, Context->ComputeVmidBinding.Ready);
    if (result != BC250_GCVM_VMID8_OK) {
        Bc250DisplayLog("[SubmitVirtual Node1] RECUSA PrepareComputeVmid: GcvmComputeInstall falhou (result=%u)\n",
            (uint32_t)result);
        return result == BC250_GCVM_VMID8_CONTEXT_ENABLED ? STATUS_GRAPHICS_ALLOCATION_BUSY : STATUS_DEVICE_NOT_READY;
    }
    Bc250SubmissionEngineCaptureRciuStage(Context, "AFTER_GCVM_INSTALL");

    /*
     * CP_HQD_VMID pertence ao estado carregado da HQD. As 17 submissoes de
     * autoteste usam IB_VMID=0; o primeiro trabalho WDDM precisa de IB_VMID=9.
     * Ate o 3.116 essa troca era feita com ACTIVE=1, uma sequencia que o load
     * upstream nao sustenta e que antecedeu o WAIT_ON_RCIU_READ observado.
     * A transacao abaixo exige fila drenada e faz off -> VMID -> queue mask ->
     * on, com readback e sem jamais escrever RPTR.
     */
    status = Bc250SubmissionEngineRetargetDrainedIbVmid(
        Context, BC250_GCVM_COMPUTE_VMID);
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[SubmitVirtual Node1] RECUSA PrepareComputeVmid: retarget drenado do IB_VMID falhou (status=0x%08X)\n",
            (uint32_t)status);
        return status;
    }

    KeAcquireSpinLock(&Context->SubmissionEngine.Lock, &irql);
    uint32_t targetHqdVmid = ((uint32_t)BC250_GCVM_COMPUTE_VMID << 8);
    uint32_t readbackHqdVmid = 0u;
    uint32_t spiWgpMask = 0u, rlcWgpMask = 0u;
    uint32_t rlcPgCntl = 0u;
    Context->SubmissionEngine.Mqd.CpHqdVmid = targetHqdVmid;
    if (Context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
        (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
        if (Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Context->SubmissionEngine.SelectedSelector)) {
            (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
                BC250_HQD_MMIO_VMID, &readbackHqdVmid);
            (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
                BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
            (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Context->SubmissionEngine.OriginalSelector);
        }

        /*
         * A politica de harvest/power-gating dos 24 CUs pertence ao firmware.
         * O 0x1F usado pelos candidatos 3.111--3.114 veio do patch opcional de
         * desbloqueio de 40 CUs e exige tambem alterar CC_GC_SHADER_ARRAY_CONFIG.
         * Aplicar apenas metade desse contrato deixou CPC_STALLED=0x00040000
         * antes do doorbell na corrida 3.114.1.0. Estes tres registradores
         * permanecem SOMENTE LEITURA para observabilidade.
         */
        (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_HQD_MMIO_RLC_PG_CNTL, &rlcPgCntl);
        (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK, &spiWgpMask);
        (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK, &rlcWgpMask);
    }
    KeReleaseSpinLock(&Context->SubmissionEngine.Lock, irql);

    Bc250DisplayLog("[SubmitVirtual Node1] HQD_VMID: Target=0x%08X Readback=0x%08X WGP_PG_READONLY: SPI=0x%08X RLC=0x%08X RLC_PG_CNTL=0x%08X Policy=FIRMWARE_PRESERVED\n",
        targetHqdVmid, readbackHqdVmid, spiWgpMask, rlcWgpMask, rlcPgCntl);
    Bc250SubmissionEngineCaptureRciuStage(Context, "AFTER_HQD_READBACK");
    return STATUS_SUCCESS;
}

static void Bc250ActiveCaptureMecTelemetry(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    const char *Tag)
{
    uint32_t selReadback = 0u, restReadback = 0u;
    uint32_t hqdValid = 0u, cpValid = 0u;
    uint32_t hqdActive = 0u, hqdRptr = 0u, hqdWptr = 0u, hqdError = 0u, hqStatus0 = 0u, ibCtrl = 0u, hqdVmid = 0u;
    uint32_t cpfStatus = 0u, cpfBusy = 0u, cpfStalled = 0u;
    uint32_t cpcStatus = 0u, cpcBusy = 0u, cpcStalled = 0u;
    uint32_t cpcStalledBeforeHqd = 0u;
    uint32_t mec1InstrPntr = 0u, mec2InstrPntr = 0u;
    uint32_t cpStalled1 = 0u, cpStalled2 = 0u, cpBusy = 0u, cpStat = 0u;
    uint32_t spiWgp = 0u, rlcWgp = 0u, smuMisc = 0u, rlcPgCntl = 0u;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) return;
    if (Context->Core.MmioTransport.State != BC250_MMIO_TOPOLOGY_MAPPED) return;

    /* Antes de selecionar/ler a HQD: separa estado preexistente de perturbacao
     * causada pela propria fotografia indexada. */
    (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
        BC250_HQD_MMIO_CP_CPC_STALLED_STAT1, &cpcStalledBeforeHqd);

    (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Context->SubmissionEngine.SelectedSelector);
    (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &selReadback);

    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_ACTIVE, &hqdActive)) hqdValid |= (1u << 0);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_PQ_RPTR, &hqdRptr)) hqdValid |= (1u << 1);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_PQ_WPTR_LO, &hqdWptr)) hqdValid |= (1u << 2);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_HQD_ERROR, &hqdError)) hqdValid |= (1u << 3);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_HQ_STATUS0, &hqStatus0)) hqdValid |= (1u << 4);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_IB_CONTROL, &ibCtrl)) hqdValid |= (1u << 5);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_VMID, &hqdVmid)) hqdValid |= (1u << 6);

    (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Context->SubmissionEngine.OriginalSelector);
    (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &restReadback);

    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_CP_CPF_STATUS, &cpfStatus)) cpValid |= (1u << 0);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &cpfBusy)) cpValid |= (1u << 1);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_CP_CPF_STALLED_STAT1, &cpfStalled)) cpValid |= (1u << 2);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_CP_CPC_STATUS, &cpcStatus)) cpValid |= (1u << 3);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_CP_CPC_BUSY_STAT, &cpcBusy)) cpValid |= (1u << 4);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_CP_CPC_STALLED_STAT1, &cpcStalled)) cpValid |= (1u << 5);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_CP_MEC1_INSTR_PNTR, &mec1InstrPntr)) cpValid |= (1u << 6);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_CP_MEC2_INSTR_PNTR, &mec2InstrPntr)) cpValid |= (1u << 7);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_CP_STALLED_STAT1, &cpStalled1)) cpValid |= (1u << 8);
    if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_CP_STAT, &cpStat)) cpValid |= (1u << 9);
    (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_CP_STALLED_STAT2, &cpStalled2);
    (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_CP_BUSY_STAT, &cpBusy);
    (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK, &spiWgp);
    (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK, &rlcWgp);
    (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_SMUIO_GFX_MISC_CNTL, &smuMisc);
    (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_HQD_MMIO_RLC_PG_CNTL, &rlcPgCntl);

    Bc250DisplayLog("[%s] Selector=0x%08X (Req=0x%08X) RestoredSel=0x%08X HqdValid=0x%X Active=%08X RPTR=%u WPTR=%u Error=%08X HqStatus=%08X IbCtrl=%08X Vmid=%08X\n",
        Tag, selReadback, Context->SubmissionEngine.SelectedSelector, restReadback,
        hqdValid, hqdActive, hqdRptr, hqdWptr, hqdError, hqStatus0, ibCtrl, hqdVmid);
    Bc250DisplayLog("[%s_CPF] CpValid=0x%X CpfStat=%08X CpfBusy=%08X CpfStalled=%08X CpcStat=%08X CpcBusy=%08X CpcStalledBeforeHqd=%08X CpcStalled=%08X Mec1Pntr=%08X Mec2Pntr=%08X CpStalled1=%08X CpStalled2=%08X CpBusy=%08X CpStat=%08X SpiWgp=0x%X RlcWgp=0x%X SmuMisc=0x%X RlcPgCntl=0x%X\n",
        Tag, cpValid, cpfStatus, cpfBusy, cpfStalled, cpcStatus, cpcBusy,
        cpcStalledBeforeHqd, cpcStalled, mec1InstrPntr, mec2InstrPntr,
        cpStalled1, cpStalled2, cpBusy, cpStat, spiWgp, rlcWgp, smuMisc,
        rlcPgCntl);
}

static NTSTATUS Bc250ActiveSubmitCommandVirtualBody(
    HANDLE AdapterContext,
    CONST DXGKARG_SUBMITCOMMANDVIRTUAL *pSubmitCommand)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext;
    BC250_CONTEXT_SUBMISSION_VIEW contextView;
    BC250_GPUVA_SNAPSHOT_READER gpuvaReader;
    BC250_MCDM_OBJECT_STATUS objStatus;
    BC250_GPUVA_STATUS vaStatus;
    BC250_ACTIVE_FENCE_SYNC reserveSync;
    BOOLEAN reserved = FALSE;
    uint64_t assignedFence = 0ull;
    uint32_t ibSizeDwords;
    NTSTATUS submitStatus;
    BOOLEAN physicalGpuvaAdmission;

    RtlZeroMemory(&gpuvaReader, sizeof(gpuvaReader));

    /* 1. Validar argumentos basicos fail-closed */
    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE ||
        pSubmitCommand == NULL || pSubmitCommand->hContext == NULL ||
        pSubmitCommand->DmaBufferSize == 0u ||
        (pSubmitCommand->DmaBufferSize % sizeof(uint32_t)) != 0u ||
        pSubmitCommand->DmaBufferVirtualAddress == 0ull ||
        (pSubmitCommand->DmaBufferVirtualAddress & 3ull) != 0ull ||
        pSubmitCommand->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    if (InterlockedCompareExchange(&context->IsActive, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }

    /*
     * Node 0 (SDMA) e' o no de paginacao/copia (DXGK_ENGINE_TYPE_COPY).
     * Submissoes neste no sao emitidas pelo GPU scheduler do Windows para
     * buffers de paginacao (p.ex. preenchimento da primary allocation gerado
     * por BuildPagingBuffer/VirtualFill).
     *
     * ESTADO MEDIDO EM 04/09 22:27 -- LEIA ANTES DE MEXER:
     *
     * O DMA buffer de paginacao E submetido ao hardware: SetRootPageTable
     * instala a root fisica no VMID8 com readback e invalidacao de TLB, e
     * daqui sai um pacote INDIRECT no anel da SDMA0 apontando para a GPUVA do
     * buffer. Nao ha mais notificacao de conclusao sem execucao.
     *
     * O QUE AINDA NAO ESTA PROVADO NO SILICIO: a IB retirar. Na corrida de
     * 22:27 o RPTR parou em 96 bytes (DW 24), que e exatamente o fim do pacote
     * INDIRECT -- a engine buscou o pacote, saltou para o IB sob VMID8 e nao
     * voltou. A fence externa ficou no poison 0x43DAFFFE e o GFXHUB nao
     * latchou fault (STATUS=0). Uma corrida anterior, as 22:16, retirou uma IB
     * de 15 dwords sem NOP de cauda; a de 22:27 travou com 16 dwords. Nao ha
     * dado suficiente para escolher entre "o NOP/IB_SIZE de cauda mata" e "a
     * corrida de 22:16 foi fragil", por isso o IB_SIZE agora e explicito e o
     * encoder despeja os dwords que a engine vai buscar (IB_DUMP/IB_SHAPE).
     *
     * Enquanto isso, cada submissao nao executada continua contada e logada,
     * e a falta e reportada por DXGK_INTERRUPT_DMA_FAULTED: a Lei 4 proibe
     * sucesso silencioso sem backend.
     */
    if (pSubmitCommand->NodeOrdinal == BC250_SUBMISSION_NODE_SDMA) {
        BC250_MCDM_OBJECT_RECORD submitContextRecord;
        BC250_GPUVA_SNAPSHOT_READER ibReader;
        BC250_MCDM_OBJECT_STATUS ibObjectStatus;
        BC250_GPUVA_STATUS ibResolveStatus;
        uint64_t ibBackingOffset = 0ull;
        uint32_t ibBackingSegment = 0u;
        uint32_t ibUsefulDwords = 0u;
        BC250_PAGING_PROOF pagingProof = {0};
        BOOLEAN ibExecuted = FALSE;
        BOOLEAN ibWalkOk = FALSE;
        BC250_SUBMIT_PATH_INPUT pathInput;
        BC250_SUBMIT_EXEC_PATH execPath = BC250_SUBMIT_PATH_REFUSED;
        uint64_t destPhysStart = 0ull;
        uint64_t vramMcBase = 0ull;
        BC250_MCDM_RUNTIME_CAPS walkCaps;
        uint32_t assignedHwFence = 0u;

        RtlZeroMemory(&submitContextRecord, sizeof(submitContextRecord));
        RtlZeroMemory(&ibReader, sizeof(ibReader));
        RtlZeroMemory(&walkCaps, sizeof(walkCaps));

        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] FenceId=%llu, DmaSize=%u, DmaVA=0x%llX\n",
            (unsigned long long)pSubmitCommand->SubmissionFenceId,
            pSubmitCommand->DmaBufferSize,
            (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress);

        /*
         * item 25 (07/09) -- HANG INDUZIDO, UMA VEZ POR BIND.
         *
         * Aceita a submissao e NAO notifica completion, de proposito: e' a
         * fence pendente que faz o timeout de 2 s do dxgkrnl disparar o TDR,
         * e e' o TDR que esta sendo medido. A engine para de verdade num
         * POLL_REGMEM que nunca satisfaz -- nada aqui e' simulado.
         *
         * Fail-closed: so' com TdrInduceHang presente E igual a 1, e o
         * InterlockedCompareExchange garante disparo unico para a corrida nao
         * entrar em laco de TDR.
         */
        if (context->TdrInduceHangPresent == 1u && context->TdrInduceHang == 1u &&
            InterlockedCompareExchange(&context->TdrHangInduced, 1, 0) == 0) {
            if (Bc250SdmaPresentInduceHangForTdr()) {
                Bc250DisplayLog("[SubmitVirtual Node0 SDMA] TDR_ENSAIO: hang induzido no silicio para FenceId=%llu; completion DELIBERADAMENTE nao notificado. Espere ResetFromTimeout/QueryEngineStatus/RestartFromTimeout do dxgkrnl em ~2 s\n",
                    (unsigned long long)pSubmitCommand->SubmissionFenceId);
                return STATUS_SUCCESS;
            }
            Bc250DisplayLog("[SubmitVirtual Node0 SDMA] TDR_ENSAIO ABORTADO: nao foi possivel armar o hang; seguindo com a submissao normal\n");
        }

        if (context->Core.DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
            context->Core.DxgkInterface.DeviceHandle == NULL) {
            Bc250DisplayLog("[SubmitVirtual Node0 SDMA] DeviceHandle or DxgkCbSynchronizeExecution is NULL\n");
            return STATUS_DEVICE_NOT_READY;
        }

        /*
         * DxgkDdiSubmitCommandVirtual executa em DISPATCH_LEVEL (IRQL 2): push locks e
         * KeEnterCriticalRegion são ESTRITAMENTE PROIBIDOS aqui (causam BugCheck 0xD1).
         * Acesso a objetos e sincronização usam primitivas lock-free / spinlock.
         */
        (void)Bc250ActiveQueryRuntimeCaps(context, &walkCaps);
        vramMcBase = walkCaps.VramMcBase;
        if (vramMcBase == 0ull) {
            vramMcBase = BC250_GCVM_VRAM_MC_BASE_FALLBACK;
        }

        /*
         * Comutação Dinâmica de VMID 8 (Context Switching):
         * Se o contexto desta submissão (ex.: contexto de paginação do VidMm) não for o
         * que está atualmente instalado no hardware da GCVM (ex.: DWM que instalou sua
         * própria root anteriormente), comutamos o VMID 8 para a root deste contexto
         * ANTES de validar a GPUVA e despachar.
         */
        BC250_MCDM_OBJECT_RECORD submitProcessRecord;
        uint64_t targetPtPhys = 0ull;
        BOOLEAN hasRootPt = FALSE;

        RtlZeroMemory(&submitProcessRecord, sizeof(submitProcessRecord));
        ibObjectStatus = Bc250McdmObjectLookup(&context->Objects,
            (BC250_MCDM_HANDLE)pSubmitCommand->hContext,
            BC250_MCDM_OBJECT_CONTEXT, NULL, &submitContextRecord);

        if (ibObjectStatus == BC250_MCDM_OBJECT_OK && submitContextRecord.OwnerProcess != NULL) {
            (void)Bc250McdmObjectLookup(&context->Objects,
                submitContextRecord.OwnerProcess,
                BC250_MCDM_OBJECT_PROCESS, NULL, &submitProcessRecord);
        }

        if (submitContextRecord.RootPageTableSetCount > 0u &&
            submitContextRecord.RootPageTableSegmentId == 1u) {
            targetPtPhys = vramMcBase + walkCaps.LocalSegmentBaseOffset +
                submitContextRecord.RootPageTableAddress;
            hasRootPt = TRUE;
        } else if (submitProcessRecord.RootPageTableSetCount > 0u &&
                   submitProcessRecord.RootPageTableSegmentId == 1u) {
            targetPtPhys = vramMcBase + walkCaps.LocalSegmentBaseOffset +
                submitProcessRecord.RootPageTableAddress;
            hasRootPt = TRUE;
        }

        BOOLEAN isPaging = (pSubmitCommand->Flags.Paging != 0);
        uint32_t targetVmid = isPaging ? BC250_GCVM_VMID8 : BC250_GCVM_USER_VMID;
        BC250_MCDM_HANDLE *pInstalledCtx = isPaging ?
            &context->Vmid8InstalledContext : &context->UserVmidInstalledContext;
        uint64_t *pInstalledRoot = isPaging ?
            &context->Vmid8InstalledRootPhysical : &context->UserVmidInstalledRootPhysical;
        BC250_GCVM_VMID8_STATUS *pInstallStatus = isPaging ?
            &context->Vmid8InstallStatus : &context->UserVmidInstallStatus;
        BC250_GCVM_VMID8_INSTALL_EVIDENCE *pInstallEvidence = isPaging ?
            &context->Vmid8InstallEvidence : &context->UserVmidInstallEvidence;

        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] CTX_PROBE: Ctx=%p, Proc=%p, hasRoot=%d, targetPtPhys=0x%llX, Vmid=%u, instCtx=%p, instRoot=0x%llX\n",
            pSubmitCommand->hContext, submitContextRecord.OwnerProcess,
            hasRootPt, (unsigned long long)targetPtPhys, targetVmid,
            *pInstalledCtx,
            (unsigned long long)*pInstalledRoot);

        if (hasRootPt && targetPtPhys != 0ull &&
            context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED &&
            walkCaps.Ready != 0u) {
            if (*pInstalledCtx != (BC250_MCDM_HANDLE)pSubmitCommand->hContext ||
                *pInstalledRoot != targetPtPhys ||
                *pInstallStatus != BC250_GCVM_VMID8_OK) {

                /*
                 * INVARIANTE DE ISOLAMENTO DE VMID:
                 * Se houver submissões em voo no anel SDMA, tentamos aposentar oportunisticamente.
                 * Contexto de paginação (VMID 8) e contexto de usuário/DWM (VMID 7) usam
                 * registradores e identificadores de VMID distintos, de modo que um NUNCA
                 * sobrescreve o outro.
                 */
                if (g_Bc250SdmaPresent.InFlightCount > 0u) {
                    Bc250ActiveSdmaRetireAndNotify(context, FALSE);
                }

                if (g_Bc250SdmaPresent.InFlightCount > 0u) {
                    /*
                     * Submissão anterior ainda em voo no momento da comutação.
                     * Registramos telemetria de diagnóstico completa de registradores SDMA.
                     */
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
                    uint32_t rptrReg = 0u;
                    uint32_t wptrReg = 0u;
                    uint32_t statusReg = 0u;
                    uint32_t status1Reg = 0u;
                    uint32_t errReg = 0u;
                    uint32_t ibCntl = 0u;
                    uint32_t ibRptr = 0u;
                    uint32_t ibOffset = 0u;
                    uint32_t ibBaseLo = 0u;
                    uint32_t ibBaseHi = 0u;
                    uint32_t ibSize = 0u;
                    uint32_t ctxStatus = 0u;
                    uint32_t ibSubRemain = 0u;
                    uint32_t headSlot = g_Bc250SdmaPresent.InFlightHead;
                    uint32_t slotFenceOffset = Bc250SdmaAsyncSlotFenceOffset(headSlot);
                    uint32_t fenceObs = (g_Bc250SdmaPresent.Work != NULL) ?
                        g_Bc250SdmaPresent.Work[slotFenceOffset / 4u] : 0u;
                    uint32_t fenceExp = g_Bc250SdmaPresent.InFlightQueue[headSlot].HardwareFence;

                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_REG_RB_RPTR, &rptrReg);
                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_REG_RB_WPTR, &wptrReg);
                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_REG_STATUS, &statusReg);
                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_REG_STATUS1, &status1Reg);
                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_REG_ERROR_LOG, &errReg);
                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_GFX_IB_CNTL_OFFSET, &ibCntl);
                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_GFX_IB_RPTR_OFFSET, &ibRptr);
                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_GFX_IB_OFFSET_OFFSET, &ibOffset);
                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_GFX_IB_BASE_LO_OFFSET, &ibBaseLo);
                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_GFX_IB_BASE_HI_OFFSET, &ibBaseHi);
                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_GFX_IB_SIZE_OFFSET, &ibSize);
                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_GFX_CONTEXT_STATUS_OFFSET, &ctxStatus);
                    (void)Bc250ActiveVmidRead32(context, BC250_SDMA0_GFX_IB_SUB_REMAIN_OFFSET, &ibSubRemain);

                    Bc250DisplayLog("[SubmitVirtual Node0 SDMA] AVISO CONTEXT SWITCH COM IN_FLIGHT: InFlight=%u Slot=%u WPTR=%u RPTR=%u STATUS=%08X (Idle=%u InsideIb=%u) STATUS1=%08X (CeIdle=%u) ERR_LOG=%08X FenceExp=%08X FenceObs=%08X IB_CNTL=%08X IB_RPTR=%u IB_OFFSET=%u IB_BASE=0x%08X%08X IB_SIZE=%u CTX_STATUS=%08X SUB_REMAIN=%u (Ctx %p -> %p Vmid=%u)\n",
                        g_Bc250SdmaPresent.InFlightCount,
                        headSlot, wptrReg, rptrReg, statusReg,
                        (statusReg & 1u), ((statusReg & (1u << 9)) != 0u) ? 1u : 0u,
                        status1Reg, ((status1Reg & 0x3FFu) == 0x3FFu) ? 1u : 0u,
                        errReg, fenceExp, fenceObs, ibCntl,
                        ibRptr, ibOffset, ibBaseHi, ibBaseLo, ibSize, ctxStatus, ibSubRemain,
                        *pInstalledCtx, pSubmitCommand->hContext, targetVmid);
#else
                    Bc250DisplayLog("[SubmitVirtual Node0 SDMA] AVISO CONTEXT SWITCH COM IN_FLIGHT: InFlight=%u (Ctx %p -> %p Vmid=%u)\n",
                        g_Bc250SdmaPresent.InFlightCount,
                        *pInstalledCtx, pSubmitCommand->hContext, targetVmid);
#endif
                    Bc250ActiveSdmaRetirementEnsureScheduled(context);
                }

                BC250_GCVM_VMID8_INTERFACE io;
                BC250_GCVM_VMID8_STATUS vStatus;

                RtlZeroMemory(&io, sizeof(io));
                io.Identity = BC250_GCVM_VMID8_IDENTITY;
                io.Context = context;
                io.Read32 = Bc250ActiveVmidRead32;
                io.Write32 = Bc250ActiveVmidWrite32;
                if (isPaging) {
                    io.Flush = Bc250ActiveFlushVmid8Only;
                } else {
                    io.Flush = Bc250ActiveFlushUserVmidOnly;
                }

                RtlZeroMemory(pInstallEvidence, sizeof(*pInstallEvidence));
                vStatus = Bc250GcvmInstallVmid(targetVmid, targetPtPhys, &io, pInstallEvidence);
                *pInstallStatus = vStatus;
                if (vStatus == BC250_GCVM_VMID8_OK) {
                    HANDLE prevCtx = (HANDLE)*pInstalledCtx;
                    *pInstalledCtx = (BC250_MCDM_HANDLE)pSubmitCommand->hContext;
                    *pInstalledRoot = targetPtPhys;
                    Bc250DisplayLog("[SubmitVirtual Node0 SDMA] VMID%u CONTEXT SWITCH: Ctx %p -> %p (Root=0x%llX, PT_BASE=0x%llX)\n",
                        targetVmid, prevCtx, pSubmitCommand->hContext,
                        (unsigned long long)targetPtPhys,
                        (unsigned long long)pInstallEvidence->PageTablePhysical);
                } else {
                    Bc250DisplayLog("[SubmitVirtual Node0 SDMA] VMID%u CONTEXT SWITCH FALHOU: Ctx=%p, Root=0x%llX, Status=%u\n",
                        targetVmid, pSubmitCommand->hContext, (unsigned long long)targetPtPhys, (uint32_t)vStatus);
                }
            }
        }

        if (*pInstallStatus == BC250_GCVM_VMID8_OK &&
            pInstallEvidence->PhysicalReady != 0u &&
            *pInstalledCtx ==
                (BC250_MCDM_HANDLE)pSubmitCommand->hContext &&
            *pInstalledRoot != 0ull) {
            /*
             * Fotografia logica da mesma GPUVA que o hardware vai buscar.
             * Nao substitui a caminhada fisica da page table, mas separa
             * desde ja "journal nao conhece a IB" de falha na GCVM/UTCL1.
             */
            if (ibObjectStatus == BC250_MCDM_OBJECT_OK) {
                ibResolveStatus = Bc250GpuvaSnapshotAcquire(
                    &context->GpuvaSnapshots, &ibReader);
                if (ibResolveStatus == BC250_GPUVA_OK) {
                    ibResolveStatus = Bc250GpuvaSpaceResolveAddress(
                        ibReader.Space, submitContextRecord.OwnerProcess,
                        (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
                        BC250_PTE_READABLE, &ibBackingSegment,
                        &ibBackingOffset);
                    Bc250GpuvaSnapshotRelease(&context->GpuvaSnapshots,
                        &ibReader);
                }
            } else {
                ibResolveStatus = BC250_GPUVA_BAD_STATE;
            }
            Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_GPUVA_SNAPSHOT: Status=%u ObjStatus=%u VA=0x%llX Size=%u -> Segment=%u Offset=0x%llX Epoch=%u\n",
                (uint32_t)ibResolveStatus, (uint32_t)ibObjectStatus,
                (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress,
                pSubmitCommand->DmaBufferSize, ibBackingSegment,
                (unsigned long long)ibBackingOffset,
                submitContextRecord.PageTableEpoch);
            /*
             * Gate de traducao: resolvemos a GPUVA do IB pela CPU, lendo as
             * mesmas page tables que a GPU vai ler, ANTES de publicar o
             * INDIRECT. Se a caminhada nao fecha, a engine tambem nao vai
             * fechar -- e uma engine travada custa uma corrida e um reboot.
             * Fail-closed: nao tocamos no silicio.
             */
            {
                uint64_t resolved = 0ull;

                Bc250DisplayLog("[PT_WALK] RootMC=0x%llX VramMcBase=0x%llX LocalSegBase=0x%llX Janela=%u bytes\n",
                    (unsigned long long)*pInstalledRoot,
                    (unsigned long long)vramMcBase,
                    (unsigned long long)walkCaps.LocalSegmentBaseOffset,
                    g_Bc250DisplayState.FrameBufferSize);

                ibWalkOk = Bc250ActiveValidateGpuVaRange("IB",
                    (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
                    (uint64_t)pSubmitCommand->DmaBufferSize,
                    *pInstalledRoot,
                    vramMcBase, BC250_PTE_READABLE, &resolved);

                /*
                 * O PA que a caminhada resolveu tem de ser o PA onde a CPU
                 * realmente escreveu os pacotes. Ate agora o IB_DUMP saia da
                 * VA da CPU e ninguem conferia o outro lado -- resolver a
                 * traducao nao prova que a GPU busca a NOSSA pagina.
                 */
                if (ibWalkOk) {
                    BOOLEAN proofMatched = pSubmitCommand->Flags.Paging && Bc250PagingTakeProof(resolved,
                        pSubmitCommand->DmaBufferSize, &pagingProof);
                    Bc250DisplayLog("[PT_WALK] IB_PROOF_BIND: PA=0x%llX Bytes=%u ExactMetadata=%u (miss: no poison)\n",
                        (unsigned long long)resolved, pSubmitCommand->DmaBufferSize,
                        proofMatched ? 1u : 0u);
                }

                /*
                 * Validar o IB nao basta. Se for COPY_LINEAR, caminhamos TODAS
                 * as paginas da origem e do destino. Se for CONST_FILL,
                 * caminhamos TODAS as paginas do destino. Tres probes eram uma
                 * amostra, nao um gate de seguranca para faixas grandes.
                 */
                {
                    UINT64 destStart = pagingProof.Destination;
                    UINT64 destEnd = destStart + pagingProof.Bytes;

                    if (pagingProof.Bytes == 0u) {
                        Bc250DisplayLog("[PT_WALK] DEST: encoder nao publicou faixa de escrita\n");
                    } else {
                        if (pagingProof.Opcode == BC250_PAGING_PROOF_OP_COPY) {
                            UINT64 srcStart = pagingProof.Source;
                            UINT64 srcEnd = srcStart + pagingProof.Bytes;
                            Bc250DisplayLog("[PT_WALK] SRC: faixa de leitura [0x%llX, 0x%llX)\n",
                                (unsigned long long)srcStart,
                                (unsigned long long)srcEnd);
                            if (!Bc250ActiveValidateGpuVaRange("SRC", srcStart,
                                    pagingProof.Bytes, *pInstalledRoot,
                                    vramMcBase, BC250_PTE_READABLE, NULL)) {
                                ibWalkOk = FALSE;
                            }
                        }

                        Bc250DisplayLog("[PT_WALK] DEST: faixa de escrita [0x%llX, 0x%llX)\n",
                            (unsigned long long)destStart,
                            (unsigned long long)destEnd);
                        if (!Bc250ActiveValidateGpuVaRange("DEST", destStart,
                                pagingProof.Bytes, *pInstalledRoot,
                                vramMcBase, BC250_PTE_WRITEABLE,
                                &destPhysStart)) {
                            ibWalkOk = FALSE;
                        }
                    }
                }
            }

            {
                UINT64 fillDestVa = pagingProof.Destination;
                ULONG fillBytes = pagingProof.Bytes;
                ULONG fillPattern = pagingProof.Pattern;
                BOOLEAN hasFill = (pagingProof.Opcode == BC250_PAGING_PROOF_OP_FILL && pagingProof.Bytes != 0u);

                ibUsefulDwords = pSubmitCommand->DmaBufferSize / (uint32_t)sizeof(uint32_t);

                /*
                 * DIRECT FILL primeiro, e SEM exigir ibWalkOk: quem escreve e a
                 * engine lendo pacotes do ANEL, com endereco fisico ja
                 * resolvido por nos. A GPU nunca toca o DMA buffer, entao a
                 * GPUVA dele nao precisa resolver.
                 *
                 * O gate que continua valendo e o do DESTINO: sem
                 * destPhysStart nao ha para onde escrever.
                 *
                 * A13 (07/09): a escolha do ramo sai de
                 * Bc250SelectSubmitExecPath, fonte unica e testada.
                 * B3 (07/09, depois de 17:43 e 18:16): o DEFAULT passou a ser
                 * somente INDIRECT -- ele executou o DMA buffer real com o
                 * tripe completo. Os dois PLANO A so' entram com
                 * SdmaPlanoAFallback=1 e existem apenas para bisseccao.
                 */
                pathInput.HasPendingFill = hasFill ? 1u : 0u;
                pathInput.FillBytes = fillBytes;
                pathInput.DestPhysicalStart = destPhysStart;
                pathInput.DmaBufferSizeBytes = pSubmitCommand->DmaBufferSize;
                pathInput.IbGpuVaResolves = ibWalkOk ? 1u : 0u;
                pathInput.PlanoAFallbackEnabled =
                    (context->SdmaPlanoAFallbackPresent == 1u &&
                     context->SdmaPlanoAFallback == 1u) ? 1u : 0u;
                execPath = Bc250SelectSubmitExecPath(&pathInput);

                Bc250DisplayLog("[SubmitVirtual Node0 SDMA] CAMINHO_ESCOLHIDO: %s (Fill=%u/%u bytes DestPA=0x%llX DmaBytes=%u IbResolve=%u PlanoAFallback=%u)\n",
                    Bc250SubmitPathName(execPath),
                    pathInput.HasPendingFill,
                    pathInput.FillBytes,
                    (unsigned long long)pathInput.DestPhysicalStart,
                    pathInput.DmaBufferSizeBytes,
                    pathInput.IbGpuVaResolves,
                    pathInput.PlanoAFallbackEnabled);

                Bc250ActiveSdmaRetirementTimerStart(context);

                Bc250ActiveSdmaRetireAndNotify(context, FALSE);

                if (g_Bc250SdmaPresent.InFlightCount >= BC250_SDMA_IN_FLIGHT_CAPACITY) {
                    Bc250DisplayLog("[SubmitVirtual Node0 SDMA] AVISO: InFlightQueue cheia (%u submissoes em voo)\n",
                        g_Bc250SdmaPresent.InFlightCount);
                }

                if (execPath == BC250_SUBMIT_PATH_RING_DIRECT_FILL) {
                    uint64_t sdmaMcBase = (g_Bc250SdmaPresent.VramMcStart != 0ull) ?
                        g_Bc250SdmaPresent.VramMcStart : BC250_SDMA_VRAM_MC_BASE;
                    uint64_t destMcAddress = destPhysStart;
                    if (destPhysStart >= vramMcBase) {
                        uint64_t vramOffset = destPhysStart - vramMcBase;
                        destMcAddress = sdmaMcBase + vramOffset;
                    }
                    Bc250DisplayLog("[SubmitVirtual Node0 SDMA] PLANO A DIRECT FILL: DestVA=0x%llX PA=0x%llX -> MC=0x%llX Size=%u Pattern=0x%08X (substituindo INDIRECT)\n",
                        (unsigned long long)fillDestVa,
                        (unsigned long long)destPhysStart,
                        (unsigned long long)destMcAddress,
                        fillBytes, fillPattern);
                    ibExecuted = Bc250SdmaPresentSubmitFillAsync(
                        destMcAddress,
                        fillBytes,
                        fillPattern,
                        pSubmitCommand->SubmissionFenceId,
                        (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
                        &assignedHwFence);
                    Bc250PagingClearFillParams();
                } else if (execPath == BC250_SUBMIT_PATH_RING_SYNC_NOP) {
                    Bc250DisplayLog("[SubmitVirtual Node0 SDMA] PLANO A SYNC NOP: DmaVA=0x%llX (substituindo INDIRECT)\n",
                        (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress);
                    ibExecuted = Bc250SdmaPresentSubmitSyncNopAsync(
                        pSubmitCommand->SubmissionFenceId,
                        (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
                        &assignedHwFence);
                } else if (execPath == BC250_SUBMIT_PATH_INDIRECT) {
                    uint32_t alignedDwords = ibUsefulDwords;
                    uint64_t effectDestMc = 0ull;
                    ULONG effectBytes = 0u;
                    uint32_t effectPattern = 0u;

                    if (alignedDwords == 0u) {
                        alignedDwords = pSubmitCommand->DmaBufferSize / (uint32_t)sizeof(uint32_t);
                    }
                    if (alignedDwords < 8u) {
                        alignedDwords = 8u;
                    }
                    if (hasFill && destPhysStart != 0ull && fillBytes > 0u) {
                        uint64_t sdmaMcBase = (g_Bc250SdmaPresent.VramMcStart != 0ull) ?
                            g_Bc250SdmaPresent.VramMcStart : BC250_SDMA_VRAM_MC_BASE;
                        effectDestMc = destPhysStart;
                        if (destPhysStart >= vramMcBase) {
                            effectDestMc = sdmaMcBase + (destPhysStart - vramMcBase);
                        }
                        effectBytes = fillBytes;
                        effectPattern = fillPattern;
                        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_EFEITO_DECLARADO: DestVA=0x%llX PA=0x%llX -> MC=0x%llX Size=%u Pattern=0x%08X (o IB deve produzir isto; canaries plantados antes do disparo)\n",
                            (unsigned long long)fillDestVa,
                            (unsigned long long)destPhysStart,
                            (unsigned long long)effectDestMc,
                            effectBytes, effectPattern);
                    } else {
                        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB_EFEITO_DECLARADO: AUSENTE (hasFill=%u destPA=0x%llX bytes=%u) -- perna de dados ficara NAO CONFERIDA\n",
                            hasFill ? 1u : 0u,
                            (unsigned long long)destPhysStart,
                            fillBytes);
                    }
                    ibExecuted = Bc250SdmaPresentSubmitIndirectAsync(
                        (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
                        pSubmitCommand->DmaBufferSize,
                        alignedDwords,
                        targetVmid,
                        effectDestMc,
                        effectBytes,
                        effectPattern,
                        pSubmitCommand->SubmissionFenceId,
                        (uint32_t)execPath,
                        &assignedHwFence);
                    if (ibExecuted && effectBytes > 0u) {
                        Bc250PagingClearFillParams();
                    }
                } else {
                    Bc250DisplayLog("[SubmitVirtual Node0 SDMA] RECUSA (%s): sem caminho aplicavel -- a GPUVA do IB (0x%llX) nao resolve sob VMID%u; silicio nao tocado\n",
                        Bc250SubmitPathName(execPath),
                        (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress,
                        targetVmid);
                }
            }
        } else {
            Bc250DisplayLog("[SubmitVirtual Node0 SDMA] REJECT VMID%u binding: SubmitCtx=%p InstalledCtx=%p Status=%u PhysReady=%u Root=0x%llX\n",
                targetVmid,
                pSubmitCommand->hContext,
                *pInstalledCtx,
                (uint32_t)*pInstallStatus,
                pInstallEvidence->PhysicalReady,
                (unsigned long long)*pInstalledRoot);
        }
        if (!ibExecuted) {
            uint32_t faultStatus = 0u;
            uint32_t faultAddrLo = 0u;
            uint32_t faultAddrHi = 0u;
            uint32_t faultStatusRead;
            uint32_t faultLoRead;
            uint32_t faultHiRead;
            LONG notExecuted = InterlockedIncrement(
                (volatile LONG *)&context->SubmissionsNotExecutedCount);
            faultStatusRead = Bc250ActiveVmidRead32(context,
                BC250_ACTIVE_GCVM_FAULT_STATUS, &faultStatus);
            faultLoRead = Bc250ActiveVmidRead32(context,
                BC250_ACTIVE_GCVM_FAULT_ADDR_LO, &faultAddrLo);
            faultHiRead = Bc250ActiveVmidRead32(context,
                BC250_ACTIVE_GCVM_FAULT_ADDR_HI, &faultAddrHi);
            Bc250DisplayLog("[SubmitVirtual Node0 SDMA] GFXHUB_FAULT_SNAPSHOT: Read=%u/%u/%u STATUS=%08X ADDR=%08X:%08X\n",
                faultStatusRead, faultLoRead, faultHiRead, faultStatus,
                faultAddrHi, faultAddrLo);
            Bc250DisplayLog("[SubmitVirtual Node0 SDMA] IB NAO EXECUTADO #%ld: FenceId=%llu; completion NAO notificado\n",
                notExecuted,
                (unsigned long long)pSubmitCommand->SubmissionFenceId);

            /*
             * MEDIDO NA CORRIDA DE 04/09 23:23 -- NAO REINTRODUZIR SEM NOVA
             * EVIDENCIA:
             *
             * Reportar a falta por DXGK_INTERRUPT_DMA_FAULTED daqui NAO evita
             * o BugCheck. A notificacao chega ao scheduler e ele derruba a
             * maquina de dentro dela:
             *
             *   bc250mcdm!Bc250McdmSubmitCommandVirtual
             *     -> dxgkrnl!DpSynchronizeExecution -> nt!KeSynchronizeExecution
             *       -> dxgkrnl!DxgNotifyInterruptCB
             *         -> dxgmms2!VidSchDdiNotifyInterrupt
             *           -> dxgmms2!VidSchDdiNotifyInterruptWorker
             *             -> watchdog!WdCriticalError
             *   BugCheck 0x119, Arg1=0xD, Arg2=0, Arg3=0, Arg4=1 (o FenceId).
             *
             * A submissao veio de dxgmms2!VidSchiSubmitPagingCommand: esta e a
             * fila de PAGINACAO. Trocamos um 0x119 Arg1=2 por um 0x119 Arg1=0xD
             * sem ganhar nada. Voltamos a devolver o erro, que ao menos e o
             * caminho conhecido e deixa dump e log nomeando o motivo.
             */
            return STATUS_IO_DEVICE_ERROR;
        }

        /*
         * Despacho assincrono: agenda DPC de retirement do SDMA. O hardware
         * processa em paralelo sem prender a CPU em DISPATCH_LEVEL.
         */
        KeInsertQueueDpc(&context->SdmaRetirementDpc, NULL, NULL);

        /*
         * Retirement oportunista de baixissima latencia (sem stall / sem YieldProcessor):
         * Se a submissao completou imediatamente, aposenta e notifica de forma nao-bloqueante.
         */
        if (g_Bc250SdmaPresent.InFlightCount > 0u && g_Bc250SdmaPresent.Work != NULL) {
            uint32_t headSlot = g_Bc250SdmaPresent.InFlightHead;
            uint32_t headHwFence = g_Bc250SdmaPresent.InFlightQueue[headSlot].HardwareFence;
            uint32_t slotFenceOffset = Bc250SdmaAsyncSlotFenceOffset(headSlot);
            if (g_Bc250SdmaPresent.Work[slotFenceOffset / 4u] == headHwFence) {
                Bc250ActiveSdmaRetireAndNotify(context, FALSE);
            }
        }

        Bc250ActiveSdmaRetirementEnsureScheduled(context);

        InterlockedIncrement((volatile LONG *)&context->SubmissionsCount);

        Bc250DisplayLog("[SubmitVirtual Node0 SDMA] SUBMISSAO DESPACHADA (ASYNC %s): HwFence=%u FenceId=%llu DmaBytes=%u; DPC agendado\n",
            Bc250SubmitPathName(execPath),
            assignedHwFence,
            (unsigned long long)pSubmitCommand->SubmissionFenceId,
            pSubmitCommand->DmaBufferSize);

        return STATUS_SUCCESS;
    }

    /*
     * C3.17 (12/09) -- a page table fisica e' a fonte de verdade do submit.
     *
     * A corrida 19:27 mediu o primeiro IB do RADV executando no MEC e, antes
     * do segundo, o publicador do espelho logico encontrou o slot inativo
     * ainda fixado pela fence anterior (PublishStatus=BUSY, Attempts=16,
     * Busy=16). Depois a mesma corrida mediu sete NO_SPACE com 853--879
     * intervalos vivos. Portanto nem esperar mais nem aumentar novamente o
     * array torna esse espelho uma condicao de correcao do hardware.
     *
     * O modo de laboratorio continua explicitamente armado por
     * GpuvaLabPromotion. Nele, a admissao final acontece abaixo caminhando
     * TODAS as paginas do IB na root do proprio contexto, imediatamente antes
     * de publicar o descriptor. Fora do laboratorio, o gate antigo permanece
     * fail-closed.
     */
    physicalGpuvaAdmission =
        (context->GpuvaLabPromotionPresent == 1u &&
         context->GpuvaLabPromotion == 1u) ? TRUE : FALSE;
    if (InterlockedCompareExchange(
            &context->GpuvaMappingsOperational, 1, 1) != 1 &&
        !physicalGpuvaAdmission) {
        /*
         * B4 (07/09) -- ESTA RECUSA ERA MUDA. Toda submissao de Node 1 saia
         * daqui com STATUS_NOT_SUPPORTED e ZERO linhas no log; nas corridas o
         * KMD recebeu 2 contextos Node 1 e nenhum submit, e nao havia como
         * distinguir "o scheduler nao submeteu" de "nos recusamos".
         *
         * O gate de GpuvaMappingsOperational so' abre em ActiveFlushTlb quando
         * GpuvaLabPromotion esta presente E em 1 E a elegibilidade fecha. O
         * mask abaixo diz exatamente qual condicao falta.
         */
        Bc250DisplayLog("[SubmitVirtual Node%u] RECUSA: GpuvaMappingsOperational=0 -- Eligible=%ld BlockMask=0x%08X (JOURNAL=%u HIER=%u INATIVO=%u CTXABI=%u ALLOCABI=%u ROOTABI=%u ENGINE_OFF=%u SNAPSLOT=%u SNAPSIG=%u SNAPVAZIO=%u) LabPromotion=%u (present=%u) FenceId=%llu DmaSize=%u; silicio nao tocado\n",
            (uint32_t)pSubmitCommand->NodeOrdinal,
            InterlockedCompareExchange(&context->GpuvaActivationEligible, 0, 0),
            context->GpuvaActivationBlockMask,
            (context->GpuvaActivationBlockMask & BC250_GPUVA_GATE_BLOCK_JOURNAL_PENDING) ? 1u : 0u,
            (context->GpuvaActivationBlockMask & BC250_GPUVA_GATE_BLOCK_HIERARCHY_PENDING) ? 1u : 0u,
            (context->GpuvaActivationBlockMask & BC250_GPUVA_GATE_BLOCK_ADAPTER_INACTIVE) ? 1u : 0u,
            (context->GpuvaActivationBlockMask & BC250_GPUVA_GATE_BLOCK_CONTEXT_ABI) ? 1u : 0u,
            (context->GpuvaActivationBlockMask & BC250_GPUVA_GATE_BLOCK_ALLOCATION_ABI) ? 1u : 0u,
            (context->GpuvaActivationBlockMask & BC250_GPUVA_GATE_BLOCK_ROOT_ABI) ? 1u : 0u,
            (context->GpuvaActivationBlockMask & BC250_GPUVA_GATE_BLOCK_ENGINE_OFFLINE) ? 1u : 0u,
            (context->GpuvaActivationBlockMask & BC250_GPUVA_GATE_BLOCK_SNAPSHOT_SLOT) ? 1u : 0u,
            (context->GpuvaActivationBlockMask & BC250_GPUVA_GATE_BLOCK_SNAPSHOT_SIGNATURE) ? 1u : 0u,
            (context->GpuvaActivationBlockMask & BC250_GPUVA_GATE_BLOCK_SNAPSHOT_EMPTY) ? 1u : 0u,
            context->GpuvaLabPromotion,
            context->GpuvaLabPromotionPresent,
            (unsigned long long)pSubmitCommand->SubmissionFenceId,
            pSubmitCommand->DmaBufferSize);
        /*
         * C3.9a: quem segura o journal. "JOURNAL=1" sozinho nao distinguia
         * pendencia propria de pendencia do dwm.exe -- e na corrida 13:29 era
         * do dwm.exe, que ficou vivo 3 min 13 s e nunca deixou o journal drenar.
         */
        Bc250DisplayLog("[SubmitVirtual Node%u] JOURNAL_PENDENTE: SlotMask=0x%08X Count=%u GatePerProcess=%u (present=%u)\n",
            (uint32_t)pSubmitCommand->NodeOrdinal,
            context->GpuvaJournalPendingSlotMask,
            context->GpuvaJournalPendingCount,
            context->Node1JournalGatePerProcess,
            context->Node1JournalGatePerProcessPresent);
        return STATUS_NOT_SUPPORTED;
    }

    if (InterlockedCompareExchange(
            &context->GpuvaMappingsOperational, 1, 1) != 1) {
        Bc250DisplayLog("[SubmitVirtual Node%u] SNAPSHOT LOGICO INDISPONIVEL: seguindo para gate fisico integral da page table (LabPromotion=%u/%u)\n",
            (uint32_t)pSubmitCommand->NodeOrdinal,
            context->GpuvaLabPromotionPresent,
            context->GpuvaLabPromotion);
    }

    Bc250DisplayLog("[SubmitVirtual Node%u] GATE ABERTO: GpuvaMappingsOperational=1; publicando IB de %u bytes em VA=0x%llX no anel persistente MEC (FenceId=%llu)\n",
        (uint32_t)pSubmitCommand->NodeOrdinal,
        pSubmitCommand->DmaBufferSize,
        (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress,
        (unsigned long long)pSubmitCommand->SubmissionFenceId);

    /* The virtual MEC IB uses the dedicated compute VMID. */
    /* VMID is assigned under the engine lock immediately before publication. */

    ibSizeDwords = (uint32_t)(pSubmitCommand->DmaBufferSize / sizeof(uint32_t));

    /* 2. Adquirir visao imutavel do contexto e incrementar InFlightSubmissions */
    objStatus = Bc250McdmContextAcquireSubmissionView(
        &context->Objects,
        (BC250_MCDM_HANDLE)pSubmitCommand->hContext,
        context,
        pSubmitCommand->NodeOrdinal,
        &contextView);
    if (objStatus != BC250_MCDM_OBJECT_OK) {
        Bc250DisplayLog("[SubmitVirtual Node%u] RECUSA: Bc250McdmContextAcquireSubmissionView falhou (objStatus=%u hContext=%p)\n",
            (uint32_t)pSubmitCommand->NodeOrdinal, (uint32_t)objStatus, pSubmitCommand->hContext);
        return Bc250ObjectStatusToNtStatus(objStatus);
    }

    /*
     * 3. O snapshot logico e' somente diagnostico. Ele nao protege a page
     * table fisica: UpdatePageTable grava as PTEs antes de tentar publica-lo.
     * Segura-lo ate a fence retirar bloqueava o publicador sem acrescentar
     * qualquer garantia ao silicio. A leitura, quando disponivel, termina aqui.
     */
    vaStatus = Bc250GpuvaSnapshotAcquire(
        &context->GpuvaSnapshots, &gpuvaReader);
    if (vaStatus == BC250_GPUVA_OK) {
        vaStatus = Bc250GpuvaSpaceValidateRangeCoverage(
            gpuvaReader.Space,
            contextView.OwnerProcess,
            contextView.ContextHandle,
            (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
            (uint64_t)pSubmitCommand->DmaBufferSize,
            BC250_PTE_READABLE,
            gpuvaReader.Space->PageTableEpoch);
        Bc250DisplayLog("[SubmitVirtual Node%u] SNAPSHOT_DIAGNOSTICO: vaStatus=%u VA=0x%llX Size=%u Generation=%u\n",
            (uint32_t)pSubmitCommand->NodeOrdinal, (uint32_t)vaStatus,
            (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress,
            pSubmitCommand->DmaBufferSize,
            gpuvaReader.PublicationGeneration);
        Bc250GpuvaSnapshotRelease(&context->GpuvaSnapshots, &gpuvaReader);
    } else {
        Bc250DisplayLog("[SubmitVirtual Node%u] SNAPSHOT_DIAGNOSTICO INDISPONIVEL: vaStatus=%u; decisao pertence ao gate fisico\n",
            (uint32_t)pSubmitCommand->NodeOrdinal, (uint32_t)vaStatus);
    }

    /* 4. Gate autoritativo: validar TODAS as paginas na root deste contexto. */
    {
        BC250_MCDM_RUNTIME_CAPS physicalCaps;
        uint64_t physicalRoot = 0ull;
        uint64_t firstPhysical = 0ull;

        RtlZeroMemory(&physicalCaps, sizeof(physicalCaps));
        submitStatus = Bc250ActiveQueryRuntimeCaps(context, &physicalCaps);
        if (NT_SUCCESS(submitStatus) && physicalCaps.Ready != 0u &&
            contextView.RootPageTableSegmentId == 1u &&
            contextView.RootPageTableEntries > 0u) {
            physicalRoot = physicalCaps.VramMcBase +
                physicalCaps.LocalSegmentBaseOffset +
                contextView.RootPageTableAddress;
        }
        if (physicalRoot == 0ull ||
            !Bc250ActiveValidateGpuVaRange("NODE1_IB",
                (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
                (uint64_t)pSubmitCommand->DmaBufferSize,
                physicalRoot, physicalCaps.VramMcBase,
                BC250_PTE_READABLE, &firstPhysical)) {
            Bc250DisplayLog("[SubmitVirtual Node%u] RECUSA: gate fisico integral falhou Root=0x%llX VA=0x%llX Size=%u; silicio nao tocado\n",
                (uint32_t)pSubmitCommand->NodeOrdinal,
                (unsigned long long)physicalRoot,
                (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress,
                pSubmitCommand->DmaBufferSize);
            Bc250McdmContextReleaseSubmissionView(&context->Objects,
                (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
            return STATUS_DEVICE_NOT_READY;
        }
        Bc250DisplayLog("[SubmitVirtual Node%u] GATE FISICO PASS: Root=0x%llX VA=[0x%llX,0x%llX) FirstPA=0x%llX\n",
            (uint32_t)pSubmitCommand->NodeOrdinal,
            (unsigned long long)physicalRoot,
            (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress,
            (unsigned long long)((uint64_t)pSubmitCommand->DmaBufferVirtualAddress +
                pSubmitCommand->DmaBufferSize),
            (unsigned long long)firstPhysical);
    }

    /* 5. Reservar slot no rastreador de fences WDDM antes de tocar no anel */
    if (context->Core.DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
        context->Core.DxgkInterface.DeviceHandle == NULL) {
        Bc250DisplayLog("[SubmitVirtual Node%u] RECUSA: DxgkCbSynchronizeExecution ou DeviceHandle nulo\n",
            (uint32_t)pSubmitCommand->NodeOrdinal);
        Bc250GpuvaSnapshotRelease(&context->GpuvaSnapshots, &gpuvaReader);
        Bc250McdmContextReleaseSubmissionView(&context->Objects,
            (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
        return STATUS_DEVICE_NOT_READY;
    }

    RtlZeroMemory(&reserveSync, sizeof(reserveSync));
    reserveSync.Context = context;
    reserveSync.Operation = BC250_ACTIVE_FENCE_SYNC_RESERVE;
    reserveSync.SubmissionFenceId = pSubmitCommand->SubmissionFenceId;
    reserveSync.NodeOrdinal = pSubmitCommand->NodeOrdinal;
    reserveSync.EngineOrdinal = pSubmitCommand->EngineOrdinal;
    reserveSync.GpuvaPinValid = FALSE;

    submitStatus = context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
        context->Core.DxgkInterface.DeviceHandle,
        Bc250ActiveSynchronizedFenceCallback,
        &reserveSync,
        0,
        &reserved);
    if (!NT_SUCCESS(submitStatus) || !reserved || !reserveSync.ReserveAccepted) {
        Bc250DisplayLog("[SubmitVirtual Node%u] RECUSA: Fence reserve sync falhou (status=0x%08X reserved=%u accepted=%u)\n",
            (uint32_t)pSubmitCommand->NodeOrdinal, (uint32_t)submitStatus, reserved, reserveSync.ReserveAccepted);
        ++context->RetirementBackpressure;
        Bc250GpuvaSnapshotRelease(&context->GpuvaSnapshots, &gpuvaReader);
        Bc250McdmContextReleaseSubmissionView(&context->Objects,
            (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
        return STATUS_GRAPHICS_ALLOCATION_BUSY;
    }

    /* 6. Publicar IB no anel persistente MEC */
    submitStatus = Bc250ActivePrepareComputeVmid(context, &contextView);
    if (NT_SUCCESS(submitStatus)) {
        BC250_MCDM_RUNTIME_CAPS walkCaps;
        uint64_t resolvedIb = 0ull;
        uint64_t ibOffset;
        uint32_t word, count;
        context->Node1IbObservedVa = (uint64_t)pSubmitCommand->DmaBufferVirtualAddress;
        context->Node1IbObservedPhysical = 0ull;
        context->Node1IbRequestedBytes = pSubmitCommand->DmaBufferSize;
        context->Node1IbObservedDwords = 0u;
        RtlZeroMemory(&walkCaps, sizeof(walkCaps));
        if (NT_SUCCESS(Bc250ActiveQueryRuntimeCaps(context, &walkCaps)) &&
            walkCaps.Ready) {
            /* Observe the actual process page tables, not only the CPU
             * journal. This does not claim that the GPU fetched the IB. */
            if (Bc250ActiveWalkGpuVa("NODE1_IB",
                (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
                context->ComputeVmidBinding.Root, walkCaps.VramMcBase,
                &resolvedIb)) {
                context->Node1IbObservedPhysical = resolvedIb;
                /* Observe only an already mapped VRAM prefix. System memory
                 * and the next PTE are not assumed physically contiguous. */
                count = pSubmitCommand->DmaBufferSize / sizeof(uint32_t);
                if (count > RTL_NUMBER_OF(context->Node1IbCpuWords))
                    count = RTL_NUMBER_OF(context->Node1IbCpuWords);
                if (count > (4096u - ((uint32_t)resolvedIb & 4095u)) / sizeof(uint32_t))
                    count = (4096u - ((uint32_t)resolvedIb & 4095u)) / sizeof(uint32_t);
                if (resolvedIb >= walkCaps.VramMcBase &&
                    g_Bc250DisplayState.FrameBufferVirtual != NULL) {
                    ibOffset = resolvedIb - walkCaps.VramMcBase;
                    if (ibOffset <= g_Bc250DisplayState.FrameBufferSize &&
                        (uint64_t)count * sizeof(uint32_t) <=
                            g_Bc250DisplayState.FrameBufferSize - ibOffset) {
                        KeMemoryBarrier();
                        for (word = 0u; word < count; ++word) {
                            context->Node1IbCpuWords[word] = *(volatile uint32_t *)
                                ((PUCHAR)g_Bc250DisplayState.FrameBufferVirtual +
                                 ibOffset + (uint64_t)word * sizeof(uint32_t));
                        }
                        context->Node1IbObservedDwords = count;
                    }
                }
            }
            if (context->Node1IbObservedDwords > 0u) {
                Bc250DisplayLog("[SubmitVirtual Node1] IB_CPU_PREVIEW: VA=0x%llX PA=0x%llX Count=%u DW0=%08X DW1=%08X DW2=%08X DW3=%08X DW4=%08X DW5=%08X DW6=%08X DW7=%08X\n",
                    (unsigned long long)context->Node1IbObservedVa,
                    (unsigned long long)context->Node1IbObservedPhysical,
                    context->Node1IbObservedDwords,
                    context->Node1IbCpuWords[0],
                    (context->Node1IbObservedDwords > 1u) ? context->Node1IbCpuWords[1] : 0u,
                    (context->Node1IbObservedDwords > 2u) ? context->Node1IbCpuWords[2] : 0u,
                    (context->Node1IbObservedDwords > 3u) ? context->Node1IbCpuWords[3] : 0u,
                    (context->Node1IbObservedDwords > 4u) ? context->Node1IbCpuWords[4] : 0u,
                    (context->Node1IbObservedDwords > 5u) ? context->Node1IbCpuWords[5] : 0u,
                    (context->Node1IbObservedDwords > 6u) ? context->Node1IbCpuWords[6] : 0u,
                    (context->Node1IbObservedDwords > 7u) ? context->Node1IbCpuWords[7] : 0u);
            } else {
                Bc250DisplayLog("[SubmitVirtual Node1] IB_CPU_SKIP: Resolved=0x%llX McBase=0x%llX FbSize=%u FbVirt=%p\n",
                    (unsigned long long)resolvedIb,
                    (unsigned long long)walkCaps.VramMcBase,
                    g_Bc250DisplayState.FrameBufferSize,
                    g_Bc250DisplayState.FrameBufferVirtual);
            }
        }
        Bc250DisplayLog("[SubmitVirtual Node%u] INICIANDO PUBLICACAO: DmaVA=0x%llX SizeDW=%u FenceId=%llu\n",
            (uint32_t)pSubmitCommand->NodeOrdinal,
            (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress,
            ibSizeDwords,
            (unsigned long long)pSubmitCommand->SubmissionFenceId);
    }
    if (NT_SUCCESS(submitStatus)) {
        Bc250ActiveCaptureMecTelemetry(context, "MEC_PRE_SUBMIT");
    }
    if (NT_SUCCESS(submitStatus)) submitStatus = Bc250SubmissionEngineSubmit(
        context,
        (uint32_t)pSubmitCommand->NodeOrdinal,
        (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
        ibSizeDwords,
        &assignedFence);
    if (NT_SUCCESS(submitStatus)) {
        Bc250ActiveCaptureMecTelemetry(context, "MEC_POST_DOORBELL");
    }
    Bc250SubmissionEngineRecordTelemetry(context);
    if (NT_SUCCESS(submitStatus)) {
        Bc250DisplayLog("[SubmitVirtual Node%u] SUBMIT SUCESSO: AssignedFence=%llu RingWptr=%u WptrMmio=%u\n",
            (uint32_t)pSubmitCommand->NodeOrdinal,
            (unsigned long long)assignedFence,
            context->SubmissionEngine.ComputeRing.WptrDwords,
            context->SubmissionEngine.LastWptrMmioReadback);
        Bc250DisplayLog("[SubmitVirtual Node%u] DESCRIPTOR: Raw=%08X,%08X,%08X,%08X\n",
            (uint32_t)pSubmitCommand->NodeOrdinal,
            context->SubmissionEngine.DiagnosticDescriptor[0],
            context->SubmissionEngine.DiagnosticDescriptor[1],
            context->SubmissionEngine.DiagnosticDescriptor[2],
            context->SubmissionEngine.DiagnosticDescriptor[3]);
    }
    if (!NT_SUCCESS(submitStatus)) {
        Bc250DisplayLog("[SubmitVirtual Node%u] RECUSA DETALHADA: submitStatus=0x%08X (Step=%u DbCtrl=0x%08X ExpOff=0x%X Act=%u Rptr=%u Err=0x%08X Virt=%p Size=0x%X Failures=%u)\n",
            (uint32_t)pSubmitCommand->NodeOrdinal, (uint32_t)submitStatus,
            context->SubmissionEngine.PublishFailStep,
            context->SubmissionEngine.PublishReadbackDoorbellControl,
            context->SubmissionEngine.DoorbellOffsetBytes,
            context->SubmissionEngine.PublishHqdActive,
            context->SubmissionEngine.PublishHqdRptr,
            context->SubmissionEngine.PublishHqdError,
            context->SubmissionEngine.DoorbellVirtual,
            context->SubmissionEngine.Buffers.DoorbellSizeBytes,
            context->SubmissionEngine.DoorbellWriteFailures);
        /* Cancelar reserva imediatamente em caso de falha de publicacao */
        BC250_ACTIVE_FENCE_SYNC cancelSync;
        BOOLEAN cancelled = FALSE;
        RtlZeroMemory(&cancelSync, sizeof(cancelSync));
        cancelSync.Context = context;
        cancelSync.Operation = BC250_ACTIVE_FENCE_SYNC_CANCEL;
        cancelSync.ReservationToken = reserveSync.ReservationToken;
        (void)context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
            context->Core.DxgkInterface.DeviceHandle,
            Bc250ActiveSynchronizedFenceCallback,
            &cancelSync,
            0,
            &cancelled);
        Bc250GpuvaSnapshotRelease(&context->GpuvaSnapshots, &gpuvaReader);
        Bc250McdmContextReleaseSubmissionView(&context->Objects,
            (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
        return submitStatus;
    }

    /* 7. Commit definitivo da fence fisica atribuida */
    {
        BC250_ACTIVE_FENCE_SYNC commitSync;
        BOOLEAN committed = FALSE;
        RtlZeroMemory(&commitSync, sizeof(commitSync));
        commitSync.Context = context;
        commitSync.Operation = BC250_ACTIVE_FENCE_SYNC_COMMIT;
        commitSync.ReservationToken = reserveSync.ReservationToken;
        commitSync.RingFence = assignedFence;

        submitStatus = context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
            context->Core.DxgkInterface.DeviceHandle,
            Bc250ActiveSynchronizedFenceCallback,
            &commitSync,
            0,
            &committed);
        if (NT_SUCCESS(submitStatus) && committed && commitSync.CommitAccepted) {
            Bc250DisplayLog("[SubmitVirtual Node%u] FENCE COMMIT: Token=%llu RingFence=%llu committed=%u accepted=%u status=0x%08X\n",
                (uint32_t)pSubmitCommand->NodeOrdinal,
                (unsigned long long)commitSync.ReservationToken,
                (unsigned long long)commitSync.RingFence,
                committed,
                commitSync.CommitAccepted,
                (uint32_t)submitStatus);
        }
        if (!NT_SUCCESS(submitStatus) || !committed || !commitSync.CommitAccepted) {
            Bc250DisplayLog("[SubmitVirtual Node%u] RECUSA: Fence commit sync falhou (status=0x%08X committed=%u accepted=%u)\n",
                (uint32_t)pSubmitCommand->NodeOrdinal, (uint32_t)submitStatus, committed, commitSync.CommitAccepted);
            ++context->RetirementPushFailures;
            InterlockedExchange(&context->SubmissionEngine.State,
                BC250_SUBMISSION_ENGINE_FAULTED);
            Bc250GpuvaSnapshotRelease(
                &context->GpuvaSnapshots, &gpuvaReader);
            Bc250McdmContextReleaseSubmissionView(&context->Objects,
                (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
            return STATUS_DEVICE_DATA_ERROR;
        }
        /* O snapshot diagnostico ja foi liberado antes do gate fisico. */
    }

    /* 8. Iniciar retirada e liberar as duas referencias imutaveis. */
    /*
     * Nao rearmar depois que StopDevice fechou o portao. Se a leitura de
     * RUNNING competir com STOPPING, a segunda parada feita pela quiescencia
     * depois de SubmitInFlight==0 garante o cancelamento definitivo.
     */
    if (InterlockedCompareExchange(&context->LifecycleState, 0, 0) ==
            (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING) {
        Bc250ActiveRetirementTimerStart(context);
    }
    Bc250McdmContextReleaseSubmissionView(&context->Objects,
        (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
    Bc250DisplayLog("[SubmitVirtual Node%u] PUBLICADO NO ANEL MEC: FenceId=%llu AssignedFence=%llu DmaVA=0x%llX DmaSize=%u\n",
        (uint32_t)pSubmitCommand->NodeOrdinal,
        (unsigned long long)pSubmitCommand->SubmissionFenceId,
        (unsigned long long)assignedFence,
        (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress,
        pSubmitCommand->DmaBufferSize);
    InterlockedIncrement((volatile LONG *)&context->SubmissionsCount);
    Bc250SubmissionEngineRecordTelemetry(context);
    return STATUS_SUCCESS;
}

/*
 * Invólucro de ciclo de vida do SubmitCommandVirtual (3.67).
 *
 * Duas responsabilidades, ambas necessarias para que o StopDevice consiga
 * fotografar o rastreador com confianca:
 *
 *   1. Recusar submissao nova assim que o contexto entra em STOPPING.
 *   2. Contabilizar submissoes em voo, para que o dreno do teardown saiba
 *      esperar as que ja passaram pelo portao.
 *
 * O corpo ficou em Bc250ActiveSubmitCommandVirtualBody justamente para que o
 * decremento tenha um unico ponto de saida — espalhar InterlockedDecrement
 * pelos dezoito retornos do corpo seria a forma classica de vazar a contagem
 * em um caminho de erro e travar o teardown para sempre.
 */
BOOLEAN Bc250ActiveSubmissionGateEnter(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return FALSE;
    }
    if (InterlockedCompareExchange(&Context->LifecycleState, 0, 0) !=
            (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING) {
        return FALSE;
    }
    (void)InterlockedIncrement(&Context->SubmitInFlight);
    if (InterlockedCompareExchange(&Context->LifecycleState, 0, 0) !=
            (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING) {
        (void)InterlockedDecrement(&Context->SubmitInFlight);
        return FALSE;
    }
    return TRUE;
}

void Bc250ActiveSubmissionGateLeave(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    if (Context != NULL && Context->Signature == BC250_ACTIVE_SIGNATURE) {
        (void)InterlockedDecrement(&Context->SubmitInFlight);
    }
}

static NTSTATUS Bc250ActiveSubmitCommandVirtualLifecycle(
    HANDLE AdapterContext,
    CONST DXGKARG_SUBMITCOMMANDVIRTUAL *pSubmitCommand)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)AdapterContext;
    NTSTATUS status;

    if (context == NULL || context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Bc250ActiveSubmissionGateEnter(context)) {
        return STATUS_DEVICE_NOT_READY;
    }

    Bc250ProbeMarkPostStartStage(BC250_V5_POST_START_SUBMIT);
    /*
     * DxgkDdiSubmitCommandVirtual executa em DISPATCH_LEVEL (IRQL 2):
     * push locks (ObjectLock) e KeEnterCriticalRegion são proibidos (BugCheck 0xD1).
     */
    status = Bc250ActiveSubmitCommandVirtualBody(AdapterContext, pSubmitCommand);
    Bc250ActiveSubmissionGateLeave(context);
    return status;
}

static uint32_t Bc250ActiveFlushMappedGfxhubTlb(void *Context);

NTSTATUS Bc250ActiveCurrentFenceProvider(
    _In_ HANDLE hAdapter,
    _In_ UINT NodeOrdinal,
    _Out_ UINT *pCurrentFence)
{
    BC250_DXGK_ACTIVE_CONTEXT *active = (BC250_DXGK_ACTIVE_CONTEXT *)hAdapter;
    BC250_ACTIVE_FENCE_SYNC sync;
    BOOLEAN queried = FALSE;
    NTSTATUS status;

    if (active == NULL || pCurrentFence == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (active->Signature != BC250_ACTIVE_SIGNATURE ||
        active->SubmissionEngine.State != BC250_SUBMISSION_ENGINE_ONLINE) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (NodeOrdinal > BC250_SUBMISSION_NODE_COMPUTE ||
        active->Core.DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
        active->Core.DxgkInterface.DeviceHandle == NULL) {
        return STATUS_NOT_SUPPORTED;
    }
    RtlZeroMemory(&sync, sizeof(sync));
    sync.Context = active;
    sync.Operation = BC250_ACTIVE_FENCE_SYNC_QUERY;
    sync.NodeOrdinal = NodeOrdinal;
    status = active->Core.DxgkInterface.DxgkCbSynchronizeExecution(
        active->Core.DxgkInterface.DeviceHandle,
        Bc250ActiveSynchronizedFenceCallback,
        &sync, 0, &queried);
    if (!NT_SUCCESS(status) || !queried) return STATUS_DEVICE_NOT_READY;
    *pCurrentFence = sync.CurrentFence;
    return STATUS_SUCCESS;
}

/* Calcula prontidão do candidato, mas não autoriza produção. ObjectLock held. */
static LONG Bc250ActiveEvaluateGpuvaActivationEligibility(
    BC250_DXGK_ACTIVE_CONTEXT *active,
    BC250_MCDM_HANDLE Process)
{
    LONG slot;
    const BC250_GPUVA_SPACE *space = NULL;
    uint32_t blocked = 0u;

    if (active == NULL) return 0;
    /*
     * C3.9a: telemetria pura -- QUAIS slots estao pendentes, e quantos. Antes o
     * log dizia apenas "JOURNAL=1", o que nao distinguia "o meu processo esta
     * paginando" de "o dwm.exe esta paginando". Nao decide nada.
     */
    (void)Bc250GpuvaJournalSetDescribePending(active->GpuvaJournals,
        BC250_ACTIVE_GPUVA_JOURNAL_SLOTS,
        &active->GpuvaJournalPendingSlotMask,
        &active->GpuvaJournalPendingCount);
    /*
     * C3.9b: granularidade do bloqueio.
     *
     * Default (tunable em 0): condicao GLOBAL -- qualquer slot pendente fecha o
     * gate. E' o comportamento medido na corrida 13:29, em que o dwm.exe vivo
     * manteve o journal ocupado por 3 min 13 s e o compute do RADV nunca pode
     * submeter.
     *
     * Armado (tunable em 1) e com processo conhecido: bloqueia apenas se o
     * PROCESSO QUE ESTA FLUSHANDO tiver pendencia propria. Cada slot ja carrega
     * Journal.Process, entao a vista por processo nao inventa dado nenhum.
     *
     * RELAXAMENTO DECLARADO, NAO VALIDADO EM HARDWARE: se a pendencia de OUTRO
     * processo puder afetar a traducao deste, esta troca e' insegura. O
     * argumento a favor e' que as raizes de page table sao por processo; o
     * argumento nao e' prova. Por isso e' fail-closed por default.
     */
    if (active->Node1JournalGatePerProcessPresent == 1u &&
        active->Node1JournalGatePerProcess == 1u &&
        Process != NULL) {
        if (Bc250GpuvaJournalSetAnyPendingForProcess(active->GpuvaJournals,
                BC250_ACTIVE_GPUVA_JOURNAL_SLOTS, Process) != 0u)
            blocked |= BC250_GPUVA_GATE_BLOCK_JOURNAL_PENDING;
    } else if (Bc250GpuvaJournalSetAnyPending(active->GpuvaJournals,
            BC250_ACTIVE_GPUVA_JOURNAL_SLOTS) != 0u) {
        blocked |= BC250_GPUVA_GATE_BLOCK_JOURNAL_PENDING;
    }
    if (active->GpuvaHierarchyPending != 0u)
        blocked |= BC250_GPUVA_GATE_BLOCK_HIERARCHY_PENDING;
    if (InterlockedCompareExchange(&active->IsActive, 1, 1) != 1)
        blocked |= BC250_GPUVA_GATE_BLOCK_ADAPTER_INACTIVE;
    if (InterlockedCompareExchange(&active->ContextAbiReady, 1, 1) != 1)
        blocked |= BC250_GPUVA_GATE_BLOCK_CONTEXT_ABI;
    if (InterlockedCompareExchange(&active->AllocationAbiReady, 1, 1) != 1)
        blocked |= BC250_GPUVA_GATE_BLOCK_ALLOCATION_ABI;
    if (InterlockedCompareExchange(&active->RootPageTableAbiReady, 1, 1) != 1)
        blocked |= BC250_GPUVA_GATE_BLOCK_ROOT_ABI;
    if (InterlockedCompareExchange(&active->SubmissionEngine.State,
            BC250_SUBMISSION_ENGINE_ONLINE,
            BC250_SUBMISSION_ENGINE_ONLINE) != BC250_SUBMISSION_ENGINE_ONLINE)
        blocked |= BC250_GPUVA_GATE_BLOCK_ENGINE_OFFLINE;
    slot = InterlockedCompareExchange(&active->GpuvaSnapshots.ActiveSlot, 0, 0);
    if (slot < 0 || slot > 1) {
        blocked |= BC250_GPUVA_GATE_BLOCK_SNAPSHOT_SLOT;
    } else {
        space = &active->GpuvaSnapshots.Slots[(uint32_t)slot];
        active->GpuvaActivationSnapshotGeneration = (uint32_t)
            InterlockedCompareExchange(
                &active->GpuvaSnapshots.SlotGeneration[(uint32_t)slot], 0, 0);
        active->GpuvaActivationSnapshotEpoch = space->PageTableEpoch;
        active->GpuvaActivationSnapshotLiveCount = space->LiveCount;
        if (space->Signature != BC250_GPUVA_SPACE_SIGNATURE)
            blocked |= BC250_GPUVA_GATE_BLOCK_SNAPSHOT_SIGNATURE;
        if (space->LiveCount == 0u)
            blocked |= BC250_GPUVA_GATE_BLOCK_SNAPSHOT_EMPTY;
        if (space->PageTableEpoch == 0u)
            blocked |= BC250_GPUVA_GATE_BLOCK_SNAPSHOT_EPOCH;
    }
    active->GpuvaActivationBlockMask = blocked;
    return blocked == 0u ? 1 : 0;
}

static void Bc250ActiveRecordGpuvaGateTelemetry(
    ULONG eligible, ULONG operational, ULONG blocked,
    ULONG generation, ULONG epoch, ULONG liveCount)
{
    Bc250ProbeRecord(BC250_PROBE_GPUVA_GATE_ELIGIBLE, eligible);
    Bc250ProbeRecord(BC250_PROBE_GPUVA_GATE_OPERATIONAL, operational);
    Bc250ProbeRecord(BC250_PROBE_GPUVA_GATE_BLOCK_MASK, blocked);
    Bc250ProbeRecord(BC250_PROBE_GPUVA_SNAPSHOT_GENERATION, generation);
    Bc250ProbeRecord(BC250_PROBE_GPUVA_SNAPSHOT_EPOCH, epoch);
    Bc250ProbeRecord(BC250_PROBE_GPUVA_SNAPSHOT_LIVE_COUNT, liveCount);
}

/*
 * The VidMm root address is an offset inside segment 1, where zero is valid.
 * The journal contract historically reserves zero for "root not known yet".
 * Encode an established segment-1 root as offset+1 when it crosses that
 * boundary, preserving zero exclusively as the unknown sentinel.
 */
static uint64_t Bc250ActiveGpuvaRootKey(
    uint32_t SegmentId,
    uint64_t SegmentOffset)
{
    if (SegmentId != 1u || SegmentOffset == (uint64_t)~0ull) return 0ull;
    return SegmentOffset + 1ull;
}

NTSTATUS Bc250ActiveFlushTlbHandler(
    _In_ HANDLE hAdapter,
    _In_ CONST struct _DXGK_BUILDPAGINGBUFFER_FLUSHTLB *pFlushTlb)
{
    BC250_DXGK_ACTIVE_CONTEXT *active = (BC250_DXGK_ACTIVE_CONTEXT *)hAdapter;
    BC250_GPUVA_JOURNAL_STATUS journalStatus;
    uint32_t generation = 0u;
    ULONG telemetryEligible, telemetryOperational, telemetryBlocked;
    ULONG telemetryGeneration, telemetryEpoch, telemetryLiveCount;
    uint64_t flushRootKey;

    if (active == NULL || pFlushTlb == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (active->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    flushRootKey = Bc250ActiveGpuvaRootKey(
        pFlushTlb->RootPageTableAddress.SegmentId,
        pFlushTlb->RootPageTableAddress.SegmentOffset);

    Bc250DisplayLog("[ActiveFlushTlb] RootSeg=%u, RootOff=0x%llX, StartVA=0x%llX, EndVA=0x%llX, hProc=0x%p, Pending=%u, HierPending=%u\n",
        pFlushTlb->RootPageTableAddress.SegmentId,
        (unsigned long long)pFlushTlb->RootPageTableAddress.SegmentOffset,
        (unsigned long long)pFlushTlb->StartVirtualAddress,
        (unsigned long long)pFlushTlb->EndVirtualAddress,
        pFlushTlb->hProcess,
        Bc250GpuvaJournalSetAnyPending(active->GpuvaJournals,
            BC250_ACTIVE_GPUVA_JOURNAL_SLOTS),
        active->GpuvaHierarchyPending);

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&active->ObjectLock);
    if (InterlockedCompareExchange(&active->GpuvaJournalReady, 1, 1) != 1) {
        ExReleasePushLockExclusive(&active->ObjectLock);
        KeLeaveCriticalRegion();
        Bc250DisplayLog("[ActiveFlushTlb] GpuvaJournalReady != 1 -> Handled (SUCCESS)\n");
        return STATUS_SUCCESS;
    }
    BOOLEAN canCommitJournal = FALSE;
    BC250_GPUVA_JOURNAL *flushJournal = Bc250GpuvaJournalSetSelect(
        active->GpuvaJournals, BC250_ACTIVE_GPUVA_JOURNAL_SLOTS,
        (BC250_MCDM_HANDLE)pFlushTlb->hProcess,
        flushRootKey, 0u, NULL);
    if (flushJournal != NULL && flushJournal->Pending != 0u &&
        (flushJournal->ExpectedRootPageTable == 0u ||
         flushJournal->ExpectedRootPageTable == flushRootKey) &&
        flushRootKey != 0ull) {
        canCommitJournal = TRUE;
    }
    if (active->GpuvaHierarchyPending != 0u &&
        (active->GpuvaHierarchyProcess != (BC250_MCDM_HANDLE)pFlushTlb->hProcess ||
         active->GpuvaHierarchyRootPageTable != flushRootKey)) {
        Bc250DisplayLog("[ActiveFlushTlb] Hierarchy process mismatch -> Resetting hierarchy\n");
        active->GpuvaHierarchyPending = 0u;
        active->GpuvaHierarchyProcess = NULL;
        active->GpuvaHierarchyRootPageTable = 0u;
        active->GpuvaHierarchyHighestLevel = 0u;
    }
    if (Bc250ActiveFlushMappedGfxhubTlb(active) == 0u) {
        InterlockedExchange(&active->GpuvaMappingsOperational, 0);
        ExReleasePushLockExclusive(&active->ObjectLock);
        KeLeaveCriticalRegion();
        Bc250DisplayLog("[ActiveFlushTlb] FlushMappedGfxhubTlb failed -> internal fail-closed (SUCCESS)\n");
        return STATUS_SUCCESS;
    }
    if (!canCommitJournal) {
        active->GpuvaHierarchyPending = 0u;
        active->GpuvaHierarchyProcess = NULL;
        active->GpuvaHierarchyRootPageTable = 0u;
        active->GpuvaHierarchyHighestLevel = 0u;
        InterlockedExchange(&active->GpuvaActivationEligible, 0);
        active->GpuvaActivationBlockMask =
            BC250_GPUVA_GATE_BLOCK_NO_LEAF_COMMIT;
        active->GpuvaActivationSnapshotGeneration = (uint32_t)
            InterlockedCompareExchange(
                &active->GpuvaSnapshots.PublicationGeneration, 0, 0);
        telemetryEligible = 0u;
        telemetryOperational = (ULONG)InterlockedCompareExchange(
            &active->GpuvaMappingsOperational, 0, 0);
        telemetryBlocked = active->GpuvaActivationBlockMask;
        telemetryGeneration = active->GpuvaActivationSnapshotGeneration;
        telemetryEpoch = active->GpuvaActivationSnapshotEpoch;
        telemetryLiveCount = active->GpuvaActivationSnapshotLiveCount;
        ExReleasePushLockExclusive(&active->ObjectLock);
        KeLeaveCriticalRegion();
        Bc250ActiveRecordGpuvaGateTelemetry(telemetryEligible,
            telemetryOperational, telemetryBlocked, telemetryGeneration,
            telemetryEpoch, telemetryLiveCount);
        Bc250DisplayLog("[ActiveFlushTlb] (Pending=%u, CanCommit=%u) Finished -> SUCCESS\n",
            flushJournal != NULL ? flushJournal->Pending : 0u,
            canCommitJournal ? 1u : 0u);
        return STATUS_SUCCESS;
    }
    journalStatus = Bc250GpuvaJournalCommitFlush(flushJournal,
        &active->GpuvaSnapshots, &active->GpuvaSnapshotScratch,
        (BC250_MCDM_HANDLE)pFlushTlb->hProcess,
        flushRootKey,
        0u, 0u,
        &generation);
    if (journalStatus == BC250_GPUVA_JOURNAL_OK) {
        active->GpuvaHierarchyPending = 0u;
        active->GpuvaHierarchyProcess = NULL;
        active->GpuvaHierarchyRootPageTable = 0u;
        active->GpuvaHierarchyHighestLevel = 0u;
        InterlockedExchange(&active->GpuvaActivationEligible,
            Bc250ActiveEvaluateGpuvaActivationEligibility(active,
                (BC250_MCDM_HANDLE)pFlushTlb->hProcess));
        if (active->GpuvaLabPromotionPresent == 1u &&
            active->GpuvaLabPromotion == 1u &&
            InterlockedCompareExchange(
                &active->GpuvaActivationEligible, 0, 0) == 1) {
            InterlockedExchange(&active->GpuvaMappingsOperational, 1);
            /* VMID9 is selected at submission, not by a paging callback. */
        } else {
            InterlockedExchange(&active->GpuvaMappingsOperational, 0);
            /* Admission closes without modifying an in-flight ring descriptor. */
        }
    } else {
        InterlockedExchange(&active->GpuvaActivationEligible,
            Bc250ActiveEvaluateGpuvaActivationEligibility(active,
                (BC250_MCDM_HANDLE)pFlushTlb->hProcess));
    }
    telemetryEligible = (ULONG)InterlockedCompareExchange(
        &active->GpuvaActivationEligible, 0, 0);
    telemetryOperational = (ULONG)InterlockedCompareExchange(
        &active->GpuvaMappingsOperational, 0, 0);
    telemetryBlocked = active->GpuvaActivationBlockMask;
    telemetryGeneration = active->GpuvaActivationSnapshotGeneration;
    telemetryEpoch = active->GpuvaActivationSnapshotEpoch;
    telemetryLiveCount = active->GpuvaActivationSnapshotLiveCount;
    ExReleasePushLockExclusive(&active->ObjectLock);
    KeLeaveCriticalRegion();
    if (journalStatus != BC250_GPUVA_JOURNAL_OK) {
        Bc250GpuvaJournalAbort(flushJournal);
        InterlockedExchange(&active->GpuvaMappingsOperational, 0);
        telemetryOperational = 0u;
        Bc250ActiveRecordGpuvaGateTelemetry(telemetryEligible,
            telemetryOperational, telemetryBlocked, telemetryGeneration,
            telemetryEpoch, telemetryLiveCount);
        /*
         * C3.8 (12/09): "Journal status=7" colapsava DUAS causas -- contencao
         * transitoria com o leitor do snapshot (BUSY) e erro real de
         * substituicao de faixa. Sem separar, nao havia como dizer se retentar
         * resolve. Os tres campos abaixo vem do Store e nomeiam a causa.
         */
        /*
         * C3.12 (12/09): ocupacao por estado no momento da falha.
         *
         * PublishStatus=3 e BC250_GPUVA_NO_SPACE -- a publicacao falha por
         * exaustao do teto antigo de 256 slots, medido na
         * corrida 16:31 com Attempts=1 Busy=0 (o publicador nunca foi
         * bloqueado; a hipotese de contencao da C3.8 esta REFUTADA).
         *
         * O numero sozinho nao diz se sao 256 faixas legitimamente em uso ou
         * slots presos em TEARDOWN e nunca reciclados -- Bc250GpuvaSpaceInsertRange
         * so reaproveita FREE e UNMAPPED. Reusable espelha esse criterio: se
         * vier 0 com Teardown alto, o defeito e reciclagem, nao tamanho, e
         * aumentar o limite apenas mascararia.
         *
         * Leitura pura sobre o slot ativo, fora do pushlock (liberado acima).
         */
        {
            BC250_GPUVA_SPACE_OCCUPANCY occupancy = { 0, 0, 0, 0, 0, 0, 0 };
            /*
             * C3.12: leitura PURA. A versao anterior amostrava com
             * InterlockedCompareExchange(&ActiveSlot, 0, 0), que no x86 executa
             * "lock cmpxchg" e portanto uma ESCRITA TRAVADA mesmo sem trocar
             * valor. Telemetria nao pode usar read-modify-write sobre um campo
             * cujo dono e outra thread. O campo e volatile long; ler basta.
             */
            LONG occSlot = active->GpuvaSnapshots.ActiveSlot;
            BC250_GPUVA_STATUS occStatus = BC250_GPUVA_BAD_ARGUMENT;
            occupancy.Capacity = BC250_GPUVA_SPACE_MAX_RANGES;
            if (occSlot >= 0 && occSlot <= 1) {
                occStatus = Bc250GpuvaSpaceDescribeOccupancy(
                    &active->GpuvaSnapshots.Slots[occSlot], &occupancy);
            }
            Bc250DisplayLog("[ActiveFlushTlb] Journal status=0x%08X after HW GFXHUB flush -> STATUS_IO_DEVICE_ERROR "
                "(PublishStatus=%ld Attempts=%ld Busy=%ld)\n",
                journalStatus,
                active->GpuvaSnapshots.LastPublishStatus,
                active->GpuvaSnapshots.LastPublishAttempts,
                active->GpuvaSnapshots.LastPublishBusyCount);
            Bc250DisplayLog("[ActiveFlushTlb] OCUPACAO slot=%ld st=%u Cap=%u Mapped=%u Teardown=%u Reusable=%u Live=%u\n",
                occSlot, (uint32_t)occStatus,
                occupancy.Capacity, occupancy.Mapped,
                occupancy.Teardown, occupancy.Reusable, occupancy.LiveCount);
        }
        return STATUS_IO_DEVICE_ERROR;
    }
    /* GpuvaMappingsOperational permanece zero até o ensaio integrado. */
    Bc250ActiveRecordGpuvaGateTelemetry(telemetryEligible,
        telemetryOperational, telemetryBlocked, telemetryGeneration,
        telemetryEpoch, telemetryLiveCount);
    Bc250DisplayLog("[ActiveFlushTlb] Journal commit GLOBAL after physical GFXHUB invalidation: Generation=%u -> SUCCESS\n",
        generation);
    return STATUS_SUCCESS;
}

static BOOLEAN Bc250ActiveResolveDisplayGpuVa(
    HANDLE ContextHandle,
    UINT64 GpuVa,
    UINT64 *PhysicalAddress,
    UINT32 *SegmentId)
{
    uint32_t index;
    BOOLEAN resolved = FALSE;

    if (PhysicalAddress != NULL) *PhysicalAddress = 0ull;
    if (SegmentId != NULL) *SegmentId = 0u;
    if (GpuVa == 0ull || PhysicalAddress == NULL || SegmentId == NULL) {
        return FALSE;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    for (index = 0u; index < BC250_ACTIVE_MAX_ADAPTERS; ++index) {
        BC250_DXGK_ACTIVE_CONTEXT *active = g_Bc250AdapterDirectory[index];
        BC250_MCDM_OBJECT_RECORD contextRecord;
        BC250_MCDM_OBJECT_STATUS objectStatus;
        BC250_GPUVA_SNAPSHOT_READER reader;
        BC250_GPUVA_STATUS resolveStatus;
        BC250_MCDM_HANDLE ownerProcess = NULL;

        if (active == NULL || active->Signature != BC250_ACTIVE_SIGNATURE ||
            InterlockedCompareExchange(&active->IsActive, 1, 1) != 1) {
            continue;
        }

        if (ContextHandle != NULL) {
            ExAcquirePushLockExclusive(&active->ObjectLock);
            objectStatus = Bc250McdmObjectLookup(&active->Objects,
                (BC250_MCDM_HANDLE)ContextHandle, BC250_MCDM_OBJECT_NONE,
                NULL, &contextRecord);
            ExReleasePushLockExclusive(&active->ObjectLock);
            if (objectStatus == BC250_MCDM_OBJECT_OK) {
                if (contextRecord.Type == (uint32_t)BC250_MCDM_OBJECT_PROCESS) {
                    ownerProcess = (BC250_MCDM_HANDLE)ContextHandle;
                } else {
                    ownerProcess = contextRecord.OwnerProcess;
                }
            } else {
                Bc250DisplayLog("[ResolveGpuVa] Handle=%p lookup status=%u\n",
                    ContextHandle, (UINT32)objectStatus);
            }
        }

        RtlZeroMemory(&reader, sizeof(reader));
        resolveStatus = Bc250GpuvaSnapshotAcquire(
            &active->GpuvaSnapshots, &reader);
        if (resolveStatus != BC250_GPUVA_OK) {
            Bc250DisplayLog("[ResolveGpuVa] SnapshotAcquire failed: status=%u\n",
                (UINT32)resolveStatus);
            break;
        }

        if (ownerProcess != NULL) {
            resolveStatus = Bc250GpuvaSpaceResolveAddress(reader.Space,
                ownerProcess, GpuVa, BC250_PTE_READABLE,
                SegmentId, PhysicalAddress);
            if (resolveStatus == BC250_GPUVA_OK) {
                resolved = TRUE;
                Bc250DisplayLog("[ResolveGpuVa] SUCCESS: Hdl=%p Proc=%p VA=0x%llX -> Phys=0x%llX Seg=%u\n",
                    ContextHandle, ownerProcess, GpuVa, *PhysicalAddress, *SegmentId);
            } else {
                Bc250DisplayLog("[ResolveGpuVa] SpaceResolveAddress failed: status=%u (Proc=%p VA=0x%llX)\n",
                    (UINT32)resolveStatus, ownerProcess, GpuVa);
            }
        }

        /* Varredura direta no snapshot de GPUVA se nao resolveu via OwnerProcess estrito */
        if (!resolved && reader.Space != NULL && reader.Space->Teardown == 0u) {
            uint32_t i;
            for (i = 0u; i < reader.Space->SortedCount; ++i) {
                const BC250_GPUVA_RANGE_RECORD *record =
                    &reader.Space->Ranges[reader.Space->SortedIndices[i]];
                if (record->BaseGpuVa > GpuVa) break;
                if (GpuVa >= record->EndGpuVa) continue;
                if (record->State == BC250_GPUVA_RANGE_MAPPED &&
                    record->IsResident != 0u &&
                    record->TeardownState == 0u) {
                    uint64_t withinRange = GpuVa - record->BaseGpuVa;
                    *SegmentId = record->BackingSegmentId;
                    *PhysicalAddress = record->BackingSegmentOffset + withinRange;
                    resolved = TRUE;
                    Bc250DisplayLog("[ResolveGpuVa] RANGE_SCAN SUCCESS: VA=0x%llX -> Phys=0x%llX Seg=%u (Proc=%p Base=0x%llX End=0x%llX)\n",
                        GpuVa, *PhysicalAddress, *SegmentId, record->OwnerProcess,
                        record->BaseGpuVa, record->EndGpuVa);
                    break;
                }
            }
        }

        Bc250GpuvaSnapshotRelease(&active->GpuvaSnapshots, &reader);
        if (resolved) {
            break;
        }
    }
    ExReleasePushLockExclusive(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return resolved;
}

static BC250_GPUVA_JOURNAL_STATUS Bc250ActiveInitializeJournalSet(
    BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    uint32_t slot;
    if (Context == NULL) return BC250_GPUVA_JOURNAL_BAD_ARGUMENT;
    for (slot = 0u; slot < BC250_ACTIVE_GPUVA_JOURNAL_SLOTS; ++slot) {
        BC250_GPUVA_JOURNAL_STATUS status =
            Bc250GpuvaJournalInitialize(&Context->GpuvaJournals[slot]);
        if (status != BC250_GPUVA_JOURNAL_OK) return status;
    }
    return BC250_GPUVA_JOURNAL_OK;
}

static NTSTATUS Bc250ActiveUpdatePageTableHandler(
    _In_ HANDLE hAdapter,
    _In_ CONST BC250_MCDM_RUNTIME_CAPS *Caps,
    _In_ DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update)
{
    BC250_DXGK_ACTIVE_CONTEXT *active = (BC250_DXGK_ACTIVE_CONTEXT *)hAdapter;
    BC250_MCDM_OBJECT_RECORD processRecord;
    BC250_GPUVA_JOURNAL_PTE *entries;
    BC250_GPUVA_JOURNAL_STATUS journalStatus;
    BC250_GPUVA_JOURNAL *leafJournal = NULL;
    uint32_t evictionKind = BC250_GPUVA_JOURNAL_EVICT_NONE;
    UINT32 index;
    UINT32 chunkStart = 0u;
    UINT32 chunkCount = 0u;
    UINT64 *destination;
    UINT64 rootKey;

    if (active == NULL || Caps == NULL || update == NULL ||
        active->Signature != BC250_ACTIVE_SIGNATURE ||
        update->PageTableLevel >= Caps->PageTableLevelCount ||
        (update->PageTableLevel == 0u &&
         (update->NumPageTableEntries == 0u ||
          update->pPageTableEntries == NULL))) {
        Bc250DisplayLog("[ActiveUpdatePT] Rejected parameters: Level=%u, FirstPteVA=0x%llX, Num=%u, pPTE=0x%p\n",
            update ? update->PageTableLevel : 0,
            update ? (unsigned long long)update->FirstPteVirtualAddress : 0,
            update ? update->NumPageTableEntries : 0,
            update ? update->pPageTableEntries : NULL);
        return STATUS_INVALID_PARAMETER;
    }
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&active->ObjectLock);
    entries = active->GpuvaJournalStaging;

    if (update->hProcess == NULL ||
        InterlockedCompareExchange(&active->GpuvaJournalReady, 1, 1) != 1 ||
        Bc250McdmObjectLookup(&active->Objects,
            (BC250_MCDM_HANDLE)update->hProcess, BC250_MCDM_OBJECT_PROCESS,
            (BC250_MCDM_HANDLE)update->hProcess, &processRecord) !=
                BC250_MCDM_OBJECT_OK) {
        /*
         * Para o processo de paginacao do sistema ou processos sem registro no objeto,
         * o tracking de journal e' ignorado, mas as PTEs continuam sendo gravadas
         * no destino fisico em RAM/VRAM para a GPU.
         */
        leafJournal = NULL;
        rootKey = 0ull;
        journalStatus = BC250_GPUVA_JOURNAL_OK;
    } else {
        Bc250DisplayLog("[ActiveUpdatePT] Lvl=%u, FirstPteVA=0x%llX, hProc=0x%p, RootAddr=0x%llX, SegId=%u\n",
            update->PageTableLevel, (unsigned long long)update->FirstPteVirtualAddress,
            update->hProcess,
            (unsigned long long)processRecord.RootPageTableAddress,
            processRecord.RootPageTableSegmentId);
        rootKey = processRecord.RootPageTableSetCount != 0u ?
            Bc250ActiveGpuvaRootKey(processRecord.RootPageTableSegmentId,
                processRecord.RootPageTableAddress) : 0ull;
        if (active->GpuvaHierarchyPending != 0u &&
            rootKey != 0u &&
            (active->GpuvaHierarchyProcess !=
                (BC250_MCDM_HANDLE)update->hProcess ||
             active->GpuvaHierarchyRootPageTable !=
                rootKey)) {
            Bc250DisplayLog("[ActiveUpdatePT] Hierarchy process switch (oldProc=0x%p, newProc=0x%p) -> Resetting hierarchy\n",
                active->GpuvaHierarchyProcess, update->hProcess);
            active->GpuvaHierarchyPending = 0u;
            active->GpuvaHierarchyProcess = NULL;
            active->GpuvaHierarchyRootPageTable = 0u;
            active->GpuvaHierarchyHighestLevel = 0u;
        }
        if (update->PageTableLevel == 0u) {
            leafJournal = Bc250GpuvaJournalSetSelect(active->GpuvaJournals,
                BC250_ACTIVE_GPUVA_JOURNAL_SLOTS,
                (BC250_MCDM_HANDLE)update->hProcess,
                rootKey, 1u, &evictionKind);
            if (leafJournal == NULL) {
                Bc250DisplayLog("[ActiveUpdatePT] Sem slot de journal para o processo 0x%p\n",
                    update->hProcess);
            }
            if (evictionKind != BC250_GPUVA_JOURNAL_EVICT_NONE) {
                /* Descarte explicito: nunca silencioso. */
                Bc250DisplayLog("[ActiveUpdatePT] Journal slot reciclado (kind=%u) para o processo 0x%p\n",
                    evictionKind, update->hProcess);
            }
            if (leafJournal != NULL) {
                journalStatus = BC250_GPUVA_JOURNAL_OK;
                for (chunkStart = 0u;
                     chunkStart < update->NumPageTableEntries &&
                         journalStatus == BC250_GPUVA_JOURNAL_OK;
                     chunkStart += chunkCount) {
                    chunkCount = update->NumPageTableEntries - chunkStart;
                    if (chunkCount > BC250_GPUVA_JOURNAL_MAX_CHANGES) {
                        chunkCount = BC250_GPUVA_JOURNAL_MAX_CHANGES;
                    }
                    RtlZeroMemory(entries, sizeof(*entries) * chunkCount);
                    for (index = 0u; index < chunkCount; ++index) {
                        const DXGK_PTE *pte = (update->Flags.Repeat != 0u) ?
                            &update->pPageTableEntries[0] :
                            &update->pPageTableEntries[chunkStart + index];
                        entries[index].Valid = pte->Valid != 0u ? 1u : 0u;
                        entries[index].Permissions = pte->Valid == 0u ? 0u : BC250_PTE_READABLE;
                        if (pte->Valid != 0u && pte->ReadOnly == 0u)
                            entries[index].Permissions |= BC250_PTE_WRITEABLE;
                        if (pte->Valid != 0u && pte->NoExecute == 0u)
                            entries[index].Permissions |= BC250_PTE_EXECUTABLE;
                        entries[index].SegmentId = (uint32_t)pte->Segment;
                        entries[index].SegmentOffset = pte->PageAddress << 12;
                        entries[index].Allocation = (BC250_MCDM_HANDLE)update->hAllocation;
                    }
                    journalStatus = Bc250GpuvaJournalStageLeafUpdate(leafJournal,
                        (BC250_MCDM_HANDLE)update->hProcess,
                        rootKey,
                        update->FirstPteVirtualAddress +
                            ((UINT64)chunkStart * BC250_GPUVA_PAGE_BYTES),
                        entries, chunkCount);
                }
            } else {
                journalStatus = BC250_GPUVA_JOURNAL_OK;
            }
        } else {
            if (rootKey != 0u) {
                /*
                 * So a transacao DO MESMO processo com root divergente e' descartada;
                 * o journal de outro processo nao e mais tocado aqui.
                 */
                (void)Bc250GpuvaJournalSetSelect(active->GpuvaJournals,
                    BC250_ACTIVE_GPUVA_JOURNAL_SLOTS,
                    (BC250_MCDM_HANDLE)update->hProcess,
                    rootKey, 0u, NULL);
            }
            if (rootKey != 0u) {
                active->GpuvaHierarchyPending = 1u;
                active->GpuvaHierarchyProcess =
                    (BC250_MCDM_HANDLE)update->hProcess;
                active->GpuvaHierarchyRootPageTable =
                    rootKey;
                if (update->PageTableLevel > active->GpuvaHierarchyHighestLevel)
                    active->GpuvaHierarchyHighestLevel = update->PageTableLevel;
            }
            journalStatus = BC250_GPUVA_JOURNAL_OK;
        }
    }
    /*
     * The physical PTEs MUST ALWAYS be encoded and written to the page table destination!
     * WDDM requires this paging operation to succeed.
     */
    destination = (UINT64 *)update->PageTableAddress.CpuVirtual;
    if (destination != NULL) {
        if (update->Flags.Repeat != 0u) {
            UINT64 encoded = 0u;
            (void)Bc250McdmEncodeVidMmPteWithLevel(
                Caps, &update->pPageTableEntries[0], update->PageTableLevel, &encoded);
            for (index = 0u; index < update->NumPageTableEntries; ++index) {
                destination[update->StartIndex + index] = encoded;
            }
        } else {
            for (index = 0u; index < update->NumPageTableEntries; ++index) {
                UINT64 encoded = 0u;
                (void)Bc250McdmEncodeVidMmPteWithLevel(
                    Caps, &update->pPageTableEntries[index], update->PageTableLevel, &encoded);
                destination[update->StartIndex + index] = encoded;
            }
        }
    }
    if (journalStatus == BC250_GPUVA_JOURNAL_OK) {
        /* Entre a escrita da PTE e o ACK do flush, snapshot e hardware
         * representam gerações diferentes: submissão deve permanecer fechada. */
        InterlockedExchange(&active->GpuvaMappingsOperational, 0);
        InterlockedExchange(&active->GpuvaActivationEligible, 0);
    }
    ExReleasePushLockExclusive(&active->ObjectLock);
    KeLeaveCriticalRegion();
    if (journalStatus == BC250_GPUVA_JOURNAL_BUSY) {
        Bc250DisplayLog("[ActiveUpdatePT] Finished: journalStatus=%d -> STATUS_DEVICE_BUSY\n",
            (int)journalStatus);
        return STATUS_DEVICE_BUSY;
    }
    if (journalStatus == BC250_GPUVA_JOURNAL_NO_SPACE) {
        /*
         * Paginas esparsas de sistema (Segmento 0 / scratch area de paginacao)
         * podem exceder o teto de faixas do journal. As PTEs ja foram gravadas
         * no destino fisico da page table pela CPU. Para evitar BugCheck 0x10E
         * no dxgmms2, desmarcamos a elegibilidade de snapshot e retornamos SUCCESS.
         */
        /*
         * C3.15: nao preserve um prefixo parcial. Na corrida 18:21, o segundo
         * chunk de um update esparso excedeu o journal antigo; o primeiro
         * chunk permaneceu Pending e o FlushTlb seguinte o publicou como se a
         * transacao estivesse inteira. Isso criou uma vista de software que
         * nao correspondia as PTEs fisicas ja escritas.
         */
        if (leafJournal != NULL) Bc250GpuvaJournalAbort(leafJournal);
        Bc250DisplayLog("[ActiveUpdatePT] Journal NO_SPACE (num=%u) -> transacao parcial ABORTADA; Hardware PTEs written -> SUCCESS\n",
            update->NumPageTableEntries);
        InterlockedExchange(&active->GpuvaMappingsOperational, 0);
        InterlockedExchange(&active->GpuvaActivationEligible, 0);
        return STATUS_SUCCESS;
    }
    if (journalStatus != BC250_GPUVA_JOURNAL_OK) {
        Bc250DisplayLog("[ActiveUpdatePT] Journal status=%d -> Hardware PTEs written -> SUCCESS\n",
            (int)journalStatus);
        InterlockedExchange(&active->GpuvaMappingsOperational, 0);
        InterlockedExchange(&active->GpuvaActivationEligible, 0);
        return STATUS_SUCCESS;
    }
    Bc250DisplayLog("[ActiveUpdatePT] Finished: journalStatus=0 -> SUCCESS\n");
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceV5Telemetry(
    CONST PVOID MiniportDeviceContext,
    PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface,
    PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren)
{
    BC250_DXGK_ACTIVE_CONTEXT *context =
        (BC250_DXGK_ACTIVE_CONTEXT *)MiniportDeviceContext;
    NTSTATUS status;

    /*
     * Marca ANTES de entrar: se o StartDevice travar ou o dxgkrnl desistir
     * durante a chamada, o rastro precisa registrar que ela comecou.
     */
    Bc250ProbeMarkDdi(BC250_V5_DDI_START_DEVICE);
    status = Bc250DxgkDdiStartDeviceActive(
        MiniportDeviceContext, DxgkStartInfo, DxgkInterface,
        NumberOfVideoPresentSources, NumberOfChildren);

    /*
     * O ciclo de vida so abre depois de o StartDevice ter devolvido sucesso.
     * Em falha o contexto permanece fora de RUNNING, de modo que nenhuma
     * submissao entra em um adaptador que nunca subiu.
     */
    if (NT_SUCCESS(status) && context != NULL &&
        context->Signature == BC250_ACTIVE_SIGNATURE) {
        InterlockedExchange(&context->LifecycleState,
            (LONG)BC250_ACTIVE_LIFECYCLE_RUNNING);
        InterlockedExchange(&context->SubmitInFlight, 0);
        Bc250ProbeRecord(BC250_PROBE_V5_LIFECYCLE_STATE,
            BC250_ACTIVE_LIFECYCLE_RUNNING);
        Bc250ProbeMarkPostStartStage(BC250_V5_POST_START_RETURNED);
    }
    Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_STATUS, (ULONG)status);
    Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_SOURCES,
        NumberOfVideoPresentSources != NULL ?
            *NumberOfVideoPresentSources : 0u);
    Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_CHILDREN,
        NumberOfChildren != NULL ? *NumberOfChildren : 0u);
    if (!NT_SUCCESS(status) && status != STATUS_DEVICE_CONFIGURATION_ERROR) {
        (void)Bc250BootFuseDisarmClean(status);
    }
    return status;
}

/*
 * Perfil do carregamento corrente.  Resolvido uma unica vez em DriverEntry,
 * antes de qualquer registro no dxgkrnl, e consultado depois por
 * StartDevice.  LONG volatil porque StartDevice roda fora do contexto de
 * DriverEntry; a escrita e' unica e anterior a qualquer leitura.
 */
static volatile LONG g_Bc250AdapterProfile =
    (LONG)BC250_ADAPTER_PROFILE_DISPLAY_ONLY;

_Check_return_
NTSTATUS Bc250ActiveSetAdapterProfile(_In_ BC250_ADAPTER_PROFILE Profile)
{
    if (Profile != BC250_ADAPTER_PROFILE_DISPLAY_ONLY &&
        Profile != BC250_ADAPTER_PROFILE_DISPLAY_RENDER &&
        Profile != BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
        return STATUS_INVALID_PARAMETER;
    }
    InterlockedExchange(&g_Bc250AdapterProfile, (LONG)Profile);
    Bc250DisplaySetChildEnumeration(
        Profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB ? FALSE : TRUE);
    return STATUS_SUCCESS;
}

_Check_return_
BC250_ADAPTER_PROFILE Bc250ActiveGetAdapterProfile(void)
{
    return (BC250_ADAPTER_PROFILE)InterlockedCompareExchange(
        &g_Bc250AdapterProfile, 0, 0);
}

_Check_return_
NTSTATUS Bc250ActiveResolveAdapterProfile(
    _In_ ULONG RenderNode,
    _In_ ULONG RenderNodePresent,
    _In_ ULONG RenderOnlyLab,
    _In_ ULONG RenderOnlyLabPresent,
    _Out_ BC250_ADAPTER_PROFILE *Profile)
{
    if (Profile == NULL) return STATUS_INVALID_PARAMETER;
    *Profile = BC250_ADAPTER_PROFILE_DISPLAY_ONLY;

    /* Fora de {0,1} nao existe interpretacao segura: nao adivinhar. */
    if (RenderNode > 1u || RenderOnlyLab > 1u) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /*
     * Um tunable ilegivel que devolveu valor nao-default e' configuracao
     * corrompida, nao ausencia.  Ausencia legitima e' Present=0 com o
     * default preservado, e cai no perfil herdado.
     */
    if (RenderNodePresent == 0u && RenderNode != 0u) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (RenderOnlyLabPresent == 0u && RenderOnlyLab != 0u) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (RenderOnlyLabPresent == 1u && RenderOnlyLab == 1u) {
        /*
         * RenderOnlyLab exige o caminho DxgkInitialize.  O caminho KMDOD e'
         * display por definicao: pedir render-only nele e' uma combinacao
         * sem significado, e adivinhar qual metade o operador queria seria
         * pior do que recusar.
         */
        if (!(RenderNodePresent == 1u && RenderNode == 1u)) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        *Profile = BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB;
        return STATUS_SUCCESS;
    }

    if (RenderNodePresent == 1u && RenderNode == 1u) {
        *Profile = BC250_ADAPTER_PROFILE_DISPLAY_RENDER;
        return STATUS_SUCCESS;
    }

    *Profile = BC250_ADAPTER_PROFILE_DISPLAY_ONLY;
    return STATUS_SUCCESS;
}

/*
 * Resolve uma GPUVA de escape para endereco MC, pela MESMA aritmetica que o
 * codificador de PTE e a instalacao da root usam (C3.4). Faixa que nao couber
 * no segmento e' RECUSADA -- aproximar viraria blt de lixo indistinguivel de
 * sucesso, defeito ja cometido e corrigido em 12/09.
 */
static BOOLEAN Bc250ActiveEscapeResolveGpuVaToMc(
    _In_ HANDLE hAdapter,
    _In_ CONST DXGKARG_ESCAPE *pEscape,
    _In_ UINT64 GpuVa,
    _In_ UINT32 ByteCount,
    _Out_ UINT64 *McAddress)
{
    UINT64 segmentOffset = 0;
    UINT32 segId = 0;
    HANDLE resolveHandle;
    BC250_MCDM_RUNTIME_CAPS caps;

    *McAddress = 0ull;
    if (GpuVa == 0ull || ByteCount == 0u) {
        return FALSE;
    }
    resolveHandle = (pEscape->hContext != NULL) ? pEscape->hContext : pEscape->hDevice;
    if (!Bc250ActiveResolveDisplayGpuVa(resolveHandle, GpuVa, &segmentOffset, &segId) &&
        !(pEscape->hDevice != NULL &&
          Bc250ActiveResolveDisplayGpuVa(pEscape->hDevice, GpuVa, &segmentOffset, &segId)) &&
        !Bc250ActiveResolveDisplayGpuVa(NULL, GpuVa, &segmentOffset, &segId)) {
        return FALSE;
    }
    if (!NT_SUCCESS(Bc250ActiveQueryRuntimeCaps(hAdapter, &caps))) {
        return FALSE;
    }
    return Bc250SegmentOffsetToMc(
        g_Bc250SdmaPresent.VramMcStart,
        caps.LocalSegmentBaseOffset,
        caps.LocalSegmentBytes,
        segId,
        segmentOffset,
        ByteCount,
        McAddress) == BC250_SEGMENT_ADDRESS_OK;
}

/*
 * COMPOSITE_BLT -- composicao do DWM executada pelo caminho de blt do KMD.
 *
 * Medicao de 13/09 08:12 que motiva este caminho: 953 buffers de paginacao
 * executaram sem UM TDR; as 5 submissoes de CONTEXTO da UMD no Node 0 (o no de
 * paginacao) produziram 5 TDRs, sempre com a engine OCIOSA e o anel DRENADO.
 * Bc250SdmaPresentBlt, ao contrario, fez 584 blts consecutivos sem TDR em
 * 12/09 13:01 -- ele nao cria pacote de contexto e nao passa pelo escalonador.
 */
static NTSTATUS Bc250ActiveEscapeCompositeBlt(
    _In_ HANDLE hAdapter,
    _In_ CONST DXGKARG_ESCAPE *pEscape)
{
    BC250_ESCAPE_COMPOSITE_BLT *esc;
    UINT64 srcMc = 0ull;
    UINT64 dstMc = 0ull;
    BC250_DATA_PROOF_PLAN srcProofPlan;
    BC250_DATA_PROOF_PLAN dstProofPlan;
    UINT32 sourceBefore[BC250_DATA_PROOF_CANARY_COUNT] = { 0u };
    UINT32 destinationAfter[BC250_DATA_PROOF_CANARY_COUNT] = { 0u };
    UINT32 proofIndex;
    UINT32 sourceNonZero = 0u;
    UINT32 proofMismatches = 0u;
    UINT32 sampleX[BC250_DATA_PROOF_CANARY_COUNT];
    UINT32 sampleY[BC250_DATA_PROOF_CANARY_COUNT];
    UINT64 srcSpan = 0ull;
    UINT64 dstSpan = 0ull;
    UINT64 pixelBytes = 0ull;
    BOOLEAN proofUsable = FALSE;
    BOOLEAN sdmaDone = FALSE;
    BC250_BLT_PROOF_VERDICT proofVerdict = BC250_BLT_PROOF_INDETERMINATE;

    /*
     * ENTRADA SEMPRE FALA.
     *
     * Corrida 13/09 11:32: o DWM chamou o escape UMA vez (BltViaKmd ENTER) e o
     * log do KMD nao teve NENHUMA linha COMPOSITE_BLT -- nem OK, nem RECUSADO.
     * Era impossivel distinguir "nao chegou ao handler" de "recusamos calados",
     * porque esta saida e a do dispatch eram MUDAS. Mesma classe de defeito que
     * eu corrigi de manha em Bc250McdmSignalMonitoredFence e reintroduzi aqui.
     */
    Bc250DisplayLog("[Bc250Escape] COMPOSITE_BLT ENTER: DataSize=%u (esperado=%u)\n",
        (uint32_t)pEscape->PrivateDriverDataSize,
        (uint32_t)sizeof(BC250_ESCAPE_COMPOSITE_BLT));
    if (pEscape->PrivateDriverDataSize < sizeof(BC250_ESCAPE_COMPOSITE_BLT)) {
        Bc250DisplayLog("[Bc250Escape] COMPOSITE_BLT RECUSADO: DataSize=%u menor que o contrato (%u) -- ABI divergente entre UMD e KMD\n",
            (uint32_t)pEscape->PrivateDriverDataSize,
            (uint32_t)sizeof(BC250_ESCAPE_COMPOSITE_BLT));
        return STATUS_INVALID_PARAMETER;
    }
    esc = (BC250_ESCAPE_COMPOSITE_BLT *)pEscape->pPrivateDriverData;

    pixelBytes = (UINT64)esc->Width * 4ull;
    if (esc->Flags != 0u || esc->Width == 0u || esc->Height == 0u ||
        esc->Width > 0x4000u || esc->Height > 0x4000u ||
        pixelBytes > esc->SourcePitch ||
        pixelBytes + (UINT64)esc->DestX * 4ull > esc->DestPitch ||
        esc->DestY > 0x3FFFu ||
        (esc->SourcePitch & 3u) != 0u || (esc->DestPitch & 3u) != 0u ||
        esc->SourceGpuVa == 0ull || esc->DestGpuVa == 0ull ||
        (esc->SourceGpuVa & 3ull) != 0ull || (esc->DestGpuVa & 3ull) != 0ull) {
        esc->ResultCode = BC250_PRESENT_BLT_STATUS_INVALID;
        Bc250DisplayLog("[Bc250Escape] COMPOSITE_BLT RECUSADO (argumentos): %ux%u srcPitch=%u dstPitch=%u DstXY=%u,%u SrcVa=0x%llX DstVa=0x%llX\n",
            esc->Width, esc->Height, esc->SourcePitch, esc->DestPitch,
            esc->DestX, esc->DestY, (unsigned long long)esc->SourceGpuVa,
            (unsigned long long)esc->DestGpuVa);
        return STATUS_INVALID_PARAMETER;
    }
    srcSpan = (UINT64)(esc->Height - 1u) * esc->SourcePitch + pixelBytes;
    dstSpan = (UINT64)(esc->DestY + esc->Height - 1u) * esc->DestPitch +
        (UINT64)esc->DestX * 4ull + pixelBytes;
    if (srcSpan == 0ull || dstSpan == 0ull ||
        srcSpan > (64ull * 1024ull * 1024ull) ||
        dstSpan > (64ull * 1024ull * 1024ull)) {
        esc->ResultCode = BC250_PRESENT_BLT_STATUS_INVALID;
        return STATUS_INVALID_PARAMETER;
    }

    if (!g_Bc250SdmaPresent.HardwareEnabled) {
        esc->ResultCode = BC250_PRESENT_BLT_STATUS_UNSUPPORTED;
        Bc250DisplayLog("[Bc250Escape] COMPOSITE_BLT RECUSADO: SDMA nao habilitada\n");
        return STATUS_DEVICE_NOT_READY;
    }

    if (!Bc250ActiveEscapeResolveGpuVaToMc(hAdapter, pEscape, esc->SourceGpuVa,
            (UINT32)srcSpan, &srcMc) ||
        !Bc250ActiveEscapeResolveGpuVaToMc(hAdapter, pEscape, esc->DestGpuVa,
            (UINT32)dstSpan, &dstMc)) {
        esc->ResultCode = BC250_PRESENT_BLT_STATUS_INVALID;
        Bc250DisplayLog("[Bc250Escape] COMPOSITE_BLT RECUSADO (resolucao): SrcVa=0x%llX->0x%llX DstVa=0x%llX->0x%llX Bytes=%u\n",
            (unsigned long long)esc->SourceGpuVa, (unsigned long long)srcMc,
            (unsigned long long)esc->DestGpuVa, (unsigned long long)dstMc,
            (unsigned)srcSpan);
        return STATUS_INVALID_PARAMETER;
    }

    /* Faixas inteiras de backing nao podem se sobrepor. */
    if ((srcMc < dstMc + dstSpan) && (dstMc < srcMc + srcSpan)) {
        esc->ResultCode = BC250_PRESENT_BLT_STATUS_INVALID;
        Bc250DisplayLog("[Bc250Escape] COMPOSITE_BLT RECUSADO (sobreposicao): SrcMc=0x%llX DstMc=0x%llX Bytes=%u\n",
            (unsigned long long)srcMc, (unsigned long long)dstMc,
            (unsigned)(pixelBytes * esc->Height));
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * C3.18: snapshot NAO DESTRUTIVO da origem antes do SDMA. A corrida de
     * 13/09 12:50 provou 60 fences/RPTRs, mas as tres amostras posteriores do
     * Present viram somente zero. Para separar "copiamos preto corretamente"
     * de "a copia nao produziu dados", guardamos cinco dwords distribuidos da
     * origem e os comparamos com o destino DEPOIS da fence. Nao plantamos
     * canaries: este e' conteudo real do DWM e altera-lo corromperia o frame.
     */
    (void)Bc250PlanDataProofCanaries(srcMc, (UINT32)srcSpan, 0u,
        g_Bc250SdmaPresent.VramMcStart,
        (UINT64)g_Bc250DisplayState.FrameBufferSize,
        (g_Bc250DisplayState.FrameBufferVirtual != NULL) ? 1u : 0u,
        &srcProofPlan);
    (void)Bc250PlanDataProofCanaries(dstMc, (UINT32)dstSpan, 0u,
        g_Bc250SdmaPresent.VramMcStart,
        (UINT64)g_Bc250DisplayState.FrameBufferSize,
        (g_Bc250DisplayState.FrameBufferVirtual != NULL) ? 1u : 0u,
        &dstProofPlan);
    proofUsable = (srcProofPlan.Usable != 0u && dstProofPlan.Usable != 0u &&
        srcProofPlan.CanaryCount == BC250_DATA_PROOF_CANARY_COUNT &&
        dstProofPlan.CanaryCount == BC250_DATA_PROOF_CANARY_COUNT);
    if (proofUsable) {
        sampleX[0] = 0u;                 sampleY[0] = 0u;
        sampleX[1] = esc->Width / 2u;    sampleY[1] = 0u;
        sampleX[2] = 0u;                 sampleY[2] = esc->Height / 2u;
        sampleX[3] = esc->Width / 2u;    sampleY[3] = esc->Height / 2u;
        sampleX[4] = esc->Width - 1u;    sampleY[4] = esc->Height - 1u;
        for (proofIndex = 0u; proofIndex < BC250_DATA_PROOF_CANARY_COUNT;
             ++proofIndex) {
            volatile CONST UINT32 *sample = (volatile CONST UINT32 *)
                ((CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual +
                 srcProofPlan.VramOffset +
                 (UINT64)sampleY[proofIndex] * esc->SourcePitch +
                 (UINT64)sampleX[proofIndex] * 4ull);
            sourceBefore[proofIndex] = *sample;
            if (sourceBefore[proofIndex] != 0u) sourceNonZero++;
        }
        KeMemoryBarrier();
    }

    if (esc->SourcePitch == esc->DestPitch && esc->DestX == 0u &&
        esc->DestY == 0u && pixelBytes == esc->SourcePitch) {
        sdmaDone = Bc250SdmaPresentBlt(srcMc, dstMc,
            (ULONG)((UINT64)esc->Height * esc->SourcePitch));
    } else {
        sdmaDone = Bc250SdmaPresentBltRect(srcMc, esc->SourcePitch,
            dstMc, esc->DestPitch, esc->DestX, esc->DestY,
            esc->Width, esc->Height);
    }
    if (!sdmaDone) {
        esc->ResultCode = BC250_PRESENT_BLT_STATUS_FAILED;
        Bc250DisplayLog("[Bc250Escape] COMPOSITE_BLT FALHOU no silicio: SrcMc=0x%llX DstMc=0x%llX Bytes=%u\n",
            (unsigned long long)srcMc, (unsigned long long)dstMc,
            (unsigned)(pixelBytes * esc->Height));
        return STATUS_UNSUCCESSFUL;
    }

    if (proofUsable) {
        KeMemoryBarrier();
        for (proofIndex = 0u; proofIndex < BC250_DATA_PROOF_CANARY_COUNT;
             ++proofIndex) {
            volatile CONST UINT32 *sample = (volatile CONST UINT32 *)
                ((CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual +
                 dstProofPlan.VramOffset +
                 (UINT64)(esc->DestY + sampleY[proofIndex]) * esc->DestPitch +
                 (UINT64)(esc->DestX + sampleX[proofIndex]) * 4ull);
            destinationAfter[proofIndex] = *sample;
            if (destinationAfter[proofIndex] != sourceBefore[proofIndex]) {
                proofMismatches++;
            }
        }
        proofVerdict = Bc250ClassifyBltProof(
            (proofMismatches == 0u) ? 1u : 0u,
            sourceNonZero,
            BC250_DATA_PROOF_CANARY_COUNT);
        if (Bc250BltProofRequiresFailClosed(proofVerdict) != 0u) {
            esc->ResultCode = BC250_PRESENT_BLT_STATUS_FAILED;
            Bc250DisplayLog("[Bc250Escape] COMPOSITE_DATA_PROOF FAIL: Mismatch=%u/%u SrcPre=[%08X %08X %08X %08X %08X] DstPost=[%08X %08X %08X %08X %08X]; engine preservada\n",
                proofMismatches, BC250_DATA_PROOF_CANARY_COUNT,
                sourceBefore[0], sourceBefore[1], sourceBefore[2],
                sourceBefore[3], sourceBefore[4], destinationAfter[0],
                destinationAfter[1], destinationAfter[2],
                destinationAfter[3], destinationAfter[4]);
            return STATUS_UNSUCCESSFUL;
        }
        if (proofVerdict == BC250_BLT_PROOF_BLANK) {
            Bc250DisplayLog("[Bc250Escape] COMPOSITE_DATA_PROOF INCONCLUSIVO_ZERO: Match=%u/%u; zero contra zero NAO prova pixel\n",
                BC250_DATA_PROOF_CANARY_COUNT, BC250_DATA_PROOF_CANARY_COUNT);
        } else {
            Bc250DisplayLog("[Bc250Escape] COMPOSITE_DATA_PROOF SAMPLED_PASS: Match=%u/%u SourceNonZero=%u/%u DstIsScanout=%u\n",
                BC250_DATA_PROOF_CANARY_COUNT, BC250_DATA_PROOF_CANARY_COUNT,
                sourceNonZero, BC250_DATA_PROOF_CANARY_COUNT,
                (dstMc == g_Bc250SdmaPresent.ScanoutGpuAddress) ? 1u : 0u);
        }
    } else {
        Bc250DisplayLog("[Bc250Escape] COMPOSITE_DATA_PROOF INDISPONIVEL: SrcMc=0x%llX DstMc=0x%llX Bytes=%u Apertura=%u\n",
            (unsigned long long)srcMc, (unsigned long long)dstMc,
            (unsigned)(pixelBytes * esc->Height),
            g_Bc250DisplayState.FrameBufferSize);
    }

    esc->ResultCode = BC250_PRESENT_BLT_STATUS_SUCCESS;
    Bc250DisplayLog("[Bc250Escape] COMPOSITE_BLT OK RECT 1:1: %ux%u srcPitch=%u dstPitch=%u DstXY=%u,%u SrcMc=0x%llX DstMc=0x%llX Bytes=%u Prova=%s\n",
        esc->Width, esc->Height, esc->SourcePitch, esc->DestPitch,
        esc->DestX, esc->DestY,
        (unsigned long long)srcMc, (unsigned long long)dstMc,
        (unsigned)(pixelBytes * esc->Height),
        proofUsable ? ((sourceNonZero != 0u) ? "DADOS_AMOSTRADOS" : "ZERO_INCONCLUSIVO") :
            "SOMENTE_ENGINE");
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ActiveEscapeHandler(
    _In_ HANDLE hAdapter,
    _In_ CONST DXGKARG_ESCAPE *pEscape)
{
    static uint32_t s_PresentBltCount = 0;
    BC250_ESCAPE_PRESENT_BLT *esc;
    UINT64 srcGpu = 0;
    BOOLEAN sdmaDone = FALSE;
    BOOLEAN shouldLog = FALSE;

    if (pEscape == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * O opcode e' lido ANTES de exigir o tamanho de um payload especifico:
     * cada operacao tem a sua estrutura, e exigir o tamanho da maior recusaria
     * a menor por engano.
     */
    if (pEscape->pPrivateDriverData == NULL ||
        pEscape->PrivateDriverDataSize < sizeof(UINT32)) {
        /*
         * Amostrado de proposito: escapes de TERCEIROS chegam aos milhares
         * (3.282 na corrida 11:32) e um log 1-em-1 aqui afogaria o arquivo.
         * O que nao pode e' ser MUDO -- era, e por isso a corrida 11:32 nao
         * soube dizer se o nosso escape havia chegado.
         */
        Bc250DisplayLog("[Bc250Escape] RECUSADO: payload ausente ou menor que 4 bytes (pData=%s, DataSize=%u)\n",
            (pEscape->pPrivateDriverData != NULL) ? "ok" : "NULL",
            (uint32_t)pEscape->PrivateDriverDataSize);
        return STATUS_INVALID_PARAMETER;
    }

    if (*(UINT32 *)pEscape->pPrivateDriverData == BC250_ESCAPE_OP_COMPOSITE_BLT) {
        return Bc250ActiveEscapeCompositeBlt(hAdapter, pEscape);
    }

    if (pEscape->PrivateDriverDataSize < sizeof(BC250_ESCAPE_PRESENT_BLT)) {
        return STATUS_INVALID_PARAMETER;
    }

    esc = (BC250_ESCAPE_PRESENT_BLT *)pEscape->pPrivateDriverData;

    if (esc->Opcode != BC250_ESCAPE_OP_PRESENT_BLT) {
        /*
         * Escapes de TERCEIROS sao a maioria absoluta: 3.282 na corrida 11:32,
         * todos com opcode 0x2 e nenhum nosso. Esta linha nao pode ser muda
         * (foi o que cegou aquela corrida) nem pode afogar o arquivo, entao e'
         * limitada por contador proprio -- "RECUSA" esta na lista que escapa da
         * amostragem global, logo o teto tem de ser local.
         */
        static uint32_t s_UnknownOpcodeCount = 0;
        ++s_UnknownOpcodeCount;
        if (s_UnknownOpcodeCount <= 8u || (s_UnknownOpcodeCount % 1024u) == 0u) {
            Bc250DisplayLog("[Bc250Escape] RECUSADO: opcode 0x%X desconhecido (ocorrencia #%u, DataSize=%u)\n",
                esc->Opcode, s_UnknownOpcodeCount,
                (uint32_t)pEscape->PrivateDriverDataSize);
        }
        return STATUS_NOT_SUPPORTED;
    }

    if (esc->SizeBytes == 0 || esc->SizeBytes > (64u * 1024u * 1024u)) {
        esc->ResultCode = BC250_PRESENT_BLT_STATUS_INVALID;
        return STATUS_INVALID_PARAMETER;
    }

    s_PresentBltCount++;
    shouldLog = (s_PresentBltCount == 1u || (s_PresentBltCount % 60u) == 0u);
    /*
     * C3: contador nao amostrado. O log e' 1-em-60; este numero e' 1-em-1 e
     * sobrevive no teardown, entao a proxima corrida consegue dizer qual foi o
     * ultimo present que o KMD REALMENTE recebeu antes do 0xC00002B6 -- que,
     * por leitura de codigo, nao pode sair daqui: STATUS_DEVICE_REMOVED nao
     * existe em nenhuma linha deste driver, logo o dxgkrnl recusou a chamada
     * antes de nos alcancar.
     */
    InterlockedIncrement(&g_Bc250SdmaPresent.EscapeEnteredCount);

    if (shouldLog) {
        Bc250DisplayLog("[Bc250Escape] PRESENT_BLT: %ux%u (%u bytes), GpuVa=0x%llX, SrcPhys=0x%llX, pUser=%p, Flags=0x%X\n",
            esc->Width, esc->Height, esc->SizeBytes,
            (unsigned long long)esc->SourceGpuVa,
            (unsigned long long)esc->SourcePhysAddress,
            esc->pUserPixelBuffer, esc->Flags);
    }

    /* 1. Tentar caminho primario acelerado por hardware GPU: SDMA Blt */
    if ((esc->Flags & BC250_PRESENT_BLT_FLAG_HOST_COPY) == 0 &&
        g_Bc250SdmaPresent.HardwareEnabled &&
        g_Bc250SdmaPresent.ScanoutGpuAddress != 0) {

        /* Se endereco fisico foi fornecido diretamente */
        if (esc->SourcePhysAddress != 0) {
            srcGpu = g_Bc250SdmaPresent.VramMcStart + esc->SourcePhysAddress;
        }
        /*
         * Se GPUVA foi fornecido, resolver para endereco MC na VRAM.
         *
         * C3.4 (12/09) -- DEFEITO DE ARITMETICA CORRIGIDO, medido na corrida
         * 12:29. Bc250ActiveResolveDisplayGpuVa devolve um OFFSET DE SEGMENTO
         * (BackingSegmentOffset + deslocamento na faixa), nao um offset cru de
         * VRAM: o campo de saida de Bc250GpuvaSpaceResolveAddress chama-se
         * BackingSegmentOffset. O segmento 1 comeca em LocalSegmentBaseOffset
         * (0xA8C000 = 11.059.200 bytes, logo apos a reserva de scanout), e este
         * sitio somava apenas VramMcStart -- lia 11.059.200 bytes ANTES da
         * imagem. O blt concluia com fence e RPTR==WPTR, porque a engine de
         * fato executou, mas copiava VRAM alheia: majoritariamente zerada, com
         * restos nao nulos. Na tela: preto com faixa cinza embaixo.
         *
         * A conversao agora sai de Bc250SegmentOffsetToMc, a mesma aritmetica
         * que o codificador de PTE e a instalacao da root ja usavam. Origem que
         * nao couber no segmento passa a ser RECUSADA -- antes virava blt de
         * lixo indistinguivel de sucesso.
         */
        else if (esc->SourceGpuVa != 0) {
            UINT64 segmentOffset = 0;
            UINT32 segId = 0;
            HANDLE targetHandle = (pEscape->hContext != NULL) ? pEscape->hContext : pEscape->hDevice;
            if (Bc250ActiveResolveDisplayGpuVa(targetHandle, esc->SourceGpuVa, &segmentOffset, &segId) ||
                (pEscape->hDevice != NULL && Bc250ActiveResolveDisplayGpuVa(pEscape->hDevice, esc->SourceGpuVa, &segmentOffset, &segId)) ||
                Bc250ActiveResolveDisplayGpuVa(NULL, esc->SourceGpuVa, &segmentOffset, &segId)) {
                BC250_MCDM_RUNTIME_CAPS srcCaps;
                UINT64 srcMc = 0;
                BC250_SEGMENT_ADDRESS_STATUS mcStatus = BC250_SEGMENT_ADDRESS_BAD_ARGUMENT;
                NTSTATUS capsStatus =
                    Bc250ActiveQueryRuntimeCaps(hAdapter, &srcCaps);

                if (NT_SUCCESS(capsStatus)) {
                    /*
                     * DOMINIO DE ENDERECO -- armadilha medida na corrida 12:51.
                     *
                     * Existem DUAS bases de VRAM neste driver, e elas NAO sao
                     * intercambiaveis:
                     *
                     *   caps.VramMcBase            = 0x450000000  dominio da GCVM
                     *                                (PTE, root, VMID8/9)
                     *   g_Bc250SdmaPresent.VramMcStart = 0xF400000000  dominio em
                     *                                que o anel de present do
                     *                                SDMA enderecca
                     *
                     * A correcao de 12:29 acertou o termo que faltava
                     * (LocalSegmentBaseOffset) mas trocou a base por engano,
                     * pondo a ORIGEM em 0x451FEC000 enquanto o DESTINO seguia em
                     * 0xF400000000. Origem e destino em dominios diferentes: o
                     * blt deu timeout com a fence no poison (43DAFFFE) em 8 de 8
                     * tentativas, e o present caiu na copia por CPU.
                     *
                     * A base tem de ser a MESMA do destino -- que e' derivado de
                     * g_Bc250SdmaPresent.VramMcStart. LocalSegmentBaseOffset e
                     * LocalSegmentBytes sao offsets dentro da VRAM e independem
                     * do dominio, entao continuam vindo das caps.
                     */
                    mcStatus = Bc250SegmentOffsetToMc(
                        g_Bc250SdmaPresent.VramMcStart,
                        srcCaps.LocalSegmentBaseOffset,
                        srcCaps.LocalSegmentBytes,
                        segId,
                        segmentOffset,
                        esc->SizeBytes,
                        &srcMc);
                }

                if (mcStatus == BC250_SEGMENT_ADDRESS_OK) {
                    srcGpu = srcMc;
                    if (shouldLog) {
                        /*
                         * DstMc sai no mesmo log de proposito: origem e destino
                         * PRECISAM compartilhar o dominio, e em 12:51 nao
                         * compartilhavam. Com os dois lado a lado, a proxima
                         * divergencia e' visivel sem mineracao.
                         */
                        Bc250DisplayLog("[Bc250Escape] PRESENT_BLT: GpuVa 0x%llX resolvido -> SegOff=0x%llX Seg=%u SegBase=0x%llX SrcMc=0x%llX DstMc=0x%llX\n",
                            (unsigned long long)esc->SourceGpuVa,
                            (unsigned long long)segmentOffset,
                            segId,
                            (unsigned long long)srcCaps.LocalSegmentBaseOffset,
                            (unsigned long long)srcGpu,
                            (unsigned long long)g_Bc250SdmaPresent.ScanoutGpuAddress);
                    }
                } else {
                    if (shouldLog) {
                        Bc250DisplayLog("[Bc250Escape] AVISO: GpuVa 0x%llX RECUSADO na conversao MC (capsStatus=0x%08X mcStatus=%u Seg=%u SegOff=0x%llX Bytes=%u)\n",
                            (unsigned long long)esc->SourceGpuVa,
                            (uint32_t)capsStatus, (uint32_t)mcStatus, segId,
                            (unsigned long long)segmentOffset, esc->SizeBytes);
                    }
                }
            } else {
                if (shouldLog) {
                    Bc250DisplayLog("[Bc250Escape] AVISO: Falha ao resolver GpuVa 0x%llX via GPUVA Space\n",
                        (unsigned long long)esc->SourceGpuVa);
                }
            }
        }

        if (srcGpu != 0 && srcGpu != g_Bc250SdmaPresent.ScanoutGpuAddress) {
            LARGE_INTEGER bltFreq;
            LARGE_INTEGER bltStart;
            LARGE_INTEGER bltEnd;
            LONG bltMicroseconds = 0;

            bltStart = KeQueryPerformanceCounter(&bltFreq);
            sdmaDone = Bc250SdmaPresentBlt(srcGpu, g_Bc250SdmaPresent.ScanoutGpuAddress, esc->SizeBytes);
            bltEnd = KeQueryPerformanceCounter(NULL);
            if (bltFreq.QuadPart > 0) {
                LONGLONG ticks = bltEnd.QuadPart - bltStart.QuadPart;
                if (ticks < 0) ticks = 0;
                bltMicroseconds = (LONG)((ticks * 1000000LL) / bltFreq.QuadPart);
            }
            /*
             * C3: acumula custo do blt. A corrida 12/09 10:42 mediu 26 FPS
             * (~38 ms/quadro) para 11.059.200 bytes. Se o blt sozinho explicar
             * o quadro, o gargalo e' a espera SINCRONA de Bc250SdmaPresentBlt
             * (divida declarada desde a A15 e causa do 0x133 de 10/09); se nao
             * explicar, o custo esta fora do KMD. Os dois desfechos informam.
             */
            InterlockedExchangeAdd64(&g_Bc250SdmaPresent.EscapeBltTotalMicroseconds,
                                     (LONG64)bltMicroseconds);
            {
                LONG previousMax;
                do {
                    previousMax = g_Bc250SdmaPresent.EscapeBltMaxMicroseconds;
                    if (bltMicroseconds <= previousMax) break;
                } while (InterlockedCompareExchange(
                             &g_Bc250SdmaPresent.EscapeBltMaxMicroseconds,
                             bltMicroseconds, previousMax) != previousMax);
            }
            if (sdmaDone) {
                if (shouldLog) {
                    Bc250DisplayLog("[Bc250Escape] PRESENT_BLT: SDMA HW BLT CONCLUIDO COM SUCESSO! SrcMc=0x%llX DstMc=0x%llX Bytes=%u Escape=#%ld Blt=%ld us\n",
                        (unsigned long long)srcGpu,
                        (unsigned long long)g_Bc250SdmaPresent.ScanoutGpuAddress,
                        esc->SizeBytes,
                        g_Bc250SdmaPresent.EscapeEnteredCount,
                        bltMicroseconds);
                }
                esc->ResultCode = BC250_PRESENT_BLT_STATUS_SUCCESS;

                /*
                 * C3 (12/09) -- A "GARANTIA DE SCANOUT GOP QUADRO 1" FOI REMOVIDA.
                 *
                 * Ela copiava, POR CPU (RtlCopyMemory de 11.059.200 bytes), o mesmo
                 * conteudo que o SDMA acabara de escrever no MESMO endereco fisico
                 * (DstMc == VramMcStart + (FrameBufferPhysical - VramBar0), medido em
                 * 0xF400000000 na corrida 12/09 11:04). Escrever por cima com a CPU nao
                 * adiciona pixel nenhum: torna o quadro 1 NAO ATRIBUIVEL a GPU, porque
                 * qualquer imagem no monitor passa a ter duas causas possiveis. Foi
                 * exatamente assim que a corrida 11/09 22:46 -- a que fechou o "Marco 2"
                 * -- entregou a tela: ZERO linhas "SDMA HW BLT CONCLUIDO" e uma unica
                 * copia por CPU. Pela Lei 4 isso e' fallback de software, e pela Lei 2 a
                 * perna de bytes do tripe nao pode ser conferida contra um destino que a
                 * CPU escreveu.
                 *
                 * Mesmo precedente da faixa C1, que removeu o memcpy do Draw.
                 *
                 * Se o byte proof por CPU ler zero apos um SDMA bem-sucedido, a resposta
                 * correta e' invalidar o HDP antes da leitura (classe ja diagnosticada em
                 * 10/09), NAO reescrever o destino pela CPU.
                 */
                return STATUS_SUCCESS;
            } else {
                Bc250DisplayLog("[Bc250Escape] PRESENT_BLT: SDMA HW BLT falhou/incompleto, acionando fallback seguro\n");
            }
        }
    }

    /*
     * 2. Caminho de copia POR CPU.
     *
     * C3 (12/09): este ramo nao e' aceleracao e nao pode se anunciar como se
     * fosse. Ele existe para que o ensaio nao morra, e cada quadro que passa por
     * aqui e' contabilizado em HostCpuCopyCount -- FallbackCpuCount e' CEGO a ele,
     * porque so' conta recusa/timeout de Bc250SdmaPresentBlt, e foi essa cegueira
     * que deixou a corrida 11/09 22:46 arquivar "Fallbacks=0" com o present inteiro
     * feito por RtlCopyMemory. O veredito do runner reprova a corrida que cair aqui.
     */
    if (esc->pUserPixelBuffer != NULL &&
        g_Bc250DisplayState.FrameBufferVirtual != NULL &&
        esc->SizeBytes <= g_Bc250DisplayState.FrameBufferSize) {

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
        __try {
            LONG hostCopies;
            ProbeForRead(esc->pUserPixelBuffer, esc->SizeBytes, sizeof(ULONG));
            RtlCopyMemory(g_Bc250DisplayState.FrameBufferVirtual, esc->pUserPixelBuffer, esc->SizeBytes);
            KeMemoryBarrier();
            hostCopies = InterlockedIncrement(&g_Bc250SdmaPresent.HostCpuCopyCount);
            Bc250DisplayLog("[Bc250Escape] PRESENT_BLT: COPIA POR CPU #%ld -- NAO e' aceleracao por GPU (%u bytes por RtlCopyMemory)\n",
                hostCopies, esc->SizeBytes);
            esc->ResultCode = BC250_PRESENT_BLT_STATUS_SUCCESS;
            return STATUS_SUCCESS;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ULONG exc = GetExceptionCode();
            Bc250DisplayLog("[Bc250Escape] PRESENT_BLT: Excecao 0x%08X ao ler buffer do usuario %p\n",
                exc, esc->pUserPixelBuffer);
            esc->ResultCode = BC250_PRESENT_BLT_STATUS_FAILED;
            return STATUS_INVALID_USER_BUFFER;
        }
#else
        memcpy(g_Bc250DisplayState.FrameBufferVirtual, esc->pUserPixelBuffer, esc->SizeBytes);
        InterlockedIncrement(&g_Bc250SdmaPresent.HostCpuCopyCount);
        esc->ResultCode = BC250_PRESENT_BLT_STATUS_SUCCESS;
        return STATUS_SUCCESS;
#endif
    }

    esc->ResultCode = BC250_PRESENT_BLT_STATUS_FAILED;
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS Bc250BuildActiveInitializationData(
    DRIVER_INITIALIZATION_DATA *InitializationData)
{
    return Bc250BuildActiveInitializationDataForContract(
        InitializationData,
        DXGKDDI_INTERFACE_VERSION_WDDM2_7,
        DXGKDDI_WDDMv2_7);
}

NTSTATUS Bc250BuildActiveInitializationDataForContract(
    DRIVER_INITIALIZATION_DATA *InitializationData,
    ULONG InterfaceVersion,
    DXGK_WDDMVERSION WddmVersion)
{
    return Bc250BuildActiveInitializationDataForProfile(
        InitializationData, InterfaceVersion, WddmVersion,
        BC250_ADAPTER_PROFILE_DISPLAY_RENDER);
}

NTSTATUS Bc250BuildActiveInitializationDataForProfile(
    DRIVER_INITIALIZATION_DATA *InitializationData,
    ULONG InterfaceVersion,
    DXGK_WDDMVERSION WddmVersion,
    BC250_ADAPTER_PROFILE Profile)
{
    if (InitializationData == NULL) return STATUS_INVALID_PARAMETER;
    if (Profile != BC250_ADAPTER_PROFILE_DISPLAY_ONLY &&
        Profile != BC250_ADAPTER_PROFILE_DISPLAY_RENDER &&
        Profile != BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((InterfaceVersion != DXGKDDI_INTERFACE_VERSION_WDDM2_0 ||
         WddmVersion != DXGKDDI_WDDMv2) &&
        (InterfaceVersion != DXGKDDI_INTERFACE_VERSION_WDDM2_7 ||
         WddmVersion != DXGKDDI_WDDMv2_7)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!NT_SUCCESS(Bc250McdmSetWddmContractVersion(WddmVersion))) {
        return STATUS_INVALID_PARAMETER;
    }
    /*
     * MiscCaps.ComputeOnly amarrado ao perfil, e este e' o UNICO ponto do
     * driver que o decide.
     *
     * O destino do projeto e' WDDM display+render de verdade; nos dois perfis
     * de producao o valor e' 0. So' o perfil de laboratorio RENDER_ONLY_LAB,
     * que reporta 0 sources e 0 children, declara 1 — porque para ele essa e'
     * a resposta coerente. Declarar ComputeOnly enquanto se enumera fonte de
     * video foi o que o dxgkrnl rejeitou em 2026-08-31 com Kernel-PnP 411.
     *
     * Escrito como expressao condicional, e nunca como literal solto, para
     * que seja impossivel um perfil de producao herdar o valor do laboratorio
     * por esquecimento de um caminho novo.
     */
    if (!NT_SUCCESS(Bc250McdmSetComputeOnly(
            (Profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) ? 1u : 0u))) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250ProbeRecord(BC250_PROBE_V5_COMPUTE_ONLY_DECLARED,
        Bc250McdmGetComputeOnly());
    RtlZeroMemory(InitializationData, sizeof(*InitializationData));
    InitializationData->Version = InterfaceVersion;
    InitializationData->DxgkDdiAddDevice = Bc250DxgkDdiAddDeviceActive;
    InitializationData->DxgkDdiStartDevice =
        Bc250DxgkDdiStartDeviceV5Telemetry;
    InitializationData->DxgkDdiStopDevice = Bc250DxgkDdiStopDeviceActive;
    InitializationData->DxgkDdiRemoveDevice = Bc250DxgkDdiRemoveDeviceActive;

    Bc250McdmInstallProcessLifecycle(
        Bc250ActiveCreateProcessLifecycle,
        Bc250ActiveDestroyProcessLifecycle);
    Bc250McdmInstallDeviceLifecycle(
        Bc250ActiveCreateDeviceLifecycle,
        Bc250ActiveDestroyDeviceLifecycle);
    Bc250McdmInstallContextLifecycle(
        Bc250ActiveCreateContextLifecycle,
        Bc250ActiveDestroyContextLifecycle);
    Bc250McdmInstallGpuVaLifecycle(
        Bc250ActiveSetRootPageTableLifecycle);
    Bc250McdmInstallAllocationLifecycle(
        Bc250ActiveCreateAllocationLifecycle,
        Bc250ActiveDestroyAllocationLifecycle,
        Bc250ActiveOpenAllocationLifecycle,
        Bc250ActiveCloseAllocationLifecycle);
    Bc250McdmInstallDescribeAllocation(Bc250ActiveDescribeAllocationLifecycle);
    Bc250McdmInstallDebugLifecycle(Bc250ActiveCollectDebugLifecycle);
    Bc250McdmInstallTdrExecutors(
        Bc250ActiveTdrQuiesceLifecycle,
        Bc250ActiveTdrRestartLifecycle);
    Bc250McdmInstallEngineResponsiveProbe(Bc250ActiveEngineResponsiveProbe);
    Bc250McdmInstallRuntimeCapsProvider(Bc250ActiveQueryRuntimeCaps);
    Bc250McdmInstallSubmissionExecutor(Bc250ActiveSubmitCommandVirtualLifecycle);
    Bc250McdmInstallCurrentFenceProvider(Bc250ActiveCurrentFenceProvider);
    Bc250McdmInstallUpdatePageTableHandler(Bc250ActiveUpdatePageTableHandler);
    Bc250McdmInstallFlushTlbHandler(Bc250ActiveFlushTlbHandler);
    Bc250McdmInstallUnloadCleanupHandler(Bc250ActiveDriverUnloadCleanup);
    Bc250DisplayInstallGpuVaResolver(Bc250ActiveResolveDisplayGpuVa);
    Bc250McdmInstallEscapeHandler(Bc250ActiveEscapeHandler);
    (void)Bc250McdmPopulateRequiredCallbacks(InitializationData);
    /*
     * DxgkInitialize valida a tabela completa de display+render antes de
     * consultar DRIVERCAPS.  Estes callbacks vivem separados para que a
     * imagem MCDM readonly continue sem as DDIs graficas, mas o miniport
     * ativo deve instala-los explicitamente.  Ate o 3.67 a funcao existia
     * sem chamador: o dxgkrnl encerrava o post-start com
     * STATUS_INTERNAL_ERROR depois de QueryChildRelations.
     */
    (void)Bc250McdmPopulateRenderCallbacks(InitializationData);

    /*
     * Gate 1.5: o perfil render-only nao anuncia NENHUMA superficie de
     * display.  Nao e' uma poda seletiva — podar Present mantendo VidPn e
     * children apenas trocaria a tela preta por uma rejeicao de tabela ou um
     * Codigo 43 mais cedo.  Ou o adaptador anuncia o conjunto coerente de
     * display, ou nao anuncia display nenhum.
     */
    if (Profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
        Bc250ProbeRecord(BC250_PROBE_V5_DISPLAY_SURFACE_ANNOUNCED, 0u);
        return Bc250DisplayPopulateBaseOnlyCallbacks(InitializationData);
    }

    Bc250ProbeRecord(BC250_PROBE_V5_DISPLAY_SURFACE_ANNOUNCED, 1u);
    return Bc250DisplayPopulateCallbacks(InitializationData);
}

NTSTATUS DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
{
    /*
     * Reset INTEGRAL da telemetria, antes de QUALQUER gravacao deste
     * carregamento.
     *
     * Os resets anteriores eram por familia: cobriam o rastro V5 e o bloco
     * P1/P2, e deixavam Shader*, HqdActive* e Dmm* fora de toda varredura. A
     * corrida 3.75 publicou DmmChildRelationsCalled=56 e
     * DmmIsSupportedCount=41 herdados de boots anteriores, e um
     * ShaderPhysicalReady=1 que nao pode ser atribuido aquela corrida.
     * Evidencia que mistura boots nao e' evidencia.
     *
     * Precisa ficar no TOPO: colocado depois da resolucao de perfil, o
     * proprio reset apagaria os campos que acabaram de ser publicados.
     */
    Bc250ProbeResetAllVolatile();
    Bc250ProbeRecord(BC250_PROBE_DRIVER_ENTRY_REACHED, 1);
    /*
     * Fato de COMPILACAO, nao de execucao: diz qual imagem esta' carregada.
     * Num pacote RENDERONLY_SAFE tem de ser 0. Se vier 1, o binario instalado
     * nao e' o que o manifesto declara, e a corrida esta' invalidada antes de
     * qualquer outra leitura.
     */
    Bc250ProbeRecord(BC250_PROBE_V5_UNSAFE_BRINGUP_COMPILED,
        BC250_UNSAFE_BRINGUP_COMPILED);
    if (DriverObject == NULL || RegistryPath == NULL) {
        Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
            (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    /*
     * O perfil e' resolvido ANTES da tabela porque governa quais callbacks
     * ela recebe.  Ler os tunables aqui tambem garante PASSIVE_LEVEL, que e'
     * requisito de RtlQueryRegistryValues.
     */
    {
        ULONG renderNodePresent = 0u;
        ULONG renderOnlyLabPresent = 0u;
        ULONG renderNode = Bc250ActiveReadTunableChecked(
            L"RenderNode", 0u, &renderNodePresent);
        ULONG renderOnlyLab = Bc250ActiveReadTunableChecked(
            L"RenderOnlyLab", 0u, &renderOnlyLabPresent);
        BC250_ADAPTER_PROFILE profile = BC250_ADAPTER_PROFILE_DISPLAY_ONLY;
        NTSTATUS profileStatus = Bc250ActiveResolveAdapterProfile(
            renderNode, renderNodePresent,
            renderOnlyLab, renderOnlyLabPresent, &profile);

        Bc250ProbeRecord(BC250_PROBE_V5_RENDER_ONLY_LAB_REQUESTED,
            renderOnlyLab);
        Bc250ProbeRecord(BC250_PROBE_V5_RENDER_ONLY_LAB_TUNABLE_PRESENT,
            renderOnlyLabPresent);
        Bc250ProbeRecord(BC250_PROBE_V5_PROFILE_RESOLVE_STATUS,
            (ULONG)profileStatus);

        if (!NT_SUCCESS(profileStatus) ||
            !NT_SUCCESS(Bc250ActiveSetAdapterProfile(profile))) {
            Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
                (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Bc250ProbeRecord(BC250_PROBE_V5_ADAPTER_PROFILE, (ULONG)profile);
    }

    {
        ULONG contractPresent = 0u;
        ULONG contract = Bc250ActiveReadTunableChecked(
            L"WddmContractVersion", 20u, &contractPresent);
        ULONG interfaceVersion;
        DXGK_WDDMVERSION wddmVersion;

        UNREFERENCED_PARAMETER(contractPresent);
        if (contract == 20u) {
            interfaceVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_0;
            wddmVersion = DXGKDDI_WDDMv2;
        } else if (contract == 27u) {
            interfaceVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_7;
            wddmVersion = DXGKDDI_WDDMv2_7;
        } else {
            Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
                (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Bc250ProbeRecord(BC250_PROBE_V5_CONTRACT_VERSION, contract);
        if (!NT_SUCCESS(Bc250BuildActiveInitializationDataForProfile(
                &g_Bc250ActiveInitializationData,
                interfaceVersion, wddmVersion,
                Bc250ActiveGetAdapterProfile()))) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }
    Bc250ProbeRecord(BC250_PROBE_INITIALIZATION_DATA_READY, 1);

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    {
        KMDDOD_INITIALIZATION_DATA dodInit;
        NTSTATUS status;
        BC250_ADAPTER_PROFILE profile = Bc250ActiveGetAdapterProfile();
        ULONG renderNodePresent = 0u;
        ULONG renderNode = Bc250ActiveReadTunableChecked(
            L"RenderNode", 0u, &renderNodePresent);

        /*
         * Zera o rastro deste carregamento antes de registrar no dxgkrnl.
         *
         * Ate o 3.66 o reset era parcial e feito a mao: cobria seis campos
         * V5 e deixava de fora o rastro de DDI, QueryInterface, StopDevice,
         * RemoveDevice e o bloco DMM inteiro. O inspetor do ensaio de
         * 2026-09-01 leu DmmQaiOutSize=544, DmmChildRelationsCalled=56 e
         * DmmControlInterruptCount=14925 — nenhum daquele boot.
         *
         * Bc250ProbeResetSubmissionEvidence estava declarado, implementado e
         * documentado com "chamar em DriverEntry", mas nao possuia UM UNICO
         * chamador em todo o repositorio: o reset P1/P2 que a documentacao
         * afirmava existir nunca executou. Agora executa aqui, antes do
         * bring-up, e a publicacao fisica de P1/P2 acontece depois.
         */
        /* O reset integral ja' ocorreu no topo de DriverEntry. */
        Bc250ProbeRecord(BC250_PROBE_V5_CONTRACT_VERSION,
            Bc250ActiveReadTunable(L"WddmContractVersion", 27u));

        Bc250ProbeRecord(BC250_PROBE_V5_RENDER_NODE_REQUESTED, renderNode);
        Bc250ProbeRecord(BC250_PROBE_V5_RENDER_NODE_TUNABLE_PRESENT,
            renderNodePresent);
        Bc250ProbeRecord(BC250_PROBE_V5_START_DEVICE_STATUS,
            (ULONG)STATUS_NOT_SUPPORTED);
        Bc250ProbeRecord(BC250_PROBE_V5_QAI_LAST_STATUS,
            (ULONG)STATUS_NOT_SUPPORTED);

        Bc250ProbeRecord(BC250_PROBE_V5_ADAPTER_PROFILE, (ULONG)profile);

        if (renderNode > 1u) {
            Bc250ProbeRecord(BC250_PROBE_V5_REGISTRATION_MODE, 0u);
            Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
                (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        /*
         * A escolha da rotina de registro segue o perfil, nao mais o tunable
         * cru: DISPLAY_RENDER e RENDER_ONLY_LAB usam DxgkInitialize com
         * tabelas diferentes; DISPLAY_ONLY continua no KMDOD.  A divergencia
         * entre perfil e RenderNode e' impossivel por construcao — o perfil
         * foi derivado de RenderNode em DriverEntry —, mas conferir aqui
         * mantem o fail-closed local caso alguem insira codigo entre os dois
         * pontos.
         */
        if (profile == BC250_ADAPTER_PROFILE_DISPLAY_RENDER ||
            profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) {
            if (!(renderNodePresent == 1u && renderNode == 1u)) {
                Bc250ProbeRecord(BC250_PROBE_V5_REGISTRATION_MODE, 0u);
                Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
                    (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            Bc250ProbeRecord(BC250_PROBE_V5_REGISTRATION_MODE,
                profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB ? 2u : 1u);
            status = DxgkInitialize(
                DriverObject, RegistryPath, &g_Bc250ActiveInitializationData);
            Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS,
                (ULONG)status);
            return status;
        }

        Bc250ProbeRecord(BC250_PROBE_V5_REGISTRATION_MODE, 0u);
        RtlZeroMemory(&dodInit, sizeof(dodInit));
        dodInit.Version = DXGKDDI_INTERFACE_VERSION_WIN8;
        dodInit.DxgkDdiAddDevice = g_Bc250ActiveInitializationData.DxgkDdiAddDevice;
        dodInit.DxgkDdiStartDevice = g_Bc250ActiveInitializationData.DxgkDdiStartDevice;
        dodInit.DxgkDdiStopDevice = g_Bc250ActiveInitializationData.DxgkDdiStopDevice;
        dodInit.DxgkDdiResetDevice = g_Bc250ActiveInitializationData.DxgkDdiResetDevice;
        dodInit.DxgkDdiRemoveDevice = g_Bc250ActiveInitializationData.DxgkDdiRemoveDevice;
        dodInit.DxgkDdiDispatchIoRequest = g_Bc250ActiveInitializationData.DxgkDdiDispatchIoRequest;
        dodInit.DxgkDdiInterruptRoutine = g_Bc250ActiveInitializationData.DxgkDdiInterruptRoutine;
        dodInit.DxgkDdiDpcRoutine = g_Bc250ActiveInitializationData.DxgkDdiDpcRoutine;
        dodInit.DxgkDdiQueryChildRelations = g_Bc250ActiveInitializationData.DxgkDdiQueryChildRelations;
        dodInit.DxgkDdiQueryChildStatus = g_Bc250ActiveInitializationData.DxgkDdiQueryChildStatus;
        dodInit.DxgkDdiQueryDeviceDescriptor = g_Bc250ActiveInitializationData.DxgkDdiQueryDeviceDescriptor;
        dodInit.DxgkDdiSetPowerState = Bc250DxgkDdiSetPowerState;
        dodInit.DxgkDdiUnload = g_Bc250ActiveInitializationData.DxgkDdiUnload;
        dodInit.DxgkDdiQueryInterface = Bc250DxgkDdiQueryInterface;
        dodInit.DxgkDdiQueryAdapterInfo = Bc250DxgkDdiQueryAdapterInfoDisplay;
        dodInit.DxgkDdiSetPointerPosition = g_Bc250ActiveInitializationData.DxgkDdiSetPointerPosition;
        dodInit.DxgkDdiSetPointerShape = g_Bc250ActiveInitializationData.DxgkDdiSetPointerShape;
        dodInit.DxgkDdiIsSupportedVidPn = g_Bc250ActiveInitializationData.DxgkDdiIsSupportedVidPn;
        dodInit.DxgkDdiRecommendFunctionalVidPn = g_Bc250ActiveInitializationData.DxgkDdiRecommendFunctionalVidPn;
        dodInit.DxgkDdiEnumVidPnCofuncModality = g_Bc250ActiveInitializationData.DxgkDdiEnumVidPnCofuncModality;
        dodInit.DxgkDdiSetVidPnSourceVisibility = g_Bc250ActiveInitializationData.DxgkDdiSetVidPnSourceVisibility;
        dodInit.DxgkDdiCommitVidPn = g_Bc250ActiveInitializationData.DxgkDdiCommitVidPn;
        dodInit.DxgkDdiUpdateActiveVidPnPresentPath = g_Bc250ActiveInitializationData.DxgkDdiUpdateActiveVidPnPresentPath;
        dodInit.DxgkDdiRecommendMonitorModes = g_Bc250ActiveInitializationData.DxgkDdiRecommendMonitorModes;
        dodInit.DxgkDdiQueryVidPnHWCapability = g_Bc250ActiveInitializationData.DxgkDdiQueryVidPnHWCapability;
        dodInit.DxgkDdiPresentDisplayOnly = Bc250DxgkDdiPresentDisplayOnly;
        dodInit.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = g_Bc250ActiveInitializationData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
        dodInit.DxgkDdiSystemDisplayEnable = g_Bc250ActiveInitializationData.DxgkDdiSystemDisplayEnable;
        dodInit.DxgkDdiSystemDisplayWrite = g_Bc250ActiveInitializationData.DxgkDdiSystemDisplayWrite;
        dodInit.DxgkDdiGetChildContainerId = Bc250DxgkDdiGetChildContainerId;
        dodInit.DxgkDdiControlInterrupt = Bc250DxgkDdiControlInterrupt;
        dodInit.DxgkDdiControlEtwLogging = Bc250DxgkDdiControlEtwLogging;
        /*
         * DIVIDA TECNICA DELIBERADA E TEMPORARIA — isolamento do Gate 1.5.
         *
         * O stub local de CollectDbgInfo e SetPalette voltam para que a
         * tabela KMDOD do 3.76 seja byte a byte a da baseline conhecida. A
         * troca por Bc250McdmCollectDbgInfo e a retirada de SetPalette estao
         * certas, mas pertencem ao candidato de limpeza, nao a este.
         * Ver o comentario extenso em Bc250DisplayPopulateCallbacks.
         */
        dodInit.DxgkDdiCollectDbgInfo = Bc250DxgkDdiCollectDbgInfo;
        dodInit.DxgkDdiNotifyAcpiEvent = Bc250DxgkDdiNotifyAcpiEvent;
        dodInit.DxgkDdiSetPalette = Bc250DxgkDdiSetPalette;
        dodInit.DxgkDdiGetScanLine = Bc250DxgkDdiGetScanLine;
        dodInit.DxgkDdiNotifySurpriseRemoval = Bc250DxgkDdiNotifySurpriseRemoval;

        status = DxgkInitializeDisplayOnlyDriver(DriverObject, RegistryPath, &dodInit);
        Bc250ProbeRecord(BC250_PROBE_DXGK_INITIALIZE_STATUS, (ULONG)status);
        return status;
    }
#else
    return STATUS_SUCCESS;
#endif
}
static void Bc250ActiveTlbStall(void *Context, uint32_t Microseconds)
{
    UNREFERENCED_PARAMETER(Context);
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeStallExecutionProcessor(Microseconds);
#else
    UNREFERENCED_PARAMETER(Microseconds);
#endif
}

static void Bc250ActivePspBarrier(void *Context)
{
    UNREFERENCED_PARAMETER(Context);
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeMemoryBarrier();
#endif
}

static uint32_t Bc250ActiveVmidRead32(
    void *Context, uint32_t ByteOffset, uint32_t *Value)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    if (active == NULL) return 0u;
    return Bc250MmioTopologyRead32(
        &active->Core.MmioTransport, ByteOffset, Value);
}

static uint32_t Bc250ActiveVmidWrite32(
    void *Context, uint32_t ByteOffset, uint32_t Value)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    if (active == NULL) return 0u;
    return Bc250MmioTopologyWrite32(
        &active->Core.MmioTransport, ByteOffset, Value);
}

static uint32_t Bc250ActiveFlushVmidTlb(void *Context, uint32_t Vmid)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    BC250_PAGING_FLUSH_TLB_PARAMS params;
    BC250_GFXHUB_TLB_FLUSH_PLAN plan;
    BC250_TLB_TRANSACTION_INTERFACE interfaceData;
    BC250_TLB_TRANSACTION_EVIDENCE localEvidence;
    BC250_TLB_TRANSACTION_STATUS localStatus;
    uint32_t success = 0u;
    KIRQL oldIrql = PASSIVE_LEVEL;

    if (active == NULL) return 0u;

    RtlZeroMemory(&params, sizeof(params));
    params.Vmid = Vmid;
    params.HubIndex = 0u;
    params.FlushType = 0u;
    if (Bc250PagingBuildGfxhubTlbFlushPlan(&params, &plan) !=
        BC250_PAGING_OK) {
        active->TlbTransactionStatus = BC250_TLB_TRANSACTION_BAD_PLAN;
        return 0u;
    }
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Stall = Bc250ActiveTlbStall;
    interfaceData.Context = &active->Core.MmioTransport;
    interfaceData.Identity = BC250_TLB_TRANSPORT_IDENTITY;

    KeAcquireSpinLock(&active->TlbLock, &oldIrql);

    RtlZeroMemory(&localEvidence, sizeof(localEvidence));
    localStatus = Bc250RunGfxhubTlbTransaction(
        &plan, &interfaceData, 1000u, &localEvidence);

    active->TlbTransactionStatus = localStatus;
    active->TlbTransactionEvidence = localEvidence;

    KeReleaseSpinLock(&active->TlbLock, oldIrql);

    Bc250DisplayLog("[FlushMappedGfxhubTlb] Transport State=%u, Base=%p, Len=%llu\n",
        active->Core.MmioTransport.State,
        active->Core.MmioTransport.VirtualBase,
        active->Core.MmioTransport.Length);
    Bc250DisplayLog("[FlushMappedGfxhubTlb] Transaction status=%u, PhysReady=%u, AckObserved=%u, Polls=%u\n",
        (uint32_t)localStatus,
        localEvidence.PhysicalReady,
        localEvidence.AckObserved,
        localEvidence.PollCount);

    success = (localStatus == BC250_TLB_TRANSACTION_OK &&
        localEvidence.PhysicalReady != 0u) ? 1u : 0u;
    return success;
}

static uint32_t Bc250ActiveFlushVmid8Only(void *Context)
{
    return Bc250ActiveFlushVmidTlb(Context, BC250_GCVM_VMID8);
}

static uint32_t Bc250ActiveFlushUserVmidOnly(void *Context)
{
    return Bc250ActiveFlushVmidTlb(Context, BC250_GCVM_USER_VMID);
}

static uint32_t Bc250ActiveFlushComputeTlb(void *Context)
{
    return Bc250ActiveFlushVmidTlb(Context, BC250_GCVM_COMPUTE_VMID);
}

static uint32_t Bc250ActiveFlushMappedGfxhubTlb(void *Context)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    uint32_t gfxhubOk;

    /* VidMm updates may affect paging (VMID 8), user process (VMID 7), or compute (VMID 9). */
    gfxhubOk = Bc250ActiveFlushVmidTlb(Context, BC250_GCVM_VMID8) &&
        Bc250ActiveFlushVmidTlb(Context, BC250_GCVM_USER_VMID) &&
        Bc250ActiveFlushComputeTlb(Context);

    /*
     * Invalidação da micro-TLB UTCL1 da SDMA0.
     * Quando o DWM cicla e o VidMm desaloca ou recria primarias (ex: 11 MB),
     * a UTCL1 pode reter traduções antigas se nao for invalidada junto com
     * o GFXHUB, causando filas de requisicao cheias (WRREQ_DATA_FIFO_FULL /
     * RQPG_REQPAGE_FIFO_FULL) e stall no motor SDMA durante o fill.
     */
    if (active != NULL &&
        active->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
        uint32_t utcl1Val = 0u;
        if (Bc250MmioTopologyRead32(&active->Core.MmioTransport,
                BC250_SDMA0_REG_UTCL1_CNTL, &utcl1Val) == BC250_MMIO_TOPOLOGY_OK) {
            utcl1Val |= BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION;
            (void)Bc250MmioTopologyWrite32(&active->Core.MmioTransport,
                BC250_SDMA0_REG_UTCL1_CNTL, utcl1Val);
        }
    }

    return gfxhubOk;
}

static uint32_t Bc250ActiveExecuteGcvmBacking(
    void *Context, uint64_t PageTablePhysical)
{
    BC250_DXGK_ACTIVE_CONTEXT *active =
        (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    BC250_GCVM_VMID8_INTERFACE interfaceData;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;

    if (active == NULL) return 0u;
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &active->Core.MmioTransport, &active->Core.DxgkInterface,
        active->Core.ResolvedBars, active->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) return 0u;
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250ActiveVmidRead32;
    interfaceData.Write32 = Bc250ActiveVmidWrite32;
    interfaceData.Flush = Bc250ActiveFlushMappedGfxhubTlb;
    interfaceData.Context = active;
    interfaceData.Identity = BC250_GCVM_VMID8_IDENTITY;
    {
        uint32_t cntlBefore = 0u;
        if (interfaceData.Read32(interfaceData.Context, BC250_GCVM_VMID8_CNTL, &cntlBefore) &&
            (cntlBefore & BC250_GCVM_ENABLE_CONTEXT) != 0u) {
            BC250_GCVM_VMID8_INSTALL_EVIDENCE cleanEv;
            RtlZeroMemory(&cleanEv, sizeof(cleanEv));
            (void)Bc250GcvmVmid8Uninstall(&interfaceData, &cleanEv);
        }
    }
    active->GcvmVmid8Status = Bc250RunGcvmVmid8InertTransaction(
        PageTablePhysical, &interfaceData, &active->GcvmVmid8Evidence);

    mmioStatus = Bc250MmioTopologyUnmap(&active->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        active->GcvmVmid8Evidence.PhysicalReady = 0u;
        return 0u;
    }
    return active->GcvmVmid8Status == BC250_GCVM_VMID8_OK &&
        active->GcvmVmid8Evidence.PhysicalReady != 0u;
}

static BC250_PSP_RING_STATUS Bc250ActiveProbePspRing(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PSP_RING_EVIDENCE *Evidence)
{
    PHYSICAL_ADDRESS low, high, boundary, ringPhysical;
    PVOID ringVirtual;
    BC250_PSP_RING_INTERFACE interfaceData;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_PSP_RING_STATUS status;

    if (Context == NULL || Evidence == NULL) return BC250_PSP_RING_BAD_ARGUMENT;
    low.QuadPart = 0;
    high.QuadPart = 0xFFFFFFFFull;
    boundary.QuadPart = 0;
    ringVirtual = MmAllocateContiguousMemorySpecifyCache(
        BC250_PSP_RING_BYTES, low, high, boundary, MmNonCached);
    if (ringVirtual == NULL) return BC250_PSP_RING_BAD_ARGUMENT;
    RtlZeroMemory(ringVirtual, BC250_PSP_RING_BYTES);
    ringPhysical = MmGetPhysicalAddress(ringVirtual);
    if (ringPhysical.QuadPart == 0 ||
        (ringPhysical.QuadPart & (BC250_PSP_RING_BYTES - 1u)) != 0u ||
        (ULONGLONG)ringPhysical.QuadPart > 0xFFFFFFFFull) {
        MmFreeContiguousMemory(ringVirtual);
        return BC250_PSP_RING_BAD_ARGUMENT;
    }
    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        MmFreeContiguousMemory(ringVirtual);
        return BC250_PSP_RING_READ_FAILED;
    }
    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250ActiveVmidRead32;
    interfaceData.Write32 = Bc250ActiveVmidWrite32;
    interfaceData.Stall = Bc250ActiveTlbStall;
    interfaceData.Context = Context;
    interfaceData.Identity = BC250_PSP_RING_IDENTITY;
    status = Bc250RunPspRingTransaction(
        (uint64_t)ringPhysical.QuadPart, &interfaceData, 500u, Evidence);
    mmioStatus = Bc250MmioTopologyUnmap(&Context->Core.MmioTransport);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK && status == BC250_PSP_RING_OK) {
        status = BC250_PSP_RING_RESTORE_FAILED;
        Evidence->PhysicalReady = 0u;
    }
    if (Evidence->DestroyPassed != 0u ||
        status == BC250_PSP_RING_TOS_NOT_READY ||
        status == BC250_PSP_RING_INHERITED_ACTIVE ||
        status == BC250_PSP_RING_READ_FAILED ||
        status == BC250_PSP_RING_BAD_ARGUMENT) {
        MmFreeContiguousMemory(ringVirtual);
    } else {
        Context->DeferredPspRingVirtual = ringVirtual;
    }
    return status;
}
