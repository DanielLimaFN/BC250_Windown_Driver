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
 * Os antigos aliases errados foram removidos do caminho executavel: o
 * catalogo os mantem em quarentena para regressao documental, sem MMIO. */
#define BC250_REG_SCRATCH0        0x30100u  /* mmSCRATCH_REG0 dw 0x2040 seg1 */
#define BC250_REG_GRBM_STATUS     0x08010u  /* mmGRBM_STATUS  dw 0x0da4 seg0 */
#define BC250_REG_CC_ARRAY        0x089BCu  /* mmCC_GC_SHADER_ARRAY_CONFIG dw 0x100f seg0 */
#define BC250_CC_ARRAY_LINUX      0xFFF80000u

/* Van Gogh/Cyan Skillfish SMUIO. O upstream define base byte 0x5A000,
 * mmSMUIO_GFX_MISC_CNTL=0x00C5 e PWR_GFXOFF_STATUS nos bits 2:1. */
#define BC250_REG_SMUIO_GFX_MISC_CNTL 0x5A314u
#define BC250_GFXOFF_STATUS_MASK      0x00000006u
#define BC250_GFXOFF_STATUS_SHIFT     1u

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
              "1002:13FE nao encontrado; MMIO permanecera desativado");
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

    if (!g_Dev.PciFound || g_Dev.Bar[BC250_BAR5_INDEX].Base == 0 ||
        (g_Dev.Bar[BC250_BAR5_INDEX].Flags & 0x1u) != 0) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_CORE,
                  "BAR5 recusado: recurso PCI da BC-250 nao foi confirmado");
        return STATUS_DEVICE_NOT_READY;
    }
    pa.QuadPart = (LONGLONG)g_Dev.Bar[BC250_BAR5_INDEX].Base;

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

    BcSdmaCaptureBootConfig();
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
/* abertura de sistema observada. A extensao real deve ser detectada; o       */
/* laboratorio so toca os 256 MB mapeados pelo BAR0 recebido do PCI.          */
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
    ULONG k;
    for (k = 0; k < BC250DBG_CP_ENGINE_COUNT; k++) {
        if (g_Dev.CpUcodeBackup[k] != NULL) {
            ExFreePoolWithTag(g_Dev.CpUcodeBackup[k], 'cUcB');
            g_Dev.CpUcodeBackup[k] = NULL;
        }
    }
    if (g_Dev.GcvmPtBackup != NULL) {
        ExFreePoolWithTag(g_Dev.GcvmPtBackup, 'tPcG');
        g_Dev.GcvmPtBackup = NULL;
    }
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

static BOOLEAN BcpSmuWaitNonZero(ULONG RspReg, ULONGLONG Deadline,
                                 ULONG *Value)
{
    ULONG value;

    for (;;) {
        value = BcSmnReadRaw(RspReg);
        if (value != 0) {
            *Value = value;
            return TRUE;
        }
        if (KeQueryInterruptTime() >= Deadline) {
            *Value = 0;
            return FALSE;
        }
        BcpSleepMs(1);
    }
}

NTSTATUS BcSmuMsg(ULONG Queue, ULONG Msg, ULONG Arg, ULONG ArgHi,
                  ULONG TimeoutMs, ULONG *SmuStatus, ULONG *Result)
{
    ULONG cmdReg, argReg, argHiReg, rspReg;
    ULONG status = 0, result = 0;
    ULONG timeout = TimeoutMs ? TimeoutMs : 1000;
    ULONG risk;
    LARGE_INTEGER mutexTimeout;
    ULONGLONG deadline;
    NTSTATUS waitStatus;

    *SmuStatus = 0;
    *Result = 0;

    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (Queue != 0 && Queue != 2 && Queue != 3) {
        return STATUS_INVALID_PARAMETER;
    }
    /* O chamador nao pode manter uma IRP presa indefinidamente. */
    if (timeout > 5000) {
        timeout = 5000;
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

    mutexTimeout.QuadPart = -((LONGLONG)timeout * 10000);
    waitStatus = KeWaitForSingleObject(&g_Dev.SmuMutex, Executive, KernelMode,
                                       FALSE, &mutexTimeout);
    if (waitStatus != STATUS_SUCCESS) {
        BcLogEvent(BC250DBG_LVL_ERROR, BC250DBG_SUB_SMU,
                   BC250DBG_EV_SMU_MSG, Queue, Msg, Arg, 0);
        return STATUS_IO_TIMEOUT;
    }

    /* 1) esperar o SMU concluir transacao anterior (RSP != 0) */
    deadline = KeQueryInterruptTime() + ((ULONGLONG)timeout * 10000ull);
    if (!BcpSmuWaitNonZero(rspReg, deadline, &status)) {
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
    deadline = KeQueryInterruptTime() + ((ULONGLONG)timeout * 10000ull);
    (VOID)BcpSmuWaitNonZero(rspReg, deadline, &status);

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

VOID BcHwCollectInfo(BC250DBG_INFO *Info)
{
    ULONG i;

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
        Info->ScratchReg0B   = 0u;
        Info->GrbmStatus     = BcRegReadRaw(BC250_REG_GRBM_STATUS);
        Info->GrbmStatusB    = 0u;
        Info->CcShaderArray  = BcRegReadRaw(BC250_REG_CC_ARRAY);
        Info->CcShaderArrayB = 0u;

        /* GET_INFO e estritamente observacional. Consultas SMU usam um
         * mailbox (logo, escrevem CMD/ARG/RSP) e ficam no IOCTL explicito. */
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
/* Diagnostico de falha. Todos derivados de sdma5_4_2_2_offset.h com a mesma
 * aritmetica ja confirmada por CNTL (0x001c), STATUS_REG (0x0025),
 * STATUS2_REG (0x0038), F32_CNTL (0x002a), UTCL1_CNTL (0x003c) e UTCL1_PAGE
 * (0x0048): byte = dw * 4 + 0x4980. Todos sao lidos, nenhum e escrito. */
#define A_SDMA0_POWER_CNTL               0x049E8  /* dw 0x001a seg0 */
#define A_SDMA0_CHICKEN_BITS_DIAG        0x049F4  /* dw 0x001d seg0 */
#define A_SDMA0_STATUS1_REG              0x04A18  /* dw 0x0026 seg0 */
#define A_SDMA0_FREEZE                   0x04A2C  /* dw 0x002b seg0 */
#define A_SDMA0_EDC_COUNTER              0x04A58  /* dw 0x0036 seg0 */
#define A_SDMA0_STATUS3_REG              0x04AB0  /* dw 0x004c seg0 */
#define A_SDMA0_ERROR_LOG                0x04AC0  /* dw 0x0050 seg0 */
#define A_SDMA0_UTCL1_WATERMK            0x04A74  /* dw 0x003d seg0 */
#define A_SDMA0_UTCL1_RD_STATUS          0x04A78  /* dw 0x003e seg0 */
#define A_SDMA0_UTCL1_WR_STATUS          0x04A7C  /* dw 0x003f seg0 */
#define A_SDMA0_UTCL1_INV0               0x04A80  /* dw 0x0040 seg0 */
#define A_SDMA0_UTCL1_INV1               0x04A84  /* dw 0x0041 seg0 */
#define A_SDMA0_UTCL1_INV2               0x04A88  /* dw 0x0042 seg0 */
#define A_SDMA0_UTCL1_RD_XNACK0          0x04A8C  /* dw 0x0043 seg0 */
#define A_SDMA0_UTCL1_RD_XNACK1          0x04A90  /* dw 0x0044 seg0 */
#define A_SDMA0_UTCL1_WR_XNACK0          0x04A94  /* dw 0x0045 seg0 */
#define A_SDMA0_UTCL1_WR_XNACK1          0x04A98  /* dw 0x0046 seg0 */
#define A_SDMA0_UTCL1_TIMEOUT            0x04A9C  /* dw 0x0047 seg0 */
#define A_GRBM_SOFT_RESET                0x08020  /* mmGRBM_SOFT_RESET dw 0x0da8 seg0 */

/* Bits vindos do header autoritativo do kernel, gc_10_1_0_sh_mask.h. Copia
 * verificavel em referencias/BC250-windowsDriverTest/docs/upstream/generated/
 * gc_10_1_0_sh_mask_extract.h, com o SHA-256 do arquivo completo no cabecalho.
 *
 * Nenhum destes bits foi inferido: sem a fonte autoritativa este caminho
 * ficou deliberadamente nao implementado, porque errar o bit de um registro de
 * soft reset da GPU tambem atinge CP, RLC e GFX. */
#define F_GRBM_SOFT_RESET__SOFT_RESET_SDMA0 0x00800000u  /* shift 0x17 */
#define F_SDMA0_FREEZE__FREEZE              0x00000010u  /* shift 0x04 */
#define F_SDMA0_FREEZE__FROZEN              0x00000020u  /* shift 0x05 */
#define F_SDMA0_CNTL__UTC_L1_ENABLE         0x00000002u  /* shift 0x01 */
/* STATUS1_REG: o upstream considera a engine ociosa quando os 10 bits baixos
 * estao todos em 1 (sdma_v5_0_stop_queue). */
#define M_SDMA0_STATUS1_REG__IDLE           0x000003FFu
/* STATUS2_REG.CMD_OP, o campo que latcha o ultimo opcode decodificado. */
#define F_SDMA0_STATUS2_REG__CMD_OP_SHIFT   4u
#define F_SDMA0_STATUS2_REG__CMD_OP_MASK    0x00000FF0u
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

/* Ordem estável do snapshot transacional. BcSdmaFree restaura RB_CNTL por
 * último para que nenhum consumidor veja uma configuração parcial. */
enum {
    SDMA_SNAP_SEM_TIMER = 0,
    SDMA_SNAP_CNTL,
    SDMA_SNAP_CHICKEN,
    SDMA_SNAP_GB_ADDR,
    SDMA_SNAP_GB_ADDR_READ,
    SDMA_SNAP_F32_CNTL,
    SDMA_SNAP_UTCL1_CNTL,
    SDMA_SNAP_UTCL1_PAGE,
    SDMA_SNAP_CLK_CTRL,
    SDMA_SNAP_RB_CNTL,
    SDMA_SNAP_RB_BASE,
    SDMA_SNAP_RB_BASE_HI,
    SDMA_SNAP_RB_RPTR,
    SDMA_SNAP_RB_RPTR_HI,
    SDMA_SNAP_RB_WPTR,
    SDMA_SNAP_RB_WPTR_HI,
    SDMA_SNAP_WPTR_POLL_CNTL,
    SDMA_SNAP_RPTR_ADDR_HI,
    SDMA_SNAP_RPTR_ADDR_LO,
    SDMA_SNAP_IB_CNTL,
    SDMA_SNAP_DOORBELL,
    SDMA_SNAP_MINOR_PTR,
    SDMA_SNAP_WPTR_POLL_ADDR_HI,
    SDMA_SNAP_WPTR_POLL_ADDR_LO
};

static const ULONG g_SdmaSnapshotRegs[BC250_SDMA_SNAPSHOT_REG_COUNT] = {
    A_SDMA0_SEM_WAIT_FAIL_TIMER_CNTL,
    A_SDMA0_CNTL,
    A_SDMA0_CHICKEN_BITS,
    A_SDMA0_GB_ADDR_CONFIG,
    A_SDMA0_GB_ADDR_CONFIG_READ,
    A_SDMA0_F32_CNTL,
    A_SDMA0_UTCL1_CNTL,
    A_SDMA0_UTCL1_PAGE,
    A_SDMA0_CLK_CTRL,
    A_SDMA0_GFX_RB_CNTL,
    A_SDMA0_GFX_RB_BASE,
    A_SDMA0_GFX_RB_BASE_HI,
    A_SDMA0_GFX_RB_RPTR,
    A_SDMA0_GFX_RB_RPTR_HI,
    A_SDMA0_GFX_RB_WPTR,
    A_SDMA0_GFX_RB_WPTR_HI,
    A_SDMA0_GFX_RB_WPTR_POLL_CNTL,
    A_SDMA0_GFX_RB_RPTR_ADDR_HI,
    A_SDMA0_GFX_RB_RPTR_ADDR_LO,
    A_SDMA0_GFX_IB_CNTL,
    A_SDMA0_GFX_DOORBELL,
    A_SDMA0_GFX_MINOR_PTR_UPDATE,
    A_SDMA0_GFX_RB_WPTR_POLL_ADDR_HI,
    A_SDMA0_GFX_RB_WPTR_POLL_ADDR_LO
};

C_ASSERT(RTL_NUMBER_OF(g_SdmaSnapshotRegs) == BC250_SDMA_SNAPSHOT_REG_COUNT);

/* Registradores GCVM / GART / VMID 0. Seg0 = 0x1260 */
#define A_GCVM_L2_CNTL                       0x0A100  /* dw 0x15e0 seg0 */
#define A_GCVM_L2_CNTL2                      0x0A104  /* dw 0x15e1 seg0 */
#define A_GCVM_L2_CNTL3                      0x0A108  /* dw 0x15e2 seg0 */
#define A_GCVM_L2_CNTL4                      0x0A160  /* dw 0x15f8 seg0 */
#define A_GCVM_L2_CNTL5                      0x0A184  /* dw 0x1601 seg0 */
#define A_GCVM_L2_PROTECTION_FAULT_STATUS    0x0A130  /* dw 0x15ec seg0 */
#define A_GCVM_L2_PROTECTION_FAULT_CNTL      0x0A120  /* dw 0x15e8 seg0 */
#define A_GCVM_L2_PROTECTION_FAULT_ADDR_LO32 0x0A134  /* dw 0x15ed seg0 */
#define A_GCVM_L2_PROTECTION_FAULT_ADDR_HI32 0x0A138  /* dw 0x15ee seg0 */
/* Bloco de traducao acrescentado em 2026-08-23 para o gate de memoria. Mesma
 * aritmetica byte = dw * 4 + 0x4980, conferida contra os registradores GCVM que
 * o laboratorio ja lia. Todos somente leitura. */
#define A_GCVM_L2_PROTECTION_FAULT_CNTL2     0x0A124  /* dw 0x15e9 seg0 */
#define A_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_LO 0x0A13C  /* dw 0x15ef seg0 */
#define A_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_HI 0x0A140  /* dw 0x15f0 seg0 */
#define A_GCVM_CONTEXT1_CNTL                 0x0A204  /* dw 0x1621 seg0 */
#define A_GCMC_VM_AGP_TOP                    0x0A608  /* dw 0x1722 seg0 */
#define A_GCMC_VM_AGP_BOT                    0x0A60C  /* dw 0x1723 seg0 */
#define A_GCMC_VM_AGP_BASE                   0x0A610  /* dw 0x1724 seg0 */
#define A_GCVM_INVALIDATE_ENG0_SEM           0x0A244  /* dw 0x1631 seg0 */
#define A_GCVM_INVALIDATE_ENG0_REQ           0x0A28C  /* dw 0x1643 seg0 */
#define A_GCVM_INVALIDATE_ENG0_ACK           0x0A2D4  /* dw 0x1655 seg0 */
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
#define A_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI 0x0A15C /* dw 0x15f7 seg0 */
#define A_GCVM_INVALIDATE_ENG0_ADDR_RANGE_LO 0x0A31C /* dw 0x1667 seg0 */
#define A_GCVM_INVALIDATE_ENG0_ADDR_RANGE_HI 0x0A320 /* dw 0x1668 seg0 */
#define A_GCVM_INVALIDATE_ENG17_SEM 0x0A288 /* dw 0x1642 seg0 */
#define A_GCVM_INVALIDATE_ENG17_REQ 0x0A2D0 /* dw 0x1654 seg0 */
#define A_GCVM_INVALIDATE_ENG17_ACK 0x0A318 /* dw 0x1666 seg0 */
/* Faixa de endereco da engine 17: e a engine da GART no upstream
 * ("Use register 17 for GART", gmc_v10_0_flush_gpu_tlb). */
#define A_GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO 0x0A3A4 /* dw 0x1689 seg0 */
#define A_GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI 0x0A3A8 /* dw 0x168a seg0 */
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

static BOOLEAN BcRangesOverlap(ULONG StartA, ULONG LengthA,
                               ULONG StartB, ULONG LengthB)
{
    ULONGLONG endA = (ULONGLONG)StartA + LengthA;
    ULONGLONG endB = (ULONGLONG)StartB + LengthB;
    return ((ULONGLONG)StartA < endB && (ULONGLONG)StartB < endA) ? TRUE : FALSE;
}

static NTSTATUS BcSdmaCaptureSnapshot(ULONG RingOffset, ULONG RingSize)
{
    ULONG ringDwords, totalDwords, i;
    PULONG backup;

    if (g_Dev.SdmaSnapshotValid) {
        return STATUS_DEVICE_BUSY;
    }

    /* Nunca tomar posse de um ring que já esteja ativo. A captura limpa deve
     * chegar aqui com RB_ENABLE=0; qualquer outro caso exige investigação. */
    if (BcRegReadRaw(A_SDMA0_GFX_RB_CNTL) & F_SDMA0_GFX_RB_CNTL__RB_ENABLE) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                  "preflight recusado: SDMA0_GFX_RB_CNTL já está habilitado");
        return STATUS_DEVICE_BUSY;
    }

    ringDwords = RingSize / sizeof(ULONG);
    totalDwords = ringDwords + 5; /* RPTR/WPTR (64-bit cada) + fence (32-bit) */
    backup = (PULONG)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                     totalDwords * sizeof(ULONG), 'bSdB');
    if (backup == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (i = 0; i < BC250_SDMA_SNAPSHOT_REG_COUNT; i++) {
        g_Dev.SdmaSnapshotValues[i] = BcRegReadRaw(g_SdmaSnapshotRegs[i]);
    }
    for (i = 0; i < ringDwords; i++) {
        backup[i] = BcVramReadRaw(RingOffset + i * sizeof(ULONG));
    }
    backup[ringDwords + 0] = BcVramReadRaw(BC250_SDMA_RPTR_WB_OFFSET);
    backup[ringDwords + 1] = BcVramReadRaw(BC250_SDMA_RPTR_WB_OFFSET + 4);
    backup[ringDwords + 2] = BcVramReadRaw(BC250_SDMA_WPTR_SHADOW_OFFSET);
    backup[ringDwords + 3] = BcVramReadRaw(BC250_SDMA_WPTR_SHADOW_OFFSET + 4);
    backup[ringDwords + 4] = BcVramReadRaw(BC250_SDMA_FENCE_VRAM_DEFAULT);

    g_Dev.SdmaVramBackup          = backup;
    g_Dev.SdmaVramBackupDwords    = totalDwords;
    g_Dev.SdmaSnapshotRingOffset  = RingOffset;
    g_Dev.SdmaSnapshotRingSize    = RingSize;
    g_Dev.SdmaSnapshotValid       = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS BcSdmaInit(ULONG RingSizeBytes, ULONG Placement, ULONG VramOffset,
                    ULONGLONG *OutGpuAddr)
{
    ULONG size = (RingSizeBytes > 0) ? RingSizeBytes : 4096;
    ULONG rbCntl, rbBufsz, ringDwords, temp, doorbell, doorbellWas;
    ULONGLONG gpuAddr = 0;
    ULONG i;
    NTSTATUS status;

    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        return STATUS_ACCESS_DENIED;
    }

    if (Placement != BC250DBG_SDMA_RING_VRAM) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                  "ring em RAM do sistema bloqueado: GCVM/VMID0 ainda não validado");
        return STATUS_NOT_SUPPORTED;
    }
    if (size < 256 || size > 4096 || (size & (size - 1)) != 0) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                  "tamanho de ring inválido: %u (exige potência de dois entre 256 e 4096)", size);
        return STATUS_INVALID_PARAMETER;
    }
    if ((VramOffset & 0xFFu) != 0) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                  "offset de ring 0x%X não está alinhado a 256 bytes", VramOffset);
        return STATUS_INVALID_PARAMETER;
    }

    if (g_Dev.SdmaRingBuffer != NULL || g_Dev.SdmaRingGpuAddr != 0) {
        if (OutGpuAddr) *OutGpuAddr = g_Dev.SdmaRingGpuAddr;
        return STATUS_SUCCESS;
    }

    if (Placement == BC250DBG_SDMA_RING_VRAM) {
        status = BcVramMap();
        if (!NT_SUCCESS(status)) {
            return status;
        }

        if ((ULONGLONG)VramOffset + size > (ULONGLONG)g_Dev.VramLength ||
            (ULONGLONG)BC250_SDMA_RPTR_WB_OFFSET + 8 > g_Dev.VramLength ||
            (ULONGLONG)BC250_SDMA_WPTR_SHADOW_OFFSET + 8 > g_Dev.VramLength ||
            (ULONGLONG)BC250_SDMA_FENCE_VRAM_DEFAULT + 4 > g_Dev.VramLength) {
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                      "ring/writebacks passam da janela de VRAM: off 0x%X, %u bytes, BAR0 %u MB",
                      VramOffset, size, g_Dev.VramLength / (1024 * 1024));
            return STATUS_INVALID_PARAMETER;
        }
        if (BcRangesOverlap(VramOffset, size, BC250_SDMA_RPTR_WB_OFFSET, 8) ||
            BcRangesOverlap(VramOffset, size, BC250_SDMA_WPTR_SHADOW_OFFSET, 8) ||
            BcRangesOverlap(VramOffset, size, BC250_SDMA_FENCE_VRAM_DEFAULT, 4)) {
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                      "ring em 0x%X/%u bytes sobrepõe writeback ou shadow reservado",
                      VramOffset, size);
            return STATUS_INVALID_PARAMETER;
        }

        status = BcSdmaCaptureSnapshot(VramOffset, size);
        if (!NT_SUCCESS(status)) {
            return status;
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

    /* Readback crítico: não continuar se base, ponteiros de polling ou enable
     * não ficaram exatamente na configuração transacional esperada. */
    if (BcRegReadRaw(A_SDMA0_GFX_RB_BASE) != (ULONG)(gpuAddr >> 8) ||
        BcRegReadRaw(A_SDMA0_GFX_RB_BASE_HI) != (ULONG)(gpuAddr >> 40) ||
        BcRegReadRaw(A_SDMA0_GFX_RB_WPTR_POLL_ADDR_LO) !=
            (ULONG)((BC250_VRAM_GPU_BASE + BC250_SDMA_WPTR_SHADOW_OFFSET) & 0xFFFFFFFFull) ||
        BcRegReadRaw(A_SDMA0_GFX_RB_RPTR_ADDR_LO) !=
            (ULONG)((BC250_VRAM_GPU_BASE + BC250_SDMA_RPTR_WB_OFFSET) & 0xFFFFFFFCull) ||
        (BcRegReadRaw(A_SDMA0_GFX_RB_CNTL) &
         (F_SDMA0_GFX_RB_CNTL__RB_ENABLE |
          F_SDMA0_GFX_RB_CNTL__RB_SIZE_MASK |
          F_SDMA0_GFX_RB_CNTL__RPTR_WB_ENABLE)) !=
         (rbCntl & (F_SDMA0_GFX_RB_CNTL__RB_ENABLE |
                    F_SDMA0_GFX_RB_CNTL__RB_SIZE_MASK |
                    F_SDMA0_GFX_RB_CNTL__RPTR_WB_ENABLE))) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                  "readback crítico da configuração SDMA divergiu; rollback imediato");
        (VOID)BcSdmaFree(NULL);
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

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

/* Guarda as primeiras divergencias de VRAM encontradas no rollback. Uma
 * contagem sozinha nao permite investigar nada: precisamos de regiao, offset,
 * valor lido e valor esperado. */
static VOID BcRollbackNoteVram(BC250DBG_SDMA_ROLLBACK_IO *Out, ULONG Region,
                               ULONG Offset, ULONG Got, ULONG Want)
{
    ULONG i;

    if (Out == NULL || Out->BadCount >= BC250DBG_ROLLBACK_BAD_SAMPLES) {
        return;
    }
    i = Out->BadCount++;
    Out->BadRegion[i] = Region;
    Out->BadOffset[i] = Offset;
    Out->BadGot[i]    = Got;
    Out->BadWant[i]   = Want;
}

static VOID BcRollbackNoteReg(BC250DBG_SDMA_ROLLBACK_IO *Out, ULONG Offset,
                              ULONG Got, ULONG Want)
{
    ULONG i;

    if (Out == NULL || Out->RegBadCount >= BC250DBG_ROLLBACK_BAD_SAMPLES) {
        return;
    }
    i = Out->RegBadCount++;
    Out->RegBadOffset[i] = Offset;
    Out->RegBadGot[i]    = Got;
    Out->RegBadWant[i]   = Want;
}

