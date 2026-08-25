/* bc250dbg_abi.h — ABI compartilhada entre bc250dbg.sys (KMD) e o user mode.
 *
 * Projeto BC250-WIN — fase 1 (instrumentacao).
 * Este header e incluido tanto pelo driver quanto pelo Studio/CLI: nao pode
 * depender de nada alem de tipos inteiros fixos e das macros de CTL_CODE.
 *
 * Regras:
 *   - Tudo little-endian, sem padding implicito (structs alinhadas a mao).
 *   - Toda struct de IOCTL comeca com Size para permitir evolucao da ABI.
 *   - Versao da ABI muda sempre que um campo existente muda de significado.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BC250DBG_ABI_H
#define BC250DBG_ABI_H

#if defined(_KERNEL_MODE) || defined(BC250_KERNEL_MODE) || defined(_NTDDK_) || defined(_WDMDDK_)
#include <ntddk.h>
#ifndef _STDINT
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef __int64            int64_t;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned __int64   uint64_t;
#define _STDINT
#endif
#else
#include <windows.h>
#include <winioctl.h>
#include <stdint.h>
#endif

#define BC250DBG_ABI_VERSION        2
#define BC250DBG_DRIVER_VERSION     0x00010000u   /* 1.0.0 */

#define BC250DBG_NT_DEVICE_NAME     L"\\Device\\bc250dbg"
#define BC250DBG_DOS_DEVICE_NAME    L"\\DosDevices\\bc250dbg"
#define BC250DBG_USER_DEVICE_PATH   "\\\\.\\bc250dbg"

/* ------------------------------------------------------------------------ */
/* Identificacao do hardware alvo                                            */
/* ------------------------------------------------------------------------ */

#define BC250_PCI_VENDOR_ID         0x1002u
#define BC250_PCI_DEVICE_ID         0x13FEu
/* BAR5 = janela de registradores da GPU. Valor observado em hardware:
 * 0xFE800000, 512 KB. O driver descobre pelo PCI config; estes valores sao
 * apenas fallback e sanity check. */
#define BC250_BAR5_INDEX            5u
#define BC250_BAR5_FALLBACK_PA      0xFE800000ull
#define BC250_BAR5_DEFAULT_LEN      0x80000u

/* BAR0 = abertura de frame buffer (VRAM). Medido nesta placa em 2026-08-19:
 * base 0xC0000000, 256 MB — deduzido da janela da ponte PCI (0xC0000000..
 * 0xD01FFFFF, 258 MB) menos os 2 MB do BAR2 em 0xD0000000. Ela janela os
 * primeiros 256 MB dos 512 MB de VRAM.
 *
 * A VRAM comeca no endereco de GPU 0xF400000000 (GCMC_VM_FB_LOCATION_BASE
 * 0xF400 << 24), e a abertura de sistema vai ate 0xF420000000. Enderecos
 * dentro dessa faixa sao os unicos que a SDMA consegue traduzir sem GART:
 *
 *     endereco_gpu = BC250_VRAM_GPU_BASE + offset_na_vram
 */
#define BC250_BAR0_INDEX            0u
#define BC250_BAR0_DEFAULT_LEN      0x10000000u   /* 256 MB */
/* A VRAM comeca no endereco de GPU que o proprio hardware declara em
 * GCMC_VM_FB_LOCATION_BASE, medido nesta placa como 0xF400 << 24. Ja tentamos
 * mover isso para 0 numa sessao anterior; a mudanca introduziu um XNACK de
 * leitura na busca do anel que nao existia antes, porque a abertura de sistema
 * deixou de cobrir o frame buffer. Ou se move tudo (incluindo FB_LOCATION),
 * ou nao se move nada. */
#define BC250_VRAM_GPU_BASE         0xF400000000ull
#define BC250_VRAM_TOTAL_BYTES      0x20000000ull /* 512 MB */

/* Onde colocamos o ring buffer da SDMA dentro da VRAM. 255 MB — perto do fim
 * da janela do BAR0 (256 MB) e bem longe do frame buffer do Adaptador de Video
 * Basico, que fica no comeco da VRAM. Alinhado a 256 bytes porque RB_BASE
 * guarda o endereco deslocado de 8 bits. */
#define BC250_SDMA_RING_VRAM_DEFAULT 0x0FF00000u

/* O anel nao e a unica coisa que precisa morar na VRAM. O amdgpu tambem
 * mantem em memoria:
 *
 *   - o writeback do RPTR: sdma_v5_0_ring_get_rptr le o RPTR do BUFFER, nao
 *     do registrador. O registrador nao e fonte de verdade — medimos nesta
 *     placa que ele apenas acompanha o WPTR, inclusive para pacotes
 *     incompletos que a engine nao poderia ter processado.
 *
 *   - o shadow do WPTR, com F32_POLL_ENABLE ligado: e assim que o
 *     microcontrolador percebe que ha trabalho novo.
 *
 * Ambos ficam logo depois do anel, ainda dentro da janela do BAR0. */
#define BC250_SDMA_RPTR_WB_OFFSET    (BC250_SDMA_RING_VRAM_DEFAULT + 0x1000u)
#define BC250_SDMA_WPTR_SHADOW_OFFSET (BC250_SDMA_RING_VRAM_DEFAULT + 0x2000u)

/* Janela SMN pela NBIO: indice em BAR5+0x38, dado em BAR5+0x3C.
 * Equivalente ao WREG32_PCIE/RREG32_PCIE do amdgpu. */
