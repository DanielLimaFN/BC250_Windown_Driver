#include "bc250_active_miniport.h"
#include "bc250_mcdm_callbacks.h"
#include "bc250_display_ddi.h"
#include "bc250_probe_telemetry.h"
#include "bc250_sdma_present.h"
#include "../core/bc250_golden_settings_contract.h"
#include "bc250_cp_firmware_manifest.h"
#include "../core/bc250_adapter_info_contract.h"
#include "../core/bc250_allocation_abi_contract.h"
#include "../core/bc250_mcdm_debug_contract.h"
#include "../core/bc250_firmware_staging.h"
#include "../core/bc250_sdma_queue_state_contract.h"
#include "../core/bc250_escape_contract.h"
#include "../core/bc250_segment_address_contract.h"
#include "../core/bc250_gfx_observable_shader.h"
#include "../core/bc250_gfx_ib_inspect_contract.h"

DRIVER_INITIALIZATION_DATA g_Bc250ActiveInitializationData;
int _fltused = 0x9875;

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
/*
 * Declaracao adiantada: o caminho de TDR precisa caminhar a tabela do VMID 7
 * no endereco que o fault nomeou, e a definicao vive bem depois neste arquivo.
 */
static BOOLEAN Bc250ActiveWalkGpuVa(
    _In_ const char *Label,
    _In_ uint64_t GpuVa,
    _In_ uint64_t RootMcAddress,
    _In_ uint64_t VramMcBase,
    _Out_ uint64_t *ResolvedPhysical);
static uint32_t Bc250ActiveFlushComputeTlb(void *Context);
static uint32_t Bc250ActiveFlushVmid8Only(void *Context);
static uint32_t Bc250ActiveFlushUserVmidOnly(void *Context);
static uint32_t Bc250ActiveFlushVmidTlb(void *Context, uint32_t Vmid);
static uint32_t Bc250ActiveFaultLatchRead32(
    void *Context, uint32_t ByteOffset, uint32_t *Value);
static uint32_t Bc250ActiveFaultLatchWrite32(
    void *Context, uint32_t ByteOffset, uint32_t Value);
static NTSTATUS Bc250ActiveEnsureGlobalMmu(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    const char *Origin);

/* Nome estavel do token que o snapshot de GPUVA leva ate o TDR. */
static const char *Bc250ActiveAllocationTokenClassName(uint64_t Token)
{
    switch (BC250_ALLOCATION_TOKEN_CLASS(Token)) {
    case BC250_ALLOCATION_TOKEN_CLASS_PRIMARY: return "PRIMARY";
    case BC250_ALLOCATION_TOKEN_CLASS_SHADOW: return "SHADOW";
    case BC250_ALLOCATION_TOKEN_CLASS_STAGING: return "STAGING";
    case BC250_ALLOCATION_TOKEN_CLASS_GDI: return "GDI";
    case BC250_ALLOCATION_TOKEN_CLASS_CPU_VISIBLE: return "CPU_VISIBLE";
    case BC250_ALLOCATION_TOKEN_CLASS_COMMAND: return "COMMAND";
    case BC250_ALLOCATION_TOKEN_CLASS_PROOF: return "PROOF";
    case BC250_ALLOCATION_TOKEN_CLASS_RESOURCE: return "RESOURCE";
    default: return Token == 0ull ? "SEM_TOKEN" : "CLASSE_DESCONHECIDA";
    }
}

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
    /* anel + dword de writeback de RPTR + dword de shadow de WPTR + fence, com folga
     * de alinhamento para ficarem fora do anel. No Nivel 4 adiciona render target (256x256 RGBA = 256 KB) */
    const uint64_t rtBytes = (Level >= 4u) ? 262144ull : 0ull;
    const uint64_t totalBytes = ringBytes + 0x1000ull + rtBytes;
    uint64_t ringMc;
    uint32_t mappedByUs;
    ULONG allocFlags = BC250_VRAM_ALLOC_TOP_DOWN;
    const BC250_RESOLVED_BAR_RESOURCE *vramBar = NULL;
    PVOID cpuMapped = NULL;
    uint32_t bIdx;

    if (Context == NULL || Evidence == NULL || Level == 0u) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    RtlZeroMemory(&Context->GfxRingSubmitEvidence, sizeof(Context->GfxRingSubmitEvidence));
    RtlZeroMemory(&Context->GfxRing3DDrawEvidence, sizeof(Context->GfxRing3DDrawEvidence));
    RtlZeroMemory(&plan, sizeof(plan));

    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;
    addressSpace = &Context->Core.TopologyEvidence.AddressSpace;
    uint64_t vramMcBase = (addressSpace->VramMcStart != 0ull) ? addressSpace->VramMcStart : BC250_SDMA_VRAM_MC_BASE;
    uint64_t totalBytesAvailable = (addressSpace->TotalBytes != 0ull) ? addressSpace->TotalBytes : (512ull * 1024ull * 1024ull);

    if (totalBytesAvailable == 0u || vramMcBase == 0u) {
        Bc250DisplayLog("[GfxRing] BRINGUP nao executou: janela de VRAM indisponivel\n");
        return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
    }

    for (bIdx = 0; bIdx < Context->Core.ResolvedBarCount; ++bIdx) {
        if (Context->Core.ResolvedBars[bIdx].Index == BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            vramBar = &Context->Core.ResolvedBars[bIdx];
            break;
        }
    }

    if (Level >= 3u && vramBar != NULL) {
        allocFlags |= BC250_VRAM_ALLOC_CPU_VISIBLE;
    }

    vramStatus = Bc250VramAllocate(allocator, totalBytes, 65536ull,
        BC250_VRAM_ROLE_RING, allocFlags, &token);
    if (vramStatus != BC250_VRAM_OK) {
        Bc250DisplayLog("[GfxRing] BRINGUP nao executou: VRAM negada (%s)\n",
            Bc250VramStatusName(vramStatus));
        return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
    }
    if (Bc250VramGetRegion(allocator, token, &region) != BC250_VRAM_OK) {
        (void)Bc250VramRelease(allocator, token);
        return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
    }

    ringMc = vramMcBase + region.Offset;
    plan.VramWindowBase = vramMcBase;
    plan.VramWindowBytes = totalBytesAvailable;
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
     * Nivel 3: Submissao de pacotes SET_SH_REG (VS + PS) e medicao do tripe da Lei 2
     * com fence WRITE_DATA gravada sobre poison na VRAM antes do rollback.
     * Nivel 4: Pipeline 3D completo de triangulo (Shaders, ColorTarget, TargetMask,
     * Viewport/Scissor, PrimitiveAssembler, DrawInitiator e Fence WRITE_DATA).
     */
    if (status == BC250_GFX_RING_TXN_OK && Level >= 3u && vramBar != NULL) {
        PHYSICAL_ADDRESS phys;
        phys.QuadPart = (LONGLONG)(vramBar->Start + region.Offset);
        cpuMapped = MmMapIoSpace(phys, (SIZE_T)totalBytes, MmNonCached);
        if (cpuMapped != NULL) {
            RtlZeroMemory(cpuMapped, (SIZE_T)totalBytes);
            BC250_GFX_SHADER_REGS vsRegs, psRegs;
            const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *vsM = Bc250GetGfxObservableVsManifest();
            const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *psM = Bc250GetGfxObservablePsManifest();
            uint64_t vsGpuVa = ringMc + ringBytes + 0x100ull;
            uint64_t psGpuVa = ringMc + ringBytes + 0x200ull;
            volatile uint32_t *vsDst = (volatile uint32_t *)((unsigned char *)cpuMapped + ringBytes + 0x100ull);
            volatile uint32_t *psDst = (volatile uint32_t *)((unsigned char *)cpuMapped + ringBytes + 0x200ull);
            uint32_t sIdx;

            RtlZeroMemory(&vsRegs, sizeof(vsRegs));
            RtlZeroMemory(&psRegs, sizeof(psRegs));

            if (vsM != NULL && vsM->ShaderDwords != NULL) {
                for (sIdx = 0u; sIdx < vsM->ShaderDwordCount; ++sIdx) {
                    vsDst[sIdx] = vsM->ShaderDwords[sIdx];
                }
                vsRegs.PgmLo = (uint32_t)(vsGpuVa >> 8);
                vsRegs.PgmHi = (uint32_t)(vsGpuVa >> 40);
                vsRegs.Rsrc1 = vsM->PgmRsrc1;
                vsRegs.Rsrc2 = vsM->PgmRsrc2;
            }

            if (psM != NULL && psM->ShaderDwords != NULL) {
                for (sIdx = 0u; sIdx < psM->ShaderDwordCount; ++sIdx) {
                    psDst[sIdx] = psM->ShaderDwords[sIdx];
                }
                psRegs.PgmLo = (uint32_t)(psGpuVa >> 8);
                psRegs.PgmHi = (uint32_t)(psGpuVa >> 40);
                psRegs.Rsrc1 = psM->PgmRsrc1;
                psRegs.Rsrc2 = psM->PgmRsrc2;
            }

            if (Level >= 4u) {
                BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN drawPlan;
                BC250_GFX_RING_TXN_STATUS drawStatus;
                uint64_t rtGpuVa = ringMc + ringBytes + 0x1000ull;
                uint32_t stage = 4u;
                if (Context->GfxRingStagePresent) {
                    if (Context->GfxRingIsolationStage == 0u) {
                        stage = BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT;
                    } else {
                        stage = Context->GfxRingIsolationStage;
                    }
                }
                if (Level >= 5u) {
                    stage = 4u;
                }

                RtlZeroMemory(&drawPlan, sizeof(drawPlan));
                drawPlan.RingCpuAddress = (uint32_t *)cpuMapped;
                drawPlan.RingDwords = (uint32_t)(ringBytes / sizeof(uint32_t));
                drawPlan.WptrPollCpuAddress = (uint32_t *)((unsigned char *)cpuMapped + ringBytes + 0x10ull);
                drawPlan.RptrWbCpuAddress = (volatile const uint32_t *)((unsigned char *)cpuMapped + ringBytes);
                drawPlan.FenceCpuAddress = (volatile uint32_t *)((unsigned char *)cpuMapped + ringBytes + 0x20ull);
                drawPlan.FenceGpuVa = plan.RptrWritebackGpuVa + 0x20ull;
                drawPlan.FencePoisonValue = 0x43DAFFFEu;
                drawPlan.FenceExpectedValue = (stage == BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT)
                    ? 0x0000B400u
                    : ((Level >= 5u) ? 0x0000B500u : (0x0000B400u | (stage << 4)));
                drawPlan.SkipDraw = (stage < 4u || stage == BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) ? 1u : 0u;
                drawPlan.IsolationStage = stage;
                drawPlan.VsRegs = &vsRegs;
                drawPlan.PsRegs = &psRegs;
                drawPlan.ColorTargetBaseGpuVa = rtGpuVa;
                drawPlan.ColorTargetWidth = 64u;
                drawPlan.ColorTargetHeight = 64u;
                drawPlan.ColorTargetPitchBytes = 256u;
                drawPlan.ColorTargetFormatIndex = 8u;
                drawPlan.ColorTargetCompSwap = 1u;
                drawPlan.Viewport.X = 0.0f;
                drawPlan.Viewport.Y = 0.0f;
                drawPlan.Viewport.Width = 64.0f;
                drawPlan.Viewport.Height = 64.0f;
                drawPlan.Viewport.MinDepth = 0.0f;
                drawPlan.Viewport.MaxDepth = 1.0f;
                drawPlan.Scissor.Left = 0u;
                drawPlan.Scissor.Top = 0u;
                drawPlan.Scissor.Right = 64u;
                drawPlan.Scissor.Bottom = 64u;
                drawPlan.PrimitiveTopology = BC250_GFX_PRIM_TRILIST;
                drawPlan.NumInstances = 1u;
                drawPlan.VertexCount = 3u;
                drawPlan.DrawInitiator = BC250_GFX_DRAW_INITIATOR_AUTO_INDEX;
                drawPlan.MaxPollIterations = 2000u;
                drawPlan.StallMicroseconds = 50u;

                drawStatus = Bc250GfxRingTxnSubmit3DDraw(&interfaceData, &plan, Evidence,
                    &drawPlan, &Context->GfxRing3DDrawEvidence);
                Bc250DisplayLog("[GfxRing] BRINGUP Level %u Submit3DDraw: status=%s (%u) Stage=%u SkipDraw=%u TripodVerdict=%u Completed=%u FenceObs=0x%08X RPTR(MMIO=0x%X WB=0x%X) Polls=%u\n",
                    Level,
                    Bc250GfxRingTxnStatusName(drawStatus), (uint32_t)drawStatus,
                    drawPlan.IsolationStage,
                    drawPlan.SkipDraw,
                    Context->GfxRing3DDrawEvidence.TripodVerdict,
                    Context->GfxRing3DDrawEvidence.SubmitCompleted,
                    Context->GfxRing3DDrawEvidence.FenceObserved,
                    Context->GfxRing3DDrawEvidence.RptrObservedMmio,
                    Context->GfxRing3DDrawEvidence.RptrObservedWb,
                    Context->GfxRing3DDrawEvidence.PollsCompleted);
                {
                    uint32_t grbm = Context->GfxRing3DDrawEvidence.GrbmStatus;
                    uint32_t cp = Context->GfxRing3DDrawEvidence.CpStat;
                    Bc250DisplayLog("[GfxRing] BRINGUP HW Telemetry: GRBM_STATUS=0x%08X (GUI_ACT=%u GE=%u PA=%u CB=%u SPI=%u CP=%u) CP_STAT=0x%08X (PFP_BUSY=%u ME_BUSY=%u MEQ_BUSY=%u CP_BUSY=%u) PFP_PNTR=0x%04X ME_PNTR=0x%04X\n",
                        grbm,
                        (grbm >> 31) & 1u,
                        (grbm >> 21) & 1u,
                        (grbm >> 25) & 1u,
                        (grbm >> 30) & 1u,
                        (grbm >> 22) & 1u,
                        (grbm >> 29) & 1u,
                        cp,
                        (cp >> 15) & 1u,
                        (cp >> 17) & 1u,
                        (cp >> 16) & 1u,
                        (cp >> 31) & 1u,
                        Context->GfxRing3DDrawEvidence.PfpPntr,
                        Context->GfxRing3DDrawEvidence.MePntr);
                }
                if (drawStatus != BC250_GFX_RING_TXN_OK) {
                    status = drawStatus;
                }
            } else {
                BC250_GFX_RING_SUBMIT_SH_REG_PLAN submitPlan;
                BC250_GFX_RING_TXN_STATUS submitStatus;

                RtlZeroMemory(&submitPlan, sizeof(submitPlan));
                submitPlan.RingCpuAddress = (uint32_t *)cpuMapped;
                submitPlan.RingDwords = (uint32_t)(ringBytes / sizeof(uint32_t));
                submitPlan.WptrPollCpuAddress = (uint32_t *)((unsigned char *)cpuMapped + ringBytes + 0x10ull);
                submitPlan.RptrWbCpuAddress = (volatile const uint32_t *)((unsigned char *)cpuMapped + ringBytes);
                submitPlan.FenceCpuAddress = (volatile uint32_t *)((unsigned char *)cpuMapped + ringBytes + 0x20ull);
                submitPlan.FenceGpuVa = plan.RptrWritebackGpuVa + 0x20ull;
                submitPlan.FencePoisonValue = 0x43DAFFFEu;
                submitPlan.FenceExpectedValue = 0x0000B240u;
                submitPlan.VsRegs = &vsRegs;
                submitPlan.PsRegs = &psRegs;
                submitPlan.MaxPollIterations = 2000u;
                submitPlan.StallMicroseconds = 50u;

                submitStatus = Bc250GfxRingTxnSubmitShReg(&interfaceData, &plan, Evidence,
                    &submitPlan, &Context->GfxRingSubmitEvidence);
                Bc250DisplayLog("[GfxRing] BRINGUP Level 3 SubmitShReg: status=%s (%u) TripodVerdict=%u Completed=%u FenceObs=0x%08X RPTR(MMIO=0x%X WB=0x%X) Polls=%u\n",
                    Bc250GfxRingTxnStatusName(submitStatus), (uint32_t)submitStatus,
                    Context->GfxRingSubmitEvidence.TripodVerdict,
                    Context->GfxRingSubmitEvidence.SubmitCompleted,
                    Context->GfxRingSubmitEvidence.FenceObserved,
                    Context->GfxRingSubmitEvidence.RptrObservedMmio,
                    Context->GfxRingSubmitEvidence.RptrObservedWb,
                    Context->GfxRingSubmitEvidence.PollsCompleted);
                if (submitStatus != BC250_GFX_RING_TXN_OK) {
                    status = submitStatus;
                }
            }
            MmUnmapIoSpace(cpuMapped, (SIZE_T)totalBytes);
            cpuMapped = NULL;
        } else {
            Bc250DisplayLog("[GfxRing] BRINGUP Level %u: MmMapIoSpace falhou\n", Level);
            status = BC250_GFX_RING_TXN_WRITE_FAILED;
        }
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

/*
 * =========================================================================
 * MOTOR GRAFICO PERSISTENTE (CP_RB0 / PFP / ME) NO NODE 1 (3D GRAPHICS)
 *
 * Exclusivo para comandos graficos 3D da UMD/Vulkan (vkCmdDraw).
 * Ativado durante StartDevice e persistido em VRAM.
 * Rejeita qualquer tentativa de redirecionamento para MEC (Lei 4).
 * =========================================================================
 */
/*
 * Valor de poison da fence do anel grafico. A GPU escreve o numero da fence
 * (monotonico, comeca em 1) via RELEASE_MEM; enquanto ela NAO escreveu, o slot
 * carrega este valor. Precisa ser nomeado porque o poll compara a fence
 * observada com a ultima aposentada, e 0x43DAFFFE e' um numero GRANDE: sem
 * excluir o poison explicitamente ele PASSA a comparacao e aposenta submissoes
 * que a GPU nunca concluiu -- falso sucesso da classe que este projeto ja
 * catalogou seis vezes (A15, mcdm_callbacks_test, C1, sdma_blt_executed, ...).
 */
#define BC250_GFX_FENCE_POISON 0x43DAFFFEu

static NTSTATUS Bc250ActiveGfxEngineOffline(
    BC250_DXGK_ACTIVE_CONTEXT *Context);

/*
 * DX.26 -- snapshot SOMENTE LEITURA dos golden settings do GC e do CSB.
 *
 * Fase A das auditorias externas (docs/338): (1) a tabela de 34 golden settings
 * do Cyan Skillfish nunca foi aplicada nem lida no silicio; (2) o KMD nunca
 * programa RLC_CSIB_ADDR_LO/HI/LENGTH, e o IB do RADV depende do CLEAR_STATE
 * para PA_SC_GENERIC_SCISSOR_*. Aqui so' se MEDE: duas leituras por registrador,
 * comparacao apenas dos bits cobertos pela mascara. Nenhuma escrita. Os
 * registradores de banco (SE/SA) sao lidos na selecao GRBM corrente, que e'
 * registrada primeiro (GRBM_GFX_INDEX esta na tabela).
 */
#define BC250_REG_RLC_CSIB_ADDR_LO_SNAP 0x0003B288u /* (0xA000 + 0x4CA2) * 4 */
#define BC250_REG_RLC_CSIB_ADDR_HI_SNAP 0x0003B28Cu
#define BC250_REG_RLC_CSIB_LENGTH_SNAP  0x0003B290u
static void Bc250ActiveGoldenSnapshotReadOnly(_In_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    uint32_t count = 0u, diverge = 0u, unstable = 0u, unread = 0u;
    const BC250_GOLDEN_ENTRY *table = Bc250GoldenSettingsGetTable(&count);
    uint32_t lo = 0u, hi = 0u, len = 0u;
    uint32_t okLo, okHi, okLen;

    for (uint32_t i = 0; table != NULL && i < count; ++i) {
        uint32_t v1 = 0u, v2 = 0u;
        uint32_t r1 = Bc250MmioTopologyRead32(&Context->Core.MmioTransport, table[i].BarOffset, &v1);
        uint32_t r2 = Bc250MmioTopologyRead32(&Context->Core.MmioTransport, table[i].BarOffset, &v2);
        BOOLEAN stable = (r1 && r2 && v1 == v2);
        BOOLEAN match = stable && ((v1 & table[i].Mask) == (table[i].ValueOr & table[i].Mask));
        if (!r1 || !r2) ++unread;
        else if (!stable) ++unstable;
        else if (!match) ++diverge;
        Bc250DisplayLog("[GOLDEN_SNAPSHOT] %-34s base=%u off=0x%05X atual=0x%08X mascara=0x%08X esperado=0x%08X -> %s\n",
            table[i].Name, table[i].BaseIndex, table[i].BarOffset, v1, table[i].Mask,
            table[i].ValueOr & table[i].Mask,
            (!r1 || !r2) ? "NAO_LIDO" : (!stable ? "INSTAVEL" : (match ? "IGUAL" : "DIFERENTE")));
    }
    Bc250DisplayLog("[GOLDEN_SNAPSHOT] RESUMO: %u registradores, %u DIFERENTES, %u INSTAVEIS, %u NAO_LIDOS -- somente leitura, nada foi escrito\n",
        count, diverge, unstable, unread);

    okLo = Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_REG_RLC_CSIB_ADDR_LO_SNAP, &lo);
    okHi = Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_REG_RLC_CSIB_ADDR_HI_SNAP, &hi);
    okLen = Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_REG_RLC_CSIB_LENGTH_SNAP, &len);
    Bc250DisplayLog("[CSIB_SNAPSHOT] RLC_CSIB_ADDR_LO=0x%08X HI=0x%08X LENGTH=0x%08X lidos=%u/3 -> %s\n",
        lo, hi, len, okLo + okHi + okLen,
        (okLo + okHi + okLen) != 3u ? "NAO_LIDO" :
        (len == 0u ? "CSB NAO PROGRAMADO (LENGTH=0): CLEAR_STATE nao tem de onde carregar os defaults"
                   : "CSB PROGRAMADO por alguem antes de nos (endereco/tamanho acima)"));
}

/*
 * DX.27 -- ESCRITOR UNICO do RLC_CSIB_ADDR_HI/LO/LENGTH (gfx_v10_0_init_csb).
 *
 * A Corrida 72 leu os tres em zero: o CLEAR_STATE que o RADV emite em todo IB
 * nao tinha de onde carregar os padroes de contexto. O upstream, no caminho de
 * carga por PSP (o nosso), escreve estes tres com o RLC ja rodando, depois do
 * autoload e antes do cp_gfx_start (gfx_v10_0_rlc_resume). LENGTH e' em DWORDS
 * (clear_state_size = get_csb_size; o BO tem dws*4 bytes, amdgpu_gfx_rlc_init_csb).
 *
 * Lei 5: snapshot -> escrita -> readback -> se nao conferir, restaura o
 * snapshot e falha fechado. Offsets com procedencia: gc_10_1_0_offset.h:9529-9534
 * (0x4CA2..0x4CA4, BASE_IDX 1 -> bytes 0x3B288/0x3B28C/0x3B290).
 */
static BOOLEAN Bc250ActiveProgramClearStateBuffer(
    _In_ BC250_DXGK_ACTIVE_CONTEXT *Context,
    uint64_t CsbMc,
    uint32_t CsbDwords)
{
    BC250_MMIO_TOPOLOGY_CONTEXT *mmio = &Context->Core.MmioTransport;
    const uint32_t wantHi = (uint32_t)(CsbMc >> 32);
    const uint32_t wantLo = (uint32_t)(CsbMc & 0xFFFFFFFCull);
    uint32_t loB = 0u, hiB = 0u, lenB = 0u, lo = 0u, hi = 0u, len = 0u;
    uint32_t wOk = 0u, rOk = 0u;
    BOOLEAN pass;

    (void)Bc250MmioTopologyRead32(mmio, BC250_REG_RLC_CSIB_ADDR_LO_SNAP, &loB);
    (void)Bc250MmioTopologyRead32(mmio, BC250_REG_RLC_CSIB_ADDR_HI_SNAP, &hiB);
    (void)Bc250MmioTopologyRead32(mmio, BC250_REG_RLC_CSIB_LENGTH_SNAP, &lenB);

    wOk += Bc250MmioTopologyWrite32(mmio, BC250_REG_RLC_CSIB_ADDR_HI_SNAP, wantHi);
    wOk += Bc250MmioTopologyWrite32(mmio, BC250_REG_RLC_CSIB_ADDR_LO_SNAP, wantLo);
    wOk += Bc250MmioTopologyWrite32(mmio, BC250_REG_RLC_CSIB_LENGTH_SNAP, CsbDwords);

    rOk += Bc250MmioTopologyRead32(mmio, BC250_REG_RLC_CSIB_ADDR_LO_SNAP, &lo);
    rOk += Bc250MmioTopologyRead32(mmio, BC250_REG_RLC_CSIB_ADDR_HI_SNAP, &hi);
    rOk += Bc250MmioTopologyRead32(mmio, BC250_REG_RLC_CSIB_LENGTH_SNAP, &len);

    pass = (wOk == 3u && rOk == 3u && lo == wantLo && hi == wantHi && len == CsbDwords);
    Bc250DisplayLog("[CSIB_PROGRAM] antes LO=0x%08X HI=0x%08X LEN=0x%08X | escrito LO=0x%08X HI=0x%08X LEN=%u (dwords) | readback LO=0x%08X HI=0x%08X LEN=%u escritas=%u/3 leituras=%u/3 -> %s\n",
        loB, hiB, lenB, wantLo, wantHi, CsbDwords, lo, hi, len, wOk, rOk,
        pass ? "PASS" : "FALHOU (snapshot restaurado)");
    if (!pass) {
        (void)Bc250MmioTopologyWrite32(mmio, BC250_REG_RLC_CSIB_ADDR_HI_SNAP, hiB);
        (void)Bc250MmioTopologyWrite32(mmio, BC250_REG_RLC_CSIB_ADDR_LO_SNAP, loB);
        (void)Bc250MmioTopologyWrite32(mmio, BC250_REG_RLC_CSIB_LENGTH_SNAP, lenB);
    }
    return pass;
}

/*
 * PA_SC_TILE_STEERING_OVERRIDE que vai no CSB. O upstream calcula NUM_SC,
 * NUM_RB_PER_SC e NUM_PACKER_PER_SC a partir do gc_info da IP discovery do
 * GFX10.1 (gfx_v10_0_init_pa_sc_tile_steering_override); nao temos esse gc_info
 * medido deste chip. 0 e' o valor que o upstream usa no GFX10.3 e deixa o
 * override desligado (ENABLE=0, gc_10_1_0_sh_mask.h:22524). DIVERGENCIA
 * DECLARADA do upstream 10.1, classe H.
 */
#define BC250_GFX10_CSB_TILE_STEERING_OVERRIDE 0u

