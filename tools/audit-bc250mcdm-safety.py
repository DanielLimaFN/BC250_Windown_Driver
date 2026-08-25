#!/usr/bin/env python3
"""Fail-closed static gate for the installable BC-250 MCDM package."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]


def require(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle not in text:
        errors.append(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle in text:
        errors.append(f"forbidden {label}: {needle}")


def main() -> int:
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    callbacks_path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    active_test_path = ROOT / "driver" / "tests" / "active_miniport_test.c"
    recovery_path = ROOT / "tools" / "BC250_RECOVERY.cmd"
    inf_path = ROOT / "driver" / "wdk" / "package" / "bc250mcdm.inf"

    active = active_path.read_text(encoding="utf-8")
    callbacks = callbacks_path.read_text(encoding="utf-8")
    active_test = active_test_path.read_text(encoding="utf-8")
    recovery = recovery_path.read_text(encoding="utf-8")
    inf = inf_path.read_text(encoding="utf-8")
    errors: list[str] = []

    # Runtime evidence must not be synthesized by AddDevice/StartDevice.
    forbid(active, "Bc250BuildDefaultActiveRuntime", "synthetic runtime builder", errors)
    forbid(active, "RuntimeOwnershipFlags =", "self-issued ownership", errors)
    forbid(active, "Reservations[0].Offset =", "fixed inherited reservation", errors)
    forbid(active, "ReadonlyGatePassed == 1", "promotion of readonly failure", errors)
    require(active, "InterlockedExchange(&context->IsActive, 0);",
            "StartDevice inactive state", errors)
    require(active, "return preflightStatus;",
            "StartDevice fail-closed return", errors)
    require(active, "Bc250McdmObjectRegistryInitialize(&context->Objects)",
            "owned MCDM object registry initialization", errors)
    require(active, "Bc250McdmObjectRegistryTeardown(",
            "MCDM object teardown on StopDevice", errors)

    # These DDIs must remain explicit failures until backed by owned hardware state.
    for function in (
        "Bc250McdmBuildPagingBuffer",
        "Bc250McdmCreateAllocation",
        "Bc250McdmCreateContext",
        "Bc250McdmPreemptCommand",
        "Bc250McdmQueryEngineStatus",
        "Bc250McdmResetFromTimeout",
        "Bc250McdmRestartFromTimeout",
        "Bc250McdmSetPowerState",
        "Bc250McdmSubmitCommandVirtual",
    ):
        start = callbacks.find(function)
        if start < 0:
            errors.append(f"missing callback: {function}")
            continue
        body = callbacks[start : start + 1800]
        require(body, "STATUS_NOT_SUPPORTED", f"fail-closed {function}", errors)

    require(callbacks, "g_Bc250CreateProcessObject(",
            "CreateProcess lifecycle routing", errors)
    require(active, "Bc250McdmInstallProcessLifecycle(",
            "active-only process lifecycle installation", errors)
    require(active, "Bc250McdmInstallDeviceLifecycle(",
            "active-only device lifecycle installation", errors)
    require(active, "Bc250McdmInstallContextLifecycle(",
            "active-only context lifecycle installation", errors)
    require(active, "ContextAbiReady", 
            "context ABI evidence gate", errors)
    require(active, "Bc250McdmInstallAllocationLifecycle(",
            "active-only allocation lifecycle installation", errors)
    require(active, "Bc250ActiveOpenAllocationLifecycle",
            "device-specific OpenAllocation lifecycle", errors)
    require(active, "Bc250ActiveCloseAllocationLifecycle",
            "device-specific CloseAllocation lifecycle", errors)
    require(active, "Bc250McdmObjectFindAllocationByToken(",
            "versioned allocation-token binding", errors)
    require(callbacks, "g_Bc250OpenAllocationObjects(",
            "OpenAllocation callback routing", errors)
    require(callbacks, "g_Bc250CloseAllocationObjects(",
            "CloseAllocation callback routing", errors)
    require(active, "Bc250ActiveCollectDebugLifecycle",
            "software-only CollectDbgInfo snapshot", errors)
    require(active, "Bc250McdmInstallDebugLifecycle(",
            "active-only debug lifecycle installation", errors)
    require(callbacks, "g_Bc250CollectDebugSnapshot(",
            "CollectDbgInfo callback routing", errors)
    require(active, "Bc250ProbeGetFirstFailure()",
            "first-failure diagnostic preservation", errors)
    debug_start = active.find("Bc250ActiveCollectDebugLifecycle")
    debug_body = active[debug_start : debug_start + 4200]
    for forbidden_debug_access in (
        "READ_REGISTER",
        "WRITE_REGISTER",
        "DxgkCbReadDeviceSpace",
        "Mmio",
    ):
        forbid(debug_body, forbidden_debug_access,
               "hardware access in CollectDbgInfo", errors)
    require(active, "AllocationAbiReady",
            "allocation ABI evidence gate", errors)
    require(active, "Bc250AllocationAbiParse(",
            "versioned allocation private-data validation", errors)
    require(active, "g_Bc250AdapterDirectoryLock", 
            "owned adapter directory for handle-only DDIs", errors)
    require(active, "InterlockedCompareExchange(&Context->IsActive, 1, 1)",
            "object creation active-state gate", errors)
    require(active_test, "Bc250RunStopRaceTest",
            "multithread StopDevice/DDI race harness", errors)
    require(active_test, "BC250_TEST_RACE_ROUNDS 16u",
            "bounded repeated race rounds", errors)
    require(active_test, "activeContext->Objects.LiveCount != 0u",
            "post-race registry-empty invariant", errors)
    require(active_test, "InterlockedCompareExchange64(",
            "atomic exclusive test lock", errors)

    # Recovery must recognize the exact active service/binary identity.
    for needle in (
        "call :remove_service bc250mcdm",
        "call :quarantine_driver bc250mcdm.sys",
        'findstr.exe /I /C:"AddService=bc250mcdm,"',
        'findstr.exe /I /C:"ServiceBinary=%%12%%\\bc250mcdm.sys"',
    ):
        require(recovery, needle, "active recovery coverage", errors)

    for needle in (
        "Class=ComputeAccelerator",
        "PCI\\VEN_1002&DEV_13FE",
        "AddService=bc250mcdm,",
        "ServiceBinary=%12%\\bc250mcdm.sys",
    ):
        require(inf, needle, "strict active INF identity", errors)

    if errors:
        print("BC250 MCDM SAFETY AUDIT: FAILED")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("BC250 MCDM SAFETY AUDIT: PASS")
    print("  synthetic ownership/reservations: absent")
    print("  operational DDIs without hardware backing: fail-closed")
    print("  bc250mcdm recovery identity: covered")
    print("  verdict: NAO VALIDADO EM HARDWARE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
