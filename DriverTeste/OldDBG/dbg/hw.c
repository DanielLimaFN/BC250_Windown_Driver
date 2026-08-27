/* hw.c — acesso a hardware: PCI config, BAR5 MMIO, SMN e mailbox do SMU.
 *
 * Licoes que estao codificadas aqui (ver roadmap secao 2 e Apendice B):
 *
 *  1. Win11 26100 descarta silenciosamente escritas MMIO feitas por deref de
 *     ponteiro volatile. TODO acesso passa por READ_REGISTER_ULONG /
 *     WRITE_REGISTER_ULONG. Isso custou semanas ao projeto anterior.
 *
 *  2. SMN e um acesso indexado em duas etapas (0x38 = endereco, 0x3C = dado).
 *     Duas threads intercaladas leem o registrador uma da outra — por isso o
 *     spin lock, sem excecao.
 *
 *  3. O SMU aceita mensagens que travam a maquina (ForceGfxFreq sem tabelas
 *     DPM ja derrubou a placa). Toda mensagem passa por classificacao de
 *     risco e as de risco alto exigem armamento explicito.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bc250dbg.h"

/* Registradores de interesse imediato. Offsets em bytes dentro do BAR5,
 * conforme a tabela verificada do projeto anterior. */
#define BC250_REG_GPU_ID        0x0000u

/* Offsets de GC em bytes no BAR5, convencao SOC15 do amdgpu:
 *
 *     byte = (GC_BASE_segN + reg_dword) * 4
 *
 * GC_BASE seg0 = 0x1260, seg1 = 0xA000 (Cyan Skillfish); reg_dword e o valor
 * de mmXXX em gc_10_1_0_offset.h e mmXXX_BASE_IDX escolhe o segmento.
 *
 * Os sufixos _LEGACY guardam os offsets que o projeto usava antes de
 * 2026-08-19. Eles estavam errados por dois motivos somados: nao aplicavam
 * o *4 e partiam de numeros de registrador (0x2000, 0x2004, 0x2074) que nao
 * existem em gc_10_1_0_offset.h. Ficam aqui apenas para reproduzir as
 * leituras antigas e mostrar que elas nao confirmavam nada. */
#define BC250_REG_SCRATCH0        0x30100u  /* mmSCRATCH_REG0 dw 0x2040 seg1 */
#define BC250_REG_GRBM_STATUS     0x08010u  /* mmGRBM_STATUS  dw 0x0da4 seg0 */
#define BC250_REG_CC_ARRAY        0x089BCu  /* mmCC_GC_SHADER_ARRAY_CONFIG dw 0x100f seg0 */
#define BC250_REG_SCRATCH0_LEGACY 0x032D4u
#define BC250_REG_GRBM_STATUS_LEGACY 0x03260u
#define BC250_REG_CC_ARRAY_LEGACY 0x03264u
#define BC250_CC_ARRAY_LINUX      0xFFF80000u

/* Mensagens PPSMC (smu_v11_8, Cyan Skillfish). */
#define PPSMC_TestMessage           0x01u
#define PPSMC_GetSmuVersion         0x02u
#define PPSMC_GetDriverIfVersion    0x03u
#define PPSMC_QueryGfxclk           0x0Fu
#define PPSMC_QueryActiveWgp        0x1Eu
#define PPSMC_GetGfxFrequency       0x37u
#define PPSMC_GetGfxVid             0x38u
#define PPSMC_GetEnabledSmuFeatures 0x3Du

/* Porta de configuracao PCI (mecanismo 1). */
/* Portas de config PCI legadas — NAO USAR.
   Mantidas apenas como documentacao do que foi removido: driver nenhum deve
   dirigir 0xCF8/0xCFC enquanto o SO esta rodando. Use HalGetBusDataByOffset.
   #define PCI_CONFIG_ADDRESS  0xCF8
   #define PCI_CONFIG_DATA     0xCFC  */

static KSPIN_LOCK g_PciLock;
static BOOLEAN    g_PciLockInit = FALSE;

/* ------------------------------------------------------------------------ */
/* PCI configuration space                                                   */
/* ------------------------------------------------------------------------ */

/* Nota de risco: um driver legacy nao tem stack PnP, entao nao ha
 * BUS_INTERFACE_STANDARD para pedir. Usamos o mecanismo 1 (CF8/CFC) com spin
 * lock. E o que ferramentas de diagnostico fazem; o risco residual e uma
 * corrida com o proprio HAL, que na pratica nao toca CF8/CFC em regime. */
/*
  Leitura de config PCI pelo HAL, NAO pelas portas 0xCF8/0xCFC.

  A versao anterior escrevia direto em 0xCF8 e lia 0xCFC sob um spinlock
  proprio. O problema: o driver PCI do Windows usa o MESMO par de portas, com
  a sincronizacao DELE. Um spinlock local nao exclui o outro agente — duas
  escritas intercaladas em 0xCF8 fazem uma das partes ler a config do
  dispositivo errado, e em alguns chipsets travam a ponte host.

  Isso importa aqui porque BcPciScan fazia 8 buses x 32 dev x 8 fn = 2048
  leituras de config na carga do driver. Uma varredura de forca bruta
  correndo contra o SO e' candidata direta aos travamentos duros sem
  bugcheck observados em 2026-08-22.

  HalGetBusDataByOffset faz a mesma leitura pelo caminho sancionado, com a
  serializacao correta, e funciona tambem onde a config e' ECAM/MMCONFIG em
  vez de CF8/CFC.
*/
ULONG BcPciConfigRead(ULONG Bus, ULONG Dev, ULONG Fn, ULONG Offset)
{
    ULONG value = 0xFFFFFFFFu;
    ULONG slot;
    ULONG got;

    /* PCI_SLOT_NUMBER: bits 0-4 = device, bits 5-7 = function. */
    slot = (Dev & 0x1Fu) | ((Fn & 0x07u) << 5);

    got = HalGetBusDataByOffset(PCIConfiguration,
                                Bus,
                                slot,
                                &value,
                                Offset & 0xFCu,
                                sizeof(value));
    if (got != sizeof(value)) {
        return 0xFFFFFFFFu;
    }
    return value;
}

static VOID BcpDecodeBars(VOID)
{
    ULONG i;

    for (i = 0; i < BC250DBG_MAX_BARS; i++) {
        ULONG raw = BcPciConfigRead(g_Dev.PciBus, g_Dev.PciDev, g_Dev.PciFn,
                                    0x10 + i * 4);
        g_Dev.Bar[i].Raw   = raw;
        g_Dev.Bar[i].Flags = 0;
        g_Dev.Bar[i].Base  = 0;

        if (raw == 0 || raw == 0xFFFFFFFFu) {
            continue;
        }

        if (raw & 1u) {
            /* espaco de I/O */
            g_Dev.Bar[i].Flags |= 0x1u;
            g_Dev.Bar[i].Base = raw & ~0x3ull;
            continue;
        }

        if (((raw >> 1) & 0x3u) == 0x2u) {
            /* 64 bits: a proxima entrada e a metade alta, nao um BAR */
            ULONG hi = 0;
            if (i + 1 < BC250DBG_MAX_BARS) {
                hi = BcPciConfigRead(g_Dev.PciBus, g_Dev.PciDev, g_Dev.PciFn,
                                     0x10 + (i + 1) * 4);
            }
            g_Dev.Bar[i].Flags |= 0x2u;
            g_Dev.Bar[i].Base = ((ULONGLONG)hi << 32) | (raw & ~0xFull);
            i++;   /* consome a metade alta */
        } else {
            g_Dev.Bar[i].Base = raw & ~0xFull;
        }
    }
}

NTSTATUS BcPciScan(VOID)
{
    ULONG bus, dev, fn;

    if (!g_PciLockInit) {
        KeInitializeSpinLock(&g_PciLock);
        g_PciLockInit = TRUE;
    }

    /*
      Varredura contida.

      A versao anterior varria 8 buses x 32 dispositivos x 8 funcoes = 2048
      leituras de config na carga, pelas portas 0xCF8/0xCFC. Alem da corrida
      com o driver PCI do SO (ver BcPciConfigRead), tocar em 2048 pares
      BDF inexistentes e' precisamente o "probe random offsets casually" que a
      documentacao de referencia associa a travamento de hardware.

      Agora: leitura pelo HAL, buses 0 e 1 apenas (a GPU e' integrada ao APU),
      e parada no primeiro casamento. Se o dispositivo estiver atras de uma
      ponte em bus mais alto, o fallback de endereco fisico cobre — e o
      usuario ve isso no log, em vez de pagarmos uma varredura completa.
    */
    for (bus = 0; bus < 2; bus++) {
        for (dev = 0; dev < 32; dev++) {
            for (fn = 0; fn < 8; fn++) {
                ULONG id = BcPciConfigRead(bus, dev, fn, 0x00);
                USHORT ven = (USHORT)(id & 0xFFFFu);
                USHORT devid = (USHORT)(id >> 16);

                if (id == 0xFFFFFFFFu || ven == 0xFFFFu) {
                    if (fn == 0) {
                        break;   /* funcao 0 ausente => dispositivo ausente */
                    }
                    continue;
                }

                if (ven == BC250_PCI_VENDOR_ID && devid == BC250_PCI_DEVICE_ID) {
                    ULONG cls = BcPciConfigRead(bus, dev, fn, 0x08);

                    g_Dev.PciFound   = TRUE;
                    g_Dev.PciBus     = bus;
                    g_Dev.PciDev     = dev;
                    g_Dev.PciFn      = fn;
                    g_Dev.VendorId   = ven;
                    g_Dev.DeviceId   = devid;
                    g_Dev.RevisionId = (UCHAR)(cls & 0xFFu);

                    BcpDecodeBars();

                    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_PCI,
                              "BC-250 em %02x:%02x.%u rev %02x",
                              bus, dev, fn, g_Dev.RevisionId);
                    return STATUS_SUCCESS;
                }
            }
        }
    }

    BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_PCI,
              "1002:13FE nao encontrado; usando fallback");
    return STATUS_NOT_FOUND;
}

/* ------------------------------------------------------------------------ */
/* Mapeamento do BAR5                                                        */
/* ------------------------------------------------------------------------ */

NTSTATUS BcMmioMap(VOID)
{
    PHYSICAL_ADDRESS pa;
    PVOID va;

    if (g_Dev.Mmio != NULL) {
        return STATUS_SUCCESS;
    }

    if (g_Dev.PciFound && g_Dev.Bar[BC250_BAR5_INDEX].Base != 0 &&
        (g_Dev.Bar[BC250_BAR5_INDEX].Flags & 0x1u) == 0) {
        pa.QuadPart = (LONGLONG)g_Dev.Bar[BC250_BAR5_INDEX].Base;
    } else {
        pa.QuadPart = (LONGLONG)BC250_BAR5_FALLBACK_PA;
        BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_CORE,
                  "BAR5 pelo fallback 0x%llx", (ULONGLONG)pa.QuadPart);
    }

    g_Dev.MmioLength = BC250_BAR5_DEFAULT_LEN;

    /* Nao sondamos o tamanho do BAR (escrever 0xFFFFFFFF nele perturbaria o
     * dispositivo vivo). Usamos o tamanho conhecido de 512 KB. */
    va = MmMapIoSpaceEx(pa, g_Dev.MmioLength, PAGE_READWRITE | PAGE_NOCACHE);
    if (va == NULL) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_CORE,
                  "MmMapIoSpaceEx falhou em 0x%llx", (ULONGLONG)pa.QuadPart);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    g_Dev.MmioPhysical = pa;
    g_Dev.Mmio = (volatile UCHAR *)va;
    g_Dev.Bar[BC250_BAR5_INDEX].Flags |= 0x4u;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE,
              "BAR5 mapeado 0x%llx +%u KB",
              (ULONGLONG)pa.QuadPart, g_Dev.MmioLength / 1024);
    return STATUS_SUCCESS;
}

VOID BcMmioUnmap(VOID)
{
    if (g_Dev.Mmio != NULL) {
        MmUnmapIoSpace((PVOID)g_Dev.Mmio, g_Dev.MmioLength);
        g_Dev.Mmio = NULL;
    }
}

/* ------------------------------------------------------------------------ */
/* Mapeamento do BAR0 (abertura de VRAM)                                     */
/*                                                                           */
/* Por que isso existe: a SDMA so consegue traduzir enderecos dentro da       */
/* abertura de sistema, que nesta placa cobre exatamente os 512 MB de VRAM    */
/* (GPU 0xF400000000..0xF420000000). Nao ha page table no VMID 0 nem          */
/* abertura AGP, entao um ring buffer em RAM do sistema e inalcancavel — a    */
/* leitura do anel pendura com RB_MC_RREQ_IDLE=0. Colocando o anel na VRAM,   */
/* o endereco cai na abertura e traduz sem GART nenhum.                       */
/* ------------------------------------------------------------------------ */