NTSTATUS BcSdmaFree(BC250DBG_SDMA_ROLLBACK_IO *Out)
{
    ULONG i, ringDwords = 0, regMismatch = 0, vramMismatch = 0;
    ULONG registersRestored = 0, vramRestored = 0;
    BOOLEAN hadSnapshot = g_Dev.SdmaSnapshotValid;

    if (Out != NULL) {
        RtlZeroMemory(Out, sizeof(*Out));
        Out->Size = sizeof(*Out);
        Out->SnapshotPresent = hadSnapshot ? 1u : 0u;
    }

    if (hadSnapshot && g_Dev.Mmio == NULL) {
        if (Out != NULL) Out->Status = BC250DBG_OPS_BADOP;
        return STATUS_DEVICE_NOT_READY;
    }

    if (hadSnapshot) {
        /* Para o consumidor e o polling antes de tocar em memória/endereço. */
        ULONG v = BcRegReadRaw(A_SDMA0_GFX_RB_CNTL);
        BcRegWriteRaw(A_SDMA0_GFX_RB_CNTL,
                      v & ~F_SDMA0_GFX_RB_CNTL__RB_ENABLE);
        BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_POLL_CNTL, 0);
        v = BcRegReadRaw(A_SDMA0_F32_CNTL);
        BcRegWriteRaw(A_SDMA0_F32_CNTL, v | F_SDMA0_F32_CNTL__HALT);
        KeStallExecutionProcessor(50);

        ringDwords = g_Dev.SdmaSnapshotRingSize / sizeof(ULONG);
        if (g_Dev.SdmaVramBackup != NULL && g_Dev.Vram != NULL &&
            g_Dev.SdmaVramBackupDwords == ringDwords + 5) {
            for (i = 0; i < ringDwords; i++) {
                BcVramWriteRaw(g_Dev.SdmaSnapshotRingOffset + i * sizeof(ULONG),
                               g_Dev.SdmaVramBackup[i]);
            }
            BcVramWriteRaw(BC250_SDMA_RPTR_WB_OFFSET,
                           g_Dev.SdmaVramBackup[ringDwords + 0]);
            BcVramWriteRaw(BC250_SDMA_RPTR_WB_OFFSET + 4,
                           g_Dev.SdmaVramBackup[ringDwords + 1]);
            BcVramWriteRaw(BC250_SDMA_WPTR_SHADOW_OFFSET,
                           g_Dev.SdmaVramBackup[ringDwords + 2]);
            BcVramWriteRaw(BC250_SDMA_WPTR_SHADOW_OFFSET + 4,
                           g_Dev.SdmaVramBackup[ringDwords + 3]);
            BcVramWriteRaw(BC250_SDMA_FENCE_VRAM_DEFAULT,
                           g_Dev.SdmaVramBackup[ringDwords + 4]);
            KeMemoryBarrier();
            BcHdpReadCacheInvalidate();
            vramRestored = ringDwords + 5;

            /* Deixar assentar antes de conferir. Medido em 2026-08-23: o
             * writeback do RPTR chega atrasado, e uma escrita em voo da engine
             * podia pousar depois da restauracao, produzindo divergencia
             * fantasma em rptr_wb. */
            {
                LARGE_INTEGER settle;
                settle.QuadPart = -50000;   /* 5 ms */
                KeDelayExecutionThread(KernelMode, FALSE, &settle);
                BcHdpReadCacheInvalidate();
            }
        } else {
            vramMismatch++;
        }

        for (i = 0; i < BC250_SDMA_SNAPSHOT_REG_COUNT; i++) {
            if (i == SDMA_SNAP_RB_CNTL ||
                i == SDMA_SNAP_RB_WPTR || i == SDMA_SNAP_RB_WPTR_HI ||
                i == SDMA_SNAP_MINOR_PTR) continue;
            BcRegWriteRaw(g_SdmaSnapshotRegs[i], g_Dev.SdmaSnapshotValues[i]);
            registersRestored++;
        }

        /* Nesta ASIC um WPTR menor só é aceito enquanto MINOR_PTR_UPDATE
         * está armado. Restaurá-lo como uma escrita comum deixa o valor da
         * última submissão residual, mesmo com o ring já desabilitado. */
        BcRegWriteRaw(A_SDMA0_GFX_MINOR_PTR_UPDATE,
                      F_SDMA0_GFX_MINOR_PTR_UPDATE__ENABLE);
        BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR,
                      g_Dev.SdmaSnapshotValues[SDMA_SNAP_RB_WPTR]);
        BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_HI,
                      g_Dev.SdmaSnapshotValues[SDMA_SNAP_RB_WPTR_HI]);
        registersRestored += 2;
        BcRegWriteRaw(A_SDMA0_GFX_MINOR_PTR_UPDATE,
                      g_Dev.SdmaSnapshotValues[SDMA_SNAP_MINOR_PTR]);
        registersRestored++;

        BcRegWriteRaw(A_SDMA0_GFX_RB_CNTL,
                      g_Dev.SdmaSnapshotValues[SDMA_SNAP_RB_CNTL]);
        registersRestored++;
        KeMemoryBarrier();

        for (i = 0; i < BC250_SDMA_SNAPSHOT_REG_COUNT; i++) {
            ULONG got = BcRegReadRaw(g_SdmaSnapshotRegs[i]);
            if (got != g_Dev.SdmaSnapshotValues[i]) {
                regMismatch++;
                BcRollbackNoteReg(Out, g_SdmaSnapshotRegs[i], got,
                                  g_Dev.SdmaSnapshotValues[i]);
            }
        }
        if (g_Dev.SdmaVramBackup != NULL && g_Dev.Vram != NULL &&
            g_Dev.SdmaVramBackupDwords == ringDwords + 5) {
            for (i = 0; i < ringDwords; i++) {
                ULONG off = g_Dev.SdmaSnapshotRingOffset + i * sizeof(ULONG);
                ULONG got = BcVramReadRaw(off);
                if (got != g_Dev.SdmaVramBackup[i]) {
                    vramMismatch++;
                    BcRollbackNoteVram(Out, BC250DBG_ROLLBACK_RGN_RING, off, got,
                                       g_Dev.SdmaVramBackup[i]);
                }
            }
            {
                static const ULONG offs[5] = {
                    BC250_SDMA_RPTR_WB_OFFSET,
                    BC250_SDMA_RPTR_WB_OFFSET + 4,
                    BC250_SDMA_WPTR_SHADOW_OFFSET,
                    BC250_SDMA_WPTR_SHADOW_OFFSET + 4,
                    BC250_SDMA_FENCE_VRAM_DEFAULT
                };
                static const ULONG rgns[5] = {
                    BC250DBG_ROLLBACK_RGN_RPTR_WB, BC250DBG_ROLLBACK_RGN_RPTR_WB,
                    BC250DBG_ROLLBACK_RGN_WPTR_SH, BC250DBG_ROLLBACK_RGN_WPTR_SH,
                    BC250DBG_ROLLBACK_RGN_FENCE
                };
                ULONG k;

                for (k = 0; k < 5; k++) {
                    ULONG want = g_Dev.SdmaVramBackup[ringDwords + k];
                    ULONG got = BcVramReadRaw(offs[k]);

                    if (got != want) {
                        /* Segunda tentativa: writeback e shadow sao escritos
                         * pela propria engine, entao uma escrita em voo pode
                         * ter pousado depois da restauracao. Reescrever uma vez
                         * e reconferir separa corrida transitoria de falha
                         * real. A tentativa fica no log; se persistir, conta
                         * como divergencia normalmente. */
                        LARGE_INTEGER settle;

                        BcVramWriteRaw(offs[k], want);
                        KeMemoryBarrier();
                        settle.QuadPart = -20000;   /* 2 ms */
                        KeDelayExecutionThread(KernelMode, FALSE, &settle);
                        BcHdpReadCacheInvalidate();

                        BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_SDMA,
                                  "rollback: 0x%X divergiu (0x%X != 0x%X); "
                                  "reescrito e reconferindo",
                                  offs[k], got, want);
                        got = BcVramReadRaw(offs[k]);
                    }
                    if (got != want) {
                        vramMismatch++;
                        BcRollbackNoteVram(Out, rgns[k], offs[k], got, want);
                    }
                }
            }
        }

        /* Ultima escrita do teardown: HALT no F32.
         *
         * Medido na BC-250 em 2026-08-23: sem isto o microcontrolador terminava
         * o teardown RODANDO. O motivo e que o snapshot e tirado em 'sdma init',
         * ou seja, depois do reset do F32, ja com HALT=0 — restaurar o snapshot
         * fielmente desfazia o HALT aplicado no inicio do rollback.
         *
         * A ordem importa: primeiro conferimos que todos os 24 registradores
         * voltaram ao snapshot, e so entao haltamos. Assim a verificacao de
         * fidelidade continua valendo e o estado final ainda e seguro. O
         * firmware em si permanece carregado ate o proximo reboot; isso o
         * rollback nao tem como desfazer. */
        {
            ULONG f32 = BcRegReadRaw(A_SDMA0_F32_CNTL);
            BcRegWriteRaw(A_SDMA0_F32_CNTL, f32 | F_SDMA0_F32_CNTL__HALT);
            KeStallExecutionProcessor(50);
            if ((BcRegReadRaw(A_SDMA0_F32_CNTL) & F_SDMA0_F32_CNTL__HALT) == 0) {
                BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                          "SDMA0 rollback: F32 recusou HALT (F32_CNTL=0x%X)",
                          BcRegReadRaw(A_SDMA0_F32_CNTL));
                regMismatch++;
            }
        }
    }

    if (g_Dev.SdmaRingBuffer != NULL) {
        MmFreeContiguousMemory(g_Dev.SdmaRingBuffer);
        g_Dev.SdmaRingBuffer = NULL;
    }
    if (g_Dev.SdmaVramBackup != NULL) {
        ExFreePoolWithTag(g_Dev.SdmaVramBackup, 'bSdB');
        g_Dev.SdmaVramBackup = NULL;
    }

    g_Dev.SdmaRingPhysical.QuadPart = 0;
    g_Dev.SdmaRingGpuAddr    = 0;
    g_Dev.SdmaRingVramOffset = 0;
    g_Dev.SdmaRingPlacement  = BC250DBG_SDMA_RING_SYSRAM;
    g_Dev.SdmaRingSize       = 0;
    g_Dev.SdmaWptr           = 0;
    g_Dev.SdmaVramBackupDwords   = 0;
    g_Dev.SdmaSnapshotRingOffset = 0;
    g_Dev.SdmaSnapshotRingSize   = 0;
    g_Dev.SdmaSnapshotValid      = FALSE;

    if (Out != NULL) {
        Out->RegistersRestored = registersRestored;
        Out->RegisterMismatches = regMismatch;
        Out->VramDwordsRestored = vramRestored;
        Out->VramMismatches = vramMismatch;
        Out->Status = (regMismatch == 0 && vramMismatch == 0)
                    ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;
    }

    BcLogText((regMismatch == 0 && vramMismatch == 0) ? BC250DBG_LVL_INFO : BC250DBG_LVL_ERROR,
              BC250DBG_SUB_SDMA,
              "SDMA0 rollback: snapshot=%u regs=%u mismatch=%u VRAMdw=%u mismatch=%u",
              hadSnapshot ? 1u : 0u, registersRestored, regMismatch,
              vramRestored, vramMismatch);

    return (regMismatch == 0 && vramMismatch == 0)
         ? STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;
}

NTSTATUS BcSdmaSubmit(const ULONG *Dwords, ULONG Count, ULONG SubmitFlags,
                      ULONG *RptrBefore, ULONG *WptrAfter, ULONG *RptrAfter,
                      BC250DBG_SDMA_OP_IO *Telemetry)
{
    ULONG rptrBefore, wptr, rptrAfter = 0, i;
    ULONG *ring;
    ULONG ringDwords;
    LARGE_INTEGER timeout;
    BOOLEAN noKick = (SubmitFlags & BC250DBG_SDMA_FLAG_NO_KICK) ? TRUE : FALSE;

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

    /* Modo negativo NO_KICK: os dwords ficam no anel, mas o ponteiro de
     * software NAO avanca e nada e publicado. A proxima submissao real
     * sobrescreve estes dwords no mesmo slot. */
    if (!noKick) {
        g_Dev.SdmaWptr = wptr;
    }
    KeMemoryBarrier();

    /* O F32 percebe trabalho novo pelo shadow do WPTR, nao pelo registrador.
     * Escrevemos nos dois: o shadow para o poll, e o registrador porque o
     * caminho bare-metal do amdgpu tambem escreve la. */
    if (!noKick && g_Dev.SdmaRingPlacement == BC250DBG_SDMA_RING_VRAM) {
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
    if (!noKick) {
        BcRegWriteRaw(A_SDMA0_GFX_MINOR_PTR_UPDATE, F_SDMA0_GFX_MINOR_PTR_UPDATE__ENABLE);
        BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR, wptr);
        BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_HI, 0);
        BcRegWriteRaw(A_SDMA0_GFX_MINOR_PTR_UPDATE, 0);
    }

    /* Amostra no ponto mais próximo possível do kick, antes do primeiro poll
     * de conclusão. Para comandos curtos, um sampler em user mode pode perder
     * completamente esta janela. */
    if (Telemetry != NULL) {
        Telemetry->GfxMiscKick = BcRegReadRaw(BC250_REG_SMUIO_GFX_MISC_CNTL);
        Telemetry->GfxoffKick = (Telemetry->GfxMiscKick & BC250_GFXOFF_STATUS_MASK)
                              >> BC250_GFXOFF_STATUS_SHIFT;
        Telemetry->GrbmKick = BcRegReadRaw(BC250_REG_GRBM_STATUS);
        Telemetry->StatusRegKick = BcRegReadRaw(A_SDMA0_STATUS_REG);
        Telemetry->F32PcKick = (BcRegReadRaw(A_SDMA0_STATUS2_REG) >> 2) & 0x3FFFu;
        Telemetry->ClkCtrlKick = BcRegReadRaw(A_SDMA0_CLK_CTRL);
        Telemetry->Kicked = noKick ? 0u : 1u;
    }

    if (WptrAfter) *WptrAfter = wptr;

    /* Progresso vem do writeback, nao do registrador: sdma_v5_0_ring_get_rptr
     * do amdgpu le exatamente daqui. */
    timeout.QuadPart = -10000;
    if (noKick) {
        /* Sem kick nao ha nada a esperar. Damos uma janela equivalente a
         * varias iteracoes do poll normal e so entao lemos o RPTR: o objetivo
         * e provar que ele NAO andou. */
        for (i = 0; i < 10; i++) {
            KeDelayExecutionThread(KernelMode, FALSE, &timeout);
        }
        rptrAfter = (g_Dev.SdmaRingPlacement == BC250DBG_SDMA_RING_VRAM)
                  ? BcVramReadRaw(BC250_SDMA_RPTR_WB_OFFSET)
                  : BcRegReadRaw(A_SDMA0_GFX_RB_RPTR);
    } else {
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
    }

    if (RptrAfter) *RptrAfter = rptrAfter;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_SDMA,
              "SDMA0 Submit %u dw%s: WPTR 0x%X -> RPTR 0x%X (%s)",
              Count, noKick ? " SEM KICK (negativo)" : "", wptr, rptrAfter,
              noKick ? "NAO PUBLICADO"
                     : ((rptrAfter == wptr) ? "CONCLUIDO" : "TIMEOUT"));

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
    if (RbBase) {
        ULONGLONG baseLo = BcRegReadRaw(A_SDMA0_GFX_RB_BASE);
        ULONGLONG baseHi = BcRegReadRaw(A_SDMA0_GFX_RB_BASE_HI);
        /* O registrador armazena GPU_ADDR >> 8. Isto também revela o anel
         * deixado pelo POST, mesmo quando ele não foi criado por nós. */
        RbBase->QuadPart = (LONGLONG)((baseHi << 40) | (baseLo << 8));
    }
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

NTSTATUS BcTelemetrySample(BC250DBG_TELEMETRY_IO *Io)
{
    LARGE_INTEGER freq, qpc;
    PHYSICAL_ADDRESS rbBase;
    ULONG smuStatus = 0, result = 0;
    NTSTATUS status;

    if (Io == NULL) return STATUS_INVALID_PARAMETER;
    if (g_Dev.Mmio == NULL) return STATUS_DEVICE_NOT_READY;

    Io->ValidMask = 0;
    Io->Status = BC250DBG_OPS_OK;
    qpc = KeQueryPerformanceCounter(&freq);
    Io->QpcTicks = (ULONGLONG)qpc.QuadPart;
    Io->QpcFrequency = (ULONGLONG)freq.QuadPart;
    Io->InterruptTime100ns = KeQueryInterruptTime();

    Io->GfxMiscCntl = BcRegReadRaw(BC250_REG_SMUIO_GFX_MISC_CNTL);
    if (g_Dev.MmioLength >= BC250_REG_SMUIO_GFX_MISC_CNTL + sizeof(ULONG) &&
        Io->GfxMiscCntl != 0xFFFFFFFFu) {
        Io->ValidMask |= BC250DBG_TELEM_GFXOFF_VALID;
        Io->GfxoffState = (Io->GfxMiscCntl & BC250_GFXOFF_STATUS_MASK)
                        >> BC250_GFXOFF_STATUS_SHIFT;
    } else {
        Io->GfxoffState = 0xFFFFFFFFu;
    }

    Io->GrbmStatus = BcRegReadRaw(BC250_REG_GRBM_STATUS);
    (VOID)BcSdmaStatus(&Io->SdmaRptr, &Io->SdmaWptr,
                       &Io->SdmaRbCntl, &Io->SdmaStatusReg, &rbBase,
                       &Io->SdmaRptrWriteback, &Io->SdmaWptrShadow);
    Io->SdmaF32Cntl = BcRegReadRaw(A_SDMA0_F32_CNTL);
    Io->SdmaF32Pc = BcSdmaF32Pc();
    Io->SdmaClkCtrl = BcRegReadRaw(A_SDMA0_CLK_CTRL);
    Io->ThermalTrusted = (ULONG)g_Dev.ThermalTrusted;
    Io->ThermalMilliC = (LONG)g_Dev.ThermalMilliC;
    Io->RawEdge = g_Dev.RawEdge;
    Io->RawJunction = g_Dev.RawJunction;
    Io->RawMem = g_Dev.RawMem;

    if (Io->RequestFlags & BC250DBG_TELEM_INCLUDE_SMU) {
        status = BcSmuMsg(0, PPSMC_QueryGfxclk, 0, 0, 100,
                          &smuStatus, &result);
        Io->SmuGfxclkStatus = smuStatus;
        Io->SmuGfxclkMhz = result;
        if (NT_SUCCESS(status) && smuStatus == BC250_SMU_OK)
            Io->ValidMask |= BC250DBG_TELEM_SMU_GFXCLK_VALID;

        status = BcSmuMsg(0, PPSMC_QueryActiveWgp, 0, 0, 100,
                          &smuStatus, &result);
        Io->SmuActiveWgpStatus = smuStatus;
        Io->SmuActiveWgp = result;
        if (NT_SUCCESS(status) && smuStatus == BC250_SMU_OK)
            Io->ValidMask |= BC250DBG_TELEM_SMU_WGP_VALID;

        status = BcSmuMsg(0, PPSMC_GetEnabledSmuFeatures, 0, 0, 100,
                          &smuStatus, &result);
        Io->SmuFeaturesStatus = smuStatus;
        Io->SmuFeatures = result;
        if (NT_SUCCESS(status) && smuStatus == BC250_SMU_OK)
            Io->ValidMask |= BC250DBG_TELEM_SMU_FEATURE_VALID;
    }

    return STATUS_SUCCESS;
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
#define BC250_SDMA_OP_FENCE       5u
#define BC250_SDMA_FENCE_MTYPE_UC 3u

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

/* Dump somente leitura do estado de falha da SDMA0. Nao escreve nada, nao
 * decodifica bit nenhum por suposicao: quem interpreta e a comparacao entre um
 * dump saudavel e um dump da engine quebrada. */
VOID BcSdmaFaultDump(BC250DBG_SDMA_FAULT_IO *Out)
{
    if (Out == NULL) {
        return;
    }

    RtlZeroMemory(Out, sizeof(*Out));
    Out->Size = sizeof(*Out);

    if (g_Dev.Mmio == NULL) {
        Out->Status = BC250DBG_OPS_DENIED;
        return;
    }

    Out->StatusReg   = BcRegReadRaw(A_SDMA0_STATUS_REG);
    Out->Status1Reg  = BcRegReadRaw(A_SDMA0_STATUS1_REG);
    Out->Status2Reg  = BcRegReadRaw(A_SDMA0_STATUS2_REG);
    Out->Status3Reg  = BcRegReadRaw(A_SDMA0_STATUS3_REG);
    Out->Freeze      = BcRegReadRaw(A_SDMA0_FREEZE);
    Out->ErrorLog    = BcRegReadRaw(A_SDMA0_ERROR_LOG);
    Out->EdcCounter  = BcRegReadRaw(A_SDMA0_EDC_COUNTER);
    Out->PowerCntl   = BcRegReadRaw(A_SDMA0_POWER_CNTL);
    Out->Cntl        = BcRegReadRaw(A_SDMA0_CNTL);
    Out->ChickenBits = BcRegReadRaw(A_SDMA0_CHICKEN_BITS_DIAG);
    Out->F32Cntl     = BcRegReadRaw(A_SDMA0_F32_CNTL);
    Out->F32Pc       = BcSdmaF32Pc();
    Out->SemWaitFailTimer = BcRegReadRaw(A_SDMA0_SEM_WAIT_FAIL_TIMER_CNTL);

    Out->Utcl1Cntl      = BcRegReadRaw(A_SDMA0_UTCL1_CNTL);
    Out->Utcl1Watermk   = BcRegReadRaw(A_SDMA0_UTCL1_WATERMK);
    Out->Utcl1RdStatus  = BcRegReadRaw(A_SDMA0_UTCL1_RD_STATUS);
    Out->Utcl1WrStatus  = BcRegReadRaw(A_SDMA0_UTCL1_WR_STATUS);
    Out->Utcl1Inv0      = BcRegReadRaw(A_SDMA0_UTCL1_INV0);
    Out->Utcl1Inv1      = BcRegReadRaw(A_SDMA0_UTCL1_INV1);
    Out->Utcl1Inv2      = BcRegReadRaw(A_SDMA0_UTCL1_INV2);
    Out->Utcl1RdXnack0  = BcRegReadRaw(A_SDMA0_UTCL1_RD_XNACK0);
    Out->Utcl1RdXnack1  = BcRegReadRaw(A_SDMA0_UTCL1_RD_XNACK1);
    Out->Utcl1WrXnack0  = BcRegReadRaw(A_SDMA0_UTCL1_WR_XNACK0);
    Out->Utcl1WrXnack1  = BcRegReadRaw(A_SDMA0_UTCL1_WR_XNACK1);
    Out->Utcl1Timeout   = BcRegReadRaw(A_SDMA0_UTCL1_TIMEOUT);
    Out->Utcl1Page      = BcRegReadRaw(A_SDMA0_UTCL1_PAGE);

    Out->GcvmFaultCntl    = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_CNTL);
    Out->GcvmFaultStatus  = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_STATUS);
    Out->GcvmFaultAddrLo  = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_ADDR_LO32);
    Out->GcvmFaultAddrHi  = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_ADDR_HI32);

    Out->GrbmStatus = BcRegReadRaw(BC250_REG_GRBM_STATUS);
    Out->RbCntl     = BcRegReadRaw(A_SDMA0_GFX_RB_CNTL);
    Out->Rptr       = BcRegReadRaw(A_SDMA0_GFX_RB_RPTR);
    Out->Wptr       = BcRegReadRaw(A_SDMA0_GFX_RB_WPTR);
    if (g_Dev.Vram != NULL) {
        Out->RptrWriteback = BcVramReadRaw(BC250_SDMA_RPTR_WB_OFFSET);
        Out->WptrShadow    = BcVramReadRaw(BC250_SDMA_WPTR_SHADOW_OFFSET);
    }

    Out->EngineSuspect = g_Dev.SdmaEngineSuspect ? 1u : 0u;
    Out->SuspectReason = g_Dev.SdmaSuspectReason;
    Out->Status = BC250DBG_OPS_OK;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_SDMA,
              "SDMA fault dump: STATUS 0x%X/%X/%X/%X ERROR_LOG 0x%X "
              "XNACK rd 0x%X/0x%X wr 0x%X/0x%X",
              Out->StatusReg, Out->Status1Reg, Out->Status2Reg, Out->Status3Reg,
              Out->ErrorLog, Out->Utcl1RdXnack0, Out->Utcl1RdXnack1,
              Out->Utcl1WrXnack0, Out->Utcl1WrXnack1);
}

/* Le a configuracao da SDMA0 no estado de boot, uma unica vez, logo depois do
 * mapeamento do BAR5.
 *
 * ATENCAO ao que este valor significa. Medido em 2026-08-23, com 20 amostras e
 * nenhuma excecao: UTCL1_PAGE le 0x004C5C20 quando o F32 esta rodando e
 * 0x000C5C20 quando ele esta haltado. No boot o F32 esta haltado, entao o que
 * esta funcao captura e a leitura do estado haltado, NAO a configuracao ativa.
 *
 * Serve como referencia comparavel, nao como "valor do POST". A diferenca de
 * 0x00400000 entre as duas leituras foi inicialmente interpretada como perda
 * do bit USE_BC causada pelo reset; a correlacao com o estado do F32 revogou
 * essa leitura. */
VOID BcSdmaCaptureBootConfig(VOID)
{
    if (g_Dev.Mmio == NULL || g_Dev.BootCfgValid) {
        return;
    }

    g_Dev.BootUtcl1Page    = BcRegReadRaw(A_SDMA0_UTCL1_PAGE);
    g_Dev.BootUtcl1Cntl    = BcRegReadRaw(A_SDMA0_UTCL1_CNTL);
    g_Dev.BootUtcl1Watermk = BcRegReadRaw(A_SDMA0_UTCL1_WATERMK);
    g_Dev.BootChickenBits  = BcRegReadRaw(A_SDMA0_CHICKEN_BITS_DIAG);
    g_Dev.BootSdmaCntl     = BcRegReadRaw(A_SDMA0_CNTL);
    g_Dev.BootClkCtrl      = BcRegReadRaw(A_SDMA0_CLK_CTRL);
    g_Dev.BootCfgValid     = TRUE;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_SDMA,
              "config SDMA0 do POST: UTCL1_PAGE 0x%X (USE_BC=%u) UTCL1_CNTL 0x%X "
              "CHICKEN 0x%X CNTL 0x%X CLK 0x%X",
              g_Dev.BootUtcl1Page,
              (g_Dev.BootUtcl1Page & 0x00400000u) ? 1u : 0u,
              g_Dev.BootUtcl1Cntl, g_Dev.BootChickenBits,
              g_Dev.BootSdmaCntl, g_Dev.BootClkCtrl);
}

/* Soft reset do bloco SDMA0, na sequencia do upstream.
 *
 * amdgpu faz isto em tres funcoes: sdma_v5_0_stop_queue (parar o anel, FREEZE,
 * HALT no F32, desligar UTC_L1), sdma_v5_0_soft_reset_engine (pulso em
 * GRBM_SOFT_RESET.SOFT_RESET_SDMA0) e sdma_v5_0_restore_queue (tirar o FREEZE
 * e reprogramar o anel).
 *
 * Aqui a terceira parte NAO e feita: o bloco perde a configuracao e quem chama
 * precisa refazer 'sdma init'. Deixar explicito e melhor do que reprogramar por
 * conta propria e devolver um estado que ninguem pediu.
 *
 * Todo o antes/depois vai para o payload, inclusive CMD_OP de STATUS2_REG, que
 * e o campo onde o opcode invalido fica latchado. */
NTSTATUS BcSdmaEngineReset(BC250DBG_SDMA_ENGINE_RESET_IO *Out)
{
    LARGE_INTEGER delay;
    ULONG v, i;

    if (Out == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Out, sizeof(*Out));
    Out->Size = sizeof(*Out);

    if (g_Dev.Mmio == NULL) {
        Out->Status = BC250DBG_OPS_DENIED;
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        Out->Status = BC250DBG_OPS_DENIED;
        return STATUS_ACCESS_DENIED;
    }

    /* Nao resetar o bloco com snapshot transacional aberto.
     *
     * Medido em 2026-08-23 08:09: o reset zerava o estado de anel do driver
     * mas deixava o snapshot valido; o 'sdma init' seguinte era recusado com
     * STATUS_DEVICE_BUSY pela captura de snapshot, e o ensaio interpretou isso
     * como "a engine nao recuperou" — quando na verdade nem chegou a tentar.
     *
     * Recusar aqui, explicitamente, e melhor que resetar e perder o ponto de
     * restauracao: os valores de registrador do snapshot deixam de valer no
     * instante em que o bloco e resetado. */
    if (g_Dev.SdmaSnapshotValid) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                  "soft reset recusado: snapshot transacional aberto; "
                  "rode 'sdma rollback' antes");
        Out->Status = BC250DBG_OPS_BADOP;
        return STATUS_INVALID_DEVICE_STATE;
    }

    /* Referencia de configuracao a restaurar depois do pulso.
     *
     * A leitura da UTCL1 so vale com o F32 rodando: haltado, ela devolve o
     * padrao de reset (20 amostras em 2026-08-23, sem excecao). Entao usamos a
     * leitura ao vivo quando o microcontrolador esta rodando e caimos para a
     * captura do boot quando nao esta, registrando qual fonte foi usada em vez
     * de fingir que sao equivalentes. */
    {
        BOOLEAN f32Running =
            (BcRegReadRaw(A_SDMA0_F32_CNTL) & F_SDMA0_F32_CNTL__HALT) ? FALSE : TRUE;

        if (f32Running) {
            Out->Utcl1PageBefore    = BcRegReadRaw(A_SDMA0_UTCL1_PAGE);
            Out->Utcl1CntlBefore    = BcRegReadRaw(A_SDMA0_UTCL1_CNTL);
            Out->ChickenBefore      = BcRegReadRaw(A_SDMA0_CHICKEN_BITS_DIAG);
            Out->Utcl1WatermkBefore = BcRegReadRaw(A_SDMA0_UTCL1_WATERMK);
            Out->ClkCtrlBefore      = BcRegReadRaw(A_SDMA0_CLK_CTRL);
        } else if (g_Dev.BootCfgValid) {
            Out->Utcl1PageBefore    = g_Dev.BootUtcl1Page;
            Out->Utcl1CntlBefore    = g_Dev.BootUtcl1Cntl;
            Out->ChickenBefore      = g_Dev.BootChickenBits;
            Out->Utcl1WatermkBefore = g_Dev.BootUtcl1Watermk;
            Out->ClkCtrlBefore      = g_Dev.BootClkCtrl;
        } else {
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                      "soft reset recusado: sem referencia de configuracao da SDMA0");
            Out->Status = BC250DBG_OPS_BADOP;
            return STATUS_DEVICE_NOT_READY;
        }

        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_SDMA,
                  "soft reset: fonte da configuracao = %s, UTCL1_PAGE 0x%X",
                  f32Running ? "leitura ao vivo (F32 rodando)"
                             : "captura do boot (F32 haltado)",
                  Out->Utcl1PageBefore);
    }

    Out->RbCntlBefore    = BcRegReadRaw(A_SDMA0_GFX_RB_CNTL);
    Out->CntlBefore      = BcRegReadRaw(A_SDMA0_CNTL);
    Out->F32CntlBefore   = BcRegReadRaw(A_SDMA0_F32_CNTL);
    Out->FreezeBefore    = BcRegReadRaw(A_SDMA0_FREEZE);
    Out->StatusRegBefore = BcRegReadRaw(A_SDMA0_STATUS_REG);
    Out->Status2Before   = BcRegReadRaw(A_SDMA0_STATUS2_REG);
    Out->CmdOpBefore     = (Out->Status2Before & F_SDMA0_STATUS2_REG__CMD_OP_MASK)
                         >> F_SDMA0_STATUS2_REG__CMD_OP_SHIFT;

    /* 1. Parar o consumidor e o polling. */
    v = BcRegReadRaw(A_SDMA0_GFX_RB_CNTL);
    BcRegWriteRaw(A_SDMA0_GFX_RB_CNTL, v & ~F_SDMA0_GFX_RB_CNTL__RB_ENABLE);
    BcRegWriteRaw(A_SDMA0_GFX_RB_WPTR_POLL_CNTL, 0);

    /* 2. FREEZE e espera pelo FROZEN. O upstream aceita seguir mesmo sem o
     *    FROZEN, desde que STATUS1_REG diga que a engine esta ociosa. */
    v = BcRegReadRaw(A_SDMA0_FREEZE);
    BcRegWriteRaw(A_SDMA0_FREEZE, v | F_SDMA0_FREEZE__FREEZE);
    Out->FreezeAfterSet = BcRegReadRaw(A_SDMA0_FREEZE);

    delay.QuadPart = -10000;   /* 1 ms */
    for (i = 0; i < 100; i++) {
        v = BcRegReadRaw(A_SDMA0_FREEZE);
        if (v & F_SDMA0_FREEZE__FROZEN) {
            Out->FrozenObserved = 1;
            break;
        }
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    Out->FreezeIterations = i;

    Out->Status1Before = BcRegReadRaw(A_SDMA0_STATUS1_REG);
    Out->Status1Idle = ((Out->Status1Before & M_SDMA0_STATUS1_REG__IDLE) ==
                        M_SDMA0_STATUS1_REG__IDLE) ? 1u : 0u;
    if (!Out->FrozenObserved && !Out->Status1Idle) {
        /* Mesmo criterio do upstream: sem FROZEN e sem ociosidade, resetar
         * agora poderia interromper transacao em voo. */
        BcRegWriteRaw(A_SDMA0_FREEZE,
                      BcRegReadRaw(A_SDMA0_FREEZE) & ~F_SDMA0_FREEZE__FREEZE);
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                  "soft reset recusado: sem FROZEN e STATUS1 0x%X nao ocioso",
                  Out->Status1Before);
        Out->Status = BC250DBG_OPS_TIMEOUT;
        return STATUS_DEVICE_BUSY;
    }

    /* 3. HALT no F32 e UTC_L1 desligado, como no stop_queue do upstream. */
    BcRegWriteRaw(A_SDMA0_F32_CNTL,
                  BcRegReadRaw(A_SDMA0_F32_CNTL) | F_SDMA0_F32_CNTL__HALT);
    BcRegWriteRaw(A_SDMA0_CNTL,
                  BcRegReadRaw(A_SDMA0_CNTL) & ~F_SDMA0_CNTL__UTC_L1_ENABLE);

    /* 4. Pulso de soft reset SOMENTE no bit da SDMA0. Os outros bits do
     *    registrador sao preservados por leitura-modificacao-escrita: um deles
     *    e SOFT_RESET_GFX. */
    Out->GrbmBefore = BcRegReadRaw(A_GRBM_SOFT_RESET);
    BcRegWriteRaw(A_GRBM_SOFT_RESET,
                  Out->GrbmBefore | F_GRBM_SOFT_RESET__SOFT_RESET_SDMA0);
    Out->GrbmWithReset = BcRegReadRaw(A_GRBM_SOFT_RESET);

    delay.QuadPart = -500;     /* 50 us, como no upstream */
    KeDelayExecutionThread(KernelMode, FALSE, &delay);

    BcRegWriteRaw(A_GRBM_SOFT_RESET,
                  Out->GrbmWithReset & ~F_GRBM_SOFT_RESET__SOFT_RESET_SDMA0);
    Out->GrbmAfterClear = BcRegReadRaw(A_GRBM_SOFT_RESET);

    delay.QuadPart = -100000;  /* 10 ms para o bloco assentar */
    KeDelayExecutionThread(KernelMode, FALSE, &delay);

    /* 5a. O que o reset deixou, antes de restaurar. */
    Out->Utcl1PageReset = BcRegReadRaw(A_SDMA0_UTCL1_PAGE);
    Out->Utcl1CntlReset = BcRegReadRaw(A_SDMA0_UTCL1_CNTL);
    Out->ChickenReset   = BcRegReadRaw(A_SDMA0_CHICKEN_BITS_DIAG);

    /* 5b. Restauracao de configuracao REMOVIDA, por resultado experimental.
     *
     * Existia a hipotese de que o reset apagava configuracao da UTCL1 — em
     * particular UTCL1_PAGE.USE_BC — e que reprograma-la era o passo que
     * faltava. O controle positivo de 2026-08-23 08:21 derrubou a hipotese:
     * na variante V2 (rollback -> soft reset -> psp loadfw -> sdma init) a
     * engine voltou a escrever com fence fisica mesmo tendo o UTCL1_PAGE
     * "restaurado" para 0x000C5C20, ou seja, sem o bit em questao.
     *
     * Quem reestabelece o que o bloco precisa e o carregamento do firmware
     * pelo PSP mais o 'sdma init'. Escrever esses registradores a partir de
     * leituras cuja validade depende do estado do bloco so acrescentava risco.
     *
     * Os campos de UTCL1, CHICKEN_BITS e CLK_CTRL do payload continuam sendo
     * preenchidos como OBSERVACAO, e ConfigRestored/ConfigMismatches ficam em
     * zero de proposito. */
    {
        LARGE_INTEGER settle;

        /* CNTL de volta ao valor de entrada: este passo faz parte do
         * stop/restore do upstream e estava presente na sequencia validada. */
        BcRegWriteRaw(A_SDMA0_CNTL, Out->CntlBefore);
        settle.QuadPart = -10000;   /* 1 ms */
        KeDelayExecutionThread(KernelMode, FALSE, &settle);
    }

    Out->Utcl1PageAfter    = BcRegReadRaw(A_SDMA0_UTCL1_PAGE);
    Out->Utcl1CntlAfter    = BcRegReadRaw(A_SDMA0_UTCL1_CNTL);
    Out->ChickenAfter      = BcRegReadRaw(A_SDMA0_CHICKEN_BITS_DIAG);
    Out->Utcl1WatermkAfter = BcRegReadRaw(A_SDMA0_UTCL1_WATERMK);
    Out->ClkCtrlAfter      = BcRegReadRaw(A_SDMA0_CLK_CTRL);

    /* 5c. Tirar o FREEZE. CNTL ja foi restaurado antes da UTCL1. */
    BcRegWriteRaw(A_SDMA0_FREEZE,
                  BcRegReadRaw(A_SDMA0_FREEZE) & ~F_SDMA0_FREEZE__FREEZE);

    Out->StatusRegAfter = BcRegReadRaw(A_SDMA0_STATUS_REG);
    Out->Status2After   = BcRegReadRaw(A_SDMA0_STATUS2_REG);
    Out->CmdOpAfter     = (Out->Status2After & F_SDMA0_STATUS2_REG__CMD_OP_MASK)
                        >> F_SDMA0_STATUS2_REG__CMD_OP_SHIFT;
    Out->CntlAfter      = BcRegReadRaw(A_SDMA0_CNTL);
    Out->F32CntlAfter   = BcRegReadRaw(A_SDMA0_F32_CNTL);
    Out->FreezeFinal    = BcRegReadRaw(A_SDMA0_FREEZE);

    /* O anel foi para o espaco junto com o bloco: quem chama tem de refazer
     * 'sdma init'. Esquecer isso deixaria o driver achando que tem um anel. */
    g_Dev.SdmaRingGpuAddr = 0;
    g_Dev.SdmaWptr = 0;

    Out->Status = BC250DBG_OPS_OK;

    BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_SDMA,
              "SDMA0 soft reset: FROZEN=%u STATUS1=0x%X GRBM 0x%X -> 0x%X -> 0x%X, "
              "CMD_OP %u -> %u",
              Out->FrozenObserved, Out->Status1Before, Out->GrbmBefore,
              Out->GrbmWithReset, Out->GrbmAfterClear,
              Out->CmdOpBefore, Out->CmdOpAfter);

    return STATUS_SUCCESS;
}