#define BC250_SMN_INDEX_OFFSET      0x38u
#define BC250_SMN_DATA_OFFSET       0x3Cu

/* Mailboxes do SMU (enderecos SMN). Tres filas observadas na BC-250. */
#define BC250_SMU_Q0_CMD            0x03B10A08u   /* C2PMSG_66 */
#define BC250_SMU_Q0_ARG            0x03B10A48u   /* C2PMSG_82 */
#define BC250_SMU_Q0_RSP            0x03B10A68u   /* C2PMSG_90 */
#define BC250_SMU_Q2_CMD            0x03B10528u
#define BC250_SMU_Q2_RSP            0x03B10564u
#define BC250_SMU_Q2_ARG            0x03B10998u
#define BC250_SMU_Q2_ARG_HI         0x03B1099Cu
#define BC250_SMU_Q3_CMD            0x03B10A20u
#define BC250_SMU_Q3_ARG            0x03B10A88u
#define BC250_SMU_Q3_RSP            0x03B10A80u

#define BC250_SMN_FW_FLAGS          0x03B10024u
#define BC250_SMN_PUB_CTRL          0x03B10B14u

/* Sondas de sensor cruas (escala NAO verificada — ver thermal.c). */
#define BC250_SMN_TEMP_EDGE         0x03B10000u
#define BC250_SMN_TEMP_JUNCTION     0x03B10020u
#define BC250_SMN_TEMP_MEM          0x03B10028u
#define BC250_SMN_FAN_RPM           0x03B10064u
#define BC250_SMN_FAN_PWM           0x03B10068u

/* Respostas do mailbox SMU (PPSMC_Result_*). */
#define BC250_SMU_OK                0x01u
#define BC250_SMU_FAILED            0xFFu
#define BC250_SMU_UNKNOWN_CMD       0xFEu
#define BC250_SMU_REJECTED_PREREQ   0xFDu
#define BC250_SMU_REJECTED_BUSY     0xFCu

/* ------------------------------------------------------------------------ */
/* Niveis de armamento — o modelo de seguranca do driver                     */
/*                                                                           */
/* O historico do projeto anterior registra uma trava de maquina causada por  */
/* ForceGfxFreq(0x39) enviado sem tabelas DPM. O driver comeca READONLY e so  */
/* aceita escrita depois de um armamento explicito, que fica registrado no    */
/* log. Nao existe caminho implicito para o nivel UNSAFE.                     */
/* ------------------------------------------------------------------------ */

#define BC250DBG_ARM_READONLY       0u  /* padrao ao carregar: so leitura     */
#define BC250DBG_ARM_WRITE          1u  /* escrita em BAR5/SMN + SMU seguro   */
#define BC250DBG_ARM_UNSAFE         2u  /* mensagens SMU de risco (clock/vid) */

/* Confirmacao obrigatoria para BC250DBG_ARM_UNSAFE ("IKNOW" em ascii). */
#define BC250DBG_ARM_MAGIC          0x494B4E4Fu

/* ------------------------------------------------------------------------ */
/* Log ring — layout do buffer compartilhado (mapeado read-only no usuario)   */
/* ------------------------------------------------------------------------ */

#define BC250DBG_LOG_MAGIC          0x4C475242u   /* 'BRGL' */
#define BC250DBG_LOG_ENTRY_SIZE     128u
#define BC250DBG_LOG_TEXT_LEN       64u
#define BC250DBG_LOG_HEADER_SIZE    4096u         /* entradas comecam aqui   */
#define BC250DBG_LOG_DEFAULT_COUNT  32768u        /* 32k * 128 B = 4 MB      */

/* Niveis. Mascara de bits para filtro em runtime. */
#define BC250DBG_LVL_ERROR          0x01u
#define BC250DBG_LVL_WARN           0x02u
#define BC250DBG_LVL_INFO           0x04u
#define BC250DBG_LVL_TRACE          0x08u   /* toda leitura/escrita de reg   */
#define BC250DBG_LVL_ALL            0x0Fu

/* Subsistemas. Mascara de bits para filtro em runtime. */
#define BC250DBG_SUB_CORE           0x0001u
#define BC250DBG_SUB_PCI            0x0002u
#define BC250DBG_SUB_REG            0x0004u
#define BC250DBG_SUB_SMN            0x0008u
#define BC250DBG_SUB_SMU            0x0010u
#define BC250DBG_SUB_THERMAL        0x0020u
#define BC250DBG_SUB_SCRIPT         0x0040u
#define BC250DBG_SUB_PSP            0x0080u
#define BC250DBG_SUB_GC             0x0100u
#define BC250DBG_SUB_SDMA           0x0200u
#define BC250DBG_SUB_ALL            0xFFFFu

/* Eventos com identidade estavel: o texto vive no user mode, o kernel so
 * emite o id + argumentos. Assim o caminho quente nao formata string. */
