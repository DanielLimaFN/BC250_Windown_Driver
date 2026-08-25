/* bc250lib.h — biblioteca user-mode compartilhada entre o CLI e a GUI.
 *
 * Cobre quatro coisas:
 *   1. abrir o driver e falar os IOCTLs;
 *   2. mapear o log ring e drenar entradas sem copia;
 *   3. mapa de registradores nomeados (nada de offset cru na mao);
 *   4. montador de scripts .bcs (texto -> ops binarias).
 *
 * O parser de texto vive AQUI, no user mode, de proposito: o kernel so
 * recebe uma lista de ops ja validada.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BC250LIB_H
#define BC250LIB_H

#include <windows.h>
#include <stdint.h>
#include "../../inc/bc250dbg_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Conexao                                                             */
/* ------------------------------------------------------------------ */

typedef struct BC250_CTX {
    HANDLE  Device;
    uint32_t D3DAdapter;
    uint32_t UseWddm;
    /* log ring mapeado */
    const BC250DBG_LOG_HEADER *LogHeader;
    const BC250DBG_LOG_ENTRY  *LogEntries;
    uint32_t LogCount;
    uint32_t LogMask;
    int64_t  LogTail;        /* ultima sequencia consumida               */
    int64_t  LogDropped;     /* perdas acumuladas por lap do ring        */
    uint64_t QpcFreq;
    uint64_t QpcBase;        /* primeira entrada vista, para tempo rel.  */
} BC250_CTX;

int  Bc250Open(BC250_CTX *ctx);           /* 0 = ok, senao GetLastError  */
void Bc250Close(BC250_CTX *ctx);

int  Bc250Info(BC250_CTX *ctx, BC250DBG_INFO *out);
int  Bc250RegRead(BC250_CTX *ctx, uint32_t off, uint32_t *value);
int  Bc250RegWrite(BC250_CTX *ctx, uint32_t off, uint32_t value,
                   uint32_t *readback);
int  Bc250SmnRead(BC250_CTX *ctx, uint32_t addr, uint32_t *value);
int  Bc250SmnWrite(BC250_CTX *ctx, uint32_t addr, uint32_t value,
                   uint32_t *readback);
int  Bc250SmuMsg(BC250_CTX *ctx, uint32_t queue, uint32_t msg, uint32_t arg,
                 uint32_t argHi, uint32_t *status, uint32_t *result);
int  Bc250Arm(BC250_CTX *ctx, uint32_t level);
int  Bc250Snapshot(BC250_CTX *ctx, uint32_t space, uint32_t start,
                   uint32_t count, uint32_t *dst);
int  Bc250ThermalGet(BC250_CTX *ctx, BC250DBG_THERMAL *out);
int  Bc250ThermalSet(BC250_CTX *ctx, const BC250DBG_THERMAL_SET *in);
int  Bc250LogSetMask(BC250_CTX *ctx, uint32_t levelMask, uint32_t subsysMask);

/* ------------------------------------------------------------------ */
/* SDMA Ring Buffer                                                   */
/* ------------------------------------------------------------------ */

int  Bc250SdmaInit(BC250_CTX *ctx, uint32_t ringSizeDwords, uint32_t placement,
                   uint32_t vramOffset, uint64_t *outGpuAddr, uint32_t *outRbCntl);
int  Bc250VramRead(BC250_CTX *ctx, uint32_t off, uint32_t *value);
int  Bc250VramWrite(BC250_CTX *ctx, uint32_t off, uint32_t value, uint32_t *readback);
int  Bc250SdmaSubmit(BC250_CTX *ctx, const uint32_t *dwords, uint32_t count, uint32_t *rptrBefore, uint32_t *wptrAfter, uint32_t *rptrAfter);
int  Bc250SdmaStatus(BC250_CTX *ctx, BC250DBG_SDMA_STATUS_IO *out);
int  Bc250SdmaLoadFirmware(BC250_CTX *ctx, const uint32_t *dwords, uint32_t count, uint32_t *outVersion, uint32_t *outChecksum);



/* ------------------------------------------------------------------ */
/* Log ring                                                            */
/* ------------------------------------------------------------------ */

int  Bc250LogAttach(BC250_CTX *ctx);
void Bc250LogDetach(BC250_CTX *ctx);
/* Posiciona o consumidor no fim do ring (so entradas novas). */
void Bc250LogSeekEnd(BC250_CTX *ctx);
/* Copia ate max entradas novas; devolve quantas. Atualiza LogDropped. */
int  Bc250LogDrain(BC250_CTX *ctx, BC250DBG_LOG_ENTRY *dst, int max);
/* Renderiza uma entrada em uma linha legivel. */
void Bc250LogFormat(const BC250_CTX *ctx, const BC250DBG_LOG_ENTRY *e,
                    char *buf, size_t cb);