NTSTATUS BcSdmaExecOp(BC250DBG_SDMA_OP_IO *Io)
{
    ULONG pkt[4 + BC250DBG_SDMA_WRITE_MAX_DW + 16];
    ULONG n = 0, i;
    ULONGLONG dstGpu, srcGpu, fenceGpu;
    ULONG rptrBefore = 0, wptr = 0, rptrAfter = 0;
    ULONG negatives;
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
    if (g_Dev.Vram == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    /* Bits desconhecidos em Flags sao recusados: um modo negativo nunca pode
     * entrar por engano e um bit novo nunca pode ser silenciosamente ignorado. */
    if ((Io->Flags & ~(BC250DBG_SDMA_FLAG_NO_GCR | BC250DBG_SDMA_FLAG_IGNORE_SUSPECT |
                       BC250DBG_SDMA_NEGATIVE_MASK)) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Engine marcada como suspeita nao recebe trabalho novo.
     *
     * Uma engine que anuncia conclusao com fence fisica e nao produz o dado ja
     * demonstrou que seu proprio sinal de sucesso nao vale. Aceitar submissao
     * depois disso e convidar resultado fabricado. O laboratorio pode
     * contornar explicitamente, para medir o dano; producao nao deve. */
    if (g_Dev.SdmaEngineSuspect &&
        (Io->Flags & BC250DBG_SDMA_FLAG_IGNORE_SUSPECT) == 0) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                  "submissao recusada: engine suspeita (motivo %u) nesta sessao",
                  g_Dev.SdmaSuspectReason);
        Io->EngineSuspect = 1;
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    negatives = Io->Flags & BC250DBG_SDMA_NEGATIVE_MASK;
    /* Um modo negativo por vez. Combinados, o resultado deixa de identificar
     * qual condicao removida causou a falha. */
    if (negatives != 0 && (negatives & (negatives - 1)) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    /* O primeiro gate usa um único dword reservado, incluído no snapshot do
     * ring. Isso evita aceitar um endereço arbitrário como fence. */
    if (Io->FenceOffset == 0) Io->FenceOffset = BC250_SDMA_FENCE_VRAM_DEFAULT;
    if (Io->FenceOffset != BC250_SDMA_FENCE_VRAM_DEFAULT ||
        (Io->FenceOffset & 3u) != 0 ||
        (ULONGLONG)Io->FenceOffset + 4 > g_Dev.VramLength) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Io->FenceExpected == 0) {
        Io->FenceExpected = 0xBC250001u ^ g_Dev.SdmaWptr;
    }
    if (Io->FencePoison == 0) {
        Io->FencePoison = ~Io->FenceExpected;
    }
    if (Io->FencePoison == Io->FenceExpected) {
        return STATUS_INVALID_PARAMETER;
    }
    /* O valor emitido no pacote e o que a GPU realmente escreve. Em operacao
     * normal ele e igual ao esperado; em BAD_FENCE ele diverge de proposito,
     * e o gate de fence tem obrigacao de reprovar. */
    Io->FenceEmitted = Io->FenceExpected;
    if (negatives & BC250DBG_SDMA_FLAG_BAD_FENCE) {
        Io->FenceEmitted = Io->FenceExpected ^ BC250DBG_SDMA_BAD_FENCE_XOR;
        if (Io->FenceEmitted == Io->FencePoison) {
            Io->FenceEmitted ^= 1u;
        }
        if (Io->FenceEmitted == Io->FenceExpected ||
            Io->FenceEmitted == Io->FencePoison) {
            return STATUS_INVALID_PARAMETER;
        }
    }
    fenceGpu = BC250_VRAM_GPU_BASE + (ULONGLONG)Io->FenceOffset;

    /* Alinhamento de dword e limites da janela de VRAM. Nao validamos contra
     * BAR0 (256 MB) e sim contra a VRAM inteira (512 MB): a SDMA alcanca tudo
     * que esta na abertura, mesmo o que nao conseguimos ler pelo BAR0. */
    if ((Io->DstOffset & 3u) != 0 || (Io->SrcOffset & 3u) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Io->AddrMode == BC250DBG_ADDR_RAW) {
        /* Endereco cru: nada e somado. RawDstGpu tem 64 bits e vence quando
         * preenchido, porque o passo 5 precisa de um VA acima de 4 GB — abaixo
         * disso todo endereco e memoria de sistema real, e escrever la com
         * traducao desligada corromperia o host. */
        dstGpu = Io->RawDstGpu ? Io->RawDstGpu : (ULONGLONG)Io->DstOffset;
        srcGpu = Io->RawSrcGpu ? Io->RawSrcGpu : (ULONGLONG)Io->SrcOffset;
    } else {
        dstGpu = BC250_VRAM_GPU_BASE + (ULONGLONG)Io->DstOffset;
        srcGpu = BC250_VRAM_GPU_BASE + (ULONGLONG)Io->SrcOffset;
    }

    Io->StatusRegBefore = BcRegReadRaw(A_SDMA0_STATUS_REG);
    Io->WptrRegBefore = BcRegReadRaw(A_SDMA0_GFX_RB_WPTR);
    Io->F32PcBefore = BcSdmaF32Pc();
    Io->GfxMiscBefore = BcRegReadRaw(BC250_REG_SMUIO_GFX_MISC_CNTL);
    Io->GfxoffBefore = (Io->GfxMiscBefore & BC250_GFXOFF_STATUS_MASK)
                     >> BC250_GFXOFF_STATUS_SHIFT;
    Io->GrbmBefore = BcRegReadRaw(BC250_REG_GRBM_STATUS);
    Io->ClkCtrlBefore = BcRegReadRaw(A_SDMA0_CLK_CTRL);
    BcVramWriteRaw(Io->FenceOffset, Io->FencePoison);
    KeMemoryBarrier();
    if (BcVramReadRaw(Io->FenceOffset) != Io->FencePoison) {
        return STATUS_DEVICE_DATA_ERROR;
    }

    if (negatives & BC250DBG_SDMA_FLAG_BAD_OPCODE) {
        /* Pacote deliberadamente invalido: apenas o header com um opcode
         * reservado, sem payload, sem GCR e SEM pacote FENCE.
         *
         * Sem payload nenhum endereco arbitrario chega a engine, entao um
         * decode errado nao pode virar escrita em VRAM. A ausencia do pacote
         * FENCE e o ponto do teste: o RPTR ate pode andar, mas a fence tem de
         * continuar com o poison e Completed tem de ser 0. Se este caso
         * "passar", o gate de conclusao esta fabricado. */
        pkt[n++] = SDMA_PKT_HEADER(BC250_SDMA_OP_RESERVED_BAD, 0);
        Io->FenceEmitted = 0;
        BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_SDMA,
                  "SDMA: caso NEGATIVO opcode reservado 0x%X; sucesso e proibido",
                  BC250_SDMA_OP_RESERVED_BAD);
        goto submit;
    }

    switch (Io->Op) {
    case BC250DBG_SDMA_OP_FILL:
        if (Io->ByteCount == 0 || Io->ByteCount > BC250_SDMA_MAX_XFER_BYTES ||
            (Io->ByteCount & 3u) != 0 ||
            (ULONGLONG)Io->DstOffset + Io->ByteCount > g_Dev.VramLength) {
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
            (ULONGLONG)Io->DstOffset + Io->ByteCount > g_Dev.VramLength ||
            (ULONGLONG)Io->SrcOffset + Io->ByteCount > g_Dev.VramLength) {
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
        if (Io->DwordCount == 0 || Io->DwordCount > BC250DBG_SDMA_WRITE_MAX_DW) {
            return STATUS_INVALID_PARAMETER;
        }
        /* A janela de VRAM so vale como limite quando o endereco E um offset de
         * VRAM. Em modo cru o endereco pode ser um VA que nada tem a ver com a
         * janela — e a garantia de que o dado foi para o lugar certo passa a ser
         * a conferencia em VerifyVramOffset, nao a aritmetica de faixa. */
        if (Io->AddrMode != BC250DBG_ADDR_RAW &&
            (ULONGLONG)Io->DstOffset + Io->DwordCount * 4 > g_Dev.VramLength) {
            return STATUS_INVALID_PARAMETER;
        }
        if (Io->AddrMode == BC250DBG_ADDR_RAW && !Io->VerifyVramValid) {
            /* Sem lugar para conferir, um WRITE cru nao pode provar nada, e a
             * ABI 16 nao deixa concluir sem dado conferido. Recusar aqui e mais
             * honesto que submeter e reportar 'nao concluido'. */
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                      "SDMA WRITE cru sem VerifyVramOffset: recusado. Sem lugar "
                      "onde conferir o dado, a submissao nao prova nada.");
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

    /* Layout oficial Navi10/SDMA5: header OP=5, MTYPE=3 nos bits 16..18,
     * endereço de 64 bits e sequência de 32 bits. A CPU só escreve o poison;
     * o valor esperado aparece exclusivamente neste pacote. */
    if (n + 4 > 64) {
        return STATUS_BUFFER_OVERFLOW;
    }
    pkt[n++] = SDMA_PKT_HEADER(BC250_SDMA_OP_FENCE, 0)
             | (BC250_SDMA_FENCE_MTYPE_UC << 16);
    pkt[n++] = (ULONG)(fenceGpu & 0xFFFFFFFFull);
    pkt[n++] = (ULONG)(fenceGpu >> 32);
    pkt[n++] = Io->FenceEmitted;

submit:
    status = BcSdmaSubmit(pkt, n, Io->Flags, &rptrBefore, &wptr, &rptrAfter, Io);

    BcHdpReadCacheInvalidate();
    KeMemoryBarrier();
    Io->FenceObserved = BcVramReadRaw(Io->FenceOffset);
    Io->FenceReached = (Io->FenceObserved == Io->FenceExpected) ? 1u : 0u;
    Io->StatusRegAfter = BcRegReadRaw(A_SDMA0_STATUS_REG);
    Io->F32PcAfter = BcSdmaF32Pc();
    Io->GfxMiscAfter = BcRegReadRaw(BC250_REG_SMUIO_GFX_MISC_CNTL);
    Io->GfxoffAfter = (Io->GfxMiscAfter & BC250_GFXOFF_STATUS_MASK)
                    >> BC250_GFXOFF_STATUS_SHIFT;
    Io->GrbmAfter = BcRegReadRaw(BC250_REG_GRBM_STATUS);
    Io->ClkCtrlAfter = BcRegReadRaw(A_SDMA0_CLK_CTRL);
    Io->WptrRegAfter = BcRegReadRaw(A_SDMA0_GFX_RB_WPTR);
    Io->RptrRegAfter = BcRegReadRaw(A_SDMA0_GFX_RB_RPTR);
    Io->RptrWbAfter = BcVramReadRaw(BC250_SDMA_RPTR_WB_OFFSET);
    Io->WptrShadowAfter = BcVramReadRaw(BC250_SDMA_WPTR_SHADOW_OFFSET);

    /* Conferencia do resultado, no kernel.
     *
     * Ate a ABI 9 o criterio de conclusao era RPTR + fence, e a comparacao dos
     * dwords ficava no CLI. Em 2026-08-23 08:31 a engine passou nesses dois e
     * nao escreveu o dado: o driver anunciou Completed=1 indevidamente. Agora a
     * comparacao acontece aqui, e sem ela nao existe conclusao. */
    if (Io->Op == BC250DBG_SDMA_OP_WRITE) {
        /* Onde conferir. Em operacao normal e o proprio offset do pacote. No
         * passo 5 do gate de memoria o endereco do pacote e um VA que a page
         * table resolve para OUTRO lugar, e conferir no offset do pacote leria
         * o lugar errado — a submissao correta apareceria como divergencia.
         *
         * ABI 16: VerifyVramValid diz explicitamente onde olhar. Nao existe
         * caminho implicito, e uma submissao WRITE que nao conferiu dado nenhum
         * nao pode declarar conclusao (ver abaixo). */
        ULONG verifyAt = Io->DstOffset;
        BOOLEAN canVerify;

        if (Io->VerifyVramValid) {
            verifyAt = Io->VerifyVramOffset;
            canVerify = TRUE;
        } else {
            canVerify = (Io->AddrMode != BC250DBG_ADDR_RAW);
        }
        if (canVerify && (verifyAt & 3u) == 0 &&
            (ULONGLONG)verifyAt + (ULONGLONG)Io->DwordCount * 4 <= g_Dev.VramLength) {
            for (i = 0; i < Io->DwordCount; i++) {
                ULONG got = BcVramReadRaw(verifyAt + i * sizeof(ULONG));
                Io->DataVerifiedDwords++;
                if (got != Io->Data[i]) {
                    Io->DataMismatches++;
                }
            }
        }
    }

    Io->RptrBefore = rptrBefore;
    Io->Wptr       = wptr;
    Io->RptrAfter  = rptrAfter;
    /* O gate de conclusao nao muda nos casos negativos: continua exigindo
     * RPTR igual ao WPTR publicado E a fence escrita pela GPU com o valor
     * que a CPU esperava. Em NO_KICK o WPTR sequer foi publicado, entao
     * comparamos contra o ponteiro que continua valendo no silicio. */
    if ((Io->Flags & BC250DBG_SDMA_FLAG_NO_KICK) != 0) {
        Io->Completed = 0u;
    } else {
        Io->Completed = (rptrAfter == wptr && Io->FenceReached &&
                         Io->DataMismatches == 0) ? 1u : 0u;
        /* ABI 16: WRITE sem dado conferido NAO conclui.
         *
         * Antes desta linha, uma submissao WRITE em modo de endereco cru
         * pulava a conferencia e voltava a decidir conclusao por ponteiro +
         * fence — exatamente o criterio que mentiu em 2026-08-23 08:31, quando
         * a engine escreveu a fence correta sem tocar no destino. O passo 5
         * usa endereco cru por necessidade, e sem esta regra ele poderia
         * declarar traducao funcionando sem ter olhado um unico dword. */
        if (Io->Op == BC250DBG_SDMA_OP_WRITE && Io->DataVerifiedDwords == 0) {
            Io->Completed = 0u;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                      "SDMA WRITE sem dado conferido: conclusao RECUSADA. "
                      "Em modo de endereco cru informe VerifyVramOffset.");
        }
    }

    /* Assinatura da engine que mente: fence fisica escrita com o valor certo,
     * ponteiros corretos, e o dado nao mudou. Nao e caso negativo nenhum, e
     * medimos que essa condicao nao se desfaz dentro da sessao. */
    if (negatives == 0 && Io->FenceReached && Io->DataMismatches > 0) {
        if (!g_Dev.SdmaEngineSuspect) {
            g_Dev.SdmaEngineSuspect = TRUE;
            g_Dev.SdmaSuspectReason = BC250DBG_SUSPECT_FENCE_NO_DATA;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_SDMA,
                      "ENGINE SUSPEITA: fence 0x%X alcancada e %u de %u dwords "
                      "divergentes; conclusao recusada",
                      Io->FenceObserved, Io->DataMismatches, Io->DataVerifiedDwords);
        }
    }
    /* O opcode reservado ja demonstrou, em cinco boots, deixar a engine sem
     * caminho de volta por software. Marcar na hora e mais honesto que esperar
     * a proxima submissao mentir. */
    if ((negatives & BC250DBG_SDMA_FLAG_BAD_OPCODE) != 0 && !g_Dev.SdmaEngineSuspect) {
        g_Dev.SdmaEngineSuspect = TRUE;
        g_Dev.SdmaSuspectReason = BC250DBG_SUSPECT_BAD_OPCODE;
        BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_SDMA,
                  "ENGINE SUSPEITA: opcode reservado submetido nesta sessao");
    }
    Io->EngineSuspect = g_Dev.SdmaEngineSuspect ? 1u : 0u;
    Io->StatusReg  = Io->StatusRegAfter;

    BcLogText(negatives ? BC250DBG_LVL_WARN : BC250DBG_LVL_INFO, BC250DBG_SUB_SDMA,
              "SDMA op %u flags 0x%X: dst 0x%llx, %u dw, RPTR 0x%X -> 0x%X, "
              "fence 0x%X -> 0x%X (%s)",
              Io->Op, Io->Flags, dstGpu, n, rptrBefore, rptrAfter,
              Io->FencePoison, Io->FenceObserved,
              Io->Completed ? "concluido"
                            : (negatives ? "negativo reprovado como esperado"
                                         : "TIMEOUT"));

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

    status = BcSdmaSubmit((const ULONG *)Io->Data, Io->DwordCount, 0,
                          &rptrBefore, &wptr, &rptrAfter, NULL);

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

/* Registradores de traducao que o passo 3 do plano vai modificar, e portanto os
 * unicos que o snapshot precisa cobrir.
 *
 * Criterio de entrada: e configuracao, e o upstream escreve, e reescrever o
 * mesmo valor nao dispara acao. Ficam FORA:
 *   GCVM_INVALIDATE_ENG0_REQ  - escrever dispara invalidacao;
 *   GCVM_INVALIDATE_ENG0_ACK  - status;
 *   GCVM_INVALIDATE_ENG0_SEM  - semaforo, tem protocolo proprio;
 *   GCVM_L2_PROTECTION_FAULT_STATUS e _ADDR - reportam falha, nao configuram;
 *   GCMC_VM_FB_LOCATION_BASE/TOP e FB_OFFSET - definem onde a VRAM aparece;
 *     mexer neles nao faz parte de montar VMID 0 e o risco nao se justifica. */
static const ULONG g_GcvmTxnRegs[BC250DBG_GCVM_TXN_REG_COUNT] = {
    A_GCVM_L2_CNTL,
    A_GCVM_L2_CNTL2,
    A_GCVM_L2_CNTL3,
    A_GCVM_L2_CNTL4,
    A_GCVM_L2_CNTL5,
    A_GCVM_CONTEXT0_CNTL,
    A_GCVM_CONTEXT1_CNTL,
    A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO,
    A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI,
    A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO,
    A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI,
    A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO,
    A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI,
    A_GCVM_L2_PROTECTION_FAULT_CNTL,
    A_GCVM_L2_PROTECTION_FAULT_CNTL2,
    A_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_LO,
    A_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_HI,
    A_GCMC_VM_MX_L1_TLB_CNTL,
    A_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR,
    A_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR,
    A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
    A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
    A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO,
    A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO,
    /* Acrescentados na ABI 15, quando o passo 3 passou a escrever de verdade.
     * A regra e simples e o kernel a impoe: um estagio nao escreve registrador
     * que o snapshot nao cobre. Sem isto, uma escrita nova escaparia do
     * rollback sem ninguem notar ate o rollback nao funcionar. */
    A_GCMC_VM_AGP_BASE,
    A_GCMC_VM_AGP_BOT,
    A_GCMC_VM_AGP_TOP,
    A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI,
    A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI,
    A_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO,
    A_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI,
    A_GCVM_INVALIDATE_ENG0_ADDR_RANGE_LO,
    A_GCVM_INVALIDATE_ENG0_ADDR_RANGE_HI,
    /* ABI 16: a engine 17 e a da GART. A leitura do upstream mostrou que o
     * plano original estava mirando a engine errada. */
    A_GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO,
    A_GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI
};

C_ASSERT(RTL_NUMBER_OF(g_GcvmTxnRegs) == BC250DBG_GCVM_TXN_REG_COUNT);

/* Libera o semaforo da engine de invalidacao do GFXHUB.
 *
 * Metade que faltava do protocolo: no upstream, ler o registrador adquire e
 * escrever 0 libera (gmc_v10_0.c, comentarios literais). Enquanto o dump lia
 * SEM, o laboratorio adquiria o semaforo e o mantinha ate o reboot, sem saber.
 *
 * O estado e observado pelo bit SEMAPHORE (16) de ACK, que nao tem efeito
 * colateral de leitura. */
#define F_GCVM_INVALIDATE_ACK__SEMAPHORE 0x00010000u

NTSTATUS BcGcvmSemaphoreRelease(BC250DBG_GCVM_SEM_IO *Io)
{
    if (Io == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Io, sizeof(*Io));
    Io->Size = sizeof(*Io);

    if (g_Dev.Mmio == NULL) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_ACCESS_DENIED;
    }

    Io->AckBefore = BcRegReadRaw(A_GCVM_INVALIDATE_ENG0_ACK);
    Io->HeldBefore = (Io->AckBefore & F_GCVM_INVALIDATE_ACK__SEMAPHORE) ? 1u : 0u;

    BcRegWriteRaw(A_GCVM_INVALIDATE_ENG0_SEM, 0);
    KeStallExecutionProcessor(10);

    Io->AckAfter = BcRegReadRaw(A_GCVM_INVALIDATE_ENG0_ACK);
    Io->HeldAfter = (Io->AckAfter & F_GCVM_INVALIDATE_ACK__SEMAPHORE) ? 1u : 0u;
    Io->Status = BC250DBG_OPS_OK;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
              "GCVM semaforo liberado: ACK 0x%08X -> 0x%08X (retido %u -> %u)",
              Io->AckBefore, Io->AckAfter, Io->HeldBefore, Io->HeldAfter);
    return STATUS_SUCCESS;
}

/* Snapshot, restauracao e conferencia dos registradores de traducao.
 *
 * O RESTORE reescreve os mesmos valores lidos. Nao e redundancia: e a unica
 * forma de provar o caminho de volta ANTES de existir divergencia para desfazer.
 * Se escrever o valor identico tivesse efeito colateral em algum destes
 * registradores, o ensaio descobre agora, com o bypass ainda intacto, e nao
 * depois de a placa ja estar dependendo da restauracao. */