#define BC250DBG_EV_TEXT            0u   /* usa o campo Text                 */
#define BC250DBG_EV_REG_READ        1u   /* A0=off A1=val                    */
#define BC250DBG_EV_REG_WRITE       2u   /* A0=off A1=val A2=readback        */
#define BC250DBG_EV_SMN_READ        3u   /* A0=addr A1=val                   */
#define BC250DBG_EV_SMN_WRITE       4u   /* A0=addr A1=val A2=readback       */
#define BC250DBG_EV_SMU_MSG         5u   /* A0=queue A1=msg A2=arg A3=status */
#define BC250DBG_EV_SMU_REFUSED     6u   /* A0=queue A1=msg A2=motivo        */
#define BC250DBG_EV_ARM_CHANGE      7u   /* A0=antes A1=depois               */
#define BC250DBG_EV_THERMAL_SAMPLE  8u   /* A0=edge A1=junc A2=mem A3=milliC */
#define BC250DBG_EV_THERMAL_TRIP    9u   /* A0=milliC A1=limite              */
#define BC250DBG_EV_SCRIPT_BEGIN    10u  /* A0=nOps                          */
#define BC250DBG_EV_SCRIPT_OP       11u  /* A0=idx A1=op A2=valor A3=status  */
#define BC250DBG_EV_SCRIPT_END      12u  /* A0=nOps A1=falhas A2=status      */
#define BC250DBG_EV_LOG_DROP        13u  /* A0=total descartado              */

#pragma pack(push, 8)

typedef struct _BC250DBG_LOG_ENTRY {
    uint64_t Seq;        /* numero de sequencia; 0 = entrada nao publicada  */
    uint64_t Qpc;        /* KeQueryPerformanceCounter                       */
    uint32_t Level;
    uint32_t Subsys;
    uint32_t EventId;
    uint32_t Irql;       /* IRQL no momento da emissao                      */
    uint64_t A[4];
    char     Text[BC250DBG_LOG_TEXT_LEN];
} BC250DBG_LOG_ENTRY;    /* == BC250DBG_LOG_ENTRY_SIZE bytes */

typedef struct _BC250DBG_LOG_HEADER {
    uint32_t Magic;
    uint32_t Version;
    uint32_t EntrySize;
    uint32_t EntryCount;      /* potencia de dois                            */
    int64_t  Head;            /* sequencias reivindicadas (produtor)         */
    int64_t  Dropped;         /* reservado; a perda por lap e calculada pelo
                               * consumidor: Head - Tail > EntryCount        */
    uint64_t QpcFrequency;
    uint32_t EntryOffset;     /* BC250DBG_LOG_HEADER_SIZE                    */
    uint32_t Reserved[9];
} BC250DBG_LOG_HEADER;

/* ------------------------------------------------------------------------ */
/* Motor de scripts — ops montadas no user mode, executadas dentro do KMD    */
/* ------------------------------------------------------------------------ */

#define BC250DBG_OP_END         0u
#define BC250DBG_OP_REG_R       1u   /* A=offset                            */
#define BC250DBG_OP_REG_W       2u   /* A=offset  B=valor                   */
#define BC250DBG_OP_SMN_R       3u   /* A=endereco                          */
#define BC250DBG_OP_SMN_W       4u   /* A=endereco B=valor                  */
#define BC250DBG_OP_SMU         5u   /* A=fila B=msg C=arg D=argHi          */
#define BC250DBG_OP_DELAY_US    6u   /* A=microssegundos                    */
#define BC250DBG_OP_POLL_REG    7u   /* A=off B=mascara C=valor D=timeout us*/
#define BC250DBG_OP_POLL_SMN    8u   /* idem para SMN                       */
#define BC250DBG_OP_EXPECT_REG  9u   /* A=off B=mascara C=valor esperado    */
#define BC250DBG_OP_EXPECT_SMN  10u
#define BC250DBG_OP_MARK        11u  /* A=id livre; marca no log            */
#define BC250DBG_OP_THERMAL_CHK 12u  /* aborta se sensor confiavel > limite */

/* Flags por op. */
#define BC250DBG_OPF_CONTINUE   0x0001u  /* falha nao aborta o script        */
#define BC250DBG_OPF_NOLOG      0x0002u  /* nao emitir TRACE (loops longos)  */

typedef struct _BC250DBG_OP {
    uint32_t Op;
    uint32_t Flags;
    uint64_t A, B, C, D;
} BC250DBG_OP;

/* Status por op. */
#define BC250DBG_OPS_OK         0u
#define BC250DBG_OPS_MISMATCH   1u   /* expect falhou                        */
#define BC250DBG_OPS_TIMEOUT    2u   /* poll estourou                        */
#define BC250DBG_OPS_DENIED     3u   /* nivel de armamento insuficiente      */
#define BC250DBG_OPS_RANGE      4u   /* offset fora do BAR                   */
#define BC250DBG_OPS_ABORTED    5u   /* abortado (usuario ou termico)        */
#define BC250DBG_OPS_BADOP      6u

typedef struct _BC250DBG_OP_RESULT {
    uint32_t Status;
    uint32_t Reserved;
    uint64_t Value;      /* valor lido, ou status do SMU                     */
    uint64_t ElapsedUs;
} BC250DBG_OP_RESULT;

#define BC250DBG_SCRIPT_MAX_OPS 4096u

typedef struct _BC250DBG_SCRIPT_IN {
    uint32_t Size;
    uint32_t OpCount;
    uint32_t Flags;
    uint32_t ThermalLimitC;  /* 0 = usa o limite global do driver            */
    char     Name[32];
    /* seguido por OpCount * BC250DBG_OP */
} BC250DBG_SCRIPT_IN;

typedef struct _BC250DBG_SCRIPT_OUT {
    uint32_t Size;
    uint32_t OpCount;        /* ops efetivamente executadas                  */
    uint32_t Failures;
    uint32_t Status;         /* BC250DBG_OPS_* do primeiro erro fatal        */
    uint64_t ElapsedUs;
    /* seguido por OpCount * BC250DBG_OP_RESULT */
} BC250DBG_SCRIPT_OUT;