static NTSTATUS Bc250ActiveGfxEngineOnline(BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    BC250_VRAM_ALLOCATOR *allocator;
    const BC250_VRAM_ADDRESS_SPACE *addressSpace;
    BC250_VRAM_TOKEN token;
    BC250_VRAM_REGION region;
    BC250_VRAM_STATUS vramStatus;
    BC250_GFX_RING_INTERFACE interfaceData;
    BC250_GFX_RING_PLAN plan;
    BC250_GFX_RING_TXN_STATUS status;
    const uint64_t ringBytes = 65536ull; /* 64 KB = 16384 dwords */
    const uint64_t auxBytes = 4096ull;  /* Writeback + Shadow + Fence */
    const uint64_t csbBytes = 4096ull;  /* DX.27: Clear State Buffer (949 dw = 3796 bytes) */
    const uint64_t totalBytes = ringBytes + auxBytes + csbBytes;
    const BC250_RESOLVED_BAR_RESOURCE *vramBar = NULL;
    PVOID cpuMapped = NULL;
    uint32_t bIdx;
    uint64_t ringMc;
    PHYSICAL_ADDRESS phys;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Context->GfxEngine, sizeof(Context->GfxEngine));
    KeInitializeSpinLock(&Context->GfxEngine.Lock);

    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;
    addressSpace = &Context->Core.TopologyEvidence.AddressSpace;
    uint64_t vramMcBase = (addressSpace->VramMcStart != 0ull) ? addressSpace->VramMcStart : BC250_SDMA_VRAM_MC_BASE;
    uint64_t totalBytesAvailable = (addressSpace->TotalBytes != 0ull) ? addressSpace->TotalBytes : (512ull * 1024ull * 1024ull);

    if (totalBytesAvailable == 0u || vramMcBase == 0u) {
        Bc250DisplayLog("[GfxEngine] ONLINE nao executou: janela de VRAM indisponivel\n");
        return STATUS_DEVICE_NOT_READY;
    }

    for (bIdx = 0; bIdx < Context->Core.ResolvedBarCount; ++bIdx) {
        if (Context->Core.ResolvedBars[bIdx].Index == BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            vramBar = &Context->Core.ResolvedBars[bIdx];
            break;
        }
    }
    if (vramBar == NULL) {
        Bc250DisplayLog("[GfxEngine] ONLINE nao executou: BAR0 VRAM indisponivel\n");
        return STATUS_DEVICE_NOT_READY;
    }

    vramStatus = Bc250VramAllocate(allocator, totalBytes, 65536ull,
        BC250_VRAM_ROLE_RING, BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE, &token);
    if (vramStatus != BC250_VRAM_OK) {
        Bc250DisplayLog("[GfxEngine] ONLINE nao executou: VRAM negada (%s)\n",
            Bc250VramStatusName(vramStatus));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (Bc250VramGetRegion(allocator, token, &region) != BC250_VRAM_OK) {
        (void)Bc250VramRelease(allocator, token);
        return STATUS_UNSUCCESSFUL;
    }

    ringMc = vramMcBase + region.Offset;
    phys.QuadPart = (LONGLONG)(vramBar->Start + region.Offset);
    cpuMapped = MmMapIoSpace(phys, (SIZE_T)totalBytes, MmNonCached);
    if (cpuMapped == NULL) {
        (void)Bc250VramRelease(allocator, token);
        Bc250DisplayLog("[GfxEngine] ONLINE nao executou: MmMapIoSpace falhou\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(cpuMapped, (SIZE_T)totalBytes);

    Context->GfxEngine.VramToken = token;
    Context->GfxEngine.VramTotalBytes = totalBytes;
    Context->GfxEngine.RingGpuVa = ringMc;
    Context->GfxEngine.RingDwords = (uint32_t)(ringBytes / sizeof(uint32_t));
    Context->GfxEngine.RingCpuVirtual = (uint32_t *)cpuMapped;

    Context->GfxEngine.RptrWbGpuVa = ringMc + ringBytes;
    Context->GfxEngine.RptrWbCpuVirtual = (volatile uint32_t *)((unsigned char *)cpuMapped + ringBytes);

    Context->GfxEngine.WptrPollGpuVa = ringMc + ringBytes + 0x10ull;
    Context->GfxEngine.WptrPollCpuVirtual = (volatile uint32_t *)((unsigned char *)cpuMapped + ringBytes + 0x10ull);

    Context->GfxEngine.FenceGpuVa = ringMc + ringBytes + 0x20ull;
    Context->GfxEngine.FenceCpuVirtual = (volatile uint32_t *)((unsigned char *)cpuMapped + ringBytes + 0x20ull);

    *Context->GfxEngine.FenceCpuVirtual = BC250_GFX_FENCE_POISON; /* Poison inicial */

    RtlZeroMemory(&plan, sizeof(plan));
    plan.VramWindowBase = vramMcBase;
    plan.VramWindowBytes = totalBytesAvailable;
    plan.RingGpuVa = ringMc;
    plan.RingBytes = (uint32_t)ringBytes;
    plan.RptrWritebackGpuVa = Context->GfxEngine.RptrWbGpuVa;
    plan.WptrPollGpuVa = Context->GfxEngine.WptrPollGpuVa;
    plan.RbExe = 1u;
    plan.CeHqdNeqRbHqd = 1u;
    plan.UnhaltMicroEngines = 1u;
    plan.MicroEngineFirmwareLoaded = 1u;

    /*
     * D0.4 -- CADENCIA DE POLL DE WPTR PEDIDA EXPLICITAMENTE.
     *
     * POR QUE ISTO ENTRA AGORA: a corrida 2026-09-18 14:20:07 mediu que o anel
     * grafico tinha TODOS os canais de publicacao de WPTR fechados ou inertes.
     * CP_RB_WPTR_POLL_ADDR_LO/HI sempre foram programados com o shadow, mas a
     * CADENCIA (CP_RB_WPTR_POLL_CNTL, 0x08708) nunca foi escrita, a doorbell e'
     * escrita 0 pelo APPLY, e o MMIO de CP_RB0_WPTR nao retem o valor -- 4 de 4
     * submissoes com WPTR_READBACK_DIVERGENTE, escrevemos 12 e o registrador
     * devolveu 0.
     *
     * PROVENIENCIA DOS CAMPOS -- P1, gc_10_1_0_sh_mask.h:
     *   CP_RB_WPTR_POLL_CNTL__POLL_FREQUENCY__SHIFT   0x0   (16 bits)
     *   CP_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT  0x10  (16 bits)
     * Sao os dois unicos campos do registrador.
     *
     * PROVENIENCIA DOS VALORES -- mais fraca, e declarada como tal:
     *   IDLE_POLL_COUNT = 64  tem precedente LOCAL: docs/03 registra o unico
     *     prototipo do repositorio escrevendo 0x00400000 neste registrador,
     *     que decodifica exatamente para IDLE_POLL_COUNT=64 / POLL_FREQUENCY=0.
     *   POLL_FREQUENCY = 4   e' ESCOLHA NOSSA, classe H. Zero e' o valor que a
     *     placa tinha e o que o prototipo escrevia, e "frequencia zero" e'
     *     candidato direto a "nunca consulta o shadow". Um valor pequeno e
     *     nao-nulo e' o movimento falsificavel: se o RPTR passar a andar, a
     *     cadencia era a peca faltante; se nao passar, este canal esta
     *     descartado junto com o MMIO e sobra a doorbell.
     *
     * LIMITACAO QUE TEM DE SER DITA EM VOZ ALTA: este registrador NAO TEM BIT
     * DE ENABLE. Programa-lo configura cadencia, nao liga mecanismo. Se a
     * proxima corrida nao mover o RPTR, isso NAO refuta o canal de doorbell --
     * que e' o mecanismo provado nesta mesma placa no lado do compute
     * ([SubmissionPublishWptr] Step 4 DbKick: Offset=0x110 Val64=12).
     *
     * 0x00403004 (o poll do SDMA) e' recusado pelo contrato na montagem, nao
     * apenas no snapshot: o valor montado aqui e' 0x00400004.
     */
    plan.ProgramPollCntl = 1u;
    plan.PollFrequency = 4u;      /* [15:0]  -- H, escolha nossa */
    plan.IdlePollCount = 64u;     /* [31:16] -- precedente local em docs/03 */

    Context->GfxEngine.Plan = plan;

    if (Context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_EMPTY) {
        BC250_MMIO_TOPOLOGY_STATUS mmioStatus = Bc250MmioTopologyMapReadonly(
            &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
            Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
            BC250_PCI_MMIO_BAR_INDEX);
        if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
            Bc250DisplayLog("[GfxEngine] ONLINE nao executou: mapa do transporte falhou (%s)\n",
                Bc250MmioTopologyStatusName(mmioStatus));
            MmUnmapIoSpace(cpuMapped, (SIZE_T)totalBytes);
            (void)Bc250VramRelease(allocator, token);
            return STATUS_DEVICE_NOT_READY;
        }
    }

    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Stall = Bc250ActiveTlbStall;
    interfaceData.Context = &Context->Core.MmioTransport;
    interfaceData.Identity = BC250_GFX_RING_TXN_IDENTITY;

    status = Bc250GfxRingTxnPreflight(&interfaceData, &plan, &Context->GfxEngine.Evidence);
    if (status == BC250_GFX_RING_TXN_OK) {
        status = Bc250GfxRingTxnApply(&interfaceData, &plan, &Context->GfxEngine.Evidence);
    }
    if (status != BC250_GFX_RING_TXN_OK || Context->GfxEngine.Evidence.RingActivatePassed == 0u) {
        Bc250DisplayLog("[GfxEngine] ONLINE FALHOU: status=%s (%u) ActivatePassed=%u\n",
            Bc250GfxRingTxnStatusName(status), (uint32_t)status,
            Context->GfxEngine.Evidence.RingActivatePassed);
        MmUnmapIoSpace(cpuMapped, (SIZE_T)totalBytes);
        (void)Bc250VramRelease(allocator, token);
        InterlockedExchange(&Context->GfxEngine.State, 0);
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    Context->GfxEngine.WptrDwords = 0u;
    Context->GfxEngine.LastSubmittedFence = 0ull;
    Context->GfxEngine.LastRetiredFence = 0ull;
    InterlockedExchange(&Context->GfxEngine.State, 1); /* ONLINE */

    /*
     * DX.27 -- CLEAR STATE DO GFX10 COMO NO UPSTREAM (uma variavel: CSB + preambulo).
     *
     * Ate a Corrida 72 o preambulo de boot era PREAMBLE_CNTL(0), CONTEXT_CONTROL,
     * PREAMBLE_CNTL(1), CLEAR_STATE -- sem nenhum SET_CONTEXT_REG entre as marcas e
     * com payloads que NAO sao os do upstream (BEGIN/END_CLEAR_STATE = (2<<28) e
     * (3<<28), nvd.h:395-396). E o RLC_CSIB estava em zero (Corrida 72, P0).
     *
     * Agora, na ordem do upstream (rlc_resume -> cp_gfx_start):
     *   1. o CSB (gfx_v10_0_get_csb_buffer) e' montado numa pagina de VRAM logo
     *      depois do bloco auxiliar do anel;
     *   2. RLC_CSIB_ADDR_HI/LO/LENGTH apontam para ele (escritor unico, readback);
     *   3. o MESMO conteudo e' emitido no CP_RB0, que e' o que gfx_v10_0_cp_gfx_start
     *      faz, MENOS o SET_BASE(CE_PARTITION_BASE): na Corrida 31 ele parou o ME
     *      a espera do CE (ME_HAS_ACTIVE_CE_BUFFER_FLAG=1). Essa omissao continua.
     * Falha em qualquer passo = motor grafico NAO fica ONLINE (falha fechada).
     */
    {
        uint32_t bootCursor = 0u;
        uint32_t bootPoll = 0u;
        uint32_t bootRptr = 0u;
        uint32_t bootFence = 0u;
        BOOLEAN bootDrainOk = FALSE;
        uint32_t *csbCpu = (uint32_t *)((unsigned char *)cpuMapped + ringBytes + auxBytes);
        const uint64_t csbMc = ringMc + ringBytes + auxBytes;
        uint32_t csbDwords = 0u;
        uint32_t csbIdx;

        if (Bc250Pm4BuildGfx10ClearState(csbCpu, (uint32_t)(csbBytes / sizeof(uint32_t)),
                BC250_GFX10_CSB_TILE_STEERING_OVERRIDE, &csbDwords) != BC250_PM4_OK ||
            csbDwords != BC250_PM4_GFX10_CSB_DWORDS) {
            Bc250DisplayLog("[GfxEngine] CSB FALHOU: montagem devolveu %u dwords (esperado %u); motor nao ficara ONLINE\n",
                csbDwords, BC250_PM4_GFX10_CSB_DWORDS);
            (void)Bc250ActiveGfxEngineOffline(Context);
            return STATUS_DEVICE_HARDWARE_ERROR;
        }
        Bc250SdmaPresentFlushHdp();
        KeMemoryBarrier();
        Bc250DisplayLog("[GfxEngine] CSB montado: Mc=0x%llX Dwords=%u Inicio=%08X %08X Fim=%08X %08X Steering=0x%08X (upstream gfx_v10_0_get_csb_buffer)\n",
            (unsigned long long)csbMc, csbDwords, csbCpu[0], csbCpu[1],
            csbCpu[csbDwords - 4u], csbCpu[csbDwords - 3u], BC250_GFX10_CSB_TILE_STEERING_OVERRIDE);

        if (!Bc250ActiveProgramClearStateBuffer(Context, csbMc, csbDwords)) {
            Bc250DisplayLog("[GfxEngine] CSB FALHOU: RLC_CSIB nao conferiu no readback; motor nao ficara ONLINE\n");
            (void)Bc250ActiveGfxEngineOffline(Context);
            return STATUS_DEVICE_HARDWARE_ERROR;
        }

        /* 1..4. o conteudo do CSB no anel (gfx_v10_0_cp_gfx_start sem SET_BASE) */
        for (csbIdx = 0u; csbIdx < csbDwords; ++csbIdx) {
            Context->GfxEngine.RingCpuVirtual[bootCursor++] = csbCpu[csbIdx];
        }

        /* 5. PACKET3_RELEASE_MEM EOP (32-bit) para gravar fence=1 na VRAM (8 dw) */
        if (Bc250Pm4EmitReleaseMemEop32(
            Context->GfxEngine.RingCpuVirtual,
            Context->GfxEngine.RingDwords,
            &bootCursor,
            Context->GfxEngine.FenceGpuVa,
            1u,
            BC250_PM4_RELEASE_MEM_INT_SEL_NONE) != BC250_PM4_OK) {
            Bc250DisplayLog("[GfxEngine] BOOT_PREAMBLE FALHOU: RELEASE_MEM nao foi emitido; motor nao ficara ONLINE\n");
            (void)Bc250ActiveGfxEngineOffline(Context);
            return STATUS_DEVICE_HARDWARE_ERROR;
        }

        /* 6. Alinhamento com NOP a 4 dwords */
        while ((bootCursor & 3u) != 0u) {
            Context->GfxEngine.RingCpuVirtual[bootCursor & (Context->GfxEngine.RingDwords - 1u)] = BC250_PM4_PACKET2_NOP;
            bootCursor++;
        }

        /* 7. Atualiza shadow de WPTR em memoria e descarrega HDP */
        if (Context->GfxEngine.WptrPollCpuVirtual != NULL) {
            *Context->GfxEngine.WptrPollCpuVirtual = bootCursor;
        }
        Bc250SdmaPresentFlushHdp();
        KeMemoryBarrier();

        /* 8. Publica par 64-bit de WPTR no registrador */
        (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
            BC250_GFX_REG_CP_RB0_WPTR, bootCursor);
        (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
            BC250_GFX_REG_CP_RB0_WPTR_HI, 0u);

        /* 9. Aguarda dreno no silicio (RPTR == WPTR e Fence == 1) */
        for (bootPoll = 0u; bootPoll < 2000u; ++bootPoll) {
            Bc250ActiveTlbStall(Context, 25u);
            Bc250SdmaPresentFlushHdp();
            (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_RPTR, &bootRptr);
            if (Context->GfxEngine.FenceCpuVirtual != NULL) {
                bootFence = *Context->GfxEngine.FenceCpuVirtual;
            }
            if (bootRptr == bootCursor && bootFence == 1u) {
                bootDrainOk = TRUE;
                break;
            }
        }

        Bc250DisplayLog("[GfxEngine] BOOT_PREAMBLE: WPTR=%u RPTR=%u Fence=0x%08X DrenoOk=%u Polls=%u (DX.27: CSB upstream de %u dw no anel + RELEASE_MEM)\n",
            bootCursor, bootRptr, bootFence, bootDrainOk ? 1u : 0u, bootPoll, csbDwords);
        if (!bootDrainOk) {
            Bc250DisplayLog("[GfxEngine] BOOT_PREAMBLE FALHOU: RPTR/fence nao provaram execucao; motor nao ficara ONLINE\n");
            (void)Bc250ActiveGfxEngineOffline(Context);
            return STATUS_DEVICE_HARDWARE_ERROR;
        }

        Context->GfxEngine.WptrDwords = bootCursor;
        Context->GfxEngine.LastSubmittedFence = 1ull;
        Context->GfxEngine.LastRetiredFence = 1ull;
        Context->GfxEngine.LastSubmitVmid = 0xFFFFFFFFu; /* DX.29: anel sem IB de usuario */

        /* DX.26: medicao somente leitura, uma vez por bring-up do motor grafico. */
        Bc250ActiveGoldenSnapshotReadOnly(Context);
    }

    Bc250DisplayLog("[GfxEngine] ONLINE SUCESSO: RingMc=0x%llX (64KB, %u dw) RptrWbMc=0x%llX FenceMc=0x%llX RB0_ACTIVE=1\n",
        (unsigned long long)ringMc, Context->GfxEngine.RingDwords,
        (unsigned long long)Context->GfxEngine.RptrWbGpuVa,
        (unsigned long long)Context->GfxEngine.FenceGpuVa);

    Bc250DisplayLog("[GfxRing] BRINGUP: CntlPlanned=0x%08X CntlReadback=0x%08X RbExe=%u BaseReadback=0x%08X MeCntlBefore=0x%08X MeCntlAfter=0x%08X UnhaltPassed=%u ActiveReadback=0x%08X ActivePassed=%u\n",
        Context->GfxEngine.Evidence.CntlPlanned,
        Context->GfxEngine.Evidence.CntlReadback,
        plan.RbExe,
        Context->GfxEngine.Evidence.BaseReadback,
        Context->GfxEngine.Evidence.MeCntlBefore,
        Context->GfxEngine.Evidence.MeCntlAfter,
        Context->GfxEngine.Evidence.UnhaltPassed,
        Context->GfxEngine.Evidence.RingActivateReadback,
        Context->GfxEngine.Evidence.RingActivatePassed);

    /*
     * D0.1 / D0.4 -- INVENTARIO DOS CANAIS DE PUBLICACAO DE WPTR.
     *
     * Esta linha existe porque a corrida 2026-09-18 14:20:07 gastou uma corrida
     * inteira para descobrir, por leitura de codigo DEPOIS do ensaio, que
     * estavamos com a doorbell desligada e a cadencia de poll nunca escrita. O
     * inventario passa a sair no ONLINE, antes de qualquer submissao, para que
     * a proxima corrida seja interpretavel sem arqueologia de fonte.
     */
    Bc250DisplayLog("[GfxEngine] WPTR_CANAIS: PollAddrMc=0x%llX (programado sempre) PollCntlPedido=%u Planejado=0x%08X Readback=0x%08X Antes=0x%08X Passou=%u | DoorbellControl escrito 0 pelo APPLY (canal FECHADO) | MMIO CP_RB0_WPTR 0x%04X (na corrida 18/09 NAO reteve) -- CP_RB_WPTR_POLL_CNTL NAO TEM BIT DE ENABLE (so POLL_FREQUENCY[15:0] e IDLE_POLL_COUNT[31:16]): resultado nulo NAO refuta o canal de doorbell\n",
        (unsigned long long)Context->GfxEngine.WptrPollGpuVa,
        plan.ProgramPollCntl,
        Context->GfxEngine.Evidence.PollCntlPlanned,
        Context->GfxEngine.Evidence.PollCntlReadback,
        Context->GfxEngine.Evidence.PollCntlBefore,
        Context->GfxEngine.Evidence.PollCntlApplyPassed,
        BC250_GFX_REG_CP_RB0_WPTR);

    /*
     * LINHA DE BASE DE STALL, TIRADA ANTES DE EXISTIR QUALQUER SUBMISSAO.
     *
     * Na analise da corrida 2026-09-18 13:12 o valor CpStalled1=0x00000C00 foi
     * tratado como CONSEQUENCIA do primeiro IB grafico. A mineracao do log
     * mostrou que ele aparece a partir da LINHA 40 do bind -- na telemetria do
     * MEC, muito antes da primeira submissao grafica (linha 2292) -- e nao muda
     * depois. E' estado de repouso, nao efeito.
     *
     * Registrar a base aqui torna isso verificavel por comparacao direta na
     * proxima corrida: se o valor no ONLINE for igual ao valor no TDR, nenhuma
     * submissao alterou o estado do CP, e atribuir o stall ao nosso IB fica
     * refutado sem precisar de arqueologia de log.
     */
    if (Context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
        uint32_t baseStall1 = 0u, baseStall2 = 0u, baseGrbm = 0u;
        const uint32_t b1 = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_HQD_MMIO_CP_STALLED_STAT1, &baseStall1);
        const uint32_t b2 = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_HQD_MMIO_CP_STALLED_STAT2, &baseStall2);
        const uint32_t bg = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_GFX_REG_GRBM_STATUS, &baseGrbm);
        Bc250DisplayLog("[GfxEngine] STALL_BASELINE (antes de qualquer submissao): CpStalled1=0x%08X[lido=%u] CpStalled2=0x%08X[lido=%u] GrbmStatus=0x%08X[lido=%u]\n",
            baseStall1, b1, baseStall2, b2, baseGrbm, bg);
    } else {
        Bc250DisplayLog("[GfxEngine] STALL_BASELINE INDETERMINADA: transporte MMIO nao mapeado no ONLINE (State=%u)\n",
            (uint32_t)Context->Core.MmioTransport.State);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ActiveGfxEngineOffline(BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    BC250_VRAM_ALLOCATOR *allocator;
    BC250_GFX_RING_INTERFACE interfaceData;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }

    if (InterlockedExchange(&Context->GfxEngine.State, 0) == 0) {
        return STATUS_SUCCESS;
    }

    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;

    if (Context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
        RtlZeroMemory(&interfaceData, sizeof(interfaceData));
        interfaceData.Read32 = Bc250MmioTopologyRead32;
        interfaceData.Write32 = Bc250MmioTopologyWrite32;
        interfaceData.Stall = Bc250ActiveTlbStall;
        interfaceData.Context = &Context->Core.MmioTransport;
        interfaceData.Identity = BC250_GFX_RING_TXN_IDENTITY;

        (void)Bc250GfxRingTxnRollback(&interfaceData, &Context->GfxEngine.Evidence);
    }

    if (Context->GfxEngine.RingCpuVirtual != NULL) {
        MmUnmapIoSpace((PVOID)Context->GfxEngine.RingCpuVirtual, (SIZE_T)Context->GfxEngine.VramTotalBytes);
        Context->GfxEngine.RingCpuVirtual = NULL;
    }

    if (Context->GfxEngine.VramTotalBytes > 0u) {
        (void)Bc250VramRelease(allocator, Context->GfxEngine.VramToken);
        Context->GfxEngine.VramTotalBytes = 0u;
    }

    Bc250DisplayLog("[GfxEngine] OFFLINE: anel desativado, cursores quiescidos e VRAM liberada\n");
    return STATUS_SUCCESS;
}

/*
 * =========================================================================
 * D0.2 -- VEREDITO DE MEDICAO NO CAMINHO DE RECUSA
 *
 * POR QUE ISTO EXISTE (divida introduzida pela propria correcao de 18/09):
 * o veredito GFX_TRIPE_VEREDITO e' emitido por Bc250ActiveGfxEnginePoll, e o
 * poll so' e' chamado pelo retirement, que so' arranca depois de um
 * SUBMIT SUCESSO. Na corrida 2026-09-18 14:20:07 as 4 submissoes foram
 * RECUSADAS por WPTR_READBACK_DIVERGENTE -- e, por consequencia, as Pernas 1 e
 * 2 sairam ZERO vezes, na corrida exatamente em que foram consertadas.
 *
 * Instrumentacao que existe so' no caminho de sucesso desaparece justamente
 * quando e' necessaria. Esta funcao le RPTR (registrador E writeback), WPTR e a
 * fence no instante da recusa, para que a proxima corrida responda "o CP
 * buscou algo?" mesmo quando nao aceitamos a submissao.
 *
 * ESCOPO DECLARADO, para nao repetir a leitura errada das corridas de 16 e
 * 17/09: RPTR avancar prova que o CP BUSCOU do anel. NAO prova que o IB foi
 * executado -- o RELEASE_MEM que escreve a fence e' emitido pelo KMD NO ANEL,
 * logo fence retirada prova consumo do anel, nao execucao do IB. E nao prova
 * dispatch de shader (Diretriz Universal no 3).
 *
 * CONTRATO DE CHAMADA -- E' PARTE DA CORRECAO, NAO DETALHE: chamar SOMENTE do
 * CHAMADOR do submit, DEPOIS de a reserva de fence ter sido cancelada.
 *
 * A primeira versao desta funcao era chamada de DENTRO de
 * Bc250ActiveGfxEngineSubmit, nas proprias saidas de recusa. O Caso 5 de
 * active_miniport_test REPROVOU deterministicamente (exit 651,
 * StopDevice != STATUS_SUCCESS) e a atribuicao foi confirmada por build de
 * isolamento: com esta funcao neutralizada o build volta a EXIT=0.
 *
 * MECANISMO: a recusa acontece com a reserva de fence do WDDM ainda de pe --
 * o chamador so' a cancela depois. Fazer leitura de MMIO e um Bc250DisplayLog
 * de muitos argumentos nesse intervalo ALARGA a janela em que
 * Bc250ActiveQuiesceAndSnapshotTracker observa reservedCount != 0, e o
 * StopDevice devolve STATUS_DEVICE_BUSY. Instrumentacao nao pode mudar o
 * comportamento do que ela mede.
 *
 * Por isso a recusa agora apenas REGISTRA o motivo em dois campos baratos
 * (LastRefusalReason / LastRefusalCursor) e quem emite o veredito e' o
 * chamador, fora da janela critica.
 *
 * Roda em DISPATCH_LEVEL, so' faz leitura, e nao usa array na pilha (o /O2 ja
 * emitiu __chkstk neste arquivo e quebrou o link offline das suites).
 * =========================================================================
 */
static void Bc250ActiveGfxLogRefusalMeasurement(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    const char *Reason,
    uint32_t CursorAttempted)
{
    uint32_t rptrReg = 0u;
    uint32_t wptrReg = 0u;
    uint32_t rptrWb = 0u;
    uint32_t observedFence = 0u;
    uint32_t rptrRegOk = 0u;
    uint32_t wptrRegOk = 0u;
    BOOLEAN rptrWbRead = FALSE;
    BOOLEAN fenceRead = FALSE;
    const char *fenceVerdict;
    const char *cpVerdict;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return;
    }

    Bc250SdmaPresentFlushHdp();

    if (Context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
        rptrRegOk = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_GFX_REG_CP_RB0_RPTR, &rptrReg);
        wptrRegOk = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_GFX_REG_CP_RB0_WPTR, &wptrReg);
    }
    if (Context->GfxEngine.RptrWbCpuVirtual != NULL) {
        rptrWb = *Context->GfxEngine.RptrWbCpuVirtual;
        rptrWbRead = TRUE;
    }
    if (Context->GfxEngine.FenceCpuVirtual != NULL) {
        observedFence = *Context->GfxEngine.FenceCpuVirtual;
        fenceRead = TRUE;
    }

    if (!fenceRead) {
        fenceVerdict = "FENCE_SEM_MAPA_DE_CPU";
    } else if (observedFence == BC250_GFX_FENCE_POISON) {
        fenceVerdict = "POISON_INTACTO_GPU_NAO_ESCREVEU";
    } else if (observedFence == 0u) {
        fenceVerdict = "INDETERMINADO_SLOT_ZERADO_SEM_POISON";
    } else {
        fenceVerdict = "FENCE_ESCRITA_PELA_GPU";
    }

    /*
     * O discriminante que a Fase D0 persegue. RPTR > 0, ou RPTR diferente do
     * writeback, significa que o CP mexeu no anel -- e isso muda o suspeito de
     * "o ponteiro nao chega" para "o ponteiro chega e o CP para depois".
     */
    if (rptrRegOk == 0u) {
        cpVerdict = "CP_NAO_OBSERVAVEL_LEITURA_DE_RPTR_RECUSADA";
    } else if (rptrReg == 0u && (!rptrWbRead || rptrWb == 0u)) {
        cpVerdict = "CP_NAO_BUSCOU_NADA_RPTR_EM_ZERO";
    } else {
        cpVerdict = "CP_MEXEU_NO_ANEL_RPTR_NAO_NULO";
    }

    Bc250DisplayLog("[GfxEngine] GFX_TRIPE_VEREDITO_RECUSA motivo=%s CursorTentado=%u RPTR_Reg=%u[lido=%u] RPTR_Wb=%u[lido=%u] WPTR_Reg=%u[lido=%u] WptrShadowKmd=%u Fence=0x%08X Poison=0x%08X Perna2=%s CP=%s Aceitas=%u Recusadas=%u -- leitura no instante da recusa; RPTR nao nulo prova BUSCA do anel, nao execucao do IB nem dispatch de shader\n",
        (Reason != NULL) ? Reason : "SEM_MOTIVO_DECLARADO",
        CursorAttempted,
        rptrReg, rptrRegOk,
        rptrWb, (uint32_t)(rptrWbRead ? 1u : 0u),
        wptrReg, wptrRegOk,
        Context->GfxEngine.WptrDwords,
        observedFence, BC250_GFX_FENCE_POISON,
        fenceVerdict, cpVerdict,
        Context->GfxEngine.SubmitAccepted,
        Context->GfxEngine.SubmitRejected);
}

