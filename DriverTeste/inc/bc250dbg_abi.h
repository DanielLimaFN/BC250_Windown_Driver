/* bc250dbg_abi.h - contrato compartilhado entre bc250dbg.sys e o Studio.
 *
 * Esta ABI pertence ao laboratorio novo. Todos os campos tem largura fixa e
 * cada payload comeca com Size; driver e ferramenta devem ser reconstruidos
 * juntos quando BC250DBG_ABI_VERSION mudar.
 */
#ifndef BC250DBG_ABI_H
#define BC250DBG_ABI_H

#define BC250DBG_ABI_VERSION     29u
#define BC250DBG_DRIVER_VERSION  0x001D0000u

#define BC250DBG_NT_DEVICE_NAME    L"\\Device\\BC250Dbg"
#define BC250DBG_DOS_DEVICE_NAME   L"\\DosDevices\\BC250Dbg"
#define BC250DBG_USER_DEVICE_PATH  "\\\\.\\BC250Dbg"

#define BC250DBG_MAX_BARS 6u
#define BC250_PCI_VENDOR_ID     0x1002u
#define BC250_PCI_DEVICE_ID     0x13FEu
#define BC250_BAR0_INDEX        0u
#define BC250_BAR5_INDEX        5u
#define BC250_BAR0_DEFAULT_LEN  0x10000000u
#define BC250_BAR5_DEFAULT_LEN  0x00080000u
#define BC250_SMN_INDEX_OFFSET  0x0038u
#define BC250_SMN_DATA_OFFSET   0x003Cu

#define BC250_SMU_Q0_CMD    0x03B10A08u
#define BC250_SMU_Q0_ARG    0x03B10A48u
#define BC250_SMU_Q0_RSP    0x03B10A68u
#define BC250_SMU_Q2_CMD    0x03B10528u
#define BC250_SMU_Q2_RSP    0x03B10564u
#define BC250_SMU_Q2_ARG    0x03B10998u
#define BC250_SMU_Q2_ARG_HI 0x03B1099Cu
#define BC250_SMU_Q3_CMD    0x03B10A20u
#define BC250_SMU_Q3_RSP    0x03B10A80u
#define BC250_SMU_Q3_ARG    0x03B10A88u
#define BC250_SMU_OK        0x01u
#define BC250_SMN_TEMP_EDGE     0x03B10000u
#define BC250_SMN_TEMP_JUNCTION 0x03B10020u
#define BC250_SMN_TEMP_MEM      0x03B10028u
#define BC250_SMN_FAN_RPM       0x03B10064u
#define BC250_SMN_FAN_PWM       0x03B10068u

/* CTL_CODE esta disponivel tanto por ntddk.h quanto por windows.h. */
#define BC250DBG_IOCTL_BASE 0x800u
#define BC250DBG_IOCTL(n) CTL_CODE(FILE_DEVICE_UNKNOWN, BC250DBG_IOCTL_BASE + (n), METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define IOCTL_BC250DBG_GET_INFO       BC250DBG_IOCTL(0x00)
#define IOCTL_BC250DBG_REG_READ       BC250DBG_IOCTL(0x01)
#define IOCTL_BC250DBG_REG_WRITE      BC250DBG_IOCTL(0x02)
#define IOCTL_BC250DBG_SMN_READ       BC250DBG_IOCTL(0x03)
#define IOCTL_BC250DBG_SMN_WRITE      BC250DBG_IOCTL(0x04)
#define IOCTL_BC250DBG_SMU_MSG        BC250DBG_IOCTL(0x05)
#define IOCTL_BC250DBG_LOG_MAP        BC250DBG_IOCTL(0x06)
#define IOCTL_BC250DBG_LOG_UNMAP      BC250DBG_IOCTL(0x07)
#define IOCTL_BC250DBG_LOG_MASK       BC250DBG_IOCTL(0x08)
#define IOCTL_BC250DBG_SCRIPT_RUN     BC250DBG_IOCTL(0x09)
#define IOCTL_BC250DBG_SCRIPT_ABORT   BC250DBG_IOCTL(0x0A)
#define IOCTL_BC250DBG_SNAPSHOT       BC250DBG_IOCTL(0x0B)
#define IOCTL_BC250DBG_THERMAL_GET    BC250DBG_IOCTL(0x0C)
#define IOCTL_BC250DBG_THERMAL_SET    BC250DBG_IOCTL(0x0D)
#define IOCTL_BC250DBG_ARM            BC250DBG_IOCTL(0x0E)
#define IOCTL_BC250DBG_PCICFG_READ    BC250DBG_IOCTL(0x0F)
#define IOCTL_BC250DBG_SDMA_INIT      BC250DBG_IOCTL(0x10)
#define IOCTL_BC250DBG_SDMA_SUBMIT    BC250DBG_IOCTL(0x11)
#define IOCTL_BC250DBG_SDMA_STATUS    BC250DBG_IOCTL(0x12)
#define IOCTL_BC250DBG_SDMA_LOAD_FW   BC250DBG_IOCTL(0x13)
#define IOCTL_BC250DBG_VRAM_READ      BC250DBG_IOCTL(0x14)
#define IOCTL_BC250DBG_VRAM_WRITE     BC250DBG_IOCTL(0x15)
#define IOCTL_BC250DBG_SDMA_F32_RESET BC250DBG_IOCTL(0x16)
#define IOCTL_BC250DBG_SDMA_OP        BC250DBG_IOCTL(0x17)
#define IOCTL_BC250DBG_VRAM_SCAN      BC250DBG_IOCTL(0x18)
#define IOCTL_BC250DBG_VRAM_FILL      BC250DBG_IOCTL(0x19)
#define IOCTL_BC250DBG_VRAM_VERIFY    BC250DBG_IOCTL(0x1A)
#define IOCTL_BC250DBG_HDP_INVALIDATE BC250DBG_IOCTL(0x1B)
#define IOCTL_BC250DBG_SDMA_RAW       BC250DBG_IOCTL(0x1C)
#define IOCTL_BC250DBG_PSP_STATUS     BC250DBG_IOCTL(0x1D)
#define IOCTL_BC250DBG_PSP_RING_INIT  BC250DBG_IOCTL(0x1E)
#define IOCTL_BC250DBG_PSP_LOAD_FW    BC250DBG_IOCTL(0x1F)
#define IOCTL_BC250DBG_PSP_SETUP_TMR  BC250DBG_IOCTL(0x20)
#define IOCTL_BC250DBG_GCVM_STATUS    BC250DBG_IOCTL(0x21)
#define IOCTL_BC250DBG_GCVM_INIT      BC250DBG_IOCTL(0x22)
#define IOCTL_BC250DBG_GFX_INIT       BC250DBG_IOCTL(0x23)
#define IOCTL_BC250DBG_GFX_SUBMIT     BC250DBG_IOCTL(0x24)
#define IOCTL_BC250DBG_GFX_STATUS     BC250DBG_IOCTL(0x25)
#define IOCTL_BC250DBG_SDMA_ROLLBACK  BC250DBG_IOCTL(0x26)
#define IOCTL_BC250DBG_PSP_ROLLBACK   BC250DBG_IOCTL(0x27)
#define IOCTL_BC250DBG_TELEMETRY       BC250DBG_IOCTL(0x28)
#define IOCTL_BC250DBG_SDMA_FAULT      BC250DBG_IOCTL(0x29)
#define IOCTL_BC250DBG_SDMA_ENGINE_RESET BC250DBG_IOCTL(0x2A)
#define IOCTL_BC250DBG_GCVM_DUMP       BC250DBG_IOCTL(0x2B)
#define IOCTL_BC250DBG_GCVM_PT         BC250DBG_IOCTL(0x2C)
#define IOCTL_BC250DBG_GCVM_TXN        BC250DBG_IOCTL(0x2D)
#define IOCTL_BC250DBG_GCVM_SEM_RELEASE BC250DBG_IOCTL(0x2E)
#define IOCTL_BC250DBG_GCVM_PROGRAM    BC250DBG_IOCTL(0x2F)
#define IOCTL_BC250DBG_GCVM_INVALIDATE BC250DBG_IOCTL(0x30)
#define IOCTL_BC250DBG_CP_UCODE        BC250DBG_IOCTL(0x31)
#define IOCTL_BC250DBG_RLC             BC250DBG_IOCTL(0x32)

#define BC250DBG_ARM_READONLY 0u
#define BC250DBG_ARM_WRITE    1u
#define BC250DBG_ARM_UNSAFE   2u
#define BC250DBG_ARM_MAGIC    0xBC250A11u

#define BC250DBG_SPACE_BAR  0u
#define BC250DBG_SPACE_SMN  1u
#define BC250DBG_SPACE_VRAM 2u

#define BC250DBG_OPS_OK       0u
#define BC250DBG_OPS_DENIED   1u
#define BC250DBG_OPS_RANGE    2u
#define BC250DBG_OPS_TIMEOUT  3u
#define BC250DBG_OPS_MISMATCH 4u
#define BC250DBG_OPS_BADOP    5u
#define BC250DBG_OPS_ABORTED  6u

typedef struct _BC250DBG_BAR_INFO {
    unsigned long long Base;
    unsigned long Raw;
    unsigned long Flags;
} BC250DBG_BAR_INFO;

typedef struct _BC250DBG_INFO {
    unsigned long Size, AbiVersion, DriverVersion, ArmLevel;
    unsigned long PciFound, PciBus, PciDev, PciFn;
    unsigned short VendorId, DeviceId;
    unsigned char RevisionId;
    unsigned char ReservedPci[3];
    BC250DBG_BAR_INFO Bar[BC250DBG_MAX_BARS];
    unsigned long long MmioPhysical;
    unsigned long MmioLength, MmioMapped;
    unsigned long GpuId, ScratchReg0, ScratchReg0B;
    unsigned long GrbmStatus, GrbmStatusB;
    unsigned long CcShaderArray, CcShaderArrayB;
    unsigned long SmuAlive, SmuVersion, SmuDriverIf, SmuFeatures;
    unsigned long GfxFreqMhz, GfxVid, ActiveWgp;
    unsigned long LogEntryCount, LogEntrySize;
    long long LogHead, LogDropped;
    unsigned long ThermalTrusted, ThermalLimitC, ThermalTripped;
    long ThermalMilliC;
} BC250DBG_INFO;