/* ------------------------------------------------------------------------ */
/* Structs de IOCTL                                                          */
/* ------------------------------------------------------------------------ */

#define BC250DBG_MAX_BARS 6

typedef struct _BC250DBG_BAR_INFO {
    uint64_t Base;
    uint32_t Raw;
    uint32_t Flags;      /* bit0 = io space, bit1 = 64 bits, bit2 = mapeado  */
} BC250DBG_BAR_INFO;

typedef struct _BC250DBG_INFO {
    uint32_t Size;
    uint32_t AbiVersion;
    uint32_t DriverVersion;
    uint32_t ArmLevel;

    uint32_t PciBus, PciDev, PciFn;
    uint32_t VendorId, DeviceId, RevisionId;
    uint32_t PciFound;          /* 0 = usando fallback hardcoded             */

    BC250DBG_BAR_INFO Bar[BC250DBG_MAX_BARS];

    uint64_t MmioPhysical;
    uint32_t MmioLength;
    uint32_t MmioMapped;

    /* Os pares A/B existem para a tarefa 0.6: a convencao de offset do BAR5.
     * A: byte = base_ip + reg        (usada pelo projeto anterior)
     * B: byte = (base_ip + reg) * 4  (a do amdgpu)
     * Ver docs/ORACULO-LINUX-2026-08-18.md. */
    uint32_t GpuId;             /* BAR5+0x0000, independente de convencao    */
    uint32_t ScratchReg0;       /* SOC15 correto: 0x30100                    */
    uint32_t ScratchReg0B;      /* legado errado: 0x32D4                     */
    uint32_t GrbmStatus;        /* SOC15 correto: 0x08010                    */
    uint32_t GrbmStatusB;       /* legado errado: 0x3260                      */
    uint32_t CcShaderArray;     /* SOC15 correto: 0x089BC — Linux le 0xFFF80000 */
    uint32_t CcShaderArrayB;    /* legado errado: 0x3264                      */

    uint32_t SmuAlive;
    uint32_t SmuVersion;
    uint32_t SmuDriverIf;
    uint32_t SmuFeatures;
    uint32_t GfxFreqMhz;
    uint32_t GfxVid;
    uint32_t ActiveWgp;

    uint32_t LogEntryCount;
    uint32_t LogEntrySize;
    int64_t  LogHead;
    int64_t  LogDropped;

    uint32_t ThermalTrusted;
    uint32_t ThermalLimitC;
    uint32_t ThermalTripped;
    int32_t  ThermalMilliC;
} BC250DBG_INFO;

typedef struct _BC250DBG_REG_IO {
    uint32_t Size;
    uint32_t Offset;      /* BAR5: offset em bytes; SMN: endereco            */
    uint32_t Value;
    uint32_t Status;      /* BC250DBG_OPS_*                                  */
} BC250DBG_REG_IO;

typedef struct _BC250DBG_SMU_IO {
    uint32_t Size;
    uint32_t Queue;       /* 0, 2 ou 3                                       */
    uint32_t Msg;
    uint32_t Arg;
    uint32_t ArgHi;
    uint32_t TimeoutMs;   /* 0 = padrao (1000)                               */
    uint32_t Status;      /* BC250_SMU_* ou 0 em timeout                     */
    uint32_t Result;      /* leitura do registrador de argumento             */
} BC250DBG_SMU_IO;

typedef struct _BC250DBG_ARM_IO {
    uint32_t Size;
    uint32_t Level;
    uint32_t Magic;       /* BC250DBG_ARM_MAGIC para o nivel UNSAFE          */
    uint32_t Status;
} BC250DBG_ARM_IO;

typedef struct _BC250DBG_LOG_MAP_OUT {
    uint32_t Size;
    uint32_t EntryCount;
    uint32_t EntrySize;
    uint32_t TotalBytes;
    uint64_t UserAddress;
} BC250DBG_LOG_MAP_OUT;

typedef struct _BC250DBG_LOG_MASK_IN {
    uint32_t Size;
    uint32_t LevelMask;
    uint32_t SubsysMask;
    uint32_t Reserved;
} BC250DBG_LOG_MASK_IN;

#define BC250DBG_SPACE_BAR  0u
#define BC250DBG_SPACE_SMN  1u
#define BC250DBG_SPACE_VRAM 2u   /* offset dentro da abertura BAR0 */

#define BC250DBG_SNAP_MAX   4096u

typedef struct _BC250DBG_SNAP_IN {
    uint32_t Size;
    uint32_t Space;       /* BC250DBG_SPACE_*                                */
    uint32_t Start;       /* offset/endereco inicial (alinhado a 4)          */
    uint32_t Count;       /* numero de dwords                                */
} BC250DBG_SNAP_IN;

typedef struct _BC250DBG_SNAP_OUT {
    uint32_t Size;
    uint32_t Count;
    /* seguido por Count * uint32_t */
} BC250DBG_SNAP_OUT;

typedef struct _BC250DBG_THERMAL {
    uint32_t Size;
    uint32_t RawEdge, RawJunction, RawMem;
    uint32_t FanRpm, FanPwm;
    int32_t  MilliC;      /* valor convertido; so vale se Trusted != 0       */
    uint32_t Trusted;     /* 0 ate calibrar contra o Linux — ver docs        */
    uint32_t LimitC;
    uint32_t Tripped;
    uint32_t ScaleNum, ScaleDen;
    int32_t  OffsetMilliC;
    uint32_t Source;      /* 0=edge 1=junction 2=mem                       */
} BC250DBG_THERMAL;