NTSTATUS BcGcvmTransaction(BC250DBG_GCVM_TXN_IO *Io)
{
    ULONG i;

    if (Io == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Dev.Mmio == NULL) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_DEVICE_NOT_READY;
    }

    Io->RegisterCount = BC250DBG_GCVM_TXN_REG_COUNT;
    for (i = 0; i < BC250DBG_GCVM_TXN_REG_COUNT; i++) {
        Io->Offsets[i] = g_GcvmTxnRegs[i];
    }

    switch (Io->Mode) {
    case BC250DBG_GCVM_TXN_SNAPSHOT:
        /* Leitura pura: nao exige armamento de escrita. */
        for (i = 0; i < BC250DBG_GCVM_TXN_REG_COUNT; i++) {
            g_Dev.GcvmTxnValues[i] = BcRegReadRaw(g_GcvmTxnRegs[i]);
            Io->Values[i] = g_Dev.GcvmTxnValues[i];
        }
        g_Dev.GcvmTxnValid = TRUE;
        Io->SnapshotPresent = 1;
        Io->Status = BC250DBG_OPS_OK;
        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
                  "GCVM snapshot: %u registradores guardados",
                  BC250DBG_GCVM_TXN_REG_COUNT);
        return STATUS_SUCCESS;

    case BC250DBG_GCVM_TXN_VERIFY:
        if (!g_Dev.GcvmTxnValid) {
            Io->SnapshotPresent = 0;
            Io->Status = BC250DBG_OPS_BADOP;
            return STATUS_INVALID_DEVICE_STATE;
        }
        Io->SnapshotPresent = 1;
        for (i = 0; i < BC250DBG_GCVM_TXN_REG_COUNT; i++) {
            ULONG got = BcRegReadRaw(g_GcvmTxnRegs[i]);
            Io->Values[i] = got;
            if (got != g_Dev.GcvmTxnValues[i]) {
                if (Io->BadCount < BC250DBG_GCVM_TXN_BAD_SAMPLES) {
                    Io->BadOffset[Io->BadCount] = g_GcvmTxnRegs[i];
                    Io->BadGot[Io->BadCount] = got;
                    Io->BadWant[Io->BadCount] = g_Dev.GcvmTxnValues[i];
                    Io->BadCount++;
                }
                Io->Mismatches++;
            }
        }
        Io->Status = (Io->Mismatches == 0) ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;
        return (Io->Mismatches == 0) ? STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;

    case BC250DBG_GCVM_TXN_RESTORE:
        if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
            Io->Status = BC250DBG_OPS_DENIED;
            return STATUS_ACCESS_DENIED;
        }
        if (!g_Dev.GcvmTxnValid) {
            Io->SnapshotPresent = 0;
            Io->Status = BC250DBG_OPS_BADOP;
            return STATUS_INVALID_DEVICE_STATE;
        }
        Io->SnapshotPresent = 1;
        for (i = 0; i < BC250DBG_GCVM_TXN_REG_COUNT; i++) {
            ULONG got;

            BcRegWriteRaw(g_GcvmTxnRegs[i], g_Dev.GcvmTxnValues[i]);
            got = BcRegReadRaw(g_GcvmTxnRegs[i]);
            Io->Values[i] = got;
            if (got == g_Dev.GcvmTxnValues[i]) {
                Io->Restored++;
            } else {
                if (Io->BadCount < BC250DBG_GCVM_TXN_BAD_SAMPLES) {
                    Io->BadOffset[Io->BadCount] = g_GcvmTxnRegs[i];
                    Io->BadGot[Io->BadCount] = got;
                    Io->BadWant[Io->BadCount] = g_Dev.GcvmTxnValues[i];
                    Io->BadCount++;
                }
                Io->Mismatches++;
                BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                          "GCVM restore: 0x%05X nao aceitou 0x%08X (leu 0x%08X)",
                          g_GcvmTxnRegs[i], g_Dev.GcvmTxnValues[i], got);
            }
        }
        Io->Status = (Io->Mismatches == 0) ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;
        BcLogText(Io->Mismatches ? BC250DBG_LVL_ERROR : BC250DBG_LVL_INFO,
                  BC250DBG_SUB_GC,
                  "GCVM restore: %u restaurados, %u divergencias",
                  Io->Restored, Io->Mismatches);
        return (Io->Mismatches == 0) ? STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;

    default:
        return STATUS_INVALID_PARAMETER;
    }
}

/* ---------------------------------------------------------------------- */
/* Passo 3 do gate de memoria: programar valores novos, em estagios         */
/* ---------------------------------------------------------------------- */

/* Campos extraidos de gc_10_1_0_sh_mask_extract.h, que por sua vez sai do
 * header autoritativo do kernel (SHA-256 registrado no cabecalho do extrato).
 * Nenhum valor abaixo foi deduzido de outra geracao de hardware. */
#define F_L2_CNTL__ENABLE_L2_CACHE                0x00000001u
#define F_L2_CNTL__ENABLE_L2_FRAGMENT_PROCESSING  0x00000002u
#define F_L2_CNTL__L2_PDE0_CACHE_TAG_GEN_MODE     0x00000100u
#define F_L2_CNTL__ENABLE_DEFAULT_PAGE_OUT_TO_SYS 0x00000800u
#define F_L2_CNTL__PDE_FAULT_CLASSIFICATION       0x00040000u
#define F_L2_CNTL__CONTEXT1_IDENTITY_ACCESS_MODE  0x00180000u
#define S_L2_CNTL__CONTEXT1_IDENTITY_ACCESS_MODE  19
#define F_L2_CNTL__IDENTITY_MODE_FRAGMENT_SIZE    0x03E00000u
#define S_L2_CNTL__IDENTITY_MODE_FRAGMENT_SIZE    21

#define F_L2_CNTL2__INVALIDATE_ALL_L1_TLBS 0x00000001u
#define F_L2_CNTL2__INVALIDATE_L2_CACHE    0x00000002u

#define F_L2_CNTL3__BANK_SELECT                0x0000003Fu
#define S_L2_CNTL3__BANK_SELECT                0
#define F_L2_CNTL3__L2_CACHE_BIGK_FRAGMENT_SZ  0x000F8000u
#define S_L2_CNTL3__L2_CACHE_BIGK_FRAGMENT_SZ  15

#define F_L2_CNTL4__VMC_TAP_PDE_REQUEST_PHYSICAL 0x00000040u
#define F_L2_CNTL4__VMC_TAP_PTE_REQUEST_PHYSICAL 0x00000080u

#define F_L2_CNTL5__L2_CACHE_SMALLK_FRAGMENT_SZ 0x0000001Fu
#define S_L2_CNTL5__L2_CACHE_SMALLK_FRAGMENT_SZ 0

#define F_FAULT_CNTL2__ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY 0x00040000u

#define F_TLB_CNTL__ENABLE_L1_TLB               0x00000001u
#define F_TLB_CNTL__SYSTEM_ACCESS_MODE          0x00000018u
#define S_TLB_CNTL__SYSTEM_ACCESS_MODE          3
#define F_TLB_CNTL__SYSTEM_APERTURE_UNMAPPED    0x00000020u
#define F_TLB_CNTL__ENABLE_ADVANCED_DRIVER_MODEL 0x00000040u
#define F_TLB_CNTL__MTYPE                       0x00003800u
#define S_TLB_CNTL__MTYPE                       11
#define V_TLB_CNTL__MTYPE_UC                    3u

#define F_CONTEXT0_CNTL__ENABLE_CONTEXT     0x00000001u
#define F_CONTEXT0_CNTL__PAGE_TABLE_DEPTH   0x00000006u
#define S_CONTEXT0_CNTL__PAGE_TABLE_DEPTH   1
#define F_CONTEXT0_CNTL__RETRY_PERM_OR_INVALID_PF 0x00000080u

#define F_FB_LOCATION__FB_BASE 0x00FFFFFFu
#define F_FB_LOCATION__FB_TOP  0x00FFFFFFu

#define BC_SETF(v, mask, shift, val) \
    ((((ULONG)(v)) & ~(ULONG)(mask)) | ((((ULONG)(val)) << (shift)) & (ULONG)(mask)))

typedef struct _BC_GCVM_PROG_WRITE {
    ULONG Offset;
    ULONG Value;
    /* Bits que a leitura de volta nao pode confirmar porque a escrita neles e
     * um gatilho: o hardware os limpa depois de agir. Zero para tudo que e
     * configuracao de verdade, que e a maioria esmagadora. */
    ULONG IgnoreMask;
} BC_GCVM_PROG_WRITE;

/* Empilha uma escrita no plano, com limite conferido ANTES de escrever no
 * vetor. Um NT_ASSERT depois de preencher chegaria tarde: a pilha ja estaria
 * corrompida. Estouro marca o plano como invalido e o estagio e recusado. */
#define BC_PROG_PUSH_M(off, val, ignore)                             \
    do {                                                             \
        if (planCount >= BC250DBG_GCVM_PROG_MAX_WRITES) {            \
            planOverflow = TRUE;                                     \
        } else {                                                     \
            plan[planCount].Offset     = (off);                      \
            plan[planCount].Value      = (ULONG)(val);               \
            plan[planCount].IgnoreMask = (ULONG)(ignore);            \
            planCount++;                                             \
        }                                                            \
    } while (0)

/* Escrita normal: todo bit tem de voltar na leitura. */
#define BC_PROG_PUSH(off, val) BC_PROG_PUSH_M(off, val, 0u)

/* O registrador esta coberto pelo snapshot da transacao?
 *
 * Nao e formalidade. Um estagio que escreve fora da lista de propriedade da
 * transacao produz uma escrita que o rollback nao desfaz, e o sintoma aparece
 * como "rollback limpo" com hardware alterado. Fail-closed: se um unico
 * registrador do plano nao estiver coberto, o estagio nao escreve nada. */
static BOOLEAN BcGcvmProgIsCovered(ULONG Offset)
{
    ULONG i;
    for (i = 0; i < BC250DBG_GCVM_TXN_REG_COUNT; i++) {
        if (g_GcvmTxnRegs[i] == Offset) {
            return TRUE;
        }
    }
    return FALSE;
}

NTSTATUS BcGcvmProgram(BC250DBG_GCVM_PROGRAM_IO *Io)
{
    BC_GCVM_PROG_WRITE plan[BC250DBG_GCVM_PROG_MAX_WRITES];
    ULONG planCount = 0;
    BOOLEAN planOverflow = FALSE;
    ULONG i;
    ULONG stage, tmp;
    ULONGLONG fbStart, fbEnd;
    NTSTATUS status;

    if (Io == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    stage = Io->Stage;
    RtlZeroMemory((PUCHAR)Io + FIELD_OFFSET(BC250DBG_GCVM_PROGRAM_IO, FbOffsetReg),
                  sizeof(*Io) - FIELD_OFFSET(BC250DBG_GCVM_PROGRAM_IO, FbOffsetReg));
    Io->Size = sizeof(*Io);
    Io->DenyReason = BC250DBG_GCVM_PROG_DENY_NONE;

    if (g_Dev.Mmio == NULL) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_ACCESS_DENIED;
    }

    /* REGRA 1: sem caminho de volta provado, nada e escrito.
     *
     * Foi assim que o 'sdma enginereset' errou em 2026-08-23: mexeu no estado
     * sem verificar se havia snapshot aberto, e cinco execucoes mediram
     * recuperacao sobre contabilidade propria em vez de hardware. */
    if (!g_Dev.GcvmTxnValid) {
        Io->DenyReason = BC250DBG_GCVM_PROG_DENY_NO_TXN;
        Io->Status = BC250DBG_OPS_BADOP;
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "GCVM program: RECUSADO, nenhum snapshot de transacao aberto. "
                  "Rode 'gcvm txn -snapshot' antes: sem caminho de volta "
                  "provado nao ha escrita em registrador de traducao.");
        return STATUS_SUCCESS;
    }

    /* REGRA 4: os estagios que substituem o bypass do POST exigem consentimento
     * explicito. TLB, CACHE e CONTEXT0 sao os que trocam o caminho de memoria
     * do qual todo o sucesso medido ate hoje depende. */
    if (stage == BC250DBG_GCVM_PROG_STAGE_TLB ||
        stage == BC250DBG_GCVM_PROG_STAGE_CACHE ||
        stage == BC250DBG_GCVM_PROG_STAGE_CONTEXT0) {
        if ((Io->Flags & BC250DBG_GCVM_PROG_FLAG_DANGER) == 0) {
            Io->DenyReason = BC250DBG_GCVM_PROG_DENY_NEED_FLAG;
            Io->Status = BC250DBG_OPS_DENIED;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "GCVM program: estagio %u substitui o bypass do POST e "
                      "exige o flag explicito de risco", stage);
            return STATUS_SUCCESS;
        }
    }

    /* Contexto lido do silicio, nunca de constante. */
    status = BcVramMap();
    if (!NT_SUCCESS(status)) {
        Io->Status = BC250DBG_OPS_DENIED;
        return status;
    }
    Io->FbOffsetReg    = BcRegReadRaw(A_GCMC_VM_FB_OFFSET);
    Io->VramBaseOffset = (ULONGLONG)Io->FbOffsetReg << 24;
    Io->FbLocationBase = BcRegReadRaw(A_GCMC_VM_FB_LOCATION_BASE);
    Io->FbLocationTop  = BcRegReadRaw(A_GCMC_VM_FB_LOCATION_TOP);
    Io->PtBasePa = (ULONGLONG)Io->TableVramOffset + Io->VramBaseOffset;
    Io->DummyPa  = (ULONGLONG)Io->DummyVramOffset + Io->VramBaseOffset;

    /* ------------------ montagem do plano, por estagio ------------------ */
    switch (stage) {

    /* gfxhub_v2_0_init_gart_aperture_regs. Inerte enquanto CONTEXT0_CNTL
     * mantiver ENABLE_CONTEXT = 0: aponta para a tabela, sem ligar nada. */
    case BC250DBG_GCVM_PROG_STAGE_GART:
        if ((Io->TableVramOffset & 0xFFFu) != 0 ||
            (ULONGLONG)Io->TableVramOffset >= g_Dev.VramLength) {
            Io->DenyReason = BC250DBG_GCVM_PROG_DENY_BAD_ARG;
            Io->Status = BC250DBG_OPS_RANGE;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "GCVM program gart: tabela 0x%X invalida "
                      "(alinhamento 4 KB, janela 0x%X)",
                      Io->TableVramOffset, g_Dev.VramLength);
            return STATUS_SUCCESS;
        }
        if (Io->PtEndVa <= Io->PtStartVa) {
            Io->DenyReason = BC250DBG_GCVM_PROG_DENY_BAD_ARG;
            Io->Status = BC250DBG_OPS_RANGE;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "GCVM program gart: faixa vazia (start 0x%llX >= end 0x%llX)",
                      Io->PtStartVa, Io->PtEndVa);
            return STATUS_SUCCESS;
        }
        BC_PROG_PUSH(A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO, (ULONG)(Io->PtBasePa & 0xFFFFFFFFull));
        BC_PROG_PUSH(A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI, (ULONG)(Io->PtBasePa >> 32));
        BC_PROG_PUSH(A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO, (ULONG)(Io->PtStartVa >> 12));
        BC_PROG_PUSH(A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI, (ULONG)(Io->PtStartVa >> 44));
        BC_PROG_PUSH(A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO, (ULONG)(Io->PtEndVa >> 12));
        BC_PROG_PUSH(A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI, (ULONG)(Io->PtEndVa >> 44));
        break;

    /* gfxhub_v2_0_init_system_aperture_regs.
     *
     * AGP: o upstream escreve base 0, BOT = agp_start >> 24, TOP = agp_end >> 24.
     * Este laboratorio nao tem abertura AGP, e a convencao do amdgpu para
     * "vazia" e BOT > TOP. O POST deixou os tres em zero, o que descreve uma
     * abertura de uma pagina em zero em vez de nenhuma. Escrevemos a vazia
     * explicita: BOT = 0x00FFFFFF, TOP = 0.
     *
     * Abertura de sistema: derivada de FB_LOCATION_BASE/TOP LIDOS agora, nao da
     * constante BC250_VRAM_GPU_BASE. */
    case BC250DBG_GCVM_PROG_STAGE_SYSTEM:
        if ((Io->DummyVramOffset & 0xFFFu) != 0 ||
            (ULONGLONG)Io->DummyVramOffset + 4096ull > g_Dev.VramLength) {
            Io->DenyReason = BC250DBG_GCVM_PROG_DENY_BAD_ARG;
            Io->Status = BC250DBG_OPS_RANGE;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "GCVM program system: pagina de falha 0x%X invalida "
                      "(alinhamento 4 KB, janela 0x%X)",
                      Io->DummyVramOffset, g_Dev.VramLength);
            return STATUS_SUCCESS;
        }
        fbStart = (ULONGLONG)(Io->FbLocationBase & F_FB_LOCATION__FB_BASE) << 24;
        fbEnd   = ((ULONGLONG)(Io->FbLocationTop & F_FB_LOCATION__FB_TOP) << 24) |
                  0xFFFFFFull;

        BC_PROG_PUSH(A_GCMC_VM_AGP_BASE, 0);
        BC_PROG_PUSH(A_GCMC_VM_AGP_BOT, 0x00FFFFFFu);
        BC_PROG_PUSH(A_GCMC_VM_AGP_TOP, 0);
        BC_PROG_PUSH(A_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR, (ULONG)(fbStart >> 18));
        BC_PROG_PUSH(A_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR, (ULONG)(fbEnd >> 18));
        BC_PROG_PUSH(A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB, (ULONG)(Io->DummyPa >> 12));
        BC_PROG_PUSH(A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB, (ULONG)(Io->DummyPa >> 44));
        BC_PROG_PUSH(A_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_LO, (ULONG)(Io->DummyPa >> 12));
        BC_PROG_PUSH(A_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_HI, (ULONG)(Io->DummyPa >> 44));
        tmp = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_CNTL2);
        tmp |= F_FAULT_CNTL2__ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY;
        BC_PROG_PUSH(A_GCVM_L2_PROTECTION_FAULT_CNTL2, tmp);
        break;

    /* gfxhub_v2_0_init_tlb_regs. PERIGOSO: SYSTEM_ACCESS_MODE sai de 0, que e
     * o bypass fisico deixado pelo POST, e vai para 3. */
    case BC250DBG_GCVM_PROG_STAGE_TLB:
        tmp = BcRegReadRaw(A_GCMC_VM_MX_L1_TLB_CNTL);
        tmp = BC_SETF(tmp, F_TLB_CNTL__ENABLE_L1_TLB, 0, 1);
        tmp = BC_SETF(tmp, F_TLB_CNTL__SYSTEM_ACCESS_MODE,
                      S_TLB_CNTL__SYSTEM_ACCESS_MODE, 3);
        tmp = BC_SETF(tmp, F_TLB_CNTL__ENABLE_ADVANCED_DRIVER_MODEL, 6, 1);
        tmp = BC_SETF(tmp, F_TLB_CNTL__SYSTEM_APERTURE_UNMAPPED, 5, 0);
        tmp = BC_SETF(tmp, F_TLB_CNTL__MTYPE, S_TLB_CNTL__MTYPE,
                      V_TLB_CNTL__MTYPE_UC);
        BC_PROG_PUSH(A_GCMC_VM_MX_L1_TLB_CNTL, tmp);
        break;

    /* gfxhub_v2_0_init_cache_regs. PERIGOSO: VMC_TAP_PDE/PTE_REQUEST_PHYSICAL
     * saem de 1 para 0, ou seja, a busca de PDE/PTE deixa de ser fisica.
     *
     * DIVERGENCIA DECLARADA: o upstream monta CNTL3/4/5 a partir da constante
     * de reset (mmGCVM_L2_CNTLn_DEFAULT), ausente dos headers disponiveis.
     * Aqui partimos do valor do POST e alteramos apenas os campos que o
     * upstream altera. */
    case BC250DBG_GCVM_PROG_STAGE_CACHE:
        tmp = BcRegReadRaw(A_GCVM_L2_CNTL);
        tmp = BC_SETF(tmp, F_L2_CNTL__ENABLE_L2_CACHE, 0, 1);
        tmp = BC_SETF(tmp, F_L2_CNTL__ENABLE_L2_FRAGMENT_PROCESSING, 1, 0);
        tmp = BC_SETF(tmp, F_L2_CNTL__ENABLE_DEFAULT_PAGE_OUT_TO_SYS, 11, 1);
        tmp = BC_SETF(tmp, F_L2_CNTL__L2_PDE0_CACHE_TAG_GEN_MODE, 8, 0);
        tmp = BC_SETF(tmp, F_L2_CNTL__PDE_FAULT_CLASSIFICATION, 18, 0);
        tmp = BC_SETF(tmp, F_L2_CNTL__CONTEXT1_IDENTITY_ACCESS_MODE,
                      S_L2_CNTL__CONTEXT1_IDENTITY_ACCESS_MODE, 1);
        tmp = BC_SETF(tmp, F_L2_CNTL__IDENTITY_MODE_FRAGMENT_SIZE,
                      S_L2_CNTL__IDENTITY_MODE_FRAGMENT_SIZE, 0);
        BC_PROG_PUSH(A_GCVM_L2_CNTL, tmp);

        /* GCVM_L2_CNTL2 e misto: os bits 0 e 1 sao GATILHOS de invalidacao, que
         * o hardware limpa depois de agir, e os bits 21..30 sao configuracao.
         * Medido em 2026-08-23 13:40: escrever 0x3 e ler 0x0 abortou o estagio
         * indevidamente. O upstream escreve e nunca le de volta.
         *
         * A conferencia continua valendo para os bits de configuracao. Apenas
         * os dois gatilhos ficam fora, declarados na saida. */
        tmp = BcRegReadRaw(A_GCVM_L2_CNTL2);
        tmp = BC_SETF(tmp, F_L2_CNTL2__INVALIDATE_ALL_L1_TLBS, 0, 1);
        tmp = BC_SETF(tmp, F_L2_CNTL2__INVALIDATE_L2_CACHE, 1, 1);
        BC_PROG_PUSH_M(A_GCVM_L2_CNTL2, tmp,
                       F_L2_CNTL2__INVALIDATE_ALL_L1_TLBS |
                       F_L2_CNTL2__INVALIDATE_L2_CACHE);

        tmp = BcRegReadRaw(A_GCVM_L2_CNTL3);
        tmp = BC_SETF(tmp, F_L2_CNTL3__BANK_SELECT, S_L2_CNTL3__BANK_SELECT, 9);
        tmp = BC_SETF(tmp, F_L2_CNTL3__L2_CACHE_BIGK_FRAGMENT_SZ,
                      S_L2_CNTL3__L2_CACHE_BIGK_FRAGMENT_SZ, 6);
        BC_PROG_PUSH(A_GCVM_L2_CNTL3, tmp);

        tmp = BcRegReadRaw(A_GCVM_L2_CNTL4);
        tmp = BC_SETF(tmp, F_L2_CNTL4__VMC_TAP_PDE_REQUEST_PHYSICAL, 6, 0);
        tmp = BC_SETF(tmp, F_L2_CNTL4__VMC_TAP_PTE_REQUEST_PHYSICAL, 7, 0);
        BC_PROG_PUSH(A_GCVM_L2_CNTL4, tmp);

        tmp = BcRegReadRaw(A_GCVM_L2_CNTL5);
        tmp = BC_SETF(tmp, F_L2_CNTL5__L2_CACHE_SMALLK_FRAGMENT_SZ,
                      S_L2_CNTL5__L2_CACHE_SMALLK_FRAGMENT_SZ, 0);
        BC_PROG_PUSH(A_GCVM_L2_CNTL5, tmp);
        break;

    /* gfxhub_v2_0_enable_system_domain. PONTO SEM VOLTA: a partir daqui o VMID 0
     * traduz de verdade, e endereco que nao resolver pela tabela falha. */
    case BC250DBG_GCVM_PROG_STAGE_CONTEXT0:
        tmp = BcRegReadRaw(A_GCVM_CONTEXT0_CNTL);
        tmp = BC_SETF(tmp, F_CONTEXT0_CNTL__ENABLE_CONTEXT, 0, 1);
        tmp = BC_SETF(tmp, F_CONTEXT0_CNTL__PAGE_TABLE_DEPTH,
                      S_CONTEXT0_CNTL__PAGE_TABLE_DEPTH, 0);
        tmp = BC_SETF(tmp, F_CONTEXT0_CNTL__RETRY_PERM_OR_INVALID_PF, 7, 0);
        BC_PROG_PUSH(A_GCVM_CONTEXT0_CNTL, tmp);
        break;

    /* gfxhub_v2_0_disable_identity_aperture. Valores literais do upstream:
     * faixa baixa em 0xF_FFFFFFFF e faixa alta em zero, ou seja, low > high,
     * abertura vazia. Inerte enquanto CONTEXT1_IDENTITY_ACCESS_MODE nao estiver
     * em uso, e escrito antes por isso mesmo. */
    case BC250DBG_GCVM_PROG_STAGE_IDENTITY:
        BC_PROG_PUSH(A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO, 0xFFFFFFFFu);
        BC_PROG_PUSH(A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI, 0x0000000Fu);
        BC_PROG_PUSH(A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO, 0);
        BC_PROG_PUSH(A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI, 0);
        BC_PROG_PUSH(A_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO, 0);
        BC_PROG_PUSH(A_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI, 0);
        break;

    /* gfxhub_v2_0_program_invalidation, engines 0 e 17. Faixa cheia:
     * LO32 = 0xFFFFFFFF, HI32 = 0x1F. Nao configura traducao e nao dispara
     * invalidacao: quem dispara e o REQ.
     *
     * A engine 17 e a que importa. O upstream escreve a faixa das 18 engines e
     * depois usa a 17 para a GART — "Use register 17 for GART", literal em
     * gmc_v10_0_flush_gpu_tlb(). Ate a ABI 15 este estagio escrevia so a
     * engine 0, que nao e usada por ninguem. A engine 0 continua aqui porque
     * ja foi exercitada no silicio em 2026-08-23 12:59 e nao custa nada. */
    case BC250DBG_GCVM_PROG_STAGE_INVRANGE:
        BC_PROG_PUSH(A_GCVM_INVALIDATE_ENG0_ADDR_RANGE_LO, 0xFFFFFFFFu);
        BC_PROG_PUSH(A_GCVM_INVALIDATE_ENG0_ADDR_RANGE_HI, 0x0000001Fu);
        BC_PROG_PUSH(A_GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO, 0xFFFFFFFFu);
        BC_PROG_PUSH(A_GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI, 0x0000001Fu);
        break;

    default:
        Io->DenyReason = BC250DBG_GCVM_PROG_DENY_BAD_STAGE;
        Io->Status = BC250DBG_OPS_BADOP;
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "GCVM program: estagio %u nao existe", stage);
        return STATUS_SUCCESS;
    }

    if (planOverflow) {
        Io->DenyReason = BC250DBG_GCVM_PROG_DENY_BAD_STAGE;
        Io->Status = BC250DBG_OPS_BADOP;
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "GCVM program: estagio %u tem mais escritas que o limite de %u; "
                  "nada foi escrito", stage, BC250DBG_GCVM_PROG_MAX_WRITES);
        return STATUS_SUCCESS;
    }

    /* REGRA 2, antes de qualquer escrita: todo registrador do plano tem de estar
     * coberto pelo snapshot. Fail-closed no plano inteiro, nao por registrador,
     * porque um estagio aplicado pela metade e pior que um estagio recusado. */
    for (i = 0; i < planCount; i++) {
        if (!BcGcvmProgIsCovered(plan[i].Offset)) {
            Io->DenyReason = BC250DBG_GCVM_PROG_DENY_UNCOVERED;
            Io->DenyOffset = plan[i].Offset;
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "GCVM program: RECUSADO, 0x%05X nao esta na lista da "
                      "transacao; o rollback nao o desfaria", plan[i].Offset);
            return STATUS_SUCCESS;
        }
    }

    /* REGRA 3: escreve, confere, e para no primeiro que nao aceitar. */
    for (i = 0; i < planCount; i++) {
        ULONG before = BcRegReadRaw(plan[i].Offset);
        ULONG keep = ~plan[i].IgnoreMask;
        ULONG after;
        BOOLEAN ok;

        BcRegWriteRaw(plan[i].Offset, plan[i].Value);
        after = BcRegReadRaw(plan[i].Offset);

        /* Compara so os bits que a leitura pode confirmar. Bit de gatilho e
         * limpo pelo hardware depois de agir, e exigir que ele volte seria
         * exigir que a acao nao tivesse acontecido. */
        ok = ((after & keep) == (plan[i].Value & keep));

        Io->Offset[i] = plan[i].Offset;
        Io->Before[i] = before;
        Io->Wanted[i] = plan[i].Value;
        Io->After[i]  = after;
        Io->IgnoredBits[i] = plan[i].IgnoreMask;
        Io->WriteCount++;

        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
                  "GCVM program e%u: 0x%05X 0x%08X -> 0x%08X (queria 0x%08X, "
                  "gatilhos 0x%08X fora da conferencia)%s",
                  stage, plan[i].Offset, before, after, plan[i].Value,
                  plan[i].IgnoreMask, ok ? "" : "  DIVERGENCIA");

        if (!ok) {
            Io->Mismatches++;
            Io->Status = BC250DBG_OPS_MISMATCH;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "GCVM program e%u ABORTADO em 0x%05X: readback nao "
                      "confirmou. %u de %u escritas aplicadas; rode "
                      "'gcvm txn -restore'.",
                      stage, plan[i].Offset, Io->WriteCount, planCount);
            return STATUS_SUCCESS;
        }
    }

    Io->Status = BC250DBG_OPS_OK;
    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
              "GCVM program e%u: %u escritas, todas confirmadas por readback",
              stage, Io->WriteCount);
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------- */
/* Passo 4 do gate de memoria: invalidar a TLB e confirmar o ACK            */
/* ---------------------------------------------------------------------- */

/* Campos de GCVM_INVALIDATE_ENG0_REQ, do extrato de mascaras. */
#define F_INV_REQ__PER_VMID_INVALIDATE_REQ 0x0000FFFFu
#define F_INV_REQ__FLUSH_TYPE              0x00070000u
#define S_INV_REQ__FLUSH_TYPE              16
#define F_INV_REQ__INVALIDATE_L2_PTES      0x00080000u
#define F_INV_REQ__INVALIDATE_L2_PDE0      0x00100000u
#define F_INV_REQ__INVALIDATE_L2_PDE1      0x00200000u
#define F_INV_REQ__INVALIDATE_L2_PDE2      0x00400000u
#define F_INV_REQ__INVALIDATE_L1_PTES      0x00800000u
#define F_INV_REQ__CLEAR_PROT_FAULT_ADDR   0x01000000u

/* Distancias entre engines, iguais as do upstream (gfxhub_v2_0_init):
 * eng_distance = ENG1_REQ - ENG0_REQ = 1 dword = 4 bytes. */
#define BC_INV_ENG_STRIDE_BYTES 4u

