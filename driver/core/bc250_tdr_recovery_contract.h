#ifndef BC250_TDR_RECOVERY_CONTRACT_H
#define BC250_TDR_RECOVERY_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "bc250_adapter_info_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_TDR_ACTION_NONE            0x00000000u
#define BC250_TDR_ACTION_FREEZE          0x00000001u
#define BC250_TDR_ACTION_RESET_POINTERS  0x00000002u
#define BC250_TDR_ACTION_DEQUEUE_HQD     0x00000004u
#define BC250_TDR_ACTION_FAULT_CONTEXT   0x00000008u
#define BC250_TDR_ACTION_RESUME          0x00000010u
#define BC250_TDR_ACTION_FULL_BLOCK_RESET 0x00000020u

typedef enum BC250_TDR_STATUS {
    BC250_TDR_OK = 0,
    BC250_TDR_BAD_ARGUMENT,
    BC250_TDR_INVALID_NODE,
    BC250_TDR_NOT_HANGING,
    BC250_TDR_RESTART_NOT_READY
} BC250_TDR_STATUS;

typedef struct BC250_TDR_ENGINE_STATE {
    uint32_t NodeIndex;
    uint32_t LastRptr;
    uint32_t LastWptr;
    uint32_t HangDwords;
    uint32_t ResetCount;
    uint32_t IsRecovered;
} BC250_TDR_ENGINE_STATE;

typedef struct BC250_TDR_RESET_PLAN {
    uint32_t NodeIndex;
    uint32_t ActionsRequired;
    uint32_t FaultedContextId;
    uint32_t RestoredRptr;
    uint32_t RestoredWptr;
    uint32_t RingBaseReset;
} BC250_TDR_RESET_PLAN;

BC250_TDR_STATUS Bc250TdrDetectHang(
    uint32_t NodeIndex,
    uint32_t Rptr1,
    uint32_t Wptr1,
    uint32_t Rptr2,
    uint32_t Wptr2,
    uint32_t *IsHung);

BC250_TDR_STATUS Bc250TdrBuildResetPlan(
    uint32_t NodeIndex,
    uint32_t FaultedContextId,
    uint32_t CurrentRptr,
    uint32_t CurrentWptr,
    BC250_TDR_RESET_PLAN *Plan);

BC250_TDR_STATUS Bc250TdrExecuteResetPlan(
    const BC250_TDR_RESET_PLAN *Plan,
    BC250_TDR_ENGINE_STATE *State);

/*
 * Gate puro para o readback minimo depois do reset completo da SDMA0.
 * A corrida 07/09 20:41 mediu RPTR=68 depois de o restart publicar WPTR=40;
 * o backend anterior nunca zerava RB_RPTR e apenas removia F32.HALT, portanto
 * podia retomar o POLL_REGMEM que causou o hang. Nenhuma fence de prova deve
 * ser submetida antes de este gate fechar.
 */
BC250_TDR_STATUS Bc250TdrValidateSdmaRestartReadback(
    uint32_t Rptr,
    uint32_t RptrHi,
    uint32_t Wptr,
    uint32_t WptrHi,
    uint32_t RbCntl,
    uint32_t RbEnableMask,
    uint32_t F32Cntl,
    uint32_t F32HaltMask,
    uint32_t GrbmSoftReset,
    uint32_t SdmaResetMask);

/*
 * Gate puro para o readback pos-quiesce/restart da fila MEC (Compute Node 1).
 * Exige fila drenada (ACTIVE=0), sem requisicao de dequeue pendente,
 * sem erro latchado em HQD_ERROR e sem stall no pipeline do CP.
 */
BC250_TDR_STATUS Bc250TdrValidateMecRestartReadback(
    uint32_t HqdActive,
    uint32_t HqdRptr,
    uint32_t HqdWptr,
    uint32_t DequeueRequest,
    uint32_t HqdError,
    uint32_t CpStalled);

const char *Bc250TdrStatusName(BC250_TDR_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