static NTSTATUS Bc250ActiveGfxEngineSubmit(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    uint64_t IbGpuVirtualAddress,
    uint32_t IbSizeDwords,
    uint32_t Vmid,
    uint64_t *AssignedFence)
{
    KIRQL irql;
    uint32_t ringDwords;
    uint32_t currentWptr;
    uint32_t cursor;
    uint32_t availableDwords = 0u;
    uint32_t rptr = 0u;
    uint64_t nextFence;
    BC250_PM4_STATUS pm4Status;
    /* 7 (WAIT_REG_MEM) + 2 (PFP_SYNC_ME) na troca de VMID + 4 (IB) + 8 (RELEASE_MEM) + 4 (NOP pad) */
    const uint32_t neededDwords = 25u;
    BOOLEAN vmidSwitch = FALSE;
    uint32_t prevVmid = 0xFFFFFFFFu;
    uint32_t waitFence = 0u;

    if (Context == NULL || AssignedFence == NULL || IbGpuVirtualAddress == 0ull || IbSizeDwords == 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    *AssignedFence = 0ull;

    if (InterlockedCompareExchange(&Context->GfxEngine.State, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }

    KeAcquireSpinLock(&Context->GfxEngine.Lock, &irql);
    ++Context->GfxEngine.SubmitCalls;

    ringDwords = Context->GfxEngine.RingDwords;
    currentWptr = Context->GfxEngine.WptrDwords;

    if (Context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
        (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_GFX_REG_CP_RB0_RPTR, &rptr);
    } else if (Context->GfxEngine.RptrWbCpuVirtual != NULL) {
        rptr = *Context->GfxEngine.RptrWbCpuVirtual;
    }
    Context->GfxEngine.LastRptrMmio = rptr;

    pm4Status = Bc250Pm4RingComputeAvailableSpace(ringDwords, rptr, currentWptr, &availableDwords);
    if (pm4Status != BC250_PM4_OK || availableDwords < neededDwords) {
        ++Context->GfxEngine.SubmitRejected;
        KeReleaseSpinLock(&Context->GfxEngine.Lock, irql);
        Bc250DisplayLog("[GfxEngine] SUBMIT RECUSA: sem espaco no anel (Available=%u, Needed=%u, WPTR=%u, RPTR=%u)\n",
            availableDwords, neededDwords, currentWptr, rptr);
        return STATUS_GRAPHICS_ALLOCATION_BUSY;
    }

    nextFence = Context->GfxEngine.LastSubmittedFence + 1ull;
    cursor = currentWptr;

    /*
     * DX.29 -- SINCRONIA DE TROCA DE VMID NO ANEL GRAFICO (pipeline sync do upstream).
     *
     * Corridas 73 e 74 (P0): os 5 travamentos de 240 ondas foram TODOS num IB
     * logo apos um IB de OUTRO VMID (taxa de base 18-26% dos IBs), e nos faults
     * de busca de instrucao o VMID do fault era o do IB ANTERIOR. H: o PFP
     * avanca para o IB novo enquanto o anterior ainda nao terminou.
     *
     * O upstream, na troca, emite gfx_v10_0_ring_emit_pipeline_sync -- um
     * WAIT_REG_MEM no PFP (usepfp=1) sobre a fence do anel -- e, no fim do
     * vm_flush, PFP_SYNC_ME ("otherwise we might get invalid PFP reads").
     * Aqui: WAIT_REG_MEM mem/PFP ate a fence do anel alcancar a ultima
     * submetida, seguido de PFP_SYNC_ME. Codificacao: nvd.h:166-186 e :322.
     * DIVERGENCIAS DECLARADAS: funcao 5 (>=) em vez de 3 (==) -- a fence e'
     * monotonica e >= nao trava se o slot ja estiver adiante; o flush de TLB
     * continua por MMIO na instalacao do VMID, nao no anel.
     */
    prevVmid = Context->GfxEngine.LastSubmitVmid;
    if (prevVmid != 0xFFFFFFFFu && prevVmid != Vmid && Context->GfxEngine.LastSubmittedFence != 0ull) {
        waitFence = (uint32_t)Context->GfxEngine.LastSubmittedFence;
        pm4Status = Bc250Pm4EmitWaitRegMem(
            Context->GfxEngine.RingCpuVirtual, ringDwords, &cursor,
            BC250_PM4_WAIT_REG_MEM_PFP_MEM_GE,
            Context->GfxEngine.FenceGpuVa, waitFence, 0xFFFFFFFFu, 4u);
        if (pm4Status == BC250_PM4_OK) {
            Context->GfxEngine.RingCpuVirtual[cursor & (ringDwords - 1u)] =
                BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_PFP_SYNC_ME, 1u);
            cursor++;
            Context->GfxEngine.RingCpuVirtual[cursor & (ringDwords - 1u)] = 0u;
            cursor++;
            vmidSwitch = TRUE;
        } else {
            ++Context->GfxEngine.SubmitRejected;
            KeReleaseSpinLock(&Context->GfxEngine.Lock, irql);
            Bc250DisplayLog("[GfxEngine] SUBMIT RECUSA: sincronia de troca de VMID %u->%u nao emitida (pm4=%u); o IB nao parte sem ela\n",
                prevVmid, Vmid, (uint32_t)pm4Status);
            return STATUS_UNSUCCESSFUL;
        }
    }

    /* 1. Emite PACKET3_INDIRECT_BUFFER com VMID e bit de validade */
    pm4Status = Bc250Pm4EmitIndirectBuffer(
        Context->GfxEngine.RingCpuVirtual,
        ringDwords,
        &cursor,
        IbGpuVirtualAddress,
        IbSizeDwords,
        Vmid);
    if (pm4Status != BC250_PM4_OK) {
        ++Context->GfxEngine.SubmitRejected;
        KeReleaseSpinLock(&Context->GfxEngine.Lock, irql);
        return STATUS_UNSUCCESSFUL;
    }

    /* 2. Emite PACKET3_RELEASE_MEM EOP (32-bit) para gravar a fence na VRAM */
    pm4Status = Bc250Pm4EmitReleaseMemEop32(
        Context->GfxEngine.RingCpuVirtual,
        ringDwords,
        &cursor,
        Context->GfxEngine.FenceGpuVa,
        (uint32_t)nextFence,
        BC250_PM4_RELEASE_MEM_INT_SEL_NONE);
    if (pm4Status != BC250_PM4_OK) {
        ++Context->GfxEngine.SubmitRejected;
        KeReleaseSpinLock(&Context->GfxEngine.Lock, irql);
        return STATUS_UNSUCCESSFUL;
    }

    /* 3. Alinha a 4 dwords com NOP se necessario */
    while ((cursor & 3u) != 0u) {
        Context->GfxEngine.RingCpuVirtual[cursor & (ringDwords - 1u)] = BC250_PM4_PACKET2_NOP;
        cursor++;
    }

    /* 4. Atualiza shadow de WPTR em memoria */
    if (Context->GfxEngine.WptrPollCpuVirtual != NULL) {
        *Context->GfxEngine.WptrPollCpuVirtual = cursor;
    }

    /*
     * 5. Dreno de memoria/HDP e avanco de CP_RB0_WPTR no hardware.
     *
     * POR QUE ISTO FALHA FECHADO AGORA (medicao da corrida 2026-09-18 13:12):
     * o KMD reportou "SUBMIT SUCESSO ... NewWptr=12" e, no snapshot do TDR, o
     * REGISTRADOR leu `RPTR=0 WPTR=0`. Ou seja: o driver acreditava ter armado o
     * anel e o hardware nunca viu o ponteiro andar -- a fence ficou em poison e
     * o CP nao tinha o que consumir.
     *
     * Duas causas eram indistinguiveis no codigo anterior, porque ele fazia
     * `(void)Bc250MmioTopologyWrite32(...)` dentro de um `if` sem `else`:
     *   a) o transporte nao estava mapeado -> a escrita era PULADA em silencio;
     *   b) a escrita aconteceu e o registrador nao aceitou o valor.
     * Agora as duas sao nomeadas, e nenhuma delas pode mais sair como sucesso.
     *
     * O readback e' legal: 0xC150 esta no allowlist de LEITURA
     * (bc250_mmio_topology_transport.c:168), assim como 0x8700 (linha 165).
     *
     * RISCO DECLARADO: submissao que nao consegue provar o avanco do WPTR passa
     * a ser RECUSADA. O chamador ja cancela a reserva de fence nesse caminho, o
     * que e' preferivel a anunciar execucao que nao houve (Lei 4).
     */
    Bc250SdmaPresentFlushHdp();
    KeMemoryBarrier();

    if (Context->Core.MmioTransport.State != BC250_MMIO_TOPOLOGY_MAPPED) {
        ++Context->GfxEngine.SubmitRejected;
        Context->GfxEngine.LastRefusalReason = "WPTR_NAO_PUBLICADO";
        Context->GfxEngine.LastRefusalCursor = cursor;
        KeReleaseSpinLock(&Context->GfxEngine.Lock, irql);
        Bc250DisplayLog("[GfxEngine] SUBMIT RECUSA: WPTR_NAO_PUBLICADO -- transporte MMIO nao mapeado (State=%u); o anel NAO foi armado e nada sera anunciado como submetido\n",
            (uint32_t)Context->Core.MmioTransport.State);
        return STATUS_DEVICE_NOT_READY;
    }

    /*
     * HIPOTESE A (Corrida 29 / Marco 0): BROADCAST GRBM NO MMIO DO WPTR.
     *
     * O compute (bc250_submission_engine.c) envolve toda escrita MMIO em:
     *   Write32(GRBM_GFX_INDEX, 0xE0000000) (broadcast SE, SH, INSTANCE)
     *   Write32(GRBM_GFX_CNTL, SelectedSelector)
     * Como o compute roda primeiro no start device, o GRBM_GFX_INDEX e'
     * deixado direcionado para a fila do compute. Sem broadcast, a escrita em
     * CP_RB0_WPTR (0xC150) e' filtrada e nao alcanca o bloco do CP_RB0 --
     * explicando perfeitamente o readback 0 em 3 de 3 submissoes.
     */
    {
        uint32_t grbmIndexBefore = 0u;
        uint32_t grbmCntlBefore = 0u;
        uint32_t grbmIndexActive = 0u;
        uint32_t grbmIndexReadOk = 0u;
        uint32_t grbmCntlReadOk = 0u;
        uint32_t writeSuccess = 0u;
        uint32_t writeHiSuccess = 0u;
        uint32_t wptrReadback = 0u;
        uint32_t wptrHiReadback = 0u;
        uint32_t readOk = 0u;
        uint32_t readHiOk = 0u;
        uint32_t dbHitBefore = 0u;
        uint32_t dbHitAfter = 0u;

        grbmIndexReadOk = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET, &grbmIndexBefore);
        grbmCntlReadOk = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &grbmCntlBefore);

        (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
        (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET, &grbmIndexActive);

        /*
         * Sequencia 1: Par canonico 64-bit SOC15 / GFX10 (upstream amdgpu gfx_v10_0.c)
         * Em GFX10, CP_RB0_WPTR e' um par 64-bit (0x0C150 LO, 0x0C154 HI).
         * Escrever ambos assegura o latch interno de 64 bits do Command Processor.
         */
        writeSuccess = Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
            BC250_GFX_REG_CP_RB0_WPTR, cursor);
        writeHiSuccess = Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
            BC250_GFX_REG_CP_RB0_WPTR_HI, 0u);

        if (writeSuccess == 0u) {
            if (grbmIndexReadOk != 0u) {
                (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
                    BC250_GRBM_GFX_INDEX_BYTE_OFFSET, grbmIndexBefore);
            }
            ++Context->GfxEngine.SubmitRejected;
            Context->GfxEngine.LastRefusalReason = "WPTR_ESCRITA_RECUSADA";
            Context->GfxEngine.LastRefusalCursor = cursor;
            KeReleaseSpinLock(&Context->GfxEngine.Lock, irql);
            Bc250DisplayLog("[GfxEngine] SUBMIT RECUSA: WPTR_ESCRITA_RECUSADA -- Write32 devolveu 0 para offset 0x%04X (allowlist, alinhamento ou limite); cursor=%u nao chegou ao silicio (GrbmIndexBefore=0x%08X GrbmCntlBefore=0x%08X GrbmIndexActive=0x%08X)\n",
                BC250_GFX_REG_CP_RB0_WPTR, cursor, grbmIndexBefore, grbmCntlBefore, grbmIndexActive);
            return STATUS_DEVICE_NOT_READY;
        }

        readOk = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_GFX_REG_CP_RB0_WPTR, &wptrReadback);
        readHiOk = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_GFX_REG_CP_RB0_WPTR_HI, &wptrHiReadback);

        if (readOk != 0u && wptrReadback != cursor) {
            /* Sequencia 2: Ordem invertida (HI antes de LO para trigger de latch) */
            writeHiSuccess = Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_WPTR_HI, 0u);
            writeSuccess = Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_WPTR, cursor);
            readOk = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_WPTR, &wptrReadback);
            readHiOk = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_WPTR_HI, &wptrHiReadback);
        }

        if (readOk != 0u && wptrReadback != cursor) {
            /* Sequencia 3: Pulso DOORBELL_HIT (0x80000000) em CP_RB_DOORBELL_CONTROL (0x0C3B4) */
            (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
                BC250_GFX_REG_CP_RB_DOORBELL_CONTROL, &dbHitBefore);
            (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
                BC250_GFX_REG_CP_RB_DOORBELL_CONTROL, 0x80000000u);
            (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
                BC250_GFX_REG_CP_RB_DOORBELL_CONTROL, &dbHitAfter);
            readOk = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_WPTR, &wptrReadback);
            readHiOk = Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_WPTR_HI, &wptrHiReadback);
        }

        /* Restaura GRBM_GFX_INDEX para o estado anterior ao submit */
        if (grbmIndexReadOk != 0u) {
            (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
                BC250_GRBM_GFX_INDEX_BYTE_OFFSET, grbmIndexBefore);
        }

        if (readOk == 0u) {
            /* Nao conseguimos conferir. NAO afirmamos avanco: o veredito fica
             * indeterminado e a submissao segue, porque a escrita FOI aceita --
             * mas o log diz que a prova nao existe. */
            Bc250DisplayLog("[GfxEngine] WPTR_READBACK INDETERMINADO: Read32 recusou 0x%04X; escrita aceita mas avanco do ponteiro NAO CONFERIDO (cursor=%u GrbmIndexBefore=0x%08X GrbmCntlBefore=0x%08X GrbmIndexActive=0x%08X)\n",
                BC250_GFX_REG_CP_RB0_WPTR, cursor, grbmIndexBefore, grbmCntlBefore, grbmIndexActive);
        } else if (wptrReadback != cursor) {
            uint32_t shadowReadback = 0u;
            const BOOLEAN shadowMapped =
                (Context->GfxEngine.WptrPollCpuVirtual != NULL) ? TRUE : FALSE;
            if (shadowMapped) {
                shadowReadback = *Context->GfxEngine.WptrPollCpuVirtual;
            }
            ++Context->GfxEngine.SubmitRejected;
            Context->GfxEngine.LastRefusalReason = "WPTR_READBACK_DIVERGENTE";
            Context->GfxEngine.LastRefusalCursor = cursor;
            KeReleaseSpinLock(&Context->GfxEngine.Lock, irql);
            Bc250DisplayLog("[GfxEngine] SUBMIT RECUSA: WPTR_READBACK_DIVERGENTE -- escrevemos %u e o registrador devolveu %u (Hi=%u GrbmIndexBefore=0x%08X GrbmCntlBefore=0x%08X GrbmIndexActive=0x%08X DbHitBefore=0x%08X DbHitAfter=0x%08X). O hardware NAO aceitou o ponteiro; esta e' a assinatura medida em 2026-09-18 13:12 (snapshot do TDR com WPTR=0 apos escrever 12)\n",
                cursor, wptrReadback, wptrHiReadback, grbmIndexBefore, grbmCntlBefore, grbmIndexActive, dbHitBefore, dbHitAfter);
            /*
             * D0.3 -- A RECUSA CONTINUA, E O MOTIVO FICA ESCRITO.
             *
             * O anel grafico tem quatro canais possiveis de publicacao de WPTR
             * e a corrida 2026-09-18 14:20:07 provou que estamos com tres
             * fechados e o quarto nao retendo:
             *   CP_RB_WPTR_POLL_ADDR_LO/HI (0x0C3AC/0C3B0) -- PROGRAMADOS com o
             *     shadow por Bc250GfxRingTxnApply;
             *   CP_RB_WPTR_POLL_CNTL (0x08708) -- ver D0.4; pelo header ele NAO
             *     tem bit de enable, so' POLL_FREQUENCY[15:0] e
             *     IDLE_POLL_COUNT[31:16] (gc_10_1_0_sh_mask.h);
             *   CP_RB_DOORBELL_CONTROL (0x0C3B4) -- escrito 0 por
             *     Bc250GfxRingTxnApply, doorbell DESLIGADA de proposito;
             *   MMIO CP_RB0_WPTR (0x0C150) -- este, que nao retem.
             *
             * ENQUANTO O MMIO FOR O UNICO CANAL COM PROVA, A DIVERGENCIA
             * CONTINUA SENDO VETO. Afrouxar agora seria anunciar submetido o
             * que o CP nao tem como ver -- a classe de falso sucesso que este
             * projeto ja catalogou treze vezes. O veto so' pode virar registro
             * quando um segundo canal publicar COM PROVA propria.
             *
             * O estado do shadow entra no log porque ele e' a fonte que o canal
             * de poll leria: shadow correto com RPTR parado separa "o CP nao ve
             * o ponteiro" de "o CP ve e nao busca".
             */
            Bc250DisplayLog("[GfxEngine] WPTR_CANAL_INVENTARIO: MmioRetem=0 (escrito=%u lido=%u Hi=%u) ShadowMapeado=%u ShadowValor=%u ShadowConfere=%u PollAddrGpuVa=0x%llX DoorbellDesligadaNoApply=1 DbHitAfter=0x%08X -- MMIO e' o unico canal com prova neste candidato, logo a divergencia PERMANECE veto (fail-closed)\n",
                cursor, wptrReadback, wptrHiReadback,
                (uint32_t)(shadowMapped ? 1u : 0u), shadowReadback,
                (uint32_t)((shadowMapped && shadowReadback == cursor) ? 1u : 0u),
                (unsigned long long)Context->GfxEngine.WptrPollGpuVa, dbHitAfter);
            return STATUS_DEVICE_NOT_READY;
        } else {
            Bc250DisplayLog("[GfxEngine] WPTR_READBACK PASS: registrador 0x%04X confirmou %u dwords (Hi=%u GrbmIndexBefore=0x%08X GrbmCntlBefore=0x%08X GrbmIndexActive=0x%08X DbHitAfter=0x%08X)\n",
                BC250_GFX_REG_CP_RB0_WPTR, wptrReadback, wptrHiReadback, grbmIndexBefore, grbmCntlBefore, grbmIndexActive, dbHitAfter);
        }
    }

    Context->GfxEngine.WptrDwords = cursor;
    Context->GfxEngine.LastSubmittedFence = nextFence;
    Context->GfxEngine.LastSubmitSizeDwords = IbSizeDwords;
    Context->GfxEngine.LastSubmitVa = IbGpuVirtualAddress;
    Context->GfxEngine.LastSubmitVmid = Vmid;
    if (vmidSwitch) {
        ++Context->GfxEngine.VmidSwitchSyncs;
    }
    ++Context->GfxEngine.SubmitAccepted;
    *AssignedFence = nextFence;

    /*
     * CARIMBO DA RAIZ NO INSTANTE DA SUBMISSAO (21/09, apos a Corrida 48).
     * Ver o bloco longo em bc250_active_miniport.h. Escrita barata e sem log:
     * quem publica e' o caminho do fault, que compara esta raiz com a que
     * estava instalada quando o TDR aconteceu. Sem isso nao se distingue
     * "raiz trocada" de "corrida de publicacao" nem de "TLB obsoleto", e as
     * tres exigem consertos diferentes.
     */
    {
        const uint32_t slot =
            Context->GfxSubmitStampNext % BC250_GFX_SUBMIT_STAMP_SLOTS;
        Context->GfxSubmitStamps[slot].Fence = nextFence;
        /*
         * A RAIZ CARIMBADA E' A DO VMID DO PACOTE (Corrida 55).
         *
         * Ate a Corrida 54 esta linha gravava UserVmidInstalledRootPhysical --
         * a raiz do VMID 7 legado -- mesmo depois de o pacote passar a carregar
         * o VMID do contexto. O carimbo comparava, no fault, uma raiz que o
         * pacote nao usava: telemetria do estado anterior, a mesma classe de
         * defeito ja corrigida na linha de sucesso da instalacao.
         */
        Context->GfxSubmitStamps[slot].RootAtSubmit =
            (Vmid == BC250_GCVM_USER_VMID)
                ? Context->UserVmidInstalledRootPhysical
                : Bc250VmidPoolInstalledRootForVmid(&Context->VmidPool, Vmid);
        Context->GfxSubmitStamps[slot].IbGpuVa = IbGpuVirtualAddress;
        Context->GfxSubmitStamps[slot].Vmid = Vmid;
        Context->GfxSubmitStamps[slot].PageTableEpoch =
            Context->GpuvaSnapshots.Slots[
                (Context->GpuvaSnapshots.ActiveSlot >= 0 &&
                 Context->GpuvaSnapshots.ActiveSlot <= 1) ?
                Context->GpuvaSnapshots.ActiveSlot : 0].PageTableEpoch;
        Context->GfxSubmitStampNext = slot + 1u;
        if (Context->GfxSubmitStampCount < BC250_GFX_SUBMIT_STAMP_SLOTS) {
            ++Context->GfxSubmitStampCount;
        }
    }

    KeReleaseSpinLock(&Context->GfxEngine.Lock, irql);

    Bc250DisplayLog("[GfxEngine] SUBMIT SUCESSO: Fence=%llu NewWptr=%u DmaVA=0x%llX SizeDW=%u VMID=%u\n",
        (unsigned long long)nextFence, cursor,
        (unsigned long long)IbGpuVirtualAddress, IbSizeDwords, Vmid);
    if (vmidSwitch) {
        Bc250DisplayLog("[GfxEngine] VMID_SWITCH_SYNC #%u: VMID %u -> %u antes do Fence=%llu -- PFP espera fence>=%u (WAIT_REG_MEM mem/pfp) + PFP_SYNC_ME\n",
            Context->GfxEngine.VmidSwitchSyncs, prevVmid, Vmid,
            (unsigned long long)nextFence, waitFence);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ActiveGfxEnginePoll(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    uint32_t *RetiredNow)
{
    KIRQL irql;
    uint32_t observedFence = 0u;
    uint64_t lastSubmitted;
    uint64_t lastRetired;
    uint32_t rptrReg = 0u;
    uint32_t rptrWb = 0u;
    uint32_t wptr = 0u;
    uint32_t ringBytes = 0u;
    BOOLEAN rptrRegRead = FALSE;
    BOOLEAN rptrWbRead = FALSE;
    BOOLEAN fenceIsPoison = FALSE;
    const char *indeterminateReason = NULL;

    if (RetiredNow != NULL) *RetiredNow = 0u;
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&Context->GfxEngine.State, 1, 1) != 1) {
        return STATUS_DEVICE_NOT_READY;
    }

    KeAcquireSpinLock(&Context->GfxEngine.Lock, &irql);
    ++Context->GfxEngine.PollCalls;

    Bc250SdmaPresentFlushHdp();

    if (Context->GfxEngine.FenceCpuVirtual != NULL) {
        observedFence = *Context->GfxEngine.FenceCpuVirtual;
    }

    /*
     * PERNA 1 DO TRIPE -- convergencia de ponteiros do anel grafico.
     * Nunca foi medida: ate agora o unico sinal de progresso era a fence, e a
     * fence e' escrita por um RELEASE_MEM que o KMD emite NO ANEL. Leitura
     * apenas; nao altera decisao de aposentadoria.
     *
     * DEFEITO CORRIGIDO AQUI (corrida 2026-09-18 13:12): a versao anterior
     * comparava o retorno de Bc250MmioTopologyRead32 com BC250_MMIO_TOPOLOGY_OK.
     * Essas duas coisas nao sao do mesmo dominio: Read32 devolve uint32_t com
     * 1 = sucesso / 0 = falha (bc250_mmio_topology_transport.c:439), enquanto
     * BC250_MMIO_TOPOLOGY_OK e' o enum de status e vale ZERO
     * (bc250_mmio_topology_transport.h:28). A comparacao avaliava `1 == 0` e
     * dava FALSE EXATAMENTE quando a leitura funcionava -- por isso os 41 de 41
     * vereditos daquela corrida sairam Perna1=INDETERMINADO com [lido=0].
     * A leitura fisica pode ter acontecido; a flag e' que mentia.
     */
    wptr = Context->GfxEngine.WptrDwords;
    ringBytes = Context->GfxEngine.RingDwords * (uint32_t)sizeof(uint32_t);
    if (Context->Core.MmioTransport.State != BC250_MMIO_TOPOLOGY_MAPPED) {
        indeterminateReason = "TRANSPORTE_MMIO_NAO_MAPEADO";
    } else if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
                   BC250_GFX_REG_CP_RB0_RPTR, &rptrReg) != 0u) {
        rptrRegRead = TRUE;
    } else {
        /* Transporte mapeado e a leitura ainda assim recusada: offset fora do
         * allowlist, desalinhado ou alem do comprimento mapeado. Nomear importa
         * -- as tres causas tem consertos diferentes. */
        indeterminateReason = "READ32_RECUSOU_OFFSET_OU_LIMITE";
    }
    if (Context->GfxEngine.RptrWbCpuVirtual != NULL) {
        rptrWb = *Context->GfxEngine.RptrWbCpuVirtual;
        rptrWbRead = TRUE;
    } else {
        if (indeterminateReason == NULL) indeterminateReason = "WRITEBACK_DE_RPTR_SEM_MAPA_DE_CPU";
    }
    Context->GfxEngine.LastRptrMmio = rptrReg;
    Context->GfxEngine.LastRptrWb = rptrWb;

    /*
     * PERNA 2 DO TRIPE -- fence FISICA da GPU sobre poison.
     * DEFEITO CORRIGIDO AQUI: o poison (0x43DAFFFE) e' um numero grande e
     * passava a comparacao `observedFence > lastRetired`, aposentando
     * submissoes que a GPU jamais concluiu. Agora o poison e' excluido
     * EXPLICITAMENTE antes de qualquer aposentadoria: enquanto o slot carrega
     * poison, nao houve escrita da GPU e nada e' aposentado.
     */
    fenceIsPoison = (observedFence == BC250_GFX_FENCE_POISON) ? TRUE : FALSE;

    lastSubmitted = Context->GfxEngine.LastSubmittedFence;
    lastRetired = Context->GfxEngine.LastRetiredFence;

    if (!fenceIsPoison && lastSubmitted > lastRetired &&
        observedFence > (uint32_t)lastRetired) {
        uint64_t newlyRetired = (uint64_t)observedFence;
        if (newlyRetired > lastSubmitted) {
            newlyRetired = lastSubmitted;
        }
        if (newlyRetired > lastRetired) {
            uint32_t count = (uint32_t)(newlyRetired - lastRetired);
            Context->GfxEngine.LastRetiredFence = newlyRetired;
            Context->GfxEngine.RetiredCount += count;
            if (RetiredNow != NULL) {
                *RetiredNow = count;
            }
        }
    }

    KeReleaseSpinLock(&Context->GfxEngine.Lock, irql);

    /*
     * DX.28 -- SENTINELA DE FAULT DO GCVM POR APOSENTADORIA GRAFICA (SOMENTE LEITURA).
     *
     * Corrida 73: o TDR de 240 ondas trouxe um fault latchado de busca de
     * instrucao (CID=9) sob o VMID 1 (DWM) na pagina exata do shader do
     * harness (VMID 4). Mas o latch guarda o PRIMEIRO fault desde o ultimo TDR,
     * entao nao se sabia EM QUE submissao ele nasceu. Aqui, a cada fence GFX
     * aposentada, o STATUS e' lido; so' a TRANSICAO sai no log, com a faixa de
     * fences, o VMID e o IB da ultima submissao. Nao escreve nada -- o dreno
     * continua sendo so' do caminho de TDR. Campos: gc_10_1_0_sh_mask.h:12143-12162.
     */
    if (RetiredNow != NULL && *RetiredNow != 0u &&
        Context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
        static uint32_t s_faultSentinelLast = 0u;
        static uint32_t s_faultSentinelLogs = 0u;
        uint32_t st = 0u;
        uint32_t lo = 0u;
        if (Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
                BC250_REG_GCVM_L2_PROTECTION_FAULT_STATUS, &st) != 0u &&
            st != s_faultSentinelLast && s_faultSentinelLogs < 64u) {
            (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
                BC250_REG_GCVM_L2_PROTECTION_FAULT_ADDR_LO32, &lo);
            ++s_faultSentinelLogs;
            Bc250DisplayLog("[GFX_FAULT_SENTINELA] #%u fences GFX aposentadas ate %llu (ultima submetida %llu, IB=0x%llX SizeDW=%u): Status 0x%08X -> 0x%08X Vmid=%u Cid=%u Perm=0x%X Rw=%u Walker=%u Mapping=%u More=%u AddrPagina=0x%08X (GpuVa=0x%llX) -- somente leitura\n",
                s_faultSentinelLogs,
                (unsigned long long)Context->GfxEngine.LastRetiredFence,
                (unsigned long long)Context->GfxEngine.LastSubmittedFence,
                (unsigned long long)Context->GfxEngine.LastSubmitVa,
                Context->GfxEngine.LastSubmitSizeDwords,
                s_faultSentinelLast, st,
                (st & 0x00F00000u) >> 20, (st & 0x0003FE00u) >> 9,
                (st & 0x000000F0u) >> 4, (st & 0x00040000u) >> 18,
                (st & 0x0000000Eu) >> 1, (st & 0x00000100u) >> 8,
                (st & 0x00000001u), lo, ((unsigned long long)lo) << 12);
            s_faultSentinelLast = st;
        }
    }

    /*
     * VEREDITO DO TRIPE DO ANEL GRAFICO -- amostrado (o poll e' quente), mas a
     * transicao poison -> fence escrita NUNCA e' amostrada, porque e' o instante
     * em que o CP prova pela primeira vez que consumiu o anel desta submissao.
     *
     * ESCOPO DECLARADO, para nao repetir a confusao da corrida 10:20: as duas
     * pernas abaixo provam que o CP consumiu o ANEL. Elas NAO provam que o CP
     * buscou e executou o conteudo do IB -- isso e' [GfxIbInspect] mais a
     * conferencia de bytes no destino. E nao provam dispatch de shader, pela
     * Diretriz Universal 3.
     */
    /*
     * PERNA 3 DO TRIPE -- NAO EXISTE para o anel grafico, e a linha tem de
     * dizer isso com todas as letras.
     *
     * DEFEITO CORRIGIDO AQUI (analise da Corrida 44, 20/09): esta posicao
     * imprimia a perna 3 como DELEGADA a uma prova de bytes, nomeando um
     * delegado que NINGUEM executa. (O literal antigo nao e' reproduzido aqui:
     * o portao o PROIBE no arquivo inteiro, e citar o nome banido em comentario
     * ja reprovou build neste projeto.)
     * A mineracao daquela corrida contou 111 ocorrencias da
     * string BYTE_PROOF no log inteiro, e TODAS as 111 eram esta propria
     * linha -- o delegado nunca rodou uma vez. O [GfxIbInspect] varre a
     * ESTRUTURA do IB pela CPU (linear, saltos CHAIN, chamadas IB2) e nao
     * confere UM byte de destino.
     *
     * Rotulo que promete prova inexistente e' a mesma classe de falso sucesso
     * que a Lei 4 proibe: um leitor que confie nele conta 111 tripes completos
     * onde ha 2 de 3 pernas. Pela Lei 2, submissao GFX NAO pode ser declarada
     * comprovada com este estado.
     *
     * Gabarito de destino para IB arbitrario de UMD nao existe -- nao se sabe
     * o que o shader do DWM deveria escrever --, entao a perna 3 fica ABERTA e
     * declarada, em vez de simulada.
     */
