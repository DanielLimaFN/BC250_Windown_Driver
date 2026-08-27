#ifndef BC250_REBAR_READER_H
#define BC250_REBAR_READER_H

#include <ntddk.h>
#include <dispmprt.h>

#include "../core/bc250_resource_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_REBAR_CONFIG_START 0x100u
#define BC250_REBAR_CONFIG_LIMIT 0x1000u
#define BC250_REBAR_MAX_CHAIN    64u

typedef enum BC250_REBAR_STATUS {
    BC250_REBAR_OK = 0,
    BC250_REBAR_BAD_ARGUMENT,
    BC250_REBAR_INTERFACE_TOO_SMALL,
    BC250_REBAR_CALLBACK_FAILED,
    BC250_REBAR_PARTIAL_READ,
    BC250_REBAR_CHAIN_MALFORMED,
    BC250_REBAR_CHAIN_LOOP,
    BC250_REBAR_NOT_FOUND,
    BC250_REBAR_CAPABILITY_MALFORMED,
    BC250_REBAR_TARGET_NOT_FOUND,
    BC250_REBAR_DUPLICATE_TARGET,
    BC250_REBAR_SELECTED_UNSUPPORTED,
    BC250_REBAR_ASSIGNED_NOT_UNIQUE,
    BC250_REBAR_SIZE_MISMATCH
} BC250_REBAR_STATUS;

typedef struct BC250_REBAR_EVIDENCE {
    uint32_t CapabilityOffset;
    uint32_t BarIndex;
    uint32_t SizesSupportedMask;
    uint32_t SelectedSizeEncoding;
    uint64_t SelectedBytes;
    uint64_t AssignedBytes;
    uint32_t EntryCount;
    uint32_t ReadCount;
} BC250_REBAR_EVIDENCE;

BC250_REBAR_STATUS Bc250ReadRebarReadonly(
    _In_ const DXGKRNL_INTERFACE *DxgkInterface,
    _In_reads_(ResolvedBarCount)
        const BC250_RESOLVED_BAR_RESOURCE *ResolvedBars,
    _In_ size_t ResolvedBarCount,
    _In_ uint32_t TargetBarIndex,
    _Out_ BC250_REBAR_EVIDENCE *Evidence,
    _Out_opt_ NTSTATUS *CallbackStatus);

const char *Bc250RebarStatusName(BC250_REBAR_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