NTSTATUS BcVramMap(VOID)
{
    PHYSICAL_ADDRESS pa;
    PVOID va;

    if (g_Dev.Vram != NULL) {
        return STATUS_SUCCESS;
    }

    if (!g_Dev.PciFound || g_Dev.Bar[BC250_BAR0_INDEX].Base == 0 ||
        (g_Dev.Bar[BC250_BAR0_INDEX].Flags & 0x1u) != 0) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_CORE,
                  "BAR0 nao disponivel para mapear a VRAM");
        return STATUS_DEVICE_NOT_READY;
    }

    pa.QuadPart = (LONGLONG)g_Dev.Bar[BC250_BAR0_INDEX].Base;
    g_Dev.VramLength = BC250_BAR0_DEFAULT_LEN;

    /* Mesma regra do BAR5: nao sondamos o tamanho escrevendo 0xFFFFFFFF no
     * registrador de BAR. 256 MB foi deduzido da janela da ponte PCI. */
    /* NOCACHE, sem write-combine. Com WC as escritas da CPU no anel podem
     * ficar no buffer de combinacao e a GPU ler dado velho — uma classe
     * inteira de duvida que nao vale a pena carregar num driver de
     * instrumentacao. O custo e leitura mais lenta, que aqui nao importa. */
    va = MmMapIoSpaceEx(pa, g_Dev.VramLength, PAGE_READWRITE | PAGE_NOCACHE);
    if (va == NULL) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_CORE,
                  "MmMapIoSpaceEx falhou para o BAR0 em 0x%llx (%u MB)",
                  (ULONGLONG)pa.QuadPart, g_Dev.VramLength / (1024 * 1024));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    g_Dev.VramPhysical = pa;
    g_Dev.Vram = (volatile UCHAR *)va;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE,
              "BAR0 mapeado 0x%llx +%u MB (VRAM em GPU 0x%llx)",
              (ULONGLONG)pa.QuadPart, g_Dev.VramLength / (1024 * 1024),
              (ULONGLONG)BC250_VRAM_GPU_BASE);
    return STATUS_SUCCESS;
}

VOID BcVramUnmap(VOID)
{
    if (g_Dev.Vram != NULL) {
        MmUnmapIoSpace((PVOID)g_Dev.Vram, g_Dev.VramLength);
        g_Dev.Vram = NULL;
    }
}

ULONG BcVramReadRaw(ULONG Offset)
{
    if (g_Dev.Vram == NULL || Offset + 4 > g_Dev.VramLength) {
        return 0xFFFFFFFFu;
    }
    BcHdpReadCacheInvalidate();
    return READ_REGISTER_ULONG((volatile ULONG *)(g_Dev.Vram + Offset));
}

VOID BcVramWriteRaw(ULONG Offset, ULONG Value)
{
    if (g_Dev.Vram == NULL || Offset + 4 > g_Dev.VramLength) {
        return;
    }
    WRITE_REGISTER_ULONG((volatile ULONG *)(g_Dev.Vram + Offset), Value);
}

/* ------------------------------------------------------------------------ */
/* Acesso cru                                                                */
/* ------------------------------------------------------------------------ */

ULONG BcRegReadRaw(ULONG Offset)
{
    if (g_Dev.Mmio == NULL || Offset + 4 > g_Dev.MmioLength) {
        return 0xFFFFFFFFu;
    }
    return READ_REGISTER_ULONG((PULONG)(g_Dev.Mmio + Offset));
}

VOID BcRegWriteRaw(ULONG Offset, ULONG Value)
{
    if (g_Dev.Mmio == NULL || Offset + 4 > g_Dev.MmioLength) {
        return;
    }
    WRITE_REGISTER_ULONG((PULONG)(g_Dev.Mmio + Offset), Value);
}

ULONG BcSmnReadRaw(ULONG Address)
{
    KIRQL irql;
    ULONG value;

    if (g_Dev.Mmio == NULL) {
        return 0xFFFFFFFFu;
    }

    KeAcquireSpinLock(&g_Dev.SmnLock, &irql);
    WRITE_REGISTER_ULONG((PULONG)(g_Dev.Mmio + BC250_SMN_INDEX_OFFSET), Address);
    /* leitura do proprio indice = posting flush antes de tocar no dado */
    (VOID)READ_REGISTER_ULONG((PULONG)(g_Dev.Mmio + BC250_SMN_INDEX_OFFSET));
    value = READ_REGISTER_ULONG((PULONG)(g_Dev.Mmio + BC250_SMN_DATA_OFFSET));
    KeReleaseSpinLock(&g_Dev.SmnLock, irql);

    return value;
}

VOID BcSmnWriteRaw(ULONG Address, ULONG Value)
{
    KIRQL irql;

    if (g_Dev.Mmio == NULL) {
        return;
    }

    KeAcquireSpinLock(&g_Dev.SmnLock, &irql);
    WRITE_REGISTER_ULONG((PULONG)(g_Dev.Mmio + BC250_SMN_INDEX_OFFSET), Address);
    (VOID)READ_REGISTER_ULONG((PULONG)(g_Dev.Mmio + BC250_SMN_INDEX_OFFSET));
    WRITE_REGISTER_ULONG((PULONG)(g_Dev.Mmio + BC250_SMN_DATA_OFFSET), Value);
    (VOID)READ_REGISTER_ULONG((PULONG)(g_Dev.Mmio + BC250_SMN_DATA_OFFSET));
    KeReleaseSpinLock(&g_Dev.SmnLock, irql);
}

/* ------------------------------------------------------------------------ */
/* Acesso com politica                                                       */
/* ------------------------------------------------------------------------ */

ULONG BcRegRead(ULONG Offset, ULONG *Status)
{
    ULONG v;

    if (g_Dev.Mmio == NULL) {
        *Status = BC250DBG_OPS_RANGE;
        return 0xFFFFFFFFu;
    }
    if ((Offset & 3u) != 0 || Offset + 4 > g_Dev.MmioLength) {
        *Status = BC250DBG_OPS_RANGE;
        return 0xFFFFFFFFu;
    }

    v = BcRegReadRaw(Offset);
    *Status = BC250DBG_OPS_OK;
    BcLogEvent(BC250DBG_LVL_TRACE, BC250DBG_SUB_REG, BC250DBG_EV_REG_READ,
               Offset, v, 0, 0);
    return v;
}

ULONG BcRegWrite(ULONG Offset, ULONG Value, ULONG *Status)
{
    ULONG readback;

    if (g_Dev.Mmio == NULL || (Offset & 3u) != 0 ||
        Offset + 4 > g_Dev.MmioLength) {
        *Status = BC250DBG_OPS_RANGE;
        return 0xFFFFFFFFu;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        *Status = BC250DBG_OPS_DENIED;
        BcLogEvent(BC250DBG_LVL_WARN, BC250DBG_SUB_REG, BC250DBG_EV_REG_WRITE,
                   Offset, Value, 0xDEADull, 0);
        return 0xFFFFFFFFu;
    }

    BcRegWriteRaw(Offset, Value);
    /* Ler de volta sempre: metade dos "sucessos" das tentativas anteriores
     * eram escritas aceitas em registrador que nao guarda o valor. */
    readback = BcRegReadRaw(Offset);

    *Status = BC250DBG_OPS_OK;
    BcLogEvent(BC250DBG_LVL_TRACE, BC250DBG_SUB_REG, BC250DBG_EV_REG_WRITE,
               Offset, Value, readback, 0);
    return readback;
}

ULONG BcSmnRead(ULONG Address, ULONG *Status)
{
    ULONG v;

    if (g_Dev.Mmio == NULL) {
        *Status = BC250DBG_OPS_RANGE;
        return 0xFFFFFFFFu;
    }

    v = BcSmnReadRaw(Address);
    *Status = BC250DBG_OPS_OK;
    BcLogEvent(BC250DBG_LVL_TRACE, BC250DBG_SUB_SMN, BC250DBG_EV_SMN_READ,
               Address, v, 0, 0);
    return v;
}

ULONG BcSmnWrite(ULONG Address, ULONG Value, ULONG *Status)
{
    ULONG readback;

    if (g_Dev.Mmio == NULL) {
        *Status = BC250DBG_OPS_RANGE;
        return 0xFFFFFFFFu;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        *Status = BC250DBG_OPS_DENIED;
        BcLogEvent(BC250DBG_LVL_WARN, BC250DBG_SUB_SMN, BC250DBG_EV_SMN_WRITE,
                   Address, Value, 0xDEADull, 0);
        return 0xFFFFFFFFu;
    }

    BcSmnWriteRaw(Address, Value);
    readback = BcSmnReadRaw(Address);

    *Status = BC250DBG_OPS_OK;
    BcLogEvent(BC250DBG_LVL_TRACE, BC250DBG_SUB_SMN, BC250DBG_EV_SMN_WRITE,
               Address, Value, readback, 0);
    return readback;
}

/* ------------------------------------------------------------------------ */
/* SMU                                                                       */
/* ------------------------------------------------------------------------ */

/* Classificacao vinda de docs/BC250-SMU-DISCOVERY-2026-07-05.md do projeto
 * anterior, que documenta uma trava real de maquina com ForceGfxFreq.
 * Politica: o que nao esta listado como seguro e tratado como risco alto.
 * Falhar fechado e o unico jeito de nao repetir a trava. */
ULONG BcSmuRisk(ULONG Queue, ULONG Msg)
{
    if (Queue == 0) {
        switch (Msg) {
        case 0x01: case 0x02: case 0x03:   /* Test, GetSmuVersion, DriverIf  */
        case 0x0C: case 0x0F: case 0x13:   /* QueryCorePstate/Gfxclk/DfPstate*/
        case 0x1E: case 0x37: case 0x38:   /* ActiveWgp, GfxFreq, GfxVid     */
        case 0x3D:                          /* GetEnabledSmuFeatures          */
            return BC250_SMU_RISK_SAFE;
        case 0x3A: case 0x3C:              /* UnForceGfxFreq/Vid — limpeza   */
            return BC250_SMU_RISK_SAFE;
        case 0x19: case 0x1A:              /* deep sleep tuning              */
        case 0x35: case 0x36:              /* SetSoftMin/MaxCclk             */
        case 0x1B: case 0x1C: case 0x1D:   /* telemetria                     */
        case 0x0E: case 0x18:              /* RequestGfxclk, RequestActiveWgp (GFXOFF wake-up) */
            return BC250_SMU_RISK_MEDIUM;
        default:
            /* 0x2C SetCoreEnable, 0x2E GcRsmuSoftReset, 0x39 ForceGfxFreq, 0x3B ForceGfxVid */
            return BC250_SMU_RISK_HIGH;
        }
    }

    if (Queue == 2) {
        switch (Msg) {
        case 0x04: case 0x0B:
            return BC250_SMU_RISK_SAFE;
        case 0x05: case 0x06:   /* Enable/Disable SMU features (ex: bit 2 GFXOFF) */
            return BC250_SMU_RISK_MEDIUM;
        default:
            return BC250_SMU_RISK_HIGH;
        }
    }

    if (Queue == 3) {
        switch (Msg) {
        case 0x36: case 0x37:   /* leitura de tensao CPU/GPU */
        case 0x44:              /* PPSMC_MSG_GetGfxFrequency */
            return BC250_SMU_RISK_SAFE;
        case 0x1C:              /* RequestGfxclk */
        case 0x3C: case 0x3D:   /* Enable/Disable features */
            return BC250_SMU_RISK_MEDIUM;
        default:
            return BC250_SMU_RISK_HIGH;
        }
    }

    /* Fila 2: protocolo pouco documentado nesta ASIC. */
    return BC250_SMU_RISK_HIGH;
}

static VOID BcpSmuQueueRegs(ULONG Queue, ULONG *Cmd, ULONG *Arg,
                            ULONG *ArgHi, ULONG *Rsp)
{
    switch (Queue) {
    case 2:
        *Cmd = BC250_SMU_Q2_CMD; *Arg = BC250_SMU_Q2_ARG;
        *ArgHi = BC250_SMU_Q2_ARG_HI; *Rsp = BC250_SMU_Q2_RSP;
        break;
    case 3:
        *Cmd = BC250_SMU_Q3_CMD; *Arg = BC250_SMU_Q3_ARG;
        *ArgHi = 0; *Rsp = BC250_SMU_Q3_RSP;
        break;
    default:
        *Cmd = BC250_SMU_Q0_CMD; *Arg = BC250_SMU_Q0_ARG;
        *ArgHi = 0; *Rsp = BC250_SMU_Q0_RSP;
        break;
    }
}

static VOID BcpSleepMs(ULONG Ms)
{
    LARGE_INTEGER t;
    t.QuadPart = -((LONGLONG)Ms * 10000);   /* relativo, 100 ns */
    KeDelayExecutionThread(KernelMode, FALSE, &t);
}