typedef struct _BC250DBG_THERMAL_SET {
    uint32_t Size;
    uint32_t LimitC;
    uint32_t Trusted;     /* 1 = declaro o sensor calibrado                  */
    uint32_t ScaleNum, ScaleDen;
    int32_t  OffsetMilliC;
    uint32_t Source;      /* 0=edge 1=junction 2=mem                         */
    uint32_t Reserved;
} BC250DBG_THERMAL_SET;

typedef struct _BC250DBG_PCICFG_IO {
    uint32_t Size;
    uint32_t Bus, Dev, Fn;
    uint32_t Offset;
    uint32_t Value;
    uint32_t Status;
} BC250DBG_PCICFG_IO;

/* Onde fica o ring buffer da SDMA. */
#define BC250DBG_SDMA_RING_SYSRAM 0u  /* RAM do sistema (exige GART/VMID0)   */
#define BC250DBG_SDMA_RING_VRAM   1u  /* VRAM via BAR0 (traduz sem GART)     */

typedef struct _BC250DBG_SDMA_INIT_IO {
    uint32_t Size;
    uint32_t RingSizeDwords;
    uint64_t PhysicalAddress;  /* saida: endereco fisico (sysram) ou de GPU  */
    uint32_t RbCntl;
    uint32_t Status;
    uint32_t Placement;        /* entrada: BC250DBG_SDMA_RING_*              */
    uint32_t VramOffset;       /* entrada: offset na VRAM quando RING_VRAM   */
} BC250DBG_SDMA_INIT_IO;

typedef struct _BC250DBG_SDMA_SUBMIT_IO {
    uint32_t Size;
    uint32_t DwordCount;
    uint32_t Dwords[64];
    uint32_t RptrBefore;
    uint32_t WptrAfter;
    uint32_t RptrAfter;
    uint32_t Status;
} BC250DBG_SDMA_SUBMIT_IO;

typedef struct _BC250DBG_SDMA_STATUS_IO {
    uint32_t Size;
    uint32_t Rptr;
    uint32_t Wptr;
    uint32_t RbCntl;
    uint32_t StatusReg;
    uint64_t RbBase;
    uint32_t RptrWriteback;  /* RPTR do buffer — a fonte de verdade      */
    uint32_t WptrShadow;     /* shadow do WPTR que o F32 pesquisa        */
    uint32_t Status;
} BC250DBG_SDMA_STATUS_IO;

typedef struct _BC250DBG_SDMA_LOAD_FW_IO {
    uint32_t Size;
    uint32_t DwordCount;
    uint32_t Version;
    uint32_t Checksum;
    uint32_t Status;
} BC250DBG_SDMA_LOAD_FW_IO;

#pragma pack(pop)

/* ------------------------------------------------------------------------ */
/* Codigos de IOCTL                                                          */
/* ------------------------------------------------------------------------ */

#define FILE_DEVICE_BC250DBG    0x8250u
#define BC250DBG_IOCTL(i)  CTL_CODE(FILE_DEVICE_BC250DBG, 0x800u + (i), \
                                    METHOD_BUFFERED, FILE_ANY_ACCESS)


/* ------------------------------------------------------------------------ */
/* PSP (MP0) — mailbox GPCOM                                                 */
/*                                                                           */
/* Base MP0 = dword 0x16000 * 4 = 0x58000 no BAR5, mesma convencao SOC15 do  */
/* resto do projeto. Medido nesta placa em 2026-08-19:                        */
/*   C2PMSG_64 (0x58200) = 0x80000000 -> bit31 = TOS pronto                   */
/*   C2PMSG_81 (0x58244) = 0x004B5A9C -> versao do SOS                        */
/*   registradores de anel zerados: nenhum anel criado                        */
/*                                                                           */
/* Protocolo espelhado de psp_v11_0_8_ring_create e psp_ring_cmd_submit do    */
/* amdgpu. A porta direta de microcodigo da SDMA nao existe em silicio real   */
/* (ver docs/RESUMO_E_PROXIMOS_PASSOS.md); o PSP e o unico caminho.           */
/* ------------------------------------------------------------------------ */
#define BC250_MP0_BASE              0x58000u
#define BC250_PSP_C2PMSG_64         (BC250_MP0_BASE + 0x200u) /* cmd / TOS / resp */
#define BC250_PSP_C2PMSG_67         (BC250_MP0_BASE + 0x20Cu) /* WPTR do anel     */
#define BC250_PSP_C2PMSG_69         (BC250_MP0_BASE + 0x214u) /* anel PA low32    */
#define BC250_PSP_C2PMSG_70         (BC250_MP0_BASE + 0x218u) /* anel PA high32   */
#define BC250_PSP_C2PMSG_71         (BC250_MP0_BASE + 0x21Cu) /* tamanho do anel  */
#define BC250_PSP_C2PMSG_81         (BC250_MP0_BASE + 0x244u) /* status/versao SOS*/