typedef struct _BC250DBG_REG_IO {
    unsigned long Size, Offset, Value, Status;
} BC250DBG_REG_IO;

typedef struct _BC250DBG_SMU_IO {
    unsigned long Size, Queue, Msg, Arg, ArgHi, TimeoutMs, Status, Result;
} BC250DBG_SMU_IO;

typedef struct _BC250DBG_ARM_IO {
    unsigned long Size, Level, Magic, Status;
} BC250DBG_ARM_IO;

typedef struct _BC250DBG_PCICFG_IO {
    unsigned long Size, Bus, Dev, Fn, Offset, Value, Status;
} BC250DBG_PCICFG_IO;

#define BC250DBG_SNAP_MAX 4096u
typedef struct _BC250DBG_SNAP_IN {
    unsigned long Size, Space, Start, Count;
} BC250DBG_SNAP_IN;
typedef struct _BC250DBG_SNAP_OUT {
    unsigned long Size, Count;
} BC250DBG_SNAP_OUT;

typedef struct _BC250DBG_THERMAL {
    unsigned long Size, RawEdge, RawJunction, RawMem, FanRpm, FanPwm;
    long MilliC;
    unsigned long Trusted, LimitC, Tripped, ScaleNum, ScaleDen;
    long OffsetMilliC;
    unsigned long Source;
} BC250DBG_THERMAL;
typedef struct _BC250DBG_THERMAL_SET {
    unsigned long Size, Trusted, LimitC, ScaleNum, ScaleDen;
    long OffsetMilliC;
    unsigned long Source;
} BC250DBG_THERMAL_SET;

/* Amostra observacional coerente. Sem INCLUDE_SMU, contém apenas leituras
 * MMIO/estado já mantido pelo watchdog e pode ser usada em alta frequência.
 * INCLUDE_SMU acrescenta queries seguras, mas escreve no mailbox e é lenta. */
#define BC250DBG_TELEM_INCLUDE_SMU       0x00000001u
#define BC250DBG_TELEM_GFXOFF_VALID      0x00000001u
#define BC250DBG_TELEM_SMU_GFXCLK_VALID  0x00000002u
#define BC250DBG_TELEM_SMU_WGP_VALID     0x00000004u
#define BC250DBG_TELEM_SMU_FEATURE_VALID 0x00000008u

typedef struct _BC250DBG_TELEMETRY_IO {
    unsigned long Size, RequestFlags, ValidMask, Status;
    unsigned long long QpcTicks, QpcFrequency, InterruptTime100ns;
    unsigned long GfxMiscCntl, GfxoffState, GrbmStatus;
    unsigned long SdmaStatusReg, SdmaRbCntl, SdmaRptr, SdmaWptr;
    unsigned long SdmaRptrWriteback, SdmaWptrShadow;
    unsigned long SdmaF32Cntl, SdmaF32Pc, SdmaClkCtrl;
    unsigned long ThermalTrusted;
    long ThermalMilliC;
    unsigned long RawEdge, RawJunction, RawMem;
    unsigned long SmuGfxclkStatus, SmuGfxclkMhz;
    unsigned long SmuActiveWgpStatus, SmuActiveWgp;
    unsigned long SmuFeaturesStatus, SmuFeatures;
} BC250DBG_TELEMETRY_IO;

/* Log compartilhado read-only. */
#define BC250DBG_LOG_MAGIC         0x474F4C42u /* BLOG */
#define BC250DBG_LOG_HEADER_SIZE   64u
#define BC250DBG_LOG_ENTRY_SIZE    128u
#define BC250DBG_LOG_TEXT_LEN      64u
#define BC250DBG_LOG_DEFAULT_COUNT 4096u

#define BC250DBG_LVL_ERROR 0x01u
#define BC250DBG_LVL_WARN  0x02u
#define BC250DBG_LVL_INFO  0x04u
#define BC250DBG_LVL_TRACE 0x08u
#define BC250DBG_LVL_ALL   0x0Fu

#define BC250DBG_SUB_CORE    0x0001u
#define BC250DBG_SUB_PCI     0x0002u
#define BC250DBG_SUB_REG     0x0004u
#define BC250DBG_SUB_SMN     0x0008u
#define BC250DBG_SUB_SMU     0x0010u
#define BC250DBG_SUB_THERMAL 0x0020u
#define BC250DBG_SUB_SCRIPT  0x0040u
#define BC250DBG_SUB_SDMA    0x0080u
#define BC250DBG_SUB_PSP     0x0100u
#define BC250DBG_SUB_GC      0x0200u
#define BC250DBG_SUB_ALL     0x03FFu

#define BC250DBG_EV_TEXT           1u
#define BC250DBG_EV_REG_READ       2u
#define BC250DBG_EV_REG_WRITE      3u
#define BC250DBG_EV_SMN_READ       4u
#define BC250DBG_EV_SMN_WRITE      5u
#define BC250DBG_EV_SMU_MSG        6u
#define BC250DBG_EV_SMU_REFUSED    7u
#define BC250DBG_EV_THERMAL_SAMPLE 8u
#define BC250DBG_EV_THERMAL_TRIP   9u
#define BC250DBG_EV_ARM_CHANGE     10u
#define BC250DBG_EV_SCRIPT_BEGIN   11u
#define BC250DBG_EV_SCRIPT_OP      12u
#define BC250DBG_EV_SCRIPT_END     13u

typedef struct _BC250DBG_LOG_HEADER {
    unsigned long Magic, Version, EntrySize, EntryCount;
    volatile long long Head;
    volatile long long Dropped;
    unsigned long long QpcFrequency;
    unsigned long EntryOffset;
    unsigned long Reserved[5];
} BC250DBG_LOG_HEADER;

typedef struct _BC250DBG_LOG_ENTRY {
    volatile long long Seq;
    unsigned long long Qpc;
    unsigned long Level, Subsys, EventId, Irql;
    unsigned long long A[4];
    char Text[BC250DBG_LOG_TEXT_LEN];
} BC250DBG_LOG_ENTRY;

typedef struct _BC250DBG_LOG_MAP_OUT {
    unsigned long Size, EntryCount, EntrySize, TotalBytes;
    unsigned long long UserAddress;
} BC250DBG_LOG_MAP_OUT;
typedef struct _BC250DBG_LOG_MASK_IN {
    unsigned long Size, LevelMask, SubsysMask;
} BC250DBG_LOG_MASK_IN;

/* Scripts de laboratorio. */
#define BC250DBG_SCRIPT_MAX_OPS 4096u
#define BC250DBG_OP_END         0u
#define BC250DBG_OP_REG_R       1u
#define BC250DBG_OP_REG_W       2u
#define BC250DBG_OP_SMN_R       3u
#define BC250DBG_OP_SMN_W       4u
#define BC250DBG_OP_SMU         5u
#define BC250DBG_OP_DELAY_US    6u
#define BC250DBG_OP_POLL_REG    7u
#define BC250DBG_OP_POLL_SMN    8u
#define BC250DBG_OP_EXPECT_REG  9u
#define BC250DBG_OP_EXPECT_SMN  10u
#define BC250DBG_OP_MARK        11u
#define BC250DBG_OP_THERMAL_CHK 12u
#define BC250DBG_OPF_CONTINUE   0x01u
#define BC250DBG_OPF_NOLOG      0x02u

typedef struct _BC250DBG_OP {
    unsigned long Op, Flags;
    unsigned long long A, B, C, D;
} BC250DBG_OP;
typedef struct _BC250DBG_OP_RESULT {
    unsigned long Status, Reserved;
    unsigned long long Value, ElapsedUs;
} BC250DBG_OP_RESULT;
typedef struct _BC250DBG_SCRIPT_IN {
    unsigned long Size, OpCount, ThermalLimitC, Flags;
    char Name[32];
} BC250DBG_SCRIPT_IN;
typedef struct _BC250DBG_SCRIPT_OUT {
    unsigned long Size, OpCount, Failures, Status;
    unsigned long long ElapsedUs;
} BC250DBG_SCRIPT_OUT;

/* Memoria/SDMA. */
#define BC250DBG_SDMA_RING_SYSRAM 0u
#define BC250DBG_SDMA_RING_VRAM   1u
#define BC250_VRAM_GPU_BASE             0x000000F400000000ull
#define BC250_VRAM_TOTAL_BYTES          0x10000000u
#define BC250_SDMA_RING_VRAM_DEFAULT    0x0FF00000u
#define BC250_SDMA_RPTR_WB_OFFSET       0x0FF01000u
#define BC250_SDMA_WPTR_SHADOW_OFFSET   0x0FF02000u
#define BC250_SDMA_FENCE_VRAM_DEFAULT   0x0FF03000u
#define BC250DBG_ADDR_GPU_BASE    0u
#define BC250DBG_ADDR_RAW         1u
#define BC250DBG_SDMA_FLAG_NO_GCR 0x01u
/* Modos negativos. Existem para provar que o caminho de sucesso nao e
 * fabricado: cada um remove uma condicao obrigatoria e o resultado esperado
 * passa a ser FALHA. Nunca use nenhum deles para declarar funcionamento.
 *
 *   NO_KICK     - o pacote e escrito no anel, mas nem o shadow do WPTR nem o
 *                 registrador sao atualizados. O F32 nao pode ver trabalho
 *                 novo: RPTR e fence precisam ficar parados.
 *   BAD_FENCE   - o pacote FENCE carrega um valor diferente do que a CPU
 *                 compara. RPTR deve alcancar o WPTR, mas FenceReached tem
 *                 de ser 0, provando que a comparacao de fence e real.
 *   BAD_OPCODE  - o pacote de operacao usa um opcode reservado. A engine nao
 *                 pode concluir. Pode deixar o F32 preso: rode por ultimo e
 *                 sempre seguido de reset do F32 e rollback.
 */