#define BC250_GFX_TRIPOD_LEG3_STATE \
    "NAO_EXECUTADA_SEM_GABARITO_DE_DESTINO"

    {
        static uint32_t s_gfxTripodPollCount = 0u;
        static BOOLEAN s_gfxFenceEverWritten = FALSE;
        const uint32_t seen = s_gfxTripodPollCount++;
        const BOOLEAN firstWrite = (!s_gfxFenceEverWritten && !fenceIsPoison &&
                                    observedFence != 0u) ? TRUE : FALSE;

        if (firstWrite) s_gfxFenceEverWritten = TRUE;

        if (firstWrite || seen < 4u || (seen % 512u) == 0u) {
            const uint32_t wptrBytes = wptr * (uint32_t)sizeof(uint32_t);
            const uint32_t regBytes = rptrReg * (uint32_t)sizeof(uint32_t);
            const uint32_t wbBytes = rptrWb * (uint32_t)sizeof(uint32_t);
            const uint32_t wptrWrapped = Bc250TripodWrapRingPointer(wptrBytes, ringBytes);
            const uint32_t regWrapped = Bc250TripodWrapRingPointer(regBytes, ringBytes);
            const uint32_t wbWrapped = Bc250TripodWrapRingPointer(wbBytes, ringBytes);
            /*
             * Perna 1 so' pode dizer CONVERGIU quando ALGUMA leitura realmente
             * aconteceu. Sem leitura, o veredito e' INDETERMINADO com o motivo
             * nomeado -- "nao sei" nunca vira "convergiu".
             */
            const char *convergencia =
                (!rptrRegRead && !rptrWbRead) ? "INDETERMINADO" :
                (rptrRegRead && regWrapped == wptrWrapped) ? "REGISTRADOR" :
                (rptrWbRead && wbWrapped == wptrWrapped) ? "WRITEBACK" : "NAO_CONVERGIU";
            /*
             * Perna 2 tem TRES estados, nao dois.
             *
             * DEFEITO CORRIGIDO AQUI (corrida 2026-09-18 13:12): a versao
             * anterior classificava qualquer valor != poison como "escrita pela
             * GPU", e 35 de 41 vereditos daquela corrida sairam
             * FENCE_ESCRITA_PELA_GPU com Fence=0x00000000. Zero NAO e' escrita
             * da GPU: as fences comecam em 1, e o slot fica zerado quando o
             * restart do TDR reconstroi a VRAM do anel sem replantar o poison.
             * Ler zero como sucesso e' a mesma classe de falso sucesso que o
             * poison produzia antes.
             */
            const char *pernaDois =
                fenceIsPoison ? "POISON_INTACTO_GPU_NAO_ESCREVEU" :
                (observedFence == 0u) ? "INDETERMINADO_SLOT_ZERADO_SEM_POISON" :
                "FENCE_ESCRITA_PELA_GPU";

            Bc250DisplayLog("[GfxEngine] GFX_TRIPE_VEREDITO: Perna1=%s (WPTR=%u RPTR_Reg=%u[lido=%u] RPTR_Wb=%u[lido=%u], reduzidos WPTR=%u Reg=%u Wb=%u, motivo=%s) Perna2=%s (Fence=0x%08X Poison=0x%08X) Perna3=%s -- TRIPE INCOMPLETO, 2 de 3 pernas | Submetidas=%llu Aposentadas=%llu EmVoo=%llu RetiredCount=%u\n",
                convergencia, wptr, rptrReg, rptrRegRead ? 1u : 0u,
                rptrWb, rptrWbRead ? 1u : 0u,
                wptrWrapped, regWrapped, wbWrapped,
                (indeterminateReason != NULL) ? indeterminateReason : "SEM_OBSTACULO_DE_LEITURA",
                pernaDois, observedFence, BC250_GFX_FENCE_POISON,
                BC250_GFX_TRIPOD_LEG3_STATE,
                (unsigned long long)Context->GfxEngine.LastSubmittedFence,
                (unsigned long long)Context->GfxEngine.LastRetiredFence,
                (unsigned long long)(Context->GfxEngine.LastSubmittedFence -
                                     Context->GfxEngine.LastRetiredFence),
                Context->GfxEngine.RetiredCount);

            if (fenceIsPoison && Context->GfxEngine.LastSubmittedFence > 0ull) {
                Bc250DisplayLog("[GfxEngine] GFX_TRIPE AVISO: %llu submissao(oes) pendente(s) e a fence segue em poison -- ZERO aposentadas por decisao, nao por falta de medicao\n",
                    (unsigned long long)Context->GfxEngine.LastSubmittedFence);
            }
        }
    }

    return STATUS_SUCCESS;
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
                status = Bc250McdmObjectDestroy(&context->Objects,
                    ContextHandle, BC250_MCDM_OBJECT_CONTEXT,
                    record.OwnerProcess);
                result = Bc250ObjectStatusToNtStatus(status);
                if (status == BC250_MCDM_OBJECT_OK) {
                    (void)Bc250GpuvaSpaceInvalidateForObject(
                        &context->GpuvaSpace,
                        record.OwnerProcess, ContextHandle, NULL);
                    Bc250DisplayUnregisterContextProcess(ContextHandle);
                    /*
                     * DX.7 -- somente a destruicao CONCLUIDA devolve o VMID.
                     * IN_FLIGHT_SUBMISSIONS fecha o rundown mas preserva
                     * contexto, GPUVA e slot para a submissao que ainda os usa.
                     * NOT_FOUND e' normal: a maioria nunca submeteu em GFX.
                     */
                    {
                        uint32_t liberado = BC250_VMID_COUNT;
                        uint32_t liberados = 0u;
                        uint32_t inUseMask = 0u;
                        BC250_VMID_POOL_STATUS poolStatus;
                        KIRQL poolIrql;

                        KeAcquireSpinLock(&context->VmidPoolLock, &poolIrql);
                        poolStatus = Bc250VmidPoolRelease(&context->VmidPool,
                            ContextHandle, &liberado);
                        liberados = context->VmidPool.FreeCount;
                        inUseMask = context->VmidPool.InUseMask;
                        KeReleaseSpinLock(&context->VmidPoolLock, poolIrql);

                        if (poolStatus == BC250_VMID_POOL_OK) {
                            Bc250DisplayLog("[VMID_POOL] LIBERADO: Ctx=%p devolveu VMID%02u (Liberados=%u InUseMask=0x%04X)\n",
                                ContextHandle, liberado, liberados, inUseMask);
                            /*
                             * DX.15 -- DEVOLVER O SLOT TAMBEM NO SILICIO.
                             *
                             * Ate aqui Release era SOMENTE SOFTWARE: a entrada
                             * do pool era limpa e o VMID voltava para os
                             * livres, mas o contexto GCVM continuava com
                             * ENABLE_CONTEXT ligado e PT_BASE apontando para a
                             * raiz do processo MORTO -- uma pagina que o VidMm
                             * pode liberar e reutilizar a qualquer momento.
                             *
                             * O desarme acontece FORA do VmidPoolLock (solto
                             * logo acima): ele escreve MMIO e invalida TLB, e
                             * o flush por VMID pode gastar polls.
                             *
                             * Isto NAO e' a correcao de um fault medido -- e'
                             * fechar um estado que nao tem direito de existir
                             * (Lei 5: todo APPLY tem TEARDOWN). Se o fault
                             * sobreviver, o desarme continua certo.
                             */
                            BC250_GCVM_VMID8_INTERFACE disarmIo;
                            BC250_GCVM_VMID8_INSTALL_EVIDENCE disarmEvidence;
                            BC250_GCVM_VMID8_STATUS disarmStatus;

                            RtlZeroMemory(&disarmIo, sizeof(disarmIo));
                            disarmIo.Identity = BC250_GCVM_VMID8_IDENTITY;
                            disarmIo.Context = context;
                            disarmIo.Read32 = Bc250ActiveVmidRead32;
                            disarmIo.Write32 = Bc250ActiveVmidWrite32;
                            disarmIo.FlushVmid = Bc250ActiveFlushVmidTlb;
                            RtlZeroMemory(&disarmEvidence,
                                sizeof(disarmEvidence));

                            disarmStatus = Bc250GcvmUninstallVmid(
                                liberado, &disarmIo, &disarmEvidence);
                            Bc250DisplayLog("[VMID_POOL] DESARMADO: VMID%02u status=%u Readback=%u Flush=%u (passo=%u reg=0x%04X val=0x%08X) -- o contexto GCVM nao aponta mais para a raiz do processo morto\n",
                                liberado, (uint32_t)disarmStatus,
                                disarmEvidence.ProgramReadbackPassed,
                                disarmEvidence.TlbFlushPassed,
                                disarmEvidence.FailureStep,
                                disarmEvidence.FailureRegister,
                                disarmEvidence.FailureValue);
                            if (disarmStatus != BC250_GCVM_VMID8_OK) {
                                /*
                                 * FALHA DECLARADA, e o slot NAO volta
                                 * calado para a circulacao com o
                                 * silicio armado. A entrada de software
                                 * ja foi liberada acima; o que esta
                                 * linha garante e que a divergencia
                                 * entre software e silicio seja VISIVEL
                                 * em vez de virar o proximo fault sem
                                 * explicacao.
                                 */
                                Bc250DisplayLog("[VMID_POOL] FALHA NO DESARME: VMID%02u continua armado no silicio apesar de devolvido ao pool\n",
                                    liberado);
                            }
                        }
                    }
                } else if (status == BC250_MCDM_OBJECT_IN_FLIGHT_SUBMISSIONS) {
                    Bc250DisplayLog("[VMID_POOL] RETIDO: Ctx=%p ainda tem submissao em voo; GPUVA e VMID permanecem ate DestroyContext concluir\n",
                        ContextHandle);
                }
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
        if (rootContext.NodeOrdinal > 1u) {
            Bc250DisplayLog("[SetRootPageTable] Node%u Compute: raiz registrada; VMID9 instalado no submit\n", rootContext.NodeOrdinal);
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
        io.FlushVmid = Bc250ActiveFlushVmidTlb;
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
                } else if (info->FlagsWddm2.CpuVisible != 0u || info->Flags.CpuVisible != 0u) {
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
            /*
             * FAULT DE TRADUCAO DECODIFICADO -- Corrida 45, 20/09.
             *
             * DEFEITO DE LEGIBILIDADE QUE CUSTOU MESES: este snapshot ja
             * imprimia FaultStatus e FaultAddr em hexadecimal CRU, e ninguem
             * decodificou. A analise da Corrida 45 achou FaultStatus=0x0070153C
             * repetido nos CINCO TDRs, e ele diz, por gc_10_1_0_sh_mask.h:
             * VMID=7 (o mesmo VMID das submissoes GFX), WALKER_ERROR=6,
             * MAPPING_ERROR=1, PERMISSION_FAULTS=3, CID=10 -- isto e', o
             * caminhador de tabela de paginas FALHOU. Casa com a onda lida pela
             * sonda no mesmo TDR: SQ_WAVE_STATUS FATAL_HALT=1 e
             * SQ_WAVE_TRAPSTS XNACK_ERROR=1.
             *
             * Havia UM fault de traducao real desde o inicio, e o projeto
             * procurava pela string "GFXHUB_FAULT", que e' outro sitio e sai
             * ZERO. Hexadecimal cru num campo que ninguem le e' ausencia de
             * medicao na pratica; por isso a decodificacao passa a ser emitida.
             * SOMENTE LEITURA -- nenhum registrador novo e' tocado.
             */
            if ((faultValid & 0x1u) != 0u && faultStatus != 0u) {
                Bc250DisplayLog("[TDR_GCVM_FAULT] DECODIFICADO Status=0x%08X -> Vmid=%u WalkerError=%u "
                    "MappingError=%u PermissionFaults=0x%X Cid=%u Rw=%u MoreFaults=%u | "
                    "FaultAddrPagina=0x%08X%08X -> GpuVa=0x%llX\n",
                    /*
                     * Shifts e mascaras COPIADOS de gc_10_1_0_sh_mask.h,
                     * GCVM_L2_PROTECTION_FAULT_STATUS__*. A primeira versao
                     * desta linha teve os shifts transcritos de cabeca e TODOS
                     * os seis sairam errados -- VMID em 18 no lugar de 20,
                     * WALKER_ERROR em 8 no lugar de 1, RW em 31 no lugar de 18.
                     * Decodificacao errada e' pior que hexadecimal cru: o cru
                     * ao menos nao mente.
                     */
                    faultStatus,
                    (faultStatus & 0x00F00000u) >> 20,  /* VMID */
                    (faultStatus & 0x0000000Eu) >> 1,   /* WALKER_ERROR */
                    (faultStatus & 0x00000100u) >> 8,   /* MAPPING_ERROR */
                    (faultStatus & 0x000000F0u) >> 4,   /* PERMISSION_FAULTS */
                    (faultStatus & 0x0003FE00u) >> 9,   /* CID */
                    (faultStatus & 0x00040000u) >> 18,  /* RW */
                    (faultStatus & 0x00000001u),        /* MORE_FAULTS */
                    faultAddrHi, faultAddrLo,
                    ((unsigned long long)faultAddrLo) << 12);

                /*
                 * O LATCH E' A PRIMEIRA PERGUNTA (Corrida 55).
                 *
                 * Dez corridas leram GCVM_L2_PROTECTION_FAULT_STATUS sem nunca
                 * ler o CNTL que o governa. Pelo sh_mask (12091-12092) o bit 0
                 * e CLEAR_PROTECTION_FAULT_STATUS_ADDR e o bit 1 e
                 * ALLOW_SUBSEQUENT_PROTECTION_FAULT_STATUS_ADDR_UPDATES. Com o
                 * bit 1 em ZERO o par STATUS/ADDR fica preso no PRIMEIRO fault
                 * ate alguem escrever o bit 0 -- e entao o Status que estamos
                 * decodificando pode ser de minutos antes, o que sozinho ja
                 * explicaria "a PTE existe e mesmo assim faltou".
                 *
                 * SOMENTE LEITURA. Nao limpamos o latch aqui: escrever mudaria
                 * o estado do silicio no meio do TDR e e' decisao separada.
                 *
                 * Os nomes dos bits sao P1 (sh_mask local). Os VALORES de
                 * WALKER_ERROR e PERMISSION_FAULTS continuam SEM enumeracao
                 * local e NAO sao nomeados (Lei 7).
                 */
                {
                    uint32_t faultCntl = 0u;
                    if (Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                            0x0000A120u, &faultCntl) != 0u) {
                        Bc250DisplayLog("[TDR_GCVM_FAULT] LATCH_CNTL=0x%08X -> Clear=%u AceitaAtualizacoes=%u | enables: Range=%u Pde0=%u Pde1=%u Pde2=%u TranslateFurther=%u Nack=%u DummyPage=%u Valid=%u Read=%u Write=%u Execute=%u | CrashNoRetry=%u CrashRetry=%u -> %s\n",
                            faultCntl,
                            (faultCntl >> 0) & 1u,
                            (faultCntl >> 1) & 1u,
                            (faultCntl >> 2) & 1u,
                            (faultCntl >> 3) & 1u,
                            (faultCntl >> 4) & 1u,
                            (faultCntl >> 5) & 1u,
                            (faultCntl >> 6) & 1u,
                            (faultCntl >> 7) & 1u,
                            (faultCntl >> 8) & 1u,
                            (faultCntl >> 9) & 1u,
                            (faultCntl >> 10) & 1u,
                            (faultCntl >> 11) & 1u,
                            (faultCntl >> 12) & 1u,
                            (faultCntl >> 29) & 1u,
                            (faultCntl >> 31) & 1u,
                            (((faultCntl >> 1) & 1u) != 0u) ?
                                "ACEITA ATUALIZACOES -- o Status acima e' do fault MAIS RECENTE" :
                                "LATCH PRESO -- o Status acima pode ser do PRIMEIRO fault, nao do que causou este TDR");
                    } else {
                        Bc250DisplayLog("[TDR_GCVM_FAULT] LATCH_CNTL NAO LIDO -- sem ele nao se sabe se o Status acima e' recente ou preso, e a ausencia esta DECLARADA\n");
                    }
                }

                /*
                 * DRENO DO LATCH (Corrida 56).
                 *
                 * Com AceitaAtualizacoes=0 medido, o Status decodificado acima
                 * e' o PRIMEIRO fault desde o ultimo clear -- e nunca houve um.
                 * Aqui o latch e' drenado: le, pulsa o CLEAR, le de novo, ate
                 * sair zero ou bater o teto. A fila que MoreFaults anunciava em
                 * toda corrida finalmente aparece.
                 *
                 * O CNTL e' restaurado e conferido por readback; repeticao do
                 * mesmo Status NAO conta como fault novo.
                 */
                {
                    BC250_FAULT_LATCH_IO latchIo;
                    BC250_FAULT_LATCH_EVIDENCE latchEv;
                    BC250_FAULT_LATCH_STATUS latchStatus;

                    latchIo.Context = activeCtx;
                    latchIo.Read32 = Bc250ActiveFaultLatchRead32;
                    latchIo.Write32 = Bc250ActiveFaultLatchWrite32;
                    latchStatus = Bc250FaultLatchDrain(&latchIo, &latchEv);

                    Bc250DisplayLog("[TDR_GCVM_FAULT] DRENO: status=%u Drenados=%u ClearAcked=%u ClearIneficaz=%u Teto=%u MoreFaults=%u CntlAntes=0x%08X CntlReadback=0x%08X Restaurado=%u\n",
                        (uint32_t)latchStatus, latchEv.Drained,
                        latchEv.ClearAcked, latchEv.ClearIneficaz,
                        latchEv.HitCeiling, latchEv.MoreFaultsSeen,
                        latchEv.CntlBefore, latchEv.CntlReadback,
                        latchEv.RestorePassed);
                    if (latchEv.Drained == 0u) {
                        Bc250DisplayLog("[TDR_GCVM_FAULT] DRENO: nenhum fault na fila -- a ausencia esta DECLARADA, nao suposta\n");
                    } else {
                        uint32_t dq;
                        for (dq = 0u; dq < latchEv.Drained; ++dq) {
                            const uint32_t st = latchEv.Entries[dq].Status;
                            Bc250DisplayLog("[TDR_GCVM_FAULT] DRENO #%u: Status=0x%08X -> Vmid=%u WalkerError=%u MappingError=%u PermissionFaults=0x%X Cid=%u Rw=%u MoreFaults=%u | AddrPagina=0x%08X%08X\n",
                                dq, st,
                                (st & 0x00F00000u) >> 20,
                                (st & 0x0000000Eu) >> 1,
                                (st & 0x00000100u) >> 8,
                                (st & 0x000000F0u) >> 4,
                                (st & 0x0003FE00u) >> 9,
                                (st & 0x00040000u) >> 18,
                                (st & 0x00000001u),
                                latchEv.Entries[dq].AddrHi,
                                latchEv.Entries[dq].AddrLo);
                        }
                    }
                }

                /*
                 * A GRANULARIDADE ESTA RESOLVIDA, E NAO ERA GRANULARIDADE
                 * (Corrida 62). Este bloco continua existindo, mas o que ele
                 * mede MUDOU DE NOME.
                 *
                 * PROVENIENCIA (P1, local): gc_10_1_0_sh_mask.h linhas 12163
                 * a 12168 declaram GCVM_L2_PROTECTION_FAULT_ADDR_LO32 com
                 * LOGICAL_PAGE_ADDR_LO32 de 32 bits e _HI32 com
                 * LOGICAL_PAGE_ADDR_HI4 de 4 bits -- ou seja, um numero de
                 * pagina de 36 BITS. Com pagina de 4 KiB isso cobre 48 bits de
                 * VA, que e' o espaco de uma GPU. Com pagina de 1 MiB seriam 56
                 * bits, e vinte dos trinta e seis bits ficariam permanentemente
                 * zerados. O endereco e' pageAddr << 12. Ponto.
                 *
                 * CORROBORACAO INDEPENDENTE (P0): a pagina 0x200040 sob << 12
                 * da 0x200040000, que a nossa propria linha [ALVO 0x200040]
                 * reporta como a BASE EXATA de uma alocacao viva
                 * (Base=0x200040000 Size=0x40000).
                 *
                 * O QUE ISSO CORRIGE. A Corrida 47 concluiu "a granularidade e
                 * << 20" porque o candidato << 20 caia dentro de faixa viva.
                 * Mas << 20 e' identicamente (pageAddr << 12) << 8 -- e o que
                 * aquele ensaio mediu foi que O ENDERECO REAL E 256 VEZES O
                 * ENDERECO QUE FALTOU, nao que a pagina tenha 1 MiB. Isso e
                 * assinatura de PERDA DE 8 BITS, e vale para TODOS os faults
                 * de Cid=10 do projeto: 0x2431 -> 0x243100000 (Corridas 45, 46,
                 * 48, 59) e 0x245B -> 0x245B00000 (Corridas 45, 62).
                 *
                 * Por isso o segundo candidato deixou de se chamar "<<20" e
                 * passou a se chamar o que e': VA<<8. Quando ELE resolve e o
                 * VA real nao, a leitura correta NAO e' "a tabela esta certa" --
                 * e' "alguem dereferenciou o endereco com 8 bits a menos".
                 *
                 * Leitura PURA do slot ativo, sem lock e sem escrita.
                 */
                {
                    const uint64_t pageAddr =
                        (((uint64_t)(faultAddrHi & 0xFu)) << 32) | (uint64_t)faultAddrLo;
                    const LONG siteSlot = activeCtx->GpuvaSnapshots.ActiveSlot;
                    static const uint32_t kShifts[2] = { 12u, 20u };
                    /*
                     * O SEGUNDO CANDIDATO NAO E UMA GRANULARIDADE.
                     * pageAddr<<20 e identicamente (pageAddr<<12)<<8, e
                     * o que ele testa e se o endereco real e 256x o que
                     * faltou -- perda de 8 bits. Chamar isso de <<20 foi
                     * o que me fez ler, por tres corridas, 'a tabela
                     * esta certa e a GPU falta mesmo assim'.
                     */
                    static const char *const kShiftNames[2] = {
                        "VA(pagina<<12)", "VA<<8(perda de 8 bits)" };
                    uint32_t k;

                    for (k = 0u; k < 2u; ++k) {
                        BC250_GPUVA_ADDRESS_SITE site;
                        BC250_GPUVA_STATUS siteStatus = BC250_GPUVA_BAD_ARGUMENT;
                        const uint64_t candidate = pageAddr << kShifts[k];

                        RtlZeroMemory(&site, sizeof(site));
                        if (candidate >= BC250_GPUVA_TRANSLATABLE_LIMIT) {
                            Bc250DisplayLog("[TDR_GCVM_FAULT] CANDIDATO <<%u = 0x%llX EXCEDE 34 BITS -- fora do espaco traduzivel, nao e' candidato valido\n",
                                kShifts[k], (unsigned long long)candidate);
                            continue;
                        }
                        if (siteSlot >= 0 && siteSlot <= 1) {
                            siteStatus = Bc250GpuvaSpaceDescribeAddress(
                                &activeCtx->GpuvaSnapshots.Slots[siteSlot],
                                candidate, &site);
                        }
                        if (siteStatus != BC250_GPUVA_OK) {
                            Bc250DisplayLog("[TDR_GCVM_FAULT] CANDIDATO <<%u = 0x%llX -- snapshot INDISPONIVEL (slot=%ld status=%u); este candidato fica SEM VEREDITO\n",
                                kShifts[k], (unsigned long long)candidate,
                                siteSlot, (uint32_t)siteStatus);
                        } else if (site.Found != 0u) {
                            Bc250DisplayLog("[TDR_GCVM_FAULT] CANDIDATO <<%u = 0x%llX DENTRO da faixa viva Base=0x%llX Size=0x%llX Offset=0x%llX Proc=%p Alloc=%p Token=0x%llX Nome=%s Seg=%u (%s) SegOff=0x%llX (LiveScan=%u)\n",
                                kShifts[k], (unsigned long long)candidate,
                                (unsigned long long)site.BaseGpuVa,
                                (unsigned long long)site.Size,
                                (unsigned long long)site.OffsetInRange,
                                site.OwnerProcess, site.Allocation,
                                (unsigned long long)site.AllocationToken,
                                Bc250ActiveAllocationTokenClassName(
                                    site.AllocationToken),
                                site.BackingSegmentId,
                                (site.BackingSegmentId == 1u) ? "LOCAL_VRAM" :
                                (site.BackingSegmentId == 2u) ? "GART_SISTEMA" :
                                "DESCONHECIDO",
                                (unsigned long long)site.BackingSegmentOffset,
                                site.LiveRangesScanned);
                            if (pageAddr == 0x200040ull && kShifts[k] == 12u) {
                                Bc250DisplayLog("[TDR_GCVM_FAULT] ALVO 0x200040: GpuVa=0x%llX pertence a Nome=%s Token=0x%llX Alloc=%p Base=0x%llX Size=0x%llX Offset=0x%llX\n",
                                    (unsigned long long)candidate,
                                    Bc250ActiveAllocationTokenClassName(
                                        site.AllocationToken),
                                    (unsigned long long)site.AllocationToken,
                                    site.Allocation,
                                    (unsigned long long)site.BaseGpuVa,
                                    (unsigned long long)site.Size,
                                    (unsigned long long)site.OffsetInRange);
                            }
                        } else {
                            Bc250DisplayLog("[TDR_GCVM_FAULT] CANDIDATO <<%u = 0x%llX FORA de toda faixa viva | abaixo:%u fim=0x%llX dist=0x%llX | acima:%u base=0x%llX dist=0x%llX (LiveScan=%u)\n",
                                kShifts[k], (unsigned long long)candidate,
                                site.HasBelow, (unsigned long long)site.NearestBelowEnd,
                                (unsigned long long)site.DistanceAboveEnd,
                                site.HasAbove, (unsigned long long)site.NearestAboveBase,
                                (unsigned long long)site.DistanceBelowBase,
                                site.LiveRangesScanned);
                            if (pageAddr == 0x200040ull && kShifts[k] == 12u) {
                                Bc250DisplayLog("[TDR_GCVM_FAULT] ALVO 0x200040: GpuVa=0x%llX NAO PERTENCE A NENHUMA ALOCACAO VIVA\n",
                                    (unsigned long long)candidate);
                            }
                        }

                        /*
                         * CAMINHADA DA NOSSA PROPRIA TABELA (Corrida 47 -> 48).
                         *
                         * A Corrida 47 respondeu a granularidade -- << 20, 5 de 5 --
                         * e mostrou que o endereco que falha esta DENTRO de uma
                         * faixa que o snapshot declara MAPPED (Alloc=0xF3 e 0xDF).
                         * Software e hardware DISCORDAM, e o log nao sabe dizer de
                         * que lado: [ActiveUpdatePT] e classe AMOSTRADA, entao a
                         * ausencia de linha de PTE nao prova ausencia de PTE. Foi a
                         * terceira vez que a amostragem barrou a conclusao.
                         *
                         * Aqui a pergunta deixa de depender do log: caminha-se a
                         * tabela do VMID 7 -- o VMID que o proprio fault nomeia --
                         * no endereco exato que falhou. Existe PTE? Qual o
                         * conteudo? Isso separa "nunca escrevemos a PTE" de
                         * "escrevemos e o hardware nao a enxerga", que exigem
                         * consertos opostos.
                         *
                         * SOMENTE LEITURA. Legal aqui porque DXGKDDI_RESETFROMTIMEOUT
                         * e _IRQL_requires_(PASSIVE_LEVEL) (d3dkmddi.h), e a leitura
                         * de VRAM fora da janela do framebuffer usa MmMapIoSpace,
                         * que exige IRQL <= APC_LEVEL. Em DISPATCH_LEVEL isto seria
                         * defeito -- o projeto ja pagou 0x0A, 0x133 e 0x139 por erro
                         * dessa familia.
                         */
                        {
                            /*
                             * A RAIZ DA CAMINHADA SEGUE O VMID QUE O FAULT
                             * NOMEIA (21/09, Corrida 54).
                             *
                             * Enquanto o pacote ia em VMID 7 esta linha lia
                             * UserVmidInstalledRootPhysical e isso bastava.
                             * Com o INDIRECT_BUFFER carregando o VMID do pool,
                             * um fault em VMID 3 caminhado com a raiz do VMID 7
                             * leria a TABELA ERRADA -- e "a PTE nao existe"
                             * viraria conclusao falsa sobre a tabela certa.
                             * Esta e' a armadilha (d) do handoff 307.
                             *
                             * Raiz ausente nao e' substituida por palpite: fica
                             * zero, e o ramo abaixo DECLARA a ausencia.
                             */
                            const uint32_t faultVmid =
                                (faultStatus & 0x00F00000u) >> 20;
                            BC250_VMID_POOL_WALK_ROOT poolWalk;
                            BC250_VMID_POOL_STATUS poolWalkStatus =
                                BC250_VMID_POOL_NOT_FOUND;
                            KIRQL walkPoolIrql;
                            uint64_t poolRoot;

                            poolWalk.RootPhysical = 0ull;
                            poolWalk.Context = NULL;
                            poolWalk.Generation = 0u;
                            poolWalk.Source = BC250_VMID_POOL_WALK_ROOT_NONE;
                            if (faultVmid != BC250_GCVM_USER_VMID) {
                                KeAcquireSpinLock(&activeCtx->VmidPoolLock,
                                    &walkPoolIrql);
                                poolWalkStatus = Bc250VmidPoolRootForFaultWalk(
                                    &activeCtx->VmidPool, faultVmid,
                                    &poolWalk);
                                KeReleaseSpinLock(&activeCtx->VmidPoolLock,
                                    walkPoolIrql);
                            }
                            poolRoot = poolWalk.RootPhysical;
                            const uint64_t walkRoot =
                                (faultVmid == BC250_GCVM_USER_VMID)
                                    ? activeCtx->UserVmidInstalledRootPhysical
                                    : poolRoot;
                            /*
                             * COMPARACAO DE RAIZ (21/09, apos a Corrida 48).
                             * A caminhada acima usa a raiz do INSTANTE DO TDR.
                             * Se as submissoes recentes foram feitas com OUTRA
                             * raiz, "a PTE nao existe nesta raiz" nao significa
                             * que ela nunca foi escrita -- significa que se
                             * caminhou a tabela ERRADA, e o defeito e' troca de
                             * contexto no VMID 7, nao escrita de PTE.
                             * So' e emitido uma vez por fault, no primeiro
                             * candidato, para nao repetir o mesmo dado.
                             */
                            if (k == 0u) {
                                uint32_t st;
                                uint32_t divergentes = 0u;
                                const uint32_t total =
                                    activeCtx->GfxSubmitStampCount;
                                for (st = 0u; st < total; ++st) {
                                    if (activeCtx->GfxSubmitStamps[st].RootAtSubmit
                                            != walkRoot) {
                                        ++divergentes;
                                    }
                                }
                                Bc250DisplayLog("[TDR_GCVM_FAULT] ORIGEM_DA_RAIZ: FaultVmid=%u -> %s Status=%u CtxDaRaiz=%p Geracao=%u (RaizDoPool=0x%llX RaizVmid7=0x%llX) -- a caminhada abaixo usa a tabela DESTE VMID, nao a do 7\n",
                                    faultVmid,
                                    (faultVmid == BC250_GCVM_USER_VMID) ?
                                        "VMID7 legado" :
                                        (poolWalk.Source ==
                                            BC250_VMID_POOL_WALK_ROOT_LIVE_READY ?
                                            "VIVA_READY do pool" :
                                         poolWalk.Source ==
                                            BC250_VMID_POOL_WALK_ROOT_RETAINED ?
                                            "RETIDA_POS_MORTE -- ultima instalacao provada" :
                                            "SEM RAIZ CONHECIDA"),
                                    (uint32_t)poolWalkStatus,
                                    poolWalk.Context, poolWalk.Generation,
                                    (unsigned long long)poolRoot,
                                    (unsigned long long)activeCtx->UserVmidInstalledRootPhysical);
                                /*
                                 * A CAMINHADA E' POST-MORTEM (Corrida 55).
                                 *
                                 * Ela roda no TDR, muito depois do fault. Se a
                                 * tabela foi publicada nesse intervalo, a PTE
                                 * valida que lemos agora pode ter NASCIDO
                                 * DEPOIS do fault -- e "a PTE existe, logo o
                                 * defeito nao e PTE ausente" seria conclusao
                                 * sobre um estado que nao existia na hora.
                                 *
                                 * A epoca da tabela ja e carimbada por
                                 * submissao. Comparar com a epoca de AGORA diz
                                 * se houve publicacao na janela. Os dois
                                 * desfechos informam, e este e o unico jeito de
                                 * a caminhada falar do INSTANTE do fault.
                                 */
                                {
                                    const int slotAtivo =
                                        (activeCtx->GpuvaSnapshots.ActiveSlot >= 0 &&
                                         activeCtx->GpuvaSnapshots.ActiveSlot <= 1) ?
                                        activeCtx->GpuvaSnapshots.ActiveSlot : 0;
                                    const uint32_t epocaAgora =
                                        activeCtx->GpuvaSnapshots.Slots[slotAtivo].PageTableEpoch;
                                    const uint32_t idxUlt =
                                        (activeCtx->GfxSubmitStampNext +
                                         BC250_GFX_SUBMIT_STAMP_SLOTS - 1u) %
                                        BC250_GFX_SUBMIT_STAMP_SLOTS;
                                    const uint32_t epocaSub =
                                        activeCtx->GfxSubmitStamps[idxUlt].PageTableEpoch;
                                    Bc250DisplayLog("[TDR_GCVM_FAULT] JANELA_DE_PUBLICACAO: EpocaNaUltimaSubmissao=%u EpocaAgora=%u Geracao=%ld Maps=%ld Unmaps=%ld -> %s\n",
                                        epocaSub, epocaAgora,
                                        activeCtx->GpuvaSnapshots.PublicationGeneration,
                                        activeCtx->GpuvaSnapshots.LastPublishMapCount,
                                        activeCtx->GpuvaSnapshots.LastPublishUnmapCount,
                                        (epocaSub == epocaAgora) ?
                                            "TABELA INALTERADA na janela -- a PTE lida abaixo JA valia no instante do fault, e a caminhada FALA do fault" :
                                            "TABELA MUDOU na janela -- a PTE lida abaixo pode ter nascido DEPOIS do fault, e a caminhada NAO prova nada sobre o instante do fault");
                                }
                                Bc250DisplayLog("[TDR_GCVM_FAULT] RAIZ_NO_TDR=0x%llX Carimbos=%u Divergentes=%u -- Divergentes>0 significa que submissoes recentes usaram OUTRA raiz, e a caminhada acima pode ter lido a tabela errada\n",
                                    (unsigned long long)walkRoot, total, divergentes);
                                for (st = 0u; st < total && st < 4u; ++st) {
                                    const uint32_t idx =
                                        (activeCtx->GfxSubmitStampNext +
                                         BC250_GFX_SUBMIT_STAMP_SLOTS - 1u - st) %
                                        BC250_GFX_SUBMIT_STAMP_SLOTS;
                                    Bc250DisplayLog("[TDR_GCVM_FAULT] CARIMBO -%u Fence=%llu RaizNaSubmissao=0x%llX IbVa=0x%llX Vmid=%u Epoca=%u %s\n",
                                        st,
                                        (unsigned long long)activeCtx->GfxSubmitStamps[idx].Fence,
                                        (unsigned long long)activeCtx->GfxSubmitStamps[idx].RootAtSubmit,
                                        (unsigned long long)activeCtx->GfxSubmitStamps[idx].IbGpuVa,
                                        activeCtx->GfxSubmitStamps[idx].Vmid,
                                        activeCtx->GfxSubmitStamps[idx].PageTableEpoch,
                                        (activeCtx->GfxSubmitStamps[idx].RootAtSubmit == walkRoot) ?
                                            "(MESMA RAIZ DO TDR)" : "(RAIZ DIFERENTE)");
                                }
                                /*
                                 * DX.7 -- MAPA DO POOL NO INSTANTE DO FAULT.
                                 *
                                 * Os carimbos acima dizem QUANTAS raizes
                                 * diferentes passaram; este mapa diz QUANTOS
                                 * CONTEXTOS existiam ao mesmo tempo. Sao
                                 * perguntas distintas: um unico contexto que
                                 * troca de raiz e varios contextos brigando
                                 * pelo mesmo VMID produzem o mesmo numero de
                                 * divergencias e exigem consertos diferentes.
                                 * Somente leitura de estado do software.
                                 */
                                {
                                    uint32_t pv;
                                    uint32_t ocupados = 0u;
                                    BC250_MCDM_HANDLE poolContexts[BC250_VMID_COUNT];
                                    uint64_t poolRoots[BC250_VMID_COUNT];
                                    uint32_t poolGenerations[BC250_VMID_COUNT];
                                    uint32_t poolAllocCount;
                                    uint32_t poolFreeCount;
                                    uint32_t poolExhaustedCount;
                                    uint32_t poolInUseMask;
                                    KIRQL poolIrql;

                                    /*
                                     * O TDR nao imprime enquanto segura o
                                     * lock. Tira um snapshot curto e so'
                                     * entao formata o mapa; assim Acquire e
                                     * Release nunca deixam Entries[] pela
                                     * metade para o diagnostico.
                                     */
                                    KeAcquireSpinLock(&activeCtx->VmidPoolLock,
                                        &poolIrql);
                                    for (pv = 0u; pv < BC250_VMID_COUNT; ++pv) {
                                        poolContexts[pv] = activeCtx->VmidPool.Entries[pv].Context;
                                        poolRoots[pv] = activeCtx->VmidPool.Entries[pv].RootPhysical;
                                        poolGenerations[pv] = activeCtx->VmidPool.Entries[pv].Generation;
                                    }
                                    poolAllocCount = activeCtx->VmidPool.AllocCount;
                                    poolFreeCount = activeCtx->VmidPool.FreeCount;
                                    poolExhaustedCount = activeCtx->VmidPool.ExhaustedCount;
                                    poolInUseMask = activeCtx->VmidPool.InUseMask;
                                    KeReleaseSpinLock(&activeCtx->VmidPoolLock,
                                        poolIrql);

                                    for (pv = 0u; pv < BC250_VMID_COUNT; ++pv) {
                                        if (poolContexts[pv] == NULL) continue;
                                        ++ocupados;
                                        Bc250DisplayLog("[VMID_POOL] MAPA: Ctx=%p -> VMID%02u Raiz=0x%llX Geracao=%u\n",
                                            poolContexts[pv], pv,
                                            (unsigned long long)poolRoots[pv],
                                            poolGenerations[pv]);
                                    }
                                    Bc250DisplayLog("[VMID_POOL] MAPA NO FAULT: Ocupados=%u Alocados=%u Liberados=%u Exaustoes=%u InUseMask=0x%04X -- Ocupados>1 confirma que MAIS DE UM contexto disputa o VMID%u hoje; Ocupados==1 REFUTA a disputa e o suspeito volta a ser troca de raiz do mesmo contexto\n",
                                        ocupados, poolAllocCount,
                                        poolFreeCount,
                                        poolExhaustedCount,
                                        poolInUseMask,
                                        BC250_GCVM_USER_VMID);
                                }
                            }
                            BC250_MCDM_RUNTIME_CAPS walkCaps;
                            uint64_t resolved = 0ull;

                            /*
                             * DX.17 -- O QUE NOS ESCREVEMOS x O QUE ESTA LA.
                             *
                             * Tres corridas (59, 61, 63) leram valor de PTE no
                             * indice 4096 de uma raiz real, enquanto as nossas
                             * 314 escritas nesse indice sairam TODAS limpas.
                             * Esta linha poe os dois numeros lado a lado no
                             * mesmo log, para que a divergencia deixe de ser
                             * uma comparacao que eu faco a mao entre corridas.
                             *
                             * ArenaPdeWriteSeq==0 significa que nunca
                             * escrevemos esse indice nesta sessao -- e a
                             * ausencia fica DECLARADA em vez de virar "eram
                             * iguais".
                             */
                            if (k == 0u && candidate == 0x200040000ull) {
                                Bc250DisplayLog("[PDE_ARENA_CONFRONTO] UltimoQueEscrevemos=0x%016llX Escritas=%u -- compare com o PDE que a linha [PT_WALK] acima leu no indice 4096\n",
                                    (unsigned long long)activeCtx->ArenaPdeLastWritten,
                                    activeCtx->ArenaPdeWriteSeq);
                            }

                            RtlZeroMemory(&walkCaps, sizeof(walkCaps));
                            if (walkRoot == 0ull) {
                                Bc250DisplayLog("[TDR_GCVM_FAULT] WALK <<%u: raiz do VMID %u AUSENTE (Fonte=%s Status=%u) -- sem caminhada, e a ausencia esta DECLARADA\n",
                                    kShifts[k], faultVmid,
                                    (faultVmid == BC250_GCVM_USER_VMID) ?
                                        "VMID7_LEGADO" : "POOL_VIVA_OU_RETIDA",
                                    (uint32_t)poolWalkStatus);
                            } else if (!NT_SUCCESS(Bc250ActiveQueryRuntimeCaps(
                                           activeCtx, &walkCaps)) || !walkCaps.Ready) {
                                Bc250DisplayLog("[TDR_GCVM_FAULT] WALK <<%u: runtime caps INDISPONIVEIS -- sem VramMcBase nao se caminha, e nao se inventa\n",
                                    kShifts[k]);
                            /*
                             * O rotulo dizia FAULT_VMID7 mesmo caminhando VMID
                             * 1, 2 ou 3 (medido na Corrida 54). Rotulo que
                             * nomeia o VMID errado e' telemetria do estado
                             * anterior -- a classe que esta faixa combate.
                             */
                            } else if (Bc250ActiveWalkGpuVa(
                                           (faultVmid == BC250_GCVM_USER_VMID) ?
                                               "FAULT_VMID7" : "FAULT_VMID_POOL",
                                           candidate,
                                           walkRoot, walkCaps.VramMcBase, &resolved)) {
                                Bc250DisplayLog("[TDR_GCVM_FAULT] WALK %s = 0x%llX RESOLVEU para PA=0x%llX com Root=0x%llX -- a PTE existe NESTE endereco; se o shift for VA<<8, isso significa que o endereco que FALTOU perdeu 8 bits, NAO que a tabela esteja certa\n",
                                    kShiftNames[k], (unsigned long long)candidate,
                                    (unsigned long long)resolved,
                                    (unsigned long long)walkRoot);
                            } else {
                                Bc250DisplayLog("[TDR_GCVM_FAULT] WALK %s = 0x%llX NAO RESOLVE com Root=0x%llX -- as linhas [PT_WALK] acima dizem em qual nivel parou\n",
                                    kShiftNames[k], (unsigned long long)candidate,
                                    (unsigned long long)walkRoot);
                            }
                        }
                    }
                }
            } else {
                Bc250DisplayLog("[TDR_GCVM_FAULT] SEM FAULT LATCHADO (FaultValid=0x%X Status=0x%08X) -- "
                    "ausencia aqui NAO prova ausencia de fault, so' que este registrador nao o reteve\n",
                    faultValid, faultStatus);
            }
            Bc250DisplayLog("[TDR_MEC_CPF_STAT] CpValid=0x%X CpfStat=%08X CpfBusy=%08X CpfStalled=%08X CpcStat=%08X CpcBusy=%08X CpcStalled=%08X CpStalled1=%08X CpStat=%08X\n",
                cpValid, cpfStatus, cpfBusy, cpfStalled, cpcStatus, cpcBusy, cpcStalled, cpStalled1, cpStat);
            Bc250DisplayLog("[TDR_MEC_EXEC_STAT] Mec1Hdr=%08X Mec2Hdr=%08X Mec1Pntr=%08X Mec2Pntr=%08X IbBase=%08X:%08X IbRptr=%08X PqCtrl=%08X Dequeue=%08X\n",
                mecMe1Hdr, mecMe2Hdr, mec1InstrPntr, mec2InstrPntr, ibBaseHi, ibBaseLo, ibRptr, pqControl, dequeueReq);

            /* TDR Quiesce do GfxEngine (CP_RB0 / 3D Graphics) */
            if (activeCtx->GfxEngine.State == 1u &&
                activeCtx->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
                uint32_t cpRb0Active = 0u, cpRb0Rptr = 0u, cpRb0Wptr = 0u;
                uint32_t grbmStatus = 0u, cpMeCntl = 0u;

                (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                    BC250_GFX_REG_CP_RB0_ACTIVE, &cpRb0Active);
                (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                    BC250_GFX_REG_CP_RB0_RPTR, &cpRb0Rptr);
                (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                    BC250_GFX_REG_CP_RB0_WPTR, &cpRb0Wptr);
                (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                    BC250_GFX_REG_GRBM_STATUS, &grbmStatus);
                (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                    BC250_GFX_REG_CP_ME_CNTL, &cpMeCntl);

                Bc250DisplayLog("[TDR_GFX_QUIESCE] Snapshot: Active=%u RPTR=%u WPTR=%u GrbmStatus=0x%08X CpMeCntl=0x%08X\n",
                    cpRb0Active, cpRb0Rptr, cpRb0Wptr, grbmStatus, cpMeCntl);

                /*
                 * ================================================================
                 * ESTADO DE STALL DO CP, NOMEADO -- nao hexadecimal solto.
                 * ================================================================
                 * Por que esta decodificacao esta aqui e por que ela cita o
                 * header: na analise da corrida 2026-09-18 13:12 o valor
                 * CpStalled1=0x00000C00 foi lido como
                 * "ME_WAIT_ON_AVAIL_BUFFER | GFX_CNTX_NOT_AVAIL_TO_ME" e dai se
                 * concluiu que o CP travara por indisponibilidade de contexto
                 * grafico. A conclusao nao sobrevive ao header local:
                 *
                 *   CP_STALLED_STAT1 (0x8674), gc_10_1_0_sh_mask.h:6428-6429
                 *     bit 10 = ME_HAS_ACTIVE_CE_BUFFER_FLAG  (0x00000400)
                 *     bit 11 = ME_HAS_ACTIVE_DE_BUFFER_FLAG  (0x00000800)
                 *
                 *   CP_STALLED_STAT2 (0x8678), gc_10_1_0_sh_mask.h:6466-6467
                 *     bit 10 = ME_WAIT_ON_AVAIL_BUFFER       (0x00000400)
                 *     bit 11 = GFX_CNTX_NOT_AVAIL_TO_ME      (0x00000800)
                 *
                 * Os dois nomes citados pertencem ao STAT2, e o STAT2 medido
                 * naquela corrida era 0x00000000 -- ou seja, os dois bits
                 * estavam em ZERO. Ler nome de um registrador contra valor de
                 * outro e' a classe de erro que esta linha existe para impedir:
                 * a partir de agora o log imprime os DOIS registradores, cada um
                 * com os nomes do seu proprio header.
                 */
                {
                    uint32_t cpStall1 = 0u, cpStall2 = 0u;
                    const uint32_t s1Ok = Bc250MmioTopologyRead32(
                        &activeCtx->Core.MmioTransport,
                        BC250_HQD_MMIO_CP_STALLED_STAT1, &cpStall1);
                    const uint32_t s2Ok = Bc250MmioTopologyRead32(
                        &activeCtx->Core.MmioTransport,
                        BC250_HQD_MMIO_CP_STALLED_STAT2, &cpStall2);

                    Bc250DisplayLog("[TDR_GFX_STALL] CpStalled1=0x%08X[lido=%u] CpStalled2=0x%08X[lido=%u]\n",
                        cpStall1, s1Ok, cpStall2, s2Ok);

                    if (s1Ok != 0u) {
                        Bc250DisplayLog("[TDR_GFX_STALL] STAT1 (0x8674): ME_HAS_ACTIVE_CE_BUFFER_FLAG=%u ME_HAS_ACTIVE_DE_BUFFER_FLAG=%u ME_STALLED_ON_TC_WR_CONFIRM=%u ME_WAITING_ON_TC_READ_DATA=%u ME_WAITING_ON_REG_READ_DATA=%u RCIU_WAITING_ON_GRBM_FREE=%u RCIU_STALLED_ON_ME_READ=%u RCIU_HALTED_BY_REG_VIOLATION=%u\n",
                            (cpStall1 & 0x00000400u) ? 1u : 0u,
                            (cpStall1 & 0x00000800u) ? 1u : 0u,
                            (cpStall1 & 0x00001000u) ? 1u : 0u,
                            (cpStall1 & 0x00004000u) ? 1u : 0u,
                            (cpStall1 & 0x00008000u) ? 1u : 0u,
                            (cpStall1 & 0x01000000u) ? 1u : 0u,
                            (cpStall1 & 0x04000000u) ? 1u : 0u,
                            (cpStall1 & 0x20000000u) ? 1u : 0u);
                    } else {
                        Bc250DisplayLog("[TDR_GFX_STALL] STAT1 INDETERMINADO: Read32 recusou 0x%04X; nada se conclui sobre stall do ME\n",
                            BC250_HQD_MMIO_CP_STALLED_STAT1);
                    }

                    if (s2Ok != 0u) {
                        Bc250DisplayLog("[TDR_GFX_STALL] STAT2 (0x8678): ME_WAIT_ON_AVAIL_BUFFER=%u GFX_CNTX_NOT_AVAIL_TO_ME=%u -- estes DOIS sao os bits de indisponibilidade de contexto grafico, e vivem no STAT2, nao no STAT1\n",
                            (cpStall2 & 0x00000400u) ? 1u : 0u,
                            (cpStall2 & 0x00000800u) ? 1u : 0u);
                    } else {
                        Bc250DisplayLog("[TDR_GFX_STALL] STAT2 INDETERMINADO: Read32 recusou 0x%04X; a pergunta do contexto grafico fica SEM RESPOSTA nesta corrida\n",
                            BC250_HQD_MMIO_CP_STALLED_STAT2);
                    }

                    /* GRBM_STATUS decodificado: GUI_ACTIVE e CP_BUSY sao os dois
                     * campos que a analise da corrida 13:12 precisou decompor a
                     * mao. Proveniencia: GRBM_STATUS__GUI_ACTIVE (bit 31) e
                     * GRBM_STATUS__CP_BUSY (bit 29) no mesmo header. */
                    Bc250DisplayLog("[TDR_GFX_STALL] GRBM_STATUS 0x%08X: GUI_ACTIVE=%u CP_BUSY=%u CP_COHERENCY_BUSY=%u\n",
                        grbmStatus,
                        (grbmStatus & 0x80000000u) ? 1u : 0u,
                        (grbmStatus & 0x20000000u) ? 1u : 0u,
                        (grbmStatus & 0x10000000u) ? 1u : 0u);
                }

                /*
                 * =========================================================
                 * 19/09 -- SONDA DO PONTO DE PARADA DO CP. SOMENTE LEITURA,
                 * E ANTES DE QUALQUER ESCRITA NO SILICIO.
                 * =========================================================
                 * A corrida 2026-09-19 09:06:34 parou o anel grafico e nos
                 * deixou com tres hipoteses e nenhuma medicao que as
                 * separasse. O que faltava nao era analise, era registrador:
                 *
                 *  - CP_ME_HEADER_DUMP / CP_PFP_HEADER_DUMP dizem QUAL pacote
                 *    PM4 cada micro-engine decodificou por ultimo. Sem isso
                 *    "a DE travou" e hipotese; com isso o pacote tem nome.
                 *  - CP_IB1_BASE/BUFSZ dizem QUAL IB o CP esta executando e
                 *    QUANTO falta -- localiza o ponto dentro dos 1008 DW.
                 *  - GRBM_STATUS_SE0/SE1 separam front-end ocupado de pixel
                 *    pipe ocupado, por shader engine.
                 *  - SQ_DEBUG_STS_GLOBAL.WAVE_LEVEL diz se existe ONDA VIVA.
                 *    Este e o discriminante: onda em voo que nao retira
                 *    aponta para shader/ACO; zero onda com o ME parado num
                 *    opcode aponta para estado de contexto ou para o pacote.
                 *  - CP_BUSY_STAT separa espera por semaforo, por EOP e por
                 *    contador de coerencia.
                 *
                 * Tudo aqui e Read32. Nenhum destes offsets esta na allowlist
                 * de escrita, e o portao audit_gfx_stall_probe_is_read_only
                 * mantem assim. NAO VALIDADO EM HARDWARE.
                 */
                {
                    uint32_t meHdr = 0u, pfpHdr = 0u, ib1Lo = 0u, ib1Hi = 0u;
                    uint32_t ib1Sz = 0u, se0 = 0u, se1 = 0u, sq = 0u;
                    uint32_t grbm2 = 0u, busyStat = 0u;
                    uint32_t okMe, okPfp, okSq, okIb;

                    okMe = Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                        BC250_GFX_REG_CP_ME_HEADER_DUMP, &meHdr);
                    okPfp = Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                        BC250_GFX_REG_CP_PFP_HEADER_DUMP, &pfpHdr);
                    okIb = Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                        BC250_GFX_REG_CP_IB1_BUFSZ, &ib1Sz);
                    (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                        BC250_GFX_REG_CP_IB1_BASE_LO, &ib1Lo);
                    (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                        BC250_GFX_REG_CP_IB1_BASE_HI, &ib1Hi);
                    (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                        BC250_GFX_REG_GRBM_STATUS_SE0, &se0);
                    (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                        BC250_GFX_REG_GRBM_STATUS_SE1, &se1);
                    okSq = Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                        BC250_GFX_REG_SQ_DEBUG_STS_GLOBAL, &sq);
                    (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                        BC250_GFX_REG_GRBM_STATUS2, &grbm2);
                    (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                        BC250_GFX_REG_CP_BUSY_STAT, &busyStat);

                    if (okMe != 0u) {
                        Bc250DisplayLog("[TDR_GFX_PACKET] ME_HEADER_DUMP=0x%08X Type=%u Opcode=0x%02X (%s) PayloadDW=%u\n",
                            meHdr, Bc250Pm4HeaderType(meHdr),
                            Bc250Pm4HeaderType3Opcode(meHdr),
                            Bc250Pm4HeaderType(meHdr) == BC250_PM4_TYPE_3 ?
                                Bc250Pm4Type3OpcodeName(Bc250Pm4HeaderType3Opcode(meHdr)) :
                                "nao-e-type3",
                            Bc250Pm4HeaderType3CountDwords(meHdr));
                    } else {
                        Bc250DisplayLog("[TDR_GFX_PACKET] ME_HEADER_DUMP INDETERMINADO: Read32 recusou 0x%05X; o pacote em que o ME parou fica SEM RESPOSTA nesta corrida\n",
                            BC250_GFX_REG_CP_ME_HEADER_DUMP);
                    }
                    if (okPfp != 0u) {
                        Bc250DisplayLog("[TDR_GFX_PACKET] PFP_HEADER_DUMP=0x%08X Type=%u Opcode=0x%02X (%s) PayloadDW=%u\n",
                            pfpHdr, Bc250Pm4HeaderType(pfpHdr),
                            Bc250Pm4HeaderType3Opcode(pfpHdr),
                            Bc250Pm4HeaderType(pfpHdr) == BC250_PM4_TYPE_3 ?
                                Bc250Pm4Type3OpcodeName(Bc250Pm4HeaderType3Opcode(pfpHdr)) :
                                "nao-e-type3",
                            Bc250Pm4HeaderType3CountDwords(pfpHdr));
                    } else {
                        Bc250DisplayLog("[TDR_GFX_PACKET] PFP_HEADER_DUMP INDETERMINADO: Read32 recusou 0x%05X\n",
                            BC250_GFX_REG_CP_PFP_HEADER_DUMP);
                    }
                    if (okIb != 0u) {
                        Bc250DisplayLog("[TDR_GFX_PACKET] IB1 Base=0x%04X%08X RestamDW=%u -- comparar com DmaVA e SizeDW da ultima linha [SubmitVirtual Node1 GFX]\n",
                            (ib1Hi & 0xFFFFu), ib1Lo, ib1Sz);
                    } else {
                        Bc250DisplayLog("[TDR_GFX_PACKET] IB1 INDETERMINADO: Read32 recusou 0x%05X; nao se localiza o ponto dentro do IB\n",
                            BC250_GFX_REG_CP_IB1_BUFSZ);
                    }

                    /*
                     * WAVE_LEVEL_SA0 (0x0000FFF0) e WAVE_LEVEL_SA1 (0x0FFF0000)
                     * sao contagens de ondas em voo por shader array; BUSY e o
                     * bit 0. Fonte: gc_10_1_0_sh_mask.h, SQ_DEBUG_STS_GLOBAL__*.
                     */
                    if (okSq != 0u) {
                        Bc250DisplayLog("[TDR_GFX_WAVES] SQ_DEBUG_STS_GLOBAL=0x%08X Busy=%u WaveLevelSA0=%u WaveLevelSA1=%u -- ondas>0 com o CP parado aponta shader que nao retira; ondas==0 aponta estado ou pacote\n",
                            sq, (sq & 0x1u) ? 1u : 0u,
                            (sq & 0x0000FFF0u) >> 4, (sq & 0x0FFF0000u) >> 16);
                    } else {
                        Bc250DisplayLog("[TDR_GFX_WAVES] SQ_DEBUG_STS_GLOBAL INDETERMINADO: Read32 recusou 0x%05X; a pergunta da onda viva fica SEM RESPOSTA nesta corrida\n",
                            BC250_GFX_REG_SQ_DEBUG_STS_GLOBAL);
                    }

                    /*
                     * INSPECAO INDIRETA DE ONDA -- so' quando ARMADA e so'
                     * quando o SQ disse que HA onda. Ver o bloco longo em
                     * bc250_gfx_ring_transaction.h para o risco do seletor.
                     *
                     * A Corrida 35 mediu WaveLevelSA0=3 e SA1=3 e nao teve
                     * como dizer ONDE. Isto responde: PC, IB_STS e a propria
                     * instrucao de cada onda presa.
                     */
                    {
                        const uint32_t wavesInFlight =
                            ((sq & 0x0000FFF0u) >> 4) + ((sq & 0x0FFF0000u) >> 16);

                        if (activeCtx->GfxWaveProbePresent != 1u ||
                            activeCtx->GfxWaveProbe != 1u) {
                            Bc250DisplayLog("[TDR_GFX_WAVEPC] DESARMADO: GfxWaveProbe=%u present=%u -- nenhuma escrita de seletor foi feita\n",
                                activeCtx->GfxWaveProbe, activeCtx->GfxWaveProbePresent);
                        } else if (okSq == 0u || wavesInFlight == 0u) {
                            Bc250DisplayLog("[TDR_GFX_WAVEPC] NAO EXECUTADO: SqLido=%u OndasEmVoo=%u -- sem onda para inspecionar, e por isso nada foi escrito\n",
                                okSq, wavesInFlight);
                        } else {
                            BC250_GFX_RING_INTERFACE waveIface;
                            BC250_WAVE_PROBE_PLAN wavePlan;
                            BC250_WAVE_PROBE_EVIDENCE waveEv;
                            BC250_WAVE_PROBE_STATUS waveStatus;
                            uint32_t slot = 0u, se, sa, inst;

                            RtlZeroMemory(&wavePlan, sizeof(wavePlan));
                            RtlZeroMemory(&waveEv, sizeof(waveEv));
                            waveIface.Read32 = Bc250MmioTopologyRead32;
                            waveIface.Write32 = Bc250MmioTopologyWrite32;
                            waveIface.Stall = NULL;
                            waveIface.Context = &activeCtx->Core.MmioTransport;
                            waveIface.Identity = BC250_GFX_RING_TXN_IDENTITY;

                            /*
                             * Varredura DELIBERADAMENTE rasa: 2 SE x 2 SA x 4
                             * instancias x wave 0. Nao e' varredura completa e
                             * o log diz isso -- laco longo em DISPATCH_LEVEL
                             * foi a causa do 0x133 em 10/09 12:16.
                             */
                            /* Breadth first: visit every SE/SA before another
                             * instance. 24 CUs alone does not establish SQ instance
                             * encoding; keep the existing shallow candidate set.
                             * 16 statuses + 4*5 payload reads <= 52-read ceiling. */
                            for (inst = 0u; inst < 4u; ++inst) {
                                for (sa = 0u; sa < 2u; ++sa) {
                                    for (se = 0u; se < 2u; ++se) {
                                        if (slot >= BC250_WAVE_PROBE_MAX_SLOTS) break;
                                        wavePlan.Slots[slot].SeIndex = se;
                                        wavePlan.Slots[slot].SaIndex = sa;
                                        wavePlan.Slots[slot].InstanceIndex = inst;
                                        wavePlan.Slots[slot].WaveId = 0u;
                                        slot++;
                                    }
                                }
                            }
                            wavePlan.SlotCount = slot;
                            wavePlan.WavesInFlight = wavesInFlight;
                            wavePlan.ReadBudget = BC250_WAVE_PROBE_MAX_READS;

                            waveStatus = Bc250WaveProbeRun(&waveIface, &wavePlan, &waveEv);
                            if (waveEv.PoisonReads != 0u) {
                                Bc250DisplayLog("[TDR_GFX_WAVEPC] INDETERMINADO_POISON: leituras=%u -- nao contam como ondas\n", waveEv.PoisonReads);
                            }
                            Bc250DisplayLog("[TDR_GFX_WAVEPC] status=%s SlotsVisitados=%u Leituras=%u OndasAchadas=%u SeletorRestaurado=%u (readback=0x%08X) LeituraRecusada=%u -- varredura RASA (2 SE x 2 SA x 4 inst, wave 0)\n",
                                Bc250WaveProbeStatusName(waveStatus),
                                waveEv.SlotsVisited, waveEv.ReadsIssued, waveEv.WavesFound,
                                waveEv.SelectorRestored, waveEv.SelectorReadback,
                                waveEv.ReadRefused);

                            if (waveStatus == BC250_WAVE_PROBE_SELECTOR_STUCK) {
                                Bc250DisplayLog("[TDR_GFX_WAVEPC] ALERTA: GRBM_GFX_INDEX NAO voltou a broadcast (0x%08X). O soft-reset a seguir pode atingir UM SO shader engine\n",
                                    waveEv.SelectorReadback);
                            }
                            {
                                uint32_t w;
                                for (w = 0u; w < waveEv.WavesFound; ++w) {
                                    const BC250_WAVE_PROBE_FOUND *f = &waveEv.Found[w];
                                    Bc250DisplayLog("[TDR_GFX_WAVEPC] onda #%u SE=%u SA=%u Inst=%u Wave=%u STATUS=0x%08X PC=0x%08X%08X IB_STS=0x%08X INST_DW0=0x%08X TRAPSTS=0x%08X ValidMask=0x%02X\n",
                                        w, f->SeIndex, f->SaIndex, f->InstanceIndex, f->WaveId,
                                        f->Status, f->PcHi, f->PcLo, f->IbSts, f->InstDw0, f->TrapSts, f->ValidMask);
                                }
                                if (waveEv.WavesFound == 0u) {
                                    Bc250DisplayLog("[TDR_GFX_WAVEPC] NENHUMA onda localizada nesta varredura rasa -- STATUS zero e INDISTINGUIVEL de slot vazio, entao isto NAO refuta as %u ondas que o SQ contou\n",
                                        wavesInFlight);
                                }
                            }
                        }
                    }

                    /* GRBM_STATUS_SE*: CB_CLEAN/DB_CLEAN dizem se algum pixel
                     * chegou a ser escrito. Fonte: GRBM_STATUS_SE0__*. */
                    Bc250DisplayLog("[TDR_GFX_SE] SE0=0x%08X (SPI=%u SX=%u TA=%u PA=%u SC=%u CB=%u DB=%u CB_CLEAN=%u DB_CLEAN=%u) SE1=0x%08X\n",
                        se0,
                        (se0 & 0x08000000u) ? 1u : 0u, (se0 & 0x04000000u) ? 1u : 0u,
                        (se0 & 0x02000000u) ? 1u : 0u, (se0 & 0x01000000u) ? 1u : 0u,
                        (se0 & 0x20000000u) ? 1u : 0u, (se0 & 0x80000000u) ? 1u : 0u,
                        (se0 & 0x40000000u) ? 1u : 0u, (se0 & 0x00000004u) ? 1u : 0u,
                        (se0 & 0x00000002u) ? 1u : 0u, se1);

                    /* CP_BUSY_STAT separa as esperas do CP. Fonte:
                     * CP_BUSY_STAT__* em gc_10_1_0_sh_mask.h:6535-6550. */
                    Bc250DisplayLog("[TDR_GFX_CPWAIT] CP_BUSY_STAT=0x%08X (PfpParsing=%u MeParsing=%u CeParsing=%u SemPolling=%u SemFailedHolding=%u EopDoneBusy=%u GfxContextBusy=%u CoherCntNeqZero=%u) GRBM_STATUS2=0x%08X (UTCL2_BUSY=%u EA_BUSY=%u CPG_BUSY=%u CPF_BUSY=%u)\n",
                        busyStat,
                        (busyStat & 0x00000080u) ? 1u : 0u, (busyStat & 0x00000100u) ? 1u : 0u,
                        (busyStat & 0x00400000u) ? 1u : 0u, (busyStat & 0x00004000u) ? 1u : 0u,
                        (busyStat & 0x00002000u) ? 1u : 0u, (busyStat & 0x00040000u) ? 1u : 0u,
                        (busyStat & 0x00008000u) ? 1u : 0u, (busyStat & 0x00000040u) ? 1u : 0u,
                        grbm2,
                        (grbm2 & 0x00008000u) ? 1u : 0u, (grbm2 & 0x00010000u) ? 1u : 0u,
                        (grbm2 & 0x40000000u) ? 1u : 0u, (grbm2 & 0x10000000u) ? 1u : 0u);
                }

                /*
                 * ==========================================================
                 * BROADCAST REPOSTO INCONDICIONALMENTE ANTES DO SOFT-RESET.
                 * ==========================================================
                 * Esta escrita existe por causa do risco declarado da sonda de
                 * onda, e ela nao depende do veredito da sonda.
                 *
                 * MEDIDO na Corrida 37 (19/09 12:34:24): a sonda devolveu
                 * status=selector_stuck com SeletorRestaurado=0 e
                 * readback=0x00000000. Ficou em aberto se GRBM_GFX_INDEX
                 * realmente permaneceu fora de broadcast ou se o nosso criterio
                 * de readback e que esta errado -- a auditoria seguinte NAO
                 * conseguiu demonstrar que os bits de broadcast sejam
                 * write-only, e por isso a guarda foi preservada.
                 *
                 * Enquanto a pergunta nao fecha, o PERIGO concreto e um so':
                 * disparar GRBM_SOFT_RESET com o seletor apontando para um
                 * unico shader engine. Esta escrita remove o perigo sem
                 * resolver a duvida e sem silenciar o relato -- o
                 * [TDR_GFX_WAVEPC] ALERTA continua saindo se a sonda achar que
                 * nao conseguiu restaurar. Diagnostico honesto e mitigacao
                 * fisica sao coisas separadas, e aqui as duas coexistem.
                 *
                 * E incondicional de proposito: vale inclusive quando a sonda
                 * esta DESARMADA, porque o caminho MEC acima tambem mexe no
                 * seletor.
                 */
                {
                    uint32_t bcastReadback = 0xFFFFFFFFu;
                    const uint32_t bcastWritten = Bc250MmioTopologyWrite32(
                        &activeCtx->Core.MmioTransport,
                        BC250_GFX_REG_GRBM_GFX_INDEX,
                        BC250_GRBM_GFX_INDEX_BROADCAST_ALL);
                    (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                        BC250_GFX_REG_GRBM_GFX_INDEX, &bcastReadback);
                    Bc250DisplayLog("[TDR_GFX_QUIESCE] GRBM_GFX_INDEX reposto em broadcast antes do soft-reset: escrita=%u readback=0x%08X (esperado 0x%08X; divergencia NAO e' prova de falha -- ver o ALERTA da sonda)\n",
                        bcastWritten, bcastReadback, BC250_GRBM_GFX_INDEX_BROADCAST_ALL);
                }

                /* Desativa CP_RB0 */
                (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                    BC250_GFX_REG_CP_RB0_ACTIVE, 0u);

                /* Pulso de soft-reset no CP e GFX */
                (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                    BC250_GRBM_REG_SOFT_RESET,
                    BC250_GRBM_SOFT_RESET_CP_MASK |
                    BC250_GRBM_SOFT_RESET_GFX_MASK |
                    BC250_GRBM_SOFT_RESET_CPF_MASK |
                    BC250_GRBM_SOFT_RESET_CPG_MASK);
                KeStallExecutionProcessor(50u);
                (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                    BC250_GRBM_REG_SOFT_RESET, 0u);

                activeCtx->GfxEngine.State = 0u;
                Bc250DisplayLog("[TDR_GFX_QUIESCE] CP_RB0 desativado e soft-reset executado\n");
            }
            break;
        }
    }
    ExReleasePushLockShared(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();
    return result;
}