NTSTATUS BcSmuMsg(ULONG Queue, ULONG Msg, ULONG Arg, ULONG ArgHi,
                  ULONG TimeoutMs, ULONG *SmuStatus, ULONG *Result)
{
    ULONG cmdReg, argReg, argHiReg, rspReg;
    ULONG i, status = 0, result = 0;
    ULONG timeout = TimeoutMs ? TimeoutMs : 1000;
    ULONG risk;

    *SmuStatus = 0;
    *Result = 0;

    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (Queue != 0 && Queue != 2 && Queue != 3) {
        return STATUS_INVALID_PARAMETER;
    }

    risk = BcSmuRisk(Queue, Msg);
    if (risk == BC250_SMU_RISK_SAFE) {
        /* consultas: exigem apenas o driver carregado */
    } else if (risk == BC250_SMU_RISK_MEDIUM) {
        if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
            BcLogEvent(BC250DBG_LVL_WARN, BC250DBG_SUB_SMU,
                       BC250DBG_EV_SMU_REFUSED, Queue, Msg, 1, 0);
            return STATUS_ACCESS_DENIED;
        }
    } else {
        if (g_Dev.ArmLevel < BC250DBG_ARM_UNSAFE) {
            BcLogEvent(BC250DBG_LVL_WARN, BC250DBG_SUB_SMU,
                       BC250DBG_EV_SMU_REFUSED, Queue, Msg, 2, 0);
            return STATUS_ACCESS_DENIED;
        }
        /* Mensagem capaz de mexer em clock/tensao: so com sensor termico
         * confiavel e abaixo do limite. */
        if (!BcThermalOk(0)) {
            BcLogEvent(BC250DBG_LVL_ERROR, BC250DBG_SUB_SMU,
                       BC250DBG_EV_SMU_REFUSED, Queue, Msg, 3, 0);
            return STATUS_DEVICE_POWER_FAILURE;
        }
    }

    BcpSmuQueueRegs(Queue, &cmdReg, &argReg, &argHiReg, &rspReg);

    KeWaitForSingleObject(&g_Dev.SmuMutex, Executive, KernelMode, FALSE, NULL);

    /* 1) esperar o SMU concluir transacao anterior (RSP != 0) */
    for (i = 0; i < timeout; i++) {
        if (BcSmnReadRaw(rspReg) != 0) {
            break;
        }
        BcpSleepMs(1);
    }
    if (i >= timeout) {
        KeReleaseMutex(&g_Dev.SmuMutex, FALSE);
        BcLogEvent(BC250DBG_LVL_ERROR, BC250DBG_SUB_SMU, BC250DBG_EV_SMU_MSG,
                   Queue, Msg, Arg, 0);
        return STATUS_IO_TIMEOUT;
    }

    /* 2) limpar RSP, 3) argumento, 4) disparar */
    BcSmnWriteRaw(rspReg, 0);
    BcSmnWriteRaw(argReg, Arg);
    if (argHiReg != 0) {
        BcSmnWriteRaw(argHiReg, ArgHi);
    }
    BcSmnWriteRaw(cmdReg, Msg);

    /* 5) esperar conclusao */
    for (i = 0; i < timeout; i++) {
        status = BcSmnReadRaw(rspReg);
        if (status != 0) {
            break;
        }
        BcpSleepMs(1);
    }

    /* 6) ler resultado */
    result = BcSmnReadRaw(argReg);

    KeReleaseMutex(&g_Dev.SmuMutex, FALSE);

    *SmuStatus = status;
    *Result = result;

    BcLogEvent(status == BC250_SMU_OK ? BC250DBG_LVL_INFO : BC250DBG_LVL_WARN,
               BC250DBG_SUB_SMU, BC250DBG_EV_SMU_MSG,
               Queue, Msg, Arg, status);

    return (status == 0) ? STATUS_IO_TIMEOUT : STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* Coleta de informacao para o GET_INFO                                      */
/* ------------------------------------------------------------------------ */

static ULONG BcpSmuQuery(ULONG Msg, ULONG *Ok)
{
    ULONG status = 0, result = 0;
    NTSTATUS st = BcSmuMsg(0, Msg, 0, 0, 200, &status, &result);

    if (!NT_SUCCESS(st) || status != BC250_SMU_OK) {
        *Ok = 0;
        return 0;
    }
    *Ok = 1;
    return result;
}

VOID BcHwCollectInfo(BC250DBG_INFO *Info)
{
    ULONG ok = 0, i;

    RtlZeroMemory(Info, sizeof(*Info));
    Info->Size          = sizeof(*Info);
    Info->AbiVersion    = BC250DBG_ABI_VERSION;
    Info->DriverVersion = BC250DBG_DRIVER_VERSION;
    Info->ArmLevel      = (ULONG)g_Dev.ArmLevel;

    Info->PciFound   = g_Dev.PciFound ? 1u : 0u;
    Info->PciBus     = g_Dev.PciBus;
    Info->PciDev     = g_Dev.PciDev;
    Info->PciFn      = g_Dev.PciFn;
    Info->VendorId   = g_Dev.VendorId;
    Info->DeviceId   = g_Dev.DeviceId;
    Info->RevisionId = g_Dev.RevisionId;

    for (i = 0; i < BC250DBG_MAX_BARS; i++) {
        Info->Bar[i] = g_Dev.Bar[i];
    }

    Info->MmioPhysical = (UINT64)g_Dev.MmioPhysical.QuadPart;
    Info->MmioLength   = g_Dev.MmioLength;
    Info->MmioMapped   = (g_Dev.Mmio != NULL) ? 1u : 0u;

    if (g_Dev.Mmio != NULL) {
        Info->GpuId          = BcRegReadRaw(BC250_REG_GPU_ID);
        Info->ScratchReg0    = BcRegReadRaw(BC250_REG_SCRATCH0);
        Info->ScratchReg0B   = BcRegReadRaw(BC250_REG_SCRATCH0_LEGACY);
        Info->GrbmStatus     = BcRegReadRaw(BC250_REG_GRBM_STATUS);
        Info->GrbmStatusB    = BcRegReadRaw(BC250_REG_GRBM_STATUS_LEGACY);
        Info->CcShaderArray  = BcRegReadRaw(BC250_REG_CC_ARRAY);
        Info->CcShaderArrayB = BcRegReadRaw(BC250_REG_CC_ARRAY_LEGACY);

        Info->SmuVersion  = BcpSmuQuery(PPSMC_GetSmuVersion, &ok);
        Info->SmuAlive    = ok;
        if (ok) {
            Info->SmuDriverIf = BcpSmuQuery(PPSMC_GetDriverIfVersion, &ok);
            Info->SmuFeatures = BcpSmuQuery(PPSMC_GetEnabledSmuFeatures, &ok);
            Info->GfxFreqMhz  = BcpSmuQuery(PPSMC_GetGfxFrequency, &ok);
            Info->GfxVid      = BcpSmuQuery(PPSMC_GetGfxVid, &ok);
            Info->ActiveWgp   = BcpSmuQuery(PPSMC_QueryActiveWgp, &ok);
        }
    }

    if (g_Dev.LogHeader != NULL) {
        Info->LogEntryCount = g_Dev.LogEntryCount;
        Info->LogEntrySize  = BC250DBG_LOG_ENTRY_SIZE;
        Info->LogHead       = g_Dev.LogHeader->Head;
        Info->LogDropped    = g_Dev.LogHeader->Dropped;
    }

    Info->ThermalTrusted = (ULONG)g_Dev.ThermalTrusted;
    Info->ThermalLimitC  = (ULONG)g_Dev.ThermalLimitC;
    Info->ThermalTripped = (ULONG)g_Dev.ThermalTripped;
    Info->ThermalMilliC  = (LONG)g_Dev.ThermalMilliC;
}

/* ------------------------------------------------------------------------ */
/* Registradores SDMA0. Offsets em bytes no BAR5, convencao SOC15 do amdgpu:
 *
 *     byte = (GC_BASE_segN + reg_dword) * 4
 *
 * com GC_BASE seg0 = 0x1260 e seg1 = 0xA000 (Cyan Skillfish). Os numeros de
 * dword vem de gc_10_1_0_offset.h (mmSDMA0_*), o BASE_IDX de cada registrador
 * escolhe o segmento. A SDMA0 nao tem base propria: divide o espaco do GC. */
#define A_SDMA0_SEM_WAIT_FAIL_TIMER_CNTL 0x04A04  /* dw 0x0021 seg0 */
#define A_SDMA0_CNTL                     0x049F0  /* dw 0x001c seg0 */
#define A_SDMA0_CHICKEN_BITS             0x049F4  /* dw 0x001d seg0 */
#define A_SDMA0_GB_ADDR_CONFIG           0x049F8  /* dw 0x001e seg0 */
#define A_SDMA0_GB_ADDR_CONFIG_READ      0x049FC  /* dw 0x001f seg0 */
#define A_SDMA0_STATUS_REG               0x04A14  /* dw 0x0025 seg0 */
#define A_SDMA0_STATUS2_REG              0x04A60  /* dw 0x0018 seg0 (mmSDMA0_STATUS2_REG); F32_INSTR_PTR bits 2..15 */
#define A_SDMA0_CLK_CTRL                 0x049EC  /* dw 0x001b seg0 */
#define A_SDMA0_UCODE_CHECKSUM           0x04A24  /* dw 0x0029 seg0 */
#define A_SDMA0_F32_CNTL                 0x04A28  /* dw 0x002a seg0 */
#define A_SDMA0_UTCL1_CNTL               0x04A70  /* dw 0x003c seg0 */
#define A_SDMA0_UTCL1_PAGE               0x04AA0  /* dw 0x0048 seg0 */
#define A_SDMA0_GFX_RB_CNTL              0x04B80  /* dw 0x0080 seg0 */
#define A_SDMA0_GFX_RB_BASE              0x04B84  /* dw 0x0081 seg0 */
#define A_SDMA0_GFX_RB_BASE_HI           0x04B88  /* dw 0x0082 seg0 */
#define A_SDMA0_GFX_RB_RPTR              0x04B8C  /* dw 0x0083 seg0 */
#define A_SDMA0_GFX_RB_RPTR_HI           0x04B90  /* dw 0x0084 seg0 */
#define A_SDMA0_GFX_RB_WPTR              0x04B94  /* dw 0x0085 seg0 */
#define A_SDMA0_GFX_RB_WPTR_HI           0x04B98  /* dw 0x0086 seg0 */
#define A_SDMA0_GFX_RB_WPTR_POLL_CNTL    0x04B9C  /* dw 0x0087 seg0 */
#define A_SDMA0_GFX_RB_RPTR_ADDR_HI      0x04BA0  /* dw 0x0088 seg0 */
#define A_SDMA0_GFX_RB_RPTR_ADDR_LO      0x04BA4  /* dw 0x0089 seg0 */
#define A_SDMA0_GFX_IB_CNTL              0x04BA8  /* dw 0x008a seg0 */
#define A_SDMA0_GFX_DOORBELL             0x04BC8  /* dw 0x0092 seg0 */
#define A_SDMA0_GFX_DOORBELL_OFFSET      0x04C2C  /* dw 0x00ab seg0 */
#define A_SDMA0_GFX_MINOR_PTR_UPDATE     0x04C54  /* dw 0x00b5 seg0 */
#define A_SDMA0_GFX_RB_WPTR_POLL_ADDR_HI 0x04C48  /* dw 0x00b2 seg0 */
#define A_SDMA0_GFX_RB_WPTR_POLL_ADDR_LO 0x04C4C  /* dw 0x00b3 seg0 */
#define A_SDMA0_UCODE_ADDR               0x3E200  /* dw 0x5880 seg1 */
#define A_SDMA0_UCODE_DATA               0x3E204  /* dw 0x5881 seg1 */

/* Campos de bit usados, de gc_10_1_0_sh_mask.h. */
#define F_SDMA0_F32_CNTL__HALT              0x00000001u
#define F_SDMA0_F32_CNTL__CHECKSUM_CLR      0x00000100u
#define F_SDMA0_GFX_RB_CNTL__RB_ENABLE      0x00000001u
#define F_SDMA0_GFX_RB_CNTL__RB_SIZE_MASK   0x0000003Eu
#define F_SDMA0_GFX_RB_CNTL__RB_SIZE_SHIFT  1
#define F_SDMA0_GFX_RB_CNTL__RPTR_WB_ENABLE 0x00001000u
#define F_SDMA0_CNTL__UTC_L1_ENABLE         0x00000002u
#define F_SDMA0_CNTL__MIDCMD_PREEMPT_ENABLE 0x00000020u
#define F_SDMA0_UTCL1_CNTL__RESP_MODE_MASK  0x00000E00u
#define F_SDMA0_UTCL1_CNTL__RESP_MODE_SHIFT 9
#define F_SDMA0_UTCL1_CNTL__REDO_DELAY_MASK 0x0000003Eu
#define F_SDMA0_UTCL1_CNTL__REDO_DELAY_SHIFT 1
#define F_SDMA0_GFX_DOORBELL__ENABLE        0x10000000u
#define F_SDMA0_GFX_IB_CNTL__IB_ENABLE      0x00000001u
#define F_SDMA0_GFX_MINOR_PTR_UPDATE__ENABLE 0x00000001u
#define F_SDMA0_GFX_RB_WPTR_POLL_CNTL__F32_POLL_ENABLE 0x00000004u
#define F_SDMA0_F32_CNTL__RESET             0x00000200u
#define F_SDMA0_CNTL__AUTO_CTXSW_ENABLE     0x00040000u
#define F_SDMA0_CLK_CTRL__SOFT_OVERRIDE_ALL 0xFF000000u

/* Registradores GCVM / GART / VMID 0. Seg0 = 0x1260 */
#define A_GCVM_L2_CNTL                       0x0A100  /* dw 0x15e0 seg0 */
#define A_GCVM_L2_CNTL2                      0x0A104  /* dw 0x15e1 seg0 */
#define A_GCVM_L2_CNTL3                      0x0A108  /* dw 0x15e2 seg0 */
#define A_GCVM_L2_CNTL4                      0x0A160  /* dw 0x15f8 seg0 */
#define A_GCVM_L2_CNTL5                      0x0A184  /* dw 0x1601 seg0 */
#define A_GCVM_L2_PROTECTION_FAULT_STATUS    0x0A130  /* dw 0x15ec seg0 */
#define A_GCVM_CONTEXT0_CNTL                 0x0A200  /* dw 0x1620 seg0 */
#define A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO 0x0A3AC /* dw 0x168b seg0 */
#define A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI 0x0A3B0 /* dw 0x168c seg0 */
#define A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO 0x0A42C /* dw 0x16ab seg0 */
#define A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI 0x0A430 /* dw 0x16ac seg0 */
#define A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO   0x0A4AC /* dw 0x16cb seg0 */
#define A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI   0x0A4B0 /* dw 0x16cc seg0 */
#define A_GCMC_VM_FB_OFFSET                  0x0A5AC  /* dw 0x170b seg0 */
#define A_GCMC_VM_FB_LOCATION_BASE           0x0A600  /* dw 0x1720 seg0 */
#define A_GCMC_VM_FB_LOCATION_TOP            0x0A604  /* dw 0x1721 seg0 */
#define A_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR   0x0A614  /* dw 0x1725 seg0 */
#define A_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR  0x0A618  /* dw 0x1726 seg0 */
#define A_GCMC_VM_MX_L1_TLB_CNTL             0x0A61C  /* dw 0x1727 seg0 */
#define A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB 0x0A5B0 /* dw 0x170c seg0 */
#define A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB 0x0A5B4 /* dw 0x170d seg0 */
#define A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO 0x0A148 /* dw 0x15f2 seg0 */
#define A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI 0x0A14C /* dw 0x15f3 seg0 */
#define A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO 0x0A150 /* dw 0x15f4 seg0 */
#define A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI 0x0A154 /* dw 0x15f5 seg0 */
#define A_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO 0x0A158 /* dw 0x15f6 seg0 */
#define A_GCVM_INVALIDATE_ENG17_SEM 0x0A288 /* dw 0x1642 seg0 */
#define A_GCVM_INVALIDATE_ENG17_REQ 0x0A2D0 /* dw 0x1654 seg0 */
#define A_GCVM_INVALIDATE_ENG17_ACK 0x0A318 /* dw 0x1666 seg0 */
/* ------------------------------------------------------------------------ */


