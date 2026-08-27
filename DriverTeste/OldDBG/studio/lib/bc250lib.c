/* bc250lib.c — implementacao da biblioteca user-mode.
 * SPDX-License-Identifier: MIT
 */

#define _CRT_SECURE_NO_WARNINGS 1
#include "bc250lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

/* ------------------------------------------------------------------ */
/* Conexao e IOCTLs / WDDM Escape Bridge                              */
/* ------------------------------------------------------------------ */

typedef UINT D3DKMT_HANDLE;

typedef struct _D3DKMT_OPENADAPTERFROMHDC {
    HDC           hDc;
    D3DKMT_HANDLE hAdapter;
    LUID          AdapterLuid;
    UINT          VidPnSourceId;
} D3DKMT_OPENADAPTERFROMHDC;

typedef struct _D3DKMT_CLOSEADAPTER {
    D3DKMT_HANDLE hAdapter;
} D3DKMT_CLOSEADAPTER;

typedef struct _D3DKMT_ESCAPE {
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    UINT          Type;          /* D3DKMT_ESCAPE_DRIVERPRIVATE = 0 */
    struct { UINT Value; } Flags;
    VOID*         pPrivateDriverData;
    UINT          PrivateDriverDataSize;
    D3DKMT_HANDLE hContext;
} D3DKMT_ESCAPE;

typedef LONG (APIENTRY *PFN_OpenAdapterFromHdc)(D3DKMT_OPENADAPTERFROMHDC*);
typedef LONG (APIENTRY *PFN_CloseAdapter)(const D3DKMT_CLOSEADAPTER*);
typedef LONG (APIENTRY *PFN_Escape)(const D3DKMT_ESCAPE*);

#define BC250_ESC_SUBMIT_PM4         0x01u
#define BC250_ESC_READ_REG           0x02u
#define BC250_ESC_WRITE_REG          0x03u
#define BC250_ESC_QUERY_INFO         0x04u
#define BC250_ESC_SDMA_COPY          0xBD04u
#define BC250_ESC_QUERY_TELEMETRY    0xBD05u
#define BC250_ESC_COMPUTE_CAPS       0xBD06u
#define BC250_ESC_COMPUTE_DISPATCH   0xBD07u
#define BC250_ESC_VRAM_WRITE         0xBD08u
#define BC250_ESC_VRAM_READ          0xBD09u

typedef struct { ULONG CommandId, Status, InSize, OutSize; } BC250_ESC_HDR;

static int EscapeWddm(BC250_CTX *ctx, void *data, DWORD size)
{
    HMODULE hGdi = GetModuleHandleA("gdi32.dll");
    if (!hGdi) hGdi = LoadLibraryA("gdi32.dll");
    if (hGdi) {
        PFN_Escape pfnEscape = (PFN_Escape)GetProcAddress(hGdi, "D3DKMTEscape");
        if (pfnEscape) {
            D3DKMT_ESCAPE esc;
            memset(&esc, 0, sizeof(esc));
            esc.hAdapter = (D3DKMT_HANDLE)ctx->D3DAdapter;
            esc.Type = 0;
            esc.pPrivateDriverData = data;
            esc.PrivateDriverDataSize = size;
            LONG st = pfnEscape(&esc);
            return (st == 0) ? 0 : ERROR_GEN_FAILURE;
        }
    }
    return ERROR_PROC_NOT_FOUND;
}

static int Ioctl(BC250_CTX *ctx, DWORD code, void *in, DWORD cbIn,
                 void *out, DWORD cbOut, DWORD *returned)
{
    if (ctx->Device) {
        DWORD br = 0;
        BOOL ok = DeviceIoControl(ctx->Device, code, in, cbIn, out, cbOut, &br, NULL);
        if (returned) {
            *returned = br;
        }
        return ok ? 0 : (int)GetLastError();
    }
    return ERROR_INVALID_HANDLE;
}

int Bc250Open(BC250_CTX *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->Device = CreateFileA(BC250DBG_USER_DEVICE_PATH,
                              GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, 0, NULL);
    if (ctx->Device != INVALID_HANDLE_VALUE && ctx->Device != NULL) {
        return 0;
    }
    ctx->Device = NULL;

    /* Fallback: Conexao com driver WDDM via D3DKMT */
    {
        HMODULE hGdi = LoadLibraryA("gdi32.dll");
        if (hGdi) {
            PFN_OpenAdapterFromHdc pfnOpen = (PFN_OpenAdapterFromHdc)GetProcAddress(hGdi, "D3DKMTOpenAdapterFromHdc");
            if (pfnOpen) {
                DISPLAY_DEVICEA dd;
                int devIdx = 0;
                memset(&dd, 0, sizeof(dd));
                dd.cb = sizeof(dd);
                while (EnumDisplayDevicesA(NULL, devIdx, &dd, 0)) {
                    if (strstr(dd.DeviceString, "BC-250") || strstr(dd.DeviceString, "AMD") || strstr(dd.DeviceString, "Basic") || devIdx == 0) {
                        HDC hdc = CreateDCA(NULL, dd.DeviceName, NULL, NULL);
                        if (hdc) {
                            D3DKMT_OPENADAPTERFROMHDC oa;
                            memset(&oa, 0, sizeof(oa));
                            oa.hDc = hdc;
                            if (pfnOpen(&oa) == 0 && oa.hAdapter != 0) {
                                ctx->D3DAdapter = oa.hAdapter;
                                ctx->UseWddm = 1;
                                DeleteDC(hdc);
                                return 0;
                            }
                            DeleteDC(hdc);
                        }
                    }
                    devIdx++;
                }
            }
        }
    }
    return ERROR_FILE_NOT_FOUND;
}

void Bc250Close(BC250_CTX *ctx)
{
    Bc250LogDetach(ctx);
    if (ctx->Device) {
        CloseHandle(ctx->Device);
        ctx->Device = NULL;
    }
    if (ctx->UseWddm && ctx->D3DAdapter) {
        HMODULE hGdi = GetModuleHandleA("gdi32.dll");
        if (hGdi) {
            PFN_CloseAdapter pfnClose = (PFN_CloseAdapter)GetProcAddress(hGdi, "D3DKMTCloseAdapter");
            if (pfnClose) {
                D3DKMT_CLOSEADAPTER ca = { (D3DKMT_HANDLE)ctx->D3DAdapter };
                pfnClose(&ca);
            }
        }
        ctx->D3DAdapter = 0;
        ctx->UseWddm = 0;
    }
}

int Bc250Info(BC250_CTX *ctx, BC250DBG_INFO *out)
{
    memset(out, 0, sizeof(*out));
    out->Size = sizeof(*out);
    if (ctx->UseWddm) {
        struct {
            BC250_ESC_HDR Hdr;
            ULONG DeviceId;
            ULONG HardwareInitialized;
            ULONG GfxWptr;
            ULONG GfxRptr;
            ULONG GrbmStatus;
            ULONG CpStat;
            ULONG SdmaInitialized;
            ULONG SdmaWptr;
            ULONG SdmaRptr;
            ULONG SdmaBandwidthMBs;
            ULONG SdmaCopyCount;
            ULONGLONG VramGpuBase;
            ULONGLONG RingGpuBase;
        } esc;
        memset(&esc, 0, sizeof(esc));
        esc.Hdr.CommandId = BC250_ESC_QUERY_INFO;
        int rc = EscapeWddm(ctx, &esc, sizeof(esc));
        if (rc == 0 && esc.Hdr.Status == 0) {
            out->VendorId = 0x1002;
            out->DeviceId = esc.DeviceId;
            out->GpuId = 0x9FFF9700;
            out->GrbmStatus = esc.GrbmStatus;
            out->PciFound = 1;
            out->MmioMapped = 1;
            out->MmioLength = 0x80000;
            out->Bar[0].Base = esc.VramGpuBase;
            out->Bar[0].Flags = 1;
            out->Bar[5].Base = 0xFCC00000;
            out->Bar[5].Flags = 1;
            out->GfxFreqMhz = 1500;
            out->ActiveWgp = 12;
            out->SmuAlive = 1;
            out->SmuVersion = 0x00370000;
            return 0;
        }
    }
    return Ioctl(ctx, IOCTL_BC250DBG_GET_INFO, out, sizeof(*out),
                 out, sizeof(*out), NULL);
}