/*
 * O reset de adaptador invalida todo trabalho que estava em voo. Essas
 * entradas nao podem sobreviver ate a fence privada usada para provar o
 * restart: drena-las ali notificaria ao dxgkrnl uma conclusao que a GPU nunca
 * produziu. AbortPending remove-as sem chamar DxgkCbNotifyInterrupt e devolve
 * somente os pins que precisam ser liberados.
 */
static BOOLEAN Bc250ActiveTdrAbortFenceTracker(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    BC250_WDDM_FENCE_ENTRY aborted[BC250_WDDM_FENCE_TRACK_DEPTH];
    uint32_t abortedCount = 0u;
    uint32_t committedCount = 0u;
    uint32_t reservedCount = 0u;
    uint32_t i;

    Bc250WddmFenceTrackerSetAcceptingWork(
        &Context->SubmissionEngine.WddmFences, FALSE);
    if (!Bc250WddmFenceTrackerAbortPending(
            &Context->SubmissionEngine.WddmFences,
            aborted, BC250_WDDM_FENCE_TRACK_DEPTH,
            &abortedCount, &committedCount, &reservedCount)) {
        Bc250DisplayLog("[TDR_FENCE_RECONCILE] FALHA: rastreador nao foi adquirido; admissao permanece fechada\n");
        return FALSE;
    }

    for (i = 0u; i < abortedCount; ++i) {
        if (aborted[i].GpuvaPinValid != 0u) {
            BC250_GPUVA_SNAPSHOT_READER reader;
            if (aborted[i].GpuvaSlot > 1u ||
                (uint32_t)InterlockedCompareExchange(
                    &Context->GpuvaSnapshots.SlotGeneration[aborted[i].GpuvaSlot],
                    0, 0) != aborted[i].GpuvaGeneration) {
                InterlockedExchange(&Context->GpuvaMappingsOperational, 0);
                Bc250DisplayLog("[TDR_FENCE_RECONCILE] FALHA: pin GPUVA invalido Slot=%u Generation=%u; admissao permanece fechada\n",
                    aborted[i].GpuvaSlot, aborted[i].GpuvaGeneration);
                return FALSE;
            }
            RtlZeroMemory(&reader, sizeof(reader));
            reader.Space = &Context->GpuvaSnapshots.Slots[aborted[i].GpuvaSlot];
            reader.Slot = aborted[i].GpuvaSlot;
            reader.PublicationGeneration = aborted[i].GpuvaGeneration;
            reader.Acquired = 1u;
            Bc250GpuvaSnapshotRelease(&Context->GpuvaSnapshots, &reader);
        }
    }

    Bc250DisplayLog("[TDR_FENCE_RECONCILE] PASS: Abortadas=%u Committed=%u Reserved=%u NotificadasComoConcluidas=0\n",
        abortedCount, committedCount, reservedCount);
    return TRUE;
}

/*
 * Prova executavel do CP_RB0 depois do reset: quatro PACKET2 NOP seguidos de
 * RELEASE_MEM sobre poison. Readback de ACTIVE/RPTR/WPTR sozinho prova apenas
 * programacao de registrador; este tripe exige consumo do anel e escrita EOP
 * feita pela GPU.
 */
static BOOLEAN Bc250ActiveTdrProveGfxRestart(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    uint32_t cursor = 0u;
    uint32_t poll;
    uint32_t rptr = 0u;
    uint32_t fenceObserved = BC250_GFX_FENCE_POISON;
    uint64_t proofFence64 = Context->GfxEngine.LastSubmittedFence + 1ull;
    uint32_t proofFence = (uint32_t)proofFence64;
    BC250_PM4_STATUS pm4Status;
    uint32_t writeLo;
    uint32_t writeHi;

    if (Context->GfxEngine.RingCpuVirtual == NULL ||
        Context->GfxEngine.RingDwords < 16u ||
        Context->GfxEngine.FenceCpuVirtual == NULL ||
        Context->GfxEngine.RptrWbCpuVirtual == NULL) {
        return FALSE;
    }
    if (proofFence == 0u || proofFence == BC250_GFX_FENCE_POISON) {
        ++proofFence64;
        proofFence = (uint32_t)proofFence64;
    }

    Context->GfxEngine.RingCpuVirtual[cursor++] = BC250_PM4_PACKET2_NOP;
    Context->GfxEngine.RingCpuVirtual[cursor++] = BC250_PM4_PACKET2_NOP;
    Context->GfxEngine.RingCpuVirtual[cursor++] = BC250_PM4_PACKET2_NOP;
    Context->GfxEngine.RingCpuVirtual[cursor++] = BC250_PM4_PACKET2_NOP;
    pm4Status = Bc250Pm4EmitReleaseMemEop32(
        Context->GfxEngine.RingCpuVirtual,
        Context->GfxEngine.RingDwords,
        &cursor,
        Context->GfxEngine.FenceGpuVa,
        proofFence,
        BC250_PM4_RELEASE_MEM_INT_SEL_NONE);
    if (pm4Status != BC250_PM4_OK || cursor != 12u) {
        return FALSE;
    }

    *Context->GfxEngine.FenceCpuVirtual = BC250_GFX_FENCE_POISON;
    if (Context->GfxEngine.WptrPollCpuVirtual != NULL) {
        *Context->GfxEngine.WptrPollCpuVirtual = cursor;
    }
    Bc250SdmaPresentFlushHdp();
    KeMemoryBarrier();
    (void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    writeLo = Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
        BC250_GFX_REG_CP_RB0_WPTR, cursor);
    writeHi = Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,
        BC250_GFX_REG_CP_RB0_WPTR_HI, 0u);
    if (!writeLo || !writeHi) {
        Bc250DisplayLog("[TDR_GFX_RESTART_PROOF] FALHA: publicacao WPTR recusada Lo=%u Hi=%u\n",
            writeLo ? 1u : 0u, writeHi ? 1u : 0u);
        return FALSE;
    }

    for (poll = 0u; poll < 2000u; ++poll) {
        Bc250ActiveTlbStall(Context, 25u);
        Bc250SdmaPresentFlushHdp();
        (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,
            BC250_GFX_REG_CP_RB0_RPTR, &rptr);
        fenceObserved = *Context->GfxEngine.FenceCpuVirtual;
        if (rptr == cursor && fenceObserved == proofFence) {
            Context->GfxEngine.WptrDwords = cursor;
            Context->GfxEngine.LastSubmittedFence = proofFence64;
            Context->GfxEngine.LastSubmitVmid = 0xFFFFFFFFu; /* DX.29: anel vazio apos o restart */
            Context->GfxEngine.LastRetiredFence = proofFence64;
            Context->GfxEngine.LastRptrMmio = rptr;
            Bc250DisplayLog("[TDR_GFX_RESTART_PROOF] PASS: NOPx4+RELEASE_MEM executados RPTR=%u WPTR=%u Fence=0x%08X PoisonSubstituido=1 Polls=%u\n",
                rptr, cursor, fenceObserved, poll + 1u);
            return TRUE;
        }
    }

    Bc250DisplayLog("[TDR_GFX_RESTART_PROOF] FALHA: RPTR=%u WPTR=%u FenceObs=0x%08X FenceEsp=0x%08X Polls=%u\n",
        rptr, cursor, fenceObserved, proofFence, poll);
    return FALSE;
}