#define BC250_PSP_TOS_READY         0x80000000u
#define BC250_PSP_CMD_DESTROY_GPCOM_RING 0x000C0000u  /* GFX_CTRL_CMD_ID_* */
#define BC250_PSP_RING_TYPE_KM      2u
#define BC250_PSP_RING_SIZE         0x1000u
#define BC250_PSP_CMD_BUF_SIZE      0x1000u
#define BC250_PSP_FRAME_BYTES       64u
#define BC250_PSP_RESP_OFFSET       864u   /* resp dentro de psp_gfx_cmd_resp */
#define BC250_PSP_CMD_BUF_DECLARED  0x400u /* buf_size anunciado ao PSP       */

/* GFX_CMD_ID_* de psp_gfx_if.h */
#define BC250_PSP_CMD_LOAD_IP_FW    0x06u

/* GFX_FW_TYPE_* de psp_gfx_if.h */
#define BC250_PSP_FW_CP_ME          1u
#define BC250_PSP_FW_CP_PFP         2u
#define BC250_PSP_FW_CP_CE          3u
#define BC250_PSP_FW_CP_MEC         4u
#define BC250_PSP_FW_RLC_G          8u
#define BC250_PSP_FW_SDMA0          9u
#define BC250_PSP_FW_SDMA1          10u
#define BC250_PSP_FW_SMU            18u

/* PSP_ERR_* de psp_gfx_if.h */
#define BC250_PSP_ERR_UNKNOWN_CMD   0x00000100u

#define BC250DBG_PSP_MAX_FW_DWORDS  (1024u * 1024u)  /* 4 MB */


/* Pacotes da SDMA (navi10_sdma_pkt_open.h). Cabecalho:
 *     header = op | (sub_op << 8)
 *
 * CONST_FILL (5 dw): header, dst_lo, dst_hi, valor, bytes-1
 * COPY_LINEAR (7 dw): header, bytes-1, 0, src_lo, src_hi, dst_lo, dst_hi
 * WRITE_LINEAR (4+N dw): header, dst_lo, dst_hi, N-1, dados...
 *
 * Layouts conferidos contra sdma_v5_0_emit_fill_buffer,
 * sdma_v5_0_emit_copy_buffer e sdma_v5_0_ring_test_ring do amdgpu. */
#define BC250_SDMA_OP_COPY          1u
#define BC250_SDMA_OP_WRITE         2u
#define BC250_SDMA_OP_CONST_FILL    11u
#define BC250_SDMA_OP_GCR_REQ       17u
#define BC250_SDMA_SUBOP_LINEAR     0u

/* Limites do amdgpu_buffer_funcs para esta ASIC. */
#define BC250_SDMA_MAX_XFER_BYTES   0x400000u   /* 4 MB */

#define BC250DBG_SDMA_OP_FILL       0u
#define BC250DBG_SDMA_OP_COPY       1u
#define BC250DBG_SDMA_OP_WRITE      2u

#define BC250DBG_SDMA_WRITE_MAX_DW  16u

/* Pular o rinse de cache apos a operacao — so para medir a diferenca. */
#define BC250DBG_SDMA_FLAG_NO_GCR   0x00000001u


/* Como o endereco de destino dos pacotes e formado. A busca do anel (RB_BASE)
 * usa endereco de MC; nao esta provado que os operandos dos pacotes usem a
 * mesma forma, entao medimos as duas. */
#define BC250DBG_ADDR_GPU_BASE  0u   /* BC250_VRAM_GPU_BASE + offset */
#define BC250DBG_ADDR_RAW       1u   /* o offset cru                 */

/* HDP: caminho da CPU para a VRAM. Se ele cacheia leituras, a escrita da GPU
 * pode estar na memoria e a CPU continuar vendo o valor velho. */
#define BC250_HDP_READ_CACHE_INVALIDATE 0x03FC4u  /* dw 0x00d1 + HDP_BASE 0x0F20 */

#define BC250DBG_VRAM_SCAN_MAX_HITS 16u

/* Teto de bytes por chamada de varredura. O chamador fatia. */
#define BC250DBG_VRAM_SCAN_MAX_BYTES 0x02000000u  /* 32 MB */


/* Submissao de dwords arbitrarios no anel — para descobrir se a engine
 * realmente decodifica os pacotes ou apenas arrasta o RPTR ate o WPTR.
 * Um opcode invalido deve travar ou faltar numa engine que decodifica. */
#define BC250DBG_SDMA_RAW_MAX_DW   32u

typedef struct _BC250DBG_SDMA_RAW_IO {
    uint32_t Size;
    uint32_t DwordCount;
    uint32_t RptrBefore;
    uint32_t RptrAfter;
    uint32_t Wptr;
    uint32_t Completed;
    uint32_t StatusReg;
    uint32_t F32Pc;
    uint32_t Data[BC250DBG_SDMA_RAW_MAX_DW];
} BC250DBG_SDMA_RAW_IO;


/* Preenchimento e verificacao de VRAM feitos no kernel.
 *
 * Por que existe: conferir 1 MB por IOCTL individual custa ~15 us por dword
 * (leitura MMIO + travessia do IOCTL) = ~4 s por MB. No kernel, um laco
 * direto no ponteiro mapeado resolve a mesma regiao em fracao disso.
 *
 * Padroes:
 *   CONST  — todo dword igual a Value
 *   ADDR   — dword no offset X vale (Value | (X >> 2))
 *
 * O padrao ADDR e o que importa: se a SDMA colocar dados no lugar errado, o
 * proprio conteudo diz de onde ele veio. Um fill constante nao detecta
 * deslocamento. */
#define BC250DBG_PAT_CONST  0u
#define BC250DBG_PAT_ADDR   1u