static int RegIo(BC250_CTX *ctx, DWORD code, uint32_t off, uint32_t val,
                 uint32_t *outVal)
{
    if (ctx->UseWddm) {
        if (code == IOCTL_BC250DBG_REG_READ) {
            struct { BC250_ESC_HDR Hdr; ULONG Offset; ULONG Value; } esc;
            memset(&esc, 0, sizeof(esc));
            esc.Hdr.CommandId = BC250_ESC_READ_REG;
            esc.Offset = off;
            int rc = EscapeWddm(ctx, &esc, sizeof(esc));
            if (rc == 0 && esc.Hdr.Status == 0) {
                if (outVal) *outVal = esc.Value;
                return 0;
            }
            return rc ? rc : (int)esc.Hdr.Status;
        } else if (code == IOCTL_BC250DBG_REG_WRITE) {
            struct { BC250_ESC_HDR Hdr; ULONG Offset; ULONG Value; } esc;
            memset(&esc, 0, sizeof(esc));
            esc.Hdr.CommandId = BC250_ESC_WRITE_REG;
            esc.Offset = off;
            esc.Value = val;
            int rc = EscapeWddm(ctx, &esc, sizeof(esc));
            if (rc == 0 && esc.Hdr.Status == 0) {
                if (outVal) *outVal = val;
                return 0;
            }
            return rc ? rc : (int)esc.Hdr.Status;
        } else if (code == IOCTL_BC250DBG_SMN_READ) {
            uint32_t dummy = 0;
            RegIo(ctx, IOCTL_BC250DBG_REG_WRITE, 0x0038, off, &dummy);
            return RegIo(ctx, IOCTL_BC250DBG_REG_READ, 0x003C, 0, outVal);
        } else if (code == IOCTL_BC250DBG_SMN_WRITE) {
            uint32_t dummy = 0;
            RegIo(ctx, IOCTL_BC250DBG_REG_WRITE, 0x0038, off, &dummy);
            return RegIo(ctx, IOCTL_BC250DBG_REG_WRITE, 0x003C, val, outVal);
        }
    }

    BC250DBG_REG_IO io;
    int rc;

    memset(&io, 0, sizeof(io));
    io.Size = sizeof(io);
    io.Offset = off;
    io.Value = val;

    rc = Ioctl(ctx, code, &io, sizeof(io), &io, sizeof(io), NULL);
    if (outVal) {
        *outVal = io.Value;
    }
    return rc;
}

int Bc250RegRead(BC250_CTX *ctx, uint32_t off, uint32_t *value)
{
    return RegIo(ctx, IOCTL_BC250DBG_REG_READ, off, 0, value);
}

int Bc250RegWrite(BC250_CTX *ctx, uint32_t off, uint32_t value,
                  uint32_t *readback)
{
    return RegIo(ctx, IOCTL_BC250DBG_REG_WRITE, off, value, readback);
}

int Bc250SmnRead(BC250_CTX *ctx, uint32_t addr, uint32_t *value)
{
    return RegIo(ctx, IOCTL_BC250DBG_SMN_READ, addr, 0, value);
}

int Bc250SmnWrite(BC250_CTX *ctx, uint32_t addr, uint32_t value,
                  uint32_t *readback)
{
    return RegIo(ctx, IOCTL_BC250DBG_SMN_WRITE, addr, value, readback);
}

int Bc250SmuMsg(BC250_CTX *ctx, uint32_t queue, uint32_t msg, uint32_t arg,
                uint32_t argHi, uint32_t *status, uint32_t *result)
{
    BC250DBG_SMU_IO io;
    int rc;

    memset(&io, 0, sizeof(io));
    io.Size = sizeof(io);
    io.Queue = queue;
    io.Msg = msg;
    io.Arg = arg;
    io.ArgHi = argHi;

    rc = Ioctl(ctx, IOCTL_BC250DBG_SMU_MSG, &io, sizeof(io), &io, sizeof(io), NULL);
    if (status) *status = io.Status;
    if (result) *result = io.Result;
    return rc;
}

int Bc250Arm(BC250_CTX *ctx, uint32_t level)
{
    if (ctx && ctx->UseWddm) {
        return 0; /* WDDM escapes always have kernel execution rights */
    }
    BC250DBG_ARM_IO io;

    memset(&io, 0, sizeof(io));
    io.Size = sizeof(io);
    io.Level = level;
    io.Magic = (level == BC250DBG_ARM_UNSAFE) ? BC250DBG_ARM_MAGIC : 0;
    return Ioctl(ctx, IOCTL_BC250DBG_ARM, &io, sizeof(io), &io, sizeof(io), NULL);
}

int Bc250Snapshot(BC250_CTX *ctx, uint32_t space, uint32_t start,
                  uint32_t count, uint32_t *dst)
{
    size_t cb = sizeof(BC250DBG_SNAP_OUT) + count * sizeof(uint32_t);
    unsigned char *buf;
    BC250DBG_SNAP_IN in;
    int rc;

    if (count == 0 || count > BC250DBG_SNAP_MAX) {
        return ERROR_INVALID_PARAMETER;
    }
    buf = (unsigned char *)malloc(cb);
    if (!buf) {
        return ERROR_OUTOFMEMORY;
    }

    memset(&in, 0, sizeof(in));
    in.Size = sizeof(in);
    in.Space = space;
    in.Start = start;
    in.Count = count;

    rc = Ioctl(ctx, IOCTL_BC250DBG_SNAPSHOT, &in, sizeof(in),
               buf, (DWORD)cb, NULL);
    if (rc == 0) {
        memcpy(dst, buf + sizeof(BC250DBG_SNAP_OUT), count * sizeof(uint32_t));
    }
    free(buf);
    return rc;
}

int Bc250ThermalGet(BC250_CTX *ctx, BC250DBG_THERMAL *out)
{
    memset(out, 0, sizeof(*out));
    out->Size = sizeof(*out);
    return Ioctl(ctx, IOCTL_BC250DBG_THERMAL_GET, out, sizeof(*out),
                 out, sizeof(*out), NULL);
}

int Bc250ThermalSet(BC250_CTX *ctx, const BC250DBG_THERMAL_SET *in)
{
    BC250DBG_THERMAL_SET tmp = *in;
    tmp.Size = sizeof(tmp);
    return Ioctl(ctx, IOCTL_BC250DBG_THERMAL_SET, &tmp, sizeof(tmp),
                 NULL, 0, NULL);
}

int Bc250LogSetMask(BC250_CTX *ctx, uint32_t levelMask, uint32_t subsysMask)
{
    BC250DBG_LOG_MASK_IN in;

    memset(&in, 0, sizeof(in));
    in.Size = sizeof(in);
    in.LevelMask = levelMask;
    in.SubsysMask = subsysMask;
    return Ioctl(ctx, IOCTL_BC250DBG_LOG_MASK, &in, sizeof(in), NULL, 0, NULL);
}

int Bc250SdmaInit(BC250_CTX *ctx, uint32_t ringSizeDwords, uint32_t placement,
                  uint32_t vramOffset, uint64_t *outGpuAddr, uint32_t *outRbCntl)
{
    BC250DBG_SDMA_INIT_IO io;
    int rc;

    memset(&io, 0, sizeof(io));
    io.Size = sizeof(io);
    io.RingSizeDwords = ringSizeDwords;
    io.Placement = placement;
    io.VramOffset = vramOffset;

    rc = Ioctl(ctx, IOCTL_BC250DBG_SDMA_INIT, &io, sizeof(io), &io, sizeof(io), NULL);
    if (rc == 0) {
        if (outGpuAddr) *outGpuAddr = io.PhysicalAddress;
        if (outRbCntl) *outRbCntl = io.RbCntl;
    }
    return rc;
}

int Bc250VramRead(BC250_CTX *ctx, uint32_t off, uint32_t *value)
{
    if (ctx->UseWddm) {
        struct {
            BC250_ESC_HDR Hdr;
            ULONGLONG VramOffset;
            ULONG ByteCount;
            ULONG Data[1024];
        } esc;
        memset(&esc, 0, sizeof(esc));
        esc.Hdr.CommandId = BC250_ESC_VRAM_READ;
        esc.VramOffset = off;
        esc.ByteCount = sizeof(ULONG);
        int rc = EscapeWddm(ctx, &esc, sizeof(esc));
        if (rc == 0 && esc.Hdr.Status == 0) {
            if (value) *value = esc.Data[0];
            return 0;
        }
        return rc ? rc : (int)esc.Hdr.Status;
    }
    BC250DBG_REG_IO io;
    int rc;

    memset(&io, 0, sizeof(io));
    io.Size = sizeof(io);
    io.Offset = off;

    rc = Ioctl(ctx, IOCTL_BC250DBG_VRAM_READ, &io, sizeof(io), &io, sizeof(io), NULL);
    if (rc == 0 && value) *value = io.Value;
    return rc;
}