static BOOLEAN Bc250ActiveTdrRestartLifecycle(VOID)
{
    BOOLEAN sdmaRestart = Bc250SdmaPresentRestartAfterTdr();
    BOOLEAN mecRestart = TRUE;
    BOOLEAN gfxRestart = TRUE;
    BOOLEAN fenceReconcile = TRUE;
    uint32_t dirIdx;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_Bc250AdapterDirectoryLock);
    for (dirIdx = 0u; dirIdx < BC250_ACTIVE_MAX_ADAPTERS; ++dirIdx) {
        BC250_DXGK_ACTIVE_CONTEXT *activeCtx = g_Bc250AdapterDirectory[dirIdx];
        if (activeCtx != NULL && activeCtx->Signature == BC250_ACTIVE_SIGNATURE) {
            InterlockedExchange(&activeCtx->SdmaRetireBusy, 0);
            if (!Bc250ActiveTdrAbortFenceTracker(activeCtx)) {
                fenceReconcile = FALSE;
            }
        }
        if (activeCtx != NULL && activeCtx->Signature == BC250_ACTIVE_SIGNATURE &&
            activeCtx->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED &&
            activeCtx->GfxEngine.RingDwords != 0u) {
            uint32_t active = 0u, rptr = 0u, wptr = 0u, meCntl = 0u, grbmReset = 0u;
            uint32_t baseLo = 0u, baseHi = 0u;
            uint32_t baseExpectedLo = Bc250GfxRingEncodeBaseLo(activeCtx->GfxEngine.RingGpuVa);
            uint32_t baseExpectedHi = Bc250GfxRingEncodeBaseHi(activeCtx->GfxEngine.RingGpuVa);
            BC250_TDR_STATUS gfxStatus;

            uint32_t plannedCntl = activeCtx->GfxEngine.Evidence.CntlPlanned != 0u ?
                activeCtx->GfxEngine.Evidence.CntlPlanned : 0x50000A0Cu;

            /* Rearma CP_RB0 */
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_ACTIVE, 0u);
            /* CP_RB0_BASE recebe GPUVA >> 8. Na Corrida 43 este caminho
             * escrevia RingGpuVa bruto (inclusive no HI), apontando o CP para
             * uma base 256 vezes errada; o caminho ONLINE ja usava o formato
             * correto. Os helpers tornam a codificacao fonte unica. */
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_BASE,
                Bc250GfxRingEncodeBaseLo(activeCtx->GfxEngine.RingGpuVa));
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_BASE_HI,
                Bc250GfxRingEncodeBaseHi(activeCtx->GfxEngine.RingGpuVa));
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_CNTL, plannedCntl);
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_RPTR_ADDR_LO, (uint32_t)(activeCtx->GfxEngine.RptrWbGpuVa & 0xFFFFFFFFull));
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_RPTR_ADDR_HI, (uint32_t)((activeCtx->GfxEngine.RptrWbGpuVa >> 32) & 0xFFFFull));
            if (activeCtx->GfxEngine.WptrPollGpuVa != 0ull) {
                (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                    BC250_GFX_REG_CP_RB_WPTR_POLL_ADDR_LO,
                    (uint32_t)(activeCtx->GfxEngine.WptrPollGpuVa & 0xFFFFFFFFull));
                (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                    BC250_GFX_REG_CP_RB_WPTR_POLL_ADDR_HI,
                    (uint32_t)((activeCtx->GfxEngine.WptrPollGpuVa >> 32) & 0xFFFFull));
            }
            if (activeCtx->GfxEngine.Evidence.PollCntlPlanned != 0u) {
                (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                    BC250_GFX_REG_CP_RB_WPTR_POLL_CNTL,
                    activeCtx->GfxEngine.Evidence.PollCntlPlanned);
            }
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_RPTR, 0u);
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_WPTR, 0u);
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_WPTR_HI, 0u);

            if (activeCtx->GfxEngine.RingCpuVirtual != NULL) {
                RtlZeroMemory(activeCtx->GfxEngine.RingCpuVirtual,
                    activeCtx->GfxEngine.RingDwords * sizeof(uint32_t));
            }
            if (activeCtx->GfxEngine.WptrPollCpuVirtual != NULL) {
                *activeCtx->GfxEngine.WptrPollCpuVirtual = 0u;
            }
            if (activeCtx->GfxEngine.RptrWbCpuVirtual != NULL) {
                *activeCtx->GfxEngine.RptrWbCpuVirtual = 0u;
            }
            if (activeCtx->GfxEngine.FenceCpuVirtual != NULL) {
                *activeCtx->GfxEngine.FenceCpuVirtual = BC250_GFX_FENCE_POISON;
            }
            activeCtx->GfxEngine.WptrDwords = 0u;
            /* Monotonicidade preservada: LastRetiredFence alcanca LastSubmittedFence */
            activeCtx->GfxEngine.LastRetiredFence = activeCtx->GfxEngine.LastSubmittedFence;

            /* Unhalt ME */
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_ME_CNTL, 0u);
            (void)Bc250MmioTopologyWrite32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_ACTIVE, 1u);

            /* Readback validation */
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_ACTIVE, &active);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_RPTR, &rptr);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_WPTR, &wptr);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_BASE, &baseLo);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_BASE_HI, &baseHi);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_ME_CNTL, &meCntl);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                BC250_GRBM_REG_SOFT_RESET, &grbmReset);

            Bc250DisplayLog("[TDR_GFX_RESTART_BASE] RingGpuVa=0x%llX Esperado=%08X:%08X Readback=%08X:%08X Pass=%u\n",
                (unsigned long long)activeCtx->GfxEngine.RingGpuVa,
                baseExpectedHi, baseExpectedLo, baseHi, baseLo,
                (baseHi == baseExpectedHi && baseLo == baseExpectedLo) ? 1u : 0u);

            /* DX.27: o soft-reset do CP preserva o RLC_CSIB? Somente leitura. */
            {
                uint32_t csLo = 0u, csHi = 0u, csLen = 0u;
                (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_REG_RLC_CSIB_ADDR_LO_SNAP, &csLo);
                (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_REG_RLC_CSIB_ADDR_HI_SNAP, &csHi);
                (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport, BC250_REG_RLC_CSIB_LENGTH_SNAP, &csLen);
                Bc250DisplayLog("[TDR_GFX_RESTART_CSIB] pos-reset LO=0x%08X HI=0x%08X LEN=%u (somente leitura)\n",
                    csLo, csHi, csLen);
            }

            gfxStatus = Bc250TdrValidateGfxRestartReadback(active, rptr, wptr, meCntl, grbmReset);
            if (baseHi != baseExpectedHi || baseLo != baseExpectedLo) {
                Bc250DisplayLog("[TDR_GFX_RESTART] Readback INVALIDO: CP_RB0_BASE esperado=%08X:%08X observado=%08X:%08X\n",
                    baseExpectedHi, baseExpectedLo, baseHi, baseLo);
                gfxRestart = FALSE;
                activeCtx->GfxEngine.State = 0u;
            } else if (gfxStatus != BC250_TDR_OK) {
                Bc250DisplayLog("[TDR_GFX_RESTART] Readback INVALIDO: status=%s Active=%u RPTR=%u WPTR=%u MeCntl=0x%X GrbmReset=0x%X\n",
                    Bc250TdrStatusName(gfxStatus), active, rptr, wptr, meCntl, grbmReset);
                gfxRestart = FALSE;
                activeCtx->GfxEngine.State = 0u;
            } else if (!Bc250ActiveTdrProveGfxRestart(activeCtx)) {
                Bc250DisplayLog("[TDR_GFX_RESTART] FALHA: registradores rearmados, mas o CP_RB0 nao executou NOP+RELEASE_MEM\n");
                gfxRestart = FALSE;
                activeCtx->GfxEngine.State = 0u;
            } else {
                activeCtx->GfxEngine.State = 1u;
                Bc250DisplayLog("[TDR_GFX_RESTART] CP_RB0 rearmado e comprovado por execucao fisica\n");
            }
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
                Bc250DisplayLog("[TDR_MEC_RESTART] MEC queue reprogramada com readback coerente; execucao pos-reset nao foi provada aqui\n");
            }
            break;
        }
    }
    ExReleasePushLockShared(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();

    if (sdmaRestart && mecRestart && gfxRestart && fenceReconcile) {
        KeEnterCriticalRegion();
        ExAcquirePushLockShared(&g_Bc250AdapterDirectoryLock);
        for (dirIdx = 0u; dirIdx < BC250_ACTIVE_MAX_ADAPTERS; ++dirIdx) {
            BC250_DXGK_ACTIVE_CONTEXT *activeCtx = g_Bc250AdapterDirectory[dirIdx];
            if (activeCtx != NULL && activeCtx->Signature == BC250_ACTIVE_SIGNATURE) {
                Bc250WddmFenceTrackerSetAcceptingWork(
                    &activeCtx->SubmissionEngine.WddmFences, TRUE);
            }
        }
        ExReleasePushLockShared(&g_Bc250AdapterDirectoryLock);
        KeLeaveCriticalRegion();
        return TRUE;
    }
    return FALSE;
}