#define BC250DBG_SDMA_FLAG_NO_KICK    0x02u
#define BC250DBG_SDMA_FLAG_BAD_FENCE  0x04u
#define BC250DBG_SDMA_FLAG_BAD_OPCODE 0x08u
#define BC250DBG_SDMA_NEGATIVE_MASK     (BC250DBG_SDMA_FLAG_NO_KICK | BC250DBG_SDMA_FLAG_BAD_FENCE |      BC250DBG_SDMA_FLAG_BAD_OPCODE)
/* Deixa o laboratorio submeter mesmo com a engine marcada como suspeita.
 *
 * Em producao o driver DEVE recusar: uma engine que mente sobre conclusao nao
 * pode receber trabalho novo. Mas os experimentos de recuperacao precisam
 * medir o que acontece depois do dano, e para isso a recusa tem de ser
 * explicitamente contornavel. Nao e um modo negativo: nao remove condicao
 * nenhuma da prova. */
#define BC250DBG_SDMA_FLAG_IGNORE_SUSPECT 0x10u

/* Opcode reservado usado apenas pelo modo negativo BAD_OPCODE. */
#define BC250_SDMA_OP_RESERVED_BAD 0x3Fu
/* Diferenca aplicada ao valor emitido no pacote FENCE em BAD_FENCE. */
#define BC250DBG_SDMA_BAD_FENCE_XOR 0xA5A5A5A5u
#define BC250DBG_SDMA_OP_FILL     1u
#define BC250DBG_SDMA_OP_COPY     2u
#define BC250DBG_SDMA_OP_WRITE    3u
#define BC250_SDMA_OP_COPY        1u
#define BC250_SDMA_OP_WRITE       2u
#define BC250_SDMA_OP_CONST_FILL  11u
#define BC250_SDMA_OP_GCR_REQ     17u
#define BC250_SDMA_SUBOP_LINEAR   0u
#define BC250_SDMA_MAX_XFER_BYTES (4u * 1024u * 1024u)
#define BC250_HDP_READ_CACHE_INVALIDATE 0x03FC4u
#define BC250DBG_SDMA_WRITE_MAX_DW 256u
#define BC250DBG_SDMA_RAW_MAX_DW   256u
#define BC250DBG_VRAM_SCAN_MAX_HITS 64u
#define BC250DBG_VRAM_BAD_SAMPLES   16u
#define BC250DBG_VRAM_SCAN_MAX_BYTES (4u * 1024u * 1024u)
#define BC250DBG_VRAM_OP_MAX_BYTES   (4u * 1024u * 1024u)

typedef struct _BC250DBG_SDMA_INIT_IO {
    unsigned long Size, RingSizeDwords, Placement, VramOffset, Status, RbCntl;
    unsigned long long PhysicalAddress;
} BC250DBG_SDMA_INIT_IO;
typedef struct _BC250DBG_SDMA_SUBMIT_IO {
    unsigned long Size, DwordCount, Dwords[BC250DBG_SDMA_RAW_MAX_DW];
    unsigned long RptrBefore, WptrAfter, RptrAfter, Status;
} BC250DBG_SDMA_SUBMIT_IO;
typedef struct _BC250DBG_SDMA_STATUS_IO {
    unsigned long Size, Rptr, Wptr, RbCntl, StatusReg;
    unsigned long long RbBase;
    unsigned long RptrWriteback, WptrShadow, Status;
} BC250DBG_SDMA_STATUS_IO;
/* ABI 6: um rollback que falha precisa dizer ONDE falhou. Sem isto o unico
 * dado disponivel e uma contagem, que nao permite investigar nada. */
#define BC250DBG_ROLLBACK_BAD_SAMPLES 8u
#define BC250DBG_ROLLBACK_RGN_RING     1u
#define BC250DBG_ROLLBACK_RGN_RPTR_WB  2u
#define BC250DBG_ROLLBACK_RGN_WPTR_SH  3u
#define BC250DBG_ROLLBACK_RGN_FENCE    4u

typedef struct _BC250DBG_SDMA_ROLLBACK_IO {
    unsigned long Size, Status, SnapshotPresent, RegistersRestored;
    unsigned long RegisterMismatches, VramDwordsRestored, VramMismatches;
    unsigned long BadCount;
    unsigned long BadRegion[BC250DBG_ROLLBACK_BAD_SAMPLES];
    unsigned long BadOffset[BC250DBG_ROLLBACK_BAD_SAMPLES];
    unsigned long BadGot[BC250DBG_ROLLBACK_BAD_SAMPLES];
    unsigned long BadWant[BC250DBG_ROLLBACK_BAD_SAMPLES];
    unsigned long RegBadCount;
    unsigned long RegBadOffset[BC250DBG_ROLLBACK_BAD_SAMPLES];
    unsigned long RegBadGot[BC250DBG_ROLLBACK_BAD_SAMPLES];
    unsigned long RegBadWant[BC250DBG_ROLLBACK_BAD_SAMPLES];
} BC250DBG_SDMA_ROLLBACK_IO;
typedef struct _BC250DBG_SDMA_LOAD_FW_IO {
    unsigned long Size, DwordCount, Version, Checksum, Status;
} BC250DBG_SDMA_LOAD_FW_IO;
typedef struct _BC250DBG_SDMA_F32_RESET_IO {
    unsigned long Size, PcBefore, PcAfter, Moved, Status;
} BC250DBG_SDMA_F32_RESET_IO;
typedef struct _BC250DBG_SDMA_OP_IO {
    unsigned long Size, Op, Flags, AddrMode;
    unsigned long SrcOffset, DstOffset, ByteCount, Value, FillSize;
    unsigned long DwordCount, Data[BC250DBG_SDMA_WRITE_MAX_DW];
    unsigned long RptrBefore, Wptr, RptrAfter, Completed, StatusReg;
    unsigned long FenceOffset, FencePoison, FenceExpected, FenceObserved;
    unsigned long FenceReached, StatusRegBefore, StatusRegAfter;
    unsigned long F32PcBefore, F32PcAfter;
    unsigned long GfxMiscBefore, GfxoffBefore, GfxMiscKick, GfxoffKick;
    unsigned long GfxMiscAfter, GfxoffAfter;
    unsigned long GrbmBefore, GrbmKick, GrbmAfter;
    unsigned long StatusRegKick, F32PcKick;
    unsigned long ClkCtrlBefore, ClkCtrlKick, ClkCtrlAfter;
    /* ABI 5: evidencia dos modos negativos.
     * FenceEmitted e o valor realmente colocado no pacote FENCE; em operacao
     * normal ele e igual a FenceExpected. Kicked indica se o WPTR foi de fato
     * publicado. Os *Reg* sao lidos do silicio depois da operacao para provar
     * que um modo negativo nao mexeu no ponteiro. */
    unsigned long FenceEmitted, Kicked;
    unsigned long WptrRegBefore, WptrRegAfter, RptrRegAfter;
    unsigned long RptrWbAfter, WptrShadowAfter;
    /* ABI 10: conferencia do resultado feita DENTRO do kernel.
     *
     * Medido em 2026-08-23 08:31: depois da injuria de opcode reservado, uma
     * submissao normal teve RPTR alcancando o WPTR e a fence fisica escrita
     * pela GPU com o valor correto — e os dwords de destino NAO mudaram. O
     * driver reportou Completed=1. Somente a comparacao de dados feita em user
     * mode pegou a falha.
     *
     * Ponteiro e fence, juntos, nao bastam. A conferencia do dado passou a ser
     * parte do criterio de conclusao, no kernel, para que o proprio driver nao
     * consiga anunciar sucesso sem ela. */
    unsigned long DataVerifiedDwords, DataMismatches;
    unsigned long EngineSuspect;
    /* ABI 16: onde conferir o dado, quando o endereco do pacote NAO e o lugar
     * onde o dado deve aparecer.
     *
     * Existe para o passo 5 do gate de memoria. A prova de traducao e uma
     * submissao cujo endereco no pacote e um VA que a page table resolve para
     * OUTRO endereco fisico. Sem este campo, a conferencia de dado do kernel
     * leria o offset do pacote — o lugar errado — e a submissao correta
     * apareceria como divergencia.
     *
     * VerifyVramValid = 1 diz explicitamente 'confira em VerifyVramOffset'.
     * Nao ha caminho implicito: uma submissao WRITE que nao conferiu dado
     * nenhum NAO pode declarar Completed, porque ponteiro e fence juntos ja
     * mentiram uma vez neste projeto (2026-08-23 08:31). */
    unsigned long VerifyVramOffset, VerifyVramValid;
    /* Endereco cru de 64 bits, usado quando AddrMode == BC250DBG_ADDR_RAW e
     * RawDstGpu != 0.
     *
     * DstOffset tem 32 bits, o que limitava endereco cru a 4 GB — e todo
     * endereco abaixo de 4 GB e memoria de sistema real nesta maquina. Um
     * WRITE_LINEAR para la, com traducao desligada, escreveria em RAM do host.
     * O passo 5 precisa de um VA que seja inofensivo se for interpretado como
     * fisico, e isso exige sair dos 32 bits. */
    unsigned long long RawDstGpu, RawSrcGpu;
} BC250DBG_SDMA_OP_IO;
typedef struct _BC250DBG_SDMA_RAW_IO {
    unsigned long Size, DwordCount, Data[BC250DBG_SDMA_RAW_MAX_DW];
    unsigned long RptrBefore, Wptr, RptrAfter, Completed, StatusReg, F32Pc;
} BC250DBG_SDMA_RAW_IO;
typedef struct _BC250DBG_VRAM_SCAN_IO {
    unsigned long Size, Start, Length, Value, Mask, HitCount;
    unsigned long Hits[BC250DBG_VRAM_SCAN_MAX_HITS];
} BC250DBG_VRAM_SCAN_IO;
typedef struct _BC250DBG_VRAM_PATTERN_IO {
    unsigned long Size, Pattern, Value, Start, Length, BadCount;
    unsigned long BadOffset[BC250DBG_VRAM_BAD_SAMPLES];
    unsigned long BadGot[BC250DBG_VRAM_BAD_SAMPLES];
    unsigned long BadWant[BC250DBG_VRAM_BAD_SAMPLES];
} BC250DBG_VRAM_PATTERN_IO;
#define BC250DBG_PAT_CONST 0u
#define BC250DBG_PAT_ADDR  1u