#define BC250DBG_VRAM_OP_MAX_BYTES 0x02000000u  /* 32 MB por chamada */
#define BC250DBG_VRAM_BAD_SAMPLES  8u

typedef struct _BC250DBG_VRAM_PATTERN_IO {
    uint32_t Size;
    uint32_t Pattern;     /* BC250DBG_PAT_*                             */
    uint32_t Value;       /* constante, ou semente do padrao ADDR       */
    uint32_t Start;
    uint32_t Length;
    uint32_t BadCount;    /* so na verificacao                          */
    uint32_t BadOffset[BC250DBG_VRAM_BAD_SAMPLES];
    uint32_t BadGot[BC250DBG_VRAM_BAD_SAMPLES];
    uint32_t BadWant[BC250DBG_VRAM_BAD_SAMPLES];
} BC250DBG_VRAM_PATTERN_IO;

typedef struct _BC250DBG_VRAM_SCAN_IO {
    uint32_t Size;
    uint32_t Value;       /* padrao procurado                          */
    uint32_t Mask;        /* 0 = compara o dword inteiro               */
    uint32_t Start;       /* offset inicial na VRAM                    */
    uint32_t Length;      /* bytes a varrer                            */
    uint32_t HitCount;    /* quantos foram encontrados (pode passar do array) */
    uint32_t Hits[BC250DBG_VRAM_SCAN_MAX_HITS];
} BC250DBG_VRAM_SCAN_IO;

typedef struct _BC250DBG_SDMA_OP_IO {
    uint32_t Size;
    uint32_t Op;            /* BC250DBG_SDMA_OP_*                        */
    uint32_t DstOffset;     /* offset na VRAM                            */
    uint32_t SrcOffset;     /* offset na VRAM (COPY)                     */
    uint32_t Value;         /* padrao (FILL)                             */
    uint32_t ByteCount;
    uint32_t DwordCount;    /* WRITE: quantos dwords seguem              */
    uint32_t FillSize;      /* FILL: 0=byte 1=half 2=dword               */
    uint32_t Flags;         /* BC250DBG_SDMA_FLAG_*                      */
    uint32_t AddrMode;      /* BC250DBG_ADDR_*                           */
    uint32_t RptrBefore;
    uint32_t RptrAfter;
    uint32_t Wptr;
    uint32_t Completed;     /* 1 se RPTR alcancou WPTR                   */
    uint32_t StatusReg;
    uint32_t Data[BC250DBG_SDMA_WRITE_MAX_DW];
} BC250DBG_SDMA_OP_IO;

typedef struct _BC250DBG_SDMA_F32_RESET_IO {
    uint32_t Size;
    uint32_t PcBefore;
    uint32_t PcAfter;
    uint32_t Moved;      /* transicoes de PC em 16 amostras; 0 = parado */
    uint32_t Status;
} BC250DBG_SDMA_F32_RESET_IO;

typedef struct _BC250DBG_PSP_STATUS_IO {
    uint32_t Size;
    uint32_t C2pmsg64;      /* bit31 = TOS pronto                         */
    uint32_t C2pmsg81;      /* versao do SOS                              */
    uint32_t C2pmsg67;      /* WPTR do anel                               */
    uint32_t RingLo;
    uint32_t RingHi;
    uint32_t RingSizeReg;
    uint32_t RingCreated;   /* 1 se este driver criou o anel              */
    uint64_t RingPhysical;
} BC250DBG_PSP_STATUS_IO;

typedef struct _BC250DBG_PSP_RING_INIT_IO {
    uint32_t Size;
    uint32_t Status;        /* BC250DBG_OPS_*                             */
    uint64_t RingPhysical;
    uint32_t C2pmsg64Before;
    uint32_t C2pmsg64After;
} BC250DBG_PSP_RING_INIT_IO;

typedef struct _BC250DBG_PSP_LOAD_FW_IO {
    uint32_t Size;
    uint32_t FwType;        /* BC250_PSP_FW_*                             */
    uint32_t DwordCount;    /* payload SEM o header do blob               */
    uint32_t FenceReached;  /* 1 se o PSP consumiu o frame                */
    uint32_t RespStatus;    /* 0 = ok, 0x100 = comando desconhecido       */
    uint32_t RespFwAddrLo;
    uint32_t RespFwAddrHi;
    uint32_t RespTmrSize;
    /* seguido por DwordCount * uint32_t */
} BC250DBG_PSP_LOAD_FW_IO;

typedef struct _BC250DBG_GCVM_STATUS_IO {
    uint32_t Size;
    uint32_t L2Cntl;
    uint32_t L2Cntl2;
    uint32_t Context0Cntl;
    uint32_t Context0PtBaseLo;
    uint32_t Context0PtBaseHi;
    uint32_t Context0StartLo;
    uint32_t Context0EndLo;
    uint32_t SysApertureLow;
    uint32_t SysApertureHigh;
    uint32_t SysApertureDefLo;
    uint32_t SysApertureDefHi;
    uint32_t L2ProtFaultStatus;
} BC250DBG_GCVM_STATUS_IO;

typedef struct _BC250DBG_GCVM_INIT_IO {
    uint32_t Size;
    uint32_t Flags;
    uint32_t Status;
    uint32_t Context0CntlBefore;
    uint32_t Context0CntlAfter;
    uint32_t L2CntlAfter;
} BC250DBG_GCVM_INIT_IO;