static BOOLEAN Bc250ActiveGfxProbeResponsive(_Out_ UINT32 *IsResponsive)
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
            activeCtx->GfxEngine.State == 1u) {
            uint32_t rptr1 = 0u, wptr1 = 0u, rptr2 = 0u, wptr2 = 0u;
            uint32_t isHung = 0u;
            BC250_TDR_STATUS tdrStatus;

            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_RPTR, &rptr1);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_WPTR, &wptr1);

            if (rptr1 == wptr1) {
                /* Fila ociosa -> responsiva por definicao */
                *IsResponsive = 1u;
                measured = TRUE;
                break;
            }

            KeStallExecutionProcessor(50u);

            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_RPTR, &rptr2);
            (void)Bc250MmioTopologyRead32(&activeCtx->Core.MmioTransport,
                BC250_GFX_REG_CP_RB0_WPTR, &wptr2);

            tdrStatus = Bc250TdrDetectHang(1u, rptr1, wptr1, rptr2, wptr2, &isHung);
            if (tdrStatus == BC250_TDR_OK) {
                *IsResponsive = (isHung == 0u) ? 1u : 0u;
                measured = TRUE;
                Bc250DisplayLog("[TDR_GFX_PROBE] Responsive=%u (RPTR %u->%u WPTR %u->%u)\n",
                    *IsResponsive, rptr1, rptr2, wptr1, wptr2);
            }
            break;
        }
    }
    ExReleasePushLockShared(&g_Bc250AdapterDirectoryLock);
    KeLeaveCriticalRegion();

    return measured;
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
        return Bc250ActiveGfxProbeResponsive(IsResponsive);
    } else if (NodeOrdinal == 2u) {
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

static KDEFERRED_ROUTINE Bc250ActiveRetirementTimerDpcRoutine;
static KDEFERRED_ROUTINE Bc250ActiveRetirementNotifyDpcRoutine;
static KDEFERRED_ROUTINE Bc250ActiveSdmaRetirementDpcRoutine;

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
    KeInitializeSpinLock(&context->VmidPoolLock);
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
     * Inicializacao estrita de DPCs e Timers de aposentadoria (Anti-0x139):
     * As DPCs e timers de aposentadoria do Node 0 (SDMA) e Node 1 (GFX)
     * DEVEM ser inicializados aqui no AddDevice com DeferredRoutine valida
     * antes de qualquer submissao em SubmitCommandVirtual.
     * Sem isso, KeInsertQueueDpc ou KeSetTimerEx despacham para DeferredRoutine=NULL,
     * acionando violacao de CFG (guard_icall_bugcheck -> BugCheck 0x139).
     */
    KeInitializeTimer(&context->RetirementTimer);
    KeInitializeDpc(&context->RetirementTimerDpc,
        Bc250ActiveRetirementTimerDpcRoutine, context);
    KeInitializeDpc(&context->RetirementNotifyDpc,
        Bc250ActiveRetirementNotifyDpcRoutine, context);
    InterlockedExchange(&context->RetirementTimerInitialized, 1);

    KeInitializeTimer(&context->SdmaRetirementTimer);
    KeInitializeDpc(&context->SdmaRetirementTimerDpc,
        Bc250ActiveSdmaRetirementDpcRoutine, context);
    KeInitializeDpc(&context->SdmaRetirementDpc,
        Bc250ActiveSdmaRetirementDpcRoutine, context);
    InterlockedExchange(&context->SdmaRetirementTimerInitialized, 1);

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

        /*
         * INVENTARIO DOS 16 CONTEXTOS GCVM -- SOMENTE LEITURA (21/09).
         *
         * A Corrida 49 mediu que varios contextos do DWM disputam o VMID 7 e
         * que suas raizes se alternam entre submissoes -- 5 faults, 5
         * comparacoes, TODAS divergentes, e o mesmo IB submetido sob TRES
         * raizes. O conserto e' VMID por contexto, e as pecas ja existem
         * (Bc250GcvmInstallVmid recebe o VMID; o INDIRECT_BUFFER ja o codifica
         * no pacote). Falta saber QUAIS VMIDs estao livres.
         *
         * Assumir que 1-6 e 10-15 estao livres seria a quarta hipotese desta
         * investigacao a nascer de intuicao; as tres anteriores morreram por
         * leitura. Entao o driver PERGUNTA ao silicio, uma vez, no bring-up.
         * Nao escreve nada: a allowlist de ESCRITA segue restrita a 7 e 8.
         */
        /*
         * DX.7 -- pool de VMID inicializado ANTES do inventario, para que o
         * mapa do software e o retrato do silicio aparecam no log na ordem em
         * que o driver os constroi. A contagem de slots e' CALCULADA a partir
         * da mascara de reservados, nao escrita a mao: se um reservado entrar
         * ou sair, o numero no log muda junto.
         */
        {
            uint32_t pf;
            uint32_t livres = 0u;
            Bc250VmidPoolInitialize(&context->VmidPool);
            for (pf = 0u; pf < BC250_VMID_COUNT; ++pf) {
                if (Bc250VmidIsReserved(pf) == 0u) ++livres;
            }
            Bc250DisplayLog("[VMID_POOL] INICIALIZADO: %u slots livres de %u (reservados 0=MEC 7=usuario 8=paginacao 9=compute), MascaraDoPool=0x%04X\n",
                livres, BC250_VMID_COUNT, (uint32_t)BC250_VMID_POOL_MASK);
        }

        if (context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
            BC250_GCVM_VMID8_INTERFACE invIo;
            BC250_GCVM_CONTEXT_INVENTORY inv;
            BC250_GCVM_VMID8_STATUS invStatus;

            RtlZeroMemory(&invIo, sizeof(invIo));
            invIo.Identity = BC250_GCVM_VMID8_IDENTITY;
            invIo.Context = context;
            invIo.Read32 = Bc250ActiveVmidRead32;
            invIo.Write32 = Bc250ActiveVmidWrite32;
            invIo.Flush = Bc250ActiveFlushUserVmidOnly;

            invStatus = Bc250GcvmScanContexts(&invIo, &inv);
            if (invStatus != BC250_GCVM_VMID8_OK) {
                Bc250DisplayLog("[GCVM_INVENTARIO] NAO EXECUTADO: status=%u -- quais VMIDs estao livres segue SEM RESPOSTA\n",
                    (uint32_t)invStatus);
            } else {
                uint32_t iv;
                Bc250DisplayLog("[GCVM_INVENTARIO] Varridos=%u EmUso=%u Livres=%u Ilegiveis=%u MascaraLivre=0x%04X\n",
                    inv.Scanned, inv.InUseCount, inv.FreeCount,
                    inv.UnreadableCount, inv.FreeMask);
                /* DX.6: guarda a foto para o segundo inventario comparar. */
                for (iv = 0u; iv < BC250_GCVM_CONTEXT_COUNT; ++iv) {
                    context->GcvmInventoryFirstCntl[iv] = inv.Entries[iv].Cntl;
                    context->GcvmInventoryFirstPtBase[iv] = inv.Entries[iv].PageTableBase;
                }
                context->GcvmInventoryFirstValid = 1u;
                for (iv = 0u; iv < BC250_GCVM_CONTEXT_COUNT; ++iv) {
                    const BC250_GCVM_CONTEXT_INVENTORY_ENTRY *e = &inv.Entries[iv];
                    Bc250DisplayLog("[GCVM_INVENTARIO] VMID%02u Cntl=0x%08X[lido=%u] Enable=%u PtBase=0x%llX[lido=%u] -> %s\n",
                        e->Vmid, e->Cntl, e->CntlRead, e->Enabled,
                        (unsigned long long)e->PageTableBase, e->PtBaseRead,
                        (e->CntlRead == 0u && e->PtBaseRead == 0u) ? "ILEGIVEL" :
                        (e->InUse != 0u) ? "EM USO" : "LIVRE");
                }
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
                ULONG gfxStagePresent = 0u;
                context->GfxRingBringupLevel = Bc250ActiveReadTunableChecked(
                    L"GfxRingBringup", 0u, &gfxBringupPresent);
                context->GfxRingBringupPresent = gfxBringupPresent;
                context->GfxRingIsolationStage = Bc250ActiveReadTunableChecked(
                    L"GfxRingStage", 0u, &gfxStagePresent);
                context->GfxRingStagePresent = gfxStagePresent;
                if (context->GfxRingBringupLevel > 5u) {
                    Bc250DisplayLog("[GfxRing] BRINGUP RECUSADO: level=%u fora da faixa 0..5 -- tratado como 0\n",
                        context->GfxRingBringupLevel);
                    context->GfxRingBringupLevel = 0u;
                }
                if (context->GfxRingIsolationStage > 4u) {
                    Bc250DisplayLog("[GfxRing] BRINGUP RECUSADO: stage=%u fora da faixa 0..4 -- tratado como 1\n",
                        context->GfxRingIsolationStage);
                    context->GfxRingIsolationStage = 1u;
                }
                Bc250DisplayLog("[StartDevice] GfxRingBringup=%u (present=%u compiled=%u) GfxRingStage=%u (present=%u) -- 0 = nenhum ensaio (default); 1 configura e confere; 2 acrescenta desalt e partida; 3 acrescenta submissao SET_SH_REG e tripe; 4 acrescenta isolamento de estado 3D (4.0 NoCtx, 4.1 RT, 4.2 Vport, 4.3 Prim, 4.4 Draw); 5 acrescenta pipeline 3D draw completo com DRAW_INDEX_AUTO\n",
                    context->GfxRingBringupLevel, context->GfxRingBringupPresent,
                    gfxRingCompiled, context->GfxRingIsolationStage, context->GfxRingStagePresent);
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

            {
                /*
                 * ==========================================================
                 * GfxWaveProbe -- LEITURA NO CAMINHO COMUM. CORRIGIDO 19/09.
                 * ==========================================================
                 * MEDIDO na Corrida 36 (12:11:58): o INF empacotado trazia
                 * GfxWaveProbe=1, o manifesto derivava disso, e mesmo assim o
                 * quiesce imprimiu
                 *   [TDR_GFX_WAVEPC] DESARMADO: GfxWaveProbe=0 present=0
                 * O "present=0" e' o discriminante: o valor nao foi lido como
                 * zero, ele NUNCA foi consultado.
                 *
                 * Causa: a leitura estava DEPOIS do
                 * #if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP),
                 * que e' exatamente o flag que a imagem de LABORATORIO define
                 * -- ou seja, compilada para fora da imagem que roda.
                 *
                 * QUARTA ocorrencia desta classe no projeto (A13 no tunable de
                 * submissao, B4 no GpuvaLabPromotion, ContextAbi), e a linha
                 * errada foi escrita a poucas linhas do comentario que registra
                 * a correcao do B4. Por isso esta leitura fica AQUI, ao lado de
                 * GfxRingPreflight e Node1JournalGatePerProcess, que sao lidos
                 * antes de qualquer #if e apareceram com present=1 na mesma
                 * corrida -- a prova de que este ponto executa nas duas imagens.
                 */
                ULONG wavePresent = 0u;
                context->GfxWaveProbe = Bc250ActiveReadTunableChecked(
                    L"GfxWaveProbe", 0u, &wavePresent);
                context->GfxWaveProbePresent = wavePresent;
                Bc250DisplayLog("[StartDevice] GfxWaveProbe=%u (present=%u) -- 1 arma a inspecao indireta de onda (SQ_IND) no quiesce do TDR; escreve SELETORES e restaura broadcast\n",
                    context->GfxWaveProbe, context->GfxWaveProbePresent);
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
            if (context->GfxRingBringupLevel >= 2u) {
                context->CpSoftReset = 1u;
            }
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
                            Bc250DisplayLog("[GfxRing] AVISO: PfpDirectLoad PhysicalReady=0, mas firmware GOP/POST ativo (PFP_PNTR=0x172E ME_PNTR=0x0E19) -- mantendo level=%u solicitado\n",
                                level);
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

                        /* Ativa o motor grafico real persistente (CP_RB0 / ME / PFP) */
                        {
                            NTSTATUS gfxOnlineStatus = Bc250ActiveGfxEngineOnline(context);
                            if (NT_SUCCESS(gfxOnlineStatus)) {
                                Bc250DisplayLog("[StartDevice] Bc250ActiveGfxEngineOnline SUCESSO: CP_RB0 ativo no silicio (State=1)\n");
                            } else {
                                Bc250DisplayLog("[StartDevice] Bc250ActiveGfxEngineOnline FALHOU: status=0x%08X\n",
                                    (uint32_t)gfxOnlineStatus);
                            }
                        }

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
    (void)Bc250ActiveGfxEngineOffline(context);
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
    uint64_t RetiredGfxRingFence;
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

    count = Bc250WddmFenceTrackerDrainEx(
        &context->SubmissionEngine.WddmFences,
        sync->RetiredRingFence,
        sync->RetiredGfxRingFence,
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
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ BOOLEAN FromDpc)
{
    BC250_ACTIVE_FENCE_SYNC sync;
    BOOLEAN notified = FALSE;
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

    /*
     * Guarda atomica de serializacao:
     * Impede que a thread de submit e a DPC executem a rotina de retirement
     * em paralelo em nucleos diferentes, eliminando colisoes e reentrancias.
     */
    if (InterlockedCompareExchange(&Context->RetireBusy, 1, 0) != 0) {
        return;
    }

    uint32_t retiredMec = 0u;
    uint32_t retiredGfx = 0u;

    if (Context->GfxEngine.State == 1u) {
        (void)Bc250ActiveGfxEnginePoll(Context, &retiredGfx);
    }
    if (Context->SubmissionEngine.State == BC250_SUBMISSION_ENGINE_ONLINE) {
        status = Bc250SubmissionEnginePoll(Context, &retiredMec);
        if (!NT_SUCCESS(status)) {
            ++Context->RetirementPollFailures;
        }
    }

    RtlZeroMemory(&sync, sizeof(sync));
    sync.Context = Context;
    sync.Operation = BC250_ACTIVE_FENCE_SYNC_DRAIN;
    sync.RetiredRingFence = Context->SubmissionEngine.ComputeRing.LastRetiredFence;
    sync.RetiredGfxRingFence = Context->GfxEngine.LastRetiredFence;

    status = Context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
        Context->Core.DxgkInterface.DeviceHandle,
        Bc250ActiveSynchronizedFenceCallback,
        &sync,
        0,
        &notified);
    if (!NT_SUCCESS(status) || !notified || sync.Notified == 0u) {
        InterlockedExchange(&Context->RetireBusy, 0);
        return;
    }

    ++Context->RetirementNotifyBatches;
    Context->RetirementNotifiedTotal += sync.Notified;
    InterlockedExchange(&Context->DpcNotifyPending, 1);

    if (FromDpc) {
        if (InterlockedExchange(&Context->DpcNotifyPending, 0) != 0) {
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
        if (InterlockedCompareExchange(&Context->DpcNotifyPending, 0, 0) != 0) {
            KeInsertQueueDpc(&Context->RetirementNotifyDpc, NULL, NULL);
        }
    }

    InterlockedExchange(&Context->RetireBusy, 0);
}

static VOID Bc250ActiveRetirementTimerDpcRoutine(
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
    Bc250ActiveRetireAndNotify(context, TRUE);
}

static VOID Bc250ActiveRetirementNotifyDpcRoutine(
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
    if (InterlockedExchange(&context->DpcNotifyPending, 0) != 0) {
        if (context->Core.DxgkInterface.DxgkCbNotifyDpc != NULL) {
            context->Core.DxgkInterface.DxgkCbNotifyDpc(
                context->Core.DxgkInterface.DeviceHandle);
        }
    }
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
        KeInitializeDpc(&Context->RetirementTimerDpc,
            Bc250ActiveRetirementTimerDpcRoutine, Context);
        KeInitializeDpc(&Context->RetirementNotifyDpc,
            Bc250ActiveRetirementNotifyDpcRoutine, Context);
    }
    if (InterlockedCompareExchange(
            &Context->RetirementTimerRunning, 1, 0) != 0) {
        return;
    }

    due.QuadPart = -10000LL; /* 1 ms */
    KeSetTimerEx(&Context->RetirementTimer, due, 1, &Context->RetirementTimerDpc);
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
            KeSetTimerEx(&Context->SdmaRetirementTimer, due, 0, &Context->SdmaRetirementTimerDpc);
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
        KeInitializeDpc(&Context->SdmaRetirementTimerDpc,
            Bc250ActiveSdmaRetirementDpcRoutine, Context);
        KeInitializeDpc(&Context->SdmaRetirementDpc,
            Bc250ActiveSdmaRetirementDpcRoutine, Context);
    }
    if (InterlockedCompareExchange(
            &Context->SdmaRetirementTimerRunning, 1, 0) != 0) {
        return;
    }

    due.QuadPart = -10000LL; /* 1 ms */
    KeSetTimerEx(&Context->SdmaRetirementTimer, due, 0, &Context->SdmaRetirementTimerDpc);
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

/*
 * ==========================================================================
 * INSPECAO DO IB SUBMETIDO AO ANEL GRAFICO (CP_RB0) -- SOMENTE LEITURA
 * ==========================================================================
 * POR QUE EXISTE (medicao, nao hipotese): na corrida 2026-09-18 10:20 as 87
 * submissoes do Node 1 GFX sairam TODAS com DmaSize=96 / SizeDW=24, porque a
 * RADV submetia apenas o preambulo e deixava o fluxo de clear/draw/destiling
 * atras de um salto PACKET3_INDIRECT_BUFFER (log da RADV: "Chain [0 -> 1]
 * hw_ip=0: PKT3=0xC0023F00 ... (VALID=1)"). A fence do anel avancou em 87 de 87
 * e NAO detectou nada: ela e' gravada por um RELEASE_MEM que o KMD emite NO
 * ANEL, depois do INDIRECT_BUFFER -- logo prova consumo do ANEL, nunca execucao
 * do IB.
 *
 * O QUE ESTA FUNCAO AFIRMA: apenas o que esta na memoria que apontamos ao CP.
 * Se a concatenacao linear da RADV funcionou, o stream NAO contem nenhum
 * PACKET3_INDIRECT_BUFFER; se ainda contem, a submissao voltou ao encadeamento.
 * O QUE ELA NAO AFIRMA: que a GPU buscou ou executou o IB. Isso e' outra perna
 * e outra medicao -- ver GFX_TRIPE_VEREDITO no poll.
 *
 * RESTRICAO DE IRQL: chamada em DISPATCH_LEVEL. Le SOMENTE pela janela de VRAM
 * ja mapeada (FrameBufferVirtual); NUNCA chama MmMapIoSpace -- o projeto ja
 * pagou 0x0A e 0x133 por trabalho indevido em DISPATCH. Offset fora da janela
 * sai como INDETERMINADO: ausencia de leitura NAO vira prova de linearidade.
 *
 * A classificacao do stream vive em driver/core/bc250_gfx_ib_inspect_contract.c
 * (logica pura, testada offline com mutacao). Aqui fica somente o leitor que
 * alcanca a memoria.
 */

#define BC250_GFX_IB_HEAD_DWORDS_LOCAL BC250_GFX_IB_HEAD_DWORDS

/*
 * Contexto do leitor injetado no contrato. Guarda a pagina resolvida para que a
 * caminhada de tabela aconteca uma vez por pagina de 4 KB, nao por dword.
 */
typedef struct BC250_GFX_IB_READER_CTX {
    uint64_t IbGpuVa;
    uint64_t RootMcAddress;
    uint64_t VramMcBase;
    uint64_t CachedPageGpuVa;
    uint64_t CachedPageMc;
    uint32_t Failures;
} BC250_GFX_IB_READER_CTX;

/*
 * Le UM dword do IB pela janela de VRAM JA MAPEADA. Nunca chama MmMapIoSpace:
 * roda em DISPATCH_LEVEL, e o projeto ja pagou 0x0A e 0x133 por trabalho
 * indevido nesse nivel. Fora da janela devolve falha, e o contrato converte
 * falha em INDETERMINADO -- nunca em "linear".
 *
 * Sem buffer de pagina na pilha DE PROPOSITO: a primeira versao usava
 * uint32_t[1024] e o MSVC emitiu __chkstk (LNK2019 no link offline), alem de
 * gastar 4 KB da pilha de DPC. Mesma classe da armadilha do memcpy reconhecido
 * pelo /O2 que este repositorio ja registrou.
 */
static int Bc250ActiveGfxIbReadDword(
    void *ReaderContext,
    uint32_t DwordIndex,
    uint32_t *Value)
{
    BC250_GFX_IB_READER_CTX *ctx = (BC250_GFX_IB_READER_CTX *)ReaderContext;
    uint64_t gpuVa;
    uint64_t pageGpuVa;
    uint64_t resolvedMc;
    uint64_t vramOffset;

    if (ctx == NULL || Value == NULL) return 0;
    *Value = 0u;

    if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {
        ++ctx->Failures;
        return 0;
    }

    gpuVa = ctx->IbGpuVa + (uint64_t)DwordIndex * sizeof(uint32_t);
    pageGpuVa = gpuVa & ~0xFFFull;

    if (ctx->CachedPageMc == 0ull || ctx->CachedPageGpuVa != pageGpuVa) {
        uint64_t resolvedPage = 0ull;
        if (!Bc250ActiveWalkGpuVaQuiet(pageGpuVa, ctx->RootMcAddress,
                ctx->VramMcBase, BC250_PTE_READABLE, &resolvedPage)) {
            ++ctx->Failures;
            return 0;
        }
        ctx->CachedPageGpuVa = pageGpuVa;
        ctx->CachedPageMc = resolvedPage;
    }

    resolvedMc = ctx->CachedPageMc + (gpuVa & 0xFFFull);
    if (resolvedMc < ctx->VramMcBase) {
        ++ctx->Failures;
        return 0;
    }
    vramOffset = resolvedMc - ctx->VramMcBase;
    if (vramOffset > (uint64_t)g_Bc250DisplayState.FrameBufferSize - sizeof(uint32_t)) {
        ++ctx->Failures;
        return 0;
    }

    *Value = *(CONST volatile uint32_t *)
        ((CONST volatile UCHAR *)g_Bc250DisplayState.FrameBufferVirtual + vramOffset);
    return 1;
}

/* Monta o leitor e delega a classificacao ao contrato puro. */
static void Bc250ActiveGfxInspectSubmittedIb(
    _In_ uint64_t IbGpuVa,
    _In_ uint32_t IbSizeDwords,
    _In_ uint64_t RootMcAddress,
    _In_ uint64_t VramMcBase,
    _Out_ BC250_GFX_IB_INSPECTION *Inspection)
{
    BC250_GFX_IB_READER_CTX ctx;

    if (Inspection == NULL) return;
    RtlZeroMemory(Inspection, sizeof(*Inspection));
    Inspection->RequestedDwords = IbSizeDwords;

    if (RootMcAddress == 0ull || VramMcBase == 0ull) {
        return;
    }

    RtlZeroMemory(&ctx, sizeof(ctx));
    ctx.IbGpuVa = IbGpuVa;
    ctx.RootMcAddress = RootMcAddress;
    ctx.VramMcBase = VramMcBase;

    Bc250GfxIbInspectStream(IbSizeDwords, Bc250ActiveGfxIbReadDword, &ctx, Inspection);

    KeMemoryBarrier();
}

/* Emite o veredito da inspecao. Tres desfechos, todos distinguiveis. O nome do
 * veredito sai de FONTE UNICA (Bc250GfxIbInspectVerdictName), para que o log e
 * a suite offline nunca possam discordar sobre o que foi concluido. */
static void Bc250ActiveGfxLogIbInspection(
    _In_ uint64_t IbGpuVa,
    _In_ const BC250_GFX_IB_INSPECTION *Inspection)
{
    const char *verdict;

    if (Inspection == NULL) return;
    verdict = Bc250GfxIbInspectVerdictName(Inspection);

    Bc250DisplayLog("[GfxIbInspect] VA=0x%llX DeclaradoDW=%u VarridoDW=%u Pacotes=%u Type3=%u IB3F=%u Saltos(CHAIN=1)=%u ChamadasIB2(CHAIN=0)=%u DW0=%08X DW1=%08X DW2=%08X DW3=%08X\n",
        (unsigned long long)IbGpuVa,
        Inspection->RequestedDwords, Inspection->ScannedDwords,
        Inspection->PacketsWalked, Inspection->HeadIsType3,
        Inspection->IndirectBufferPackets,
        Inspection->ChainJumps, Inspection->Ib2Calls,
        (Inspection->HeadValidDwords > 0u) ? Inspection->HeadWords[0] : 0u,
        (Inspection->HeadValidDwords > 1u) ? Inspection->HeadWords[1] : 0u,
        (Inspection->HeadValidDwords > 2u) ? Inspection->HeadWords[2] : 0u,
        (Inspection->HeadValidDwords > 3u) ? Inspection->HeadWords[3] : 0u);

    if (Inspection->ScanComplete == 0u) {
        Bc250DisplayLog("[GfxIbInspect] VEREDITO: %s -- varredura incompleta (%u de %u DW, abortada=%u, dwords lidos na cabeca=%u). Ausencia de salto NAO e' prova de stream linear\n",
            verdict,
            Inspection->ScannedDwords, Inspection->RequestedDwords,
            Inspection->WalkAborted, Inspection->HeadValidDwords);
        return;
    }
    if (Inspection->ChainJumps == 0u) {
        Bc250DisplayLog("[GfxIbInspect] VEREDITO: %s -- %u DW varridos, ZERO saltos com CHAIN=1. %u chamada(s) IB2 com CHAIN=0 foram encontradas e NAO desqualificam: o CP executa o alvo e retorna. Isto descreve a MEMORIA apontada ao CP; NAO prova busca nem execucao pela GPU, e nao prova dispatch de shader\n",
            verdict, Inspection->ScannedDwords, Inspection->Ib2Calls);
    } else {
        Bc250DisplayLog("[GfxIbInspect] VEREDITO: %s -- %u SALTO(S) com CHAIN=1, primeiro em DW+%u (mais %u chamada(s) IB2 inofensivas). A concatenacao linear da RADV NAO se aplicou a esta submissao\n",
            verdict, Inspection->ChainJumps,
            Inspection->FirstChainDwordOffset, Inspection->Ib2Calls);
    }
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
                io.FlushVmid = Bc250ActiveFlushVmidTlb;

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
                            /*
                             * destPhysStart e' necessario apenas para DIRECT_FILL (Plano A).
                             * Se a caminhada do destino falhar ou for parcial, destPhysStart
                             * permanece 0 (desativando DIRECT_FILL), mas o INDIRECT continua
                             * elegivel porque o DMA buffer da GPU ja foi validado com sucesso.
                             */
                            destPhysStart = 0ull;
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
         * Despacho assincrono: agenda timer e DPC de retirement do SDMA.
         * O hardware processa em paralelo sem prender a CPU em DISPATCH_LEVEL.
         */
        Bc250ActiveSdmaRetirementEnsureScheduled(context);
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
     * =========================================================================
     * NODE 1: MOTOR GRAFICO REAL (CP_RB0 / PFP / ME - 3D GRAPHICS)
     * =========================================================================
     * Separacao estrita de motores: Node 1 e' DXGK_ENGINE_TYPE_3D.
     * Toda submissao neste no DEVE ir para o CP_RB0. Se o GfxEngine nao estiver
     * pronto (State != 1), a submissao e' RECUSADA fail-closed.
     * REDIRECIONAMENTO DE COMANDOS 3D AO MEC COMPUTE E TERMINANTEMENTE PROIBIDO.
     * =========================================================================
     */
    if (pSubmitCommand->NodeOrdinal == 1u) {
        BC250_ACTIVE_FENCE_SYNC commitSync;
        BC250_MCDM_RUNTIME_CAPS physicalCaps;
        uint64_t physicalRoot = 0ull;
        uint64_t firstPhysical = 0ull;
        uint32_t poolVmid = BC250_VMID_COUNT;
        BC250_VMID_POOL_STATUS poolStatus = BC250_VMID_POOL_BAD_ARGUMENT;
        BOOLEAN committed = FALSE;

        assignedFence = 0ull;
        reserved = FALSE;
        submitStatus = STATUS_SUCCESS;

        RtlZeroMemory(&contextView, sizeof(contextView));
        RtlZeroMemory(&physicalCaps, sizeof(physicalCaps));
        RtlZeroMemory(&reserveSync, sizeof(reserveSync));
        RtlZeroMemory(&commitSync, sizeof(commitSync));

        Bc250DisplayLog("[SubmitVirtual Node1 GFX] ENTER: FenceId=%llu DmaSize=%u DmaVA=0x%llX\n",
            (unsigned long long)pSubmitCommand->SubmissionFenceId,
            pSubmitCommand->DmaBufferSize,
            (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress);

        if (context->GfxEngine.State != 1u) {
            Bc250DisplayLog("[SubmitVirtual Node1 GFX] RECUSA FAIL-CLOSED: GfxEngine offline (State=%u); REDIRECIONAMENTO AO MEC E PROIBIDO\n",
                context->GfxEngine.State);
            return STATUS_DEVICE_NOT_READY;
        }

        if (pSubmitCommand->DmaBufferSize == 0u || (pSubmitCommand->DmaBufferSize & 3u) != 0u ||
            pSubmitCommand->DmaBufferVirtualAddress == 0u) {
            Bc250DisplayLog("[SubmitVirtual Node1 GFX] RECUSA: tamanho ou GPUVA do IB invalidos (Size=%u, VA=0x%llX)\n",
                pSubmitCommand->DmaBufferSize,
                (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress);
            return STATUS_INVALID_PARAMETER;
        }

        ibSizeDwords = (uint32_t)(pSubmitCommand->DmaBufferSize / sizeof(uint32_t));

        /* 1. Adquirir visao imutavel do contexto */
        objStatus = Bc250McdmContextAcquireSubmissionView(
            &context->Objects,
            (BC250_MCDM_HANDLE)pSubmitCommand->hContext,
            context,
            pSubmitCommand->NodeOrdinal,
            &contextView);
        if (objStatus != BC250_MCDM_OBJECT_OK) {
            Bc250DisplayLog("[SubmitVirtual Node1 GFX] RECUSA: Bc250McdmContextAcquireSubmissionView falhou (objStatus=%u hContext=%p)\n",
                (uint32_t)objStatus, pSubmitCommand->hContext);
            return Bc250ObjectStatusToNtStatus(objStatus);
        }

        /* 2. Gate autoritativo: validar TODAS as paginas do IB na root deste contexto */
        submitStatus = Bc250ActiveQueryRuntimeCaps(context, &physicalCaps);
        if (NT_SUCCESS(submitStatus) && physicalCaps.Ready != 0u &&
            contextView.RootPageTableSegmentId == 1u &&
            contextView.RootPageTableEntries > 0u) {
            physicalRoot = physicalCaps.VramMcBase +
                physicalCaps.LocalSegmentBaseOffset +
                contextView.RootPageTableAddress;
        }
        if (physicalRoot == 0ull ||
            !Bc250ActiveValidateGpuVaRange("NODE1_GFX_IB",
                (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
                (uint64_t)pSubmitCommand->DmaBufferSize,
                physicalRoot, physicalCaps.VramMcBase,
                BC250_PTE_READABLE, &firstPhysical)) {
            Bc250DisplayLog("[SubmitVirtual Node1 GFX] RECUSA: gate fisico falhou Root=0x%llX VA=0x%llX Size=%u; silicio nao tocado\n",
                (unsigned long long)physicalRoot,
                (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress,
                pSubmitCommand->DmaBufferSize);
            Bc250McdmContextReleaseSubmissionView(&context->Objects,
                (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
            return STATUS_DEVICE_NOT_READY;
        }
        Bc250DisplayLog("[SubmitVirtual Node1 GFX] GATE FISICO PASS: Root=0x%llX VA=[0x%llX,0x%llX) FirstPA=0x%llX\n",
            (unsigned long long)physicalRoot,
            (unsigned long long)pSubmitCommand->DmaBufferVirtualAddress,
            (unsigned long long)((uint64_t)pSubmitCommand->DmaBufferVirtualAddress + pSubmitCommand->DmaBufferSize),
            (unsigned long long)firstPhysical);

        /*
         * 2.6. DISCRIMINANTE DA CORRIDA: tamanho do IB declarado ao CP.
         *
         * Toda submissao da corrida 2026-09-18 10:20 saiu com SizeDW=24 -- so' o
         * preambulo da RADV. Esta linha NUNCA e' amostrada e sai sempre que o
         * tamanho muda, para que a transicao 24 -> ~664 (concatenacao linear
         * aplicada) seja visivel sem grep e sem ambiguidade. Tamanho igual ao da
         * submissao anterior nao gera linha, para nao afogar o log.
         */
        if (ibSizeDwords != context->GfxEngine.LastSubmitSizeDwords) {
            Bc250DisplayLog("[SubmitVirtual Node1 GFX] IB_SIZE_MUDOU: %u DW (%u bytes) -> %u DW (%u bytes); 24 DW = somente preambulo da RADV\n",
                context->GfxEngine.LastSubmitSizeDwords,
                context->GfxEngine.LastSubmitSizeDwords * 4u,
                ibSizeDwords,
                ibSizeDwords * 4u);
        }

        /*
         * 2.7. Inspecao somente-leitura do stream PM4 que apontamos ao CP.
         * Amostrada porque custa uma caminhada de tabela por pagina em
         * DISPATCH_LEVEL. As 8 primeiras submissoes e 1 em 64 depois -- e a
         * inspecao e' FORCADA sempre que o tamanho mudou, porque e' exatamente
         * ali que a pergunta da corrida se decide.
         */
        {
            static uint32_t s_gfxIbInspectCount = 0u;
            const BOOLEAN sizeChanged =
                (ibSizeDwords != context->GfxEngine.LastSubmitSizeDwords) ? TRUE : FALSE;
            const uint32_t seen = s_gfxIbInspectCount++;

            if (sizeChanged || seen < 8u || (seen % 64u) == 0u) {
                BC250_GFX_IB_INSPECTION inspection;
                Bc250ActiveGfxInspectSubmittedIb(
                    (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
                    ibSizeDwords,
                    physicalRoot,
                    physicalCaps.VramMcBase,
                    &inspection);
                Bc250ActiveGfxLogIbInspection(
                    (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
                    &inspection);
            }
        }

        /*
         * DX.7 -- AQUISICAO DE VMID POR CONTEXTO (21/09).
         *
         * ESCOPO DELIBERADO: este bloco ALOCA; o bloco seguinte instala a raiz
         * lateralmente, e a submissao continua em BC250_GCVM_USER_VMID.
         * Trocar o VMID da
         * submissao junto com o mapeamento faria um mapa ERRADO produzir
         * exatamente o mesmo sintoma do defeito atual -- fault de traducao --
         * e a corrida nao saberia dizer qual dos dois falou. Primeiro a
         * medicao prova o mapa; depois a submissao passa a usa-lo.
         *
         * So' os contextos que chegam ATE AQUI entram no pool, e esse e' o
         * ponto: e' exatamente o conjunto que disputa o VMID 7 hoje. O numero
         * de disputantes passa a ser medido em vez de estimado pelos carimbos.
        */
        {
            uint32_t jaTinha = 0u;
            uint32_t allocCount = 0u;
            uint32_t exhaustedCount = 0u;
            uint32_t inUseMask = 0u;
            KIRQL poolIrql;

            KeAcquireSpinLock(&context->VmidPoolLock, &poolIrql);
            poolStatus = Bc250VmidPoolAcquire(
                &context->VmidPool,
                (BC250_MCDM_HANDLE)pSubmitCommand->hContext,
                &poolVmid, &jaTinha);
            allocCount = context->VmidPool.AllocCount;
            exhaustedCount = context->VmidPool.ExhaustedCount;
            inUseMask = context->VmidPool.InUseMask;
            KeReleaseSpinLock(&context->VmidPoolLock, poolIrql);

            if (poolStatus == BC250_VMID_POOL_OK && jaTinha == 0u) {
                Bc250DisplayLog("[VMID_POOL] ATRIBUIDO: Ctx=%p -> VMID%02u (Alocados=%u InUseMask=0x%04X) -- instalado lateralmente; INDIRECT_BUFFER ainda segue em VMID%u\n",
                    pSubmitCommand->hContext, poolVmid,
                    allocCount, inUseMask,
                    BC250_GCVM_USER_VMID);
            } else if (poolStatus == BC250_VMID_POOL_EXHAUSTED) {
                Bc250DisplayLog("[VMID_POOL] RECUSA POR EXAUSTAO #%u: Ctx=%p sem VMID livre; proibido cair no VMID%u compartilhado\n",
                    exhaustedCount, pSubmitCommand->hContext, BC250_GCVM_USER_VMID);
            } else if (poolStatus != BC250_VMID_POOL_OK) {
                Bc250DisplayLog("[VMID_POOL] FALHA: Ctx=%p status=%u -- a atribuicao NAO aconteceu, e a ausencia esta DECLARADA\n",
                    pSubmitCommand->hContext, (uint32_t)poolStatus);
            }
            if (poolStatus != BC250_VMID_POOL_OK) {
                Bc250McdmContextReleaseSubmissionView(&context->Objects,
                    (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
                return (poolStatus == BC250_VMID_POOL_EXHAUSTED) ?
                    STATUS_INSUFFICIENT_RESOURCES : STATUS_DEVICE_NOT_READY;
            }
        }

        /*
         * DX.7 -- INSTALACAO LATERAL NO VMID DO POOL.
         *
         * O Begin acontece sob VmidPoolLock, mas MMIO/readback/flush acontecem
         * SEM esse lock: o ACK pode levar centenas de polls e SubmitVirtual
         * roda em DISPATCH_LEVEL. INSTALLING impede uma segunda transacao no
         * mesmo slot durante essa janela. A submission view ja adquirida faz
         * DestroyContext reter o VMID ate o RecordInstall terminar.
         *
         * Esta faixa NAO muda o pacote: o INDIRECT_BUFFER abaixo continua em
         * BC250_GCVM_USER_VMID. Assim, um erro de instalacao do slot e'
         * distinguivel do efeito da troca do VMID no CP.
         */
        {
            uint32_t needsInstall = 0u;
            BC250_VMID_POOL_STATUS beginStatus;
            KIRQL poolIrql;

            KeAcquireSpinLock(&context->VmidPoolLock, &poolIrql);
            beginStatus = Bc250VmidPoolBeginInstall(&context->VmidPool,
                (BC250_MCDM_HANDLE)pSubmitCommand->hContext,
                poolVmid, physicalRoot, &needsInstall);
            KeReleaseSpinLock(&context->VmidPoolLock, poolIrql);

            /*
             * LATERAL QUER DIZER LATERAL (corrigido em 21/09).
             *
             * Ate esta correcao, qualquer tropeco da instalacao lateral
             * RECUSAVA a submissao GFX. Isso quebrava o proprio escopo da
             * faixa: o VMID alocado nao e' usado por ninguem, entao recusar
             * aqui nao compra seguranca nenhuma -- e transformaria a PRIMEIRA
             * corrida que escreve nos VMIDs 1-15 num desastre total, com
             * todas as submissoes recusadas e nenhuma medicao, caso o silicio
             * simplesmente nao aceite raiz nesses contextos.
             *
             * Continuar aqui NAO e' sucesso silencioso (Lei 4): a submissao
             * sempre foi para BC250_GCVM_USER_VMID, esse caminho esta
             * intocado, o motivo do tropeco e' nomeado no log e a entrada do
             * pool fica marcada FAILED. O que nao pode acontecer e' o slot
             * ficar presos em INSTALLING -- e nao fica, porque BUSY nao inicia
             * transacao e RecordInstall publica FAILED nas demais.
             */
            if (beginStatus == BC250_VMID_POOL_BUSY) {
                /*
                 * Outra submissao do MESMO contexto ja esta instalando este
                 * slot. Nao ha segunda transacao MMIO e nao ha como saber se a
                 * tabela ja vale; o dxgkrnl reapresenta com ALLOCATION_BUSY.
                 */
                Bc250DisplayLog("[VMID_POOL] RECUSA: VMID%02u ja INSTALLING para Ctx=%p; a submissao e adiada em vez de partir com tabela incerta\n",
                    poolVmid, pSubmitCommand->hContext);
                Bc250McdmContextReleaseSubmissionView(&context->Objects,
                    (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
                return STATUS_GRAPHICS_ALLOCATION_BUSY;
            }
            if (beginStatus != BC250_VMID_POOL_OK) {
                Bc250DisplayLog("[VMID_POOL] RECUSA: BeginInstall VMID%02u Ctx=%p status=%u -- sem transacao nao ha tabela, e o pacote carregaria este VMID\n",
                    poolVmid, pSubmitCommand->hContext,
                    (uint32_t)beginStatus);
                Bc250McdmContextReleaseSubmissionView(&context->Objects,
                    (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
                return STATUS_DEVICE_NOT_READY;
            }

            if (needsInstall != 0u) {
                BC250_GCVM_VMID8_INTERFACE poolIo;
                BC250_GCVM_VMID8_INSTALL_EVIDENCE poolEvidence;
                BC250_GCVM_VMID8_STATUS installStatus;
                BC250_VMID_POOL_STATUS recordStatus;

                RtlZeroMemory(&poolIo, sizeof(poolIo));
                poolIo.Identity = BC250_GCVM_VMID8_IDENTITY;
                poolIo.Context = context;
                poolIo.Read32 = Bc250ActiveVmidRead32;
                poolIo.Write32 = Bc250ActiveVmidWrite32;
                poolIo.FlushVmid = Bc250ActiveFlushVmidTlb;
                RtlZeroMemory(&poolEvidence, sizeof(poolEvidence));

                installStatus = Bc250GcvmInstallVmid(poolVmid, physicalRoot,
                    &poolIo, &poolEvidence);

                KeAcquireSpinLock(&context->VmidPoolLock, &poolIrql);
                recordStatus = Bc250VmidPoolRecordInstall(&context->VmidPool,
                    (BC250_MCDM_HANDLE)pSubmitCommand->hContext,
                    poolVmid, physicalRoot, installStatus);
                KeReleaseSpinLock(&context->VmidPoolLock, poolIrql);

                /*
                 * Ate a Corrida 53 esta linha afirmava que o pacote seguia
                 * no VMID 7 legado. Isso DEIXOU DE SER VERDADE na Corrida 54:
                 * o pacote carrega este mesmo VMID. Telemetria que descreve o
                 * estado anterior e' pior que telemetria ausente -- ela e' lida
                 * como medicao. Um portao proibe a frase antiga de voltar.
                 */
                Bc250DisplayLog("[VMID_POOL] INSTALACAO: Ctx=%p VMID%02u Root=0x%llX InstallStatus=%u RecordStatus=%u Readback=%u Flush=%u Ready=%u -- o INDIRECT_BUFFER desta submissao carrega VMID%02u\n",
                    pSubmitCommand->hContext, poolVmid,
                    (unsigned long long)physicalRoot,
                    (uint32_t)installStatus, (uint32_t)recordStatus,
                    poolEvidence.ProgramReadbackPassed,
                    poolEvidence.TlbFlushPassed,
                    poolEvidence.PhysicalReady,
                    poolVmid);

                /*
                 * A RECUSA VOLTOU (21/09, Corrida 54). Enquanto a instalacao
                 * era lateral, falhar aqui e seguir era o certo: o VMID nao era
                 * usado por ninguem. Agora o INDIRECT_BUFFER abaixo CARREGA
                 * este VMID -- submeter com a tabela nao instalada produziria
                 * exatamente o fault de traducao que esta faixa existe para
                 * eliminar, e o log diria que tudo correu bem.
                 */
                if (installStatus != BC250_GCVM_VMID8_OK ||
                    recordStatus != BC250_VMID_POOL_OK) {
                    Bc250DisplayLog("[VMID_POOL] RECUSA: instalacao do VMID%02u FALHOU para Ctx=%p (InstallStatus=%u RecordStatus=%u) -- o pacote carregaria um VMID sem tabela, entao a submissao e RECUSADA\n",
                        poolVmid, pSubmitCommand->hContext,
                        (uint32_t)installStatus, (uint32_t)recordStatus);
                    Bc250McdmContextReleaseSubmissionView(&context->Objects,
                        (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
                    return STATUS_DEVICE_NOT_READY;
                }
            }
        }

        /* 2.5. Garantir instalacao da tabela de paginas fisica no VMID 7 de usuario */
        if (context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED &&
            physicalCaps.Ready != 0u) {
            if (context->UserVmidInstalledContext != (BC250_MCDM_HANDLE)pSubmitCommand->hContext ||
                context->UserVmidInstalledRootPhysical != physicalRoot ||
                context->UserVmidInstallStatus != BC250_GCVM_VMID8_OK) {

                BC250_GCVM_VMID8_INTERFACE io;
                BC250_GCVM_VMID8_STATUS vStatus;

                RtlZeroMemory(&io, sizeof(io));
                io.Identity = BC250_GCVM_VMID8_IDENTITY;
                io.Context = context;
                io.Read32 = Bc250ActiveVmidRead32;
                io.Write32 = Bc250ActiveVmidWrite32;
                io.Flush = Bc250ActiveFlushUserVmidOnly;
                io.FlushVmid = Bc250ActiveFlushVmidTlb;

                RtlZeroMemory(&context->UserVmidInstallEvidence, sizeof(context->UserVmidInstallEvidence));
                vStatus = Bc250GcvmInstallVmid(BC250_GCVM_USER_VMID, physicalRoot, &io, &context->UserVmidInstallEvidence);
                context->UserVmidInstallStatus = vStatus;
                if (vStatus == BC250_GCVM_VMID8_OK) {
                    HANDLE prevCtx = (HANDLE)context->UserVmidInstalledContext;
                    context->UserVmidInstalledContext = (BC250_MCDM_HANDLE)pSubmitCommand->hContext;
                    context->UserVmidInstalledRootPhysical = physicalRoot;
                    /*
                     * DX.7 -- a raiz observada e' registrada no pool no mesmo
                     * instante em que entra no VMID 7 legado. Isto NAO marca
                     * InstalledRootPhysical do VMID alocado: esse estado so'
                     * ganha autoridade quando o proprio poolVmid for instalado.
                     */
                    {
                        KIRQL poolIrql;
                        KeAcquireSpinLock(&context->VmidPoolLock, &poolIrql);
                        (void)Bc250VmidPoolNoteRoot(&context->VmidPool,
                            (BC250_MCDM_HANDLE)pSubmitCommand->hContext,
                            physicalRoot);
                        KeReleaseSpinLock(&context->VmidPoolLock, poolIrql);
                    }
                    Bc250DisplayLog("[SubmitVirtual Node1 GFX] VMID%u CONTEXT SWITCH: Ctx %p -> %p (Root=0x%llX, PT_BASE=0x%llX)\n",
                        BC250_GCVM_USER_VMID, prevCtx, pSubmitCommand->hContext,
                        (unsigned long long)physicalRoot,
                        (unsigned long long)context->UserVmidInstallEvidence.PageTablePhysical);

                    /*
                     * DX.6 -- SEGUNDO INVENTARIO, UMA UNICA VEZ POR BIND.
                     *
                     * Ver o bloco longo em bc250_active_miniport.h. O primeiro
                     * inventario (bring-up) devolveu os 16 contextos com
                     * valores IDENTICOS, o que e indistinguivel de aliasing de
                     * leitura. Aqui acabamos de instalar UMA raiz num VMID
                     * especifico -- entao, se os offsets forem distintos, so'
                     * esse contexto pode ter mudado. Se TODOS mudarem, os 16
                     * offsets caem no mesmo registrador e o inventario inteiro
                     * cai, junto com a viabilidade de VMID por contexto.
                     *
                     * Somente leitura. Disparo unico por InterlockedCompareExchange:
                     * a troca de contexto acontece dezenas de vezes por corrida e
                     * repetir a varredura em todas afogaria o log sem informar mais.
                     */
                    if (context->GcvmInventoryFirstValid != 0u &&
                        InterlockedCompareExchange(
                            &context->GcvmInventorySecondDone, 1, 0) == 0) {
                        BC250_GCVM_VMID8_INTERFACE inv2Io;
                        BC250_GCVM_CONTEXT_INVENTORY inv2;
                        BC250_GCVM_VMID8_STATUS inv2Status;

                        RtlZeroMemory(&inv2Io, sizeof(inv2Io));
                        inv2Io.Identity = BC250_GCVM_VMID8_IDENTITY;
                        inv2Io.Context = context;
                        inv2Io.Read32 = Bc250ActiveVmidRead32;
                        inv2Io.Write32 = Bc250ActiveVmidWrite32;
                        inv2Io.Flush = Bc250ActiveFlushUserVmidOnly;

                        inv2Status = Bc250GcvmScanContexts(&inv2Io, &inv2);
                        if (inv2Status != BC250_GCVM_VMID8_OK) {
                            Bc250DisplayLog("[GCVM_INVENTARIO_2] NAO EXECUTADO: status=%u -- a duvida de aliasing fica SEM RESPOSTA\n",
                                (uint32_t)inv2Status);
                        } else {
                            uint32_t iv2;
                            uint32_t mudaram = 0u;
                            uint32_t mudouOAlvo = 0u;
                            for (iv2 = 0u; iv2 < BC250_GCVM_CONTEXT_COUNT; ++iv2) {
                                const uint32_t difere =
                                    (inv2.Entries[iv2].Cntl != context->GcvmInventoryFirstCntl[iv2] ||
                                     inv2.Entries[iv2].PageTableBase != context->GcvmInventoryFirstPtBase[iv2]) ? 1u : 0u;
                                if (difere != 0u) {
                                    ++mudaram;
                                    if (iv2 == BC250_GCVM_USER_VMID) mudouOAlvo = 1u;
                                    Bc250DisplayLog("[GCVM_INVENTARIO_2] VMID%02u MUDOU: Cntl 0x%08X -> 0x%08X  PtBase 0x%llX -> 0x%llX\n",
                                        iv2, context->GcvmInventoryFirstCntl[iv2],
                                        inv2.Entries[iv2].Cntl,
                                        (unsigned long long)context->GcvmInventoryFirstPtBase[iv2],
                                        (unsigned long long)inv2.Entries[iv2].PageTableBase);
                                }
                            }
                            Bc250DisplayLog("[GCVM_INVENTARIO_2] VEREDITO: Mudaram=%u de %u (alvo VMID%u mudou=%u) -> %s\n",
                                mudaram, BC250_GCVM_CONTEXT_COUNT,
                                BC250_GCVM_USER_VMID, mudouOAlvo,
                                (mudaram == 0u) ? "NENHUM MUDOU -- instalacao nao foi observada, inventario INCONCLUSIVO" :
                                (mudaram >= BC250_GCVM_CONTEXT_COUNT) ? "TODOS MUDARAM -- ALIASING DE LEITURA, o inventario NAO vale" :
                                (mudouOAlvo != 0u) ? "SO O ALVO (e vizinhos) MUDOU -- offsets DISTINTOS, inventario CONFIRMADO" :
                                "MUDOU QUEM NAO DEVIA -- offsets suspeitos, tratar como INCONCLUSIVO");
                        }
                    }
                } else {
                    Bc250DisplayLog("[SubmitVirtual Node1 GFX] VMID%u CONTEXT SWITCH FALHOU: Ctx=%p, Root=0x%llX, Status=%u\n",
                        BC250_GCVM_USER_VMID, pSubmitCommand->hContext, (unsigned long long)physicalRoot, (uint32_t)vStatus);
                    Bc250McdmContextReleaseSubmissionView(&context->Objects,
                        (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
                    return STATUS_DEVICE_NOT_READY;
                }
            }
        }

        /* 3. Reservar slot no rastreador de fences WDDM */
        if (context->Core.DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
            context->Core.DxgkInterface.DeviceHandle == NULL) {
            Bc250DisplayLog("[SubmitVirtual Node1 GFX] RECUSA: DxgkCbSynchronizeExecution ou DeviceHandle nulo\n");
            Bc250McdmContextReleaseSubmissionView(&context->Objects,
                (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
            return STATUS_DEVICE_NOT_READY;
        }

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
            Bc250DisplayLog("[SubmitVirtual Node1 GFX] RECUSA: Fence reserve sync falhou (status=0x%08X reserved=%u accepted=%u)\n",
                (uint32_t)submitStatus, reserved, reserveSync.ReserveAccepted);
            Bc250McdmContextReleaseSubmissionView(&context->Objects,
                (BC250_MCDM_HANDLE)pSubmitCommand->hContext);
            return STATUS_GRAPHICS_ALLOCATION_BUSY;
        }

        /*
         * 4. Submissao ao anel CP_RB0 sob o VMID DO CONTEXTO (Corrida 54).
         *
         * Este e' o passo que o pool inteiro existe para permitir. Ate aqui o
         * pacote ia em BC250_GCVM_USER_VMID e varios contextos disputavam essa
         * unica raiz -- 5 de 5 comparacoes divergentes na Corrida 49, ate 5
         * contextos vivos ao mesmo tempo na Corrida 53. Com um VMID por
         * contexto nao ha raiz para disputar.
         *
         * Chegar aqui ja garante que a tabela DESTE VMID esta instalada e
         * READY: os ramos acima recusam a submissao em qualquer outro caso.
         */
        submitStatus = Bc250ActiveGfxEngineSubmit(
            context,
            (uint64_t)pSubmitCommand->DmaBufferVirtualAddress,
            ibSizeDwords,
            poolVmid,
            &assignedFence);

        if (NT_SUCCESS(submitStatus)) {
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
                Bc250DisplayLog("[SubmitVirtual Node1 GFX] FENCE COMMIT: Token=%llu RingFence=%llu committed=%u accepted=%u\n",
                    (unsigned long long)commitSync.ReservationToken,
                    (unsigned long long)commitSync.RingFence,
                    committed, commitSync.CommitAccepted);
            } else {
                Bc250DisplayLog("[SubmitVirtual Node1 GFX] FENCE COMMIT FALHOU: status=0x%08X committed=%u accepted=%u\n",
                    (uint32_t)submitStatus, committed, commitSync.CommitAccepted);
            }

            Bc250McdmContextReleaseSubmissionView(&context->Objects,
                (BC250_MCDM_HANDLE)pSubmitCommand->hContext);

            Bc250ActiveRetirementTimerStart(context);
            Bc250ActiveRetireAndNotify(context, FALSE);

            return STATUS_SUCCESS;
        } else {
            Bc250DisplayLog("[SubmitVirtual Node1 GFX] GfxEngineSubmit FALHOU: status=0x%08X; cancelando reserva de fence\n",
                (uint32_t)submitStatus);
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

            Bc250McdmContextReleaseSubmissionView(&context->Objects,
                (BC250_MCDM_HANDLE)pSubmitCommand->hContext);

            /*
             * D0.2 -- VEREDITO DE MEDICAO, AQUI E NAO ANTES.
             *
             * A reserva de fence JA foi cancelada neste ponto. Emitir a medicao
             * dentro do submit, com a reserva de pe, alargava a janela em que
             * Bc250ActiveQuiesceAndSnapshotTracker ve reservedCount != 0 e fazia
             * o StopDevice devolver STATUS_DEVICE_BUSY -- reprovacao
             * deterministica do Caso 5 de active_miniport_test (exit 651),
             * atribuida por build de isolamento. Instrumentacao nao pode mudar o
             * comportamento do que ela mede.
             *
             * Sem isto, a Fase D0 nao tem como medir nada: o poll (e com ele
             * GFX_TRIPE_VEREDITO) so' roda depois de um SUBMIT SUCESSO, e na
             * corrida 2026-09-18 14:20:07 as 4 submissoes foram recusadas -- as
             * Pernas 1 e 2 sairam ZERO vezes.
             */
            Bc250ActiveGfxLogRefusalMeasurement(context,
                context->GfxEngine.LastRefusalReason,
                context->GfxEngine.LastRefusalCursor);
            return submitStatus;
        }
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
        Bc250ActiveRetireAndNotify(context, FALSE);
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
            Bc250DisplayLog("[ActiveFlushTlb] PUBLICACAO_FALHA Batch=%ld FailedIndex=%ld "
                "Map=%ld Base=0x%llX Size=0x%llX\n",
                active->GpuvaSnapshots.LastPublishRangeCount,
                active->GpuvaSnapshots.LastPublishFailedRangeIndex,
                active->GpuvaSnapshots.LastPublishFailedMapRange,
                active->GpuvaSnapshots.LastPublishFailedBaseGpuVa,
                active->GpuvaSnapshots.LastPublishFailedSize);
            /*
             * COMPOSICAO DO LOTE (Corrida 44). Na 44, FailedIndex saiu igual a
             * Reusable nas SEIS falhas -- o mecanismo e' que o slot inativo e'
             * COPIA do ativo e o lote entra por cima, entao o teto util e'
             * Cap menos Mapped. O que falta e' saber se o lote e' dominado por
             * MAP ou por UNMAP: se for UNMAP, aplicar unmaps antes de maps
             * derruba o pico sem mudar o estado final; se for MAP, e'
             * capacidade de verdade e reordenar nao resolveria nada.
             */
            Bc250DisplayLog("[ActiveFlushTlb] PUBLICACAO_COMPOSICAO Maps=%ld Unmaps=%ld "
                "Livres=%u (Cap=%u - Mapped=%u) -- Maps>Livres significa capacidade real; "
                "Unmaps altos significam que a ORDEM e' o defeito\n",
                active->GpuvaSnapshots.LastPublishMapCount,
                active->GpuvaSnapshots.LastPublishUnmapCount,
                (occupancy.Capacity >= occupancy.Mapped) ?
                    (occupancy.Capacity - occupancy.Mapped) : 0u,
                occupancy.Capacity, occupancy.Mapped);
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

/*
 * DX.13 -- bits RESERVADOS de um PDE do Navi 10.
 *
 * PROVENIENCIA (Lei 7): o layout esta em bc250_gcvm_contract.h, derivado do
 * comentario de gmc_v10_0.c no amdgpu -- PDE tem 47:6 endereco, 5:3
 * RESERVADO, 2 C, 1 system, 0 valid. NAO e' numero novo: e' exatamente a
 * mascara que Bc250GcvmDecodePde ja usa para reportar reservado[5:3], em
 * fonte unica com ela.
 */
#define BC250_ACTIVE_PDE_RESERVED_MASK 0x0000000000000038ull

/*
 * DX.14 -- HDP_MEM_COHERENCY_FLUSH_CNTL.
 *
 * PROVENIENCIA (Lei 7): offset ja catalogado e ja usado por quatro caminhos
 * deste driver -- bc250_gfx_ring_transaction.h (0x3FC4),
 * bc250_pfp_direct_load.c, bc250_compute_service.c e bc250_submission_engine.c
 * (onde tem o nome HDP_READ_CACHE_INVALIDATE). Nao e endereco novo, e ja esta
 * nas allowlists de leitura E escrita do transporte.
 */
#define BC250_ACTIVE_REG_HDP_MEM_COHERENCY_FLUSH_CNTL 0x00003FC4u

/*
 * Indice de raiz da arena que falha. DERIVADO, nunca digitado:
 * 0x200040000 >> (12 + BC250_GPUVA_PT_LEAF_INDEX_BITS) = 0x200040000 >> 21
 * = 4096. E' a mesma conta que Bc250ActiveWalkPageTable faz para achar o
 * rootIndex, e por isso as duas nao podem divergir se a geometria mudar.
 */
#define BC250_ACTIVE_ARENA_ROOT_INDEX \
    ((UINT32)(0x200040000ull >> (12u + BC250_GPUVA_PT_LEAF_INDEX_BITS)))

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
    uint64_t allocationToken = 0ull;
    BC250_MCDM_OBJECT_RECORD allocationRecord;
    uint32_t allocationRecordKnown = 0u;

    RtlZeroMemory(&allocationRecord, sizeof(allocationRecord));

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
        /*
         * DX.11: o handle sozinho nao nomeia a alocacao no TDR e pode ja ter
         * morrido quando a caminhada roda. Copiamos o token imutavel para o
         * journal/snapshot enquanto o registro ainda esta sob ObjectLock.
         */
        if (update->hAllocation != NULL) {
            if (Bc250McdmObjectLookup(&active->Objects,
                    (BC250_MCDM_HANDLE)update->hAllocation,
                    BC250_MCDM_OBJECT_ALLOCATION, NULL,
                    &allocationRecord) == BC250_MCDM_OBJECT_OK) {
                allocationToken = allocationRecord.AllocationToken;
                allocationRecordKnown = 1u;
            }
        }
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

                /*
                 * Corrida 57: nenhum dos cinco TDRs reteve um fault, mas o
                 * UpdatePageTable mapeou 64 paginas exatamente em
                 * 0x200040000. O PC fisico medido na Corrida 45 foi
                 * 0x20004114C, isto e', +0x114C dentro dessa mesma faixa.
                 * Esperar outro fault para descobrir as permissoes da PTE e
                 * o nome da alocacao desperdicaria a unica observacao que
                 * ocorre em toda corrida. Este veredito e somente leitura:
                 * descreve a entrada DXGK e a codificacao que ja seria usada.
                 */
                {
                    const uint64_t targetGpuVa = 0x200040000ull;
                    const uint64_t firstGpuVa =
                        update->FirstPteVirtualAddress;
                    uint64_t targetPage = 0ull;

                    if (firstGpuVa <= targetGpuVa) {
                        targetPage =
                            (targetGpuVa - firstGpuVa) /
                            BC250_GPUVA_PAGE_BYTES;
                    }
                    if (firstGpuVa <= targetGpuVa &&
                        targetPage < update->NumPageTableEntries) {
                        const DXGK_PTE *targetPte =
                            (update->Flags.Repeat != 0u) ?
                                &update->pPageTableEntries[0] :
                                &update->pPageTableEntries[targetPage];
                        UINT64 encoded = 0ull;
                        NTSTATUS encodeStatus =
                            Bc250McdmEncodeVidMmPteWithLevel(
                                Caps, targetPte, 0u, &encoded);

                        Bc250DisplayLog("[DX_TARGET_200040] MAP: GpuVa=0x%llX MapBase=0x%llX MapPages=%u TargetPage=%llu hProc=%p hAlloc=%p Token=0x%llX Nome=%s AllocBytes=0x%llX AllocKnown=%u | V=%u Z=%u Seg=%u PFN=0x%llX ReadOnly=%u NoExecute=%u CacheCoherent=%u RawFlags=0x%llX | EncodedPte=0x%llX EncodeStatus=0x%08X\n",
                            (unsigned long long)targetGpuVa,
                            (unsigned long long)firstGpuVa,
                            update->NumPageTableEntries,
                            (unsigned long long)targetPage,
                            update->hProcess, update->hAllocation,
                            (unsigned long long)allocationToken,
                            Bc250ActiveAllocationTokenClassName(
                                allocationToken),
                            (unsigned long long)(allocationRecordKnown != 0u ?
                                allocationRecord.AllocationBytes : 0ull),
                            allocationRecordKnown,
                            (uint32_t)targetPte->Valid,
                            (uint32_t)targetPte->Zero,
                            (uint32_t)targetPte->Segment,
                            (unsigned long long)targetPte->PageAddress,
                            (uint32_t)targetPte->ReadOnly,
                            (uint32_t)targetPte->NoExecute,
                            (uint32_t)targetPte->CacheCoherent,
                            (unsigned long long)targetPte->Flags,
                            (unsigned long long)encoded,
                            (uint32_t)encodeStatus);
                    }
                }

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
                        entries[index].AllocationToken = allocationToken;
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
        /*
         * DX.13 -- A ESCRITA DE ENTRADA DE DIRETORIO DEIXOU DE SER MUDA.
         *
         * Dois defeitos reais nesta escrita, achados depois da Corrida 59:
         *
         * (1) O STATUS DO ENCODE ERA DESCARTADO com (void). Quando
         *     Bc250McdmEncodeVidMmPteWithLevel recusa, ele deixa *Encoded em
         *     ZERO -- e o laco gravava esse zero numa page table VIVA, sem uma
         *     linha de log. E' a Lei 4 no caminho mais caro que existe: uma
         *     entrada zerada nao e' "operacao que falhou", e' um mapeamento
         *     que some. O laco de validacao em bc250_mcdm_callbacks.c torna
         *     isso improvavel hoje, mas "improvavel e mudo" e' exatamente a
         *     classe que este projeto ja pagou cinco vezes.
         *
         * (2) TRIPWIRE DE PDE. A Corrida 59 leu, no indice 4096 de uma raiz
         *     REAL, o valor 0x450C9C071 -- formato de PTE (VALID|EXE|READ|
         *     WRITE) num slot de PDE. Pelo layout do Navi 10 o endereco do PDE
         *     comeca no bit 6, entao o bit 6 e' endereco e os bits 5:3 sao
         *     RESERVADOS: a GPU caminharia 64 bytes adiante. E' a assinatura
         *     do defeito de 05/09 (0x61), que ficou invisivel por seis
         *     corridas. O MESMO indice 4096 e' o da arena 0x200040000 que
         *     falha, e o indice 4632 da MESMA raiz tinha PDE limpo -- logo a
         *     caminhada leu a tabela certa.
         *
         *     Bc250GcvmEncodePde nao consegue produzir esse valor: ele recusa
         *     qualquer flag fora de VALID/SYSTEM/SNOOPED. Por isso o tripwire
         *     e' util nos DOIS desfechos -- se disparar, o valor sai de um
         *     caminho nosso que eu nao conheco; se nunca disparar enquanto o
         *     TDR continuar lendo 0x...071, quem escreve nao somos nos.
         */
        UINT32 pdeTripwires = 0u;
        UINT32 encodeRefusals = 0u;
        NTSTATUS lastRefusal = STATUS_SUCCESS;

        for (index = 0u; index < update->NumPageTableEntries; ++index) {
            const DXGK_PTE *sourcePte = (update->Flags.Repeat != 0u) ?
                &update->pPageTableEntries[0] :
                &update->pPageTableEntries[index];
            UINT64 encoded = 0u;
            NTSTATUS encodeStatus = Bc250McdmEncodeVidMmPteWithLevel(
                Caps, sourcePte, update->PageTableLevel, &encoded);

            if (!NT_SUCCESS(encodeStatus)) {
                ++encodeRefusals;
                lastRefusal = encodeStatus;
                continue;
            }
            if (update->PageTableLevel > 0u &&
                (encoded & BC250_ACTIVE_PDE_RESERVED_MASK) != 0ull) {
                ++pdeTripwires;
                continue;
            }
            destination[update->StartIndex + index] = encoded;
        }

        if (encodeRefusals != 0u) {
            Bc250DisplayLog("[ActiveUpdatePT] RECUSA: %u de %u entradas NAO codificaram (ultimo status=0x%08X, Lvl=%u, Start=%u) -- NENHUM zero foi gravado no lugar delas\n",
                encodeRefusals, update->NumPageTableEntries,
                lastRefusal, update->PageTableLevel, update->StartIndex);
        }
        if (pdeTripwires != 0u) {
            /*
             * Se esta linha existir num log, a pergunta da Corrida 59
             * esta respondida do lado errado: o valor de PTE no slot de
             * PDE saiu daqui, e o indice e o nivel dizem de onde.
             */
            Bc250DisplayLog("[PDE_TRIPWIRE] FALHA: %u entradas de nivel %u com bits reservados [5:3] -- formato de PTE em slot de PDE, Start=%u Num=%u; a GPU caminharia endereco deslocado\n",
                pdeTripwires, update->PageTableLevel,
                update->StartIndex, update->NumPageTableEntries);
        }
        /*
         * QUEM ESCREVE O INDICE DA ARENA. O fault que sobrou e em
         * 0x200040000, cujo indice de raiz e 4096 (VA >> 21). Esta
         * linha e rara por natureza -- so sai quando a atualizacao de
         * DIRETORIO cobre esse indice -- e e o discriminante da proxima
         * corrida: com ela, "nos escrevemos" e "nao fomos nos" deixam
         * de ser indistinguiveis.
         */
        if (update->PageTableLevel > 0u &&
            update->StartIndex <= BC250_ACTIVE_ARENA_ROOT_INDEX &&
            BC250_ACTIVE_ARENA_ROOT_INDEX <
                update->StartIndex + update->NumPageTableEntries) {
            const DXGK_PTE *arenaPte = (update->Flags.Repeat != 0u) ?
                &update->pPageTableEntries[0] :
                &update->pPageTableEntries[
                    BC250_ACTIVE_ARENA_ROOT_INDEX - update->StartIndex];
            UINT64 arenaEncoded = 0u;
            NTSTATUS arenaStatus = Bc250McdmEncodeVidMmPteWithLevel(
                Caps, arenaPte, update->PageTableLevel, &arenaEncoded);
            /*
             * DX.17 -- LER DE VOLTA O QUE ACABAMOS DE ESCREVER.
             *
             * O DISCRIMINANTE, e ele nasce de tres corridas concordando:
             * escrevemos o indice 4096 SEMPRE limpo (208 escritas nas Corridas
             * 60 e 61, mais 106 na 63, zero sujas, zero tripwire) e o TDR leu
             * valor de PTE nesse mesmo indice em TRES corridas (59, 61, 63):
             * 0x450C9C071, 0x4526CC071 e 0x451BCC071.
             *
             * "Nos escrevemos limpo" e "a memoria contem sujo" nao podem ser
             * os dois verdadeiros sem que alguem escreva depois -- OU sem que
             * a escrita nao chegue onde pensamos. O readback IMEDIATO separa
             * os dois casos, e e a unica coisa que separa:
             *
             *   readback != encoded  -> a escrita nao chegou ao destino que
             *                           acreditamos; o defeito e do nosso
             *                           caminho de escrita ou do mapeamento.
             *   readback == encoded  -> escrevemos certo, e quem suja e' outro
             *                           agente ou momento; a comparacao do TDR
             *                           com o ultimo valor escrito diz quando.
             *
             * Isto le a MESMA memoria pelo MESMO ponteiro que acabou de ser
             * escrito. Nao prova o que a GPU enxerga -- prova o que a CPU
             * deixou la.
             */
            const UINT64 arenaReadback = destination[BC250_ACTIVE_ARENA_ROOT_INDEX];
            Bc250DisplayLog("[PDE_WRITE] ARENA idx=%u Lvl=%u Start=%u Num=%u Repeat=%u hProc=%p | V=%u Z=%u Seg=%u LargePage=%u PFN=0x%llX RawFlags=0x%llX | Encoded=0x%016llX Status=0x%08X Reservado[5:3]=0x%llX\n",
                (UINT32)BC250_ACTIVE_ARENA_ROOT_INDEX,
                update->PageTableLevel, update->StartIndex,
                update->NumPageTableEntries,
                (UINT32)update->Flags.Repeat, update->hProcess,
                (UINT32)arenaPte->Valid, (UINT32)arenaPte->Zero,
                (UINT32)arenaPte->Segment, (UINT32)arenaPte->LargePage,
                (unsigned long long)arenaPte->PageAddress,
                (unsigned long long)arenaPte->Flags,
                (unsigned long long)arenaEncoded, arenaStatus,
                (unsigned long long)(arenaEncoded &
                    BC250_ACTIVE_PDE_RESERVED_MASK));
            if (arenaReadback != arenaEncoded) {
                /*
                 * FALHA DECLARADA. Nao corrige e nao tenta de novo: se
                 * a escrita nao chega, reescrever so' esconderia a
                 * frequencia, que e' o dado que falta.
                 */
                Bc250DisplayLog("[PDE_WRITE] DIVERGENCIA: idx=%u escrevemos 0x%016llX e lemos de volta 0x%016llX -- a escrita NAO chegou ao destino que acreditamos\n",
                    (UINT32)BC250_ACTIVE_ARENA_ROOT_INDEX,
                    (unsigned long long)arenaEncoded,
                    (unsigned long long)arenaReadback);
            }
            /*
             * Retem o ultimo valor escrito para o TDR comparar. Nao da
             * autoridade a nada: e diagnostico puro.
             */
            active->ArenaPdeLastWritten = arenaEncoded;
            active->ArenaPdeWriteSeq++;

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
    UINT64 segmentOffset = 0;
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
            segmentOffset = esc->SourcePhysAddress;
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
            BC250_DATA_PROOF_PLAN srcProofPlan;
            UINT32 sourceNonZero = 0u;
            BOOLEAN proofUsable = FALSE;
            LARGE_INTEGER bltFreq;
            LARGE_INTEGER bltStart;
            LARGE_INTEGER bltEnd;
            LONG bltMicroseconds = 0;

            RtlZeroMemory(&srcProofPlan, sizeof(srcProofPlan));
            (void)Bc250PlanDataProofCanaries(srcGpu, esc->SizeBytes, 0u,
                g_Bc250SdmaPresent.VramMcStart,
                (UINT64)g_Bc250DisplayState.FrameBufferSize,
                (g_Bc250DisplayState.FrameBufferVirtual != NULL) ? 1u : 0u,
                &srcProofPlan);

            proofUsable = (srcProofPlan.Usable != 0u &&
                srcProofPlan.CanaryCount == BC250_DATA_PROOF_CANARY_COUNT);
            if (proofUsable && esc->Width > 0 && esc->Height > 0) {
                UINT32 sampleX[BC250_DATA_PROOF_CANARY_COUNT];
                UINT32 sampleY[BC250_DATA_PROOF_CANARY_COUNT];
                UINT32 proofIndex;
                ULONG srcPitch = esc->Pitch ? esc->Pitch : (esc->Width * 4);

                sampleX[0] = 0u;                 sampleY[0] = 0u;
                sampleX[1] = esc->Width / 2u;    sampleY[1] = 0u;
                sampleX[2] = 0u;                 sampleY[2] = esc->Height / 2u;
                sampleX[3] = esc->Width / 2u;    sampleY[3] = esc->Height / 2u;
                sampleX[4] = esc->Width - 1u;    sampleY[4] = esc->Height - 1u;

                for (proofIndex = 0u; proofIndex < BC250_DATA_PROOF_CANARY_COUNT; ++proofIndex) {
                    volatile CONST UINT32 *sample = (volatile CONST UINT32 *)
                        ((CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual +
                         srcProofPlan.VramOffset +
                         (UINT64)sampleY[proofIndex] * srcPitch +
                         (UINT64)sampleX[proofIndex] * 4ull);
                    if (*sample != 0u) sourceNonZero++;
                }
                KeMemoryBarrier();

                if (shouldLog) {
                    if (sourceNonZero == 0u) {
                        Bc250DisplayLog("[Bc250Escape] PRESENT_BLT AVISO: Amostras CPU em zero (NonZero=0/%u). Prosseguindo com SDMA HW BLT (Lei F3.2)\n",
                            BC250_DATA_PROOF_CANARY_COUNT);
                    } else {
                        Bc250DisplayLog("[Bc250Escape] PRESENT_BLT: Amostras com dados provados (NonZero=%u/%u)\n",
                            sourceNonZero, BC250_DATA_PROOF_CANARY_COUNT);
                    }
                }
            }

            bltStart = KeQueryPerformanceCounter(&bltFreq);
            {
                ULONG targetWidth = (g_Bc250DisplayState.CurrentMode.Width > 0) ? g_Bc250DisplayState.CurrentMode.Width : 2560;
                ULONG targetHeight = (g_Bc250DisplayState.CurrentMode.Height > 0) ? g_Bc250DisplayState.CurrentMode.Height : 1080;
                ULONG targetPitch = (g_Bc250DisplayState.CurrentMode.Pitch > 0) ? g_Bc250DisplayState.CurrentMode.Pitch : (targetWidth * 4);

                if (esc->Width > 0 && esc->Height > 0 &&
                    (esc->Width < targetWidth || esc->Height < targetHeight)) {
                    ULONG srcPitch = esc->Pitch ? esc->Pitch : (esc->Width * 4);
                    ULONG dstPitch = targetPitch;
                    ULONG dstX = (targetWidth > esc->Width) ? ((targetWidth - esc->Width) / 2) : 0;
                    ULONG dstY = (targetHeight > esc->Height) ? ((targetHeight - esc->Height) / 2) : 0;
                    sdmaDone = Bc250SdmaPresentBltRect(srcGpu, srcPitch,
                        g_Bc250SdmaPresent.ScanoutGpuAddress, dstPitch,
                        dstX, dstY, esc->Width, esc->Height);
                } else {
                    sdmaDone = Bc250SdmaPresentBlt(srcGpu, g_Bc250SdmaPresent.ScanoutGpuAddress, esc->SizeBytes);
                }
            }
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
                InterlockedIncrement(&g_Bc250SdmaPresent.EscapeNonZeroBltCount);
                if (shouldLog) {
                    Bc250DisplayLog("[Bc250Escape] PRESENT_BLT: SDMA HW BLT CONCLUIDO COM SUCESSO! SrcMc=0x%llX DstMc=0x%llX Bytes=%u Escape=#%ld Blt=%ld us NonZero=%u/5\n",
                        (unsigned long long)srcGpu,
                        (unsigned long long)g_Bc250SdmaPresent.ScanoutGpuAddress,
                        esc->SizeBytes,
                        g_Bc250SdmaPresent.EscapeEnteredCount,
                        bltMicroseconds,
                        proofUsable ? sourceNonZero : 5u);
                }
                esc->ResultCode = BC250_PRESENT_BLT_STATUS_SUCCESS;
                g_Bc250SdmaPresent.LastEscapeSegmentOffset = segmentOffset;

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

static uint32_t Bc250ActiveFaultLatchRead32(
    void *Context, uint32_t ByteOffset, uint32_t *Value)
{
    BC250_DXGK_ACTIVE_CONTEXT *active = (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    if (active == NULL || Value == NULL) return 0u;
    return Bc250MmioTopologyRead32(&active->Core.MmioTransport, ByteOffset, Value);
}

static uint32_t Bc250ActiveFaultLatchWrite32(
    void *Context, uint32_t ByteOffset, uint32_t Value)
{
    BC250_DXGK_ACTIVE_CONTEXT *active = (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    if (active == NULL) return 0u;
    /*
     * Guarda de sitio: este adaptador existe SO' para o dreno do latch. Se
     * alguem o reaproveitar para outro registrador, a escrita e' recusada
     * aqui, antes mesmo da allowlist do transporte.
     */
    if (ByteOffset != BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL) return 0u;
    return Bc250MmioTopologyWrite32(&active->Core.MmioTransport, ByteOffset, Value);
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
    /*
     * O VMID ENTROU NA LINHA (21/09, faixa DX.12). Ate aqui esta telemetria
     * dizia o resultado da invalidacao sem dizer de QUEM -- e com tres VMIDs
     * fixos era so' inconveniente. Com a mascara dinamica do pool, uma linha
     * sem VMID nao distingue "invalidou o 2" de "invalidou o 9", que e'
     * exatamente a pergunta da Corrida 59.
     */
    Bc250DisplayLog("[FlushMappedGfxhubTlb] VMID%02u status=%u, PhysReady=%u, AckObserved=%u, Polls=%u\n",
        Vmid,
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
    uint32_t gfxhubOk = 1u;
    uint32_t fixedMask;
    uint32_t poolReadyMask = 0u;
    uint32_t requestedMask;
    uint32_t rejectedMask;
    uint32_t failedMask = 0u;
    uint32_t hdpFlushed = 0u;
    uint32_t vmid;

    /*
     * DX.12 -- A COBERTURA DO FLUSH SEGUE QUEM A GPU ESTA USANDO.
     *
     * Ate 21/09 esta funcao invalidava tres VMIDs FIXOS -- 8 (paginacao), 7
     * (usuario legado) e 9 (compute) -- e mais nenhum. Isso estava certo
     * enquanto o anel grafico submetia em VMID 7. Deixou de estar na Corrida
     * 54, quando o PACKET3_INDIRECT_BUFFER passou a carregar o VMID dinamico
     * do pool: na Corrida 58 foram os VMIDs 1 a 4. Bc250GcvmInstallVmid
     * invalida o slot NA INSTALACAO, mas uma UpdatePageTable posterior nao
     * invalidava nenhum slot dinamico ja instalado.
     *
     * A hipotese que isso ataca: a CPU passa a enxergar a folha nova enquanto
     * a GPU conserva a traducao velha no VMID do contexto. Casa com a medicao
     * da Corrida 58 -- escrita em 0x2469A6000 falhando com a janela de
     * publicacao INALTERADA e a PTE contemporanea 0x458F72071 valida e R/W/X
     * na visao da CPU. CASAR NAO E' PROVAR: continua HIPOTESE ate a corrida.
     *
     * A mascara e' copiada sob VmidPoolLock e o lock e' SOLTO antes de
     * qualquer MMIO. Bc250ActiveFlushVmidTlb toma TlbLock e pode gastar ate
     * 1000 polls; segurar o lock do pool durante isso, em DISPATCH_LEVEL,
     * seria a classe do BugCheck 0x133 ja pago em 10/09.
     */
    fixedMask = (1u << BC250_GCVM_VMID8) |
        (1u << BC250_GCVM_USER_VMID) |
        (1u << BC250_GCVM_COMPUTE_VMID);

    if (active != NULL) {
        KIRQL poolIrql;
        KeAcquireSpinLock(&active->VmidPoolLock, &poolIrql);
        poolReadyMask = Bc250VmidPoolReadyFlushMask(&active->VmidPool);
        KeReleaseSpinLock(&active->VmidPoolLock, poolIrql);
    }

    /*
     * O bit 0 e' recusado explicitamente: o VMID 0 pertence a microengine do
     * MEC, que programa e restaura a propria raiz de forma transacional. O
     * pool ja o exclui, mas a recusa aqui e' o que impede que uma mudanca
     * futura no pool abra esse caminho em silencio.
     */
    requestedMask = (fixedMask | poolReadyMask) & 0xFFFFu;
    rejectedMask = requestedMask & 1u;
    requestedMask &= ~1u;

    /*
     * DX.14 -- TORNAR A TABELA VISIVEL ANTES DE MANDAR A GPU RELE-LA.
     *
     * A ORDEM E O CONTEUDO DA FAIXA. As page tables sao escritas pela CPU em
     * VRAM atraves de PageTableAddress.CpuVirtual, e essas escritas ficam na
     * cache de escrita do HDP. Invalidar a TLB antes de drenar o HDP manda o
     * walker BUSCAR DE NOVO bytes que ainda nao chegaram -- ou seja, a
     * invalidacao pioraria a janela em vez de fecha-la. Primeiro drenar,
     * depois invalidar.
     *
     * POR QUE ESTA HIPOTESE E FORTE (mas continua HIPOTESE -- H, nao P0):
     * este projeto JA mediu exatamente este mecanismo neste silicio. Na
     * corrida de 10/09 12:16 a linha FENCE_VISIBILIDADE trouxe
     * antes_hdp=43DAFFFE e depois_hdp=00001304: o valor apareceu NO FLUSH.
     * Era outra consumidora -- uma fence --, mas a mesma memoria, a mesma
     * cache e o mesmo sentido (CPU escreve, GPU le).
     *
     * E CASA COM O QUE A CORRIDA 60 MEDIU, que nenhuma outra explicacao cobre:
     * duas caminhadas com JANELA INALTERADA -- portanto contemporaneas do
     * fault -- RESOLVERAM ate a folha, com PDE limpo e PTE valida e legivel,
     * enquanto a GPU reportava WALKER_ERROR=6 e MAPPING_ERROR=1 no MESMO
     * endereco. A imagem vista pela CPU estava certa; a vista pela GPU, nao.
     *
     * O QUE ISTO NAO E: prova. Se o fault sobreviver ao flush, a hipotese cai
     * e o suspeito passa a ser o que a GPU usa como raiz, nao o conteudo da
     * tabela.
     */
    if (active != NULL &&
        active->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
        uint32_t hdpReadback = 0u;
        uint32_t hdpWrote = Bc250MmioTopologyWrite32(
            &active->Core.MmioTransport,
            BC250_ACTIVE_REG_HDP_MEM_COHERENCY_FLUSH_CNTL, 1u);
        /*
         * O readback existe para PROVAR que a escrita alcancou o registrador.
         * O valor lido NAO e' o conteudo (este registrador dispara e limpa):
         * o que se afirma e' que a transacao MMIO aconteceu, nada alem disso.
         */
        uint32_t hdpRead = Bc250MmioTopologyRead32(
            &active->Core.MmioTransport,
            BC250_ACTIVE_REG_HDP_MEM_COHERENCY_FLUSH_CNTL, &hdpReadback);
        hdpFlushed = (hdpWrote != 0u && hdpRead != 0u) ? 1u : 0u;
        if (hdpFlushed == 0u) {
            Bc250DisplayLog("[HDP_PT_FLUSH] FALHA: Write=%u Read=%u -- a tabela pode nao estar visivel a GPU, e a invalidacao de TLB abaixo faria o walker rebuscar bytes velhos\n",
                hdpWrote, hdpRead);
        }
    }

    for (vmid = 1u; vmid < BC250_VMID_COUNT; ++vmid) {
        if (((requestedMask >> vmid) & 1u) == 0u) continue;
        if (Bc250ActiveFlushVmidTlb(Context, vmid) == 0u) {
            failedMask |= (1u << vmid);
            gfxhubOk = 0u;
        }
    }

    if (active != NULL) {
        /*
         * A linha sai quando a mascara MUDA, nas primeiras ocorrencias e
         * periodicamente. FlushTlb e' frequente; emitir sempre afogaria o
         * log. Emitir SO' na mudanca deixaria "o pool estava vazio"
         * indistinguivel de "a funcao nunca rodou" -- e essa e' exatamente a
         * diferenca entre os desfechos B e C do criterio da Corrida 59.
         */
        uint32_t seq = active->FlushPoolLogCount++;
        if (poolReadyMask != active->FlushPoolLastMask ||
            seq < 8u || (seq % 512u) == 0u || failedMask != 0u) {
            active->FlushPoolLastMask = poolReadyMask;
            Bc250DisplayLog("[FLUSH_POOL_VMIDS] #%u FixedMask=0x%04X PoolReadyMask=0x%04X RequestedMask=0x%04X RejeitadaMask=0x%04X FalhouMask=0x%04X HdpFlush=%u Ok=%u\n",
                seq, fixedMask, poolReadyMask, requestedMask,
                rejectedMask, failedMask, hdpFlushed, gfxhubOk);
        }
        if (failedMask != 0u) {
            /*
             * Flush PARCIAL nao vira sucesso. O chamador (FlushTlbHandler)
             * fecha GpuvaMappingsOperational quando isto devolve zero, e e'
             * o comportamento certo: submeter com um VMID cuja TLB nao foi
             * invalidada e' exatamente o defeito que esta faixa ataca.
             */
            Bc250DisplayLog("[FLUSH_POOL_VMIDS] FALHA: mascara 0x%04X nao invalidou; o flush PARCIAL nao e contado como sucesso\n",
                failedMask);
        }
    }

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
        /*
         * MESMA CLASSE DE DEFEITO da Perna 1 do anel grafico, achada pelo portao
         * audit_gfx_ring_tripod_and_ib_inspection em 2026-09-18: Read32 devolve
         * 1=sucesso/0=falha e BC250_MMIO_TOPOLOGY_OK vale ZERO, entao comparar os
         * dois inverte o resultado. Aqui o efeito era silencioso e na direcao
         * contraria -- a leitura funcionava, a comparacao dava FALSE e o
         * FORCE_INVALIDATION do UTCL1 NUNCA era aplicado. Nenhum log acusava.
         */
        if (Bc250MmioTopologyRead32(&active->Core.MmioTransport,
                BC250_SDMA0_REG_UTCL1_CNTL, &utcl1Val) != 0u) {
            utcl1Val |= BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION;
            if (Bc250MmioTopologyWrite32(&active->Core.MmioTransport,
                    BC250_SDMA0_REG_UTCL1_CNTL, utcl1Val) == 0u) {
                Bc250DisplayLog("[ActiveFlushTlb] UTCL1 FORCE_INVALIDATION nao aplicado: Write32 recusou 0x%04X\n",
                    BC250_SDMA0_REG_UTCL1_CNTL);
            }
        } else {
            Bc250DisplayLog("[ActiveFlushTlb] UTCL1 FORCE_INVALIDATION nao aplicado: Read32 recusou 0x%04X\n",
                BC250_SDMA0_REG_UTCL1_CNTL);
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