/* gfxhub_v2_0_get_invalidate_req. */
static ULONG BcGcvmBuildInvalidateReq(ULONG Vmid, ULONG FlushType)
{
    ULONG req = 0;

    req |= (1u << Vmid) & F_INV_REQ__PER_VMID_INVALIDATE_REQ;
    req = BC_SETF(req, F_INV_REQ__FLUSH_TYPE, S_INV_REQ__FLUSH_TYPE, FlushType);
    req |= F_INV_REQ__INVALIDATE_L2_PTES;
    req |= F_INV_REQ__INVALIDATE_L2_PDE0;
    req |= F_INV_REQ__INVALIDATE_L2_PDE1;
    req |= F_INV_REQ__INVALIDATE_L2_PDE2;
    req |= F_INV_REQ__INVALIDATE_L1_PTES;
    /* CLEAR_PROTECTION_FAULT_STATUS_ADDR fica em 0, como no upstream: limpar o
     * endereco da falha aqui apagaria a evidencia de uma falha anterior. */
    return req;
}

NTSTATUS BcGcvmInvalidate(BC250DBG_GCVM_INVALIDATE_IO *Io)
{
    ULONG reqOff, ackOff;
    ULONG i;

    if (Io == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Io->Vmid > 15 || Io->FlushType > 7) {
        RtlZeroMemory((PUCHAR)Io + FIELD_OFFSET(BC250DBG_GCVM_INVALIDATE_IO, Engine),
                      sizeof(*Io) - FIELD_OFFSET(BC250DBG_GCVM_INVALIDATE_IO, Engine));
        Io->Size = sizeof(*Io);
        Io->Status = BC250DBG_OPS_RANGE;
        return STATUS_SUCCESS;
    }
    RtlZeroMemory((PUCHAR)Io + FIELD_OFFSET(BC250DBG_GCVM_INVALIDATE_IO, Engine),
                  sizeof(*Io) - FIELD_OFFSET(BC250DBG_GCVM_INVALIDATE_IO, Engine));
    Io->Size = sizeof(*Io);

    if (g_Dev.Mmio == NULL) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_ACCESS_DENIED;
    }

    /* Engine 17: "Use register 17 for GART", literal no upstream. Calculada
     * pela distancia, como o upstream faz, e nao por offset colado a mao. */
    Io->Engine = BC250DBG_GCVM_INV_ENGINE;
    reqOff = A_GCVM_INVALIDATE_ENG0_REQ + Io->Engine * BC_INV_ENG_STRIDE_BYTES;
    ackOff = A_GCVM_INVALIDATE_ENG0_ACK + Io->Engine * BC_INV_ENG_STRIDE_BYTES;

    /* Conferencia da aritmetica contra os offsets que ja estavam no catalogo.
     * Se as duas contas discordarem, o problema e de mapa e nao de hardware, e
     * disparar invalidacao no registrador errado nao ajuda ninguem. */
    if (reqOff != A_GCVM_INVALIDATE_ENG17_REQ ||
        ackOff != A_GCVM_INVALIDATE_ENG17_ACK) {
        Io->Status = BC250DBG_OPS_BADOP;
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "GCVM invalidate: offset calculado (REQ 0x%05X, ACK 0x%05X) "
                  "nao bate com o catalogo (0x%05X, 0x%05X); nada foi escrito",
                  reqOff, ackOff, A_GCVM_INVALIDATE_ENG17_REQ,
                  A_GCVM_INVALIDATE_ENG17_ACK);
        return STATUS_SUCCESS;
    }

    Io->ReqValue = BcGcvmBuildInvalidateReq(Io->Vmid, Io->FlushType);
    Io->AckExpectedMask = 1u << Io->Vmid;
    Io->AckBefore = BcRegReadRaw(ackOff);

    /* O upstream faz flush do HDP antes. Sem isso, PTEs escritas pela CPU podem
     * ainda nao estar visiveis para a GPU quando a invalidacao acontece — e a
     * TLB recarregaria conteudo velho. */
    BcHdpReadCacheInvalidate();
    KeMemoryBarrier();

    /* O GFXHUB NAO usa semaforo: gmc_v10_0_use_invalidate_semaphore() devolve
     * verdadeiro apenas para MMHUB0. Nada de adquirir aqui. */
    BcRegWriteRaw(reqOff, Io->ReqValue);

    /* Leitura descartada de REQ, OBRIGATORIA neste chip. O upstream a faz para
     * GFXHUB quando GC_HWIP < IP_VERSION(10, 3, 0) porque a interface GRBM
     * rapida pode devolver um ACK falso. GC 10.1.3 esta abaixo de 10.3.0.
     * Guardamos o valor lido em vez de descartar: e barato e diz se o REQ
     * sequer aceitou o que escrevemos. */
    Io->ReqReadback = BcRegReadRaw(reqOff);

    for (i = 0; i < 100000u; i++) {
        Io->AckAfter = BcRegReadRaw(ackOff);
        if ((Io->AckAfter & Io->AckExpectedMask) != 0) {
            break;
        }
        KeStallExecutionProcessor(1);
    }
    Io->PollIterations = i;
    Io->TimedOut = ((Io->AckAfter & Io->AckExpectedMask) == 0) ? 1u : 0u;

    /* Estado de falha lido DEPOIS. Se a invalidacao expos PTE invalida, e aqui
     * que aparece — e e informacao, nao ruido. */
    Io->FaultStatus = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_STATUS);
    Io->FaultAddrLo = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_ADDR_LO32);
    Io->FaultAddrHi = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_ADDR_HI32);

    Io->Status = Io->TimedOut ? BC250DBG_OPS_TIMEOUT : BC250DBG_OPS_OK;

    BcLogText(Io->TimedOut ? BC250DBG_LVL_ERROR : BC250DBG_LVL_INFO,
              BC250DBG_SUB_GC,
              "GCVM invalidate eng%u vmid%u: REQ 0x%08X (releu 0x%08X), "
              "ACK 0x%08X -> 0x%08X, mascara 0x%08X, %u iteracoes%s",
              Io->Engine, Io->Vmid, Io->ReqValue, Io->ReqReadback,
              Io->AckBefore, Io->AckAfter, Io->AckExpectedMask,
              Io->PollIterations, Io->TimedOut ? "  TIMEOUT" : "");
    return STATUS_SUCCESS;
}

/* Constroi e confere uma page table na VRAM, sem tocar em registrador de
 * traducao.
 *
 * Este e o passo 1 do gate de memoria, e o unico que nao pode piorar nada: se a
 * estrutura sair errada, ela e apenas bytes numa area de VRAM que ninguem usa,
 * porque nenhuma unidade de traducao foi apontada para ela.
 *
 * Procedencia de cada numero usado aqui:
 *   - formato do PTE e mascara de 48 bits: gmc_v10_0.c e amdgpu_gmc.c;
 *   - flags VALID/EXECUTABLE/READABLE/WRITEABLE: amdgpu_vm.h;
 *   - MTYPE_UC = 3: navi10_enum.h;
 *   - PA da pagina de VRAM: amdgpu_gmc_vram_mc2pa(), com vram_base_offset lido
 *     do registrador GCMC_VM_FB_OFFSET em tempo de execucao, nao de constante.
 *
 * O snapshot da regiao e obrigatorio: a area escolhida e VRAM de verdade, e a
 * regra do projeto e devolver o que foi tocado. */
NTSTATUS BcGcvmPageTable(BC250DBG_GCVM_PT_IO *Io)
{
    ULONG i, tableBytes, tableDwords;
    ULONGLONG flags, pa;
    NTSTATUS status;

    if (Io == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Dev.Mmio == NULL) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_ACCESS_DENIED;
    }

    /* A abertura de VRAM e mapeada sob demanda, como em BcSdmaInit. Medido em
     * 2026-08-23 09:39: sem isto, o ensaio de page table falhava com
     * STATUS_DEVICE_NOT_READY simplesmente porque nada antes dele havia tocado
     * VRAM naquela sessao — falha do laboratorio, nao da placa. */
    status = BcVramMap();
    if (!NT_SUCCESS(status)) {
        Io->Status = BC250DBG_OPS_DENIED;
        return status;
    }

    /* ---------------- rollback ---------------- */
    if (Io->Mode == BC250DBG_GCVM_PT_MODE_ROLLBACK) {
        ULONG mismatches = 0;

        if (g_Dev.GcvmPtBackup == NULL) {
            Io->SnapshotPresent = 0;
            Io->Status = BC250DBG_OPS_BADOP;
            return STATUS_INVALID_DEVICE_STATE;
        }
        for (i = 0; i < g_Dev.GcvmPtBackupDwords; i++) {
            BcVramWriteRaw(g_Dev.GcvmPtOffset + i * sizeof(ULONG),
                           g_Dev.GcvmPtBackup[i]);
        }
        KeMemoryBarrier();
        BcHdpReadCacheInvalidate();
        for (i = 0; i < g_Dev.GcvmPtBackupDwords; i++) {
            if (BcVramReadRaw(g_Dev.GcvmPtOffset + i * sizeof(ULONG)) !=
                g_Dev.GcvmPtBackup[i]) {
                mismatches++;
            }
        }

        Io->SnapshotPresent = 1;
        Io->SnapshotDwords = g_Dev.GcvmPtBackupDwords;
        Io->TableVramOffset = g_Dev.GcvmPtOffset;
        Io->RestoreMismatches = mismatches;
        Io->Status = (mismatches == 0) ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;

        ExFreePoolWithTag(g_Dev.GcvmPtBackup, 'tPcG');
        g_Dev.GcvmPtBackup = NULL;
        g_Dev.GcvmPtBackupDwords = 0;
        g_Dev.GcvmPtOffset = 0;

        BcLogText(mismatches ? BC250DBG_LVL_ERROR : BC250DBG_LVL_INFO,
                  BC250DBG_SUB_GC,
                  "GCVM page table rollback: %u dwords, %u divergencias",
                  Io->SnapshotDwords, mismatches);
        return (mismatches == 0) ? STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;
    }

    /* ---------------- build ---------------- */
    if (Io->PageCount == 0) Io->PageCount = BC250DBG_GCVM_PT_MAX_PAGES;
    if (Io->PageCount > BC250DBG_GCVM_PT_MAX_PAGES) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "page table: %u paginas pedidas, maximo %u",
                  Io->PageCount, BC250DBG_GCVM_PT_MAX_PAGES);
        Io->Status = BC250DBG_OPS_RANGE;
        return STATUS_INVALID_PARAMETER;
    }
    /* Tabela alinhada a 4 KB: e o alinhamento que o registrador de base da
     * page table exige, e conferir isso agora evita descobrir depois. */
    if ((Io->TableVramOffset & 0xFFFu) != 0 || (Io->TargetVramOffset & 0xFFFu) != 0) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "page table: offsets precisam ser alinhados a 4 KB "
                  "(tabela 0x%X, alvo 0x%X)",
                  Io->TableVramOffset, Io->TargetVramOffset);
        Io->Status = BC250DBG_OPS_RANGE;
        return STATUS_INVALID_PARAMETER;
    }

    tableBytes = Io->PageCount * 8u;
    tableDwords = tableBytes / sizeof(ULONG);

    if ((ULONGLONG)Io->TableVramOffset + tableBytes > g_Dev.VramLength) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "page table: tabela 0x%X + %u bytes passa da janela de VRAM (%u MB)",
                  Io->TableVramOffset, tableBytes, g_Dev.VramLength / (1024 * 1024));
        Io->Status = BC250DBG_OPS_RANGE;
        return STATUS_INVALID_PARAMETER;
    }
    /* A regiao APONTADA tambem tem de caber na janela: sao %u paginas de 4 KB a
     * partir do alvo. Medido em 2026-08-23 09:42: 512 paginas a partir de
     * 0x0FF20000 terminam em 0x10120000 e estouram a abertura de 256 MB. */
    if ((ULONGLONG)Io->TargetVramOffset + (ULONGLONG)Io->PageCount * 4096ull >
            g_Dev.VramLength) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "page table: alvo 0x%X + %u paginas termina em 0x%llX, "
                  "passa da janela de VRAM (0x%X)",
                  Io->TargetVramOffset, Io->PageCount,
                  (ULONGLONG)Io->TargetVramOffset + (ULONGLONG)Io->PageCount * 4096ull,
                  g_Dev.VramLength);
        Io->Status = BC250DBG_OPS_RANGE;
        return STATUS_INVALID_PARAMETER;
    }
    /* A tabela nao pode encostar no que a SDMA usa. Descobrir sobreposicao
     * depois, com traducao ligada, seria caro. */
    if (BcRangesOverlap(Io->TableVramOffset, tableBytes, BC250_SDMA_RING_VRAM_DEFAULT, 4096) ||
        BcRangesOverlap(Io->TableVramOffset, tableBytes, BC250_SDMA_RPTR_WB_OFFSET, 8) ||
        BcRangesOverlap(Io->TableVramOffset, tableBytes, BC250_SDMA_WPTR_SHADOW_OFFSET, 8) ||
        BcRangesOverlap(Io->TableVramOffset, tableBytes, BC250_SDMA_FENCE_VRAM_DEFAULT, 4)) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "page table em 0x%X/%u bytes sobrepoe area da SDMA",
                  Io->TableVramOffset, tableBytes);
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Dev.GcvmPtBackup != NULL) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "page table: snapshot anterior aberto; rode o rollback antes");
        Io->Status = BC250DBG_OPS_BADOP;
        return STATUS_INVALID_DEVICE_STATE;
    }

    /* vram_base_offset vem do silicio, como no upstream. */
    Io->FbOffsetReg = BcRegReadRaw(A_GCMC_VM_FB_OFFSET);
    Io->VramBaseOffset = (ULONGLONG)Io->FbOffsetReg << 24;

    flags = BC250DBG_PTE_VALID | BC250DBG_PTE_EXECUTABLE |
            BC250DBG_PTE_READABLE | BC250DBG_PTE_WRITEABLE |
            BC250DBG_PTE_MTYPE_UC;
    Io->PteFlags = flags;

    /* Snapshot antes de escrever. */
    g_Dev.GcvmPtBackup = (PULONG)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                 tableDwords * sizeof(ULONG),
                                                 'tPcG');
    if (g_Dev.GcvmPtBackup == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (i = 0; i < tableDwords; i++) {
        g_Dev.GcvmPtBackup[i] = BcVramReadRaw(Io->TableVramOffset + i * sizeof(ULONG));
    }
    g_Dev.GcvmPtBackupDwords = tableDwords;
    g_Dev.GcvmPtOffset = Io->TableVramOffset;
    Io->SnapshotPresent = 1;
    Io->SnapshotDwords = tableDwords;

    /* Escrita das PTEs. Dois dwords por entrada; a GPU nao le nada disso
     * enquanto CONTEXT0 estiver desabilitado, entao nao ha ordem a respeitar
     * alem da conferencia posterior. */
    for (i = 0; i < Io->PageCount; i++) {
        ULONGLONG pte;

        pa = (ULONGLONG)Io->TargetVramOffset + (ULONGLONG)i * 4096ull +
             Io->VramBaseOffset;
        pte = (pa & BC250DBG_PTE_ADDR_MASK) | flags;

        BcVramWriteRaw(Io->TableVramOffset + i * 8u, (ULONG)(pte & 0xFFFFFFFFull));
        BcVramWriteRaw(Io->TableVramOffset + i * 8u + 4u, (ULONG)(pte >> 32));

        if (i == 0) Io->FirstPa = pa;
        if (i + 1 == Io->PageCount) Io->LastPa = pa;
        if (i < BC250DBG_GCVM_PT_SAMPLES) Io->PteSample[i] = pte;
        Io->EntriesWritten++;
    }

    KeMemoryBarrier();
    BcHdpReadCacheInvalidate();

    /* Conferencia: cada entrada relida e comparada. Sem isso, "tabela
     * construida" seria apenas uma afirmacao sobre o que a CPU tentou fazer. */
    for (i = 0; i < Io->PageCount; i++) {
        ULONGLONG want, got;

        pa = (ULONGLONG)Io->TargetVramOffset + (ULONGLONG)i * 4096ull +
             Io->VramBaseOffset;
        want = (pa & BC250DBG_PTE_ADDR_MASK) | flags;
        got = (ULONGLONG)BcVramReadRaw(Io->TableVramOffset + i * 8u) |
              ((ULONGLONG)BcVramReadRaw(Io->TableVramOffset + i * 8u + 4u) << 32);
        if (got != want) {
            if (Io->Mismatches == 0) {
                Io->FirstMismatchIndex = i;
                Io->FirstMismatchGot = got;
                Io->FirstMismatchWant = want;
            }
            Io->Mismatches++;
        }
    }

    Io->Status = (Io->Mismatches == 0) ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;

    BcLogText(Io->Mismatches ? BC250DBG_LVL_ERROR : BC250DBG_LVL_INFO,
              BC250DBG_SUB_GC,
              "page table em 0x%X: %u PTEs, PA 0x%llx..0x%llx, FB_OFFSET 0x%X, "
              "%u divergencias",
              Io->TableVramOffset, Io->EntriesWritten, Io->FirstPa, Io->LastPa,
              Io->FbOffsetReg, Io->Mismatches);

    return (Io->Mismatches == 0) ? STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;
}

/* ---------------------------------------------------------------------- */
/* Compartilhado entre RLC e CP                                            */
/* ---------------------------------------------------------------------- */

#define A_GRBM_STATUS2         0x08008  /* dw 0x0da2 seg0 */

/* Um registrador que le 0xFFFFFFFF nao esta respondendo.
 *
 * Medido em 2026-08-23: CP_PFP_IC_OP_CNTL e os tres IC_BASE leem 0xFFFFFFFF nos
 * enderecos CORRETOS do segmento 1, enquanto SCRATCH_REG0..7 no mesmo segmento
 * respondem normalmente e CP_ME_CNTL no segmento 0 le 0x15000000. Bloco de GFX
 * nao decodificado devolve todos os bits em 1.
 *
 * Isso produziu um FALSO POSITIVO grave: a espera por
 * IC_OP_CNTL.INVALIDATE_CACHE_COMPLETE (bit 1) era satisfeita na primeira
 * leitura, porque 0xFFFFFFFF tem esse bit ligado. O ensaio declarou
 * "o hardware confirmou por COMPLETE" em duas sessoes sem que o registrador
 * tivesse respondido uma unica vez.
 *
 * Regra: antes de esperar por um bit de status, conferir que o registrador
 * responde. Todos-em-um satisfaz qualquer mascara de bit. */
#define BC_REG_NO_RESPONSE 0xFFFFFFFFu

static BOOLEAN BcRegResponds(ULONG Offset, ULONG *OutValue)
{
    ULONG v = BcRegReadRaw(Offset);
    if (OutValue != NULL) {
        *OutValue = v;
    }
    return (v != BC_REG_NO_RESPONSE);
}

/* ---------------------------------------------------------------------- */
/* Bring-up do RLC                                                        */
/* ---------------------------------------------------------------------- */

/* Enderecos do segmento 1: byte = (0xA000 + dw) * 4. Base conferida contra
 * registrador vivo em 2026-08-23 17:54 e cross-validada com o Linux nos mesmos
 * offsets em 23:07. */
#define A_RLC_CNTL             0x3B000  /* dw 0x4c00 seg1 */
#define A_RLC_SAFE_MODE        0x3B014  /* dw 0x4c05 seg1 */
#define A_RLC_PG_CNTL          0x3B10C  /* dw 0x4c43 seg1 */
#define A_RLC_CGCG_CGLS_CTRL   0x3B124  /* dw 0x4c49 seg1 */
#define A_RLC_SRM_CNTL         0x3B200  /* dw 0x4c80 seg1 */
#define A_RLC_GPM_STAT         0x3B9B8  /* dw 0x4e6e seg1 */
#define A_CGTT_CPF_CLK_CTRL    0x3C2C4  /* dw 0x50b1 seg1 */
/* Portas de escrita do ucode do RLC. Leem 0xFFFFFFFF nos dois sistemas: sao
 * write-only, como as portas equivalentes do CP. */
#define A_RLC_GPM_UCODE_ADDR   0x3ED84  /* dw 0x5b61 seg1 */
#define A_RLC_GPM_UCODE_DATA   0x3ED88  /* dw 0x5b62 seg1 */

/* Campos, do header autoritativo do kernel (gc_10_1_0_sh_mask.h, SHA-256
 * cba20414ecf02e91bafdcfbff7d4ba686326f0bdaf236d1a883364f08410c167). */
#define F_RLC_CNTL__RLC_ENABLE_F32     0x00000001u
#define F_RLC_CNTL__FORCE_RETRY        0x00000002u
#define F_RLC_GPM_STAT__RLC_BUSY       0x00000001u
#define F_RLC_GPM_STAT__EXEC_ROM_CODE  0x00400000u  /* shift 0x16 */
#define F_RLC_SRM_CNTL__SRM_ENABLE     0x00000001u
#define F_RLC_SRM_CNTL__AUTO_INCR_ADDR 0x00000002u

static const ULONG g_RlcTxnRegs[BC250DBG_RLC_TXN_REG_COUNT] = {
    A_RLC_CNTL,
    A_RLC_CGCG_CGLS_CTRL,
    A_RLC_PG_CNTL,
    A_RLC_SRM_CNTL
};

C_ASSERT(RTL_NUMBER_OF(g_RlcTxnRegs) == BC250DBG_RLC_TXN_REG_COUNT);

static BOOLEAN BcRlcTxnCovers(ULONG Offset)
{
    ULONG i;
    for (i = 0; i < BC250DBG_RLC_TXN_REG_COUNT; i++) {
        if (g_RlcTxnRegs[i] == Offset) return TRUE;
    }
    return FALSE;
}

/* Estado lido em toda operacao, com os bits que decidem ja decodificados.
 *
 * RLC_EXEC_ROM_CODE e o que importa: 1 significa RLC rodando o bootloader
 * interno, sem microcodigo. O gate de todo este caminho e esse bit cair para 0. */
static VOID BcRlcFillState(BC250DBG_RLC_IO *Io)
{
    Io->Cntl         = BcRegReadRaw(A_RLC_CNTL);
    Io->GpmStat      = BcRegReadRaw(A_RLC_GPM_STAT);
    Io->CgcgCglsCntl = BcRegReadRaw(A_RLC_CGCG_CGLS_CTRL);
    Io->PgCntl       = BcRegReadRaw(A_RLC_PG_CNTL);
    Io->SafeMode     = BcRegReadRaw(A_RLC_SAFE_MODE);
    Io->SrmCntl      = BcRegReadRaw(A_RLC_SRM_CNTL);
    Io->GrbmStatus   = BcRegReadRaw(BC250_REG_GRBM_STATUS);
    Io->GrbmStatus2  = BcRegReadRaw(A_GRBM_STATUS2);
    Io->CgttCpfClkCtrl = BcRegReadRaw(A_CGTT_CPF_CLK_CTRL);

    Io->EnableF32   = (Io->Cntl & F_RLC_CNTL__RLC_ENABLE_F32) ? 1u : 0u;
    Io->ExecRomCode = (Io->GpmStat & F_RLC_GPM_STAT__EXEC_ROM_CODE) ? 1u : 0u;
    Io->RlcBusy     = (Io->GpmStat & F_RLC_GPM_STAT__RLC_BUSY) ? 1u : 0u;
}

/* Uma escrita registrada e conferida, exigindo cobertura da transacao. */
static BOOLEAN BcRlcWrite(BC250DBG_RLC_IO *Io, ULONG Offset, ULONG Value)
{
    ULONG before, after;

    if (Io->WriteCount >= BC250DBG_RLC_WRITES) {
        return FALSE;
    }
    if (!BcRlcTxnCovers(Offset)) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "RLC: 0x%05X nao esta na lista da transacao; o rollback nao o "
                  "desfaria", Offset);
        return FALSE;
    }
    if (!BcRegResponds(Offset, &before)) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "RLC: 0x%05X le 0xFFFFFFFF; nao escrevo em bloco que nao "
                  "responde", Offset);
        return FALSE;
    }
    BcRegWriteRaw(Offset, Value);
    after = BcRegReadRaw(Offset);

    Io->WrOffset[Io->WriteCount] = Offset;
    Io->WrBefore[Io->WriteCount] = before;
    Io->WrWanted[Io->WriteCount] = Value;
    Io->WrAfter[Io->WriteCount]  = after;
    Io->WriteCount++;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
              "RLC: 0x%05X 0x%08X -> 0x%08X (queria 0x%08X)%s",
              Offset, before, after, Value,
              (after == Value) ? "" : "  DIVERGENCIA");

    if (after != Value) {
        Io->Mismatches++;
        return FALSE;
    }
    return TRUE;
}

NTSTATUS BcRlc(BC250DBG_RLC_IO *Io, const ULONG *Payload, ULONG PayloadCount)
{
    ULONG i, tmp;

    if (Io == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory((PUCHAR)Io + FIELD_OFFSET(BC250DBG_RLC_IO, Cntl),
                  sizeof(*Io) - FIELD_OFFSET(BC250DBG_RLC_IO, Cntl));
    Io->Size = sizeof(*Io);

    if (g_Dev.Mmio == NULL) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_DEVICE_NOT_READY;
    }
    /* STATUS, SNAPSHOT e VERIFY sao leitura pura e passam em readonly. Todo o
     * resto escreve e exige armamento. */
    if (Io->Mode != BC250DBG_RLC_MODE_STATUS &&
        Io->Mode != BC250DBG_RLC_MODE_TXN_SNAP &&
        Io->Mode != BC250DBG_RLC_MODE_TXN_VERIFY &&
        g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_ACCESS_DENIED;
    }

    Io->RegisterCount = BC250DBG_RLC_TXN_REG_COUNT;
    for (i = 0; i < BC250DBG_RLC_TXN_REG_COUNT; i++) {
        Io->Offsets[i] = g_RlcTxnRegs[i];
    }
    BcRlcFillState(Io);

    switch (Io->Mode) {

    case BC250DBG_RLC_MODE_STATUS:
        Io->Status = BC250DBG_OPS_OK;
        return STATUS_SUCCESS;

    case BC250DBG_RLC_MODE_TXN_SNAP:
        for (i = 0; i < BC250DBG_RLC_TXN_REG_COUNT; i++) {
            g_Dev.RlcTxnValues[i] = BcRegReadRaw(g_RlcTxnRegs[i]);
            Io->Values[i] = g_Dev.RlcTxnValues[i];
        }
        g_Dev.RlcTxnValid = TRUE;
        Io->SnapshotPresent = 1;
        Io->Status = BC250DBG_OPS_OK;
        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
                  "RLC snapshot: %u registradores guardados",
                  BC250DBG_RLC_TXN_REG_COUNT);
        return STATUS_SUCCESS;

    case BC250DBG_RLC_MODE_TXN_VERIFY:
    case BC250DBG_RLC_MODE_TXN_RESTORE: {
        BOOLEAN restore = (Io->Mode == BC250DBG_RLC_MODE_TXN_RESTORE);

        if (!g_Dev.RlcTxnValid) {
            Io->SnapshotPresent = 0;
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "RLC: nenhum snapshot aberto; rode 'rlc txn snapshot'");
            return STATUS_SUCCESS;
        }
        Io->SnapshotPresent = 1;
        for (i = 0; i < BC250DBG_RLC_TXN_REG_COUNT; i++) {
            ULONG got;
            if (restore) {
                BcRegWriteRaw(g_RlcTxnRegs[i], g_Dev.RlcTxnValues[i]);
            }
            got = BcRegReadRaw(g_RlcTxnRegs[i]);
            Io->Values[i] = got;
            if (got == g_Dev.RlcTxnValues[i]) {
                if (restore) Io->Restored++;
            } else {
                if (Io->Mismatches < BC250DBG_RLC_TXN_BAD_SAMPLES) {
                    Io->BadOffset[Io->Mismatches] = g_RlcTxnRegs[i];
                    Io->BadGot[Io->Mismatches] = got;
                    Io->BadWant[Io->Mismatches] = g_Dev.RlcTxnValues[i];
                }
                Io->Mismatches++;
            }
        }
        BcRlcFillState(Io);
        Io->Status = (Io->Mismatches == 0) ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;
        BcLogText(Io->Mismatches ? BC250DBG_LVL_ERROR : BC250DBG_LVL_INFO,
                  BC250DBG_SUB_GC, "RLC txn %s: %u restaurados, %u divergencias",
                  restore ? "restore" : "verify", Io->Restored, Io->Mismatches);
        return STATUS_SUCCESS;
    }

    /* gfx_v10_0_rlc_stop: RLC_ENABLE_F32 = 0. */
    case BC250DBG_RLC_MODE_STOP:
        if (!g_Dev.RlcTxnValid) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "RLC stop: RECUSADO, nenhum snapshot de transacao aberto");
            return STATUS_SUCCESS;
        }
        tmp = Io->Cntl & ~F_RLC_CNTL__RLC_ENABLE_F32;
        BcRlcWrite(Io, A_RLC_CNTL, tmp);
        BcRlcFillState(Io);
        Io->Status = Io->Mismatches ? BC250DBG_OPS_MISMATCH : BC250DBG_OPS_OK;
        return STATUS_SUCCESS;

    /* "disable CG": o upstream escreve zero inteiro, nao um campo. */
    case BC250DBG_RLC_MODE_CGCG_OFF:
        if (!g_Dev.RlcTxnValid) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "RLC cgcg: RECUSADO, nenhum snapshot de transacao aberto");
            return STATUS_SUCCESS;
        }
        BcRlcWrite(Io, A_RLC_CGCG_CGLS_CTRL, 0);
        BcRlcFillState(Io);
        Io->Status = Io->Mismatches ? BC250DBG_OPS_MISMATCH : BC250DBG_OPS_OK;
        return STATUS_SUCCESS;

    /* "disable PG": idem, zero inteiro. */
    case BC250DBG_RLC_MODE_PG_OFF:
        if (!g_Dev.RlcTxnValid) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "RLC pg: RECUSADO, nenhum snapshot de transacao aberto");
            return STATUS_SUCCESS;
        }
        BcRlcWrite(Io, A_RLC_PG_CNTL, 0);
        BcRlcFillState(Io);
        Io->Status = Io->Mismatches ? BC250DBG_OPS_MISMATCH : BC250DBG_OPS_OK;
        return STATUS_SUCCESS;

    /* gfx_v10_0_rlc_load_microcode.
     *
     * ADDR e DATA sao portas de escrita: leem 0xFFFFFFFF nos dois sistemas, e
     * por isso NAO passam pela guarda de responsividade nem pela transacao. O
     * que se pode conferir e o ADDR relido no fim — e mesmo isso vale como
     * indicio, nao como prova. A prova e o RLC sair da ROM. */
    case BC250DBG_RLC_MODE_LOAD_UCODE:
        if (Payload == NULL || PayloadCount == 0 ||
            PayloadCount > BC250DBG_RLC_MAX_UCODE_DWORDS ||
            PayloadCount != Io->DwordCount) {
            Io->Status = BC250DBG_OPS_RANGE;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "RLC load: payload de %u dwords invalido (declarado %u, "
                      "maximo %u)", PayloadCount, Io->DwordCount,
                      BC250DBG_RLC_MAX_UCODE_DWORDS);
            return STATUS_SUCCESS;
        }
        BcRegWriteRaw(A_RLC_GPM_UCODE_ADDR, BC250DBG_RLC_UCODE_START_ADDR);
        for (i = 0; i < PayloadCount; i++) {
            BcRegWriteRaw(A_RLC_GPM_UCODE_DATA, Payload[i]);
            Io->DwordsWritten++;
        }
        BcRegWriteRaw(A_RLC_GPM_UCODE_ADDR, Io->UcodeVersion);
        Io->AddrReadback = BcRegReadRaw(A_RLC_GPM_UCODE_ADDR);
        BcRlcFillState(Io);
        Io->Status = BC250DBG_OPS_OK;
        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
                  "RLC load: %u dwords a partir de 0x%X, versao 0x%X "
                  "(ADDR releu 0x%08X); EXEC_ROM_CODE=%u",
                  Io->DwordsWritten, BC250DBG_RLC_UCODE_START_ADDR,
                  Io->UcodeVersion, Io->AddrReadback, Io->ExecRomCode);
        return STATUS_SUCCESS;

    /* gfx_v10_0_rlc_start: RLC_ENABLE_F32 = 1, e o upstream espera 50 us. */
    case BC250DBG_RLC_MODE_START:
        if (!g_Dev.RlcTxnValid) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "RLC start: RECUSADO, nenhum snapshot de transacao aberto");
            return STATUS_SUCCESS;
        }
        tmp = Io->Cntl | F_RLC_CNTL__RLC_ENABLE_F32;
        BcRlcWrite(Io, A_RLC_CNTL, tmp);
        KeStallExecutionProcessor(50);
        BcRlcFillState(Io);
        /* O status reflete a ESCRITA. O veredito sobre execucao e o ensaio quem
         * da, olhando EXEC_ROM_CODE — e o log registra os dois para que ninguem
         * confunda um com o outro. */
        Io->Status = Io->Mismatches ? BC250DBG_OPS_MISMATCH : BC250DBG_OPS_OK;
        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
                  "RLC start: ENABLE_F32=%u, EXEC_ROM_CODE=%u, RLC_BUSY=%u, "
                  "GPM_STAT=0x%08X. Escrita aceita NAO e execucao: o gate e o "
                  "EXEC_ROM_CODE cair para 0.",
                  Io->EnableF32, Io->ExecRomCode, Io->RlcBusy, Io->GpmStat);
        return STATUS_SUCCESS;

    /* gfx_v10_0_rlc_enable_srm. */
    case BC250DBG_RLC_MODE_SRM_ENABLE:
        if (!g_Dev.RlcTxnValid) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "RLC srm: RECUSADO, nenhum snapshot de transacao aberto");
            return STATUS_SUCCESS;
        }
        tmp = Io->SrmCntl | F_RLC_SRM_CNTL__SRM_ENABLE |
              F_RLC_SRM_CNTL__AUTO_INCR_ADDR;
        BcRlcWrite(Io, A_RLC_SRM_CNTL, tmp);
        BcRlcFillState(Io);
        Io->Status = Io->Mismatches ? BC250DBG_OPS_MISMATCH : BC250DBG_OPS_OK;
        return STATUS_SUCCESS;

    default:
        Io->Status = BC250DBG_OPS_BADOP;
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "RLC: modo %u nao existe", Io->Mode);
        return STATUS_SUCCESS;
    }
}