typedef struct _BC250DBG_PSP_SETUP_TMR_IO {
    uint32_t Size;
    uint32_t TmrSizeBytes;
    uint32_t VramOffset;
    uint32_t FenceReached;
    uint32_t RespStatus;
    uint64_t TmrGpuAddr;
    uint64_t TmrPhysAddr;
} BC250DBG_PSP_SETUP_TMR_IO;

#define IOCTL_BC250DBG_GET_INFO      BC250DBG_IOCTL(0)
#define IOCTL_BC250DBG_REG_READ      BC250DBG_IOCTL(1)
#define IOCTL_BC250DBG_REG_WRITE     BC250DBG_IOCTL(2)
#define IOCTL_BC250DBG_SMN_READ      BC250DBG_IOCTL(3)
#define IOCTL_BC250DBG_SMN_WRITE     BC250DBG_IOCTL(4)
#define IOCTL_BC250DBG_SMU_MSG       BC250DBG_IOCTL(5)
#define IOCTL_BC250DBG_LOG_MAP       BC250DBG_IOCTL(6)
#define IOCTL_BC250DBG_LOG_UNMAP     BC250DBG_IOCTL(7)
#define IOCTL_BC250DBG_LOG_MASK      BC250DBG_IOCTL(8)
#define IOCTL_BC250DBG_SCRIPT_RUN    BC250DBG_IOCTL(9)
#define IOCTL_BC250DBG_SCRIPT_ABORT  BC250DBG_IOCTL(10)
#define IOCTL_BC250DBG_SNAPSHOT      BC250DBG_IOCTL(11)
#define IOCTL_BC250DBG_THERMAL_GET   BC250DBG_IOCTL(12)
#define IOCTL_BC250DBG_THERMAL_SET   BC250DBG_IOCTL(13)
#define IOCTL_BC250DBG_ARM           BC250DBG_IOCTL(14)
#define IOCTL_BC250DBG_PCICFG_READ   BC250DBG_IOCTL(15)
#define IOCTL_BC250DBG_SDMA_INIT     BC250DBG_IOCTL(16)
#define IOCTL_BC250DBG_SDMA_SUBMIT   BC250DBG_IOCTL(17)
#define IOCTL_BC250DBG_SDMA_STATUS   BC250DBG_IOCTL(18)
#define IOCTL_BC250DBG_SDMA_LOAD_FW  BC250DBG_IOCTL(19)
#define IOCTL_BC250DBG_VRAM_READ     BC250DBG_IOCTL(20)
#define IOCTL_BC250DBG_VRAM_WRITE    BC250DBG_IOCTL(21)
#define IOCTL_BC250DBG_PSP_STATUS    BC250DBG_IOCTL(22)
typedef struct _BC250DBG_GFX_INIT_IO {
    uint32_t Size;
    uint32_t RingSizeBytes;
    uint32_t VramOffset;
    uint64_t GpuAddr;
    uint32_t RbCntl;
    uint32_t MeCntlBefore;
    uint32_t MeCntlAfter;
    uint32_t Status;
} BC250DBG_GFX_INIT_IO;

#define BC250DBG_GFX_SUBMIT_MAX_DW 256u

typedef struct _BC250DBG_GFX_SUBMIT_IO {
    uint32_t Size;
    uint32_t DwordCount;
    uint32_t RptrBefore;
    uint32_t RptrAfter;
    uint32_t Wptr;
    uint32_t Completed;
    uint32_t Status;
    uint32_t Data[BC250DBG_GFX_SUBMIT_MAX_DW];
} BC250DBG_GFX_SUBMIT_IO;

typedef struct _BC250DBG_GFX_STATUS_IO {
    uint32_t Size;
    uint32_t MeCntl;
    uint32_t Rb0Cntl;
    uint64_t Rb0Base;
    uint32_t Rb0Wptr;
    uint32_t Rb0Rptr;
    uint32_t Rb0RptrWb;
    uint32_t GrbmStatus;
} BC250DBG_GFX_STATUS_IO;

#define IOCTL_BC250DBG_PSP_RING_INIT BC250DBG_IOCTL(23)
#define IOCTL_BC250DBG_PSP_LOAD_FW   BC250DBG_IOCTL(24)
#define IOCTL_BC250DBG_SDMA_F32_RESET BC250DBG_IOCTL(25)
#define IOCTL_BC250DBG_SDMA_OP        BC250DBG_IOCTL(26)
#define IOCTL_BC250DBG_VRAM_SCAN      BC250DBG_IOCTL(27)
#define IOCTL_BC250DBG_HDP_INVALIDATE BC250DBG_IOCTL(28)
#define IOCTL_BC250DBG_SDMA_RAW       BC250DBG_IOCTL(29)
#define IOCTL_BC250DBG_VRAM_FILL      BC250DBG_IOCTL(32)
#define IOCTL_BC250DBG_VRAM_VERIFY    BC250DBG_IOCTL(33)
#define IOCTL_BC250DBG_GCVM_STATUS    BC250DBG_IOCTL(30)
#define IOCTL_BC250DBG_GCVM_INIT      BC250DBG_IOCTL(31)
#define IOCTL_BC250DBG_PSP_SETUP_TMR  BC250DBG_IOCTL(34)
#define IOCTL_BC250DBG_GFX_INIT       BC250DBG_IOCTL(35)
#define IOCTL_BC250DBG_GFX_SUBMIT     BC250DBG_IOCTL(36)
#define IOCTL_BC250DBG_GFX_STATUS     BC250DBG_IOCTL(37)

#endif /* BC250DBG_ABI_H */