/* SDMA Ring Buffer                                                          */
/* ------------------------------------------------------------------------ */


/* soc15_program_register_sequence: os bits fora da and_mask sobrevivem. */
static VOID BcSdmaGoldenRmw(ULONG Offset, ULONG AndMask, ULONG OrMask)
{
    ULONG v = BcRegReadRaw(Offset);
    v &= ~AndMask;
    v |= (OrMask & AndMask);
    BcRegWriteRaw(Offset, v);
}

NTSTATUS BcSdmaInit(ULONG RingSizeBytes, ULONG Placement, ULONG VramOffset,
                    ULONGLONG *OutGpuAddr)
{
    PHYSICAL_ADDRESS lowAddr, highAddr, boundary;
    ULONG size = (RingSizeBytes > 0) ? RingSizeBytes : 4096;
    ULONG rbCntl, rbBufsz, ringDwords, temp, doorbell, doorbellWas;
    ULONGLONG gpuAddr;
    ULONG i;

    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        return STATUS_ACCESS_DENIED;
    }

    if (g_Dev.SdmaRingBuffer != NULL || g_Dev.SdmaRingGpuAddr != 0) {
        if (OutGpuAddr) *OutGpuAddr = g_Dev.SdmaRingGpuAddr;
        return STATUS_SUCCESS;
    }

    if (Placement == BC250DBG_SDMA_RING_VRAM) {
        NTSTATUS st = BcVramMap();
        if (!NT_SUCCESS(st)) {
            return st;
        }

        /* O offset precisa estar alinhado a 256 bytes: RB_BASE guarda o
         * endereco deslocado de 8 bits, entao os 8 bits baixos somem. */
        VramOffset &= ~0xFFu;

        if ((ULONGLONG)VramOffset + size > (ULONGLONG)g_Dev.VramLength) {
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                      "offset 0x%X + %u bytes passa da janela de VRAM (%u MB)",
                      VramOffset, size, g_Dev.VramLength / (1024 * 1024));
            return STATUS_INVALID_PARAMETER;
        }

        /* Zera o anel pela abertura. Escrita de dword, nao RtlZeroMemory:
         * memoria de dispositivo nao aceita as instrucoes de bloco que o
         * compilador pode gerar. */
        for (i = 0; i < size; i += 4) {
            BcVramWriteRaw(VramOffset + i, 0);
        }

        gpuAddr = BC250_VRAM_GPU_BASE + (ULONGLONG)VramOffset;

        g_Dev.SdmaRingBuffer     = NULL;
        g_Dev.SdmaRingPhysical.QuadPart = 0;
        g_Dev.SdmaRingVramOffset = VramOffset;
        g_Dev.SdmaRingPlacement  = BC250DBG_SDMA_RING_VRAM;

        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_SDMA,
                  "SDMA0 ring na VRAM: offset 0x%X, GPU 0x%llx, %u bytes",
                  VramOffset, gpuAddr, size);
    } else {
        lowAddr.QuadPart = 0;
        highAddr.QuadPart = 0xFFFFFFFFFFull;
        boundary.QuadPart = 0;

        g_Dev.SdmaRingBuffer = MmAllocateContiguousMemorySpecifyCache(
            size, lowAddr, highAddr, boundary, MmNonCached);
        if (g_Dev.SdmaRingBuffer == NULL) {
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                      "MmAllocateContiguousMemory falhou para SDMA ring (%u bytes)", size);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(g_Dev.SdmaRingBuffer, size);
        g_Dev.SdmaRingPhysical   = MmGetPhysicalAddress(g_Dev.SdmaRingBuffer);
        g_Dev.SdmaRingVramOffset = 0;
        g_Dev.SdmaRingPlacement  = BC250DBG_SDMA_RING_SYSRAM;

        gpuAddr = (ULONGLONG)g_Dev.SdmaRingPhysical.QuadPart;

        /* Aviso: em RAM do sistema o endereco fisico so funciona se o GART e
         * o VMID 0 estiverem programados. Medido nesta placa em 2026-08-19:
         * nao estao, e a leitura do anel pendura (RB_MC_RREQ_IDLE=0). Use
         * BC250DBG_SDMA_RING_VRAM. */
        BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_SDMA,
                  "SDMA0 ring em RAM do sistema (PA 0x%llx): sem GART/VMID0 a "
                  "SDMA nao alcanca esse endereco", gpuAddr);
    }

    g_Dev.SdmaRingSize    = size;
    g_Dev.SdmaWptr        = 0;
    g_Dev.SdmaRingGpuAddr = gpuAddr;

    /* Sequencia espelhada de sdma_v5_0_gfx_resume_instance() do amdgpu.
     * A ordem importa: RB_CNTL com o tamanho (mas sem RB_ENABLE) primeiro,
     * ponteiros e base depois, e RB_ENABLE somente no final, ja com o motor
     * F32 rodando. */

    BcRegWriteRaw(A_SDMA0_SEM_WAIT_FAIL_TIMER_CNTL, 0);

    /* RB_SIZE e log2 do tamanho do anel em dwords (order_base_2(size/4)). */
    ringDwords = size / (ULONG)sizeof(ULONG);
    rbBufsz = 0;
    while ((1UL << (rbBufsz + 1)) <= ringDwords) {
        rbBufsz++;
    }

    rbCntl = BcRegReadRaw(A_SDMA0_GFX_RB_CNTL);
    rbCntl &= ~F_SDMA0_GFX_RB_CNTL__RB_SIZE_MASK;
    rbCntl |= (rbBufsz << F_SDMA0_GFX_RB_CNTL__RB_SIZE_SHIFT)
              & F_SDMA0_GFX_RB_CNTL__RB_SIZE_MASK;
    rbCntl &= ~F_SDMA0_GFX_RB_CNTL__RB_ENABLE;
    /* Sem writeback: nao temos buffer de RPTR mapeado para a GPU, entao
     * BcSdmaStatus le o RPTR direto por MMIO. */
    rbCntl &= ~F_SDMA0_GFX_RB_CNTL__RPTR_WB_ENABLE;
    BcRegWriteRaw(A_SDMA0_GFX_RB_CNTL, rbCntl);

    BcRegWriteRaw(A_SDMA0_GFX_RB_RPTR, 0);
    BcRegWriteRaw(A_SDMA0_GFX_RB_RPTR_HI, 0);
    BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR, 0);
    BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_HI, 0);

    /* Shadow do WPTR e writeback do RPTR, ambos na VRAM. O amdgpu faz assim e
     * nos nao fazíamos: o F32 percebe trabalho novo pelo poll do shadow, e
     * reporta progresso escrevendo o RPTR no writeback. Ler o registrador de
     * RPTR nao serve — medimos que ele so acompanha o WPTR. */
    if (g_Dev.SdmaRingPlacement == BC250DBG_SDMA_RING_VRAM) {
        ULONGLONG wptrShadowGpu = BC250_VRAM_GPU_BASE + BC250_SDMA_WPTR_SHADOW_OFFSET;
        ULONGLONG rptrWbGpu     = BC250_VRAM_GPU_BASE + BC250_SDMA_RPTR_WB_OFFSET;

        BcVramWriteRaw(BC250_SDMA_WPTR_SHADOW_OFFSET, 0);
        BcVramWriteRaw(BC250_SDMA_WPTR_SHADOW_OFFSET + 4, 0);
        BcVramWriteRaw(BC250_SDMA_RPTR_WB_OFFSET, 0);
        BcVramWriteRaw(BC250_SDMA_RPTR_WB_OFFSET + 4, 0);

        BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_POLL_ADDR_LO, (ULONG)(wptrShadowGpu & 0xFFFFFFFFull));
        BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_POLL_ADDR_HI, (ULONG)(wptrShadowGpu >> 32));
        BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_POLL_CNTL, 0x00403000u | F_SDMA0_GFX_RB_WPTR_POLL_CNTL__F32_POLL_ENABLE);

        BcRegWriteRaw(A_SDMA0_GFX_RB_RPTR_ADDR_HI, (ULONG)(rptrWbGpu >> 32));
        BcRegWriteRaw(A_SDMA0_GFX_RB_RPTR_ADDR_LO, (ULONG)(rptrWbGpu & 0xFFFFFFFCull));
        rbCntl |= F_SDMA0_GFX_RB_CNTL__RPTR_WB_ENABLE;
    } else {
        BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_POLL_ADDR_LO, 0);
        BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_POLL_ADDR_HI, 0);
        BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_POLL_CNTL, 0);
        BcRegWriteRaw(A_SDMA0_GFX_RB_RPTR_ADDR_HI, 0);
        BcRegWriteRaw(A_SDMA0_GFX_RB_RPTR_ADDR_LO, 0);
    }

    /* Golden registers para Cyan Skillfish, de golden_settings_sdma_cyan_skillfish[]
     * em sdma_v5_0.c.
     *
     * ATENCAO A SEMANTICA: SOC15_REG_GOLDEN_VALUE(ip, inst, reg, and_mask,
     * or_mask) NAO e escrita cega. O soc15_program_register_sequence faz
     *
     *     temp  = RREG32(reg);
     *     temp &= ~and_mask;
     *     temp |= (or_mask & and_mask);
     *     WREG32(reg, temp);
     *
     * ou seja, os bits FORA da and_mask sao PRESERVADOS do valor que o POST
     * deixou. Escrever o or_mask direto zera esses bits.
     *
     * Isso nos custou caro: com a escrita cega, GB_ADDR_CONFIG perdeu os bits
     * de 0xFFE78800 (configuracao de enderecamento de memoria) e o firmware
     * da SDMA passou a travar num laco de 3 instrucoes no ponto de entrada
     * (PC oscilando entre 0x0E e 0x10) em vez de alcancar o laco principal. */
    BcSdmaGoldenRmw(A_SDMA0_CHICKEN_BITS,      0xffbf1f0fu, 0x03ab0107u);
    BcSdmaGoldenRmw(A_SDMA0_GB_ADDR_CONFIG,    0x001877ffu, 0x00000044u);
    BcSdmaGoldenRmw(A_SDMA0_GB_ADDR_CONFIG_READ, 0x001877ffu, 0x00000044u);

    BcRegWriteRaw(A_SDMA0_GFX_RB_BASE, (ULONG)(gpuAddr >> 8));
    BcRegWriteRaw(A_SDMA0_GFX_RB_BASE_HI, (ULONG)(gpuAddr >> 40));

    /* MINOR_PTR_UPDATE precisa estar em 1 enquanto o WPTR e reprogramado
     * para um valor menor que o atual. */
    BcRegWriteRaw(A_SDMA0_GFX_MINOR_PTR_UPDATE, F_SDMA0_GFX_MINOR_PTR_UPDATE__ENABLE);
    BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR, 0);
    BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_HI, 0);

    /* Doorbell OFF. Se o POST/VBIOS deixou o doorbell armado, a SDMA ignora
     * escritas de WPTR via MMIO e o RPTR nunca avanca. */
    doorbell = BcRegReadRaw(A_SDMA0_GFX_DOORBELL);
    doorbellWas = doorbell;
    doorbell &= ~F_SDMA0_GFX_DOORBELL__ENABLE;
    BcRegWriteRaw(A_SDMA0_GFX_DOORBELL, doorbell);

    BcRegWriteRaw(A_SDMA0_GFX_MINOR_PTR_UPDATE, 0);

    /* Clock gating da SDMA desligado: o amdgpu faz isso quando cg_flags = 0,
     * e para Cyan Skillfish cg_flags e zero. Setar os SOFT_OVERRIDE forca os
     * clocks ligados. */
    temp = BcRegReadRaw(A_SDMA0_CLK_CTRL);
    BcRegWriteRaw(A_SDMA0_CLK_CTRL, temp | F_SDMA0_CLK_CTRL__SOFT_OVERRIDE_ALL);

    /* UTCL1 ligada: sem isso a SDMA nao traduz o endereco do anel.
     * AUTO_CTXSW_ENABLE vem de sdma_v5_0_ctx_switch_enable. */
    temp = BcRegReadRaw(A_SDMA0_CNTL);
    temp |= F_SDMA0_CNTL__UTC_L1_ENABLE | F_SDMA0_CNTL__MIDCMD_PREEMPT_ENABLE
          | F_SDMA0_CNTL__AUTO_CTXSW_ENABLE;
    BcRegWriteRaw(A_SDMA0_CNTL, temp);

    temp = BcRegReadRaw(A_SDMA0_UTCL1_CNTL);
    temp &= ~F_SDMA0_UTCL1_CNTL__RESP_MODE_MASK;
    temp |= (3u << F_SDMA0_UTCL1_CNTL__RESP_MODE_SHIFT)
            & F_SDMA0_UTCL1_CNTL__RESP_MODE_MASK;
    temp &= ~F_SDMA0_UTCL1_CNTL__REDO_DELAY_MASK;
    temp |= (9u << F_SDMA0_UTCL1_CNTL__REDO_DELAY_SHIFT)
            & F_SDMA0_UTCL1_CNTL__REDO_DELAY_MASK;
    BcRegWriteRaw(A_SDMA0_UTCL1_CNTL, temp);

    /* Politica default de cache de leitura/escrita na L2 (golden Cyan Skillfish) */
    /* amdgpu: temp &= 0xFF0FFF; temp |= politicas de cache. Mesma logica de
     * preservacao — nao e escrita cega. */
    BcSdmaGoldenRmw(A_SDMA0_UTCL1_PAGE, 0x00FFF000u, 0x004c5c00u);

    /* Tira o F32 do halt antes de armar o anel. */
    temp = BcRegReadRaw(A_SDMA0_F32_CNTL);
    temp &= ~F_SDMA0_F32_CNTL__HALT;
    BcRegWriteRaw(A_SDMA0_F32_CNTL, temp);

    /* Agora sim: liga o anel e os IBs. */
    rbCntl |= F_SDMA0_GFX_RB_CNTL__RB_ENABLE;
    BcRegWriteRaw(A_SDMA0_GFX_RB_CNTL, rbCntl);

    temp = BcRegReadRaw(A_SDMA0_GFX_IB_CNTL);
    temp |= F_SDMA0_GFX_IB_CNTL__IB_ENABLE;
    BcRegWriteRaw(A_SDMA0_GFX_IB_CNTL, temp);

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_SDMA,
              "SDMA0 Ring inicializado: %s, RB_BASE GPU 0x%llx (>>8 = 0x%x), "
              "%u bytes, RB_SIZE=%u, RB_CNTL=0x%08X, DOORBELL 0x%08X -> 0x%08X",
              (g_Dev.SdmaRingPlacement == BC250DBG_SDMA_RING_VRAM) ? "VRAM" : "RAM do sistema",
              gpuAddr, (ULONG)(gpuAddr >> 8), size,
              rbBufsz, rbCntl, doorbellWas, doorbell);

    if (doorbellWas & F_SDMA0_GFX_DOORBELL__ENABLE) {
        BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_SDMA,
                  "SDMA0 doorbell estava ARMADO pelo POST e foi desligado; "
                  "escritas de WPTR por MMIO estavam sendo ignoradas ate agora");
    }

    if (OutGpuAddr) *OutGpuAddr = gpuAddr;
    return STATUS_SUCCESS;
}