const char *Bc250LevelName(uint32_t level);
const char *Bc250SubsysName(uint32_t subsys);

/* ------------------------------------------------------------------ */
/* Mapa de registradores nomeados                                      */
/* ------------------------------------------------------------------ */

typedef struct BC250_REGDEF {
    const char *Name;
    uint32_t    Space;      /* BC250DBG_SPACE_BAR | BC250DBG_SPACE_SMN     */
    uint32_t    Offset;
    const char *Note;
} BC250_REGDEF;

extern const BC250_REGDEF g_Bc250Regs[];
extern const int          g_Bc250RegCount;

/* Resolve nome ou literal numerico. 0 = falhou. */
int Bc250ResolveReg(const char *token, uint32_t *space, uint32_t *offset);
/* Nome conhecido para um offset, ou NULL. */
const char *Bc250RegName(uint32_t space, uint32_t offset);

/* ------------------------------------------------------------------ */
/* Montador de scripts .bcs                                            */
/* ------------------------------------------------------------------ */

typedef struct BC250_SCRIPT {
    char          Name[32];
    uint32_t      ThermalLimitC;
    BC250DBG_OP  *Ops;
    uint32_t      OpCount;
    /* linha de origem de cada op, para mensagens de erro e relatorio */
    uint32_t     *Lines;
} BC250_SCRIPT;

/* Monta a partir de texto. Em erro devolve 0 e preenche err. */
int  Bc250ScriptAssemble(const char *text, BC250_SCRIPT *out,
                         char *err, size_t cbErr);
int  Bc250ScriptLoadFile(const char *path, BC250_SCRIPT *out,
                         char *err, size_t cbErr);
void Bc250ScriptFree(BC250_SCRIPT *s);

/* Executa e devolve os resultados (o chamador libera com free()). */
int  Bc250ScriptRun(BC250_CTX *ctx, const BC250_SCRIPT *s,
                    BC250DBG_SCRIPT_OUT *out, BC250DBG_OP_RESULT **results);

const char *Bc250OpName(uint32_t op);
const char *Bc250OpStatusName(uint32_t status);

int  Bc250SdmaF32Reset(BC250_CTX *ctx, BC250DBG_SDMA_F32_RESET_IO *out);
int  Bc250SdmaOp(BC250_CTX *ctx, BC250DBG_SDMA_OP_IO *io);
int  Bc250VramScan(BC250_CTX *ctx, BC250DBG_VRAM_SCAN_IO *io);
int  Bc250VramFillPattern(BC250_CTX *ctx, BC250DBG_VRAM_PATTERN_IO *io);
int  Bc250VramVerifyPattern(BC250_CTX *ctx, BC250DBG_VRAM_PATTERN_IO *io);
int  Bc250SdmaRaw(BC250_CTX *ctx, BC250DBG_SDMA_RAW_IO *io);
int  Bc250HdpInvalidate(BC250_CTX *ctx);
int  Bc250PspStatus(BC250_CTX *ctx, BC250DBG_PSP_STATUS_IO *out);
int  Bc250PspRingInit(BC250_CTX *ctx, BC250DBG_PSP_RING_INIT_IO *out);
int  Bc250PspSetupTmr(BC250_CTX *ctx, BC250DBG_PSP_SETUP_TMR_IO *io);
int  Bc250PspLoadIpFw(BC250_CTX *ctx, uint32_t fwType, const uint32_t *dwords,
                      uint32_t count, BC250DBG_PSP_LOAD_FW_IO *out);
int  Bc250GcvmStatus(BC250_CTX *ctx, BC250DBG_GCVM_STATUS_IO *out);
int  Bc250GcvmInit(BC250_CTX *ctx, BC250DBG_GCVM_INIT_IO *io);
int  Bc250GfxInit(BC250_CTX *ctx, BC250DBG_GFX_INIT_IO *io);
int  Bc250GfxSubmit(BC250_CTX *ctx, BC250DBG_GFX_SUBMIT_IO *io);
int  Bc250GfxStatus(BC250_CTX *ctx, BC250DBG_GFX_STATUS_IO *out);

#ifdef __cplusplus
}
#endif

#endif /* BC250LIB_H */