/* ---------------------------------------------------------------------- */
/* Direct load do CP grafico: PFP, ME, CE                                  */
/* ---------------------------------------------------------------------- */

/* Enderecos do segmento 1: byte = (0xA000 + mmREG_dw) * 4.
 *
 * A base 0xA000 foi conferida contra registrador vivo em 2026-08-23 17:54
 * (SCRATCH_REG0..7, dois padroes complementares, controle no segmento 0
 * intacto). Antes disso era derivacao, e escrever aqui seria escrever MMIO por
 * suposicao. */
/* Registradores do CP (GC_BASE 0x1260 dw = 0x4980 bytes).
 * Calculo: (0x1260 + dw_offset) * 4 */
#define A_CP_PFP_IC_BASE_LO    0x3E100  /* dw 0x5840 seg1 */
#define A_CP_PFP_IC_BASE_HI    0x3E104  /* dw 0x5841 seg1 */
#define A_CP_PFP_IC_BASE_CNTL  0x3E108  /* dw 0x5842 seg1 */
#define A_CP_PFP_IC_OP_CNTL    0x3E10C  /* dw 0x5843 seg1 */
#define A_CP_ME_IC_BASE_LO     0x3E110  /* dw 0x5844 seg1 */
#define A_CP_ME_IC_BASE_HI     0x3E114  /* dw 0x5845 seg1 */
#define A_CP_ME_IC_BASE_CNTL   0x3E118  /* dw 0x5846 seg1 */
#define A_CP_ME_IC_OP_CNTL     0x3E11C  /* dw 0x5847 seg1 */
#define A_CP_CE_IC_BASE_LO     0x3E120  /* dw 0x5848 seg1 */
#define A_CP_CE_IC_BASE_HI     0x3E124  /* dw 0x5849 seg1 */
#define A_CP_CE_IC_BASE_CNTL   0x3E128  /* dw 0x584a seg1 */
#define A_CP_CE_IC_OP_CNTL     0x3E12C  /* dw 0x584b seg1 */
#define A_CP_CPC_IC_BASE_LO    0x3E130  /* dw 0x584c seg1 */
#define A_CP_CPC_IC_BASE_HI    0x3E134  /* dw 0x584d seg1 */
#define A_CP_CPC_IC_BASE_CNTL  0x3E138  /* dw 0x584e seg1 */
#define A_CP_CPC_IC_OP_CNTL    0x3E13C  /* dw 0x584f seg1 */

/* CP Jump Table / RAM Registers (GC_BASE 0x1260 shifted) */
#define A_CP_PFP_UCODE_ADDR    0x3E050  /* dw 0x5814 seg1 */
#define A_CP_PFP_UCODE_DATA    0x3E054  /* dw 0x5815 seg1 */
#define A_CP_ME_UCODE_ADDR     0x3E058  /* dw 0x5816 seg1 */
#define A_CP_ME_UCODE_DATA     0x3E05C  /* dw 0x5817 seg1 */
#define A_CP_CE_UCODE_ADDR     0x3E060  /* dw 0x5818 seg1 */
#define A_CP_CE_UCODE_DATA     0x3E064  /* dw 0x5819 seg1 */
#define A_CP_MEC_ME1_UCODE_ADDR 0x3E068 /* dw 0x581a seg1 */
#define A_CP_MEC_ME1_UCODE_DATA 0x3E06C /* dw 0x581b seg1 */
#define A_CPG_PSP_DEBUG        0x3F040  /* dw 0x5c10 seg1 */
#define A_CPC_PSP_DEBUG        0x3F044  /* dw 0x5c11 seg1 */

/* Segmento 0. */
#define A_CP_ME_CNTL           0x086D8  /* dw 0x0f56 seg0 */
#define A_CP_MEC_CNTL          0x08234  /* dw 0x0e2d seg0 */
#define A_CP_STAT              0x08680  /* dw 0x0f40 seg0 */
#define A_CP_PFP_INSTR_PNTR    0x08694  /* dw 0x0f45 seg0 */
#define A_CP_ME_INSTR_PNTR     0x08698  /* dw 0x0f46 seg0 */
#define A_CP_CE_INSTR_PNTR     0x0869C  /* dw 0x0f47 seg0 */
#define A_CP_MEC1_INSTR_PNTR   0x086A0  /* dw 0x0f48 seg0 */

/* ABI 26: Jump Table status readback (Segmento 0). */
#define A_CP_PFP_JT_STAT       0x0C4CC  /* dw 0x1ed3 seg0 */
#define A_CP_CE_JT_STAT        0x0C4D0  /* dw 0x1ed4 seg0 */
#define A_CP_MEC_JT_STAT       0x0C4D4  /* dw 0x1ed5 seg0 */

/* ABI 29: Golden settings Cyan Skillfish. Enderecos SOC15 conferidos com
 * GC_BASE seg0=0x1260, seg1=0xA000 e presentes nas duas passagens do sweep. */
#define A_GRBM_GFX_INDEX       0x30800  /* dw 0x2200 seg1 */
#define A_GE_FAST_CLKS         0x08920  /* dw 0x0fe8 seg0 */
#define A_CGTT_CPF_CLK_CTRL    0x3C2C4
#define A_CGTT_SPI_CLK_CTRL    0x3C200
#define A_CB_HW_CONTROL_3      0x09A0C
#define A_CB_HW_CONTROL_4      0x09A08
#define A_CH_DRAM_BURST_CTRL   0x33610
#define A_CH_PIPE_STEER        0x33640
#define A_CH_VC5_ENABLE        0x33650
#define A_DB_DEBUG             0x09830
#define A_DB_DEBUG3            0x09838
#define A_DB_DEBUG4            0x0983C
#define A_DB_LAST_OF_BURST_CONFIG 0x09868
#define A_GB_ADDR_CONFIG       0x098F8
#define A_GCR_GENERAL_CNTL     0x09F8C
#define A_GCMC_VM_CACHEABLE_DRAM_ADDRESS_END 0x0A5C8
#define A_GL1_DRAM_BURST_CTRL  0x33410
#define A_GL1_PIPE_STEER       0x33440
#define A_GL2_PIPE_STEER_0     0x33894
#define A_GL2_PIPE_STEER_1     0x33898
#define A_GL2A_ADDR_MATCH_MASK 0x33884
#define A_GL2C_ADDR_MATCH_MASK 0x3380C
#define A_GL2C_CGTT_SCLK_CTRL  0x3C2B0
#define A_GL2C_CTRL2           0x33804
#define A_GL2C_CTRL3           0x33830
#define A_PA_SC_ENHANCE        0x08BF0
#define A_PA_SC_BINNER_EVENT_CNTL_0 0x08B30
#define A_PA_SC_LINE_STIPPLE_STATE 0x30A04
#define A_RMI_SPARE            0x09E7C
#define A_SQ_ARB_CONFIG        0x08C30
#define A_SQ_LDS_CLK_CTRL      0x3C240
#define A_TA_CNTL_AUX          0x09508
#define A_UTCL1_CTRL           0x09FA0

/* Valores steady-state do sweep. Nao pertencem a tabela golden de 34 itens. */
#define A_CP_PFP_LOAD_CONTROL  0x30238  /* dw 0x208e seg1 */
#define A_CP_ROQ1_THRESHOLDS   0x08754  /* dw 0x0f75 seg0 */
#define A_CP_ROQ2_THRESHOLDS   0x08758  /* dw 0x0f76 seg0 */
#define A_CP_ROQ3_THRESHOLDS   0x087B0  /* dw 0x0f8c seg0 */
#define A_CP_STQ_THRESHOLDS    0x0875C  /* dw 0x0f77 seg0 */
#define A_CP_MEQ_THRESHOLDS    0x08764  /* dw 0x0f79 seg0 */
#define A_CP_MAX_CONTEXT       0x0C2B8  /* dw 0x1eae seg0 */
#define A_CP_DEVICE_ID         0x0C12C  /* dw 0x1e8b seg0 */
#define A_CP_CONTEXT_CNTL      0x0C2B4  /* dw 0x1ead seg0 */
#define A_CP_SD_CNTL           0x0C6DC  /* dw 0x1f77 seg0 */
#define A_CP_COHER_START_DELAY 0x301EC  /* dw 0x207b seg1 */
#define A_CP_DMA_CNTL          0x30228  /* dw 0x208a seg1 */
#define A_CGTT_CPC_CLK_CTRL    0x3C2C8  /* dw 0x50b2 seg1 */

/* Campos, do header autoritativo do kernel (gc_10_1_0_sh_mask.h). */
#define F_CP_ME_CNTL__CE_HALT   0x01000000u  /* shift 0x18 */
#define F_CP_ME_CNTL__PFP_HALT  0x04000000u  /* shift 0x1a */
#define F_CP_ME_CNTL__ME_HALT   0x10000000u  /* shift 0x1c */

#define F_CP_IC_BASE_CNTL__VMID          0x0000000Fu  /* shift 0 */
#define F_CP_IC_BASE_CNTL__ADDRESS_CLAMP 0x00000010u  /* shift 4 */
#define F_CP_IC_BASE_CNTL__EXE_DISABLE   0x00800000u  /* shift 0x17 */
#define F_CP_IC_BASE_CNTL__CACHE_POLICY  0x03000000u  /* shift 0x18 */

#define F_CP_IC_OP_CNTL__INVALIDATE_CACHE          0x00000001u  /* shift 0 */
#define F_CP_IC_OP_CNTL__INVALIDATE_CACHE_COMPLETE 0x00000002u  /* shift 1 */
#define F_CP_IC_OP_CNTL__PRIME_ICACHE              0x00000010u  /* shift 4 */
#define F_CP_IC_OP_CNTL__ICACHE_PRIMED             0x00000020u  /* shift 5 */

typedef struct _BC_CP_ENGINE_REGS {
    const char *Nome;
    ULONG IcBaseLo, IcBaseHi, IcBaseCntl, IcOpCntl;
    ULONG UcodeAddr, UcodeData;
    ULONG HaltMask;
} BC_CP_ENGINE_REGS;

static const BC_CP_ENGINE_REGS g_CpEngines[BC250DBG_CP_ENGINE_COUNT] = {
    { "pfp", A_CP_PFP_IC_BASE_LO, A_CP_PFP_IC_BASE_HI, A_CP_PFP_IC_BASE_CNTL,
      A_CP_PFP_IC_OP_CNTL, A_CP_PFP_UCODE_ADDR, A_CP_PFP_UCODE_DATA,
      F_CP_ME_CNTL__PFP_HALT },
    { "me",  A_CP_ME_IC_BASE_LO,  A_CP_ME_IC_BASE_HI,  A_CP_ME_IC_BASE_CNTL,
      A_CP_ME_IC_OP_CNTL,  A_CP_ME_UCODE_ADDR,  A_CP_ME_UCODE_DATA,
      F_CP_ME_CNTL__ME_HALT },
    { "ce",  A_CP_CE_IC_BASE_LO,  A_CP_CE_IC_BASE_HI,  A_CP_CE_IC_BASE_CNTL,
      A_CP_CE_IC_OP_CNTL,  A_CP_CE_UCODE_ADDR,  A_CP_CE_UCODE_DATA,
      F_CP_ME_CNTL__CE_HALT },
    { "mec", A_CP_CPC_IC_BASE_LO, A_CP_CPC_IC_BASE_HI, A_CP_CPC_IC_BASE_CNTL,
      A_CP_CPC_IC_OP_CNTL, A_CP_MEC_ME1_UCODE_ADDR, A_CP_MEC_ME1_UCODE_DATA,
      0x50000000u },
};

typedef struct _BC_CP_GOLDEN_REG {
    ULONG Offset;
    /* soc15_program_register_sequence: esta mascara seleciona os bits que
     * serao SUBSTITUIDOS, nao os bits preservados. Formula autoritativa:
     * (lido & ~WriteMask) | (OrValue & WriteMask). */
    ULONG WriteMask;
    ULONG OrValue;
    BOOLEAN AllOnesIsValid;
    const char *Name;
} BC_CP_GOLDEN_REG;

/* Copia literal de golden_settings_gc_10_0_cyan_skillfish[]. O endereco BAR
 * deriva de (GC_BASE[BASE_IDX] + mmREG) * 4. SQ_LDS_CLK_CTRL e a unica entrada
 * cujo valor legitimo e 0xFFFFFFFF; para todas as outras isso significa que o
 * endereco nao respondeu e o preflight deve falhar fechado. */
static const BC_CP_GOLDEN_REG g_CpGoldenRegs[BC250DBG_CP_GOLDEN_REG_COUNT] = {
    { A_GRBM_GFX_INDEX,       0xFFFFFFFFu, 0xE0000000u, FALSE, "GRBM_GFX_INDEX" },
    { A_GE_FAST_CLKS,         0x3FFFFFFFu, 0x0000493Eu, FALSE, "GE_FAST_CLKS" },
    { A_CGTT_CPF_CLK_CTRL,    0xFCFF8FFFu, 0xF8000100u, FALSE, "CGTT_CPF_CLK_CTRL" },
    { A_CGTT_SPI_CLK_CTRL,    0xFF7F0FFFu, 0x3C000100u, FALSE, "CGTT_SPI_CLK_CTRL" },
    { A_CB_HW_CONTROL_3,      0xA0000000u, 0xA0000000u, FALSE, "CB_HW_CONTROL_3" },
    { A_CB_HW_CONTROL_4,      0x00008000u, 0x003C8014u, FALSE, "CB_HW_CONTROL_4" },
    { A_CH_DRAM_BURST_CTRL,   0x00000010u, 0x00000017u, FALSE, "CH_DRAM_BURST_CTRL" },
    { A_CH_PIPE_STEER,        0xFFFFFFFFu, 0xD8D8D8D8u, FALSE, "CH_PIPE_STEER" },
    { A_CH_VC5_ENABLE,        0x00000003u, 0x00000003u, FALSE, "CH_VC5_ENABLE" },
    { A_CP_SD_CNTL,           0x800007FFu, 0x000005FFu, FALSE, "CP_SD_CNTL" },
    { A_DB_DEBUG,             0xFFFFFFFFu, 0x20000000u, FALSE, "DB_DEBUG" },
    { A_DB_DEBUG3,            0xFFFFFFFFu, 0x00000200u, FALSE, "DB_DEBUG3" },
    { A_DB_DEBUG4,            0xFFFFFFFFu, 0x04800000u, FALSE, "DB_DEBUG4" },
    { A_DB_LAST_OF_BURST_CONFIG, 0xFFFFFFFFu, 0x03860210u, FALSE, "DB_LAST_OF_BURST_CONFIG" },
    { A_GB_ADDR_CONFIG,       0x0C1800FFu, 0x00000044u, FALSE, "GB_ADDR_CONFIG" },
    { A_GCR_GENERAL_CNTL,     0x00009D00u, 0x00008500u, FALSE, "GCR_GENERAL_CNTL" },
    { A_GCMC_VM_CACHEABLE_DRAM_ADDRESS_END, 0xFFFFFFFFu, 0x000FFFFFu, FALSE, "GCMC_VM_CACHEABLE_DRAM_ADDRESS_END" },
    { A_GL1_DRAM_BURST_CTRL,  0x00000010u, 0x00000017u, FALSE, "GL1_DRAM_BURST_CTRL" },
    { A_GL1_PIPE_STEER,       0xFCFCFCFCu, 0xD8D8D8D8u, FALSE, "GL1_PIPE_STEER" },
    { A_GL2_PIPE_STEER_0,     0x77707770u, 0x21302130u, FALSE, "GL2_PIPE_STEER_0" },
    { A_GL2_PIPE_STEER_1,     0x77707770u, 0x21302130u, FALSE, "GL2_PIPE_STEER_1" },
    { A_GL2A_ADDR_MATCH_MASK, 0xFFFFFFFFu, 0xFFFFFFCFu, FALSE, "GL2A_ADDR_MATCH_MASK" },
    { A_GL2C_ADDR_MATCH_MASK, 0xFFFFFFFFu, 0xFFFFFFCFu, FALSE, "GL2C_ADDR_MATCH_MASK" },
    { A_GL2C_CGTT_SCLK_CTRL,  0x10000000u, 0x10000100u, FALSE, "GL2C_CGTT_SCLK_CTRL" },
    { A_GL2C_CTRL2,           0xFC02002Fu, 0x9402002Fu, FALSE, "GL2C_CTRL2" },
    { A_GL2C_CTRL3,           0x00002188u, 0x00000188u, FALSE, "GL2C_CTRL3" },
    { A_PA_SC_ENHANCE,        0x08000009u, 0x08000009u, FALSE, "PA_SC_ENHANCE" },
    { A_PA_SC_BINNER_EVENT_CNTL_0, 0xCC3FCC03u, 0x842A4C02u, FALSE, "PA_SC_BINNER_EVENT_CNTL_0" },
    { A_PA_SC_LINE_STIPPLE_STATE, 0x0000000Fu, 0x00000000u, FALSE, "PA_SC_LINE_STIPPLE_STATE" },
    { A_RMI_SPARE,            0xFFFF3109u, 0xFFFF3101u, FALSE, "RMI_SPARE" },
    { A_SQ_ARB_CONFIG,        0x00000100u, 0x00000130u, FALSE, "SQ_ARB_CONFIG" },
    { A_SQ_LDS_CLK_CTRL,      0xFFFFFFFFu, 0xFFFFFFFFu, TRUE,  "SQ_LDS_CLK_CTRL" },
    { A_TA_CNTL_AUX,          0x00030008u, 0x01030000u, FALSE, "TA_CNTL_AUX" },
    { A_UTCL1_CTRL,           0x00800000u, 0x00800000u, FALSE, "UTCL1_CTRL" }
};

static const ULONG g_CpGoldenAuditRegs[BC250DBG_CP_GOLDEN_AUDIT_REG_COUNT] = {
    A_GRBM_GFX_INDEX, A_GE_FAST_CLKS, A_CGTT_CPF_CLK_CTRL, A_CGTT_SPI_CLK_CTRL,
    A_CB_HW_CONTROL_3, A_CB_HW_CONTROL_4, A_CH_DRAM_BURST_CTRL, A_CH_PIPE_STEER,
    A_CH_VC5_ENABLE, A_CP_SD_CNTL, A_DB_DEBUG, A_DB_DEBUG3, A_DB_DEBUG4,
    A_DB_LAST_OF_BURST_CONFIG, A_GB_ADDR_CONFIG, A_GCR_GENERAL_CNTL,
    A_GCMC_VM_CACHEABLE_DRAM_ADDRESS_END, A_GL1_DRAM_BURST_CTRL,
    A_GL1_PIPE_STEER, A_GL2_PIPE_STEER_0, A_GL2_PIPE_STEER_1,
    A_GL2A_ADDR_MATCH_MASK, A_GL2C_ADDR_MATCH_MASK, A_GL2C_CGTT_SCLK_CTRL,
    A_GL2C_CTRL2, A_GL2C_CTRL3, A_PA_SC_ENHANCE, A_PA_SC_BINNER_EVENT_CNTL_0,
    A_PA_SC_LINE_STIPPLE_STATE, A_RMI_SPARE, A_SQ_ARB_CONFIG, A_SQ_LDS_CLK_CTRL,
    A_TA_CNTL_AUX, A_UTCL1_CTRL,
    A_CPG_PSP_DEBUG, A_CPC_PSP_DEBUG
};

C_ASSERT(RTL_NUMBER_OF(g_CpGoldenRegs) == BC250DBG_CP_GOLDEN_REG_COUNT);
C_ASSERT(RTL_NUMBER_OF(g_CpGoldenAuditRegs) == BC250DBG_CP_GOLDEN_AUDIT_REG_COUNT);

/* Registradores de CONFIGURACAO cobertos pela transacao. */
static const ULONG g_CpTxnRegs[BC250DBG_CP_TXN_REG_COUNT] = {
    A_CP_PFP_IC_BASE_LO,    A_CP_PFP_IC_BASE_HI,    A_CP_PFP_IC_BASE_CNTL,
    A_CP_ME_IC_BASE_LO,     A_CP_ME_IC_BASE_HI,     A_CP_ME_IC_BASE_CNTL,
    A_CP_CE_IC_BASE_LO,     A_CP_CE_IC_BASE_HI,     A_CP_CE_IC_BASE_CNTL,
    A_CP_CPC_IC_BASE_LO,    A_CP_CPC_IC_BASE_HI,    A_CP_CPC_IC_BASE_CNTL,
    A_CP_ME_CNTL,
    A_CP_MEC_CNTL,
    A_CGTT_CPF_CLK_CTRL,
    A_CGTT_CPC_CLK_CTRL,
    /* ABI 26: registradores golden */
    A_GRBM_GFX_INDEX,
    A_CP_PFP_LOAD_CONTROL,
    A_CP_ROQ1_THRESHOLDS,
    A_CP_ROQ2_THRESHOLDS,
    A_CP_ROQ3_THRESHOLDS,
    A_CP_STQ_THRESHOLDS,
    A_CP_MEQ_THRESHOLDS,
    A_CP_MAX_CONTEXT,
    A_CP_DEVICE_ID,
    A_CP_CONTEXT_CNTL,
    A_CP_SD_CNTL,
    A_CP_COHER_START_DELAY,
    A_CP_DMA_CNTL,
    A_GE_FAST_CLKS
};

C_ASSERT(RTL_NUMBER_OF(g_CpTxnRegs) == BC250DBG_CP_TXN_REG_COUNT);


static BOOLEAN BcCpTxnCovers(ULONG Offset)
{
    ULONG i;
    for (i = 0; i < BC250DBG_CP_TXN_REG_COUNT; i++) {
        if (g_CpTxnRegs[i] == Offset) return TRUE;
    }
    return FALSE;
}

static BOOLEAN BcCpGoldenAllOnesIsValid(ULONG Offset)
{
    ULONG i;
    for (i = 0; i < BC250DBG_CP_GOLDEN_REG_COUNT; i++) {
        if (g_CpGoldenRegs[i].Offset == Offset) {
            return g_CpGoldenRegs[i].AllOnesIsValid;
        }
    }
    return FALSE;
}

/* Duas leituras iguais eliminam contador/estado volatil; 0xFFFFFFFF reprova,
 * exceto no SQ_LDS_CLK_CTRL onde esse e o valor legitimo medido no Linux e o
 * proprio valor pedido pela tabela golden. Nenhuma escrita ocorre aqui. */
static BOOLEAN BcCpGoldenPreflight(BC250DBG_CP_UCODE_IO *Io)
{
    ULONG i;

    Io->RegisterCount = BC250DBG_CP_GOLDEN_AUDIT_REG_COUNT;
    for (i = 0; i < BC250DBG_CP_GOLDEN_AUDIT_REG_COUNT; i++) {
        ULONG first = BcRegReadRaw(g_CpGoldenAuditRegs[i]);
        ULONG second = BcRegReadRaw(g_CpGoldenAuditRegs[i]);
        BOOLEAN ok = (first == second) &&
                     (first != BC_REG_NO_RESPONSE ||
                      BcCpGoldenAllOnesIsValid(g_CpGoldenAuditRegs[i]));

        Io->Offsets[i] = g_CpGoldenAuditRegs[i];
        Io->Values[i] = second;
        if (!ok) {
            if (Io->Mismatches < BC250DBG_CP_TXN_BAD_SAMPLES) {
                Io->BadOffset[Io->Mismatches] = g_CpGoldenAuditRegs[i];
                Io->BadGot[Io->Mismatches] = second;
                Io->BadWant[Io->Mismatches] = first;
            }
            Io->Mismatches++;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP golden preflight: 0x%05X nao responde ou e instavel "
                      "(0x%08X / 0x%08X)",
                      g_CpGoldenAuditRegs[i], first, second);
        }
    }
    return (Io->Mismatches == 0);
}

/* Estado do CP, lido em toda operacao. Barato, e sem isto uma medicao nao diz
 * em que condicao o bloco estava quando ela aconteceu. */
static VOID BcCpFillState(BC250DBG_CP_UCODE_IO *Io)
{
    Io->CpMeCntl      = BcRegReadRaw(A_CP_ME_CNTL);
    Io->CpStat        = BcRegReadRaw(A_CP_STAT);
    Io->GrbmStatus    = BcRegReadRaw(BC250_REG_GRBM_STATUS);
    Io->GrbmStatus2   = BcRegReadRaw(A_GRBM_STATUS2);
    Io->PfpInstrPntr  = BcRegReadRaw(A_CP_PFP_INSTR_PNTR);
    Io->MeInstrPntr   = BcRegReadRaw(A_CP_ME_INSTR_PNTR);
    Io->CeInstrPntr   = BcRegReadRaw(A_CP_CE_INSTR_PNTR);
    Io->Mec1InstrPntr = BcRegReadRaw(A_CP_MEC1_INSTR_PNTR);
    /* ABI 26: Status da Jump Table nas microengines */
    Io->PfpJtStat     = BcRegReadRaw(A_CP_PFP_JT_STAT);
    Io->CeJtStat      = BcRegReadRaw(A_CP_CE_JT_STAT);
    Io->MecJtStat     = BcRegReadRaw(A_CP_MEC_JT_STAT);
}