VOID BcSdmaFree(VOID)
{
    if (g_Dev.SdmaRingGpuAddr == 0 && g_Dev.SdmaRingBuffer == NULL) {
        return;
    }

    if (g_Dev.Mmio != NULL) {
        BcRegWriteRaw(A_SDMA0_GFX_RB_CNTL, 0);
    }

    if (g_Dev.SdmaRingBuffer != NULL) {
        MmFreeContiguousMemory(g_Dev.SdmaRingBuffer);
        g_Dev.SdmaRingBuffer = NULL;
    }

    g_Dev.SdmaRingPhysical.QuadPart = 0;
    g_Dev.SdmaRingGpuAddr    = 0;
    g_Dev.SdmaRingVramOffset = 0;
    g_Dev.SdmaRingPlacement  = BC250DBG_SDMA_RING_SYSRAM;
    g_Dev.SdmaRingSize       = 0;
    g_Dev.SdmaWptr           = 0;
}

NTSTATUS BcSdmaSubmit(const ULONG *Dwords, ULONG Count, ULONG *RptrBefore, ULONG *WptrAfter, ULONG *RptrAfter)
{
    ULONG rptrBefore, wptr, rptrAfter = 0, i;
    ULONG *ring;
    ULONG ringDwords;
    LARGE_INTEGER timeout;

    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        return STATUS_ACCESS_DENIED;
    }
    if (g_Dev.SdmaRingGpuAddr == 0 || Count == 0 || Count > 64) {
        return STATUS_INVALID_PARAMETER;
    }

    ringDwords = g_Dev.SdmaRingSize / sizeof(ULONG);

    rptrBefore = (g_Dev.SdmaRingPlacement == BC250DBG_SDMA_RING_VRAM)
               ? BcVramReadRaw(BC250_SDMA_RPTR_WB_OFFSET)
               : BcRegReadRaw(A_SDMA0_GFX_RB_RPTR);
    if (RptrBefore) *RptrBefore = rptrBefore;

    wptr = g_Dev.SdmaWptr;

    if (g_Dev.SdmaRingPlacement == BC250DBG_SDMA_RING_VRAM) {
        for (i = 0; i < Count; i++) {
            ULONG slot = (wptr / 4) % ringDwords;
            BcVramWriteRaw(g_Dev.SdmaRingVramOffset + slot * 4, Dwords[i]);
            wptr += 4;
        }
    } else {
        ring = (ULONG *)g_Dev.SdmaRingBuffer;
        for (i = 0; i < Count; i++) {
            ring[(wptr / 4) % ringDwords] = Dwords[i];
            wptr += 4;
        }
    }

    g_Dev.SdmaWptr = wptr;
    KeMemoryBarrier();

    /* O F32 percebe trabalho novo pelo shadow do WPTR, nao pelo registrador.
     * Escrevemos nos dois: o shadow para o poll, e o registrador porque o
     * caminho bare-metal do amdgpu tambem escreve la. */
    if (g_Dev.SdmaRingPlacement == BC250DBG_SDMA_RING_VRAM) {
        BcVramWriteRaw(BC250_SDMA_WPTR_SHADOW_OFFSET, wptr);
        BcVramWriteRaw(BC250_SDMA_WPTR_SHADOW_OFFSET + 4, 0);
        BcHdpReadCacheInvalidate();
    }

    /* Cerca de ordenacao antes de chutar o WPTR.
     *
     * Duas coisas dependem disso: os dwords do pacote que acabamos de escrever
     * no anel, e qualquer escrita que a CPU tenha feito na regiao de destino.
     * Sem a cerca, escritas da CPU em voo pelo HDP podem pousar DEPOIS da
     * escrita da GPU e sobrescreve-la. Medido nesta placa em 2026-08-19. */
    if (g_Dev.Vram != NULL) {
        (VOID)BcVramReadRaw(g_Dev.SdmaRingVramOffset);
    }
    BcHdpReadCacheInvalidate();
    KeMemoryBarrier();

    /* O WPTR so late com MINOR_PTR_UPDATE armado. Medido na BC-250 em
     * 2026-08-19: escrita direta com MINOR_PTR_UPDATE=0 e descartada e o
     * registrador continua lendo o valor antigo. */
    BcRegWriteRaw(A_SDMA0_GFX_MINOR_PTR_UPDATE, F_SDMA0_GFX_MINOR_PTR_UPDATE__ENABLE);
    BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR, wptr);
    BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_HI, 0);
    BcRegWriteRaw(A_SDMA0_GFX_MINOR_PTR_UPDATE, 0);

    if (WptrAfter) *WptrAfter = wptr;

    /* Progresso vem do writeback, nao do registrador: sdma_v5_0_ring_get_rptr
     * do amdgpu le exatamente daqui. */
    timeout.QuadPart = -10000;
    for (i = 0; i < 100; i++) {
        ULONG rptrWb = (g_Dev.SdmaRingPlacement == BC250DBG_SDMA_RING_VRAM)
                     ? BcVramReadRaw(BC250_SDMA_RPTR_WB_OFFSET) : 0;
        ULONG rptrReg = BcRegReadRaw(A_SDMA0_GFX_RB_RPTR);
        rptrAfter = (rptrWb == wptr) ? rptrWb : rptrReg;
        if (rptrAfter == wptr) {
            break;
        }
        KeDelayExecutionThread(KernelMode, FALSE, &timeout);
    }

    if (RptrAfter) *RptrAfter = rptrAfter;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_SDMA,
              "SDMA0 Submit %u dw: WPTR 0x%X -> RPTR 0x%X (%s)",
              Count, wptr, rptrAfter, (rptrAfter == wptr) ? "CONCLUIDO" : "TIMEOUT");

    return STATUS_SUCCESS;
}

NTSTATUS BcSdmaStatus(ULONG *Rptr, ULONG *Wptr, ULONG *RbCntl, ULONG *StatusReg,
                      PHYSICAL_ADDRESS *RbBase, ULONG *RptrWb, ULONG *WptrShadow)
{
    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (Rptr)      *Rptr      = BcRegReadRaw(A_SDMA0_GFX_RB_RPTR);
    if (Wptr)      *Wptr      = BcRegReadRaw(A_SDMA0_GFX_RB_WPTR);
    if (RbCntl)    *RbCntl    = BcRegReadRaw(A_SDMA0_GFX_RB_CNTL);
    if (StatusReg) *StatusReg = BcRegReadRaw(A_SDMA0_STATUS_REG);
    if (RbBase)    RbBase->QuadPart = (LONGLONG)g_Dev.SdmaRingGpuAddr;
    if (RptrWb) {
        *RptrWb = (g_Dev.SdmaRingPlacement == BC250DBG_SDMA_RING_VRAM)
                ? BcVramReadRaw(BC250_SDMA_RPTR_WB_OFFSET) : 0;
    }
    if (WptrShadow) {
        *WptrShadow = (g_Dev.SdmaRingPlacement == BC250DBG_SDMA_RING_VRAM)
                    ? BcVramReadRaw(BC250_SDMA_WPTR_SHADOW_OFFSET) : 0;
    }

    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* Reinicio do microcontrolador F32                                          */
/*                                                                           */
/* Depois que o PSP carrega o microcodigo, tirar o HALT NAO basta: o F32      */
/* fica parado no ponto de entrada (PC = 0x0001) e so avanca uma instrucao    */
/* por escrita em F32_CNTL. E preciso um pulso no bit RESET.                  */
/*                                                                           */
/* Medido nesta placa em 2026-08-19: com o firmware carregado e HALT=0 o PC   */
/* ficava congelado em 0x0001; apos a sequencia HALT=1 / RESET=1 / RESET=0 /  */
/* HALT=0 o PC passou a saltar (0x01D6, 0x00A3, 0x0178, 0x00BB, 0x0178 — com  */
/* desvios e repeticao, ou seja, um laco principal de verdade) e a SDMA0      */
/* passou a consumir o ring buffer.                                          */
/* ------------------------------------------------------------------------ */

static ULONG BcSdmaF32Pc(VOID)
{
    return (BcRegReadRaw(A_SDMA0_STATUS2_REG) >> 2) & 0x3FFFu;
}

NTSTATUS BcSdmaF32Reset(ULONG *OutPcBefore, ULONG *OutPcAfter, ULONG *OutMoved)
{
    LARGE_INTEGER delay;
    ULONG pcBefore, pc, prev, moved = 0, i, clk, cntl;

    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        return STATUS_ACCESS_DENIED;
    }

    pcBefore = BcSdmaF32Pc();
    if (OutPcBefore) *OutPcBefore = pcBefore;

    /* Clock gating da SDMA desligado, como o amdgpu faz quando cg_flags = 0 —
     * e para Cyan Skillfish cg_flags e zero. Setar os SOFT_OVERRIDE forca os
     * clocks ligados. */
    clk = BcRegReadRaw(A_SDMA0_CLK_CTRL);
    BcRegWriteRaw(A_SDMA0_CLK_CTRL, clk | F_SDMA0_CLK_CTRL__SOFT_OVERRIDE_ALL);

    /* AUTO_CTXSW_ENABLE — sdma_v5_0_ctx_switch_enable, parte do
     * sdma_v5_0_start que faltava. */
    cntl = BcRegReadRaw(A_SDMA0_CNTL);
    BcRegWriteRaw(A_SDMA0_CNTL, cntl | F_SDMA0_CNTL__AUTO_CTXSW_ENABLE);

    /* Pulso de reset com o F32 parado. */
    BcRegWriteRaw(A_SDMA0_F32_CNTL, F_SDMA0_F32_CNTL__HALT);
    BcRegWriteRaw(A_SDMA0_F32_CNTL, F_SDMA0_F32_CNTL__HALT | F_SDMA0_F32_CNTL__RESET);

    delay.QuadPart = -500000;   /* 50 ms */
    KeDelayExecutionThread(KernelMode, FALSE, &delay);

    BcRegWriteRaw(A_SDMA0_F32_CNTL, F_SDMA0_F32_CNTL__HALT);
    BcRegWriteRaw(A_SDMA0_F32_CNTL, 0);

    delay.QuadPart = -1000000;  /* 100 ms para o firmware subir */
    KeDelayExecutionThread(KernelMode, FALSE, &delay);

    /* O PC precisa se mover sozinho. Se ficar parado, nao ha microcodigo
     * executando e nao adianta armar o anel. */
    prev = BcSdmaF32Pc();
    pc = prev;
    for (i = 0; i < 16; i++) {
        pc = BcSdmaF32Pc();
        if (pc != prev) moved++;
        prev = pc;
    }

    if (OutPcAfter) *OutPcAfter = pc;
    if (OutMoved)   *OutMoved   = moved;

    BcLogText(moved ? BC250DBG_LVL_INFO : BC250DBG_LVL_WARN, BC250DBG_SUB_SDMA,
              "SDMA0 F32 reset: PC 0x%04X -> 0x%04X, %u transicoes em 16 amostras (%s)",
              pcBefore, pc, moved,
              moved ? "microcodigo executando" : "PC congelado, sem microcodigo");

    return moved ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
}

/* ------------------------------------------------------------------------ */
/* Pacotes de DMA de verdade — Fase 3                                        */
/*                                                                           */
/* Enderecos passados aos pacotes sao de GPU, nao offsets: a SDMA so resolve  */
/* o que cai na abertura de sistema (BC250_VRAM_GPU_BASE + offset).           */
/* ------------------------------------------------------------------------ */