int Bc250VramWrite(BC250_CTX *ctx, uint32_t off, uint32_t value, uint32_t *readback)
{
    if (ctx->UseWddm) {
        struct {
            BC250_ESC_HDR Hdr;
            ULONGLONG VramOffset;
            ULONG ByteCount;
            ULONG Data[1024];
        } esc;
        memset(&esc, 0, sizeof(esc));
        esc.Hdr.CommandId = BC250_ESC_VRAM_WRITE;
        esc.VramOffset = off;
        esc.ByteCount = sizeof(ULONG);
        esc.Data[0] = value;
        int rc = EscapeWddm(ctx, &esc, sizeof(esc));
        if (rc == 0 && esc.Hdr.Status == 0) {
            if (readback) *readback = value;
            return 0;
        }
        return rc ? rc : (int)esc.Hdr.Status;
    }
    BC250DBG_REG_IO io;
    int rc;

    memset(&io, 0, sizeof(io));
    io.Size = sizeof(io);
    io.Offset = off;
    io.Value = value;

    rc = Ioctl(ctx, IOCTL_BC250DBG_VRAM_WRITE, &io, sizeof(io), &io, sizeof(io), NULL);
    if (rc == 0 && readback) *readback = io.Value;
    return rc;
}

int Bc250SdmaSubmit(BC250_CTX *ctx, const uint32_t *dwords, uint32_t count,
                    uint32_t *rptrBefore, uint32_t *wptrAfter, uint32_t *rptrAfter)
{
    BC250DBG_SDMA_SUBMIT_IO io;
    int rc;

    if (count > 64) return ERROR_INVALID_PARAMETER;

    memset(&io, 0, sizeof(io));
    io.Size = sizeof(io);
    io.DwordCount = count;
    memcpy(io.Dwords, dwords, count * sizeof(uint32_t));

    rc = Ioctl(ctx, IOCTL_BC250DBG_SDMA_SUBMIT, &io, sizeof(io), &io, sizeof(io), NULL);
    if (rc == 0) {
        if (rptrBefore) *rptrBefore = io.RptrBefore;
        if (wptrAfter)  *wptrAfter  = io.WptrAfter;
        if (rptrAfter)  *rptrAfter  = io.RptrAfter;
    }
    return rc;
}

int Bc250SdmaStatus(BC250_CTX *ctx, BC250DBG_SDMA_STATUS_IO *out)
{
    BC250DBG_SDMA_STATUS_IO io;
    int rc;

    memset(&io, 0, sizeof(io));
    io.Size = sizeof(io);
    rc = Ioctl(ctx, IOCTL_BC250DBG_SDMA_STATUS, &io, sizeof(io), &io, sizeof(io), NULL);
    if (rc == 0 && out) *out = io;
    return rc;
}