/* PSP 11 GPCOM, offsets em bytes no BAR5. */
#define BC250_MP0_BASE           0x58000u
#define BC250_PSP_C2PMSG_64      0x58200u
#define BC250_PSP_C2PMSG_67      0x5820Cu
#define BC250_PSP_C2PMSG_69      0x58214u
#define BC250_PSP_C2PMSG_70      0x58218u
#define BC250_PSP_C2PMSG_71      0x5821Cu
#define BC250_PSP_C2PMSG_81      0x58244u
#define BC250_PSP_TOS_READY      0x80000000u
#define BC250_PSP_RING_SIZE      0x1000u
#define BC250_PSP_FRAME_BYTES    64u
#define BC250_PSP_CMD_BUF_SIZE   0x1000u
#define BC250_PSP_CMD_BUF_DECLARED 0x400u
#define BC250_PSP_RESP_OFFSET    864u
#define BC250_PSP_RING_TYPE_KM   2u
#define BC250_PSP_CMD_DESTROY_GPCOM_RING 0x000C0000u
#define BC250_PSP_CMD_LOAD_IP_FW 6u
#define BC250_PSP_ERR_UNKNOWN_CMD 0x0000000Bu
#define BC250_PSP_FW_CP_ME  1u
#define BC250_PSP_FW_CP_PFP 2u
#define BC250_PSP_FW_CP_CE  3u
#define BC250_PSP_FW_CP_MEC 4u
#define BC250_PSP_FW_CP_MEC_JT1 5u
#define BC250_PSP_FW_RLC_G  8u
#define BC250_PSP_FW_SDMA0  9u
#define BC250_PSP_FW_SDMA1  10u
#define BC250_PSP_FW_SMU    18u
#define BC250DBG_PSP_MAX_FW_DWORDS (128u * 1024u)

typedef struct _BC250DBG_PSP_STATUS_IO {
    unsigned long Size, C2pmsg64, C2pmsg81, C2pmsg67;
    unsigned long RingLo, RingHi, RingSizeReg, RingCreated;
    unsigned long long RingPhysical;
} BC250DBG_PSP_STATUS_IO;
typedef struct _BC250DBG_PSP_ROLLBACK_IO {
    unsigned long Size, Status, SnapshotPresent, DestroyAck;
    unsigned long RegistersRestored, RegisterMismatches;
} BC250DBG_PSP_ROLLBACK_IO;
typedef struct _BC250DBG_PSP_RING_INIT_IO {
    unsigned long Size, Status, C2pmsg64Before, C2pmsg64After;
    unsigned long long RingPhysical;
} BC250DBG_PSP_RING_INIT_IO;
typedef struct _BC250DBG_PSP_LOAD_FW_IO {
    unsigned long Size, FwType, DwordCount, FenceReached;
    unsigned long RespStatus, RespFwAddrLo, RespFwAddrHi, RespTmrSize;
} BC250DBG_PSP_LOAD_FW_IO;
typedef struct _BC250DBG_PSP_SETUP_TMR_IO {
    unsigned long Size, TmrSizeBytes, VramOffset, FenceReached, RespStatus;
    unsigned long long TmrGpuAddr, TmrPhysAddr;
} BC250DBG_PSP_SETUP_TMR_IO;

/* Diagnostico de falha da SDMA. Somente leitura.
 *
 * Existe por causa do estado medido em 2026-08-23: depois de um pacote com
 * opcode reservado a engine passa a consumir o anel sem escrever memoria —
 * RPTR alcanca o WPTR, o writeback acompanha, e nem WRITE_LINEAR nem FENCE
 * chegam a VRAM. Nenhum dos registradores que o laboratorio lia ate entao
 * explica isso.
 *
 * Os candidatos que este dump cobre:
 *   - UTCL1 (a TLB da propria SDMA): XNACK indica traducao recusada, que
 *     silenciaria exatamente as escritas sem travar o microcontrolador;
 *   - ERROR_LOG e STATUS1/2/3 da engine;
 *   - FREEZE, usado pelo upstream no caminho de parada antes do soft reset;
 *   - falha de protecao latchada na L2 do GC.
 *
 * Nenhum bit e decodificado por suposicao: o consumidor compara o dump de uma
 * sessao saudavel com o da sessao quebrada. */
typedef struct _BC250DBG_SDMA_FAULT_IO {
    unsigned long Size, Status;
    unsigned long StatusReg, Status1Reg, Status2Reg, Status3Reg;
    unsigned long Freeze, ErrorLog, EdcCounter, PowerCntl;
    unsigned long Cntl, ChickenBits, F32Cntl, F32Pc, SemWaitFailTimer;
    unsigned long Utcl1Cntl, Utcl1Watermk, Utcl1RdStatus, Utcl1WrStatus;
    unsigned long Utcl1Inv0, Utcl1Inv1, Utcl1Inv2;
    unsigned long Utcl1RdXnack0, Utcl1RdXnack1;
    unsigned long Utcl1WrXnack0, Utcl1WrXnack1;
    unsigned long Utcl1Timeout, Utcl1Page;
    unsigned long GcvmFaultCntl, GcvmFaultStatus, GcvmFaultAddrLo, GcvmFaultAddrHi;
    unsigned long GrbmStatus, RbCntl, Rptr, Wptr, RptrWriteback, WptrShadow;
    /* ABI 10: a engine ja foi pega mentindo nesta sessao? */
    unsigned long EngineSuspect, SuspectReason;
} BC250DBG_SDMA_FAULT_IO;

/* Motivos de suspeita. */
#define BC250DBG_SUSPECT_NONE          0u
#define BC250DBG_SUSPECT_FENCE_NO_DATA 1u  /* fence escrita, dado nao mudou */
#define BC250DBG_SUSPECT_BAD_OPCODE    2u  /* opcode reservado foi submetido */

/* Soft reset do bloco SDMA0 inteiro, na sequencia do upstream
 * (sdma_v5_0_stop_queue + sdma_v5_0_soft_reset_engine + unfreeze).
 *
 * Existe porque nenhuma acao mais branda recupera a engine depois de um pacote
 * malformado: nem reset do F32, nem recriar o anel, nem recarregar o firmware
 * pelo PSP. Medido em 2026-08-23 em quatro boots.
 *
 * Depois deste reset o anel PRECISA ser recriado: o bloco perde a
 * configuracao. O chamador e responsavel por 'sdma init' em seguida. */
typedef struct _BC250DBG_SDMA_ENGINE_RESET_IO {
    unsigned long Size, Status;
    unsigned long RbCntlBefore, CntlBefore, F32CntlBefore;
    unsigned long FreezeBefore, FreezeAfterSet, FrozenObserved, FreezeIterations;
    unsigned long Status1Before, Status1Idle;
    unsigned long GrbmBefore, GrbmWithReset, GrbmAfterClear;
    unsigned long StatusRegBefore, StatusRegAfter;
    unsigned long Status2Before, Status2After;
    unsigned long CmdOpBefore, CmdOpAfter;
    unsigned long CntlAfter, F32CntlAfter, FreezeFinal;
    /* ABI 9: o soft reset devolve a configuracao da UTCL1 e do CHICKEN_BITS ao
     * padrao de reset, apagando o que o POST tinha deixado. Medido em
     * 2026-08-23: UTCL1_PAGE.USE_BC cai de 1 para 0, e sem esse bit a engine
     * deixa de escrever memoria mesmo estando saudavel. Estes campos guardam a
     * configuracao antes do pulso e o que ficou depois da restauracao. */
    unsigned long Utcl1PageBefore, Utcl1PageReset, Utcl1PageAfter;
    unsigned long Utcl1CntlBefore, Utcl1CntlReset, Utcl1CntlAfter;
    unsigned long ChickenBefore, ChickenReset, ChickenAfter;
    unsigned long Utcl1WatermkBefore, Utcl1WatermkAfter;
    unsigned long ClkCtrlBefore, ClkCtrlAfter;
    unsigned long ConfigRestored, ConfigMismatches;
} BC250DBG_SDMA_ENGINE_RESET_IO;

/* GCVM e CP GFX - ainda diagnosticos experimentais, nunca prova isolada. */
typedef struct _BC250DBG_GCVM_STATUS_IO {
    unsigned long Size, L2Cntl, L2Cntl2, Context0Cntl;
    unsigned long Context0PtBaseLo, Context0PtBaseHi;
    unsigned long Context0StartLo, Context0EndLo;
    unsigned long SysApertureLow, SysApertureHigh;
    unsigned long SysApertureDefLo, SysApertureDefHi;
    unsigned long L2ProtFaultStatus;
} BC250DBG_GCVM_STATUS_IO;
/* Snapshot transacional dos registradores de traducao.
 *
 * Passo 2 do gate de memoria. Ligar traducao substitui o bypass do qual todo o
 * acesso a memoria depende hoje, e a medicao de 2026-08-23 mostrou que engine
 * SDMA quebrada nesta placa nao volta por software. Antes de escrever qualquer
 * valor NOVO, e preciso provar que o caminho de volta existe.
 *
 * Este IOCTL faz tres coisas e nada mais:
 *   SNAPSHOT  guarda os valores atuais no driver;
 *   RESTORE   reescreve exatamente os valores guardados e confere cada readback;
 *   VERIFY    compara o estado atual com o snapshot, sem escrever nada.
 *
 * O RESTORE escreve os MESMOS valores que leu. Isso e deliberado: prova o
 * mecanismo antes de existir divergencia para desfazer. Registradores de acao ou
 * de status ficam FORA da lista de propriedade — INVALIDATE_ENG0_REQ dispara
 * invalidacao quando escrito, e FAULT_STATUS/FAULT_ADDR reportam em vez de
 * configurar. Reescrever esses seria acao disfarcada de restauracao. */