static VOID BcCpFillEngineRegs(BC250DBG_CP_UCODE_IO *Io, const BC_CP_ENGINE_REGS *e)
{
    Io->RegBaseLo   = BcRegReadRaw(e->IcBaseLo);
    Io->RegBaseHi   = BcRegReadRaw(e->IcBaseHi);
    Io->RegBaseCntl = BcRegReadRaw(e->IcBaseCntl);
    Io->RegOpCntl   = BcRegReadRaw(e->IcOpCntl);
    Io->RegUcodeAddr = e->UcodeAddr;   /* endereco, nao valor: a porta nao e lida */
    Io->RegUcodeData = e->UcodeData;
}

/* Uma escrita registrada: antes, querido, depois — os tres LIDOS do hardware.
 *
 * WriteOnly = TRUE para registrador cuja leitura nao vale. Nesse caso o readback
 * e registrado como informacao (vai ser 0xFFFFFFFF) e a funcao devolve TRUE,
 * porque exigir confirmacao de quem nao pode confirmar bloquearia o caminho que
 * funciona no Linux.
 *
 * O que NUNCA acontece: preencher WrAfter com o valor querido. Isso ja existiu
 * neste arquivo e fabricou um "readback exato" que custou dias de hipoteses
 * sobre estado de GPU. */
static BOOLEAN BcCpWriteEx(BC250DBG_CP_UCODE_IO *Io, ULONG Offset, ULONG Value,
                           BOOLEAN WriteOnly)
{
    ULONG before, after;

    if (Io->WriteCount >= BC250DBG_CP_MAX_WRITES) {
        return FALSE;
    }
    before = BcRegReadRaw(Offset);
    BcRegWriteRaw(Offset, Value);
    after = BcRegReadRaw(Offset);

    Io->WrOffset[Io->WriteCount] = Offset;
    Io->WrBefore[Io->WriteCount] = before;
    Io->WrWanted[Io->WriteCount] = Value;
    Io->WrAfter[Io->WriteCount]  = after;
    Io->WrWriteOnly[Io->WriteCount] = WriteOnly ? 1u : 0u;
    Io->WriteCount++;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
              "CP: 0x%05X 0x%08X -> 0x%08X (queria 0x%08X)%s",
              Offset, before, after, Value,
              WriteOnly ? "  [write-only: readback nao confirma]"
                        : ((after == Value) ? "" : "  DIVERGENCIA"));

    if (WriteOnly) {
        return TRUE;
    }
    return (after == Value);
}

static BOOLEAN BcCpWriteChecked(BC250DBG_CP_UCODE_IO *Io, ULONG Offset, ULONG Value)
{
    return BcCpWriteEx(Io, Offset, Value, FALSE);
}

NTSTATUS BcCpUcode(BC250DBG_CP_UCODE_IO *Io, const ULONG *Payload, ULONG PayloadCount)
{
    const BC_CP_ENGINE_REGS *e;
    NTSTATUS status;
    ULONG i;

    if (Io == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    /* Zera so a metade de saida, preservando entrada.
     *
     * GpuAddr E A FRONTEIRA. Campo de entrada acrescentado DEPOIS dela chega
     * preenchido do user mode e e zerado aqui antes da primeira leitura — sem
     * erro, sem aviso, sem divergencia de readback. Foi assim que as ABIs 21, 23
     * e 24 perderam tres ensaios: 'cp icbase -va' escreveu o fisico, o A/B do
     * EXE_DISABLE comparou o bit em 0 contra o bit em 0, e o pulso de reset
     * nunca assertou.
     *
     * As C_ASSERT no ABI transformam a reincidencia em erro de compilacao. */
    RtlZeroMemory((PUCHAR)Io + FIELD_OFFSET(BC250DBG_CP_UCODE_IO, GpuAddr),
                  sizeof(*Io) - FIELD_OFFSET(BC250DBG_CP_UCODE_IO, GpuAddr));
    Io->Size = sizeof(*Io);

    if (g_Dev.Mmio == NULL) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_DEVICE_NOT_READY;
    }
    /* SNAPSHOT e VERIFY sao leitura pura: guardam ou comparam valores sem
     * escrever nada. Exigir armamento para observar contraria a disciplina do
     * projeto, e obrigaria a linha de base de um ensaio a armar antes de medir.
     * Todo o resto escreve, e continua exigindo armamento. */
    if (Io->Mode != BC250DBG_CP_MODE_TXN_SNAP &&
        Io->Mode != BC250DBG_CP_MODE_TXN_VERIFY &&
        Io->Mode != BC250DBG_CP_MODE_GOLDEN_PREFLIGHT &&
        g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        Io->Status = BC250DBG_OPS_DENIED;
        return STATUS_ACCESS_DENIED;
    }
    if (Io->Mode == BC250DBG_CP_MODE_STAGE ||
        Io->Mode == BC250DBG_CP_MODE_ICINVAL ||
        Io->Mode == BC250DBG_CP_MODE_ICBASE ||
        Io->Mode == BC250DBG_CP_MODE_JT ||
        (Io->Mode == BC250DBG_CP_MODE_ROLLBACK && Io->Engine != 0xFFFFFFFFu)) {
        if (Io->Engine >= BC250DBG_CP_ENGINE_COUNT) {
            Io->Status = BC250DBG_OPS_RANGE;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP: engine %u nao existe (0=pfp, 1=me, 2=ce)", Io->Engine);
            return STATUS_SUCCESS;
        }
        e = &g_CpEngines[Io->Engine];
    } else {
        e = &g_CpEngines[0];
    }

    status = BcVramMap();
    if (!NT_SUCCESS(status)) {
        Io->Status = BC250DBG_OPS_DENIED;
        return status;
    }

    BcCpFillState(Io);
    if (e != NULL) {
        BcCpFillEngineRegs(Io, e);
    }
    Io->GpuAddr = BC250_VRAM_GPU_BASE + (ULONGLONG)Io->VramOffset;
    if (Io->Mode >= BC250DBG_CP_MODE_GOLDEN_INIT &&
        Io->Mode <= BC250DBG_CP_MODE_GOLDEN_PREFLIGHT) {
        Io->RegisterCount = BC250DBG_CP_GOLDEN_AUDIT_REG_COUNT;
        for (i = 0; i < BC250DBG_CP_GOLDEN_AUDIT_REG_COUNT; i++) {
            Io->Offsets[i] = g_CpGoldenAuditRegs[i];
        }
    } else {
        Io->RegisterCount = BC250DBG_CP_TXN_REG_COUNT;
        for (i = 0; i < BC250DBG_CP_TXN_REG_COUNT; i++) {
            Io->Offsets[i] = g_CpTxnRegs[i];
        }
    }

    switch (Io->Mode) {

    /* ---------------- transacao dos registradores do CP ---------------- */
    case BC250DBG_CP_MODE_TXN_SNAP:
        for (i = 0; i < BC250DBG_CP_TXN_REG_COUNT; i++) {
            g_Dev.CpTxnValues[i] = BcRegReadRaw(g_CpTxnRegs[i]);
            Io->Values[i] = g_Dev.CpTxnValues[i];
        }
        g_Dev.CpTxnValid = TRUE;
        Io->SnapshotPresent = 1;
        Io->Status = BC250DBG_OPS_OK;
        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
                  "CP snapshot: %u registradores guardados",
                  BC250DBG_CP_TXN_REG_COUNT);
        return STATUS_SUCCESS;

    case BC250DBG_CP_MODE_TXN_VERIFY:
    case BC250DBG_CP_MODE_TXN_RESTORE: {
        BOOLEAN restore = (Io->Mode == BC250DBG_CP_MODE_TXN_RESTORE);

        if (!g_Dev.CpTxnValid) {
            Io->SnapshotPresent = 0;
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP: nenhum snapshot aberto; rode 'cp txn snapshot' antes");
            return STATUS_SUCCESS;
        }
        Io->SnapshotPresent = 1;
        for (i = 0; i < BC250DBG_CP_TXN_REG_COUNT; i++) {
            ULONG got;

            if (restore) {
                BcRegWriteRaw(g_CpTxnRegs[i], g_Dev.CpTxnValues[i]);
            }
            got = BcRegReadRaw(g_CpTxnRegs[i]);
            Io->Values[i] = got;
            if (got == g_Dev.CpTxnValues[i]) {
                if (restore) Io->Restored++;
            } else {
                if (Io->Mismatches < BC250DBG_CP_TXN_BAD_SAMPLES) {
                    Io->BadOffset[Io->Mismatches] = g_CpTxnRegs[i];
                    Io->BadGot[Io->Mismatches] = got;
                    Io->BadWant[Io->Mismatches] = g_Dev.CpTxnValues[i];
                }
                Io->Mismatches++;
            }
        }
        BcCpFillState(Io);
        Io->Status = (Io->Mismatches == 0) ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;
        BcLogText(Io->Mismatches ? BC250DBG_LVL_ERROR : BC250DBG_LVL_INFO,
                  BC250DBG_SUB_GC, "CP txn %s: %u restaurados, %u divergencias",
                  restore ? "restore" : "verify", Io->Restored, Io->Mismatches);
        return STATUS_SUCCESS;
    }

    /* ---------------- ucode para a VRAM ---------------- */
    case BC250DBG_CP_MODE_STAGE: {
        ULONG bytes;

        if (Payload == NULL || PayloadCount == 0 ||
            PayloadCount > BC250DBG_CP_MAX_UCODE_DWORDS ||
            PayloadCount != Io->DwordCount) {
            Io->Status = BC250DBG_OPS_RANGE;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP stage: payload de %u dwords invalido (declarado %u, "
                      "maximo %u)", PayloadCount, Io->DwordCount,
                      BC250DBG_CP_MAX_UCODE_DWORDS);
            return STATUS_SUCCESS;
        }
        /* IC_BASE_LO usa endereco & 0xFFFFF000: o ucode tem de estar alinhado a
         * 4 KB, senao a cache buscaria de um lugar diferente do que a CPU
         * escreveu — e a divergencia apareceria como ucode corrompido. */
        if ((Io->VramOffset & 0xFFFu) != 0) {
            Io->Status = BC250DBG_OPS_RANGE;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP stage: offset 0x%X precisa de alinhamento de 4 KB",
                      Io->VramOffset);
            return STATUS_SUCCESS;
        }
        bytes = PayloadCount * sizeof(ULONG);
        if ((ULONGLONG)Io->VramOffset + bytes > g_Dev.VramLength) {
            Io->Status = BC250DBG_OPS_RANGE;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP stage: 0x%X + %u bytes termina em 0x%llX, passa da "
                      "janela de VRAM (0x%X)", Io->VramOffset, bytes,
                      (ULONGLONG)Io->VramOffset + bytes, g_Dev.VramLength);
            return STATUS_SUCCESS;
        }
        if (BcRangesOverlap(Io->VramOffset, bytes, BC250_SDMA_RING_VRAM_DEFAULT, 4096) ||
            BcRangesOverlap(Io->VramOffset, bytes, BC250_SDMA_RPTR_WB_OFFSET, 8) ||
            BcRangesOverlap(Io->VramOffset, bytes, BC250_SDMA_WPTR_SHADOW_OFFSET, 8) ||
            BcRangesOverlap(Io->VramOffset, bytes, BC250_SDMA_FENCE_VRAM_DEFAULT, 4)) {
            Io->Status = BC250DBG_OPS_RANGE;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP stage: 0x%X/%u bytes sobrepoe area da SDMA",
                      Io->VramOffset, bytes);
            return STATUS_SUCCESS;
        }
        if (Io->Engine >= BC250DBG_CP_ENGINE_COUNT) {
            Io->Status = BC250DBG_OPS_RANGE;
            return STATUS_SUCCESS;
        }
        if (g_Dev.CpUcodeBackup[Io->Engine] != NULL) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP stage: snapshot anterior aberto para engine %s; rode o rollback antes",
                      e->Nome);
            return STATUS_SUCCESS;
        }

        g_Dev.CpUcodeBackup[Io->Engine] = (PULONG)ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, 'cUcB');
        if (g_Dev.CpUcodeBackup[Io->Engine] == NULL) {
            Io->Status = BC250DBG_OPS_DENIED;
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        for (i = 0; i < PayloadCount; i++) {
            g_Dev.CpUcodeBackup[Io->Engine][i] = BcVramReadRaw(Io->VramOffset + i * sizeof(ULONG));
        }
        g_Dev.CpUcodeBackupDwords[Io->Engine] = PayloadCount;
        g_Dev.CpUcodeOffset[Io->Engine] = Io->VramOffset;
        Io->SnapshotPresent = 1;
        Io->SnapshotDwords = PayloadCount;

        for (i = 0; i < PayloadCount; i++) {
            BcVramWriteRaw(Io->VramOffset + i * sizeof(ULONG), Payload[i]);
            Io->DwordsWritten++;
            if (i < BC250DBG_CP_UCODE_SAMPLES) Io->Sample[i] = Payload[i];
        }
        KeMemoryBarrier();
        BcHdpReadCacheInvalidate();

        /* Conferencia dword a dword. Sem isto, "ucode carregado" seria uma
         * afirmacao sobre o que a CPU tentou fazer. */
        for (i = 0; i < PayloadCount; i++) {
            ULONG got = BcVramReadRaw(Io->VramOffset + i * sizeof(ULONG));
            if (got != Payload[i]) {
                if (Io->Mismatches == 0) {
                    Io->FirstMismatchIndex = i;
                    Io->FirstMismatchGot = got;
                    Io->FirstMismatchWant = Payload[i];
                }
                Io->Mismatches++;
            }
        }
        BcCpFillState(Io);
        Io->Status = (Io->Mismatches == 0) ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;
        BcLogText(Io->Mismatches ? BC250DBG_LVL_ERROR : BC250DBG_LVL_INFO,
                  BC250DBG_SUB_GC,
                  "CP stage %s: %u dwords em 0x%X (GPU 0x%llX), %u divergencias",
                  e->Nome, Io->DwordsWritten, Io->VramOffset, Io->GpuAddr,
                  Io->Mismatches);
        return STATUS_SUCCESS;
    }

    case BC250DBG_CP_MODE_ROLLBACK: {
        ULONG k, startEng = 0, endEng = BC250DBG_CP_ENGINE_COUNT;
        ULONG totalDwords = 0;

        if (Io->Engine < BC250DBG_CP_ENGINE_COUNT) {
            startEng = Io->Engine;
            endEng = Io->Engine + 1;
        }

        Io->SnapshotPresent = 0;
        for (k = startEng; k < endEng; k++) {
            if (g_Dev.CpUcodeBackup[k] != NULL) {
                ULONG dwords = g_Dev.CpUcodeBackupDwords[k];
                ULONG off = g_Dev.CpUcodeOffset[k];
                Io->SnapshotPresent = 1;
                Io->VramOffset = off;
                totalDwords += dwords;

                for (i = 0; i < dwords; i++) {
                    BcVramWriteRaw(off + i * sizeof(ULONG), g_Dev.CpUcodeBackup[k][i]);
                }
                KeMemoryBarrier();
                BcHdpReadCacheInvalidate();
                for (i = 0; i < dwords; i++) {
                    ULONG got = BcVramReadRaw(off + i * sizeof(ULONG));
                    if (got != g_Dev.CpUcodeBackup[k][i]) {
                        if (Io->Mismatches == 0) {
                            Io->FirstMismatchIndex = i;
                            Io->FirstMismatchGot = got;
                            Io->FirstMismatchWant = g_Dev.CpUcodeBackup[k][i];
                        }
                        Io->Mismatches++;
                    }
                }
                ExFreePoolWithTag(g_Dev.CpUcodeBackup[k], 'cUcB');
                g_Dev.CpUcodeBackup[k] = NULL;
                g_Dev.CpUcodeBackupDwords[k] = 0;
                g_Dev.CpUcodeOffset[k] = 0;
            }
        }

        if (!Io->SnapshotPresent) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP rollback: nenhum snapshot de ucode aberto");
            return STATUS_SUCCESS;
        }

        Io->SnapshotDwords = totalDwords;
        BcCpFillState(Io);
        Io->Status = (Io->Mismatches == 0) ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;
        BcLogText(Io->Mismatches ? BC250DBG_LVL_ERROR : BC250DBG_LVL_INFO,
                  BC250DBG_SUB_GC, "CP rollback: %u dwords, %u divergencias",
                  totalDwords, Io->Mismatches);
        return STATUS_SUCCESS;
    }

    /* ---------------- invalidar a cache de instrucoes ---------------- */
    case BC250DBG_CP_MODE_ICINVAL: {
        ULONG tmp;

        /* Escrever INVALIDATE_CACHE e ACAO: por isso IC_OP_CNTL fica fora da
         * transacao. O que este modo entrega e a confirmacao do hardware pelo
         * bit INVALIDATE_CACHE_COMPLETE, nao a escrita em si. */
        Io->InvConfirmavel = BcRegResponds(e->IcOpCntl, &Io->OpCntlBefore) ? 1u : 0u;
        if (!Io->InvConfirmavel) {
            /* Nao recusa: o amdgpu faz esta invalidacao e o CP funciona. O que
             * nao se pode fazer e CHAMAR DE CONFIRMADA — 0xFFFFFFFF satisfaz o
             * bit COMPLETE, e foi assim que duas sessoes anunciaram confirmacao
             * sem uma unica leitura valida. */
            BcRegWriteRaw(e->IcOpCntl, F_CP_IC_OP_CNTL__INVALIDATE_CACHE);
            KeStallExecutionProcessor(50);
            Io->OpCntlAfterWrite = BcRegReadRaw(e->IcOpCntl);
            Io->OpCntlAfterWait  = Io->OpCntlAfterWrite;
            Io->PollIterations = 0;
            Io->TimedOut = 0;
            BcCpFillState(Io);
            BcCpFillEngineRegs(Io, e);
            Io->Status = BC250DBG_OPS_OK;
            BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_GC,
                      "CP icinval %s: invalidacao PEDIDA, NAO CONFIRMAVEL. "
                      "IC_OP_CNTL em 0x%05X e write-only; o bit COMPLETE nao "
                      "pode ser observado. Isto nao prova nada.",
                      e->Nome, e->IcOpCntl);
            return STATUS_SUCCESS;
        }
        /* Daqui para baixo, o registrador RESPONDE: a espera por COMPLETE vale,
         * e o resultado pode ser chamado de confirmado. */
        tmp = Io->OpCntlBefore | F_CP_IC_OP_CNTL__INVALIDATE_CACHE;
        BcRegWriteRaw(e->IcOpCntl, tmp);
        Io->OpCntlAfterWrite = BcRegReadRaw(e->IcOpCntl);

        for (i = 0; i < 50000u; i++) {
            Io->OpCntlAfterWait = BcRegReadRaw(e->IcOpCntl);
            if (Io->OpCntlAfterWait & F_CP_IC_OP_CNTL__INVALIDATE_CACHE_COMPLETE) {
                break;
            }
            KeStallExecutionProcessor(1);
        }
        Io->PollIterations = i;
        Io->TimedOut = (Io->OpCntlAfterWait & F_CP_IC_OP_CNTL__INVALIDATE_CACHE_COMPLETE)
                     ? 0u : 1u;
        BcCpFillState(Io);
        BcCpFillEngineRegs(Io, e);
        Io->Status = Io->TimedOut ? BC250DBG_OPS_TIMEOUT : BC250DBG_OPS_OK;
        BcLogText(Io->TimedOut ? BC250DBG_LVL_ERROR : BC250DBG_LVL_INFO,
                  BC250DBG_SUB_GC,
                  "CP icinval %s: OP_CNTL 0x%08X -> 0x%08X -> 0x%08X, "
                  "%u iteracoes%s", e->Nome, Io->OpCntlBefore,
                  Io->OpCntlAfterWrite, Io->OpCntlAfterWait, Io->PollIterations,
                  Io->TimedOut ? "  TIMEOUT" : "");
        return STATUS_SUCCESS;
    }

    /* ---------------- apontar a cache para o ucode ---------------- */
    /* ---------------- ungate do clock do CPF ---------------- */
    /* ---------------- CP_ME_CNTL por campo (ABI 24) ---------------- */
    /* Mascaras conferidas no gc_10_1_0_sh_mask.h completo, trazido do OldDBG.
     * Nao sao deducao: sao os campos do header autoritativo. */
    case BC250DBG_CP_MODE_PIPE_RESET: {
        ULONG antes, querido, depois;
        const ULONG HALTS = 0x15000000u;   /* CE|PFP|ME _HALT, bits 24/26/28 */
        const ULONG RESETS = 0x003F0000u;  /* CE/PFP/ME _PIPE0/1_RESET, 16..21 */

        if (!g_Dev.CpTxnValid) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP pipereset: RECUSADO, nenhum snapshot de transacao aberto.");
            return STATUS_SUCCESS;
        }
        if (!BcCpTxnCovers(A_CP_ME_CNTL)) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP pipereset: CP_ME_CNTL fora da transacao.");
            return STATUS_SUCCESS;
        }

        antes = BcRegReadRaw(A_CP_ME_CNTL);
        /* Os HALT sao MANTIDOS nos dois sentidos: o reset e das pipes, e tirar
         * engine do HALT aqui seria acao disfarcada de configuracao. */
        querido = Io->PipeResetAssert ? (HALTS | RESETS) : HALTS;
        BcCpWriteChecked(Io, A_CP_ME_CNTL, querido);
        depois = BcRegReadRaw(A_CP_ME_CNTL);

        BcCpFillState(Io);
        Io->Status = (depois == querido) ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;
        BcLogText((depois == querido) ? BC250DBG_LVL_INFO : BC250DBG_LVL_ERROR,
                  BC250DBG_SUB_GC,
                  "CP pipereset %s: CP_ME_CNTL 0x%08X -> 0x%08X (queria 0x%08X)",
                  Io->PipeResetAssert ? "ASSERT" : "DEASSERT",
                  antes, depois, querido);
        return STATUS_SUCCESS;
    }

    case BC250DBG_CP_MODE_ICACHE_MECNTL: {
        ULONG antes, querido, depois, limpo;
        const ULONG INVAL = 0x00000150u;   /* CE|PFP|ME _INVALIDATE_ICACHE */

        if (!g_Dev.CpTxnValid || !BcCpTxnCovers(A_CP_ME_CNTL)) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP icinval-mecntl: RECUSADO, transacao ausente ou nao cobre "
                      "CP_ME_CNTL.");
            return STATUS_SUCCESS;
        }

        antes = BcRegReadRaw(A_CP_ME_CNTL);
        querido = antes | INVAL;
        BcCpWriteChecked(Io, A_CP_ME_CNTL, querido);
        depois = BcRegReadRaw(A_CP_ME_CNTL);

        /* Os bits de invalidacao PODEM ser auto-limpantes: e um pedido, nao um
         * estado. NAO afirmo qual dos dois e — relato o que a leitura devolveu e
         * deixo o ensaio decidir. Depois devolvo o registrador ao valor de
         * entrada, para nao deixar pedido pendurado. */
        BcRegWriteRaw(A_CP_ME_CNTL, antes);
        limpo = BcRegReadRaw(A_CP_ME_CNTL);

        Io->OpCntlBefore = antes;
        Io->OpCntlAfterWrite = depois;
        Io->OpCntlAfterWait = limpo;
        /* Confirmavel = o registrador devolveu os bits pedidos na leitura logo
         * apos a escrita. Se nao devolveu, ou eles se auto-limpam, ou o pedido
         * nao pegou — e essas duas coisas NAO se distinguem por readback. */
        Io->InvConfirmavel = ((depois & INVAL) == INVAL) ? 1u : 0u;

        BcCpFillState(Io);
        Io->Status = (limpo == antes) ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;
        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
                  "CP icinval-mecntl: 0x%08X -> 0x%08X (pediu 0x%08X) -> 0x%08X. "
                  "bits lidos de volta: %s",
                  antes, depois, querido, limpo,
                  Io->InvConfirmavel ? "sim" : "nao (auto-limpante ou nao aceito)");
        return STATUS_SUCCESS;
    }

    case BC250DBG_CP_MODE_CLOCKS: {
        ULONG antes, depois;

        /* Mesma exigencia de todas as escritas do CP: sem caminho de volta
         * provado, nao se escreve. */
        if (!g_Dev.CpTxnValid) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP clocks: RECUSADO, nenhum snapshot de transacao aberto.");
            return STATUS_SUCCESS;
        }
        if (!BcCpTxnCovers(A_CGTT_CPF_CLK_CTRL)) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP clocks: CGTT_CPF_CLK_CTRL fora da transacao; o rollback "
                      "nao o desfaria");
            return STATUS_SUCCESS;
        }
        /* Este registrador RESPONDE. Diferente dos IC_BASE, aqui o readback vale
         * como confirmacao, e por isso a escrita passa por BcCpWriteChecked. */
        if (!BcRegResponds(A_CGTT_CPF_CLK_CTRL, NULL)) {
            Io->Status = BC250DBG_OPS_DENIED;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP clocks: CGTT_CPF_CLK_CTRL em 0x%05X nao responde. "
                      "Nao escrevo em endereco que nao decodifica.",
                      A_CGTT_CPF_CLK_CTRL);
            return STATUS_SUCCESS;
        }

        antes = BcRegReadRaw(A_CGTT_CPF_CLK_CTRL);
        /* 0xF8000100 e o valor MEDIDO no Linux com o CP executando, nao um valor
         * inventado nem copiado de outra geracao: os cinco SOFT_OVERRIDE_* em 1
         * sobre o mesmo 0x100 que este sistema ja tem. */
        BcCpWriteChecked(Io, A_CGTT_CPF_CLK_CTRL, 0xF8000100u);
        depois = BcRegReadRaw(A_CGTT_CPF_CLK_CTRL);

        BcCpFillState(Io);
        Io->Status = (depois == 0xF8000100u) ? BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;
        BcLogText((depois == 0xF8000100u) ? BC250DBG_LVL_INFO : BC250DBG_LVL_ERROR,
                  BC250DBG_SUB_GC,
                  "CP clocks: CGTT_CPF_CLK_CTRL 0x%08X -> 0x%08X (queria 0xF8000100, "
                  "Linux com o CP vivo). CPF e o fetcher da microengine.",
                  antes, depois);
        return STATUS_SUCCESS;
    }

    case BC250DBG_CP_MODE_GOLDEN_PREFLIGHT:
        /* Somente leitura. Inclui os 34 registros golden e os dois GPA que a
         * fase posterior do direct load precisara cobrir. */
        Io->Status = BcCpGoldenPreflight(Io) ?
                     BC250DBG_OPS_OK : BC250DBG_OPS_MISMATCH;
        BcCpFillState(Io);
        BcLogText(Io->Status == BC250DBG_OPS_OK ?
                  BC250DBG_LVL_INFO : BC250DBG_LVL_ERROR,
                  BC250DBG_SUB_GC,
                  "CP golden preflight: %u registros, %u falhas; zero escrita",
                  Io->RegisterCount, Io->Mismatches);
        return STATUS_SUCCESS;

    case BC250DBG_CP_MODE_GOLDEN_INIT:
        /* Continua fail-closed. GRBM_GFX_INDEX=0xE0000000 faz broadcast; um
         * snapshot unico nao preserva valores possivelmente diferentes por
         * SE/SH/instancia. Liberar RMW antes de snapshotar a topologia inteira
         * fabricaria rollback. */
        Io->Status = BC250DBG_OPS_BADOP;
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "CP golden apply: QUARENTENA, rollback por SE/SH/instancia ausente");
        return STATUS_SUCCESS;

    case BC250DBG_CP_MODE_ICBASE: {
        ULONG cntl;
        /* ABI 21: se o chamador entregou um endereco explicito, e ele que vai
         * para o IC_BASE. O caso de uso e apontar para uma VA que a page table
         * do VMID 0 resolve, reproduzindo o que o amdgpu faz com um endereco de
         * GTT. Sem override, o comportamento e o de antes: fisico derivado. */
        ULONGLONG addr = Io->OverrideValid ? Io->OverrideGpuAddr : Io->GpuAddr;

        if (!g_Dev.CpTxnValid) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP icbase: RECUSADO, nenhum snapshot de transacao aberto. "
                      "Sem caminho de volta provado nao ha escrita em "
                      "registrador do CP.");
            return STATUS_SUCCESS;
        }
        if ((addr & 0xFFFull) != 0) {
            Io->Status = BC250DBG_OPS_RANGE;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP icbase: endereco 0x%llX precisa de alinhamento de 4 KB "
                      "(o registrador so carrega os bits 31:12)",
                      addr);
            return STATUS_SUCCESS;
        }
        /* O registrador guarda 31:12 de LO e 63:32 de HI. Bits 11:0 sao
         * descartados pelo hardware; ja recusamos acima. */
        /* A guarda BcRegResponds() NAO se aplica aqui.
         *
         * IC_BASE_* le 0xFFFFFFFF nos dois sistemas, e no Linux o CP FUNCIONA
         * com essa mesma leitura. Sao write-only: a escrita vale, a leitura nao.
         * Recusar por nao responder bloquearia exatamente o caminho que
         * funciona.
         *
         * O preco esta declarado abaixo, escrita por escrita, no campo
         * WrWriteOnly: aqui a prova sai do readback e vai para o ponteiro de
         * instrucao da microengine andar. */
        if (!BcCpTxnCovers(e->IcBaseLo) || !BcCpTxnCovers(e->IcBaseHi) ||
            !BcCpTxnCovers(e->IcBaseCntl)) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP icbase: registrador fora da transacao; o rollback nao "
                      "o desfaria");
            return STATUS_SUCCESS;
        }

        /* Ordem do upstream: CNTL primeiro, depois LO e HI.
         * Os registradores de IC_BASE do CP sao portas de escrita para o controlador
         * de cache de instrucoes da microengine: nao possuem readback no MMIO
         * (leem 0xFFFFFFFF no barramento). Construimos o valor limpo com ADDRESS_CLAMP=1. */
        cntl = 0;
        cntl = BC_SETF(cntl, F_CP_IC_BASE_CNTL__VMID, 0, 0);
        cntl = BC_SETF(cntl, F_CP_IC_BASE_CNTL__CACHE_POLICY, 24, 0);
        /* ABI 23: normalmente 0, como o upstream. O teste diferencial usa 1 para
         * medir se estas escritas produzem QUALQUER efeito observavel. */
        cntl = BC_SETF(cntl, F_CP_IC_BASE_CNTL__EXE_DISABLE, 23,
                       Io->ExeDisable ? 1 : 0);
        cntl = BC_SETF(cntl, F_CP_IC_BASE_CNTL__ADDRESS_CLAMP, 4, 1);

        /* CORRECAO DE 2026-08-24: aqui WrBefore era 0xFFFFFFFF de constante e
         * WrAfter recebia o valor QUERIDO, sem ler o hardware. A saida entao
         * mostrava "0xFFFFFFFF -> <querido> (queria <querido>)" e parecia
         * confirmada por readback quando nada havia sido lido.
         *
         * Isso EXPLICA a anomalia das 16:19, que ficou dias sem explicacao: o
         * "readback exato" daquela execucao nao veio do silicio, veio deste
         * campo. Nenhuma hipotese sobre estado da GPU precisava existir.
         *
         * Agora before e after saem de leitura de verdade — vao ler 0xFFFFFFFF,
         * e e isso que deve aparecer — e WrWriteOnly declara que a confirmacao
         * e impossivel neste registrador. */
        BcCpWriteEx(Io, e->IcBaseCntl, cntl, TRUE);
        BcCpWriteEx(Io, e->IcBaseLo, (ULONG)(addr & 0xFFFFF000ull), TRUE);
        BcCpWriteEx(Io, e->IcBaseHi, (ULONG)(addr >> 32), TRUE);

        BcCpFillState(Io);
        BcCpFillEngineRegs(Io, e);
        Io->Status = BC250DBG_OPS_OK;
        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
                  "CP icbase %s: endereco 0x%llX (%s), 3 escritas WRITE-ONLY "
                  "(CNTL=0x%08X, LO=0x%08X, HI=0x%08X). Nao ha readback: a prova "
                  "e o ponteiro de instrucao andar depois do unhalt.",
                  e->Nome, addr,
                  Io->OverrideValid ? "VA explicita, VMID 0" : "fisico derivado",
                  cntl, (ULONG)(addr & 0xFFFFF000ull),
                  (ULONG)(addr >> 32));
        return STATUS_SUCCESS;
    }

    /* ---------------- jump table ---------------- */
    case BC250DBG_CP_MODE_JT: {
        if (!g_Dev.CpTxnValid) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP jt: RECUSADO, nenhum snapshot de transacao aberto");
            return STATUS_SUCCESS;
        }
        if (Payload == NULL || PayloadCount == 0 ||
            PayloadCount > BC250DBG_CP_MAX_JT_DWORDS ||
            PayloadCount != Io->DwordCount) {
            Io->Status = BC250DBG_OPS_RANGE;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP jt: %u dwords invalido (declarado %u, maximo %u)",
                      PayloadCount, Io->DwordCount, BC250DBG_CP_MAX_JT_DWORDS);
            return STATUS_SUCCESS;
        }

        /* Aqui NAO cabe guarda de responsividade, pelo mesmo motivo do icbase.
         *
         * Medido no dump do Linux de 2026-08-23 20:01, com o CP comprovadamente
         * funcionando naquela maquina:
         *
         *   CP_PFP_UCODE_ADDR  dw 0x5814  seg 1  0x3E050  0xFFFFFFFF
         *   CP_CE_UCODE_ADDR   dw 0x5818  seg 1  0x3E060  0xFFFFFFFF
         *
         * Ou seja, UCODE_ADDR le 0xFFFFFFFF nos DOIS sistemas. E write-only, nao
         * e sintoma de bloco morto. Recusar por isso recusaria exatamente o
         * caminho que funciona.
         *
         * ADDR = 0, jt em DATA, e por fim ADDR = versao. A porta DATA escreve na
         * RAM interna da microengine e nunca teve readback. O ADDR tambem nao
         * tem — as duas escritas saem marcadas como nao confirmaveis, e a prova
         * do passo inteiro se desloca para o ponteiro de instrucao andar. */
        BcCpWriteEx(Io, e->UcodeAddr, 0, TRUE);

        for (i = 0; i < PayloadCount; i++) {
            BcRegWriteRaw(e->UcodeData, Payload[i]);
            Io->DwordsWritten++;
            if (i < BC250DBG_CP_UCODE_SAMPLES) Io->Sample[i] = Payload[i];
        }

        BcCpWriteEx(Io, e->UcodeAddr, Io->UcodeVersion, TRUE);

        BcCpFillState(Io);
        BcCpFillEngineRegs(Io, e);
        Io->Status = BC250DBG_OPS_OK;
        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
                  "CP jt %s: %u dwords pela porta 0x%05X, versao 0x%X. "
                  "UCODE_ADDR e write-only (le 0xFFFFFFFF tambem no Linux com o "
                  "CP vivo): nao ha readback que confirme isto.",
                  e->Nome, Io->DwordsWritten, e->UcodeData, Io->UcodeVersion);
        return STATUS_SUCCESS;
    }

    /* ---------------- unhalt: tirar as 4 engines do HALT ---------------- */
    case BC250DBG_CP_MODE_UNHALT: {
        ULONG beforeMe, afterMe, beforeMec, afterMec;

        if (!g_Dev.CpTxnValid) {
            Io->Status = BC250DBG_OPS_BADOP;
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                      "CP unhalt: RECUSADO, nenhum snapshot de transacao aberto. "
                      "Sem caminho de volta provado nao ha unhalt.");
            return STATUS_SUCCESS;
        }

        beforeMec = BcRegReadRaw(A_CP_MEC_CNTL);
        beforeMe  = BcRegReadRaw(A_CP_ME_CNTL);
        /* Limpa MEC_ME1_HALT / MEC_ME2_HALT (0) e PFP/ME/CE_HALT (0) */
        BcRegWriteRaw(A_CP_MEC_CNTL, 0);
        BcRegWriteRaw(A_CP_ME_CNTL, 0);
        KeStallExecutionProcessor(50);
        afterMec = BcRegReadRaw(A_CP_MEC_CNTL);
        afterMe  = BcRegReadRaw(A_CP_ME_CNTL);

        Io->WrOffset[0] = A_CP_ME_CNTL;
        Io->WrBefore[0] = beforeMe;
        Io->WrWanted[0] = 0;
        Io->WrAfter[0]  = afterMe;
        Io->WrOffset[1] = A_CP_MEC_CNTL;
        Io->WrBefore[1] = beforeMec;
        Io->WrWanted[1] = 0;
        Io->WrAfter[1]  = afterMec;
        Io->WriteCount = 2;

        BcCpFillState(Io);
        Io->Status = BC250DBG_OPS_OK;
        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
                  "CP unhalt: ME_CNTL 0x%08X -> 0x%08X, MEC_CNTL 0x%08X -> 0x%08X (PFP=0x%X, ME=0x%X, CE=0x%X, MEC1=0x%X, STAT=0x%X)",
                  beforeMe, afterMe, beforeMec, afterMec,
                  Io->PfpInstrPntr, Io->MeInstrPntr, Io->CeInstrPntr, Io->Mec1InstrPntr, Io->CpStat);
        return STATUS_SUCCESS;
    }

    /* ---------------- halt: voltar as 4 engines para o HALT ---------------- */
    case BC250DBG_CP_MODE_HALT: {
        ULONG beforeMe, afterMe, beforeMec, afterMec;

        beforeMec = BcRegReadRaw(A_CP_MEC_CNTL);
        beforeMe  = BcRegReadRaw(A_CP_ME_CNTL);
        /* Reativa MEC_ME1_HALT/MEC_ME2_HALT (0x50000000) e PFP/ME/CE_HALT (0x15000000) */
        BcRegWriteRaw(A_CP_MEC_CNTL, 0x50000000u);
        BcRegWriteRaw(A_CP_ME_CNTL, 0x15000000u);
        KeStallExecutionProcessor(50);
        afterMec = BcRegReadRaw(A_CP_MEC_CNTL);
        afterMe  = BcRegReadRaw(A_CP_ME_CNTL);

        Io->WrOffset[0] = A_CP_ME_CNTL;
        Io->WrBefore[0] = beforeMe;
        Io->WrWanted[0] = 0x15000000u;
        Io->WrAfter[0]  = afterMe;
        Io->WrOffset[1] = A_CP_MEC_CNTL;
        Io->WrBefore[1] = beforeMec;
        Io->WrWanted[1] = 0x50000000u;
        Io->WrAfter[1]  = afterMec;
        Io->WriteCount = 2;

        BcCpFillState(Io);
        Io->Status = BC250DBG_OPS_OK;
        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
                  "CP halt: ME_CNTL 0x%08X -> 0x%08X, MEC_CNTL 0x%08X -> 0x%08X (PFP=0x%X, ME=0x%X, CE=0x%X, MEC1=0x%X, STAT=0x%X)",
                  beforeMe, afterMe, beforeMec, afterMec,
                  Io->PfpInstrPntr, Io->MeInstrPntr, Io->CeInstrPntr, Io->Mec1InstrPntr, Io->CpStat);
        return STATUS_SUCCESS;
    }

    /* ---------------- sample: amostrar ponteiros e status ---------------- */
    case BC250DBG_CP_MODE_SAMPLE: {
        BcCpFillState(Io);
        Io->Status = BC250DBG_OPS_OK;
        return STATUS_SUCCESS;
    }

    default:
        Io->Status = BC250DBG_OPS_BADOP;
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_GC,
                  "CP: modo %u nao existe", Io->Mode);
        return STATUS_SUCCESS;
    }
}