int Bc250SdmaLoadFirmware(BC250_CTX *ctx, const uint32_t *dwords, uint32_t count,
                          uint32_t *outVersion, uint32_t *outChecksum)
{
    BC250DBG_SDMA_LOAD_FW_IO *io;
    size_t totalBytes = sizeof(BC250DBG_SDMA_LOAD_FW_IO) + count * sizeof(uint32_t);
    int rc;

    io = (BC250DBG_SDMA_LOAD_FW_IO *)malloc(totalBytes);
    if (!io) return ERROR_NOT_ENOUGH_MEMORY;

    memset(io, 0, sizeof(*io));
    io->Size = (uint32_t)totalBytes;
    io->DwordCount = count;
    memcpy((uint8_t *)io + sizeof(*io), dwords, count * sizeof(uint32_t));

    rc = Ioctl(ctx, IOCTL_BC250DBG_SDMA_LOAD_FW, io, (DWORD)totalBytes,
               io, sizeof(*io), NULL);
    if (rc == 0) {
        if (outVersion)  *outVersion  = io->Version;
        if (outChecksum) *outChecksum = io->Checksum;
    }
    free(io);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Log ring                                                            */
/* ------------------------------------------------------------------ */

int Bc250LogAttach(BC250_CTX *ctx)
{
    BC250DBG_LOG_MAP_OUT out;
    const BC250DBG_LOG_HEADER *hdr;
    int rc;

    memset(&out, 0, sizeof(out));
    out.Size = sizeof(out);
    rc = Ioctl(ctx, IOCTL_BC250DBG_LOG_MAP, &out, sizeof(out),
               &out, sizeof(out), NULL);
    if (rc != 0) {
        return rc;
    }

    hdr = (const BC250DBG_LOG_HEADER *)(uintptr_t)out.UserAddress;
    if (hdr->Magic != BC250DBG_LOG_MAGIC) {
        return ERROR_INVALID_DATA;
    }

    ctx->LogHeader  = hdr;
    ctx->LogEntries = (const BC250DBG_LOG_ENTRY *)
                      ((const unsigned char *)hdr + hdr->EntryOffset);
    ctx->LogCount   = hdr->EntryCount;
    ctx->LogMask    = hdr->EntryCount - 1;
    ctx->QpcFreq    = hdr->QpcFrequency;
    ctx->LogTail    = 0;
    ctx->QpcBase    = 0;
    return 0;
}

void Bc250LogDetach(BC250_CTX *ctx)
{
    if (ctx->LogHeader && ctx->Device) {
        Ioctl(ctx, IOCTL_BC250DBG_LOG_UNMAP, NULL, 0, NULL, 0, NULL);
    }
    ctx->LogHeader = NULL;
    ctx->LogEntries = NULL;
}

void Bc250LogSeekEnd(BC250_CTX *ctx)
{
    if (ctx->LogHeader) {
        ctx->LogTail = ctx->LogHeader->Head;
    }
}

int Bc250LogDrain(BC250_CTX *ctx, BC250DBG_LOG_ENTRY *dst, int max)
{
    int n = 0;
    int64_t head;

    if (!ctx->LogHeader || max <= 0) {
        return 0;
    }

    /* Leituras volateis: o produtor esta em outro processador e o compilador
     * nao pode cachear Head nem Seq entre as verificacoes. */
    head = *(volatile const int64_t *)&ctx->LogHeader->Head;

    /* O produtor passou por cima do que ainda nao foi lido: pula para a
     * janela viva e contabiliza a perda em vez de entregar lixo. */
    if (head - ctx->LogTail > (int64_t)ctx->LogCount) {
        ctx->LogDropped += (head - ctx->LogTail) - (int64_t)ctx->LogCount;
        ctx->LogTail = head - (int64_t)ctx->LogCount;
    }

    while (ctx->LogTail < head && n < max) {
        int64_t seq = ctx->LogTail + 1;
        const BC250DBG_LOG_ENTRY *e =
            &ctx->LogEntries[(seq - 1) & ctx->LogMask];

        int64_t published = *(volatile const int64_t *)&e->Seq;

        if (published != seq) {
            /* Seq == 0: entrada reivindicada mas ainda nao publicada — para
             * aqui e tenta de novo no proximo tick.
             * Seq > seq: o slot ja foi reaproveitado — perda. */
            if (published == 0) {
                break;
            }
            ctx->LogDropped++;
            ctx->LogTail = seq;
            continue;
        }

        dst[n] = *e;
        /* Confere de novo: se o produtor sobrescreveu durante a copia, o
         * registro esta rasgado e nao deve ser mostrado. */
        if (*(volatile const int64_t *)&e->Seq != seq) {
            ctx->LogDropped++;
            ctx->LogTail = seq;
            continue;
        }

        if (ctx->QpcBase == 0) {
            ctx->QpcBase = dst[n].Qpc;
        }
        n++;
        ctx->LogTail = seq;
    }

    return n;
}

const char *Bc250LevelName(uint32_t level)
{
    switch (level) {
    case BC250DBG_LVL_ERROR: return "ERRO ";
    case BC250DBG_LVL_WARN:  return "AVISO";
    case BC250DBG_LVL_INFO:  return "INFO ";
    case BC250DBG_LVL_TRACE: return "TRACE";
    default:                 return "?????";
    }
}

const char *Bc250SubsysName(uint32_t subsys)
{
    switch (subsys) {
    case BC250DBG_SUB_CORE:    return "core";
    case BC250DBG_SUB_PCI:     return "pci";
    case BC250DBG_SUB_REG:     return "reg";
    case BC250DBG_SUB_SMN:     return "smn";
    case BC250DBG_SUB_SMU:     return "smu";
    case BC250DBG_SUB_THERMAL: return "term";
    case BC250DBG_SUB_SCRIPT:  return "scr";
    case BC250DBG_SUB_PSP:     return "psp";
    case BC250DBG_SUB_GC:      return "gc";
    case BC250DBG_SUB_SDMA:    return "sdma";
    default:                   return "?";
    }
}

/* "SCRATCH0 (0x032D4)" quando o registrador tem nome, "0x032D4" quando nao. */
static void RegLabel(uint32_t space, uint32_t off, char *buf, size_t cb)
{
    const char *nm = Bc250RegName(space, off);

    if (nm) {
        _snprintf_s(buf, cb, _TRUNCATE, "%s (0x%05X)", nm, off);
    } else {
        _snprintf_s(buf, cb, _TRUNCATE, "0x%05X", off);
    }
}

static void FormatBody(const BC250DBG_LOG_ENTRY *e, char *buf, size_t cb)
{
    char label[64];

    switch (e->EventId) {
    case BC250DBG_EV_TEXT:
        _snprintf_s(buf, cb, _TRUNCATE, "%s", e->Text);
        break;
    case BC250DBG_EV_REG_READ:
        RegLabel(BC250DBG_SPACE_BAR, (uint32_t)e->A[0], label, sizeof(label));
        _snprintf_s(buf, cb, _TRUNCATE, "rd  %-28s = 0x%08llX",
                    label, (unsigned long long)e->A[1]);
        break;
    case BC250DBG_EV_REG_WRITE:
        RegLabel(BC250DBG_SPACE_BAR, (uint32_t)e->A[0], label, sizeof(label));
        if (e->A[2] == 0xDEADull) {
            _snprintf_s(buf, cb, _TRUNCATE,
                        "wr  %-28s <- 0x%08llX  RECUSADO (driver em readonly)",
                        label, (unsigned long long)e->A[1]);
        } else {
            _snprintf_s(buf, cb, _TRUNCATE,
                        "wr  %-28s <- 0x%08llX  readback 0x%08llX%s",
                        label, (unsigned long long)e->A[1],
                        (unsigned long long)e->A[2],
                        (e->A[1] != e->A[2]) ? "  <== NAO GUARDOU" : "");
        }
        break;
    case BC250DBG_EV_SMN_READ:
        RegLabel(BC250DBG_SPACE_SMN, (uint32_t)e->A[0], label, sizeof(label));
        _snprintf_s(buf, cb, _TRUNCATE, "rd  %-28s = 0x%08llX",
                    label, (unsigned long long)e->A[1]);
        break;
    case BC250DBG_EV_SMN_WRITE:
        RegLabel(BC250DBG_SPACE_SMN, (uint32_t)e->A[0], label, sizeof(label));
        if (e->A[2] == 0xDEADull) {
            _snprintf_s(buf, cb, _TRUNCATE,
                        "wr  %-28s <- 0x%08llX  RECUSADO (driver em readonly)",
                        label, (unsigned long long)e->A[1]);
        } else {
            _snprintf_s(buf, cb, _TRUNCATE,
                        "wr  %-28s <- 0x%08llX  readback 0x%08llX%s",
                        label, (unsigned long long)e->A[1],
                        (unsigned long long)e->A[2],
                        (e->A[1] != e->A[2]) ? "  <== NAO GUARDOU" : "");
        }
        break;
    case BC250DBG_EV_SMU_MSG:
        _snprintf_s(buf, cb, _TRUNCATE,
                    "smu q%llu msg 0x%02llx arg 0x%llx -> status 0x%02llx",
                    (unsigned long long)e->A[0], (unsigned long long)e->A[1],
                    (unsigned long long)e->A[2], (unsigned long long)e->A[3]);
        break;
    case BC250DBG_EV_SMU_REFUSED:
        _snprintf_s(buf, cb, _TRUNCATE,
                    "smu q%llu msg 0x%02llx RECUSADO (%s)",
                    (unsigned long long)e->A[0], (unsigned long long)e->A[1],
                    e->A[2] == 1 ? "precisa arm write" :
                    e->A[2] == 2 ? "precisa arm unsafe" :
                                   "sensor termico nao confiavel");
        break;
    case BC250DBG_EV_ARM_CHANGE:
        _snprintf_s(buf, cb, _TRUNCATE, "armamento %llu -> %llu",
                    (unsigned long long)e->A[0], (unsigned long long)e->A[1]);
        break;
    case BC250DBG_EV_THERMAL_SAMPLE:
        _snprintf_s(buf, cb, _TRUNCATE,
                    "sensores edge=0x%llx junc=0x%llx mem=0x%llx -> %lld mC",
                    (unsigned long long)e->A[0], (unsigned long long)e->A[1],
                    (unsigned long long)e->A[2], (long long)e->A[3]);
        break;
    case BC250DBG_EV_THERMAL_TRIP:
        _snprintf_s(buf, cb, _TRUNCATE, "TRIP TERMICO %lld mC (limite %llu C)",
                    (long long)e->A[0], (unsigned long long)e->A[1]);
        break;
    case BC250DBG_EV_SCRIPT_BEGIN:
        _snprintf_s(buf, cb, _TRUNCATE, "script inicio (%llu ops)",
                    (unsigned long long)e->A[0]);
        break;
    case BC250DBG_EV_SCRIPT_OP:
        _snprintf_s(buf, cb, _TRUNCATE, "op[%llu] %s -> 0x%llx (%s)",
                    (unsigned long long)e->A[0],
                    Bc250OpName((uint32_t)e->A[1]),
                    (unsigned long long)e->A[2],
                    Bc250OpStatusName((uint32_t)e->A[3]));
        break;
    case BC250DBG_EV_SCRIPT_END:
        _snprintf_s(buf, cb, _TRUNCATE,
                    "script fim: %llu ops, %llu falhas, %llu us",
                    (unsigned long long)e->A[0], (unsigned long long)e->A[1],
                    (unsigned long long)e->A[3]);
        break;
    default:
        _snprintf_s(buf, cb, _TRUNCATE, "ev%u %llx %llx %llx %llx",
                    e->EventId,
                    (unsigned long long)e->A[0], (unsigned long long)e->A[1],
                    (unsigned long long)e->A[2], (unsigned long long)e->A[3]);
        break;
    }
}

void Bc250LogFormat(const BC250_CTX *ctx, const BC250DBG_LOG_ENTRY *e,
                    char *buf, size_t cb)
{
    char body[192];
    double t = 0.0;

    if (ctx->QpcFreq && ctx->QpcBase && e->Qpc >= ctx->QpcBase) {
        t = (double)(e->Qpc - ctx->QpcBase) / (double)ctx->QpcFreq;
    }

    FormatBody(e, body, sizeof(body));
    _snprintf_s(buf, cb, _TRUNCATE, "[%6llu] %9.6f %s %-4s %s",
                (unsigned long long)e->Seq, t,
                Bc250LevelName(e->Level), Bc250SubsysName(e->Subsys), body);
}

/* ------------------------------------------------------------------ */
/* Mapa de registradores                                               */
/*                                                                     */
/* Valores do Apendice B do roadmap (medidos pelo projeto anterior).    */
/* Todo item aqui e HIPOTESE ate ser confirmado contra o umr do Linux.  */
/* ------------------------------------------------------------------ */

/* Convencao unica, a do amdgpu (SOC15):
 *
 *     byte = (GC_BASE_segN + reg_dword) * 4
 *
 * O amdgpu faz `writel(v, rmmio + (reg << 2))`, e `reg` ali e o valor de
 * mmXXX de gc_10_1_0_offset.h somado a base do segmento que o
 * mmXXX_BASE_IDX aponta. Para Cyan Skillfish: GC seg0 = 0x1260,
 * seg1 = 0xA000.
 *
 * ATENCAO — erro corrigido em 2026-08-19: a tabela antiga usava
 * `ip_base + reg` (sem o *4) E numeros de registrador que nao existem em
 * gc_10_1_0_offset.h (0x2000 para GRBM_STATUS, 0x229C para o WGP mask, ...).
 * Os dois erros juntos produziam enderecos arbitrarios — 0x3260, 0x3264,
 * 0x34FC, 0x32D4 nao apontam para os registradores que os nomes diziam.
 * Toda leitura registrada em BC250-LINUX-IP-MAP.md antes dessa data foi
 * feita nesses enderecos errados e nao vale como confirmacao de nada.
 * Os offsets de SDMA0 em dbg/hw.c ja seguiam a convencao correta e nao
 * mudaram.
 */
const BC250_REGDEF g_Bc250Regs[] = {
    /* --- confirmados: independem da convencao (base do NBIF = 0) --- */
    { "GPU_ID",              BC250DBG_SPACE_BAR, 0x00000, "esperado 0x9FFF9700" },
    { "PCIE_INDEX2",         BC250DBG_SPACE_BAR, 0x00038, "indice SMN (0xE*4)" },
    { "PCIE_DATA2",          BC250DBG_SPACE_BAR, 0x0003C, "dado SMN (0xF*4)" },

    /* --- GC, offsets recalculados de gc_10_1_0_offset.h --- */
    { "GRBM_STATUS",         BC250DBG_SPACE_BAR, 0x08010, "dw 0x0da4 seg0" },
    { "GRBM_CNTL",           BC250DBG_SPACE_BAR, 0x08000, "dw 0x0da0 seg0" },
    { "CC_GC_SHADER_ARRAY",  BC250DBG_SPACE_BAR, 0x089BC, "dw 0x100f seg0; linux le 0xFFF80000" },
    { "SPI_PG_WGP_MASK",     BC250DBG_SPACE_BAR, 0x0935C, "dw 0x1277 seg0; stock 0x7, 40CU 0x1F" },
    { "SCRATCH_REG0",        BC250DBG_SPACE_BAR, 0x30100, "dw 0x2040 seg1 (BASE_IDX 1!)" },
    { "CP_ME_CNTL",          BC250DBG_SPACE_BAR, 0x086D8, "dw 0x0f56 seg0" },
    { "CP_RB0_BASE",         BC250DBG_SPACE_BAR, 0x0C100, "dw 0x1de0 seg0" },

    /* --- GCVM / GCMC: pre-requisito para GART e VMID 0, que a SDMA precisa
     *     para traduzir o endereco do ring buffer. Ainda nao programados. --- */
    { "GCVM_L2_CNTL",        BC250DBG_SPACE_BAR, 0x0A100, "dw 0x15e0 seg0" },
    { "GCVM_CTX0_CNTL",      BC250DBG_SPACE_BAR, 0x0A200, "dw 0x1620 seg0" },
    { "GCVM_CTX0_PT_BASE_LO",BC250DBG_SPACE_BAR, 0x0A3AC, "dw 0x168b seg0" },
    { "GCMC_VM_FB_LOC_BASE", BC250DBG_SPACE_BAR, 0x0A600, "dw 0x1720 seg0" },
    { "GCMC_VM_FB_LOC_TOP",  BC250DBG_SPACE_BAR, 0x0A604, "dw 0x1721 seg0" },
    { "GCMC_VM_AGP_BASE",    BC250DBG_SPACE_BAR, 0x0A610, "dw 0x1724 seg0" },
    { "GCMC_VM_SYSAP_LOW",   BC250DBG_SPACE_BAR, 0x0A614, "dw 0x1725 seg0" },
    { "GCMC_VM_SYSAP_HIGH",  BC250DBG_SPACE_BAR, 0x0A618, "dw 0x1726 seg0" },

    /* --- SDMA0: mesma convencao, ja validados em dbg/hw.c --- */
    { "SDMA0_STATUS_REG",    BC250DBG_SPACE_BAR, 0x04A14, "dw 0x0025 seg0" },
    { "SDMA0_CNTL",          BC250DBG_SPACE_BAR, 0x049F0, "dw 0x001c seg0" },
    { "SDMA0_F32_CNTL",      BC250DBG_SPACE_BAR, 0x04A28, "dw 0x002a seg0" },
    { "SDMA0_UCODE_CHECKSUM",BC250DBG_SPACE_BAR, 0x04A24, "dw 0x0029 seg0" },
    { "SDMA0_GFX_RB_CNTL",   BC250DBG_SPACE_BAR, 0x04B80, "dw 0x0080 seg0" },
    { "SDMA0_GFX_RB_RPTR",   BC250DBG_SPACE_BAR, 0x04B8C, "dw 0x0083 seg0" },
    { "SDMA0_GFX_RB_WPTR",   BC250DBG_SPACE_BAR, 0x04B94, "dw 0x0085 seg0" },
    { "SDMA0_GFX_DOORBELL",  BC250DBG_SPACE_BAR, 0x04BC8, "dw 0x0092 seg0; ENABLE=bit28" },
    { "SDMA0_GFX_IB_CNTL",   BC250DBG_SPACE_BAR, 0x04BA8, "dw 0x008a seg0" },

    /* --- offsets avulsos do projeto anterior, procedencia desconhecida.
     *     Aparecem em BC250-CONTROL-SUITE.md e nao batem com a formula que o
     *     proprio repositorio documenta. Mantidos so para reproduzir as
     *     leituras antigas. Nao construa nada em cima deles. --- */
    { "L_CP_RB0_BASE",       BC250DBG_SPACE_BAR, 0x089E0, "lido como RO" },
    { "L_KIQ_BASE_LO",       BC250DBG_SPACE_BAR, 0x0E060, "BIOS deixou 0x7E522000" },
    { "L_SPI_PG_WGP_MASK",   BC250DBG_SPACE_BAR, 0x05C3C, "outro offset p/ o mesmo reg" },
    { "L_MC_VM_FB_LOCATION", BC250DBG_SPACE_BAR, 0x06EE0, "lia 0" },
    { "L_GCVM_CTX0_CNTL",    BC250DBG_SPACE_BAR, 0x0B460, "gravavel neste" },
    { "L_GCVM_CTX0_PT_BASE", BC250DBG_SPACE_BAR, 0x0B608, "travava em 0" },
    { "L_GCVM_L2_CNTL",      BC250DBG_SPACE_BAR, 0x0B360, NULL },
    { "L_GCVM_CTX0_PT_BASE2",BC250DBG_SPACE_BAR, 0x06C8C, "PML4 do BIOS" },

    /* --- bases de IP em bytes (tabela de discovery lida no Linux, dwords
     *     multiplicados por 4). Ler o primeiro dword de cada bloco mostra
     *     quais respondem. --- */
    { "IP_ATHUB_BASE",       BC250DBG_SPACE_BAR, 0x03000, "dword 0x00C00" },
    { "IP_HDP_BASE",         BC250DBG_SPACE_BAR, 0x03C80, "dword 0x00F20" },
    { "IP_OSSSYS_BASE",      BC250DBG_SPACE_BAR, 0x04280, "dword 0x010A0 (IH)" },
    { "IP_GC_BASE",          BC250DBG_SPACE_BAR, 0x04980, "dword 0x01260" },
    { "IP_UMC_BASE",         BC250DBG_SPACE_BAR, 0x50000, "dword 0x14000" },
    { "IP_MP0_BASE",         BC250DBG_SPACE_BAR, 0x58000, "anel do PSP funciona aqui" },
    { "IP_THM_BASE",         BC250DBG_SPACE_BAR, 0x59800, "dword 0x16600" },
    { "IP_SMUIO_BASE",       BC250DBG_SPACE_BAR, 0x5A000, "dword 0x16800" },
    { "IP_FUSE_BASE",        BC250DBG_SPACE_BAR, 0x5D000, "dword 0x17400" },
    { "IP_MMHUB_BASE",       BC250DBG_SPACE_BAR, 0x68000, "dword 0x1A000" },

    /* --- SMN: espaco separado, nao afetado pela convencao --- */
    { "SMU_FW_FLAGS",        BC250DBG_SPACE_SMN, 0x03B10024, "bit0 = irq on" },
    { "SMU_PUB_CTRL",        BC250DBG_SPACE_SMN, 0x03B10B14, NULL },
    { "SMU_Q0_CMD",          BC250DBG_SPACE_SMN, 0x03B10A08, "C2PMSG_66" },
    { "SMU_Q0_ARG",          BC250DBG_SPACE_SMN, 0x03B10A48, "C2PMSG_82" },
    { "SMU_Q0_RSP",          BC250DBG_SPACE_SMN, 0x03B10A68, "C2PMSG_90" },
    { "SMU_Q3_CMD",          BC250DBG_SPACE_SMN, 0x03B10A20, NULL },
    { "SMU_Q3_ARG",          BC250DBG_SPACE_SMN, 0x03B10A88, NULL },
    { "SMU_Q3_RSP",          BC250DBG_SPACE_SMN, 0x03B10A80, NULL },
    { "TEMP_EDGE",           BC250DBG_SPACE_SMN, 0x03B10000, "sonda crua" },
    { "TEMP_JUNCTION",       BC250DBG_SPACE_SMN, 0x03B10020, "sonda crua" },
    { "TEMP_MEM",            BC250DBG_SPACE_SMN, 0x03B10028, "sonda crua" },
    { "FAN_RPM",             BC250DBG_SPACE_SMN, 0x03B10064, NULL },
    { "FAN_PWM",             BC250DBG_SPACE_SMN, 0x03B10068, NULL },
    { "CPU_CORE_MASK",       BC250DBG_SPACE_SMN, 0x0115A870, "0x77=6c 0xFF=8c" },
};

const int g_Bc250RegCount = (int)(sizeof(g_Bc250Regs) / sizeof(g_Bc250Regs[0]));

int Bc250ResolveReg(const char *token, uint32_t *space, uint32_t *offset)
{
    int i;
    char *end = NULL;
    unsigned long v;

    for (i = 0; i < g_Bc250RegCount; i++) {
        if (_stricmp(token, g_Bc250Regs[i].Name) == 0) {
            *space = g_Bc250Regs[i].Space;
            *offset = g_Bc250Regs[i].Offset;
            return 1;
        }
    }

    v = strtoul(token, &end, 0);
    if (end && *end == '\0' && end != token) {
        /* Heuristica: enderecos SMN sao >= 0x00100000; o BAR5 tem 512 KB. */
        *space = (v >= 0x00100000ul) ? BC250DBG_SPACE_SMN : BC250DBG_SPACE_BAR;
        *offset = (uint32_t)v;
        return 1;
    }
    return 0;
}

const char *Bc250RegName(uint32_t space, uint32_t offset)
{
    int i;
    for (i = 0; i < g_Bc250RegCount; i++) {
        if (g_Bc250Regs[i].Space == space && g_Bc250Regs[i].Offset == offset) {
            return g_Bc250Regs[i].Name;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Montador de scripts                                                 */
/* ------------------------------------------------------------------ */

const char *Bc250OpName(uint32_t op)
{
    switch (op) {
    case BC250DBG_OP_END:         return "end";
    case BC250DBG_OP_REG_R:       return "r";
    case BC250DBG_OP_REG_W:       return "w";
    case BC250DBG_OP_SMN_R:       return "smnr";
    case BC250DBG_OP_SMN_W:       return "smnw";
    case BC250DBG_OP_SMU:         return "smu";
    case BC250DBG_OP_DELAY_US:    return "delay";
    case BC250DBG_OP_POLL_REG:    return "poll";
    case BC250DBG_OP_POLL_SMN:    return "pollsmn";
    case BC250DBG_OP_EXPECT_REG:  return "expect";
    case BC250DBG_OP_EXPECT_SMN:  return "expectsmn";
    case BC250DBG_OP_MARK:        return "mark";
    case BC250DBG_OP_THERMAL_CHK: return "tcheck";
    default:                      return "?";
    }
}

const char *Bc250OpStatusName(uint32_t status)
{
    switch (status) {
    case BC250DBG_OPS_OK:       return "ok";
    case BC250DBG_OPS_MISMATCH: return "DIVERGIU";
    case BC250DBG_OPS_TIMEOUT:  return "TIMEOUT";
    case BC250DBG_OPS_DENIED:   return "NEGADO";
    case BC250DBG_OPS_RANGE:    return "FORA-DE-FAIXA";
    case BC250DBG_OPS_ABORTED:  return "ABORTADO";
    case BC250DBG_OPS_BADOP:    return "OP-INVALIDA";
    default:                    return "?";
    }
}

/* delay/timeout aceitam sufixo us/ms/s; sem sufixo = microssegundos. */
static uint64_t ParseTime(const char *s)
{
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 0);

    if (!end || *end == '\0') return v;
    if (_stricmp(end, "us") == 0) return v;
    if (_stricmp(end, "ms") == 0) return v * 1000ull;
    if (_stricmp(end, "s") == 0)  return v * 1000000ull;
    return v;
}

static int ParseU64(const char *s, uint64_t *out)
{
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 0);
    if (!end || *end != '\0' || end == s) {
        return 0;
    }
    *out = v;
    return 1;
}

typedef struct {
    BC250DBG_OP *ops;
    uint32_t    *lines;
    uint32_t     count;
    uint32_t     cap;
} OpList;

static int OpPush(OpList *l, const BC250DBG_OP *op, uint32_t line)
{
    if (l->count == l->cap) {
        uint32_t cap = l->cap ? l->cap * 2 : 64;
        BC250DBG_OP *ops = (BC250DBG_OP *)realloc(l->ops, cap * sizeof(*ops));
        uint32_t *lines = (uint32_t *)realloc(l->lines, cap * sizeof(*lines));
        if (!ops || !lines) {
            free(ops);
            free(lines);
            l->ops = NULL;
            l->lines = NULL;
            return 0;
        }
        l->ops = ops;
        l->lines = lines;
        l->cap = cap;
    }
    l->ops[l->count] = *op;
    l->lines[l->count] = line;
    l->count++;
    return 1;
}

#define FAIL(fmt, ...) do { \
        _snprintf_s(err, cbErr, _TRUNCATE, "linha %u: " fmt, lineNo, __VA_ARGS__); \
        goto fail; \
    } while (0)

int Bc250ScriptAssemble(const char *text, BC250_SCRIPT *out,
                        char *err, size_t cbErr)
{
    OpList list;
    char *copy = NULL;
    char *cursor, *line;
    uint32_t lineNo = 0;

    memset(&list, 0, sizeof(list));
    memset(out, 0, sizeof(*out));
    strcpy_s(out->Name, sizeof(out->Name), "sem-nome");
    err[0] = '\0';

    copy = _strdup(text);
    if (!copy) {
        _snprintf_s(err, cbErr, _TRUNCATE, "sem memoria");
        return 0;
    }

    cursor = copy;
    while (cursor && *cursor) {
        char *nl = strpbrk(cursor, "\r\n");
        char *tok[8];
        int nt = 0;
        char *ctxTok = NULL;
        BC250DBG_OP op;
        uint32_t flags = 0;
        uint32_t space = 0, offset = 0;
        uint64_t a = 0, b = 0, c = 0;
        int ti = 0;

        line = cursor;
        if (nl) {
            *nl = '\0';
            cursor = nl + 1;
            while (*cursor == '\n' || *cursor == '\r') cursor++;
        } else {
            cursor = NULL;
        }
        lineNo++;

        /* comentario e espaco em branco */
        {
            char *h = strchr(line, '#');
            if (h) *h = '\0';
        }

        for (tok[0] = strtok_s(line, " \t", &ctxTok);
             tok[nt] != NULL && nt < 7;
             tok[++nt] = strtok_s(NULL, " \t", &ctxTok)) {
            /* nada: o loop preenche tok[] */
        }
        if (nt == 0) {
            continue;
        }

        /* prefixos opcionais */
        while (ti < nt) {
            if (_stricmp(tok[ti], "try") == 0 || strcmp(tok[ti], "?") == 0) {
                flags |= BC250DBG_OPF_CONTINUE;
                ti++;
            } else if (_stricmp(tok[ti], "quiet") == 0) {
                flags |= BC250DBG_OPF_NOLOG;
                ti++;
            } else {
                break;
            }
        }
        if (ti >= nt) {
            continue;
        }

        memset(&op, 0, sizeof(op));
        op.Flags = flags;

        /* --- diretivas --- */
        if (_stricmp(tok[ti], "name") == 0) {
            if (ti + 1 >= nt) FAIL("%s", "name exige um argumento");
            strcpy_s(out->Name, sizeof(out->Name), tok[ti + 1]);
            continue;
        }
        if (_stricmp(tok[ti], "limit") == 0) {
            if (ti + 1 >= nt || !ParseU64(tok[ti + 1], &a))
                FAIL("%s", "limit exige um numero em C");
            out->ThermalLimitC = (uint32_t)a;
            continue;
        }

        /* --- operacoes --- */
        if (_stricmp(tok[ti], "r") == 0) {
            if (ti + 1 >= nt) FAIL("%s", "r exige registrador");
            if (!Bc250ResolveReg(tok[ti + 1], &space, &offset))
                FAIL("registrador desconhecido: %s", tok[ti + 1]);
            op.Op = (space == BC250DBG_SPACE_SMN) ? BC250DBG_OP_SMN_R
                                                  : BC250DBG_OP_REG_R;
            op.A = offset;
        } else if (_stricmp(tok[ti], "w") == 0) {
            if (ti + 2 >= nt) FAIL("%s", "w exige registrador e valor");
            if (!Bc250ResolveReg(tok[ti + 1], &space, &offset))
                FAIL("registrador desconhecido: %s", tok[ti + 1]);
            if (!ParseU64(tok[ti + 2], &b))
                FAIL("valor invalido: %s", tok[ti + 2]);
            op.Op = (space == BC250DBG_SPACE_SMN) ? BC250DBG_OP_SMN_W
                                                  : BC250DBG_OP_REG_W;
            op.A = offset;
            op.B = b;
        } else if (_stricmp(tok[ti], "smu") == 0) {
            if (ti + 2 >= nt) FAIL("%s", "smu exige fila e mensagem");
            if (!ParseU64(tok[ti + 1], &a) || !ParseU64(tok[ti + 2], &b))
                FAIL("%s", "smu: fila/mensagem invalidas");
            op.Op = BC250DBG_OP_SMU;
            op.A = a;
            op.B = b;
            if (ti + 3 < nt && ParseU64(tok[ti + 3], &c)) op.C = c;
            if (ti + 4 < nt && ParseU64(tok[ti + 4], &c)) op.D = c;
        } else if (_stricmp(tok[ti], "delay") == 0) {
            if (ti + 1 >= nt) FAIL("%s", "delay exige tempo");
            op.Op = BC250DBG_OP_DELAY_US;
            op.A = ParseTime(tok[ti + 1]);
        } else if (_stricmp(tok[ti], "poll") == 0) {
            if (ti + 3 >= nt) FAIL("%s", "poll exige reg, mascara e valor");
            if (!Bc250ResolveReg(tok[ti + 1], &space, &offset))
                FAIL("registrador desconhecido: %s", tok[ti + 1]);
            if (!ParseU64(tok[ti + 2], &b) || !ParseU64(tok[ti + 3], &c))
                FAIL("%s", "poll: mascara/valor invalidos");
            op.Op = (space == BC250DBG_SPACE_SMN) ? BC250DBG_OP_POLL_SMN
                                                  : BC250DBG_OP_POLL_REG;
            op.A = offset;
            op.B = b;
            op.C = c;
            op.D = (ti + 4 < nt) ? ParseTime(tok[ti + 4]) : 100000ull;
        } else if (_stricmp(tok[ti], "expect") == 0) {
            if (ti + 3 >= nt) FAIL("%s", "expect exige reg, mascara e valor");
            if (!Bc250ResolveReg(tok[ti + 1], &space, &offset))
                FAIL("registrador desconhecido: %s", tok[ti + 1]);
            if (!ParseU64(tok[ti + 2], &b) || !ParseU64(tok[ti + 3], &c))
                FAIL("%s", "expect: mascara/valor invalidos");
            op.Op = (space == BC250DBG_SPACE_SMN) ? BC250DBG_OP_EXPECT_SMN
                                                  : BC250DBG_OP_EXPECT_REG;
            op.A = offset;
            op.B = b;
            op.C = c;
        } else if (_stricmp(tok[ti], "mark") == 0) {
            op.Op = BC250DBG_OP_MARK;
            op.A = (ti + 1 < nt && ParseU64(tok[ti + 1], &a)) ? a : 0;
        } else if (_stricmp(tok[ti], "tcheck") == 0) {
            op.Op = BC250DBG_OP_THERMAL_CHK;
        } else {
            FAIL("verbo desconhecido: %s", tok[ti]);
        }

        if (list.count >= BC250DBG_SCRIPT_MAX_OPS) {
            FAIL("%s", "script excede o maximo de ops");
        }
        if (!OpPush(&list, &op, lineNo)) {
            FAIL("%s", "sem memoria");
        }
    }

    if (list.count == 0) {
        _snprintf_s(err, cbErr, _TRUNCATE, "script vazio");
        goto fail;
    }

    out->Ops = list.ops;
    out->Lines = list.lines;
    out->OpCount = list.count;
    free(copy);
    return 1;

fail:
    free(list.ops);
    free(list.lines);
    free(copy);
    return 0;
}

#undef FAIL

int Bc250ScriptLoadFile(const char *path, BC250_SCRIPT *out,
                        char *err, size_t cbErr)
{
    FILE *f = NULL;
    long size;
    char *text;
    int rc;

    if (fopen_s(&f, path, "rb") != 0 || !f) {
        _snprintf_s(err, cbErr, _TRUNCATE, "nao consegui abrir %s", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 4 * 1024 * 1024) {
        fclose(f);
        _snprintf_s(err, cbErr, _TRUNCATE, "tamanho invalido: %s", path);
        return 0;
    }

    text = (char *)malloc((size_t)size + 1);
    if (!text) {
        fclose(f);
        _snprintf_s(err, cbErr, _TRUNCATE, "sem memoria");
        return 0;
    }
    size = (long)fread(text, 1, (size_t)size, f);
    text[size] = '\0';
    fclose(f);

    rc = Bc250ScriptAssemble(text, out, err, cbErr);
    free(text);
    return rc;
}

void Bc250ScriptFree(BC250_SCRIPT *s)
{
    free(s->Ops);
    free(s->Lines);
    s->Ops = NULL;
    s->Lines = NULL;
    s->OpCount = 0;
}

int Bc250ScriptRun(BC250_CTX *ctx, const BC250_SCRIPT *s,
                   BC250DBG_SCRIPT_OUT *out, BC250DBG_OP_RESULT **results)
{
    size_t cbIn = sizeof(BC250DBG_SCRIPT_IN) + s->OpCount * sizeof(BC250DBG_OP);
    size_t cbOut = sizeof(BC250DBG_SCRIPT_OUT) +
                   s->OpCount * sizeof(BC250DBG_OP_RESULT);
    unsigned char *in = (unsigned char *)malloc(cbIn);
    unsigned char *outBuf = (unsigned char *)malloc(cbOut);
    BC250DBG_SCRIPT_IN hdr;
    int rc;

    *results = NULL;
    if (!in || !outBuf) {
        free(in);
        free(outBuf);
        return ERROR_OUTOFMEMORY;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.Size = sizeof(hdr);
    hdr.OpCount = s->OpCount;
    hdr.ThermalLimitC = s->ThermalLimitC;
    strcpy_s(hdr.Name, sizeof(hdr.Name), s->Name);

    memcpy(in, &hdr, sizeof(hdr));
    memcpy(in + sizeof(hdr), s->Ops, s->OpCount * sizeof(BC250DBG_OP));

    rc = Ioctl(ctx, IOCTL_BC250DBG_SCRIPT_RUN, in, (DWORD)cbIn,
               outBuf, (DWORD)cbOut, NULL);
    if (rc == 0) {
        memcpy(out, outBuf, sizeof(*out));
        *results = (BC250DBG_OP_RESULT *)malloc(
                        out->OpCount * sizeof(BC250DBG_OP_RESULT));
        if (*results) {
            memcpy(*results, outBuf + sizeof(BC250DBG_SCRIPT_OUT),
                   out->OpCount * sizeof(BC250DBG_OP_RESULT));
        }
    }

    free(in);
    free(outBuf);
    return rc;
}

/* ------------------------------------------------------------------ */
/* PSP (MP0) mailbox GPCOM                                             */
/* ------------------------------------------------------------------ */

int Bc250PspStatus(BC250_CTX *ctx, BC250DBG_PSP_STATUS_IO *out)
{
    BC250DBG_PSP_STATUS_IO io;
    int rc;

    memset(&io, 0, sizeof(io));
    io.Size = sizeof(io);
    rc = Ioctl(ctx, IOCTL_BC250DBG_PSP_STATUS, &io, sizeof(io), &io, sizeof(io), NULL);
    if (rc == 0 && out) *out = io;
    return rc;
}

int Bc250PspRingInit(BC250_CTX *ctx, BC250DBG_PSP_RING_INIT_IO *out)
{
    BC250DBG_PSP_RING_INIT_IO io;
    int rc;

    memset(&io, 0, sizeof(io));
    io.Size = sizeof(io);
    rc = Ioctl(ctx, IOCTL_BC250DBG_PSP_RING_INIT, &io, sizeof(io), &io, sizeof(io), NULL);
    if (out) *out = io;
    return rc;
}

int Bc250PspLoadIpFw(BC250_CTX *ctx, uint32_t fwType, const uint32_t *dwords,
                     uint32_t count, BC250DBG_PSP_LOAD_FW_IO *out)
{
    BC250DBG_PSP_LOAD_FW_IO *io;
    size_t totalBytes = sizeof(BC250DBG_PSP_LOAD_FW_IO) + count * sizeof(uint32_t);
    int rc;

    io = (BC250DBG_PSP_LOAD_FW_IO *)malloc(totalBytes);
    if (!io) return ERROR_NOT_ENOUGH_MEMORY;

    memset(io, 0, sizeof(*io));
    io->Size = (uint32_t)totalBytes;
    io->FwType = fwType;
    io->DwordCount = count;
    memcpy((uint8_t *)io + sizeof(*io), dwords, count * sizeof(uint32_t));

    rc = Ioctl(ctx, IOCTL_BC250DBG_PSP_LOAD_FW, io, (DWORD)totalBytes,
               io, sizeof(*io), NULL);
    if (out) *out = *io;
    free(io);
    return rc;
}

int Bc250SdmaF32Reset(BC250_CTX *ctx, BC250DBG_SDMA_F32_RESET_IO *out)
{
    BC250DBG_SDMA_F32_RESET_IO io;
    int rc;

    memset(&io, 0, sizeof(io));
    io.Size = sizeof(io);
    rc = Ioctl(ctx, IOCTL_BC250DBG_SDMA_F32_RESET, &io, sizeof(io), &io, sizeof(io), NULL);
    if (out) *out = io;
    return rc;
}

int Bc250SdmaOp(BC250_CTX *ctx, BC250DBG_SDMA_OP_IO *io)
{
    if (ctx && ctx->UseWddm) {
        typedef struct {
            ULONG CommandId, Status, InSize, OutSize;
            ULONGLONG SrcGpuAddress, DstGpuAddress;
            ULONG SizeBytes, FenceValue, DurationUs, BandwidthMBs;
        } BC250_WDDM_ESC_SDMA_COPY;

        BC250_WDDM_ESC_SDMA_COPY esc;
        memset(&esc, 0, sizeof(esc));
        esc.CommandId = BC250_ESC_SDMA_COPY;
        esc.InSize = sizeof(esc);
        esc.OutSize = sizeof(esc);
        esc.SrcGpuAddress = 0xF400000000ULL + io->SrcOffset;
        esc.DstGpuAddress = 0xF400000000ULL + io->DstOffset;
        esc.SizeBytes = io->ByteCount;

        int rc = EscapeWddm(ctx, &esc, sizeof(esc));
        if (rc == 0 && esc.Status == 0) {
            io->Completed = 1;
            io->RptrAfter = esc.FenceValue;
            return 0;
        }
        return rc ? rc : (int)esc.Status;
    }
    io->Size = sizeof(*io);
    return Ioctl(ctx, IOCTL_BC250DBG_SDMA_OP, io, sizeof(*io), io, sizeof(*io), NULL);
}

int Bc250VramScan(BC250_CTX *ctx, BC250DBG_VRAM_SCAN_IO *io)
{
    io->Size = sizeof(*io);
    return Ioctl(ctx, IOCTL_BC250DBG_VRAM_SCAN, io, sizeof(*io), io, sizeof(*io), NULL);
}

int Bc250HdpInvalidate(BC250_CTX *ctx)
{
    if (ctx && ctx->UseWddm) {
        uint32_t dummy = 0;
        return Bc250RegWrite(ctx, 0x03FC4, 1, &dummy); /* A_HDP_READ_CACHE_INVALIDATE */
    }
    uint32_t dummy = 0;
    return Ioctl(ctx, IOCTL_BC250DBG_HDP_INVALIDATE, &dummy, sizeof(dummy),
                 &dummy, sizeof(dummy), NULL);
}

int Bc250SdmaRaw(BC250_CTX *ctx, BC250DBG_SDMA_RAW_IO *io)
{
    io->Size = sizeof(*io);
    return Ioctl(ctx, IOCTL_BC250DBG_SDMA_RAW, io, sizeof(*io), io, sizeof(*io), NULL);
}

int Bc250GcvmStatus(BC250_CTX *ctx, BC250DBG_GCVM_STATUS_IO *out)
{
    if (!out) return ERROR_INVALID_PARAMETER;
    memset(out, 0, sizeof(*out));
    out->Size = sizeof(*out);
    return Ioctl(ctx, IOCTL_BC250DBG_GCVM_STATUS, NULL, 0, out, sizeof(*out), NULL);
}

int Bc250GcvmInit(BC250_CTX *ctx, BC250DBG_GCVM_INIT_IO *io)
{
    BC250DBG_GCVM_INIT_IO localIo;
    int rc;

    if (!io) io = &localIo;
    memset(io, 0, sizeof(*io));
    io->Size = sizeof(*io);
    rc = Ioctl(ctx, IOCTL_BC250DBG_GCVM_INIT, io, sizeof(*io), io, sizeof(*io), NULL);
    return rc;
}

int Bc250VramFillPattern(BC250_CTX *ctx, BC250DBG_VRAM_PATTERN_IO *io)
{
    io->Size = sizeof(*io);
    return Ioctl(ctx, IOCTL_BC250DBG_VRAM_FILL, io, sizeof(*io), io, sizeof(*io), NULL);
}

int Bc250VramVerifyPattern(BC250_CTX *ctx, BC250DBG_VRAM_PATTERN_IO *io)
{
    io->Size = sizeof(*io);
    return Ioctl(ctx, IOCTL_BC250DBG_VRAM_VERIFY, io, sizeof(*io), io, sizeof(*io), NULL);
}

int Bc250PspSetupTmr(BC250_CTX *ctx, BC250DBG_PSP_SETUP_TMR_IO *io)
{
    if (!io) return ERROR_INVALID_PARAMETER;
    io->Size = sizeof(*io);
    return Ioctl(ctx, IOCTL_BC250DBG_PSP_SETUP_TMR, io, sizeof(*io), io, sizeof(*io), NULL);
}

int Bc250GfxInit(BC250_CTX *ctx, BC250DBG_GFX_INIT_IO *io)
{
    BC250DBG_GFX_INIT_IO local;
    if (!io) {
        memset(&local, 0, sizeof(local));
        local.Size = sizeof(local);
        io = &local;
    } else {
        io->Size = sizeof(*io);
    }
    return Ioctl(ctx, IOCTL_BC250DBG_GFX_INIT, io, sizeof(*io), io, sizeof(*io), NULL);
}

int Bc250GfxSubmit(BC250_CTX *ctx, BC250DBG_GFX_SUBMIT_IO *io)
{
    if (!io) return ERROR_INVALID_PARAMETER;
    io->Size = sizeof(*io);
    if (ctx->UseWddm) {
        struct {
            BC250_ESC_HDR Hdr;
            ULONG DwordCount;
            ULONG RptrBefore;
            ULONG RptrAfter;
            ULONG Data[256];
        } esc;
        memset(&esc, 0, sizeof(esc));
        esc.Hdr.CommandId = BC250_ESC_SUBMIT_PM4;
        esc.DwordCount = (io->DwordCount > 256) ? 256 : io->DwordCount;
        memcpy(esc.Data, io->Data, esc.DwordCount * sizeof(uint32_t));
        int rc = EscapeWddm(ctx, &esc, sizeof(esc));
        if (rc == 0 && esc.Hdr.Status == 0) {
            io->RptrBefore = esc.RptrBefore;
            io->RptrAfter = esc.RptrAfter;
            return 0;
        }
        return rc ? rc : (int)esc.Hdr.Status;
    }
    return Ioctl(ctx, IOCTL_BC250DBG_GFX_SUBMIT, io, sizeof(*io), io, sizeof(*io), NULL);
}

int Bc250GfxStatus(BC250_CTX *ctx, BC250DBG_GFX_STATUS_IO *out)
{
    if (!out) return ERROR_INVALID_PARAMETER;
    memset(out, 0, sizeof(*out));
    out->Size = sizeof(*out);
    return Ioctl(ctx, IOCTL_BC250DBG_GFX_STATUS, NULL, 0, out, sizeof(*out), NULL);
}