#define BC250DBG_GCVM_TXN_SNAPSHOT 0u
#define BC250DBG_GCVM_TXN_RESTORE  1u
#define BC250DBG_GCVM_TXN_VERIFY   2u
#define BC250DBG_GCVM_TXN_REG_COUNT 35u
#define BC250DBG_GCVM_TXN_BAD_SAMPLES 6u

typedef struct _BC250DBG_GCVM_TXN_IO {
    unsigned long Size, Status, Mode;
    unsigned long SnapshotPresent, RegisterCount;
    unsigned long Restored, Mismatches;
    unsigned long BadCount;
    unsigned long BadOffset[BC250DBG_GCVM_TXN_BAD_SAMPLES];
    unsigned long BadGot[BC250DBG_GCVM_TXN_BAD_SAMPLES];
    unsigned long BadWant[BC250DBG_GCVM_TXN_BAD_SAMPLES];
    unsigned long Offsets[BC250DBG_GCVM_TXN_REG_COUNT];
    unsigned long Values[BC250DBG_GCVM_TXN_REG_COUNT];
} BC250DBG_GCVM_TXN_IO;

/* Construcao e conferencia de page table na VRAM.
 *
 * Passo 1 do gate de memoria, e o unico que pode ser feito sem risco: escreve
 * PTEs pela CPU num pedaco de VRAM nao usado e confere tudo por leitura da CPU.
 * NENHUM registrador de traducao e tocado. Se a estrutura estiver errada, o
 * hardware continua exatamente onde estava.
 *
 * Formato do PTE, de fonte autoritativa do kernel:
 *   valor = (PA & pte_addr_mask) | flags
 *   pte_addr_mask = 0x0000FFFFFFFFF000  (gmc_v10_0.c: 48 bits de PA)
 *   VALID=1<<0, EXECUTABLE=1<<4, READABLE=1<<5, WRITEABLE=1<<6  (amdgpu_vm.h)
 *   MTYPE nos bits 50:48, MTYPE_UC=3  (navi10_enum.h)
 *
 * O PA de uma pagina de VRAM vem de amdgpu_gmc_vram_mc2pa():
 *   PA = endereco_MC - vram_start + vram_base_offset
 * com vram_base_offset = GCMC_VM_FB_OFFSET << 24, lido do silicio em tempo de
 * execucao (gfxhub_v2_0.c: get_mc_fb_offset). O driver nao usa constante para
 * isso: le o registrador. */
#define BC250DBG_GCVM_PT_MODE_BUILD    0u
#define BC250DBG_GCVM_PT_MODE_ROLLBACK 1u
#define BC250DBG_GCVM_PT_MAX_PAGES     512u   /* 512 PTEs = tabela de 4 KB */
#define BC250DBG_GCVM_PT_SAMPLES       8u
/* Flags do PTE usadas pelo laboratorio. Espelham gart_pte_flags do gmc_v10_0
 * (MTYPE_UC | EXECUTABLE) mais o que amdgpu_gart_bind acrescenta para memoria
 * acessivel: VALID, READABLE, WRITEABLE. SYSTEM fica FORA de proposito: a
 * pagina alvo esta na VRAM, nao na memoria do sistema. */
#define BC250DBG_PTE_VALID       0x0000000000000001ull
#define BC250DBG_PTE_EXECUTABLE  0x0000000000000010ull
#define BC250DBG_PTE_READABLE    0x0000000000000020ull
#define BC250DBG_PTE_WRITEABLE   0x0000000000000040ull
#define BC250DBG_PTE_MTYPE_UC    0x0003000000000000ull
#define BC250DBG_PTE_ADDR_MASK   0x0000FFFFFFFFF000ull

typedef struct _BC250DBG_GCVM_PT_IO {
    unsigned long Size, Status, Mode;
    unsigned long TableVramOffset;    /* onde a tabela vai ficar, na VRAM */
    unsigned long TargetVramOffset;   /* primeira pagina apontada, na VRAM */
    unsigned long PageCount;
    unsigned long FbOffsetReg;        /* GCMC_VM_FB_OFFSET lido do silicio */
    unsigned long long VramBaseOffset;/* FbOffsetReg << 24 */
    unsigned long long PteFlags;
    unsigned long long FirstPa, LastPa;
    unsigned long long PteSample[BC250DBG_GCVM_PT_SAMPLES];
    unsigned long EntriesWritten, Mismatches;
    unsigned long FirstMismatchIndex;
    unsigned long long FirstMismatchGot, FirstMismatchWant;
    unsigned long SnapshotPresent, SnapshotDwords, RestoreMismatches;
} BC250DBG_GCVM_PT_IO;

/* Dump SOMENTE LEITURA do estado de traducao do GC (GCVM/GCMC).
 *
 * Existe para o gate de memoria. Toda a execucao de SDMA demonstrada ate aqui
 * acontece sem page table nossa; o driver definitivo precisa montar VMID 0 de
 * verdade, e o primeiro passo e saber exatamente o que o POST deixou
 * configurado. Nenhum registrador aqui e escrito.
 *
 * A lista espelha a sequencia de gfxhub_v2_0.c do upstream: aberturas, TLB,
 * cache L2, contexto 0, contexto 1, falha de protecao e invalidacao. */
typedef struct _BC250DBG_GCVM_DUMP_IO {
    unsigned long Size, Status;
    /* Cache L2 */
    unsigned long L2Cntl, L2Cntl2, L2Cntl3, L2Cntl4, L2Cntl5;
    /* Falha de protecao */
    unsigned long FaultCntl, FaultCntl2, FaultStatus;
    unsigned long FaultAddrLo, FaultAddrHi;
    unsigned long FaultDefaultAddrLo, FaultDefaultAddrHi;
    /* Contexto 0 (VMID 0) e contexto 1 */
    unsigned long Context0Cntl, Context1Cntl;
    unsigned long Ctx0PtBaseLo, Ctx0PtBaseHi;
    unsigned long Ctx0PtStartLo, Ctx0PtStartHi;
    unsigned long Ctx0PtEndLo, Ctx0PtEndHi;
    /* TLB L1 e aberturas */
    unsigned long MxL1TlbCntl;
    unsigned long SysApertureLow, SysApertureHigh;
    unsigned long SysApertureDefaultLsb, SysApertureDefaultMsb;
    unsigned long AgpBase, AgpBot, AgpTop;
    unsigned long FbLocationBase, FbLocationTop, FbOffset;
    /* Abertura de identidade do contexto 1 */
    unsigned long IdentityApertureLowLo, IdentityApertureHighLo;
    /* Invalidacao, engine 0.
     *
     * GCVM_INVALIDATE_ENG0_SEM foi REMOVIDO deste dump em 2026-08-23. Ler esse
     * registrador nao e observacao: e aquisicao. O upstream diz literalmente
     * "a read return value of 1 means semaphore acuqire" e "write with 0 means
     * semaphore release" (gmc_v10_0.c). O dump ficou dias se anunciando como
     * somente leitura enquanto adquiria um semaforo de hardware e nunca o
     * devolvia.
     *
     * O estado do semaforo continua observavel sem efeito colateral: e o bit
     * SEMAPHORE (16) de GCVM_INVALIDATE_ENG0_ACK, que este dump le. */
    unsigned long InvalidateEng0Req, InvalidateEng0Ack;
} BC250DBG_GCVM_DUMP_IO;

/* Libera o semaforo da engine de invalidacao, escrevendo 0.
 *
 * E a outra metade do protocolo do upstream. Existe como comando explicito,
 * separado de qualquer dump, porque adquirir e liberar semaforo sao ACOES e
 * nunca devem estar embutidas numa leitura. */
typedef struct _BC250DBG_GCVM_SEM_IO {
    unsigned long Size, Status;
    unsigned long AckBefore, AckAfter;
    unsigned long HeldBefore, HeldAfter;
} BC250DBG_GCVM_SEM_IO;

/* Passo 3 do gate de memoria: programar valores novos nos registradores de
 * traducao, em estagios, cada escrita com readback conferido.
 *
 * Toda a ordem e todos os valores vem de gfxhub_v2_0.c (upstream, copia em
 * referencias/BC250-windowsDriverTest/docs/upstream/deep/). A funcao
 * gfxhub_v2_0_gart_enable() executa nesta ordem:
 *
 *   init_gart_aperture_regs  -> ESTAGIO GART
 *   init_system_aperture_regs-> ESTAGIO SYSTEM
 *   init_tlb_regs            -> ESTAGIO TLB       (PERIGOSO)
 *   init_cache_regs          -> ESTAGIO CACHE     (PERIGOSO)
 *   enable_system_domain     -> ESTAGIO CONTEXT0  (PONTO SEM VOLTA)
 *   disable_identity_aperture-> ESTAGIO IDENTITY
 *   setup_vmid_config        -> DELIBERADAMENTE FORA, ver abaixo
 *   program_invalidation     -> ESTAGIO INVRANGE, so a engine 0
 *
 * O que fica de fora, e por que:
 *
 *   setup_vmid_config configura os contextos 1..15, que existem para VMs de
 *   usuario. Este laboratorio submete por VMID 0. Escrever 15 contextos que
 *   ninguem vai usar aumenta a superficie de escrita sem aumentar a prova.
 *
 *   program_invalidation escreve a faixa de endereco das 18 engines de
 *   invalidacao. O passo 4 do plano usa a engine 0. As outras 17 ficam como o
 *   POST deixou.
 *
 * REGRAS QUE O KERNEL IMPOE, e nao sao opcionais:
 *
 *   1. Exige snapshot de transacao aberto (BC250DBG_GCVM_TXN_SNAPSHOT). Sem
 *      caminho de volta provado, nenhuma escrita acontece. Foi exatamente o
 *      erro do 'sdma enginereset' de 2026-08-23: mexer no estado sem checar se
 *      havia como desfazer.
 *   2. Todo registrador que um estagio escreve tem de estar na lista do
 *      snapshot. Se um nao estiver, o estagio inteiro e RECUSADO sem escrever
 *      nada. Isso impede que um estagio novo escape do rollback por descuido.
 *   3. Readback conferido por escrita. Divergencia ABORTA o estagio na hora,
 *      sem seguir para o registrador seguinte.
 *   4. Os estagios TLB, CACHE e CONTEXT0 substituem o bypass do POST, do qual
 *      todo o sucesso medido depende. Exigem BC250DBG_GCVM_PROG_FLAG_DANGER
 *      explicito. Um deles programado errado custa reboot, no melhor caso.
 *
 * DIVERGENCIA DECLARADA em relacao ao upstream: para L2_CNTL3, L2_CNTL4 e
 * L2_CNTL5 o upstream parte da constante de reset (mmGCVM_L2_CNTLn_DEFAULT),
 * que nao existe nos headers que temos. Este codigo parte do valor que o POST
 * deixou e altera apenas os campos que o upstream altera. Nao e equivalencia
 * assumida: e uma diferenca conhecida, registrada, cujo efeito so pode ser
 * julgado pela submissao traduzida do passo 5. */