#define SDMA_PKT_HEADER(op, subop) (((op) & 0xFFu) | (((subop) & 0xFFu) << 8))

/* Graphics Cache Rinse — SDMA_OP_GCR_REQ.
 *
 * Escrita da SDMA que fica suja na L2 do GC nao aparece para a CPU pela
 * abertura do BAR0: a CPU chega na VRAM pelo HDP, que nao ve a L2. O amdgpu
 * resolve isso com este pacote (sdma_v5_0_ring_emit_mem_sync).
 *
 * gcr_cntl e o mesmo do amdgpu: GL2_WB | GL2_INV | GL1_INV | GLV_INV |
 * GLK_INV | GLM_INV | GLI_INV(1) = 0xC3A1. Base e limite de VA em zero, como
 * la — o comentario do kernel diz "flush entire cache L0/L1/L2".
 *
 * Campos de navi10_sdma_pkt_open.h (baixado para a raiz do projeto):
 *   payload2: gcr_control_15_0 << 16
 *   payload3: gcr_control_18_16 << 0
 *   payload4: vmid << 24
 */
#define BC250_SDMA_GCR_CNTL   0x0000C3A1u

static ULONG BcSdmaEmitGcr(ULONG *Pkt)
{
    ULONG n = 0;

    Pkt[n++] = SDMA_PKT_HEADER(BC250_SDMA_OP_GCR_REQ, 0);
    Pkt[n++] = 0;                                              /* base_va_31_7  */
    Pkt[n++] = (BC250_SDMA_GCR_CNTL & 0xFFFFu) << 16;          /* gcr_control_15_0 */
    Pkt[n++] = (BC250_SDMA_GCR_CNTL >> 16) & 0x7u;             /* gcr_control_18_16 */
    Pkt[n++] = 0;                                              /* limit_va / vmid */

    return n;
}

NTSTATUS BcSdmaExecOp(BC250DBG_SDMA_OP_IO *Io)
{
    ULONG pkt[4 + BC250DBG_SDMA_WRITE_MAX_DW + 8];
    ULONG n = 0, i;
    ULONGLONG dstGpu, srcGpu;
    ULONG rptrBefore = 0, wptr = 0, rptrAfter = 0;
    NTSTATUS status;

    if (Io == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        return STATUS_ACCESS_DENIED;
    }
    if (g_Dev.SdmaRingGpuAddr == 0) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                  "SDMA: anel nao inicializado; rode 'sdma init' antes");
        return STATUS_DEVICE_NOT_READY;
    }

    /* Alinhamento de dword e limites da janela de VRAM. Nao validamos contra
     * BAR0 (256 MB) e sim contra a VRAM inteira (512 MB): a SDMA alcanca tudo
     * que esta na abertura, mesmo o que nao conseguimos ler pelo BAR0. */
    if ((Io->DstOffset & 3u) != 0 || (Io->SrcOffset & 3u) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Io->AddrMode == BC250DBG_ADDR_RAW) {
        dstGpu = (ULONGLONG)Io->DstOffset;
        srcGpu = (ULONGLONG)Io->SrcOffset;
    } else {
        dstGpu = BC250_VRAM_GPU_BASE + (ULONGLONG)Io->DstOffset;
        srcGpu = BC250_VRAM_GPU_BASE + (ULONGLONG)Io->SrcOffset;
    }

    switch (Io->Op) {
    case BC250DBG_SDMA_OP_FILL:
        if (Io->ByteCount == 0 || Io->ByteCount > BC250_SDMA_MAX_XFER_BYTES ||
            (Io->ByteCount & 3u) != 0 ||
            (ULONGLONG)Io->DstOffset + Io->ByteCount > BC250_VRAM_TOTAL_BYTES) {
            return STATUS_INVALID_PARAMETER;
        }
        /* FILLSIZE nos bits 30-31: 0=byte, 1=half, 2=dword. O amdgpu emite 0,
         * mas com 0 a SDMA replica apenas o byte baixo do padrao. Deixamos
         * configuravel para medir qual valor esta ASIC realmente aceita. */
        pkt[n++] = SDMA_PKT_HEADER(BC250_SDMA_OP_CONST_FILL, 0)
                 | ((Io->FillSize & 3u) << 30);
        pkt[n++] = (ULONG)(dstGpu & 0xFFFFFFFFull);
        pkt[n++] = (ULONG)(dstGpu >> 32);
        pkt[n++] = Io->Value;
        pkt[n++] = Io->ByteCount - 1;
        break;

    case BC250DBG_SDMA_OP_COPY:
        if (Io->ByteCount == 0 || Io->ByteCount > BC250_SDMA_MAX_XFER_BYTES ||
            (Io->ByteCount & 3u) != 0 ||
            (ULONGLONG)Io->DstOffset + Io->ByteCount > BC250_VRAM_TOTAL_BYTES ||
            (ULONGLONG)Io->SrcOffset + Io->ByteCount > BC250_VRAM_TOTAL_BYTES) {
            return STATUS_INVALID_PARAMETER;
        }
        pkt[n++] = SDMA_PKT_HEADER(BC250_SDMA_OP_COPY, BC250_SDMA_SUBOP_LINEAR);
        pkt[n++] = Io->ByteCount - 1;
        pkt[n++] = 0;                                   /* endian swap */
        pkt[n++] = (ULONG)(srcGpu & 0xFFFFFFFFull);
        pkt[n++] = (ULONG)(srcGpu >> 32);
        pkt[n++] = (ULONG)(dstGpu & 0xFFFFFFFFull);
        pkt[n++] = (ULONG)(dstGpu >> 32);
        break;

    case BC250DBG_SDMA_OP_WRITE:
        if (Io->DwordCount == 0 || Io->DwordCount > BC250DBG_SDMA_WRITE_MAX_DW ||
            (ULONGLONG)Io->DstOffset + Io->DwordCount * 4 > BC250_VRAM_TOTAL_BYTES) {
            return STATUS_INVALID_PARAMETER;
        }
        pkt[n++] = SDMA_PKT_HEADER(BC250_SDMA_OP_WRITE, BC250_SDMA_SUBOP_LINEAR);
        pkt[n++] = (ULONG)(dstGpu & 0xFFFFFFFFull);
        pkt[n++] = (ULONG)(dstGpu >> 32);
        pkt[n++] = Io->DwordCount - 1;
        for (i = 0; i < Io->DwordCount; i++) {
            pkt[n++] = Io->Data[i];
        }
        break;

    default:
        return STATUS_INVALID_PARAMETER;
    }

    /* Rinse de cache logo depois da operacao, no mesmo submit: sem isso a
     * escrita fica suja na L2 do GC e a CPU nao a enxerga pela abertura do
     * BAR0. Pode ser desligado com BC250DBG_SDMA_FLAG_NO_GCR para medir a
     * diferenca. */
    if ((Io->Flags & BC250DBG_SDMA_FLAG_NO_GCR) == 0) {
        n += BcSdmaEmitGcr(&pkt[n]);
    }

    status = BcSdmaSubmit(pkt, n, &rptrBefore, &wptr, &rptrAfter);

    Io->RptrBefore = rptrBefore;
    Io->Wptr       = wptr;
    Io->RptrAfter  = rptrAfter;
    Io->Completed  = (rptrAfter == wptr) ? 1u : 0u;
    Io->StatusReg  = BcRegReadRaw(A_SDMA0_STATUS_REG);

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_SDMA,
              "SDMA op %u: dst 0x%llx, %u dw de pacote, RPTR 0x%X -> 0x%X (%s)",
              Io->Op, dstGpu, n, rptrBefore, rptrAfter,
              Io->Completed ? "concluido" : "TIMEOUT");

    return status;
}

NTSTATUS BcSdmaLoadFirmware(const ULONG *Dwords, ULONG Count, ULONG *OutVersion, ULONG *OutChecksum)
{
    ULONG i, f32_cntl, probe;

    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        return STATUS_ACCESS_DENIED;
    }
    if (Dwords == NULL || Count == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Para o microcontrolador F32 antes de mexer na instruction RAM. */
    f32_cntl = BcRegReadRaw(A_SDMA0_F32_CNTL);
    BcRegWriteRaw(A_SDMA0_F32_CNTL, f32_cntl | F_SDMA0_F32_CNTL__HALT);

    /* Limpa o checksum acumulado para que a leitura no fim signifique algo. */
    BcRegWriteRaw(A_SDMA0_F32_CNTL,
                  BcRegReadRaw(A_SDMA0_F32_CNTL) | F_SDMA0_F32_CNTL__CHECKSUM_CLR);
    BcRegWriteRaw(A_SDMA0_F32_CNTL,
                  BcRegReadRaw(A_SDMA0_F32_CNTL) & ~F_SDMA0_F32_CNTL__CHECKSUM_CLR);

    /* A porta de instruction RAM existe? Medido na BC-250 em 2026-08-19:
     * UCODE_ADDR/UCODE_DATA leem 0xFFFFFFFF e recusam escrita. Isso bate com
     * o comentario do proprio kernel em sdma_v5_0.c:
     *
     *     // emulation only, won't work on real chip
     *     // navi10 real chip need to use PSP to load firmware
     *
     * Em silicio real quem carrega o microcodigo da SDMA e o PSP, e a porta
     * direta fica desabilitada. Detectamos isso e falhamos de forma explicita
     * em vez de fingir sucesso depois de jogar 33 KB no vazio. */
    BcRegWriteRaw(A_SDMA0_UCODE_ADDR, 0);
    probe = BcRegReadRaw(A_SDMA0_UCODE_ADDR);
    if (probe == 0xFFFFFFFFu) {
        BcRegWriteRaw(A_SDMA0_F32_CNTL, f32_cntl & ~F_SDMA0_F32_CNTL__HALT);

        if (OutVersion)  *OutVersion  = probe;
        if (OutChecksum) *OutChecksum = 0;

        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                  "SDMA0 UCODE_ADDR (0x%05X) le 0xFFFFFFFF e nao aceita escrita: "
                  "a porta de instruction RAM nao esta acessivel. Em silicio real "
                  "o microcodigo da SDMA e carregado pelo PSP (ver sdma_v5_0.c).",
                  A_SDMA0_UCODE_ADDR);
        return STATUS_DEVICE_FEATURE_NOT_SUPPORTED;
    }

    /* Escoa os dwords para a instruction RAM. */
    for (i = 0; i < Count; i++) {
        BcRegWriteRaw(A_SDMA0_UCODE_DATA, Dwords[i]);
    }

    if (OutChecksum) *OutChecksum = BcRegReadRaw(A_SDMA0_UCODE_CHECKSUM);
    if (OutVersion)  *OutVersion  = BcRegReadRaw(A_SDMA0_UCODE_ADDR);

    /* Solta o F32. */
    f32_cntl = BcRegReadRaw(A_SDMA0_F32_CNTL);
    BcRegWriteRaw(A_SDMA0_F32_CNTL, f32_cntl & ~F_SDMA0_F32_CNTL__HALT);

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_SDMA,
              "SDMA0 Firmware carregado: %u dwords (%u B), checksum=0x%08X",
              Count, Count * 4, OutChecksum ? *OutChecksum : 0);

    return STATUS_SUCCESS;
}







/* ------------------------------------------------------------------------ */
/* Varredura da VRAM e invalidacao do HDP                                    */
/*                                                                           */
/* A varredura responde a pergunta que nenhuma leitura pontual responde: se a */
/* escrita da SDMA nao apareceu onde mandamos, ela apareceu em algum lugar?   */
/* ------------------------------------------------------------------------ */

VOID BcHdpReadCacheInvalidate(VOID)
{
    ULONG miscCntl;

    if (g_Dev.Mmio == NULL) {
        return;
    }

    /* Flush writes from Host to VRAM (HDP_MEM_COHERENCY_FLUSH_CNTL)
     * No amdgpu (hdp_v5_0_flush_hdp), eles escrevem 0 neste registrador.
     * Na BC-250 (Navi10), o offset e 0x4A80 (dword 0x12A0). */
    BcRegWriteRaw(0x4A80, 0);
    (VOID)BcRegReadRaw(0x4A80);

    /* HDP_READ_CACHE_INVALIDATE bit 0. O HDP e o caminho da CPU ate a VRAM;
     * se ele guardou leituras antigas, a escrita da GPU pode ja estar na
     * memoria sem que a CPU enxergue. */
    BcRegWriteRaw(BC250_HDP_READ_CACHE_INVALIDATE, 1);
    (VOID)BcRegReadRaw(BC250_HDP_READ_CACHE_INVALIDATE);

    /* Toggle FLUSH_INVALIDATE_CACHE (bit 0) em HDP_MISC_CNTL (0x03FCC) */
    miscCntl = BcRegReadRaw(0x03FCC);
    BcRegWriteRaw(0x03FCC, miscCntl | 1);
    (VOID)BcRegReadRaw(0x03FCC);
    BcRegWriteRaw(0x03FCC, miscCntl & ~1u);
    (VOID)BcRegReadRaw(0x03FCC);
}