/* Dump somente leitura do estado de traducao do GC. A lista espelha o que
 * gfxhub_v2_0.c programa, para que a comparacao entre "o que o POST deixou" e
 * "o que o upstream exige" seja mecanica em vez de leitura a olho. */
VOID BcGcvmDump(BC250DBG_GCVM_DUMP_IO *Out)
{
    if (Out == NULL) {
        return;
    }
    RtlZeroMemory(Out, sizeof(*Out));
    Out->Size = sizeof(*Out);

    if (g_Dev.Mmio == NULL) {
        Out->Status = BC250DBG_OPS_DENIED;
        return;
    }

    Out->L2Cntl  = BcRegReadRaw(A_GCVM_L2_CNTL);
    Out->L2Cntl2 = BcRegReadRaw(A_GCVM_L2_CNTL2);
    Out->L2Cntl3 = BcRegReadRaw(A_GCVM_L2_CNTL3);
    Out->L2Cntl4 = BcRegReadRaw(A_GCVM_L2_CNTL4);
    Out->L2Cntl5 = BcRegReadRaw(A_GCVM_L2_CNTL5);

    Out->FaultCntl   = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_CNTL);
    Out->FaultCntl2  = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_CNTL2);
    Out->FaultStatus = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_STATUS);
    Out->FaultAddrLo = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_ADDR_LO32);
    Out->FaultAddrHi = BcRegReadRaw(A_GCVM_L2_PROTECTION_FAULT_ADDR_HI32);
    Out->FaultDefaultAddrLo = BcRegReadRaw(A_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_LO);
    Out->FaultDefaultAddrHi = BcRegReadRaw(A_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_HI);

    Out->Context0Cntl = BcRegReadRaw(A_GCVM_CONTEXT0_CNTL);
    Out->Context1Cntl = BcRegReadRaw(A_GCVM_CONTEXT1_CNTL);
    Out->Ctx0PtBaseLo  = BcRegReadRaw(A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO);
    Out->Ctx0PtBaseHi  = BcRegReadRaw(A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI);
    Out->Ctx0PtStartLo = BcRegReadRaw(A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO);
    Out->Ctx0PtStartHi = BcRegReadRaw(A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI);
    Out->Ctx0PtEndLo   = BcRegReadRaw(A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO);
    Out->Ctx0PtEndHi   = BcRegReadRaw(A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI);

    Out->MxL1TlbCntl = BcRegReadRaw(A_GCMC_VM_MX_L1_TLB_CNTL);
    Out->SysApertureLow  = BcRegReadRaw(A_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR);
    Out->SysApertureHigh = BcRegReadRaw(A_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR);
    Out->SysApertureDefaultLsb = BcRegReadRaw(A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB);
    Out->SysApertureDefaultMsb = BcRegReadRaw(A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB);
    Out->AgpBase = BcRegReadRaw(A_GCMC_VM_AGP_BASE);
    Out->AgpBot  = BcRegReadRaw(A_GCMC_VM_AGP_BOT);
    Out->AgpTop  = BcRegReadRaw(A_GCMC_VM_AGP_TOP);
    Out->FbLocationBase = BcRegReadRaw(A_GCMC_VM_FB_LOCATION_BASE);
    Out->FbLocationTop  = BcRegReadRaw(A_GCMC_VM_FB_LOCATION_TOP);
    Out->FbOffset       = BcRegReadRaw(A_GCMC_VM_FB_OFFSET);

    Out->IdentityApertureLowLo  = BcRegReadRaw(A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO);
    Out->IdentityApertureHighLo = BcRegReadRaw(A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO);

    /* REQ e ACK sao lidos; o upstream tambem le ACK em laco de espera, entao
     * leitura ali nao tem efeito. SEM ficou de fora: ler SEM ADQUIRE o
     * semaforo, e um dump nao pode adquirir nada. O estado do semaforo sai do
     * bit SEMAPHORE de ACK. */
    Out->InvalidateEng0Req = BcRegReadRaw(A_GCVM_INVALIDATE_ENG0_REQ);
    Out->InvalidateEng0Ack = BcRegReadRaw(A_GCVM_INVALIDATE_ENG0_ACK);

    Out->Status = BC250DBG_OPS_OK;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
              "GCVM dump: L2_CNTL 0x%X CTX0_CNTL 0x%X TLB_CNTL 0x%X FAULT_STATUS 0x%X",
              Out->L2Cntl, Out->Context0Cntl, Out->MxL1TlbCntl, Out->FaultStatus);
}

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
    ULONG cntlBefore, cntlAfter, l2Cntl;

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

/* Bloco secundario / alternativo do CP Graphics Ring (WDDM standard) */
#define A_CP_RB0_BASE_WDDM    0x0DA60u
#define A_CP_RB0_BASE_HI_WDDM 0x0DA64u
#define A_CP_RB0_CNTL_WDDM    0x0DA68u
#define A_CP_RB0_RPTR_WDDM    0x0DA6Cu
#define A_CP_RB0_WPTR_HI_WDDM 0x0DA74u
#define A_CP_RB0_WPTR_WDDM    0x0DA78u
#define A_CP_RB_ACTIVE_WDDM   0x0DA7Cu

NTSTATUS BcGfxInit(BC250DBG_GFX_INIT_IO *Io)
{
    ULONG size, vramOffset, i;
    ULONGLONG gpuAddr, rptrWbAddr, wptrPollAddr;
    ULONG meCntlBefore, rbCntl;   /* o 'after' vai direto para Io->MeCntlAfter */
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

        if (Io->GpuAddr != 0) {
            gpuAddr = Io->GpuAddr;
        } else {
            gpuAddr = BC250_VRAM_GPU_BASE + (ULONGLONG)vramOffset;
        }
    }

    rptrWbAddr = gpuAddr + (ULONGLONG)size; /* logo apos o anel */
    wptrPollAddr = gpuAddr + (ULONGLONG)size + 0x100ull;

    /* 0. Garante que a Cache L2 e Abertura de Sistema do GFXHUB estejam ativas */
    {
        BC250DBG_GCVM_INIT_IO gcvmIo;
        RtlZeroMemory(&gcvmIo, sizeof(gcvmIo));
        gcvmIo.Size = sizeof(gcvmIo);
        BcGcvmInit(&gcvmIo);
    }

    meCntlBefore = BcRegReadRaw(A_CP_ME_CNTL);

    /* 1. Congela as 4 microengines com HALT seguro (0x15000000 e 0x50000000) */
    BcRegWriteRaw(A_CP_MEC_CNTL, 0x50000000u);
    BcRegWriteRaw(A_CP_ME_CNTL, 0x15000000u);
    KeStallExecutionProcessor(50);

    /* 2. Configura Golden Registers, clocks e contexto do CP de acordo com o padrao upstream GFX10 */
    BcRegWriteRaw(0x30800u, 0x00000000); /* mmGRBM_GFX_INDEX: Instance 0, SE 0, SA 0 */
    BcRegWriteRaw(0x08088u, 0x00000000); /* mmGRBM_GFX_CNTL: Pipe 0, ME 0 */
    BcRegWriteRaw(0x3C2C0u, 0x00000100); /* mmCGTT_CP_CLK_CTRL (CP Core Clock Un-gate) */
    BcRegWriteRaw(0x3C2C4u, 0xF8000100); /* mmCGTT_CPF_CLK_CTRL (CPF Clock Un-gate) */
    BcRegWriteRaw(0x3C2C8u, 0x00000100); /* mmCGTT_CPC_CLK_CTRL (CPC Clock Un-gate) */
    BcRegWriteRaw(0x3C200u, 0x3C000100); /* mmCGTT_SPI_CLK_CTRL (SPI Clock Un-gate) */
    BcRegWriteRaw(0x3C230u, 0x60000100); /* mmCGTT_SQ_CLK_CTRL (SQ Clock Un-gate) */
    BcRegWriteRaw(0x3C234u, 0x40000100); /* mmCGTT_SQG_CLK_CTRL (SQG Clock Un-gate) */
    BcRegWriteRaw(0x3C210u, 0xFFFF8100); /* mmCGTT_VGT_CLK_CTRL (VGT Clock Un-gate) */
    BcRegWriteRaw(0x3C218u, 0xFEFF8100); /* mmCGTT_WD_CLK_CTRL (WD Clock Un-gate) */
    BcRegWriteRaw(A_CP_PFP_LOAD_CONTROL, 0x01018003u); /* CP_PFP_LOAD_CONTROL */
    BcRegWriteRaw(A_CP_ROQ1_THRESHOLDS, 0x06008010u);  /* CP_ROQ1_THRESHOLDS */
    BcRegWriteRaw(A_CP_ROQ2_THRESHOLDS, 0x000380A0u);  /* CP_ROQ2_THRESHOLDS */
    BcRegWriteRaw(A_CP_ROQ3_THRESHOLDS, 0x0004C120u);  /* CP_ROQ3_THRESHOLDS */
    BcRegWriteRaw(A_CP_STQ_THRESHOLDS, 0x00804000u);   /* CP_STQ_THRESHOLDS */
    BcRegWriteRaw(A_CP_MEQ_THRESHOLDS, 0x00008040u);   /* CP_MEQ_THRESHOLDS */
    BcRegWriteRaw(A_CP_CONTEXT_CNTL, 0x00750075u);     /* CP_CONTEXT_CNTL */
    BcRegWriteRaw(A_CP_COHER_START_DELAY, 0x00000020u);/* CP_COHER_START_DELAY */
    BcRegWriteRaw(A_CP_DMA_CNTL, 0x10100020u);         /* CP_DMA_CNTL */
    BcRegWriteRaw(A_GE_FAST_CLKS, 0x0000493Eu);        /* GE_FAST_CLKS */
    BcRegWriteRaw(A_CPG_PSP_DEBUG, 0x00000008u);       /* mmCPG_PSP_DEBUG (GPA_OVERRIDE) */
    BcRegWriteRaw(A_CPC_PSP_DEBUG, 0x00000008u);       /* mmCPC_PSP_DEBUG (GPA_OVERRIDE) */
    BcRegWriteRaw(0x0C6DCu, 0x000005FF); /* mmCP_SD_CNTL */
    BcRegWriteRaw(0x09F8Cu, 0x00008500); /* mmGCR_GENERAL_CNTL */
    BcRegWriteRaw(0x098F8u, 0x00000044); /* mmGB_ADDR_CONFIG */
    BcRegWriteRaw(0x08C34u, 0x00000101); /* mmSH_MEM_CONFIG (64-bit unaligned) */
    BcRegWriteRaw(0x0C12Cu, 0x00000001); /* mmCP_DEVICE_ID = 1 */
    BcRegWriteRaw(0x0C2B8u, 0x00000007); /* mmCP_MAX_CONTEXT = 7 (8 contextos de hardware) */
    BcRegWriteRaw(0x0C144u, 0x00000000); /* mmCP_RB_VMID = 0 */
    BcRegWriteRaw(0x08704u, 0x00000000); /* mmCP_RB_WPTR_DELAY = 0 */
    BcRegWriteRaw(0x0C168u, 0x00000000); /* mmCP_RB_DOORBELL_RANGE_LOWER */
    BcRegWriteRaw(0x0C16Cu, 0x000003FE); /* mmCP_RB_DOORBELL_RANGE_UPPER */

    /* 3. Base do anel (alinhado a 256 bytes) */
    BcRegWriteRaw(A_CP_RB0_BASE, (ULONG)((gpuAddr >> 8) & 0xFFFFFFFFull));
    BcRegWriteRaw(A_CP_RB0_BASE_HI, (ULONG)((gpuAddr >> 40) & 0xFFull));

    /* 4. Writeback de RPTR e polling de WPTR */
    BcRegWriteRaw(A_CP_RB0_RPTR_ADDR, (ULONG)(rptrWbAddr & 0xFFFFFFFCull));
    BcRegWriteRaw(A_CP_RB0_RPTR_ADDR_HI, (ULONG)(rptrWbAddr >> 32));
    BcRegWriteRaw(A_CP_RB_DOORBELL_CONTROL, 0x00000000);
    BcRegWriteRaw(A_CP_RB_WPTR_POLL_ADDR_LO, (ULONG)(wptrPollAddr & 0xFFFFFFFCull));
    BcRegWriteRaw(A_CP_RB_WPTR_POLL_ADDR_HI, (ULONG)(wptrPollAddr >> 32));
    BcRegWriteRaw(0x08708, 0x00400000u); /* mmCP_RB_WPTR_POLL_CNTL (IDLE_POLL_COUNT=64) */

    /* 5. Configuração do buffer do anel com RB_RPTR_WR_ENA (bit 31) para destravar escrita de RPTR */
    rbCntl = (9u & 0x3Fu) | ((7u & 0x3Fu) << 8) | (2u << 20) | (2u << 22); /* 0x00A00709 */
    BcRegWriteRaw(A_CP_RB0_CNTL, 0x80000000u | rbCntl);
    BcRegWriteRaw(A_CP_RB0_RPTR, 0);
    BcRegWriteRaw(A_CP_RB0_WPTR, 0);
    BcRegWriteRaw(A_CP_RB0_WPTR_HI, 0);
    BcRegWriteRaw(A_CP_RB0_CNTL, rbCntl);

    /* 6. Invalida Cache L2 e TLBs da GFXHUB e define Golden Registers */
    BcRegWriteRaw(0x03FC4u, 1); /* HDP_MEM_COHERENCY_FLUSH_CNTL */
    BcRegWriteRaw(A_GCVM_L2_CNTL2, 0x00000003u);
    BcHdpReadCacheInvalidate();
    BcGcvmTlbFlush(0);

    /* Golden Registers: VGT_NUM_INSTANCES = 1 e PA_SC_LINE_STIPPLE_STATE = 0 */
    BcRegWriteRaw(0x30934, 1);
    BcRegWriteRaw(0x30A04, 0);

    /* 7. Ativa Ring 0 no CP */
    BcRegWriteRaw(0x0C680u, 1); /* mmCP_RB0_ACTIVE */

    /* 8. Toda a configuracao agora esta visivel; libera os consumidores (Unhalt MEC, CE, PFP, ME) */
    BcRegWriteRaw(A_CP_MEC_CNTL, 0x00000000);
    BcRegWriteRaw(A_CP_ME_CNTL, 0x00000000);
    KeStallExecutionProcessor(50);

    /* Re-afirma controle do anel e polling ativo */
    BcRegWriteRaw(A_CP_RB0_CNTL, rbCntl);
    BcRegWriteRaw(0x08708, 0x00400000u);
    BcRegWriteRaw(0x0C680u, 1);

    g_Dev.GfxRingInitialized = TRUE;
    g_Dev.GfxRingVramOffset  = vramOffset;
    g_Dev.GfxRingSize        = size;
    g_Dev.GfxWptr            = 0;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_GC,
              "GFX Ring 0 pronto: VRAM 0x%X (GPU 0x%llX), %u bytes, CNTL 0x%08X",
              vramOffset, gpuAddr, size, rbCntl);

    Io->Size          = sizeof(*Io);
    Io->RingSizeBytes = size;
    Io->VramOffset    = vramOffset;
    Io->GpuAddr       = gpuAddr;
    Io->RbCntl        = rbCntl;
    Io->MeCntlBefore  = meCntlBefore;
    Io->MeCntlAfter   = BcRegReadRaw(A_CP_ME_CNTL);
    Io->Status        = BC250DBG_OPS_OK;
    return STATUS_SUCCESS;
}

NTSTATUS BcGfxSubmit(BC250DBG_GFX_SUBMIT_IO *Io)
{
    ULONG ringDwords, wptr, rptrBefore, rptrAfter, wptrFinal, i, count;
    ULONG timeout = 200000;
    ULONG rptrWb = 0;

    if (Io == NULL) return STATUS_INVALID_PARAMETER;
    if (g_Dev.Mmio == NULL) return STATUS_DEVICE_NOT_READY;
    if (!g_Dev.GfxRingInitialized) return STATUS_INVALID_DEVICE_STATE;
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
        *(PULONG)((PUCHAR)g_Dev.GfxRingBuffer + g_Dev.GfxRingSize + 0x100) = wptrFinal;
    } else {
        for (i = 0; i < count; i++) {
            ULONG off = g_Dev.GfxRingVramOffset + ((wptr + i) % ringDwords) * 4;
            BcVramWriteRaw(off, Io->Data[i]);
        }
        wptrFinal = (wptr + count) % ringDwords;
        g_Dev.GfxWptr = wptrFinal;
        BcVramWriteRaw(g_Dev.GfxRingVramOffset + g_Dev.GfxRingSize + 0x100, wptrFinal);
    }

    /* Flush de coerencia HDP, invalidacao L2 e TLB Flush da GFXHUB para garantir que o CP enxerga os pacotes */
    BcRegWriteRaw(0x03FC4u, 1); /* HDP_MEM_COHERENCY_FLUSH_CNTL */
    BcHdpReadCacheInvalidate();
    BcRegWriteRaw(A_GCVM_L2_CNTL2, 0x00000003u);
    BcGcvmTlbFlush(0);

    /* Garante anel ativo e polling engatilhado */
    BcRegWriteRaw(0x0C680u, 1);
    BcRegWriteRaw(0x08708, 0x00400100u);

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
        if (rptrAfter == wptrFinal || rptrWb == wptrFinal) {
            rptrAfter = wptrFinal;
            break;
        }
        KeStallExecutionProcessor(5);
    }

    Io->Size          = sizeof(*Io);
    Io->RptrBefore    = rptrBefore;
    Io->RptrAfter     = rptrAfter;
    Io->Wptr          = wptrFinal;
    Io->Completed     = (rptrAfter == wptrFinal || rptrWb == wptrFinal) ? 1 : 0;
    Io->CpMeCntl      = BcRegReadRaw(A_CP_ME_CNTL);
    Io->CpMecCntl     = BcRegReadRaw(A_CP_MEC_CNTL);
    Io->CpStat        = BcRegReadRaw(A_CP_STAT);
    Io->PfpInstrPntr  = BcRegReadRaw(A_CP_PFP_INSTR_PNTR);
    Io->MeInstrPntr   = BcRegReadRaw(A_CP_ME_INSTR_PNTR);
    Io->CeInstrPntr   = BcRegReadRaw(A_CP_CE_INSTR_PNTR);
    Io->Mec1InstrPntr = BcRegReadRaw(A_CP_MEC1_INSTR_PNTR);
    Io->PfpJtStat     = BcRegReadRaw(A_CP_PFP_JT_STAT);
    Io->CeJtStat      = BcRegReadRaw(A_CP_CE_JT_STAT);
    Io->MecJtStat     = BcRegReadRaw(A_CP_MEC_JT_STAT);
    Io->Status        = BC250DBG_OPS_OK;

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