#define BC250DBG_GCVM_PROG_STAGE_GART     1u
#define BC250DBG_GCVM_PROG_STAGE_SYSTEM   2u
#define BC250DBG_GCVM_PROG_STAGE_TLB      3u   /* PERIGOSO */
#define BC250DBG_GCVM_PROG_STAGE_CACHE    4u   /* PERIGOSO */
#define BC250DBG_GCVM_PROG_STAGE_CONTEXT0 5u   /* PONTO SEM VOLTA */
#define BC250DBG_GCVM_PROG_STAGE_IDENTITY 6u
#define BC250DBG_GCVM_PROG_STAGE_INVRANGE 7u

#define BC250DBG_GCVM_PROG_FLAG_DANGER 0x01u

#define BC250DBG_GCVM_PROG_MAX_WRITES 12u

/* Motivos de recusa, para que a saida diga QUAL regra barrou. Um numero de erro
 * generico manda quem opera adivinhar, e adivinhacao vira MMIO escrito por
 * suposicao. */
#define BC250DBG_GCVM_PROG_DENY_NONE       0u
#define BC250DBG_GCVM_PROG_DENY_NO_TXN     1u  /* snapshot ausente */
#define BC250DBG_GCVM_PROG_DENY_UNCOVERED  2u  /* registrador fora do snapshot */
#define BC250DBG_GCVM_PROG_DENY_NEED_FLAG  3u  /* estagio perigoso sem -danger */
#define BC250DBG_GCVM_PROG_DENY_BAD_STAGE  4u
#define BC250DBG_GCVM_PROG_DENY_BAD_ARG    5u

typedef struct _BC250DBG_GCVM_PROGRAM_IO {
    unsigned long Size, Status;
    unsigned long Stage, Flags;
    /* Entrada. TableVramOffset e o mesmo offset usado pelo 'gcvm ptbuild'; o
     * kernel converte para PA com o mesmo calculo do passo 1, para que nao
     * existam duas aritmeticas de endereco no projeto. */
    unsigned long TableVramOffset;
    unsigned long DummyVramOffset;
    unsigned long long PtStartVa, PtEndVa;
    /* Saida de contexto, lida do silicio. */
    unsigned long FbOffsetReg, FbLocationBase, FbLocationTop;
    unsigned long long VramBaseOffset, PtBasePa, DummyPa;
    /* Saida por escrita. */
    unsigned long WriteCount, Mismatches, DenyReason, DenyOffset;
    unsigned long Offset[BC250DBG_GCVM_PROG_MAX_WRITES];
    unsigned long Before[BC250DBG_GCVM_PROG_MAX_WRITES];
    unsigned long Wanted[BC250DBG_GCVM_PROG_MAX_WRITES];
    unsigned long After[BC250DBG_GCVM_PROG_MAX_WRITES];
    /* ABI 17: bits EXCLUIDOS da conferencia por readback, por escrita.
     *
     * Medido em 2026-08-23 13:40: o estagio 'cache' abortou em GCVM_L2_CNTL2,
     * escrevendo 0x00000003 e lendo 0x00000000. Nao era falha de hardware. Os
     * bits INVALIDATE_ALL_L1_TLBS (0) e INVALIDATE_L2_CACHE (1) sao GATILHOS:
     * escrever 1 dispara a invalidacao e o hardware limpa o bit. O upstream
     * escreve esse registrador e nunca o le de volta.
     *
     * A regra de readback continua valendo para todo o resto do registrador —
     * L2_CNTL2 tem campos de configuracao dos bits 21 ao 30, e esses SAO
     * conferidos. O que a mascara faz e dizer, por escrito e na saida, quais
     * bits nao podem ser confirmados por leitura e por que. Enfraquecer a regra
     * inteira para acomodar dois bits seria trocar precisao por conveniencia.
     *
     * Mesma classe do semaforo de invalidacao: registrador cuja ESCRITA e uma
     * acao, nao uma configuracao. */
    unsigned long IgnoredBits[BC250DBG_GCVM_PROG_MAX_WRITES];
} BC250DBG_GCVM_PROGRAM_IO;

/* Passo 4 do gate de memoria: invalidar a TLB e confirmar o ACK.
 *
 * Sequencia derivada de gmc_v10_0_flush_gpu_tlb(), caminho "antes de KIQ/MES",
 * que e exatamente o caso deste laboratorio. Tres coisas que a leitura do
 * upstream corrigiu no plano original:
 *
 * 1. A ENGINE E A 17, NAO A 0. O comentario do upstream e literal:
 *    "Use register 17 for GART". A engine 0 nao e a da GART.
 *
 * 2. O GFXHUB NAO USA SEMAFORO. gmc_v10_0_use_invalidate_semaphore() devolve
 *    verdadeiro apenas para MMHUB0. O protocolo de adquirir/liberar que este
 *    projeto descobriu por acidente em 2026-08-23 vale para o outro hub — o
 *    'gcvm semrelease' continua correto e necessario porque o dump antigo
 *    adquiriu o semaforo indevidamente, mas a invalidacao em si nao o usa.
 *
 * 3. A LEITURA MUDA DE REQ E OBRIGATORIA NESTE CHIP. O upstream faz uma leitura
 *    descartada de REQ depois de escrever, para GFXHUB com
 *    GC_HWIP < IP_VERSION(10, 3, 0), porque a interface GRBM rapida pode
 *    devolver um ACK falso. GC 10.1.3 esta abaixo de 10.3.0. Sem essa leitura,
 *    um ACK observado nao prova invalidacao.
 *
 * E a SDMA importa aqui por um motivo medido em fonte, nao suposto:
 * sdma_v5_0.c atribui ring->vm_hub = AMDGPU_GFXHUB(0). A SDMA traduz pelo
 * GFXHUB, que e o hub que estes registradores controlam. Se fosse o MMHUB,
 * todo o gate de memoria estaria mexendo no hub errado. */
#define BC250DBG_GCVM_INV_ENGINE 17u

typedef struct _BC250DBG_GCVM_INVALIDATE_IO {
    unsigned long Size, Status;
    unsigned long Vmid, FlushType;
    /* Saida. */
    unsigned long Engine, ReqValue;
    unsigned long AckBefore, AckAfter, AckExpectedMask;
    unsigned long ReqReadback;      /* leitura descartada, registrada por honestidade */
    unsigned long PollIterations, TimedOut;
    unsigned long FaultStatus, FaultAddrLo, FaultAddrHi;
} BC250DBG_GCVM_INVALIDATE_IO;

/* Direct load do CP grafico: PFP, ME e CE.
 *
 * POR QUE DIRECT LOAD E NAO PSP
 *
 * Medido em 2026-08-23 14:21: o SOS desta placa ACEITA o RLC (RespStatus 0) e
 * RECUSA as quatro microengines do CP com 0x80000203. Coerente com o upstream:
 * psp_v11_0_8 nao tem caminho de load_ip_fw para o CP.
 *
 * O MECANISMO, DE gfx_v10_0_cp_gfx_load_pfp_microcode()
 *
 * Nao e streaming de ucode por registrador. A microengine busca o proprio ucode
 * da memoria, pela cache de instrucoes:
 *
 *   1. ucode em memoria alcancavel pela GPU, alinhado a 4 KB;
 *   2. invalidar a cache: IC_OP_CNTL.INVALIDATE_CACHE = 1, esperar
 *      INVALIDATE_CACHE_COMPLETE;
 *   3. IC_BASE_CNTL com VMID = 0, CACHE_POLICY = 0, EXE_DISABLE = 0,
 *      ADDRESS_CLAMP = 1;
 *   4. IC_BASE_LO = endereco & 0xFFFFF000, IC_BASE_HI = 32 bits altos;
 *   5. jump table por UCODE_ADDR/DATA: ADDR = 0, jt_size dwords em DATA,
 *      e por fim ADDR = versao do firmware.
 *
 * O VMID EM IC_BASE_CNTL
 *
 * O upstream programa 0. Se a busca de ucode do CP passa pelo VMID 0 do GFXHUB,
 * o gate de memoria fechado em 2026-08-23 13:49 e pre-requisito disto, e nao
 * trabalho paralelo. Este laboratorio faz o direct load com o bypass do POST
 * ativo, isto e, sem traducao ligada — deliberadamente, para nao misturar duas
 * variaveis de risco na mesma medicao.
 *
 * ENDERECOS, TODOS NO SEGMENTO 1
 *
 * byte = (0xA000 + mmREG_dw) * 4. A base do segmento 1 foi conferida contra
 * registrador vivo em 2026-08-23 17:54 (SCRATCH_REG0..7, dois padroes
 * complementares, controle no segmento 0 intacto). Antes disso ela era classe
 * P1, derivada — e escrever IC_BASE sobre derivacao seria escrever MMIO por
 * suposicao.
 *
 * CP_HYP_*_UCODE_ADDR/DATA sao ALIAS de CP_*_UCODE_ADDR/DATA: gfx_v10_0.c
 * define mmCP_HYP_PFP_UCODE_ADDR = 0x5814 BASE_IDX 1, exatamente o mesmo dword
 * e segmento de mmCP_PFP_UCODE_ADDR. Nomes diferentes, registrador unico.
 *
 * O QUE ESTE CAMINHO NAO FAZ
 *
 * Nao tira nenhuma microengine do HALT. Enquanto CP_ME_CNTL mantiver
 * PFP_HALT (26), CE_HALT (24) e ME_HALT (28), tudo aqui e configuracao inerte:
 * a cache aponta para um lugar e ninguem busca. Medido no POST desta placa:
 * CP_ME_CNTL = 0x15000000, com os tres HALTs ligados. */