NTSTATUS BcVramScan(BC250DBG_VRAM_SCAN_IO *Io)
{
    ULONG off, end, v, hits = 0;
    NTSTATUS status;

    if (Io == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = BcVramMap();
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Teto por chamada. Uma varredura de 256 MB por MMIO segura o IOCTL por
     * minutos e o processo nem pode ser morto enquanto isso — o chamador
     * fatia em pedacos e continua. */
    if (Io->Length == 0 || (ULONGLONG)Io->Start + Io->Length > g_Dev.VramLength) {
        Io->Start  = 0;
        Io->Length = g_Dev.VramLength;
    }
    if (Io->Length > BC250DBG_VRAM_SCAN_MAX_BYTES) {
        Io->Length = BC250DBG_VRAM_SCAN_MAX_BYTES;
    }

    BcHdpReadCacheInvalidate();

    end = Io->Start + Io->Length;
    for (off = Io->Start & ~3u; off + 4 <= end; off += 4) {
        v = READ_REGISTER_ULONG((volatile ULONG *)(g_Dev.Vram + off));
        if (Io->Mask ? ((v & Io->Mask) == (Io->Value & Io->Mask)) : (v == Io->Value)) {
            if (hits < BC250DBG_VRAM_SCAN_MAX_HITS) {
                Io->Hits[hits] = off;
            }
            hits++;
        }
    }

    Io->HitCount = hits;
    /* Start/Length ficam com o que foi realmente coberto, para o chamador
     * saber onde continuar. */

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE,
              "varredura da VRAM 0x%X +%u por 0x%08X: %u ocorrencias",
              Io->Start, Io->Length, Io->Value, hits);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* Submissao crua — para descobrir se a engine decodifica ou so arrasta       */
/* ------------------------------------------------------------------------ */

NTSTATUS BcSdmaRaw(BC250DBG_SDMA_RAW_IO *Io)
{
    ULONG rptrBefore = 0, wptr = 0, rptrAfter = 0;
    NTSTATUS status;

    if (Io == NULL || Io->DwordCount == 0 ||
        Io->DwordCount > BC250DBG_SDMA_RAW_MAX_DW) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        return STATUS_ACCESS_DENIED;
    }
    if (g_Dev.SdmaRingGpuAddr == 0) {
        return STATUS_DEVICE_NOT_READY;
    }

    status = BcSdmaSubmit((const ULONG *)Io->Data, Io->DwordCount, &rptrBefore, &wptr, &rptrAfter);

    Io->RptrBefore = rptrBefore;
    Io->Wptr       = wptr;
    Io->RptrAfter  = rptrAfter;
    Io->Completed  = (rptrAfter == wptr) ? 1u : 0u;
    Io->StatusReg  = BcRegReadRaw(A_SDMA0_STATUS_REG);
    Io->F32Pc      = BcSdmaF32Pc();

    return status;
}

/* ------------------------------------------------------------------------ */
/* GCVM / GART / VMID 0                                                      */
/* ------------------------------------------------------------------------ */

VOID BcGcvmStatus(BC250DBG_GCVM_STATUS_IO *Out)
{
    RtlZeroMemory(Out, sizeof(*Out));
    Out->Size = sizeof(*Out);
    if (g_Dev.Mmio == NULL) {
        return;
    }

    Out->L2Cntl            = BcRegReadRaw(A_GCVM_L2_CNTL);
    Out->L2Cntl2           = BcRegReadRaw(A_GCVM_L2_CNTL2);
    Out->Context0Cntl      = BcRegReadRaw(A_GCVM_CONTEXT0_CNTL);
    Out->Context0PtBaseLo  = BcRegReadRaw(A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO);
    Out->Context0PtBaseHi  = BcRegReadRaw(A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI);
    Out->Context0StartLo   = BcRegReadRaw(A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO);
    Out->Context0EndLo     = BcRegReadRaw(A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO);
    Out->SysApertureLow    = BcRegReadRaw(A_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR);
    Out->SysApertureHigh   = BcRegReadRaw(A_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR);
    Out->SysApertureDefLo  = BcRegReadRaw(A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB);
    Out->SysApertureDefHi  = BcRegReadRaw(A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB);
    Out->L2ProtFaultStatus = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_STATUS);
}

NTSTATUS BcGcvmInit(BC250DBG_GCVM_INIT_IO *Io)
{
    ULONG cntlBefore, cntlAfter, l2Cntl, temp;

    if (Io == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        return STATUS_ACCESS_DENIED;
    }

    cntlBefore = BcRegReadRaw(A_GCVM_CONTEXT0_CNTL);
    Io->Context0CntlBefore = cntlBefore;

    /* 1. Configurar L2 Cache da MMU (espelhando gfxhub_v2_0_init_cache_regs) */
    l2Cntl = BcRegReadRaw(A_GCVM_L2_CNTL);
    l2Cntl |= 0x00000001u; /* ENABLE_L2_CACHE = 1 */
    l2Cntl |= 0x00000800u; /* ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY = 1 */
    l2Cntl |= 0x00080000u; /* CONTEXT1_IDENTITY_ACCESS_MODE = 1 */
    BcRegWriteRaw(A_GCVM_L2_CNTL, l2Cntl);

    /* Invalidar TLBs L1 e Cache L2 */
    BcRegWriteRaw(A_GCVM_L2_CNTL2, 0x00000003u); /* INVALIDATE_ALL_L1_TLBS | INVALIDATE_L2_CACHE */

    /* 2. Abertura de sistema derivada do FB_LOCATION que o proprio hardware
     *    declara, e nao de um valor fixo. O gmc_v10_0_init_system_aperture faz
     *    exatamente isso: low = min(fb_start, agp_start) >> 18,
     *    high = max(fb_end, agp_end) >> 18.
     *
     *    Fixar a abertura em [0, 16 GB) enquanto o FB_LOCATION continuava em
     *    0xF400 deixou o frame buffer FORA da abertura e criou um XNACK de
     *    leitura na busca do anel. Alem disso, uma abertura cobrindo enderecos
     *    baixos se sobrepoe a RAM do sistema: escrita da GPU ali pode atingir
     *    memoria do host. */
    {
        ULONG fbBase = BcRegReadRaw(A_GCMC_VM_FB_LOCATION_BASE);
        ULONG fbTop  = BcRegReadRaw(A_GCMC_VM_FB_LOCATION_TOP);
        ULONG fbOff  = BcRegReadRaw(A_GCMC_VM_FB_OFFSET);
        ULONGLONG fbStart = (ULONGLONG)fbBase << 24;
        ULONGLONG fbEnd   = ((ULONGLONG)fbTop + 1) << 24;
        ULONGLONG fbOffset = (ULONGLONG)fbOff << 24;
        ULONGLONG defaultPa = (fbEnd - 0x100000ull) - fbStart + fbOffset;

        BcRegWriteRaw(A_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR,  (ULONG)(fbStart >> 18));
        BcRegWriteRaw(A_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR, (ULONG)(fbEnd   >> 18));

        /* Pagina default como endereco FISICO (PA), como amdgpu_gmc_vram_mc2pa */
        BcRegWriteRaw(A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB, (ULONG)(defaultPa >> 12));
        BcRegWriteRaw(A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB, (ULONG)(defaultPa >> 44));

        /* L1 TLB: SYSTEM_ACCESS_MODE=3 (abertura de sistema), MTYPE=UC (3), ENABLE_ADVANCED_DRIVER_MODEL=1 */
        BcRegWriteRaw(A_GCMC_VM_MX_L1_TLB_CNTL, 0x00001859u);

        /* 3. Desabilitar Abertura de Identidade do Contexto 1 (gfxhub_v2_0_disable_identity_aperture) */
        BcRegWriteRaw(A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO, 0xFFFFFFFFu);
        BcRegWriteRaw(A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI, 0x0000000Fu);
        BcRegWriteRaw(A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO, 0);
        BcRegWriteRaw(A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI, 0);
        BcRegWriteRaw(A_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO, 0);

        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE,
                  "GCVM: abertura de sistema [0x%llx, 0x%llx), default PA 0x%llx, L1_TLB_CNTL 0x1859",
                  fbStart, fbEnd, defaultPa);
    }

    /* 4. Contexto 0 do GCVM: DESLIGADO.
     * A abertura de sistema resolve os operandos de pacote. */
    BcRegWriteRaw(A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO, 0);
    BcRegWriteRaw(A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI, 0);
    BcRegWriteRaw(A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO, 0);
    BcRegWriteRaw(A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI, 0);
    BcRegWriteRaw(A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO, 0);
    BcRegWriteRaw(A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI, 0);
    BcRegWriteRaw(A_GCVM_CONTEXT0_CNTL, 0x007FFE80u);

    cntlAfter = BcRegReadRaw(A_GCVM_CONTEXT0_CNTL);
    Io->Context0CntlAfter = cntlAfter;
    Io->L2CntlAfter = BcRegReadRaw(A_GCVM_L2_CNTL);
    /* Sucesso e o contexto DESLIGADO: sem page table, ligar ENABLE_CONTEXT faz
     * operandos de endereco vindos de pacote falharem a traducao em silencio. */
    Io->Status = (cntlAfter & 1) ? BC250DBG_OPS_MISMATCH : BC250DBG_OPS_OK;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE,
              "GCVM Init: CONTEXT0_CNTL 0x%08X -> 0x%08X (contexto desligado de "
              "proposito), L2_CNTL 0x%08X",
              cntlBefore, cntlAfter, Io->L2CntlAfter);

    return STATUS_SUCCESS;
}

/* Invalida TLB e caches de traducao da GFXHUB por MMIO (Engine 17) */
VOID BcGcvmTlbFlush(ULONG Vmid)
{
    ULONG i;
    ULONG invReq = (1u << (Vmid & 0xFu)) | 0x02F80000u; /* Flush all L1/L2 PTEs/PDEs */

    /* 1. Aguarda ou toma posse do semaforo de hardware da Engine 17 */
    for (i = 0; i < 1000; i++) {
        if (BcRegReadRaw(A_GCVM_INVALIDATE_ENG17_SEM) == 0) break;
        KeStallExecutionProcessor(1);
    }

    /* 2. Dispara requisicao de invalidação */
    BcRegWriteRaw(A_GCVM_INVALIDATE_ENG17_REQ, invReq);

    /* 3. Aguarda confirmacao (ACK) do hardware */
    for (i = 0; i < 10000; i++) {
        if (BcRegReadRaw(A_GCVM_INVALIDATE_ENG17_ACK) & (1u << (Vmid & 0xFu))) break;
        KeStallExecutionProcessor(1);
    }

    /* 4. Libera o semaforo */
    BcRegWriteRaw(A_GCVM_INVALIDATE_ENG17_SEM, 0);
}

/* ------------------------------------------------------------------------ */
/* Preenchimento e verificacao de padrao na VRAM, no kernel                   */
/* ------------------------------------------------------------------------ */

static ULONG BcPatExpected(ULONG Pattern, ULONG Value, ULONG Offset)
{
    if (Pattern == BC250DBG_PAT_ADDR) {
        /* O conteudo diz de onde veio: deslocamento fica visivel. */
        return Value + (Offset >> 2);
    }
    return Value;
}

static NTSTATUS BcVramPatternRange(BC250DBG_VRAM_PATTERN_IO *Io, BOOLEAN Verify)
{
    ULONG off, end, v, want, bad = 0;
    NTSTATUS status;

    if (Io == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = BcVramMap();
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (!Verify && g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        return STATUS_ACCESS_DENIED;
    }

    if (Io->Length == 0 || (ULONGLONG)Io->Start + Io->Length > g_Dev.VramLength) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Io->Length > BC250DBG_VRAM_OP_MAX_BYTES) {
        Io->Length = BC250DBG_VRAM_OP_MAX_BYTES;
    }

    if (Verify) {
        BcHdpReadCacheInvalidate();
    }

    end = Io->Start + Io->Length;
    for (off = Io->Start & ~3u; off + 4 <= end; off += 4) {
        want = BcPatExpected(Io->Pattern, Io->Value, off);
        if (Verify) {
            v = READ_REGISTER_ULONG((volatile ULONG *)(g_Dev.Vram + off));
            if (v != want) {
                if (bad < BC250DBG_VRAM_BAD_SAMPLES) {
                    Io->BadOffset[bad] = off;
                    Io->BadGot[bad]    = v;
                    Io->BadWant[bad]   = want;
                }
                bad++;
            }
        } else {
            WRITE_REGISTER_ULONG((volatile ULONG *)(g_Dev.Vram + off), want);
        }
    }

    Io->BadCount = bad;

    /* Cerca de ordenacao depois de uma rajada de escritas da CPU.
     *
     * Sem isso as escritas ficam em voo no caminho do HDP: a GPU escreve
     * depois, completa, e as escritas da CPU pousam POR CIMA. O sintoma e
     * exatamente o que a bateria pegou — 'sdma fill' numa regiao virgem
     * funciona, mas na mesma regiao recem-preenchida pela CPU a escrita da
     * SDMA desaparece.
     *
     * Uma leitura da mesma abertura forca as escritas anteriores a pousarem.
     * Uma leitura no fim da rajada basta; ler a cada dword custaria o dobro. */
    if (!Verify && Io->Length >= 4) {
        (VOID)READ_REGISTER_ULONG(
            (volatile ULONG *)(g_Dev.Vram + Io->Start + Io->Length - 4));
        (VOID)READ_REGISTER_ULONG((volatile ULONG *)(g_Dev.Vram + Io->Start));
    }

    return STATUS_SUCCESS;
}

NTSTATUS BcVramFillPattern(BC250DBG_VRAM_PATTERN_IO *Io)
{
    return BcVramPatternRange(Io, FALSE);
}

NTSTATUS BcVramVerifyPattern(BC250DBG_VRAM_PATTERN_IO *Io)
{
    return BcVramPatternRange(Io, TRUE);
}

/* ------------------------------------------------------------------------ */
/* GFX Ring Buffer (CP PM4 Ring 0)                                          */
/* ------------------------------------------------------------------------ */