#define BC250DBG_CP_ENGINE_PFP 0u
#define BC250DBG_CP_ENGINE_ME  1u
#define BC250DBG_CP_ENGINE_CE  2u
#define BC250DBG_CP_ENGINE_MEC 3u
#define BC250DBG_CP_ENGINE_COUNT 4u

#define BC250DBG_CP_MODE_STAGE      0u  /* copia ucode para VRAM e confere */
#define BC250DBG_CP_MODE_ROLLBACK   1u  /* devolve a regiao de VRAM */
#define BC250DBG_CP_MODE_ICINVAL    2u  /* invalida a cache de instrucoes */
#define BC250DBG_CP_MODE_ICBASE     3u  /* aponta IC_BASE para o ucode */
#define BC250DBG_CP_MODE_JT         4u  /* escreve a jump table */
#define BC250DBG_CP_MODE_TXN_SNAP   5u
#define BC250DBG_CP_MODE_TXN_RESTORE 6u
#define BC250DBG_CP_MODE_TXN_VERIFY 7u
#define BC250DBG_CP_MODE_UNHALT     8u  /* tira as 4 engines do HALT (CP_ME_CNTL=0, CP_MEC_CNTL=0) */
#define BC250DBG_CP_MODE_HALT       9u  /* volta as 4 engines para o HALT (0x15000000, 0x50000000) */
#define BC250DBG_CP_MODE_SAMPLE     10u /* amostra ponteiros de instrucao e status */
/* ABI 22: liga os cinco SOFT_OVERRIDE_* do CGTT_CPF_CLK_CTRL, no endereco
 * correto do segmento 1, para o valor medido no Linux com o CP executando. */
#define BC250DBG_CP_MODE_CLOCKS     11u
/* ABI 23: teste diferencial de escrita: EXE_DISABLE=1 em IC_BASE_CNTL */
#define BC250DBG_CP_MODE_DIFF_TEST  12u
/* ABI 24: pulso de reset das pipes do CP, pelo CP_ME_CNTL */
#define BC250DBG_CP_MODE_PIPE_RESET 13u
/* ABI 24b: teste de invalidacao de I-cache pelo CP_ME_CNTL (bits 4,6,8) */
#define BC250DBG_CP_MODE_ICACHE_MECNTL 14u
/* ABI 29: tabela golden Cyan Skillfish, separada do steady-state do CP. */
#define BC250DBG_CP_MODE_GOLDEN_INIT 15u
#define BC250DBG_CP_MODE_GOLDEN_PREFLIGHT 16u

#define BC250DBG_CP_MAX_UCODE_DWORDS (68u * 1024u)
#define BC250DBG_CP_MAX_JT_DWORDS    256u
#define BC250DBG_CP_UCODE_SAMPLES    4u

/* Registradores de CONFIGURACAO que o direct load e o CP golden init escrevem,
 * cobertos pela transacao (snapshot/restore/verify).
 *
 * ABI 27: 30 registradores (incluindo CPC/MEC, clocks e golden). */
#define BC250DBG_CP_TXN_REG_COUNT 30u
#define BC250DBG_CP_GOLDEN_REG_COUNT 34u
#define BC250DBG_CP_GOLDEN_AUDIT_REG_COUNT 36u /* 34 golden + CPG/CPC GPA */
#define BC250DBG_CP_REPORT_REG_COUNT BC250DBG_CP_GOLDEN_AUDIT_REG_COUNT
#define BC250DBG_CP_MAX_WRITES 40u /* 34 golden + restore do GRBM_GFX_INDEX */
#define BC250DBG_CP_TXN_BAD_SAMPLES 6u

typedef struct _BC250DBG_CP_UCODE_IO {
    unsigned long Size, Status, Mode, Engine;
    /* Entrada comum. */
    unsigned long VramOffset;        /* onde o ucode fica na VRAM, alinhado 4 KB */
    unsigned long DwordCount;        /* dwords do payload (STAGE) ou da jt (JT) */
    unsigned long UcodeVersion;      /* escrito em UCODE_ADDR no fim da jt */

    /* ---- ENTRADA (ABI 25): tudo abaixo TEM de ficar antes de GpuAddr ----
     *
     * O DEFEITO QUE ISTO CORRIGE, E ELE CUSTOU TRES ENSAIOS
     *
     * BcCpUcode() zera a "metade de saida" com
     *
     *     RtlZeroMemory(Io + FIELD_OFFSET(..., GpuAddr), sizeof - offset)
     *
     * Ou seja: GpuAddr e a fronteira, e TUDO depois dela e zerado antes de ser
     * usado. Os campos de entrada das ABIs 21, 23 e 24 foram acrescentados no
     * FIM da estrutura, ou seja, do lado errado da fronteira. Chegavam do user
     * mode preenchidos e eram zerados pelo driver antes da primeira leitura.
     *
     * Consequencia, medida e conferida na evidencia:
     *
     *   ABI 21 / 02:07  'cp icbase -va' escreveu o endereco FISICO. O ensaio de
     *                   VA traduzida nunca testou VA. A evidencia mostrava
     *                   "queria 0x0E000000 / 0x000000F4" e eu li por cima.
     *   ABI 23 / 08:45  o A/B do EXE_DISABLE rodou com o bit em 0 nas DUAS
     *                   passagens. "Identico bit a bit" comparava configuracoes
     *                   identicas.
     *   ABI 24 / 13:03  o pulso de reset nunca assertou: escreveu 0x15000000 nos
     *                   dois lados. A hipotese do dominio de reset NAO foi
     *                   testada.
     *
     * A guarda C_ASSERT no fim deste arquivo impede a reincidencia: campo de
     * entrada depois de GpuAddr passa a ser erro de compilacao, nao ensaio
     * perdido. */

    /* ABI 21: endereco explicito para o IC_BASE, em vez do fisico derivado.
     * O caso de uso e apontar para uma VA que a page table do VMID 0 resolve,
     * reproduzindo o que o amdgpu faz com endereco de GTT.
     * OverrideValid = 0 mantem o comportamento anterior, byte a byte. */
    unsigned long OverrideValid;
    unsigned long long OverrideGpuAddr;

    /* ABI 23: EXE_DISABLE do IC_BASE_CNTL, para o teste diferencial.
     * IC_BASE_* e write-only, entao nenhum readback confirma aceitacao. Escrever
     * dois valores que o hardware deveria tratar de forma diferente e comparar o
     * COMPORTAMENTO e o unico jeito de medir efeito: PC diferente entre 0 e 1
     * prova que a escrita chega; identico e evidencia de que nao produz efeito. */
    unsigned long ExeDisable;

    /* ABI 24: pulso de reset das pipes pelo CP_ME_CNTL.
     * Mascaras do gc_10_1_0_sh_mask.h completo:
     *   CE/PFP/ME_INVALIDATE_ICACHE  bits 4,6,8    0x00000150
     *   CE/PFP/ME_PIPE0/1_RESET      bits 16..21   0x003F0000
     * O dominio de reset das microengines esta DENTRO do CP_ME_CNTL — nosso
     * 0x15000000 tem os tres HALT e zero reset.
     * PipeResetAssert: 1 assere os seis bits mantendo os HALT; 0 limpa. */
    unsigned long PipeResetAssert;
    /* Saida de contexto. */
    unsigned long long GpuAddr;      /* endereco de GPU do ucode */
    unsigned long RegBaseLo, RegBaseHi, RegBaseCntl, RegOpCntl;
    unsigned long RegUcodeAddr, RegUcodeData;
    /* STAGE / ROLLBACK */
    unsigned long DwordsWritten, Mismatches;
    unsigned long FirstMismatchIndex, FirstMismatchGot, FirstMismatchWant;
    unsigned long SnapshotPresent, SnapshotDwords;
    unsigned long Sample[BC250DBG_CP_UCODE_SAMPLES];
    /* ICINVAL */
    unsigned long OpCntlBefore, OpCntlAfterWrite, OpCntlAfterWait;
    unsigned long PollIterations, TimedOut;
    /* ICBASE / JT / GOLDEN: antes, querido, depois, por escrita. */
    unsigned long WriteCount;
    unsigned long WrOffset[BC250DBG_CP_MAX_WRITES];
    unsigned long WrBefore[BC250DBG_CP_MAX_WRITES];
    unsigned long WrWanted[BC250DBG_CP_MAX_WRITES];
    unsigned long WrAfter[BC250DBG_CP_MAX_WRITES];
    unsigned long WrWriteOnly[BC250DBG_CP_MAX_WRITES];
    /* ICINVAL: o COMPLETE e legivel neste hardware? */
    unsigned long InvConfirmavel;
    /* Transacao. */
    unsigned long RegisterCount, Restored;
    unsigned long BadOffset[BC250DBG_CP_TXN_BAD_SAMPLES];
    unsigned long BadGot[BC250DBG_CP_TXN_BAD_SAMPLES];
    unsigned long BadWant[BC250DBG_CP_TXN_BAD_SAMPLES];
    /* ABI 29: capacidade para o preflight readonly das 34 golden + 2 GPA. */
    unsigned long Offsets[BC250DBG_CP_REPORT_REG_COUNT];
    unsigned long Values[BC250DBG_CP_REPORT_REG_COUNT];
    /* Estado do CP, lido em toda operacao. */
    unsigned long CpMeCntl, CpStat, GrbmStatus, GrbmStatus2;
    unsigned long PfpInstrPntr, MeInstrPntr, CeInstrPntr, Mec1InstrPntr;
    /* ABI 26: Status de carga da Jump Table nas microengines (JT_LOADED). */
    unsigned long PfpJtStat, CeJtStat, MecJtStat;
    /* O payload segue esta estrutura, DwordCount dwords, como no LOAD_IP_FW. */
} BC250DBG_CP_UCODE_IO;

/* GUARDA DE COMPILACAO (ABI 25)
 *
 * BcCpUcode() zera de GpuAddr ate o fim da estrutura. Todo campo de ENTRADA tem
 * de ficar ANTES dessa fronteira. Acrescentar um campo de entrada depois dela
 * compila hoje e falha silenciosamente em hardware — foi o que aconteceu com as
 * ABIs 21, 23 e 24, e custou tres ensaios com boot limpo cada.
 *
 * Estas linhas transformam esse erro em erro de compilacao. Ao acrescentar um
 * campo de entrada novo, acrescente tambem a linha correspondente aqui. */
#ifdef C_ASSERT
C_ASSERT(FIELD_OFFSET(BC250DBG_CP_UCODE_IO, OverrideValid) <
         FIELD_OFFSET(BC250DBG_CP_UCODE_IO, GpuAddr));
C_ASSERT(FIELD_OFFSET(BC250DBG_CP_UCODE_IO, OverrideGpuAddr) <
         FIELD_OFFSET(BC250DBG_CP_UCODE_IO, GpuAddr));
C_ASSERT(FIELD_OFFSET(BC250DBG_CP_UCODE_IO, ExeDisable) <
         FIELD_OFFSET(BC250DBG_CP_UCODE_IO, GpuAddr));
C_ASSERT(FIELD_OFFSET(BC250DBG_CP_UCODE_IO, PipeResetAssert) <
         FIELD_OFFSET(BC250DBG_CP_UCODE_IO, GpuAddr));
#endif

/* Bring-up do RLC.
 *
 * POR QUE O RLC E O BLOQUEIO
 *
 * Medido em 2026-08-23 23:07, contra a referencia do Linux com o CP executando:
 *
 *   RLC_GPM_STAT.RLC_EXEC_ROM_CODE   Windows = 1     Linux = 0
 *   RLC_CNTL.RLC_ENABLE_F32          Windows = 0     Linux = 1
 *   RLC_CGCG_CGLS_CTRL               Windows = 0x0001003C   Linux = 0
 *
 * RLC_EXEC_ROM_CODE = 1 significa que o RLC esta executando o bootloader
 * interno, sem microcodigo. E o analogo exato do F32 da SDMA em HALT, e e prova
 * fisica — nao inferencia — de que o RLC nunca foi inicializado aqui.
 *
 * A MESMA MEDICAO FECHOU OUTRA QUESTAO
 *
 * A vizinhanca CP_*_UCODE_* / CP_*_IC_* le 0xFFFFFFFF nos DOIS sistemas, e o CP
 * funciona no Linux. Logo aqueles registradores sao write-only, ou nao estao no
 * caminho. Nao eram o bloqueio, e a investigacao deles se encerrou.
 *
 * O mesmo vale para RLC_GPM_UCODE_ADDR/DATA: leem 0xFFFFFFFF nos dois, e o
 * upstream os usa para carregar o ucode do RLC. Sao portas de escrita.
 *
 * SEQUENCIA, DE gfx_v10_0_rlc_resume() NO CAMINHO SEM AUTOLOAD
 *
 *   rlc_stop()                     RLC_CNTL.RLC_ENABLE_F32 = 0
 *   WREG32(RLC_CGCG_CGLS_CTRL, 0)  desliga clock gating de granularidade grossa
 *   WREG32(RLC_PG_CNTL, 0)         desliga power gating
 *   rlc_load_microcode()           RLC_GPM_UCODE_ADDR = 0x2000, ucode em DATA,
 *                                  e por fim ADDR = versao do firmware
 *   init_csb()                     CSIB, precisa de buffer de clear state
 *   update_spm_vmid_internal()
 *   rlc_start()                    RLC_CNTL.RLC_ENABLE_F32 = 1
 *
 * O QUE ESTE LABORATORIO FAZ, E O QUE NAO FAZ
 *
 * Faz stop, CGCG, PG, load de ucode e start, cada um como operacao separada e
 * auditavel, com transacao cobrindo os registradores de configuracao.
 *
 * NAO faz init_csb nem update_spm_vmid: os dois precisam de estruturas que este
 * laboratorio nao monta (clear state buffer). Se o RLC nao sair da ROM sem eles,
 * a medicao vai dizer, e a lacuna esta declarada aqui em vez de descoberta
 * depois.
 *
 * O GATE
 *
 * A prova nao e escrita aceita: e RLC_EXEC_ROM_CODE cair de 1 para 0. Este
 * projeto ja mediu quanto vale uma escrita aceita sem prova de execucao. */
#define BC250DBG_RLC_MODE_STATUS      0u
#define BC250DBG_RLC_MODE_TXN_SNAP    1u
#define BC250DBG_RLC_MODE_TXN_RESTORE 2u
#define BC250DBG_RLC_MODE_TXN_VERIFY  3u
#define BC250DBG_RLC_MODE_STOP        4u  /* RLC_ENABLE_F32 = 0 */
#define BC250DBG_RLC_MODE_CGCG_OFF    5u  /* RLC_CGCG_CGLS_CTRL = 0 */
#define BC250DBG_RLC_MODE_PG_OFF      6u  /* RLC_PG_CNTL = 0 */
#define BC250DBG_RLC_MODE_LOAD_UCODE  7u  /* ADDR/DATA, payload no fim da struct */
#define BC250DBG_RLC_MODE_START       8u  /* RLC_ENABLE_F32 = 1 */
#define BC250DBG_RLC_MODE_SRM_ENABLE  9u  /* RLC_SRM_CNTL, save/restore machine */

#define BC250DBG_RLC_MAX_UCODE_DWORDS (32u * 1024u)
#define BC250DBG_RLC_UCODE_START_ADDR 0x00002000u  /* RLCG_UCODE_LOADING_START_ADDRESS */

/* Registradores de CONFIGURACAO que a sequencia escreve, e portanto os unicos
 * que a transacao precisa cobrir.
 *
 * Ficam FORA, porque escrever neles e ACAO e nao configuracao:
 *   RLC_GPM_UCODE_ADDR   trava de endereco da porta de escrita
 *   RLC_GPM_UCODE_DATA   porta de escrita do ucode
 *   GRBM_SOFT_RESET      pulso de reset
 * Restaurar qualquer um desses seria acao disfarcada de restauracao. */
#define BC250DBG_RLC_TXN_REG_COUNT 4u
#define BC250DBG_RLC_TXN_BAD_SAMPLES 4u

#define BC250DBG_RLC_WRITES 4u

typedef struct _BC250DBG_RLC_IO {
    unsigned long Size, Status, Mode;
    unsigned long DwordCount;       /* LOAD_UCODE: dwords do payload */
    unsigned long UcodeVersion;     /* LOAD_UCODE: escrito em ADDR no fim */
    /* Estado, lido em toda operacao. */
    unsigned long Cntl, GpmStat, CgcgCglsCntl, PgCntl, SafeMode, SrmCntl;
    unsigned long GrbmStatus, GrbmStatus2, CgttCpfClkCtrl;
    /* Decodificacao do que importa, para a saida nao depender de quem le. */
    unsigned long EnableF32, ExecRomCode, RlcBusy;
    /* Escritas conferidas: antes, querido, depois. */
    unsigned long WriteCount, Mismatches;
    unsigned long WrOffset[BC250DBG_RLC_WRITES];
    unsigned long WrBefore[BC250DBG_RLC_WRITES];
    unsigned long WrWanted[BC250DBG_RLC_WRITES];
    unsigned long WrAfter[BC250DBG_RLC_WRITES];
    /* LOAD_UCODE. */
    unsigned long DwordsWritten, AddrReadback;
    /* Transacao. */
    unsigned long SnapshotPresent, RegisterCount, Restored;
    unsigned long BadOffset[BC250DBG_RLC_TXN_BAD_SAMPLES];
    unsigned long BadGot[BC250DBG_RLC_TXN_BAD_SAMPLES];
    unsigned long BadWant[BC250DBG_RLC_TXN_BAD_SAMPLES];
    unsigned long Offsets[BC250DBG_RLC_TXN_REG_COUNT];
    unsigned long Values[BC250DBG_RLC_TXN_REG_COUNT];
    /* O payload do ucode segue esta estrutura, como no LOAD_IP_FW. */
} BC250DBG_RLC_IO;

typedef struct _BC250DBG_GCVM_INIT_IO {
    unsigned long Size, Context0CntlBefore, Context0CntlAfter, L2CntlAfter, Status;
} BC250DBG_GCVM_INIT_IO;

#define BC250DBG_GFX_SUBMIT_MAX_DW 256u
typedef struct _BC250DBG_GFX_INIT_IO {
    unsigned long Size, RingSizeBytes, VramOffset, RbCntl;
    unsigned long long GpuAddr;
    unsigned long MeCntlBefore, MeCntlAfter, Status;
} BC250DBG_GFX_INIT_IO;
typedef struct _BC250DBG_GFX_SUBMIT_IO {
    unsigned long Size, DwordCount, Data[BC250DBG_GFX_SUBMIT_MAX_DW];
    unsigned long RptrBefore, RptrAfter, Wptr, Completed, Status;
    /* Diagnostico de estado das microengines do CP durante a submissao */
    unsigned long CpMeCntl, CpMecCntl, CpStat;
    unsigned long PfpInstrPntr, MeInstrPntr, CeInstrPntr, Mec1InstrPntr;
    unsigned long PfpJtStat, CeJtStat, MecJtStat;
} BC250DBG_GFX_SUBMIT_IO;
typedef struct _BC250DBG_GFX_STATUS_IO {
    unsigned long Size, MeCntl, Rb0Cntl;
    unsigned long long Rb0Base;
    unsigned long Rb0Wptr, Rb0Rptr, Rb0RptrWb, GrbmStatus;
} BC250DBG_GFX_STATUS_IO;

#endif /* BC250DBG_ABI_H */