#define A_CP_ME_CNTL          0x086D8u
#define A_CP_RB0_RPTR         0x08700u
#define A_CP_RB0_BASE         0x0C100u
#define A_CP_RB0_CNTL         0x0C104u
#define A_CP_RB0_RPTR_ADDR    0x0C10Cu
#define A_CP_RB0_RPTR_ADDR_HI 0x0C110u
#define A_CP_RB0_WPTR         0x0C150u
#define A_CP_RB0_WPTR_HI      0x0C154u
#define A_CP_RB0_BASE_HI      0x0C2C4u
#define A_CP_RB_WPTR_POLL_ADDR_LO 0x0C3ACu
#define A_CP_RB_WPTR_POLL_ADDR_HI 0x0C3B0u
#define A_CP_RB_DOORBELL_CONTROL 0x0C3B4u

NTSTATUS BcGfxInit(BC250DBG_GFX_INIT_IO *Io)
{
    ULONG size, vramOffset, i;
    ULONGLONG gpuAddr, rptrWbAddr, wptrPollAddr;
    ULONG meCntlBefore, meCntlAfter, rbCntl;
    NTSTATUS st;

    if (!Io) return STATUS_INVALID_PARAMETER;
    if (g_Dev.Mmio == NULL) return STATUS_DEVICE_NOT_READY;
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) return STATUS_ACCESS_DENIED;

    size = (Io->RingSizeBytes > 0) ? Io->RingSizeBytes : 4096;

    if (Io->VramOffset == 0) {
        PHYSICAL_ADDRESS lowAddr, highAddr, boundary;
        lowAddr.QuadPart = 0;
        highAddr.QuadPart = 0xFFFFFFFFFFull; /* 40-bit DMA address */
        boundary.QuadPart = 0;
        if (g_Dev.GfxRingBuffer != NULL) {
            MmFreeContiguousMemory(g_Dev.GfxRingBuffer);
            g_Dev.GfxRingBuffer = NULL;
        }
        g_Dev.GfxRingBuffer = MmAllocateContiguousMemorySpecifyCache(
            size + 0x1000, lowAddr, highAddr, boundary, MmNonCached);
        if (g_Dev.GfxRingBuffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(g_Dev.GfxRingBuffer, size + 0x1000);
        g_Dev.GfxRingPa = MmGetPhysicalAddress(g_Dev.GfxRingBuffer);
        gpuAddr = (ULONGLONG)g_Dev.GfxRingPa.QuadPart;
        vramOffset = 0;
    } else {
        if (g_Dev.GfxRingBuffer != NULL) {
            MmFreeContiguousMemory(g_Dev.GfxRingBuffer);
            g_Dev.GfxRingBuffer = NULL;
        }
        vramOffset = Io->VramOffset & ~0xFFu;
        st = BcVramMap();
        if (!NT_SUCCESS(st)) return st;

        if ((ULONGLONG)vramOffset + size + 0x1000 > (ULONGLONG)g_Dev.VramLength) {
            return STATUS_INVALID_PARAMETER;
        }

        /* Zera o anel e o writeback de RPTR/WPTR na VRAM */
        for (i = 0; i < size + 0x1000; i += 4) {
            BcVramWriteRaw(vramOffset + i, 0);
        }

        gpuAddr = BC250_VRAM_GPU_BASE + (ULONGLONG)vramOffset;
    }

    rptrWbAddr = gpuAddr + (ULONGLONG)size; /* logo apos o anel */
    wptrPollAddr = gpuAddr + (ULONGLONG)size + 0x100ull;

    meCntlBefore = BcRegReadRaw(A_CP_ME_CNTL);

    /* 1. Congela os consumidores antes de alterar qualquer estado do anel.
     * Sem isto o PFP pode observar BASE/CNTL/ponteiros parcialmente
     * programados e deixar uma leitura RCIU antiga pendente. */
    BcRegWriteRaw(A_CP_ME_CNTL, 0x15000150); /* ME/PFP/CE HALT + I-cache invalidate */
    KeStallExecutionProcessor(50);

    /* 2. Base e base_hi do anel (256-byte aligned base) */
    BcRegWriteRaw(A_CP_RB0_BASE, (ULONG)((gpuAddr >> 8) & 0xFFFFFFFFull));
    BcRegWriteRaw(A_CP_RB0_BASE_HI, (ULONG)((gpuAddr >> 40) & 0xFFull));

    /* 3. Writeback de RPTR */
    BcRegWriteRaw(A_CP_RB0_RPTR_ADDR, (ULONG)(rptrWbAddr & 0xFFFFFFFCull));
    BcRegWriteRaw(A_CP_RB0_RPTR_ADDR_HI, (ULONG)(rptrWbAddr >> 32));

    /* 4. Polling de WPTR e desativacao de doorbell */
    BcRegWriteRaw(A_CP_RB_DOORBELL_CONTROL, 0x00000000);
    BcRegWriteRaw(A_CP_RB_WPTR_POLL_ADDR_LO, (ULONG)(wptrPollAddr & 0xFFFFFFFCull));
    BcRegWriteRaw(A_CP_RB_WPTR_POLL_ADDR_HI, (ULONG)(wptrPollAddr >> 32));
    BcRegWriteRaw(0x08708, 0x00400100); /* CP_RB_WPTR_POLL_CNTL */

    /* 5. Habilita escrita em RPTR para zerar os ponteiros com RB_NO_UPDATE=1 */
    rbCntl = (9u & 0x3Fu) | ((7u & 0x3Fu) << 8) | (1u << 28) | (1u << 30);
    BcRegWriteRaw(A_CP_RB0_CNTL, rbCntl | 0x80000000u | (1u << 27)); /* RB_RPTR_WR_ENA = 1, RB_NO_UPDATE = 1 */
    BcRegWriteRaw(A_CP_RB0_RPTR, 0);
    BcRegWriteRaw(A_CP_RB0_WPTR, 0);
    BcRegWriteRaw(A_CP_RB0_WPTR_HI, 0);

    /* 6. Passa o controle do RPTR de volta para o hardware (RB_RPTR_WR_ENA = 0, RB_NO_UPDATE = 0, RB_EXE = 1) */
    BcRegWriteRaw(A_CP_RB0_CNTL, rbCntl);

    /* 7. Invalida Cache L2 e TLBs da GFXHUB e define Golden Registers */
    BcRegWriteRaw(A_GCVM_L2_CNTL2, 0x00000003u);
    BcHdpReadCacheInvalidate();
    BcGcvmTlbFlush(0);

    /* Golden Registers: VGT_NUM_INSTANCES = 1 e PA_SC_LINE_STIPPLE_STATE = 0 */
    BcRegWriteRaw(0x30934, 1);
    BcRegWriteRaw(0x30A04, 0);

    /* 8. Toda a configuracao agora esta visivel; libera os consumidores (Unhalt CE, PFP, ME) */
    BcRegWriteRaw(A_CP_ME_CNTL, 0x00000000);
    KeStallExecutionProcessor(50);

    /* 9. Aplica rbCntl e polling de WPTR DEPOIS de desengatilhar o unhalt */
    BcRegWriteRaw(A_CP_RB0_CNTL, rbCntl);
    BcRegWriteRaw(0x08708, 0x00400100); /* CP_RB_WPTR_POLL_CNTL */

    meCntlAfter = BcRegReadRaw(A_CP_ME_CNTL);

    g_Dev.GfxRingVramOffset  = vramOffset;
    g_Dev.GfxRingGpuAddr     = gpuAddr;
    g_Dev.GfxRingSize        = size;
    g_Dev.GfxWptr            = 0;
    g_Dev.GfxRingInitialized = TRUE;

    Io->Size           = sizeof(*Io);
    Io->RingSizeBytes  = size;
    Io->VramOffset     = vramOffset;
    Io->GpuAddr        = gpuAddr;
    Io->RbCntl         = BcRegReadRaw(A_CP_RB0_CNTL);
    Io->MeCntlBefore   = meCntlBefore;
    Io->MeCntlAfter    = meCntlAfter;
    Io->Status         = BC250DBG_OPS_OK;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE,
              "GFX: CP_RB0 init em %s 0x%llX, CNTL 0x%08X, ME_CNTL 0x%08X -> 0x%08X",
              (vramOffset == 0) ? "System RAM PA" : "VRAM GPU",
              gpuAddr, rbCntl, meCntlBefore, meCntlAfter);

    return STATUS_SUCCESS;
}

NTSTATUS BcGfxSubmit(BC250DBG_GFX_SUBMIT_IO *Io)
{
    ULONG count, ringDwords, wptr, rptrBefore, rptrAfter, wptrFinal;
    ULONG rptrWb = 0;
    ULONG timeout = 200000;
    ULONG i;

    if (!Io) return STATUS_INVALID_PARAMETER;
    if (g_Dev.Mmio == NULL || !g_Dev.GfxRingInitialized) return STATUS_DEVICE_NOT_READY;
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) return STATUS_ACCESS_DENIED;

    count = Io->DwordCount;
    if (count == 0 || count > BC250DBG_GFX_SUBMIT_MAX_DW) return STATUS_INVALID_PARAMETER;

    ringDwords = g_Dev.GfxRingSize / 4;
    wptr = g_Dev.GfxWptr;
    rptrBefore = BcRegReadRaw(A_CP_RB0_RPTR);

    /* Grava os pacotes PM4 */
    if (g_Dev.GfxRingBuffer != NULL) {
        PULONG ringPtr = (PULONG)g_Dev.GfxRingBuffer;
        for (i = 0; i < count; i++) {
            ringPtr[(wptr + i) % ringDwords] = Io->Data[i];
        }
        wptrFinal = (wptr + count) % ringDwords;
        g_Dev.GfxWptr = wptrFinal;
        /* Atualiza WPTR na memoria de polling do host */
        *(PULONG)((PUCHAR)g_Dev.GfxRingBuffer + g_Dev.GfxRingSize + 0x100) = wptrFinal;
    } else {
        for (i = 0; i < count; i++) {
            ULONG off = g_Dev.GfxRingVramOffset + ((wptr + i) % ringDwords) * 4;
            BcVramWriteRaw(off, Io->Data[i]);
        }
        wptrFinal = (wptr + count) % ringDwords;
        g_Dev.GfxWptr = wptrFinal;
        /* Atualiza WPTR na memoria de polling da VRAM */
        BcVramWriteRaw(g_Dev.GfxRingVramOffset + g_Dev.GfxRingSize + 0x100, wptrFinal);
    }

    /* Flush de coerencia HDP, invalidacao L2 e TLB Flush da GFXHUB para garantir que o CP enxerga os pacotes */
    BcHdpReadCacheInvalidate();
    BcRegWriteRaw(A_GCVM_L2_CNTL2, 0x00000003u);
    BcGcvmTlbFlush(0);

    /* Habilita polling continuo de WPTR a cada 0x100 ciclos */
    BcRegWriteRaw(0x08708, 0x00400100);

    /* Atualiza WPTR no registrador do hardware (LO primeiro, HI depois para engatilhar no SOC15) */
    BcRegWriteRaw(A_CP_RB0_WPTR, wptrFinal);
    BcRegWriteRaw(A_CP_RB0_WPTR_HI, 0);

    /* Aguarda RPTR (ou o writeback em memoria) avancar */
    rptrAfter = rptrBefore;
    for (i = 0; i < timeout; i++) {
        rptrAfter = BcRegReadRaw(A_CP_RB0_RPTR);
        if (g_Dev.GfxRingBuffer != NULL) {
            rptrWb = *(PULONG)((PUCHAR)g_Dev.GfxRingBuffer + g_Dev.GfxRingSize);
        } else if (g_Dev.Vram != NULL) {
            rptrWb = BcVramReadRaw(g_Dev.GfxRingVramOffset + g_Dev.GfxRingSize);
        }
        if (rptrAfter == wptrFinal || rptrWb == wptrFinal) break;
        KeStallExecutionProcessor(5);
    }

    Io->Size        = sizeof(*Io);
    Io->RptrBefore  = rptrBefore;
    Io->RptrAfter   = rptrAfter;
    Io->Wptr        = wptrFinal;
    Io->Completed   = (rptrAfter == wptrFinal || rptrWb == wptrFinal) ? 1 : 0;
    Io->Status      = BC250DBG_OPS_OK;

    return STATUS_SUCCESS;
}

VOID BcGfxStatus(BC250DBG_GFX_STATUS_IO *Out)
{
    if (!Out) return;
    Out->Size       = sizeof(*Out);
    Out->MeCntl     = (g_Dev.Mmio != NULL) ? BcRegReadRaw(A_CP_ME_CNTL) : 0;
    Out->Rb0Cntl    = (g_Dev.Mmio != NULL) ? BcRegReadRaw(A_CP_RB0_CNTL) : 0;
    Out->Rb0Base    = (g_Dev.Mmio != NULL) ?
                      (((ULONGLONG)BcRegReadRaw(A_CP_RB0_BASE_HI) << 40) |
                       ((ULONGLONG)BcRegReadRaw(A_CP_RB0_BASE) << 8)) : 0;
    Out->Rb0Wptr    = (g_Dev.Mmio != NULL) ? BcRegReadRaw(A_CP_RB0_WPTR) : 0;
    Out->Rb0Rptr    = (g_Dev.Mmio != NULL) ? BcRegReadRaw(A_CP_RB0_RPTR) : 0;
    Out->Rb0RptrWb  = (g_Dev.GfxRingInitialized && g_Dev.Vram != NULL) ?
                      BcVramReadRaw(g_Dev.GfxRingVramOffset + g_Dev.GfxRingSize) : 0;
    Out->GrbmStatus = (g_Dev.Mmio != NULL) ? BcRegReadRaw(0x08010) : 0;
}

VOID BcGfxFree(VOID)
{
    g_Dev.GfxRingInitialized = FALSE;
    g_Dev.GfxRingVramOffset  = 0;
    g_Dev.GfxRingGpuAddr     = 0;
    g_Dev.GfxRingSize        = 0;
    g_Dev.GfxWptr            = 0;
}
