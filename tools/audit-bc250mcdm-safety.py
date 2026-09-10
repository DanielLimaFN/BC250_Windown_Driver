#!/usr/bin/env python3
"""Fail-closed static gate for the installable BC-250 MCDM package."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]


def require(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle not in text:
        errors.append(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle in text:
        errors.append(f"forbidden {label}: {needle}")


def audit_compute_vmid_isolation(errors: list[str]) -> None:
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    engine = (ROOT / "driver/wdk/bc250_submission_engine.c").read_text(encoding="utf-8")
    core = (ROOT / "driver/core/bc250_gcvm_vmid8_transaction.c").read_text(encoding="utf-8")
    transport = (ROOT / "driver/wdk/bc250_mmio_topology_transport.c").read_text(encoding="utf-8")
    # Every live assignment to the virtual ring must use the dedicated constant.
    assignment_pattern = r"ComputeRing\.Vmid\s*(?<![=!<>])=(?!=)\s*([^;]+);"
    if re.findall(assignment_pattern, active):
        errors.append("active miniport must not retarget ComputeRing.Vmid directly")
    assignments = re.findall(assignment_pattern, engine)
    if not assignments or any(value != "TargetVmid" for value in assignments):
        errors.append("compute VMID assignments must use the validated retarget helper")
    install = core.split("BC250_GCVM_VMID8_STATUS Bc250GcvmComputeInstall(", 1)[1]
    require(install, "if (!Bc250GcvmComputeCanSwitch(Submitted, Retired)) return", "GPU retirement gate before VMID9 writes", errors)
    if install.index("Bc250GcvmComputeCanSwitch") > install.index("ComputeWriteChecked"):
        errors.append("VMID9 writes precede GPU retirement check")
    require(core, "return Submitted == Retired;", "strict retirement equality", errors)
    require(install, "Bc250GcvmComputeRestore(Io, Binding)", "VMID9 rollback", errors)
    require(active, "io.Flush = Bc250ActiveFlushComputeTlb;", "compute-specific TLB ACK", errors)
    require(active, "Bc250ActiveFlushVmidTlb(Context, BC250_GCVM_COMPUTE_VMID)", "VMID9 invalidation", errors)
    require(active, "Bc250GcvmComputeRestore(&computeIo, &context->ComputeVmidBinding)", "VMID9 teardown", errors)
    require(active, "submitStatus = Bc250ActivePrepareComputeVmid(context, &contextView);", "root installed before MEC publication", errors)
    require(active, "if (NT_SUCCESS(submitStatus)) submitStatus = Bc250SubmissionEngineSubmit(", "no publish on root failure", errors)
    if transport.count("Bc250GcvmIsComputeRegister(ByteOffset)") != 2:
        errors.append("VMID9 needs exact read AND write allowlists")


def audit_firmware_wgp_policy_is_readonly(errors: list[str]) -> None:
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    transport = (ROOT / "driver/wdk/bc250_mmio_topology_transport.c").read_text(encoding="utf-8")
    prepare = function_body(active, "static NTSTATUS Bc250ActivePrepareComputeVmid")
    read_allowlist = function_body(
        transport, "static uint32_t Bc250MmioTopologyOffsetAllowed")
    write_allowlist = function_body(
        transport, "static uint32_t Bc250MmioTopologyWriteOffsetAllowed")
    policy_registers = (
        "BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK",
        "BC250_HQD_MMIO_RLC_PG_CNTL",
        "BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK",
    )
    for register in policy_registers:
        require(read_allowlist, register,
                f"read-only telemetry for firmware policy register {register}", errors)
        forbid(write_allowlist, register,
               f"write access to firmware WGP/power policy register {register}", errors)
        if re.search(r"Bc250MmioTopologyWrite32\s*\([^;]*" + register,
                     prepare, flags=re.S):
            errors.append(
                f"PrepareComputeVmid writes firmware WGP/power policy register {register}")
    require(prepare, "Policy=FIRMWARE_PRESERVED",
            "explicit firmware WGP policy telemetry", errors)


def audit_mec_cursor_reconciliation(errors: list[str]) -> None:
    engine = (ROOT / "driver/wdk/bc250_submission_engine.c").read_text(encoding="utf-8")
    core = (ROOT / "driver/core/bc250_persistent_ring_contract.c").read_text(encoding="utf-8")
    helper = function_body(
        engine, "static NTSTATUS Bc250SubmissionReconcileCursorLocked")
    submit = function_body(engine, "NTSTATUS Bc250SubmissionEngineSubmit")
    reconcile = function_body(
        core, "BC250_PRING_STATUS Bc250PersistentRingReconcileDrainedCursor")
    require(reconcile,
            "Ring->LastSubmittedFence != Ring->LastRetiredFence",
            "no-fence-in-flight cursor reconciliation gate", errors)
    require(reconcile, "physicalRptr != physicalWptr",
            "physically drained cursor reconciliation gate", errors)
    require(reconcile, "Ring->WptrDwords = physicalWptr;",
            "logical WPTR rebased from measured physical cursor", errors)
    require(reconcile, "Ring->RptrShadowDwords = physicalRptr;",
            "logical RPTR shadow rebased from measured physical cursor", errors)
    require(helper, "[MEC_CURSOR_RECONCILE] PASS:",
            "measured cursor reconciliation verdict", errors)
    require(helper, "Engine->ComputeWptrShadowVirtual[0] = reconciled;",
            "WPTR shadow synchronized after safe rebase", errors)
    require(helper, "(RPTR nunca escrito)",
            "explicit no-fabricated-RPTR verdict", errors)
    if re.search(r"Bc250SubmissionWrite32\s*\([^;]*BC250_HQD_MMIO_PQ_RPTR",
                 helper, flags=re.S):
        errors.append("MEC cursor reconciliation writes physical RPTR")
    call = "status = Bc250SubmissionReconcileCursorLocked(Context, engine);"
    require(submit, call, "cursor reconciliation before MEC planning", errors)
    plan = "ringStatus = Bc250PersistentRingPlanSubmission(&engine->ComputeRing,"
    if not (0 <= submit.find(call) < submit.find(plan)):
        errors.append("MEC cursor reconciliation must precede submission planning")


def audit_mec_vmid_retarget(errors: list[str]) -> None:
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    engine = (ROOT / "driver/wdk/bc250_submission_engine.c").read_text(encoding="utf-8")
    transport = (ROOT / "driver/wdk/bc250_mmio_topology_transport.c").read_text(encoding="utf-8")
    prepare = function_body(active, "static NTSTATUS Bc250ActivePrepareComputeVmid")
    retarget = function_body(
        engine, "NTSTATUS Bc250SubmissionEngineRetargetDrainedIbVmid")
    read_allowlist = function_body(
        transport, "static uint32_t Bc250MmioTopologyOffsetAllowed")
    write_allowlist = function_body(
        transport, "static uint32_t Bc250MmioTopologyWriteOffsetAllowed")

    require(prepare,
            "Bc250SubmissionEngineRetargetDrainedIbVmid(\n        Context, BC250_GCVM_COMPUTE_VMID)",
            "drained VMID retarget before Node1 publication", errors)
    if re.search(r"Bc250MmioTopologyWrite32\s*\(\s*&Context->Core\.MmioTransport\s*,\s*BC250_HQD_MMIO_VMID",
                 prepare, flags=re.S):
        errors.append("PrepareComputeVmid writes CP_HQD_VMID while HQD may be active")
    require(retarget,
            "LastSubmittedFence !=\n            engine->ComputeRing.LastRetiredFence",
            "no-fence-in-flight VMID retarget gate", errors)
    require(retarget, "Bc250PersistentRingReconcileDrainedCursor(",
            "physical drain proof before VMID retarget", errors)
    off = "BC250_HQD_MMIO_ACTIVE, 0u"
    vmid = "BC250_HQD_MMIO_VMID, targetValue"
    mask = "BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1, queueMask"
    on = "BC250_HQD_MMIO_ACTIVE, 1u"
    for needle, label in ((off, "HQD deactivate"), (vmid, "VMID write"),
                          (mask, "queue-mask rearm"), (on, "HQD reactivate")):
        require(retarget, needle, label, errors)
    if not (0 <= retarget.find(off) < retarget.find(vmid) <
            retarget.find(mask) < retarget.find(on)):
        errors.append("VMID retarget order must be ACTIVE off -> VMID -> queue mask -> ACTIVE on")
    if re.search(r"Bc250SubmissionWrite32\s*\([^;]*BC250_HQD_MMIO_PQ_RPTR",
                 retarget, flags=re.S):
        errors.append("VMID retarget writes physical RPTR")
    require(retarget, "[MEC_VMID_RETARGET]", "measured VMID retarget verdict", errors)

    for register in ("BC250_HQD_MMIO_CP_STALLED_STAT2",
                     "BC250_HQD_MMIO_CP_BUSY_STAT"):
        require(read_allowlist, register,
                f"read-only RCIU diagnostic {register}", errors)
        forbid(write_allowlist, register,
               f"write access to RCIU diagnostic {register}", errors)
    for stage in ("BEFORE_GCVM_INSTALL", "AFTER_GCVM_INSTALL",
                  "PUBLISH_BEFORE_COHERENCY", "PUBLISH_AFTER_COHERENCY",
                  "PUBLISH_AFTER_MMIO_WPTR", "PUBLISH_AFTER_DOORBELL"):
        require(active + engine, stage, f"RCIU stage {stage}", errors)


def audit_submit_dispatch_level_safety(errors: list[str]) -> None:
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    submit_body = function_body(active, "static NTSTATUS Bc250ActiveSubmitCommandVirtualBody")
    submit_lifecycle = function_body(active, "static NTSTATUS Bc250ActiveSubmitCommandVirtualLifecycle")
    for name, code in (("SubmitCommandVirtualBody", submit_body),
                       ("SubmitCommandVirtualLifecycle", submit_lifecycle)):
        if not code:
            errors.append(f"{name} nao localizado para auditar DISPATCH_LEVEL safety")
            continue
        clean_code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
        clean_code = re.sub(r"//.*$", "", clean_code, flags=re.M)
        for forbidden in ("ExAcquirePushLockExclusive", "ExReleasePushLockExclusive",
                          "ExAcquirePushLockShared", "ExReleasePushLockShared",
                          "KeEnterCriticalRegion", "KeLeaveCriticalRegion"):
            if forbidden in clean_code:
                errors.append(f"{name} executa em DISPATCH_LEVEL mas contem {forbidden} (BugCheck 0xD1)")


def audit_device_removal_and_unload_safety(errors: list[str]) -> None:
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    callbacks = (ROOT / "driver/wdk/bc250_mcdm_callbacks.c").read_text(encoding="utf-8")

    remove_body = function_body(active, "NTSTATUS APIENTRY Bc250DxgkDdiRemoveDeviceActive")
    if not remove_body:
        errors.append("Bc250DxgkDdiRemoveDeviceActive nao localizado")
    else:
        require(remove_body, "Bc250ActiveRetirementTimerStop(context);", "timer stop on remove", errors)
        require(remove_body, "Bc250DisplayTeardown();", "display teardown on remove", errors)
        require(remove_body, "KeFlushQueuedDpcs();", "DPC flush on remove", errors)
        require(remove_body, "Bc250ActiveUnregisterAdapter(context);", "adapter unregister on remove", errors)
        require(remove_body, "return STATUS_SUCCESS;", "mandatory success return from RemoveDevice", errors)
        forbid(remove_body, "return status;", "conditional error return from RemoveDevice", errors)

    stop_body = function_body(active, "static NTSTATUS Bc250DxgkDdiStopDeviceActiveReason")
    if not stop_body:
        errors.append("Bc250DxgkDdiStopDeviceActiveReason nao localizado")
    else:
        require(stop_body, "BC250_ACTIVE_STOP_REASON_REMOVE_DEVICE", "check for REMOVE_DEVICE reason", errors)
        require(stop_body, "Bc250WddmFenceTrackerReset(&context->SubmissionEngine.WddmFences);",
                "tracker reset on remove in stop reason handler", errors)

    cleanup_body = function_body(active, "VOID Bc250ActiveDriverUnloadCleanup")
    if not cleanup_body:
        errors.append("Bc250ActiveDriverUnloadCleanup nao localizado")
    else:
        require(cleanup_body, "Bc250ActiveRetirementTimerStop(active);", "timer stop across all adapters on unload", errors)
        require(cleanup_body, "Bc250DisplayTeardown();", "display teardown on unload", errors)
        require(cleanup_body, "KeFlushQueuedDpcs();", "DPC flush on unload", errors)

    unload_body = function_body(callbacks, "VOID Bc250McdmUnload")
    if not unload_body:
        errors.append("Bc250McdmUnload nao localizado")
    else:
        require(unload_body, "g_Bc250UnloadCleanupHandler();", "unload cleanup handler invocation", errors)
        require(unload_body, "KeFlushQueuedDpcs();", "DPC flush before driver unmapping", errors)

    require(active, "Bc250McdmInstallUnloadCleanupHandler(Bc250ActiveDriverUnloadCleanup);",
            "unload cleanup handler installation in active miniport initialize", errors)


def audit_mec_shared_mmio_lifetime(errors: list[str]) -> None:
    engine = (ROOT / "driver/wdk/bc250_submission_engine.c").read_text(encoding="utf-8")
    active = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")
    core = (ROOT / "driver/wdk/bc250_dxgk_readonly_ddi.c").read_text(encoding="utf-8")
    # The engine borrows the adapter mapping; MMU restore outlives it.
    forbid(engine, "Bc250MmioTopologyUnmap(", "engine unmapping shared adapter MMIO", errors)
    stop = function_body(core, "NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceReadonly(")
    require(stop, "Bc250MmioTopologyUnmap(&context->MmioTransport)", "core MMIO release", errors)
    stop = function_body(active, "static NTSTATUS Bc250DxgkDdiStopDeviceActiveReason(")
    for step in ("Bc250SubmissionEngineOffline(context)",
                 "Bc250GcvmComputeRestore(&computeIo,",
                 "Bc250GcvmGlobalMmuRestore(",
                 "Bc250DxgkDdiStopDeviceReadonly(&context->Core)"):
        require(stop, step, "ordered teardown stage", errors)
    if not (stop.find("Bc250SubmissionEngineOffline(context)") <
            stop.find("Bc250GcvmComputeRestore(&computeIo,") <
            stop.find("Bc250GcvmGlobalMmuRestore(") <
            stop.find("Bc250DxgkDdiStopDeviceReadonly(&context->Core)")):
        errors.append("stop must quiesce MEC, restore MMU, then release core MMIO")
    wrapper = function_body(engine, "static NTSTATUS Bc250SubmissionApplyCoherency(")
    require(wrapper, "KeAcquireSpinLock(&Context->TlbLock, &irql);", "MEC shared TLB acquire", errors)
    require(wrapper, "KeReleaseSpinLock(&Context->TlbLock, irql);", "MEC shared TLB release", errors)
    call = "status = Bc250SubmissionApplyCoherencyLocked(Context, Engine);"
    require(wrapper, call, "MEC coherency under shared lock", errors)
    if not (wrapper.find("KeAcquireSpinLock") < wrapper.find(call) <
            wrapper.find("KeReleaseSpinLock") < wrapper.find("return status;")):
        errors.append("MEC coherency must release TLB lock on every transaction result")
    if engine.count("Bc250SubmissionApplyCoherencyLocked(") != 2:
        errors.append("unlocked coherency must have only its definition and locked caller")
    require(engine, "[MEC_IB_DESCRIPTOR]", "physical ring descriptor observation", errors)
    require(engine, "[MEC_PENDING]", "bounded pending MEC observation", errors)
    require(stop, "[MEC_PENDING_ARCHIVE]", "passive archive of DPC observations", errors)
    if not (stop.find("Bc250ActiveRetirementTimerStop(context)") <
            stop.find("[MEC_PENDING_ARCHIVE]") <
            stop.find("Bc250SubmissionEngineOffline(context)")):
        errors.append("archive MEC observation after DPC drain and before resource release")
    require(active, 'Bc250ActiveWalkGpuVa("NODE1_IB",', "Node1 process page-table observation", errors)
    archive_call = "Bc250SubmissionEngineArchiveDiagnostics(context);"
    require(stop, archive_call, "independent StopDevice MEC snapshot", errors)
    if not (stop.find("Bc250ActiveRetirementTimerStop(context)") <
            stop.find(archive_call) < stop.find("Bc250SubmissionEngineOffline(context)")):
        errors.append("independent MEC snapshot must precede engine resource release")
    poll = function_body(engine, "NTSTATUS Bc250SubmissionEnginePoll(")
    forbid(poll, "KeQueryInterruptTime", "time-gated first MEC observation", errors)
    if not (0 <= poll.find("engine->DiagnosticPendingFence = pendingFence;") <
            poll.find("Bc250PersistentRingRetire(")):
        errors.append("capture pending MEC evidence before retirement can fault engine")
    require(stop, "[NODE1_IB_CPU_ARCHIVE]", "CPU IB observation archive", errors)


def audit_tlb_invalidation_safety(errors: list[str]) -> None:
    active_h = (ROOT / "driver/wdk/bc250_active_miniport.h").read_text(encoding="utf-8")
    active_c = (ROOT / "driver/wdk/bc250_active_miniport.c").read_text(encoding="utf-8")

    require(active_h, "KSPIN_LOCK TlbLock;",
            "TlbLock member in BC250_DXGK_ACTIVE_CONTEXT", errors)

    add_body = function_body(active_c, "NTSTATUS APIENTRY Bc250DxgkDdiAddDeviceActive")
    if not add_body:
        errors.append("Bc250DxgkDdiAddDeviceActive nao localizado")
    else:
        require(add_body, "KeInitializeSpinLock(&context->TlbLock);",
                "TlbLock initialization in AddDeviceActive", errors)

    flush_body = function_body(active_c, "static uint32_t Bc250ActiveFlushVmidTlb")
    if not flush_body:
        errors.append("Bc250ActiveFlushVmidTlb nao localizado")
    else:
        require(flush_body, "KeAcquireSpinLock(&active->TlbLock,",
                "spin lock acquire in Bc250ActiveFlushVmidTlb", errors)
        require(flush_body, "KeReleaseSpinLock(&active->TlbLock,",
                "spin lock release in Bc250ActiveFlushVmidTlb", errors)

    vmid8_body = function_body(active_c, "static uint32_t Bc250ActiveFlushVmid8Only")
    if not vmid8_body:
        errors.append("Bc250ActiveFlushVmid8Only nao localizado")
    else:
        require(vmid8_body, "Bc250ActiveFlushVmidTlb(Context, BC250_GCVM_VMID8)",
                "strict VMID8 flush in Bc250ActiveFlushVmid8Only", errors)

    submit_body = function_body(active_c, "static NTSTATUS Bc250ActiveSubmitCommandVirtualBody")
    if not submit_body:
        errors.append("Bc250ActiveSubmitCommandVirtualBody nao localizado")
    else:
        require(submit_body, "io.Flush = Bc250ActiveFlushVmid8Only;",
                "isolated VMID8 flush in SubmitCommandVirtualBody context switch", errors)


def audit_probe_field_names(errors: list[str]) -> None:
    """Todo membro do enum de telemetria precisa de nome, e nomes unicos.

    Motivo (2026-09-02): o switch de Bc250ProbeFieldName tem um ramo
    `default: return NULL`, entao o MSVC nao emite C4061/C4062 e o build
    /W4 /WX passa limpo mesmo com campos sem case. Quatro campos gravados em
    todo bind por bc250_dxgk_readonly_ddi.c estavam nessa situacao: o valor
    nunca aparecia no registro, e a chave aberta em Bc250ProbeRecord vazava um
    handle de kernel por chamada.

    Nome duplicado e' o mesmo defeito pelo avesso: dois campos escreveriam no
    mesmo valor do registro e um apagaria a evidencia do outro em silencio.

    Este audit fecha as duas portas por construcao.
    """
    header = (ROOT / "driver/wdk/bc250_probe_telemetry.h").read_text(
        encoding="utf-8", errors="replace")
    source = (ROOT / "driver/wdk/bc250_probe_telemetry.c").read_text(
        encoding="utf-8", errors="replace")

    match = re.search(
        r"typedef enum[^{]*\{(.*?)\}\s*BC250_PROBE_FIELD", header, re.S)
    if match is None:
        errors.append("enum BC250_PROBE_FIELD nao localizado no header")
        return
    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.S)
    members = [
        chunk.strip().split("=")[0].strip() for chunk in body.split(",")
    ]
    members = [
        name for name in members
        if re.match(r"^BC250_PROBE_[A-Z0-9_]+$", name)
        and name != "BC250_PROBE_FIELD_COUNT"
    ]
    if not members:
        errors.append("enum BC250_PROBE_FIELD veio vazio do parser")
        return

    cases = set(re.findall(r"case\s+(BC250_PROBE_[A-Z0-9_]+)\s*:", source))
    missing = [name for name in members if name not in cases]
    if missing:
        shown = ", ".join(missing[:8])
        suffix = f" (e mais {len(missing) - 8})" if len(missing) > 8 else ""
        errors.append(
            f"campos de telemetria sem nome mapeado, vazam handle: {shown}{suffix}")

    names = re.findall(r'return\s+L"([A-Za-z0-9_]+)"\s*;', source)
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        errors.append(
            "nomes de telemetria duplicados: " + ", ".join(duplicates[:8]))


def function_body(text: str, signature: str) -> str:
    """Return a C function body, including braces, using balanced braces.

    Skips forward declarations. Sem isso, uma funcao que possui prototipo
    antes da definicao devolvia o corpo da funcao SEGUINTE, e todas as
    exigencias sobre ela passavam ou falhavam pelo motivo errado — falso
    negativo silencioso em um gate fail-closed.
    """
    search = 0
    while True:
        start = text.find(signature, search)
        if start < 0:
            return ""
        search = start + len(signature)
        # Fecha a lista de argumentos a partir do parenteses da assinatura.
        depth = 0
        index = text.find("(", start)
        if index < 0:
            continue
        end_args = -1
        for index in range(index, len(text)):
            if text[index] == "(":
                depth += 1
            elif text[index] == ")":
                depth -= 1
                if depth == 0:
                    end_args = index
                    break
        if end_args < 0:
            continue
        rest = text[end_args + 1:]
        stripped = rest.lstrip()
        if not stripped.startswith("{"):
            continue  # prototipo: procura a proxima ocorrencia
        brace = end_args + 1 + (len(rest) - len(stripped))
        depth = 0
        for index in range(brace, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    return text[brace:index + 1]
        return ""


def audit_gpuva_geometry_single_source(errors: list[str]) -> None:
    """A1 (07/09): a geometria de GPUVA tem UMA fonte, e nao e' um literal.

    MEDIDO na corrida 2026-09-07 11:08: o KMD declara ao WDDM
        [McdmQAI] GPUMMUCAPS: VaBits=34, Levels=2
    enquanto o blob DXGKQAITYPE_UMDRIVERPRIVATE publicava
    BC250_ADAPTER_GPUVA_BITS (= 48) e PageTableLevels = 2 cravado. Duas respostas
    diferentes para a mesma pergunta, saindo do mesmo driver.

    Nao e' cosmetico: a UMD -- e, na rota B, o winsys do RADV -- dispoe enderecos
    dentro do que o blob promete. Prometer 48 bits com uma hierarquia que traduz
    34 (12 offset + 9 folha + 13 raiz, docs/03) produz endereco sem PTE, que e'
    exatamente o sintoma que ja perseguimos no B1.

    Este gate exige que o publicador do blob tire GpuVaBits e PageTableLevels de
    `runtimeCaps` -- a MESMA estrutura que alimenta o GPUMMUCAPS -- e nunca de um
    literal ou de um #define proprio.
    """
    callbacks_path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    if not callbacks_path.exists():
        errors.append("bc250_mcdm_callbacks.c nao encontrado para auditar GPUVA")
        return
    text = callbacks_path.read_text(encoding="utf-8", errors="replace")

    required = (
        "facts.GpuVaBits = runtimeCaps.GpuVaBitCount;",
        "facts.PageTableLevels = runtimeCaps.PageTableLevelCount;",
    )
    for needle in required:
        if needle not in text:
            errors.append(
                "blob UMDRIVERPRIVATE nao deriva a geometria do runtime: "
                f"falta `{needle}`")

    # E o publicador do GPUMMUCAPS tem de usar a mesma origem.
    for needle in (
        "caps->VirtualAddressBitCount = runtimeCaps.GpuVaBitCount;",
        "caps->PageTableLevelCount = runtimeCaps.PageTableLevelCount;",
    ):
        if needle not in text:
            errors.append(
                "GPUMMUCAPS nao deriva a geometria do runtime: "
                f"falta `{needle}`")

    # Nenhum literal de geometria pode reaparecer atribuido a facts.
    for line in text.splitlines():
        stripped = line.strip()
        # So atribuicoes. A continuacao de um Bc250DisplayLog tambem comeca com
        # `facts.GpuVaBits,` e nao e' uma fonte de valor.
        if not stripped.startswith("facts.GpuVaBits =") and            not stripped.startswith("facts.PageTableLevels ="):
            continue
        if "runtimeCaps." not in stripped:
            errors.append(
                f"geometria de GPUVA cravada no blob: `{stripped}`")

    # A fonte unica do numero e' a decomposicao de 34 bits, nao um 48 solto.
    space_path = ROOT / "driver" / "core" / "bc250_gpuva_space_contract.h"
    if space_path.exists():
        space = space_path.read_text(encoding="utf-8", errors="replace")
        if "#define BC250_GPUVA_PT_VA_BITS (12u + BC250_GPUVA_PT_LEAF_INDEX_BITS"                 not in space:
            errors.append(
                "BC250_GPUVA_PT_VA_BITS deixou de ser derivado da decomposicao "
                "12 + folha + raiz")

    info_path = ROOT / "driver" / "core" / "bc250_adapter_info_contract.h"
    if info_path.exists():
        info = info_path.read_text(encoding="utf-8", errors="replace")
        if "#define BC250_ADAPTER_GPUVA_BITS          BC250_GPUVA_PT_VA_BITS"                 not in info:
            errors.append(
                "BC250_ADAPTER_GPUVA_BITS voltou a ser literal: era 48 e o "
                "silicio traduz 34")


def audit_frame_pointer_flag(errors: list[str]) -> None:
    """A5: FPO precisa ficar desligado no build do SYS.

    referencias/windows-driver-docs/.../display/debugging-tips-for-wddm-drivers.md
    exige que drivers WDDM 1.2+ desliguem frame pointer omission. Aqui isso nao e'
    cosmetico: sem frame pointer a call stack do MEMORY.DMP fica inutil, e o dump e'
    a unica via de diagnostico enquanto nao existe segunda maquina (P3).
    """
    build_path = ROOT / "driver" / "wdk" / "build-contract.bat"
    if not build_path.exists():
        errors.append("build-contract.bat nao encontrado para auditar FPO")
        return
    text = build_path.read_text(encoding="utf-8", errors="replace")
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped.lower().startswith("set cflags="):
            continue
        if "/Oy-" not in stripped:
            errors.append(
                "CFLAGS do SYS sem /Oy-: FPO ligado quebra a call stack do dump")
        if "/Oy " in stripped or stripped.endswith("/Oy"):
            errors.append("CFLAGS do SYS habilita /Oy explicitamente")
        return
    errors.append("nenhuma definicao de CFLAGS localizada em build-contract.bat")


def audit_batch_line_endings(errors: list[str]) -> None:
    """A8: .bat/.cmd do caminho de build precisam estar em CRLF.

    MEDIDO em 06/09: build-contract.bat estava com terminadores LF, e o cmd.exe,
    que retoma a leitura do arquivo por deslocamento em bytes, passou a comer o
    primeiro caractere das linhas depois de `call vcvars64.bat`. O sintoma foi
    LNK1181 com o nome de arquivo '^.obj' e linhas do proprio .bat sendo
    executadas como comandos -- readonly_miniport_test nunca linkava, e a queda
    ficava escondida no meio de um log de centenas de linhas. Converter para
    CRLF fez a suite inteira voltar a EXIT=0.

    docs/evidence esta fora do escopo de proposito: e' corpus imutavel.
    """
    scope = ("driver", "dbg", "tools")
    for base in scope:
        base_dir = ROOT / base
        if not base_dir.is_dir():
            continue
        for path in base_dir.rglob("*"):
            if path.suffix.lower() not in (".bat", ".cmd"):
                continue
            if "output" in path.parts or "evidence" in path.parts:
                continue
            raw = path.read_bytes()
            lf = raw.count(b"\n")
            crlf = raw.count(b"\r\n")
            if lf and crlf != lf:
                errors.append(
                    f"{path.relative_to(ROOT)}: {lf - crlf} linha(s) sem CRLF "
                    "-- cmd.exe corrompe a leitura por deslocamento")



def audit_submit_path_labels(errors: list[str]) -> None:
    """A13: a frase "IB EXECUTADO" so' pode sair do ramo INDIRECT.

    MEDIDO na corrida 2026-09-07 16:19: o log tem 14 "IB EXECUTADO" e, no mesmo
    log, 2 "PLANO A DIRECT FILL" + 12 "PLANO A SYNC NOP" -- 14 = 2 + 12 e ZERO
    INDIRECT. A linha de sucesso era comum aos tres ramos, entao afirmava
    execucao de DMA buffer onde a GPU nao leu DMA buffer nenhum (Lei 4).

    Tres portoes:
      1. a selecao de ramo sai de Bc250SelectSubmitExecPath (fonte unica);
      2. a frase e' guardada por Bc250SubmitPathExecutesDmaBuffer;
      3. os PLANO A tem rotulo proprio que nega execucao de IB.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append(
            "bc250_active_miniport.c nao encontrado para auditar rotulo de submit")
        return
    text = active_path.read_text(encoding="utf-8", errors="replace")

    require(text, "Bc250SelectSubmitExecPath(&pathInput)",
            "selecao de ramo de submit por fonte unica", errors)
    require(text, "Bc250SubmitPathExecutesDmaBuffer(execPath)",
            "guarda da frase IB EXECUTADO", errors)
    require(text, "PLANO A CONCLUIDO",
            "rotulo proprio dos caminhos PLANO A", errors)
    require(text, "NAO foi lido pela GPU",
            "negacao explicita de execucao de IB no PLANO A", errors)

    # A frase de execucao de IB nao pode aparecer fora da guarda.
    for index, line in enumerate(text.splitlines(), start=1):
        if "IB EXECUTADO" not in line:
            continue
        if "IB NAO EXECUTADO" in line:
            continue
        if "INDIRECT" not in line:
            errors.append(
                f"bc250_active_miniport.c:{index}: 'IB EXECUTADO' sem qualificar "
                "INDIRECT -- foi assim que 14 PLANO A viraram 14 IBs no log")


def audit_tripod_leg_one(errors: list[str]) -> None:
    """A13: a perna 1 do tripe (RPTR == WPTR) tem de estar NA DECISAO.

    MEDIDO na mesma corrida: "TRIPÉ NO SILÍCIO COMPROVADO (Item 63)! WPTR=68
    RPTR=52". O gate testava apenas fence e divergencia de bytes; a igualdade
    dos ponteiros nunca era avaliada, e um timeout de 2000 polls tambem passava.
    """
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    contract_path = ROOT / "driver" / "core" / "bc250_blt_proof_contract.c"
    if not present_path.exists() or not contract_path.exists():
        errors.append(
            "fontes do tripe de SDMA nao encontradas para auditar a perna 1")
        return

    present = present_path.read_text(encoding="utf-8", errors="replace")
    contract = contract_path.read_text(encoding="utf-8", errors="replace")

    require(present, "Bc250ClassifySdmaTripod(",
            "veredito do tripe por contrato testado", errors)
    require(present, "Bc250TripodAllowsClaim(tripodVerdict)",
            "guarda da frase TRIPE no bringup de SDMA", errors)
    require(present, "TRIPE_VEREDITO",
            "telemetria do veredito das tres pernas", errors)
    forbid(present,
           "if (g_Bc250SdmaPresent.TripodFenceObserved == (BC250_SDMA_PRESENT_FENCE_INIT + 1u) &&",
           "gate de tripe sem a perna RPTR==WPTR", errors)

    require(contract, "BC250_TRIPOD_RPTR_NOT_RETIRED",
            "veredito nomeado para RPTR que nao alcancou WPTR", errors)
    require(contract, "if (TargetWptrBytes == 0u)",
            "recusa de tripe por vacuidade em anel nunca armado", errors)


def audit_indirect_only_tunable(errors: list[str]) -> None:
    """B3: o default e' somente INDIRECT; PLANO A exige pedido explicito.

    MOTIVO: os dois PLANO A absorveram 14/14 submissoes em 16:19 e escondiam o
    INDIRECT. Depois de 17:43 e 18:16 -- em que o INDIRECT executou o DMA buffer
    real do dxgkrnl com o tripe completo -- a polaridade foi invertida: o
    default e' INDIRECT e SdmaPlanoAFallback=1 restaura a precedencia antiga
    apenas para bisseccao. Valor ausente ou ilegivel NAO liga o fallback.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append(
            "bc250_active_miniport.c nao encontrado para auditar SdmaIndirectOnly")
        return
    text = active_path.read_text(encoding="utf-8", errors="replace")

    require(text, 'L"SdmaPlanoAFallback", 0u',
            "PLANO A com default DESLIGADO", errors)
    require(text, "context->SdmaPlanoAFallbackPresent == 1u &&",
            "PLANO A exige valor presente e legivel", errors)
    require(text, "pathInput.PlanoAFallbackEnabled =",
            "tunable de PLANO A ligado a selecao de ramo", errors)
    # A polaridade antiga nao pode voltar por copia e cola.
    forbid(text, "SdmaIndirectOnly",
           "tunable de polaridade invertida (pre-B3)", errors)



def audit_ib_data_leg(errors: list[str]) -> None:
    """A14: o caminho INDIRECT nao pode dizer TRIPE com duas pernas.

    MEDIDO na corrida 2026-09-07 17:13: dez linhas "IB_TRIPOD PASS #N" saidas de
    um gate que avalia apenas RPTR==WPTR e fence sobre poison. Nenhum byte do
    destino era conferido -- mesma classe de falso sucesso da A13, em lugar que
    a A13 nao tocou.
    """
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    contract_path = ROOT / "driver" / "core" / "bc250_blt_proof_contract.c"
    if not present_path.exists() or not contract_path.exists():
        errors.append("fontes do IB indireto nao encontradas para auditar a perna 3")
        return

    present = present_path.read_text(encoding="utf-8", errors="replace")
    contract = contract_path.read_text(encoding="utf-8", errors="replace")

    forbid(present, "IB_TRIPOD PASS",
           "rotulo de tripe com duas pernas no caminho INDIRECT", errors)
    require(present, "Bc250CountDataProofMismatches(&dataPlan",
            "perna de dados conferida no caminho INDIRECT", errors)
    require(present, "IB_ENGINE_2LEGS",
            "rotulo honesto quando a perna de dados nao pode ser conferida", errors)
    require(present, "Bc250PlanDataProofCanaries(ExpectedEffectDestMc",
            "canaries do IB pelo contrato de fonte unica", errors)
    require(contract, "return BC250_DATA_PROOF_CANARY_COUNT;",
            "ausencia de leitura contada como divergencia total", errors)


def audit_process_gpuva_teardown(errors: list[str]) -> None:
    """A14: destruir processo tem de liberar as faixas de GPUVA dele.

    MEDIDO na corrida 17:13: apos o primeiro dwm.exe morrer, o dwm.exe seguinte
    recebeu status=10 (BC250_GPUVA_OWNER_MISMATCH) ao resolver 0x30000 -- faixa
    viva pertencente a um processo que nao existia mais.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append("bc250_active_miniport.c nao encontrado para auditar teardown de GPUVA")
        return
    text = active_path.read_text(encoding="utf-8", errors="replace")
    body = function_body(text, "NTSTATUS Bc250ActiveDestroyProcessObject(")
    if not body:
        errors.append("Bc250ActiveDestroyProcessObject nao localizado")
        return
    if "Bc250GpuvaSpaceInvalidateForObject(&Context->GpuvaSpace" not in body:
        errors.append(
            "DestroyProcess sem invalidacao das faixas de GPUVA do processo "
            "-- e assim que OWNER_MISMATCH sobrevive ao dono morto")



def audit_ring_wrap_arithmetic(errors: list[str]) -> None:
    """A15: a perna 1 tem de comparar RPTR com a volta do anel reduzida.

    MEDIDO na corrida 17:43 (FenceId=24): TargetWPTR=516 e RPTR=4612 num anel de
    4096 bytes -- 4612 = 4096 + 516. A fence havia batido; a comparacao crua
    reprovou, o timeout devolveu STATUS_IO_DEVICE_ERROR e o VidMm derrubou a
    maquina com BugCheck 0x119 (Arg1=2, Arg2=0xC0000185). A aritmetica ja existia
    no arquivo (Bc250SdmaRptrMatches) e o caminho INDIRECT nao a usava.
    """
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    if not present_path.exists():
        errors.append("bc250_sdma_present.c nao encontrado para auditar volta do anel")
        return
    text = present_path.read_text(encoding="utf-8", errors="replace")

    forbid(text, "if (rptrReg == targetWptr) {",
           "comparacao crua de RPTR sem a volta do anel", errors)
    require(text, "Bc250SdmaRptrMatches(rptrReg, rptrWbRaw, targetWptr)",
            "perna 1 do IB pela aritmetica de anel", errors)
    require(text, "Bc250TripodWrapRingPointer(",
            "reducao a uma volta antes de classificar e logar", errors)



def audit_node1_gate_telemetry(errors: list[str]) -> None:
    """B4: a recusa de submissao no Node 1 nao pode ser muda.

    MEDIDO nas corridas de 06 e 07/09: o KMD criou dois contextos Node 1 e
    recebeu ZERO submissoes, sem uma linha de log -- nao havia como distinguir
    "o scheduler nao submeteu" de "nos recusamos". A recusa vinha de
    GpuvaMappingsOperational == 0, e esse gate SO' abre quando GpuvaLabPromotion
    esta presente e em 1; essa leitura existia apenas no #else de
    BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP, isto e', fora do pacote ativo.

    Tres portoes: o tunable e lido no caminho de producao, a recusa e' logada
    com o BlockMask, e a abertura do gate tambem aparece no log.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append("bc250_active_miniport.c nao encontrado para auditar o gate do Node 1")
        return
    text = active_path.read_text(encoding="utf-8", errors="replace")

    require(text, '[StartDevice] GpuvaLabPromotion=%u (present=%u)',
            "leitura do gate de Node 1 no caminho de producao", errors)
    require(text, "RECUSA: GpuvaMappingsOperational=0",
            "recusa de Node 1 com motivo nomeado", errors)
    require(text, "BlockMask=0x%08X",
            "BlockMask no log da recusa de Node 1", errors)
    require(text, "GATE ABERTO: GpuvaMappingsOperational=1",
            "abertura do gate de Node 1 registrada", errors)

    # A recusa muda nao pode voltar.
    mute = ("&context->GpuvaMappingsOperational, 1, 1) != 1) {" + chr(10) +
            "        return STATUS_NOT_SUPPORTED;")
    if mute in text:
        errors.append(
            "recusa de Node 1 voltou a ser muda (STATUS_NOT_SUPPORTED sem log)")



def audit_tdr_pair_has_backend(errors: list[str]) -> None:
    """B6 / item 25: o par de TDR nao pode devolver sucesso sem silicio.

    ANTES: Bc250McdmResetFromTimeout e Bc250McdmRestartFromTimeout escreviam
    uma linha de log e devolviam STATUS_SUCCESS sem tocar em registrador --
    violacao direta da Lei 4, e no par de DDIs em que o dxgkrnl confia para
    decidir se o adaptador foi quiescido e se voltou. Pior: mcdm_callbacks_test
    EXIGIA esse STATUS_SUCCESS, ou seja o teste congelava a mentira.

    AGORA: as duas dependem de executor instalado; ponteiro nulo ou executor
    que devolve FALSE resulta em STATUS_DEVICE_NOT_READY. O restart ainda exige
    prova fisica (fence sobre poison) dentro do executor.
    """
    callbacks_path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    present_header_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.h"
    test_path = ROOT / "driver" / "tests" / "mcdm_callbacks_test.c"
    for path in (callbacks_path, present_path, present_header_path, test_path):
        if not path.exists():
            errors.append(f"{path.name} nao encontrado para auditar o par de TDR")
            return

    callbacks = callbacks_path.read_text(encoding="utf-8", errors="replace")
    present = present_path.read_text(encoding="utf-8", errors="replace")
    present_header = present_header_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    reset_body = function_body(callbacks,
        "NTSTATUS APIENTRY Bc250McdmResetFromTimeout(CONST HANDLE hAdapter)")
    restart_body = function_body(callbacks,
        "NTSTATUS APIENTRY Bc250McdmRestartFromTimeout(CONST HANDLE hAdapter)")
    if not reset_body or not restart_body:
        errors.append("corpo das DDIs de TDR nao localizado")
        return

    if "g_Bc250TdrQuiesce == NULL || !g_Bc250TdrQuiesce()" not in reset_body:
        errors.append(
            "ResetFromTimeout sem executor fisico -- STATUS_SUCCESS seria falso sucesso")
    if "g_Bc250TdrRestart == NULL || !g_Bc250TdrRestart()" not in restart_body:
        errors.append(
            "RestartFromTimeout sem executor fisico -- STATUS_SUCCESS seria falso sucesso")
    for body, name in ((reset_body, "ResetFromTimeout"),
                       (restart_body, "RestartFromTimeout")):
        if "STATUS_DEVICE_NOT_READY" not in body:
            errors.append(f"{name} sem caminho de recusa quando o silicio nao responde")

    # A corrida 20:41 mostrou que HALT + religar o anel preserva o comando
    # POLL_REGMEM preso. O restart precisa pulsar o reset integral, reconstruir
    # o anel, conferir readback e so' depois executar a prova fisica.
    restart_executor = function_body(present,
        "BOOLEAN Bc250SdmaPresentRestartAfterTdr(void)")
    if not restart_executor:
        errors.append("executor Bc250SdmaPresentRestartAfterTdr nao localizado")
        return
    require(present_header, "#define BC250_SDMA0_MMIO_WINDOW_BYTES     0x06000u",
            "janela MMIO cobre GRBM_SOFT_RESET em 0x08020", errors)
    require(restart_executor, "grbmBefore | BC250_GRBM_SOFT_RESET_SDMA0",
            "assert do reset integral da SDMA0", errors)
    require(restart_executor, "grbmBefore & ~BC250_GRBM_SOFT_RESET_SDMA0",
            "deassert do reset integral da SDMA0", errors)
    require(restart_executor, "grbmAsserted",
            "readback do reset assertado", errors)
    require(restart_executor, "BC250_SDMA0_REG_RB_RPTR_HI",
            "reconstrucao do RPTR alto pos-reset", errors)
    require(restart_executor, "Bc250TdrValidateSdmaRestartReadback(",
            "gate puro de readback pos-reset", errors)
    require(restart_executor,
            "TDR_FULL_RESET E REPROGRAMACAO: READBACK PASS",
            "telemetria do readback antes da prova", errors)

    readback_at = restart_executor.find(
        "readbackStatus = Bc250TdrValidateSdmaRestartReadback(")
    proof_at = restart_executor.find("proven = Bc250SdmaPresentSyncNop();")
    if readback_at < 0 or proof_at < 0 or readback_at >= proof_at:
        errors.append("prova fisica do restart aparece antes do gate de readback integral")
    success_log_at = restart_executor.find(
        "TDR_FULL_RESET E REPROGRAMACAO: READBACK PASS")
    if success_log_at < proof_at:
        errors.append("telemetria persistente de readback ainda e emitida sob o spinlock")

    # O executor de restart tem de PROVAR, nao apenas religar registradores.
    require(present, "proven = Bc250SdmaPresentSyncNop();",
            "prova fisica no reinicio pos-TDR", errors)
    require(present, "TDR_RESTART FALHOU",
            "recusa nomeada quando a engine nao volta", errors)
    require(present, "TDR_QUIESCE",
            "telemetria do quiesce fisico", errors)

    # E o teste nao pode voltar a exigir a mentira.
    if "CHECK(Bc250McdmResetFromTimeout((HANDLE)0x250) == STATUS_SUCCESS);" in test and        "Bc250McdmInstallTdrExecutors(TestTdrQuiesceOk, TestTdrRestartOk);" not in test:
        errors.append(
            "mcdm_callbacks_test exige STATUS_SUCCESS de TDR sem executor instalado")



def audit_engine_status_measures(errors: list[str]) -> None:
    """item 24: QueryEngineStatus tem de MEDIR, nao supor.

    ANTES: a DDI punha EngineStatus.Responsive = 1 sem olhar um registrador, e
    mcdm_callbacks_test EXIGIA isso. O dxgkrnl chama essa DDI durante o TDR
    para saber se a engine esta viva; responder "viva" sempre faria o TDR
    concluir que nao havia o que resetar.

    AGORA: a resposta vem de duas passagens de RPTR/WPTR (Bc250TdrDetectHang)
    com os ponteiros reduzidos a uma volta do anel -- a armadilha medida em
    17:43, onde RPTR=4612 e alvo 516 num anel de 4096 bytes.
    """
    callbacks_path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    test_path = ROOT / "driver" / "tests" / "mcdm_callbacks_test.c"
    for path in (callbacks_path, present_path, test_path):
        if not path.exists():
            errors.append(f"{path.name} nao encontrado para auditar QueryEngineStatus")
            return

    callbacks = callbacks_path.read_text(encoding="utf-8", errors="replace")
    present = present_path.read_text(encoding="utf-8", errors="replace")
    test = test_path.read_text(encoding="utf-8", errors="replace")

    body = function_body(callbacks,
        "NTSTATUS APIENTRY Bc250McdmQueryEngineStatus(")
    if not body:
        errors.append("Bc250McdmQueryEngineStatus nao localizado")
        return
    if "g_Bc250EngineResponsiveProbe == NULL" not in body:
        errors.append(
            "QueryEngineStatus sem probe de medicao -- Responsive=1 seria suposicao")
    if "EngineStatus.Responsive = 1;" in body and        "g_Bc250EngineResponsiveProbe(&responsive)" not in body:
        errors.append("QueryEngineStatus afirma Responsive=1 sem consultar a probe")

    require(present, "Bc250TdrDetectHang(0u, rptr1, wptr1, rptr2, wptr2, &isHung)",
            "deteccao propria por duas passagens", errors)
    require(present, "ENGINE_PROBE: Responsive=%u",
            "telemetria da deteccao propria", errors)
    # A reducao de volta e' obrigatoria: sem ela, anel drenado vira "travado".
    if present.count("Bc250TripodWrapRingPointer(") < 6:
        errors.append(
            "probe de responsividade sem reducao de volta do anel nos quatro ponteiros")

    if "CHECK(g_engineStatus.EngineStatus.Responsive == 1);" in test and        "Bc250McdmInstallEngineResponsiveProbe(TestProbeAlive);" not in test:
        errors.append(
            "mcdm_callbacks_test exige Responsive==1 sem probe instalada")


def audit_induced_hang_is_fail_closed(errors: list[str]) -> None:
    """item 25: o hang induzido nao pode disparar por acidente nem repetir.

    Ele existe para exercitar o TDR: trava a SDMA0 de verdade e NAO notifica
    completion, para que o timeout de 2 s do dxgkrnl dispare. Exatamente por
    isso precisa de tunable presente E igual a 1, disparo unico por bind, e
    hang FISICO (POLL_REGMEM) -- nunca um delay de CPU fingindo travamento.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    for path in (active_path, present_path):
        if not path.exists():
            errors.append(f"{path.name} nao encontrado para auditar o hang induzido")
            return

    active = active_path.read_text(encoding="utf-8", errors="replace")
    present = present_path.read_text(encoding="utf-8", errors="replace")

    require(active, 'L"TdrInduceHang", 0u',
            "hang induzido com default desarmado", errors)
    require(active, "context->TdrInduceHangPresent == 1u && context->TdrInduceHang == 1u",
            "hang induzido exige valor presente e legivel", errors)
    require(active, "InterlockedCompareExchange(&context->TdrHangInduced, 1, 0) == 0",
            "disparo unico do hang induzido", errors)
    require(present, "Bc250SdmaEmitPollRegMem(packets, 16u, &cursor,",
            "hang induzido por pacote fisico da engine", errors)
    forbid(present, "TDR_HANG_INDUZIDO: KeStallExecutionProcessor",
           "hang simulado por delay de CPU", errors)


def audit_context_abi_before_ready(errors: list[str]) -> None:
    """ContextAbiReady=1 exige ContextAbi POPULADO antes.

    MEDIDO na corrida 08/09 14:06, com a instrumentacao nova:
      [McdmCreateContext] OK: Node=0, hContext=0x..., DmaBuf=0x0, SegSet=0x0

    Bc250ActiveCreateContextObject faz "*ContextInfo = context->ContextAbi",
    sobrescrevendo os defaults que Bc250McdmCreateContext acabara de escrever. E
    ContextAbi so' era populado dentro do bloco
    "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)" -- so' na imagem
    de PRODUCAO. Na de laboratorio, ContextAbiReady ia a 1 com a estrutura
    inteiramente zerada, e o dxgkrnl recebia contexto com DmaBufferSize=0.

    Mesma classe do GpuvaLabPromotion (B4): campo escrito em UM ramo do #if.
    """
    path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not path.exists():
        errors.append(
            "bc250_active_miniport.c nao encontrado para auditar a ABI de contexto")
        return
    text = path.read_text(encoding="utf-8", errors="replace")

    require(text, "static void Bc250ActiveInitializeContextAbi(",
            "fonte unica da ABI de contexto", errors)

    # Fora do helper, ninguem pode escrever nos campos direto: isso seria uma
    # segunda fonte de verdade, que e' exatamente o defeito medido.
    helper = function_body(
        text, "static void Bc250ActiveInitializeContextAbi(")
    if not helper:
        errors.append("Bc250ActiveInitializeContextAbi nao localizada")
        return
    outside = text.replace(helper, "", 1)
    if "ContextAbi.DmaBufferSize" in outside:
        errors.append(
            "ContextAbi.DmaBufferSize escrito fora da fonte unica "
            "(segunda fonte de verdade)")

    # Sem janela arbitraria: entre um "ContextAbiReady=1" e o anterior (ou o
    # inicio do arquivo) tem de existir uma chamada a fonte unica. Assim cada
    # um dos sitios e' coberto por uma populacao propria, independentemente de
    # quantas linhas de comentario existam entre as duas.
    lines = text.splitlines()
    previous = 0
    for index, line in enumerate(lines):
        if "ContextAbiReady, 1)" not in line:
            continue
        segment = "\n".join(lines[previous:index])
        if "Bc250ActiveInitializeContextAbi(" not in segment:
            errors.append(
                "ContextAbiReady=1 na linha %d sem popular ContextAbi antes "
                "(publica DmaBufferSize=0 ao dxgkrnl)" % (index + 1))
        previous = index


def audit_process_lifecycle_ddis_speak(errors: list[str]) -> None:
    """O ciclo de vida de processo nao pode recusar em silencio.

    MEDIDO na corrida 08/09 13:00 (perfil lab, Codigo 0, tela preta): a
    negociacao de VidPn CONVERGIU 39 vezes e foi descartada 46. Em cada ciclo o
    log mostrava CreateProcess -> CreateDevice -> CreateContext(Node 0) ->
    DestroyProcess -- sete deles com o MESMO hDxgkProcess -- e NAO havia como
    saber se alguma dessas DDIs havia RECUSADO: as tres tinham caminhos de falha
    sem uma linha de log. Mesma classe do B4, agora no caminho critico do
    processo que morre.

    Tres portoes: cada uma das tres DDIs nomeia suas recusas, o desfecho de
    sucesso tambem aparece (senao "ausencia de RECUSADO" nao prova nada), e o
    nome do processo vem do contrato -- DXGKARG_CREATEPROCESS carrega
    pProcessName desde WDDM 2.4 e declaramos 2.7.
    """
    path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    if not path.exists():
        errors.append(
            "bc250_mcdm_callbacks.c nao encontrado para auditar o ciclo de "
            "vida de processo")
        return
    text = path.read_text(encoding="utf-8", errors="replace")

    # Exigir a mera PRESENCA de "RECUSADO:" prova apenas que UMA das recusas
    # fala -- verificado por mutacao: removendo uma das tres de CreateContext o
    # gate passava. O invariante correto e': DEPOIS do log de entrada, todo
    # return que nao seja STATUS_SUCCESS tem de ter um Bc250DisplayLog perto.
    signatures = {
        "McdmCreateProcess": "NTSTATUS APIENTRY Bc250McdmCreateProcess(",
        "McdmCreateDevice": "NTSTATUS APIENTRY Bc250McdmCreateDevice(",
        "McdmCreateContext": "NTSTATUS APIENTRY Bc250McdmCreateContext(",
    }
    for ddi, signature in signatures.items():
        body = function_body(text, signature)
        if not body:
            errors.append("%s nao localizada para auditar recusas mudas" % ddi)
            continue
        lines = body.splitlines()
        entered = next(
            (i for i, l in enumerate(lines) if "] Entered:" in l), None)
        if entered is None:
            errors.append("%s nao registra a entrada" % ddi)
            continue
        for index in range(entered + 1, len(lines)):
            stripped = lines[index].strip()
            if not stripped.startswith("return "):
                continue
            if "STATUS_SUCCESS" in stripped:
                continue
            window = "\n".join(lines[max(entered, index - 6):index])
            if "Bc250DisplayLog(" not in window:
                errors.append(
                    "%s: recusa MUDA em '%s' (nenhum log nas 6 linhas "
                    "anteriores)" % (ddi, stripped))

    require(text, "[McdmCreateDevice] OK: hDevice=",
            "desfecho de sucesso em CreateDevice", errors)
    require(text, "[McdmCreateContext] OK: Node=",
            "desfecho de sucesso em CreateContext", errors)

    require(text, "pArgs->pProcessName",
            "nome do processo lido do contrato DXGKARG_CREATEPROCESS", errors)
    require(text, "Flags(System=%u Gdi=%u Vm=%u VmWorker=%u Isolated=%u)",
            "flags de criacao de processo decodificadas", errors)

    # A copia do nome nao pode voltar a virar memcpy: o link offline nao o tem.
    if "volatile WCHAR *nameDst" not in text:
        errors.append(
            "copia do nome de processo sem destino volatile: o /O2 a reduz a "
            "memcpy e o link offline das suites quebra (LNK2019)")


def audit_second_preflight_transport_empty(errors: list[str]) -> None:
    """O segundo preflight readonly exige transporte MMIO vazio.

    MEDIDO na corrida 08/09 11:48 (Codigo 43): o StartDevice de laboratorio
    passou a mapear Core.MmioTransport para programar a MMU global e seguiu com
    ele MAPEADO ate o segundo Bc250DxgkDdiStartDeviceReadonly. A guarda de
    entrada daquela DDI recusa transporte nao-vazio com STATUS_INVALID_PARAMETER
    -- e recusa ANTES de escrever LastStage, entao SecondPreflightStage ficou
    exibindo o 3 (RUNTIME_MISSING) do PRIMEIRO preflight. Resultado:
    SecondReadonlyGatePassed=0, bring-up fisico inteiro pulado, e o rodapé
    publicou o inicializador de persistentStatus (0xC00000BB) como veredito.
    O bind 09:50 do MESMO perfil, sem esse bloco, fechou em estagio 8/Codigo 0.

    Tres portoes:
      1. o transporte volta a vazio antes do segundo preflight;
      2. o StartDevice publica o NTSTATUS CRU da segunda chamada -- o estagio
         sozinho e' ambiguo por construcao;
      3. o rodapé so' pode trocar o status pelo do ensaio de submissao
         persistente se esse ensaio TIVER ACONTECIDO.
    """
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    if not active_path.exists():
        errors.append(
            "bc250_active_miniport.c nao encontrado para auditar a ordem do "
            "segundo preflight")
        return
    text = active_path.read_text(encoding="utf-8", errors="replace")

    body = function_body(text, "NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceActive(")
    if not body:
        errors.append(
            "Bc250DxgkDdiStartDeviceActive nao localizada para auditar a ordem "
            "do segundo preflight")
        return

    ensure = body.find("Bc250ActiveEnsureGlobalMmu(")
    if ensure < 0:
        errors.append(
            "sitio da MMU global nao localizado no StartDevice ativo")
        return
    # A busca do segundo preflight PRECISA comecar depois da MMU global: a mesma
    # atribuicao textual ocorre no PRIMEIRO preflight, no topo da funcao. Buscar
    # do inicio casava com ele, a janela saia invertida e o gate passava sem
    # examinar nada -- falso negativo silencioso, medido ao mutar o driver.
    second = body.find(
        "preflightStatus = Bc250DxgkDdiStartDeviceReadonly(", ensure)
    if second < 0:
        errors.append(
            "segundo preflight readonly nao localizado depois da MMU global "
            "no StartDevice ativo")
        return

    window = body[ensure:second]
    if "Bc250MmioTopologyUnmap(&context->Core.MmioTransport)" not in window:
        errors.append(
            "transporte MMIO permanece mapeado entre a MMU global e o segundo "
            "preflight (regressao da corrida 08/09 11:48, Codigo 43)")
    if "BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP" not in window:
        errors.append(
            "o retorno do transporte a vazio precisa ser exclusivo do bring-up "
            "de laboratorio: o caminho de producao mantem o mapa por design")

    require(text, "BC250_PROBE_SECOND_PREFLIGHT_STATUS",
            "NTSTATUS cru do segundo preflight publicado", errors)
    require(text, "[StartDevice] Segundo preflight: status=0x%08X",
            "log do segundo preflight com status e estado do transporte",
            errors)

    require(text, "persistentAttempted != FALSE",
            "rodapé so' troca o status por um ensaio que aconteceu", errors)
    masking = ("if (context->PersistentSubmission == 1u &&" + chr(10) +
               "        persistentReady == FALSE) {")
    if masking in text:
        errors.append(
            "rodapé voltou a publicar persistentStatus sem exigir "
            "persistentAttempted (mascara a falha real do bring-up)")

    # O teste que reproduz a corrida nao pode desaparecer.
    test_path = ROOT / "driver" / "tests" / "dxgk_readonly_ddi_test.c"
    if not test_path.exists():
        errors.append("dxgk_readonly_ddi_test.c nao encontrado")
        return
    test_text = test_path.read_text(encoding="utf-8", errors="replace")
    require(test_text, "CHECK(status == STATUS_INVALID_PARAMETER);",
            "teste da recusa por transporte mapeado", errors)
    require(test_text, "CHECK(g_context.RuntimeStaged == 1u);",
            "teste prova que o runtime estava staged na recusa", errors)


def audit_induced_hang_package_order(errors: list[str]) -> None:
    """item 25: o primeiro StartDevice precisa receber o flag pelo INF.

    A chave de servico ainda nao existe antes da instalacao. O pacote normal
    permanece em zero; o empacotador materializa 1 apenas no INF candidato,
    antes do catalogo, e exige versao/consentimento novos. O binder exige o
    mesmo consentimento quando o manifesto diz que o pacote esta armado.
    """
    paths = {
        "inf": ROOT / "driver" / "wdk" / "package" / "bc250mcdm.inf",
        "build": ROOT / "tools" / "build-bc250mcdm-package.ps1",
        "bind": ROOT / "tools" / "bind-bc250mcdm-failclosed.ps1",
        "arm": ROOT / "tools" / "arm-tdr-induce-hang.ps1",
    }
    texts: dict[str, str] = {}
    for label, path in paths.items():
        if not path.exists():
            errors.append(f"{path.name} ausente para auditar ordem do ensaio TDR")
            return
        texts[label] = path.read_text(encoding="utf-8", errors="replace")

    require(texts["inf"],
            "HKR,Parameters,TdrInduceHang,0x00010001,0",
            "INF fonte TDR fail-closed", errors)
    require(texts["build"], "[switch]$ArmTdrInducedHang",
            "modo explicito de pacote TDR", errors)
    require(texts["build"],
            "-IAcceptDeliberateHangAndPossibleBugCheck",
            "consentimento no empacotamento TDR", errors)
    require(texts["build"],
            "Pacote TDR armado exige -PackageDriverVersion",
            "versao nova obrigatoria no pacote TDR", errors)
    require(texts["build"],
            "-PackageDriverVersion precisa diferir",
            "reuso da versao fonte proibido no pacote TDR", errors)
    require(texts["build"],
            "HKR,Parameters,TdrInduceHang,0x00010001,1",
            "flag materializado no INF empacotado", errors)
    require(texts["build"], "MCDM_TDR_INDUCED_HANG",
            "identidade explicita do pacote TDR", errors)
    require(texts["bind"],
            "if (-not $IAcceptDeliberateHangAndPossibleBugCheck)",
            "consentimento no bind do pacote TDR", errors)
    require(texts["arm"], "so sera lido no PROXIMO StartDevice",
            "script explica latencia do tunable", errors)
    require(texts["arm"], "Nao crie a chave a mao",
            "script proibe chave manual", errors)


def main() -> int:
    active_path = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
    engine_path = ROOT / "driver" / "wdk" / "bc250_submission_engine.c"
    callbacks_path = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
    objects_path = (ROOT / "driver" / "core" /
                    "bc250_mcdm_object_contract.c")
    active_test_path = ROOT / "driver" / "tests" / "active_miniport_test.c"
    recovery_path = ROOT / "tools" / "BC250_RECOVERY.cmd"
    inf_path = ROOT / "driver" / "wdk" / "package" / "bc250mcdm.inf"
    stage_path = ROOT / "tools" / "stage-bc250mcdm-package.ps1"
    bind_path = ROOT / "tools" / "bind-bc250mcdm-failclosed.ps1"
    lab_bind_path = ROOT / "tools" / "bind-bc250mcdm-lab.ps1"
    lab_package_path = ROOT / "tools" / "package-bc250mcdm-lab.ps1"
    lab_watchdog_path = ROOT / "tools" / "watch-bc250mcdm-lab.ps1"
    lab_rollback_path = ROOT / "tools" / "rollback-bc250mcdm-lab.ps1"
    render_prepare_path = ROOT / "tools" / "prepare-bc250mcdm-rendernode.ps1"
    render_inspect_path = ROOT / "tools" / "inspect-bc250mcdm-rendernode.ps1"
    collector_path = ROOT / "tools" / "capture-bc250mcdm-binding.ps1"
    watchdog_path = ROOT / "tools" / "watch-bc250mcdm-binding.ps1"
    confirm_display_path = ROOT / "tools" / "confirm-bc250mcdm-display.ps1"
    arm_reboot_path = (ROOT / "tools" /
                       "arm-bc250mcdm-reboot-watchdog.ps1")
    watchdog_reboot_path = (ROOT / "tools" /
                            "watch-bc250mcdm-reboot.ps1")
    confirm_reboot_path = (ROOT / "tools" /
                           "confirm-bc250mcdm-reboot.ps1")
    rollback_path = ROOT / "tools" / "rollback-bc250mcdm-online.ps1"
    replace_path = ROOT / "tools" / "replace-bc250mcdm.ps1"
    orphan_cleanup_path = (ROOT / "tools" /
                           "cleanup-bc250mcdm-prebinding-residue.ps1")
    readonly_ddi_path = ROOT / "driver" / "wdk" / "bc250_dxgk_readonly_ddi.c"
    platform_snapshot_path = (ROOT / "driver" / "core" /
                              "bc250_platform_register_snapshot.c")
    vram_probe_path = (ROOT / "driver" / "wdk" /
                       "bc250_vram_probe_transaction.c")
    paging_path = ROOT / "driver" / "core" / "bc250_paging_contract.c"
    tlb_transaction_path = (ROOT / "driver" / "core" /
                            "bc250_gfxhub_tlb_transaction.c")
    mmio_transport_path = (ROOT / "driver" / "wdk" /
                           "bc250_mmio_topology_transport.c")
    compute_mqd_path = (ROOT / "driver" / "core" /
                        "bc250_compute_mqd_contract.c")
    pm4_builder_path = (ROOT / "driver" / "core" /
                        "bc250_pm4_packet_builder.c")
    pm4_header_path = (ROOT / "driver" / "core" /
                       "bc250_pm4_packet_builder.h")
    display_path = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"
    sdma_present_path = ROOT / "driver" / "wdk" / "bc250_sdma_present.c"
    umd_path = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
    umd_header_path = ROOT / "driver" / "umd" / "bc250_d3d_umd.h"
    winsys_path = ROOT / "driver" / "umd" / "bc250_wddm_winsys.c"
    winsys_test_path = ROOT / "driver" / "tests" / "bc250_wddm_winsys_test.c"
    build_path = ROOT / "driver" / "wdk" / "build-contract.bat"
    umd_build_path = ROOT / "driver" / "umd" / "build-umd.bat"
    arm_dwm_path = ROOT / "tools" / "arm-dwm-crash-capture.ps1"
    restore_dwm_path = ROOT / "tools" / "restore-dwm-crash-capture.ps1"
    radv_tripod_path = (ROOT / "tools" /
                        "test-radv-wddm-compute-tripod.ps1")
    umd_node1_tripod_path = (ROOT / "tools" /
                             "test-b7-umd-node1-tripod.ps1")
    radv_gate_path = (ROOT / "driver" / "tests" /
                      "bc250_radv_compute_gate.c")

    active = active_path.read_text(encoding="utf-8")
    engine = engine_path.read_text(encoding="utf-8")
    callbacks = callbacks_path.read_text(encoding="utf-8")
    objects = objects_path.read_text(encoding="utf-8")
    active_test = active_test_path.read_text(encoding="utf-8")
    recovery = recovery_path.read_text(encoding="utf-8")
    inf = inf_path.read_text(encoding="utf-8")
    stage = stage_path.read_text(encoding="utf-8")
    bind = bind_path.read_text(encoding="utf-8")
    lab_bind = lab_bind_path.read_text(encoding="utf-8")
    lab_package = lab_package_path.read_text(encoding="utf-8")
    lab_watchdog = lab_watchdog_path.read_text(encoding="utf-8")
    lab_rollback = lab_rollback_path.read_text(encoding="utf-8")
    render_prepare = render_prepare_path.read_text(encoding="utf-8")
    render_inspect = render_inspect_path.read_text(encoding="utf-8")
    collector = collector_path.read_text(encoding="utf-8")
    watchdog = watchdog_path.read_text(encoding="utf-8")
    confirm_display = confirm_display_path.read_text(encoding="utf-8")
    arm_reboot = arm_reboot_path.read_text(encoding="utf-8")
    watchdog_reboot = watchdog_reboot_path.read_text(encoding="utf-8")
    confirm_reboot = confirm_reboot_path.read_text(encoding="utf-8")
    rollback = rollback_path.read_text(encoding="utf-8")
    replace = replace_path.read_text(encoding="utf-8")
    orphan_cleanup = orphan_cleanup_path.read_text(encoding="utf-8")
    readonly_ddi = readonly_ddi_path.read_text(encoding="utf-8")
    platform_snapshot = platform_snapshot_path.read_text(encoding="utf-8")
    vram_probe = vram_probe_path.read_text(encoding="utf-8")
    paging = paging_path.read_text(encoding="utf-8")
    tlb_transaction = tlb_transaction_path.read_text(encoding="utf-8")
    mmio_transport = mmio_transport_path.read_text(encoding="utf-8")
    compute_mqd = compute_mqd_path.read_text(encoding="utf-8")
    pm4_builder = pm4_builder_path.read_text(encoding="utf-8")
    pm4_header = pm4_header_path.read_text(encoding="utf-8")
    display = display_path.read_text(encoding="utf-8")
    sdma_present = sdma_present_path.read_text(encoding="utf-8")
    umd = umd_path.read_text(encoding="utf-8")
    umd_header = umd_header_path.read_text(encoding="utf-8")
    winsys = winsys_path.read_text(encoding="utf-8")
    winsys_test = winsys_test_path.read_text(encoding="utf-8")
    build = build_path.read_text(encoding="utf-8")
    umd_build = umd_build_path.read_text(encoding="utf-8")
    arm_dwm = arm_dwm_path.read_text(encoding="utf-8")
    restore_dwm = restore_dwm_path.read_text(encoding="utf-8")
    radv_tripod = radv_tripod_path.read_text(encoding="utf-8")
    umd_node1_tripod = umd_node1_tripod_path.read_text(encoding="utf-8")
    radv_gate = radv_gate_path.read_text(encoding="utf-8")
    errors: list[str] = []

    # A MMU global usa exatamente 26 offsets catalogados. O bind de 15:49
    # falhou antes do MMIO porque 0xA610 nao estava na allowlist do transporte.
    # As duas direcoes precisam compartilhar a mesma lista fechada.
    global_mmu_allowlist = function_body(
        mmio_transport,
        "static uint32_t Bc250MmioTopologyGlobalMmuOffsetAllowed(")
    mmio_read_allowlist = function_body(
        mmio_transport,
        "static uint32_t Bc250MmioTopologyOffsetAllowed(")
    mmio_write_allowlist = function_body(
        mmio_transport,
        "static uint32_t Bc250MmioTopologyWriteOffsetAllowed(")
    global_mmu_registers = (
        "BC250_REG_GCMC_VM_AGP_BASE",
        "BC250_REG_GCMC_VM_AGP_BOT",
        "BC250_REG_GCMC_VM_AGP_TOP",
        "BC250_REG_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR",
        "BC250_REG_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR",
        "BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB",
        "BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB",
        "BC250_REG_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_LO",
        "BC250_REG_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_HI",
        "BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL2",
        "BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO",
        "BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI",
        "BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO",
        "BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI",
        "BC250_REG_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO",
        "BC250_REG_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI",
        "BC250_REG_GCVM_INVALIDATE_ENG0_ADDR_RANGE_LO",
        "BC250_REG_GCVM_INVALIDATE_ENG0_ADDR_RANGE_HI",
        "BC250_REG_GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO",
        "BC250_REG_GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI",
        "BC250_REG_GCMC_VM_MX_L1_TLB_CNTL",
        "BC250_REG_GCVM_L2_CNTL",
        "BC250_REG_GCVM_L2_CNTL2",
        "BC250_REG_GCVM_L2_CNTL3",
        "BC250_REG_GCVM_L2_CNTL4",
        "BC250_REG_GCVM_L2_CNTL5",
    )
    for register in global_mmu_registers:
        require(global_mmu_allowlist, "case " + register + ":",
                "global MMU MMIO allowlist: " + register, errors)
    require(mmio_read_allowlist,
            "Bc250MmioTopologyGlobalMmuOffsetAllowed(ByteOffset)",
            "global MMU read allowlist shared helper", errors)
    require(mmio_write_allowlist,
            "Bc250MmioTopologyGlobalMmuOffsetAllowed(ByteOffset)",
            "global MMU write allowlist shared helper", errors)

    # GPUVA/Present deve permanecer transacional: UpdatePT nao publica um
    # cache paralelo, erro do journal nao vira sucesso e o blt so conclui com
    # fence nova (poison substituido) mais RPTR alcancando WPTR.
    update_pt = function_body(
        active, "static NTSTATUS Bc250ActiveUpdatePageTableHandler(")
    flush_tlb = function_body(
        active, "NTSTATUS Bc250ActiveFlushTlbHandler(")
    resolve_present = function_body(
        display, "BOOLEAN Bc250DisplayResolvePresentSource(")
    sdma_blt = function_body(
        sdma_present, "BOOLEAN Bc250SdmaPresentBlt(")
    require(update_pt, "return STATUS_DEVICE_BUSY;",
            "journal BUSY propagated by UpdatePageTable", errors)
    require(update_pt, "return STATUS_INVALID_PARAMETER;",
            "journal invalid state propagated by UpdatePageTable", errors)
    forbid(update_pt, "Bc250DisplayRecordGpuVaMapping(",
           "GPUVA publication before journal commit/FlushTlb", errors)
    require(flush_tlb, "return STATUS_IO_DEVICE_ERROR;",
            "journal commit failure propagated by FlushTlb", errors)
    require(flush_tlb, "flushRootKey != 0ull",
            "explicit valid root key gate before journal commit", errors)
    require(flush_tlb, "0u, 0u,\n        &generation",
            "global journal commit after global physical TLB invalidation", errors)
    forbid(resolve_present, "Bc250DisplayLookupPhysicalAddress(",
           "parallel uncommitted GPUVA cache in Present", errors)
    require(sdma_blt, "BC250_SDMA_PRESENT_FENCE_POISON;",
            "per-blit fence poison", errors)
    # A15: o gate era pela comparacao CRUA. Ela nao cobre a volta do anel --
    # 4612 == 516 (mod 4096) na corrida 17:43 -- e exigi-la literalmente
    # obrigava o defeito a continuar existindo. Agora o gate exige a aritmetica.
    require(sdma_blt, "Bc250SdmaRptrMatches(rptrReg, rptrWb, targetWptr)",
            "per-blit RPTR equals WPTR gate (com volta do anel)", errors)
    # A10: a palavra "TRIPE" so' pode sair quando as tres pernas estao provadas.
    # O gate antigo exigia "BLT_TRIPOD PASS" incondicional, e por isso protegia
    # uma linha que anunciava tres pernas tendo duas.
    require(sdma_blt, "BLT_TRIPOD PASS #%ld (3 pernas)",
            "tripod telemetry conditioned on the data leg", errors)
    require(sdma_blt, "BLT_ENGINE_2LEGS",
            "two-leg telemetry when the data leg is unproven", errors)
    require(sdma_blt, "BC250_BLT_DATA_PROOF_OK",
            "tripod claim gated by the data-proof state", errors)

    present_body = function_body(
        display, "NTSTATUS APIENTRY Bc250DxgkDdiPresent(")
    # A9: o veredito da prova vem do contrato em driver/core, nao de um if solto.
    require(present_body, "Bc250ClassifyBltProof(",
            "blt proof verdict from the core contract", errors)
    require(present_body, "Bc250BltProofAllowsTripodClaim(",
            "tripod claim gated by the proof contract", errors)
    require(present_body, "Bc250BltProofRequiresFailClosed(",
            "fail-closed decision from the proof contract", errors)
    require(present_body, "BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO)",
            "blank-frame proof reported as inconclusive, never PASS", errors)
    require(present_body, "BYTE_PROOF PASS (dados conferidos, frame com conteudo)",
            "byte-by-byte Present proof telemetry", errors)
    # O rotulo antigo certificava preto contra preto: 63/63 amostras com
    # NonZero=0/256 sairam como PASS na corrida 2026-09-06 22:42.
    forbid(present_body, 'BYTE_PROOF PASS: Source=',
           "unconditional BYTE_PROOF PASS label (certifies black-vs-black)",
           errors)
    require(present_body, "g_Bc250SdmaPresent.HardwareEnabled = FALSE;",
            "SDMA disable after byte proof mismatch", errors)
    forbid(present_body, "else if (fromPrimary)",
           "unresolved Present fallback to SharedPrimary", errors)

    # Display PnP must not activate laboratory hardware transactions.
    require(active,
            "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)",
            "compile-time display-only StartDevice gate", errors)
    require(active, "Bc250ProbeSnapshotDdiTrace();",
            "V5 DDI trace snapshot before teardown", errors)
    require(active,
            "InterlockedCompareExchange(&context->IsActive, 0, 0) != 0",
            "idempotent StopDevice/RemoveDevice lifecycle", errors)
    require(display, "Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_INTERFACE);",
            "QueryInterface V5 DDI trace", errors)
    require(active,
            "registry value must never be able to turn",
            "non-tunable display safety contract", errors)
    require(active,
            "Bc250ProbeRecord(BC250_PROBE_SKIP_HW_BRINGUP, 1u);",
            "compiled display-only bring-up skip evidence", errors)
    forbid(active, "#define BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP",
           "local activation of unsafe StartDevice bring-up", errors)
    forbid(build, "/DBC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP",
           "unsafe StartDevice bring-up in package build", errors)

    # A lab image must not advertise persistent submission without wiring the
    # complete physical lifecycle into the active miniport. Candidate 3.62
    # regressed here: the engine existed but StartDevice never called it.
    start_active = function_body(
        active, "NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceActive(")
    # Desde o 3.67 Bc250DxgkDdiStopDeviceActive e' um involucro de uma linha
    # sobre a implementacao parametrizada pelo motivo do teardown. Auditar o
    # involucro nao provaria nada.
    stop_active = function_body(
        active, "static NTSTATUS Bc250DxgkDdiStopDeviceActiveReason(")
    quiesce_active = function_body(
        active, "static NTSTATUS Bc250ActiveQuiesceAndSnapshotTracker(")
    submit_wrapper = function_body(
        active, "static NTSTATUS Bc250ActiveSubmitCommandVirtualLifecycle(")
    submit_gate = function_body(
        active, "BOOLEAN Bc250ActiveSubmissionGateEnter(")
    submit_body = function_body(
        active, "static NTSTATUS Bc250ActiveSubmitCommandVirtualBody(")
    one_shot_hqd = function_body(
        active, "static BC250_HQD_ACTIVE_STATUS "
                "Bc250ActiveRunActiveHqdSubmissionTransaction(")
    for needle, label in (
        ('L"PersistentSubmission"', "persistent-submission tunable read"),
        ("Bc250SubmissionEngineOnline(context);", "persistent engine online"),
        ("Bc250SubmissionEngineSelfTest(context);", "P1 physical self-test"),
        ("Bc250SubmissionEngineP2StreamTest(context);", "P2 stream test"),
        ("Bc250SubmissionEngineRecordTelemetry(context);",
         "P1/P2 telemetry publication"),
        ("context->SubmissionEngine.SelfTest.PhysicalReady != 0u",
         "P1 physical-ready success gate"),
        ("context->SubmissionEngine.P2Stream.PhysicalReady != 0u",
         "P2 physical-ready success gate"),
        ("(context->PersistentSubmission == 0u ||",
         "strict persistent-submission policy gate"),
        ("(context->ShaderDispatch == 0u ||",
         "strict shader-dispatch policy gate"),
    ):
        require(start_active, needle, label, errors)
    require(stop_active, "Bc250SubmissionEngineOffline(context);",
            "persistent engine StopDevice teardown", errors)
    if (stop_active and
            stop_active.find("Bc250SubmissionEngineOffline(context);") >
            stop_active.find("Bc250DxgkDdiStopDeviceReadonly(&context->Core)")):
        errors.append("persistent engine teardown occurs after core teardown")

    # ------------------------------------------------------------------
    # Contrato de quiescencia do teardown (candidato 3.67).
    #
    # O 3.66 decidiu o teardown pelo retorno de DxgkCbSynchronizeExecution e
    # devolveu STATUS_DEVICE_BUSY com o rastreador comprovadamente vazio
    # (Pending=0, Reserved=0, SyncStatus=0xC0000001). O gate abaixo impede
    # tanto a volta daquele mecanismo unico quanto a "correcao" oposta —
    # ler os contadores sem lock e sem quiescencia.
    # ------------------------------------------------------------------
    require(stop_active, "Bc250ActiveQuiesceAndSnapshotTracker(",
            "tracker quiescence before StopDevice teardown", errors)
    require(stop_active, "pendingCount != 0u || reservedCount != 0u",
            "fail-closed StopDevice on non-empty tracker", errors)
    require(stop_active, "STATUS_DEVICE_BUSY",
            "StopDevice busy verdict preserved", errors)
    for needle, label in (
        ("BC250_ACTIVE_LIFECYCLE_STOPPING",
         "STOPPING state before tracker snapshot"),
        ("Bc250WddmFenceTrackerSetAcceptingWork(tracker, FALSE)",
         "admission gate closed before snapshot"),
        ("Bc250ActiveRetirementTimerStop(Context);",
         "retirement timer stopped before snapshot"),
        ("Bc250ActiveStopFlushDpcs();", "DPC drain before snapshot"),
        ("Bc250WddmFenceTrackerSnapshotCounts(",
         "locked tracker snapshot"),
        ("&Context->SubmitInFlight, 0, 0",
         "in-flight submission drain"),
    ):
        require(quiesce_active, needle, label, errors)
    # A fotografia so vale sob o lock proprio; leitura crua e' proibida em
    # ambos os corpos.
    for body, where in ((quiesce_active, "quiescence"), (stop_active, "StopDevice")):
        forbid(body, "WddmFences.Count",
               f"unsynchronized tracker Count read in {where}", errors)
        forbid(body, "WddmFences.ReservedCount",
               f"unsynchronized tracker ReservedCount read in {where}", errors)

    if quiesce_active.count("Bc250ActiveRetirementTimerStop(Context);") < 2:
        errors.append("missing second retirement timer stop after in-flight drain")

    # O portao de ciclo de vida precisa recusar submissao nova depois do
    # STOPPING. Desde a revisao final do 3.67 ele e' uma funcao testavel, em
    # vez de texto enterrado no wrapper.
    for needle, label in (
        ("BC250_ACTIVE_LIFECYCLE_RUNNING",
         "submission lifecycle gate"),
        ("InterlockedIncrement(&Context->SubmitInFlight)",
         "in-flight submission accounting"),
    ):
        require(submit_gate, needle, label, errors)
    for needle, label in (
        ("Bc250ActiveSubmissionGateEnter(context)",
         "wrapper enters submission gate"),
        ("Bc250ActiveSubmitCommandVirtualBody(",
         "single-exit submission body"),
        ("Bc250ActiveSubmissionGateLeave(context)",
         "single submission gate release"),
    ):
        require(submit_wrapper, needle, label, errors)
    require(submit_body, "BC250_ACTIVE_LIFECYCLE_RUNNING",
            "timer rearm forbidden after STOPPING", errors)
    require(submit_body, "Bc250ActiveRetirementTimerStart(context);",
            "conditional retirement timer rearm", errors)

    # Rastreador com lock proprio em todos os caminhos.
    for needle, label in (
        ("Bc250FenceLockAcquire(&Tracker->Lock", "tracker lock acquisition"),
        ("BC250_FENCE_LOCK_SPIN_LIMIT", "bounded tracker lock spin"),
        ("Bc250WddmFenceTrackerReserveUnlocked(", "locked reserve path"),
        ("Bc250WddmFenceTrackerCommitUnlocked(", "locked commit path"),
        ("Bc250WddmFenceTrackerCancelUnlocked(", "locked cancel path"),
        ("++Tracker->RejectedWhileStopping;",
         "admission gate rejection accounting"),
    ):
        require(engine, needle, label, errors)

    # Evidencia nao pode misturar corridas.
    #
    # Ate o 3.75 este gate exigia os dois resets por familia. Eles tinham
    # chamador real, mas cobertura incompleta: Shader*, HqdActive* e Dmm*
    # ficavam de fora de toda varredura, e a corrida 3.75 publicou
    # DmmChildRelationsCalled=56 e DmmIsSupportedCount=41 herdados de boots
    # anteriores. O gate passava enquanto a contaminacao acontecia.
    #
    # Agora exige o reset INTEGRAL, que zera o enum inteiro e nao depende de
    # nenhuma lista ser mantida em dia.
    driver_entry = function_body(active, "NTSTATUS DriverEntry(")
    require(driver_entry, "Bc250ProbeResetAllVolatile();",
            "integral telemetry reset at load", errors)

    # ORDEM do early-return render-only dentro de StartDevice.
    #
    # Nao basta o perfil render-only devolver zero sources/children: ele tem de
    # faze-lo ANTES de adquirir post-display ownership e antes de
    # Bc250DisplayInit. Um early-return colocado depois estaria tomando o
    # scanout do GOP para so' entao desistir — o cenario de tela preta que o
    # perfil existe para evitar. Isto e' uma propriedade de ordem no codigo, e
    # so' analise estatica a captura; nenhum teste de tabela a veria.
    start_device = function_body(active, "NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceActive(")
    early = start_device.find("BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB")
    post_display = start_device.find("Bc250AcquirePostDisplayReservation")
    display_init = start_device.find("Bc250DisplayInit(")
    if early == -1:
        errors.append("StartDevice lacks the render-only early return")
    if post_display != -1 and not (0 <= early < post_display):
        errors.append(
            "render-only early return does not precede post-display ownership")
    if display_init != -1 and not (0 <= early < display_init):
        errors.append(
            "render-only early return does not precede Bc250DisplayInit")

    # A MMU global precisa ser comum aos perfis production/lab e qualquer
    # falha precisa encerrar StartDevice. A corrida 14:32 mostrou o defeito:
    # SNAPSHOT_FAILED foi apenas logado e SetRootPageTable continuou.
    global_mmu_call = start_device.find(
        'Bc250ActiveEnsureGlobalMmu(\n                context, "StartDevice")')
    unsafe_profile_guard = start_device.find(
        "#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)")
    if global_mmu_call == -1:
        errors.append("StartDevice lacks global MMU initialization")
    if unsafe_profile_guard != -1 and not (
            0 <= global_mmu_call < unsafe_profile_guard):
        errors.append("global MMU initialization is not common to active/lab profiles")
    for needle, label in (
        ("if (!NT_SUCCESS(globalMmuNtStatus))",
         "global MMU StartDevice failure branch"),
        ("return globalMmuNtStatus;",
         "global MMU StartDevice fail-closed return"),
        ("Global MMU transport map status",
         "global MMU explicit MMIO transport preflight"),
    ):
        require(start_device, needle, label, errors)

    set_root = function_body(
        active, "NTSTATUS Bc250ActiveSetRootPageTableObject(")
    ensure_at = set_root.find("Bc250ActiveEnsureGlobalMmu(")
    publish_at = set_root.find("Bc250McdmObjectSetRootPageTable(")
    if ensure_at == -1 or publish_at == -1 or ensure_at > publish_at:
        errors.append(
            "SetRootPageTable publishes software state before global MMU readiness")
    for needle, label in (
        ("return globalMmuStatus;",
         "SetRootPageTable global MMU fail-closed return"),
        ("GlobalMmuDummyAllocated",
         "owned global MMU dummy allocation"),
        ("Bc250VramAllocate(allocator, 0x1000ull, 0x1000ull",
         "allocator-backed global MMU dummy page"),
        ("allocator = &Context->GlobalMmuAllocator;",
         "global MMU allocator independent from unstaged bootstrap"),
        ("Bc250VramReserveFixed(allocator,",
         "inherited scanout reserved before global MMU dummy"),
    ):
        require(active, needle, label, errors)

    stop_device = function_body(
        active, "static NTSTATUS Bc250DxgkDdiStopDeviceActiveReason(")
    require(stop_device, "mmuRestoreStatus = Bc250GcvmGlobalMmuRestore(",
            "checked global MMU restore", errors)
    require(stop_device, "Global GFXHUB MMU restore FALHOU",
            "truthful global MMU restore failure telemetry", errors)
    forbid(stop_device, "(void)Bc250GcvmGlobalMmuRestore",
           "unchecked global MMU restore", errors)

    # O marcador de bring-up inseguro so' pode existir sob a macro. Se ele
    # vazar para fora do #if, a deteccao por conteudo do empacotador seguro
    # passa a dar falso positivo em toda imagem, e o gate vira decoracao.
    marker = "BC250-UNSAFE-STARTDEVICE-BRINGUP-COMPILED"
    if marker in active:
        guard = active.find("#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)")
        at = active.find(marker)
        endif = active.find("#endif", guard) if guard != -1 else -1
        if not (0 <= guard < at < endif):
            errors.append(
                "unsafe bring-up marker is not enclosed by its #if guard")

    # Captura integral do GUID em QueryInterface, e marcacao de DDI uma unica
    # vez por chamada (o 3.66 marcava duas e inflava V5DdiMarks).
    require(display, "Bc250ProbeRecordQueryInterface(&record);",
            "full GUID capture on QueryInterface", errors)
    require(display, "RtlCopyMemory(record.Data4",
            "QueryInterface GUID Data4 capture", errors)
    require(active, "Bc250ProbeSnapshotQueryInterfaces();",
            "QueryInterface snapshot published at teardown", errors)
    query_interface_body = function_body(
        display, "NTSTATUS APIENTRY Bc250DxgkDdiQueryInterface(")
    if query_interface_body.count(
            "Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_INTERFACE);") != 1:
        errors.append(
            "QueryInterface must mark the DDI trace exactly once")

    # Nenhuma interface pode ser aceita por suposicao para manter o
    # adaptador vivo: a recusa continua sendo o unico retorno.
    forbid(query_interface_body, "return STATUS_SUCCESS;",
           "QueryInterface accepting an unimplemented interface", errors)
    for needle, label in (
        ("props.SchedulerMe = 2u;", "Cyan Skillfish MEC2 selector"),
        ("props.SchedulerPipe = 0u;", "Cyan Skillfish pipe selector"),
        ("props.SchedulerQueue = 0u;", "Cyan Skillfish queue selector"),
    ):
        require(one_shot_hqd, needle, label, errors)

    # RenderNode must select a reachable registration path in DriverEntry.
    # Candidate 3.63 retained DxgkInitialize only behind a DEADBEEF condition,
    # so every real boot silently remained display-only.
    driver_entry = function_body(active, "NTSTATUS DriverEntry(")
    start_v5 = function_body(
        active, "NTSTATUS APIENTRY "
                "Bc250DxgkDdiStartDeviceV5Telemetry(")
    for needle, label in (
        ('L"RenderNode"', "RenderNode DriverEntry tunable read"),
        ("renderNodePresent == 1u && renderNode == 1u",
         "reachable full-WDDM registration gate"),
        ("status = DxgkInitialize(", "full-WDDM registration call"),
        ("status = DxgkInitializeDisplayOnlyDriver(",
         "display-only registration fallback"),
        ("BC250_PROBE_V5_REGISTRATION_MODE",
         "registration-mode telemetry"),
    ):
        require(driver_entry, needle, label, errors)
    forbid(driver_entry, "0xDEADBEEF",
           "dead-code-only DxgkInitialize reachability", errors)
    for needle, label in (
        ("Bc250DxgkDdiStartDeviceActive(", "active StartDevice delegation"),
        ("BC250_PROBE_V5_START_DEVICE_STATUS",
         "full-WDDM StartDevice status telemetry"),
        ("BC250_PROBE_V5_START_DEVICE_SOURCES",
         "full-WDDM source-count telemetry"),
        ("BC250_PROBE_V5_START_DEVICE_CHILDREN",
         "full-WDDM child-count telemetry"),
    ):
        require(start_v5, needle, label, errors)

    vsync_dpc = function_body(display, "static VOID Bc250VsyncDpcRoutine(")
    synchronized_vsync = function_body(
        display, "static BOOLEAN Bc250SynchronizedVsyncCallback(")
    if not vsync_dpc:
        errors.append("missing VSync DPC body")
    if not synchronized_vsync:
        errors.append("missing synchronized VSync callback body")
    require(vsync_dpc, "DxgkCbSynchronizeExecution(",
            "DIRQL-synchronized VSync notification", errors)
    forbid(vsync_dpc, "DxgkCbNotifyInterrupt(",
           "direct NotifyInterrupt call from VSync DPC", errors)
    forbid(vsync_dpc, "Bc250ProbeRecord(",
           "persistent registry telemetry from VSync DPC", errors)
    forbid(synchronized_vsync, "Bc250ProbeRecord(",
           "persistent registry telemetry from synchronized VSync", errors)
    require(display, "KeGetCurrentIrql() == PASSIVE_LEVEL",
            "PASSIVE_LEVEL display telemetry gate", errors)
    require(display, "Mode->Format.Graphics.Stride >=",
            "source stride independent from GOP pitch", errors)
    require(display, "Mode->Format.Graphics.PixelFormat == D3DDDIFMT_X8R8G8B8",
            "32-bit XRGB source mode support", errors)
    require(display, "Bc250FillNativeSignalInfo(",
            "coherent native signal timing helper", errors)
    require(display, "BC250_NATIVE_HBLANK",
            "shared EDID/VidPN horizontal blanking", errors)
    require(display, "BC250_NATIVE_VBLANK",
            "shared EDID/VidPN vertical blanking", errors)
    require(synchronized_vsync, "DxgkCbNotifyInterrupt(",
            "NotifyInterrupt inside synchronized callback", errors)
    require(display, "InterlockedExchange(&g_Bc250DisplayState.DisplayStarted, 0);",
            "VSync lifecycle shutdown before teardown/removal", errors)
    require(display, "g_Bc250DisplayState.VsyncRequested",
            "VSync request preservation across D-state transitions", errors)
    require(display, "PostDisplayInfo->PhysicAddress.QuadPart == 0",
            "GOP physical-address validation", errors)
    require(display, "PostDisplayInfo->Pitch < (PostDisplayInfo->Width * 4u)",
            "GOP pitch validation", errors)
    require(display, "return STATUS_INSUFFICIENT_RESOURCES;",
            "fail-closed framebuffer mapping", errors)
    require(display, "ExAcquireRundownProtection(&g_Bc250DisplayState.PresentRundown)",
            "present/teardown rundown acquisition", errors)
    require(display, "ExWaitForRundownProtectionRelease(&g_Bc250DisplayState.PresentRundown)",
            "present/teardown rundown drain", errors)
    require(display, "pPresentDisplayOnly->BytesPerPixel != 4",
            "display-only present pixel-format validation", errors)
    require(display, "pPresentDisplayOnly->Pitch <= 0",
            "display-only present pitch validation", errors)
    require(display, "&pPresentDisplayOnly->pMoves[i].DestRect, &clipped",
            "bounded display-only move rectangles", errors)
    require(display, "PositionX >= g_Bc250DisplayState.CurrentMode.Width",
            "system-display horizontal origin bound", errors)
    require(display, "SourceStride < SourceWidth * 4u",
            "system-display source-stride bound", errors)
    require(display, "static NTSTATUS Bc250ValidateVidPn(",
            "shared fail-closed VidPN validator", errors)
    require(display, "if (pathCount != 1)",
            "single-path VidPN topology", errors)
    require(display, "Bc250VidPnPathSupported(path, RequirePinnedModes)",
            "VidPN source/target and transform validation", errors)
    require(display, "Bc250SourceModeMatchesGop(sourceMode)",
            "pinned source mode GOP validation", errors)
    require(display, "Bc250TargetModeMatchesGop(targetMode)",
            "pinned target mode GOP validation", errors)
    require(display, "g_Bc250DisplayState.VidPnCommitted",
            "present gated by committed VidPN", errors)
    cofunc = function_body(
        display, "NTSTATUS APIENTRY Bc250DxgkDdiEnumVidPnCofuncModality(")
    require(cofunc, "if (pathCount != 1)",
            "cofunctional enumeration single-path gate", errors)
    require(cofunc, "pfnUpdatePathSupportInfo(",
            "cofunctional identity transform publication", errors)
    require(cofunc, "Bc250PublishCofuncSourceMode(",
            "checked cofunctional source publication", errors)
    require(cofunc, "Bc250PublishCofuncTargetMode(",
            "checked cofunctional target publication", errors)
    require(display, "Successful assignment transfers ownership",
            "explicit VidPN mode-set ownership transfer", errors)
    forbid(active, "context->PostDisplayEvidence.Width ?",
           "fabricated fallback GOP width", errors)
    forbid(active, "context->PostDisplayEvidence.Height ?",
           "fabricated fallback GOP height", errors)
    set_address = function_body(display, "NTSTATUS APIENTRY Bc250DxgkDdiSetVidPnSourceAddress(")
    legacy_present = function_body(display, "NTSTATUS APIENTRY Bc250DxgkDdiPresent(")
    require(set_address, "STATUS_NOT_SUPPORTED",
            "unused full-WDDM source-address DDI fails closed", errors)
    require(legacy_present, "STATUS_NOT_SUPPORTED",
            "unused full-WDDM present DDI fails closed", errors)

    # PM4 GFX10 must be structurally complete before any hardware claim.
    for needle in (
        "BC250_PM4_IT_SET_UCONFIG_REG     0x79u",
        "BC250_PM4_UCONFIG_REG_BASE 0xC000u",
    ):
        require(pm4_header, needle, "canonical GFX10 UCONFIG encoding", errors)
    for needle in (
        "ExtraCount == 0u",
        "BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_NOP, ExtraCount)",
        "(2u << 29)",
        "0u); /* ctxid/reserved */",
    ):
        require(pm4_builder, needle, "complete PM4 packet encoding", errors)
    for needle in (
        "0xC0017900u",
        "0x00000040u",
        "0xC0064900u",
        "0x06603514u",
        "0x20000000u",
        "submittedDwords != 11u",
        "rptrPoll == submittedDwords",
    ):
        require(compute_mqd, needle, "canonical HQD 3+8 dword payload", errors)
    forbid(compute_mqd, "0x00002040u", "raw segment-less SCRATCH PM4 target", errors)

    # Runtime evidence must not be synthesized by AddDevice/StartDevice.
    forbid(active, "Bc250BuildDefaultActiveRuntime", "synthetic runtime builder", errors)
    require(active,
            "context->RuntimeInventoryReport.OwnershipEvidenceFlags;",
            "runtime-derived ownership flags", errors)
    forbid(active, "Reservations[0].Offset =", "fixed inherited reservation", errors)
    require(active,
            "context->Core.ReadonlyGatePassed == 1u &&",
            "VRAM mutation gated by passed readonly preflight", errors)
    require(active, "InterlockedExchange(&context->IsActive, 0);",
            "StartDevice inactive state", errors)
    require(active, "return preflightStatus;",
            "StartDevice fail-closed return", errors)
    require(active, "Bc250AcquirePostDisplayReservation(",
            "physical post-display ownership capture", errors)
    require(active, "BC250_PROBE_POST_DISPLAY_STATUS",
            "post-display telemetry", errors)
    require(active, "BC250_RUNTIME_EVIDENCE_SCANOUT_PROBED |",
            "partial runtime inventory evidence", errors)
    require(active, "BC250_PROBE_RUNTIME_INVENTORY_COMPLETE",
            "runtime inventory completeness telemetry", errors)
    require(active, "Bc250ActiveQueryRuntimeCaps(",
            "runtime-backed capability provider", errors)
    require(callbacks, "runtimeCaps.LocalSegmentBytes",
            "runtime-backed segment sizing", errors)
    require(callbacks, "case DXGKQAITYPE_QUERYSEGMENT4:",
            "mandatory MCDM QUERYSEGMENT4", errors)
    require(callbacks, "case DXGKQAITYPE_NUMPOWERCOMPONENTS:",
            "mandatory MCDM power component query", errors)
    require(callbacks, "case DXGKQAITYPE_HISTORYBUFFERPRECISION:",
            "mandatory MCDM history precision query", errors)
    require(callbacks, "runtimeCaps.FullMcdmReady == 0u",
            "full MCDM capability gate", errors)
    forbid(callbacks, "caps->SupportNonVGA = TRUE",
           "MCDM-prohibited non-VGA capability", errors)
    # Alvo do driver: WDDM display+render (jogos).
    #
    # Este gate exigia MiscCaps.ComputeOnly = 1u, do alvo MCDM original. Essa
    # exigencia entrou em contradicao direta com o resto do driver assim que o
    # miniport passou a registrar por DxgkInitialize em 2026-08-31: o INF
    # declara Class=Display, o StartDevice reporta uma fonte de video e um
    # filho, e a tabela de VidPn esta inteira preenchida e provada em hardware.
    #
    # Um adaptador ComputeOnly nao apresenta nada. Manter a exigencia seria
    # congelar a contradicao que o dxgkrnl rejeita com STATUS_INTERNAL_ERROR.
    # A propriedade de seguranca que importa continua garantida logo acima:
    # SupportNonVGA nao pode ser TRUE.
    #
    # 2026-09-02: o bit deixou de ser literal e passou a ser derivado do
    # perfil, porque o perfil de laboratorio RENDER_ONLY_LAB reporta 0 sources
    # e 0 children e para ELE o valor coerente e' 1. O forbid abaixo continua
    # valendo -- ele impede que alguem volte a cravar 1u no fonte --, mas
    # sozinho ele nao alcanca mais o caso derivado.
    #
    # Por isso os dois require: o primeiro garante que o valor vem do getter
    # (e nao de um literal novo), e o segundo garante que o UNICO perfil que
    # recebe 1 e' o de laboratorio. A garantia de que os perfis de PRODUCAO
    # reportam 0 e' fechada por Bc250RunAdapterProfileTest, que quebra o build.
    forbid(callbacks, "caps->MiscCaps.ComputeOnly = 1u",
           "compute-only capability incompatible with display+render target",
           errors)
    require(callbacks, "caps->MiscCaps.ComputeOnly = Bc250McdmGetComputeOnly();",
            "compute-only capability must be derived from the adapter profile",
            errors)
    require(active,
            "(Profile == BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB) ? 1u : 0u",
            "compute-only may only be declared by the laboratory profile",
            errors)
    forbid(active, "BC250_RUNTIME_EVIDENCE_COMPLETE;",
           "fabricated complete runtime inventory", errors)
    require(active, "BC250_RUNTIME_EVIDENCE_PLATFORM_CARVEOUTS_PROBED",
            "explicit platform carveout evidence", errors)
    require(active, "AddressSpace.CpuVisibleBytes;",
            "runtime-derived opaque carveout boundary", errors)
    require(active, "BC250_VRAM_ROLE_PLATFORM_RESERVED",
            "opaque non-visible VRAM reservation", errors)
    forbid(active, "0x1F800000", "historical fixed PSP TMR offset", errors)
    require(readonly_ddi, "Bc250CapturePlatformRegistersReadonly(",
            "readonly platform-register capture", errors)
    require(readonly_ddi, "BC250_PROBE_PLATFORM_SNAPSHOT_STATUS",
            "platform-register telemetry", errors)
    require(platform_snapshot, "for (pass = 0u; pass < 2u; ++pass)",
            "two-pass stable platform snapshot", errors)
    require(platform_snapshot, "BC250_PLATFORM_SNAPSHOT_UNSTABLE",
            "unstable platform snapshot rejection", errors)
    require(vram_probe, "original[i] = target[i]",
            "VRAM probe original-byte capture", errors)
    require(vram_probe, "target[i] = original[i]",
            "VRAM probe restoration", errors)
    require(vram_probe, "BC250_VRAM_PROBE_RESTORE_FAILED",
            "VRAM probe restore verification", errors)
    require(paging, "Plan->EngineId = 17u;",
            "GC 10.1.3 GFXHUB invalidation engine", errors)
    require(paging, "Plan->RequiresDummyRequestRead = 1u;",
            "GFXHUB invalidation dummy request read", errors)
    require(paging, "Plan->UsesSemaphore = 0u;",
            "GFXHUB no-semaphore contract", errors)
    require(paging, "Plan->AckMask = 1u << Params->Vmid;",
            "per-VMID invalidation ACK mask", errors)
    flush_start = paging.find("BC250_PAGING_STATUS Bc250PagingBuildFlushTlb(")
    flush_body = paging[flush_start : flush_start + 850]
    forbid(flush_body, "Bc250SdmaEmitNop",
           "fake SDMA NOP TLB invalidation", errors)
    require(flush_body, "BC250_PAGING_UNSUPPORTED_OPERATION",
            "physical TLB flush remains fail-closed", errors)
    require(tlb_transaction, "Evidence->AckBefore == 0xFFFFFFFFu",
            "all-ones ACK rejection", errors)
    require(tlb_transaction, "Evidence->RequestDummyRead",
            "mandatory post-request dummy read evidence", errors)
    require(tlb_transaction, "BC250_TLB_TRANSACTION_TIMEOUT",
            "bounded TLB ACK timeout", errors)
    require(tlb_transaction, "Evidence->RangeRestorePassed = 1u;",
            "invalidation range restoration proof", errors)
    require(mmio_transport,
            "static uint32_t Bc250MmioTopologyWriteOffsetAllowed",
            "explicit MMIO write allowlist", errors)
    require(mmio_transport, "ByteOffset == 0xA2D0u ||",
            "GFXHUB engine-17 request write", errors)
    write_allowlist_start = mmio_transport.find(
        "static uint32_t Bc250MmioTopologyWriteOffsetAllowed")
    write_allowlist_body = mmio_transport[
        write_allowlist_start : write_allowlist_start + 350]
    forbid(write_allowlist_body, "ByteOffset == 0xA318u",
           "write access to read-only ACK", errors)
    require(active, "Bc250ActiveExecuteGcvmBacking(",
            "owned GCVM backing StartDevice transaction", errors)
    require(active, "Bc250ActiveFlushMappedGfxhubTlb(",
            "physical GFXHUB TLB transaction inside VMID8 probe", errors)
    require(active, "context->TlbTransactionEvidence.PhysicalReady",
            "hardware-derived TLB readiness telemetry", errors)
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
        body = function_body(callbacks, f"NTSTATUS APIENTRY {function}(")
        if not body:
            body = function_body(callbacks, f"NTSTATUS {function}(")
        if not body:
            errors.append(f"missing callback: {function}")
            continue
        require(body, "STATUS_NOT_SUPPORTED", f"fail-closed {function}", errors)

    build_paging = function_body(
        callbacks, "NTSTATUS APIENTRY Bc250McdmBuildPagingBuffer(")
    require(build_paging, "Unhandled Op=%u -> NOT_SUPPORTED",
            "unknown paging operation diagnostic", errors)
    forbid(build_paging, "Unhandled Op=%u -> SUCCESS",
           "unknown paging operation false success", errors)
    require(callbacks, "update->Flags.Repeat != 0u ? 1u",
            "Repeat PTE diagnostic bounds", errors)
    require(callbacks, "g_Bc250PagingStages[64]",
            "lock-free paging stage ring", errors)
    paging_stage = function_body(callbacks, "static VOID Bc250PagingStage(")
    require(paging_stage, "InterlockedIncrement",
            "atomic paging stage reservation", errors)
    require(paging_stage, "KeMemoryBarrier",
            "paging stage publication barrier", errors)
    forbid(paging_stage, "Zw", "blocking Zw call in paging stage ring", errors)
    forbid(paging_stage, "Bc250DisplayLog",
           "file diagnostic call in paging stage ring", errors)

    # A UMD de producao nunca pode esconder ausencia de render nativo com WARP.
    forbid(umd, "d3d10warp.dll", "WARP software rasterizer delegation", errors)
    forbid(umd, "Bc250EnsureWarp", "WARP loader in production UMD", errors)
    require(umd, "BC250_UMD_LOG_%lu.txt",
            "per-process UMD trace file", errors)
    require(umd_header, "#define BC250_D3D10_11_NATIVE_READY 0",
            "D3D10/11 production readiness gate", errors)
    for signature in (
        "HRESULT APIENTRY OpenAdapter10(",
        "HRESULT APIENTRY OpenAdapter10_2(",
        "HRESULT APIENTRY OpenAdapter11(",
    ):
        open_legacy = function_body(umd, signature)
        require(open_legacy, "return E_NOTIMPL;",
                f"fail-closed {signature.split('(')[0]}", errors)
    open12 = function_body(umd, "HRESULT APIENTRY OpenAdapter12(")
    require(open12, "return E_NOTIMPL;",
            "fail-closed D3D12 adapter advertisement", errors)
    forbid(open12, "return S_OK;",
           "D3D12 adapter success without native CreateDevice", errors)
    require(winsys, "pfnCreateContextVirtualCb",
            "WDDM2 virtual-context callback path", errors)
    require(winsys, "pfnAllocateCb",
            "VidMm allocation callback path", errors)
    require(winsys, "pfnSubmitCommandCb",
            "dxgkrnl virtual submission callback path", errors)
    require(winsys, "BroadcastContextCount = 1u",
            "explicit submission context ownership", errors)
    require(winsys, "BC250_ALLOCATION_PRIVATE_MAGIC",
            "versioned allocation ABI from UMD to KMD", errors)
    require(active, "Bc250AllocationIsPrimary(&request)",
            "primary allocation selected by explicit ABI token", errors)
    require(active, "Bc250ActiveGpuvaRootKey(",
            "zero-offset segment root encoded without null sentinel collision", errors)
    require(active, "flushRootKey != 0ull",
            "GPUVA flush accepts a valid zero segment offset through root key", errors)
    require(objects, "processRec->RootPageTableSetCount == 0u",
            "root presence uses explicit SetRootPageTable state", errors)
    forbid(active, "request.ByteSize >= 11059200ull ||",
           "primary allocation inferred from size/first resource", errors)
    forbid(winsys, "FenceValue++", "CPU-authored winsys fence", errors)
    forbid(winsys, "Sleep(", "winsys delay pretending execution", errors)
    require(winsys_test, "WDDM WINSYS CONTRACT: PASS",
            "offline winsys lifecycle contract", errors)
    require(umd_build, "bc250_wddm_winsys_test.exe",
            "winsys contract integrated in UMD build", errors)
    require(build, "Visual Studio\\18",
            "KMD build selects Visual Studio 2026 explicitly", errors)
    require(build, "set WDKVER=10.0.28000.0",
            "KMD build pins SDK/WDK 28000", errors)
    require(umd_build, "Visual Studio\\18",
            "UMD build selects Visual Studio 2026 explicitly", errors)
    forbid(build, "Visual Studio\\2022",
           "VS2022 fallback in the canonical KMD build", errors)
    forbid(umd_build, "Visual Studio\\2022",
           "VS2022 fallback in the canonical UMD build", errors)
    require(lab_package, "10.0.28000.0\\x86\\Inf2Cat.exe",
            "lab packaging prefers the WDK 28000 Inf2Cat", errors)
    require(lab_package, "10.0.28000.0\\x64\\signtool.exe",
            "lab packaging prefers the WDK 28000 SignTool", errors)
    require(collector, "LogName='Application'",
            "Application event capture for DWM failures", errors)
    require(collector, "dwm\\.exe|0xe0464645|bc250umd",
            "DWM/UMD event filter", errors)
    require(bind, "arm-dwm-crash-capture.ps1",
            "DWM crash capture before risky bind", errors)
    require(bind, "RunRadvComputeTripod nao pertence a trilha ativa",
            "active bind refuses the lab-only RADV tripod", errors)
    require(lab_bind, "-EnableGpuvaLabPromotion exige -IAcceptGpuvaLabPromotion",
            "GPUVA promotion is opt-in with explicit acceptance", errors)
    require(lab_bind, "-EnableGpuvaLabPromotion exige -EnablePersistentSubmission",
            "GPUVA promotion requires a submission node", errors)
    require(lab_bind, "GpuvaLabPromotion = $(if ($EnableGpuvaLabPromotion) { 1 } else { 0 })",
            "GPUVA promotion defaults to disabled in the pre-bind policy", errors)
    require(lab_package, "HKR,Parameters,GpuvaLabPromotion,0x00010003,0",
            "lab INF declares the GPUVA promotion as a NOCLOBBER default", errors)
    require(lab_bind, "-RunRadvComputeTripod exige -EnableGpuvaLabPromotion",
            "lab tripod hook requires the GPUVA gate open", errors)
    require(lab_bind, "-RunRadvComputeTripod exige o watchdog",
            "lab tripod hook requires the armed rollback watchdog", errors)
    require(lab_bind, "ICD explicito validado antes do binding",
            "lab bind proves the explicit ICD identity before APPLY", errors)
    require(lab_bind, "RADV_WDDM_COMPUTE_TRIPOD falhou",
            "lab bind fails closed on RADV preflight failure", errors)
    require(lab_bind, "compute-display-prerequisite-gate.json",
            "lab bind records the selected compute display gate", errors)
    require(lab_bind, "[GetStdAllocData\\] SharedPrimary:",
            "RADV requires a primary allocation from this bind", errors)
    require(lab_bind, "=== CommitVidPn Entered:",
            "RADV requires CommitVidPn from this bind", errors)
    require(lab_bind, "=== SetVidPnSourceAddress ",
            "RADV requires SetVidPnSourceAddress from this bind", errors)
    require(lab_package, "[ValidateSet(20, 27)][int]$WddmContractVersion",
            "lab WDDM contract version is an explicit, bounded choice", errors)
    require(lab_package, "$sourceDriverVersion.Build + 1",
            "lab package ranks above the active package sharing its HWID", errors)
    require(lab_bind, "[uint32]$manifest.WddmContractVersion",
            "lab bind takes the WDDM contract from the manifest", errors)
    require(lab_bind, "PackageUmdSha256 'UMD'",
            "lab bind checks the signed UMD package identity", errors)
    require(lab_bind, "SourceUmdSha256",
            "lab bind rejects a package stale against the UMD build", errors)
    require(lab_bind, "Escolha uma unica experiencia fisica: RADV ou B7_UMD_NODE1_TRIPOD",
            "lab bind forbids two physical experiments in one run", errors)
    require(lab_bind, "-RunUmdNode1Tripod exige -EnableGpuvaLabPromotion",
            "UMD Node 1 run requires explicit GPUVA promotion", errors)
    require(lab_bind, "-RunUmdNode1Tripod exige o watchdog",
            "UMD Node 1 run requires automatic rollback", errors)
    require(lab_bind, "-IAcceptOnePhysicalNode1Submission",
            "lab bind passes explicit consent to the UMD Node 1 runner", errors)
    require(lab_bind, "umd-node1-tripod-console.txt",
            "lab bind archives the UMD Node 1 runner console", errors)
    require(umd_node1_tripod, "PackageUmdSha256",
            "UMD Node 1 runner is bound to the signed package UMD", errors)
    require(umd_node1_tripod, "watchdogRemaining -lt ($TimeoutSeconds + 10)",
            "UMD Node 1 run fits inside the rollback deadline", errors)
    require(umd_node1_tripod, "watchdog.instance_id",
            "UMD Node 1 watchdog belongs to the exact device", errors)
    require(umd_node1_tripod, "WaitForExit($TimeoutSeconds * 1000)",
            "UMD Node 1 child lifetime is bounded", errors)
    require(umd_node1_tripod, "Stop-Process -Id $harnessProcess.Id -Force",
            "timed-out UMD Node 1 child is terminated before rollback", errors)
    require(umd_node1_tripod, "authorization_disarmed",
            "UMD Node 1 authorization teardown is measured", errors)
    require(radv_tripod, "output_lab.manifest.json",
            "RADV tripod bound to the lab bring-up package", errors)
    require(radv_tripod, "BuildKind -ne 'MCDM_LAB_BRINGUP'",
            "RADV tripod rejects non-lab packages", errors)
    require(radv_tripod, "manifest.SkipHwBringup -ne 0",
            "RADV tripod requires compiled hardware bring-up", errors)
    require(radv_tripod, "ProfileMode -ne 'DISPLAY_RENDER_LAB'",
            "RADV tripod requires the DISPLAY_RENDER profile", errors)
    require(radv_tripod, "GpuvaLabPromotionPresent -ne 1",
            "RADV tripod requires the GPUVA promotion armed", errors)
    require(radv_tripod, "gpuva_gate_operational",
            "GPUVA gate member of RADV tripod", errors)
    require(radv_tripod, "watch-bc250mcdm-lab\\.ps1",
            "RADV tripod watches the lab rollback watchdog", errors)
    require(radv_tripod, "watchdogState.state -ne 'ARMED'",
            "RADV physical gate requires armed watchdog", errors)
    require(radv_tripod, "processo exato do watchdog nao esta vivo",
            "RADV physical gate verifies live watchdog process", errors)
    require(radv_tripod, "$watchdogRemaining -lt ($TimeoutSeconds + 10)",
            "RADV run fits inside rollback deadline", errors)
    require(radv_tripod, "installedSysHash -ne [string]$manifest.PackageSysSha256",
            "RADV physical gate verifies installed SYS", errors)
    require(radv_tripod, "EXPLICIT_DIRECT_LOAD_NO_VULKAN_LOADER",
            "test records explicit ICD direct-load mode", errors)
    require(radv_tripod, "loaded_icd_identity",
            "loaded ICD identity is a physical-test gate", errors)
    require(radv_gate, "LoadLibraryExA(requested",
            "harness loads the exact ICD path directly", errors)
    require(radv_gate, "GetModuleFileNameA(g_icd_module",
            "harness reads back the loaded ICD module path", errors)
    require(radv_gate, "vk_icdGetInstanceProcAddr",
            "harness enters through the loader/ICD ABI", errors)
    require(radv_tripod, "WaitForExit($TimeoutSeconds * 1000)",
            "bounded RADV child lifetime", errors)
    require(radv_tripod, "Save-RadvHangDump $process.Id",
            "hung RADV process dump captured before termination", errors)
    require(radv_tripod, "radv-harness-hang.dmp",
            "stable hang dump evidence name", errors)
    require(radv_tripod, "$dumpBytes -ge 1MB",
            "hang dump must exceed the truncation floor", errors)
    require(radv_tripod, "radv-harness-hang-capture-state.json",
            "hang dump capture has an explicit measured verdict", errors)
    forbid(radv_tripod, "https://msdl.microsoft.com/download/symbols",
           "network symbol lookup inside bounded hang capture", errors)
    require(radv_tripod, "physical_fence_replaced_poison",
            "physical fence member of RADV tripod", errors)
    require(radv_tripod, "byte_proof",
            "byte proof member of RADV tripod", errors)
    require(radv_tripod, "hardware_validated = $hardwareValidated",
            "measured RADV hardware verdict", errors)
    forbid(radv_tripod, "$env:VK_DRIVER_FILES =",
           "process-global Vulkan ICD mutation", errors)
    forbid(radv_tripod, "EnvironmentVariables['VK_DRIVER_FILES']",
           "elevated Vulkan Loader override that Windows ignores", errors)
    require(arm_dwm, "LocalDumps\\dwm.exe",
            "per-application WER LocalDumps key", errors)
    require(arm_dwm, "dwm-localdumps-before.reg",
            "WER registry snapshot before mutation", errors)
    require(arm_dwm, "DumpType", "full DWM dump configuration", errors)
    require(arm_dwm, "Get-ItemProperty -LiteralPath $werKey",
            "WER LocalDumps readback", errors)
    require(restore_dwm, "BC250_DWM_DUMPS_[0-9a-f]{32}",
            "exact DWM dump folder restore gate", errors)
    require(restore_dwm, "Copy-Item -LiteralPath $dump.FullName",
            "DWM dump preservation before teardown", errors)
    require(restore_dwm, "RESTORED_PENDING_DUMPS",
            "WER registry restoration independent from locked dumps", errors)
    require(restore_dwm, "registry_restored_at",
            "WER registry restoration journal", errors)
    require(collector, "restore-dwm-crash-capture.ps1",
            "collector WER teardown", errors)
    require(watchdog, "Restore-DwmCapture",
            "watchdog WER teardown", errors)

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
    require(active, "context->AllocationSegmentSet != 1u",
            "allocation segment-1 readiness gate", errors)
    require(active, "request.ByteSize > context->AllocationSegmentBytes",
            "allocation physical segment bound", errors)
    require(active, "info->PreferredSegment.SegmentId0 = 1u",
            "allocation segment-1 preference", errors)
    require(active, "Bc250AllocationAbiParse(",
            "versioned allocation private-data validation", errors)
    require(callbacks, "case DXGKQAITYPE_GPUMMUCAPS:",
            "conditional GPU MMU capabilities query", errors)
    require(callbacks, "case DXGKQAITYPE_PAGETABLELEVELDESC:",
            "conditional page-table level query", errors)
    require(callbacks, "runtimeCaps.GpuVaReady == 0u",
            "GPUVA query readiness gate", errors)
    require(active, "RootPageTableAbiReady",
            "root page-table lifecycle gate", errors)
    require(active, "Bc250McdmObjectSetRootPageTable(",
            "owned root page-table association", errors)
    require(callbacks, "pSetPageTable->Address.SegmentId",
            "VidMm segment-address root page table", errors)
    require(callbacks, "DXGK_OPERATION_UPDATE_PAGE_TABLE",
            "VidMm page-table update operation", errors)
    require(callbacks, "DXGK_PAGETABLEUPDATE_CPU_VIRTUAL",
            "immediate CPU-virtual page-table update", errors)
    require(callbacks, "Source->Segment != 1u",
            "unsupported system/GART PTE rejection", errors)
    provider_start = active.find("static NTSTATUS Bc250ActiveQueryRuntimeCaps")
    provider_body = active[provider_start : provider_start + 4200]
    forbid(provider_body, "Caps->GpuVaReady =",
           "production GPUVA readiness assertion", errors)
    forbid(provider_body, "Caps->PagingReady =",
           "production paging readiness assertion", errors)
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

    # Candidato 3.67: a regressao medida no silicio precisa continuar coberta
    # offline, e o gate fail-closed precisa continuar provado nos dois lados.
    for needle, label in (
        ("Bc250TestSynchronizeExecutionAlwaysFails",
         "broken DxgkCbSynchronizeExecution fixture"),
        ("Bc250RunStopQuiescenceTest",
         "StopDevice quiescence regression harness"),
        ("Bc250RunTrackerRaceTest",
         "submit/retire/stop tracker race harness"),
        ("Bc250RunRenderTraceResetTest",
         "render/DMM trace reset harness"),
        ("STATUS_UNSUCCESSFUL",
         "synchronize-execution failure reproduced offline"),
    ):
        require(active_test, needle, label, errors)
    quiescence_test = function_body(
        active_test, "static int Bc250RunStopQuiescenceTest(")
    # Os dois lados do criterio: vazio conclui, nao-vazio continua ocupado.
    require(quiescence_test,
            "Bc250DxgkDdiStopDeviceActive(context) != STATUS_SUCCESS",
            "empty-tracker teardown completes despite broken sync", errors)
    require(quiescence_test,
            "Bc250DxgkDdiStopDeviceActive(context) != STATUS_DEVICE_BUSY",
            "non-empty tracker stays fail-closed", errors)
    # O teste nao pode fabricar sucesso limpando o rastreador.
    forbid(quiescence_test, "Bc250WddmFenceTrackerReset(",
           "artificial tracker clear inside teardown test", errors)

    # Recovery must recognize the exact active service/binary identity.
    for needle in (
        "call :remove_service bc250mcdm",
        "call :delete_driver bc250mcdm.sys",
        "call :enable_and_verify_basic_display",
        'findstr.exe /I /C:"AddService=bc250mcdm,"',
        'findstr.exe /I /C:"ServiceBinary=%%12%%\\bc250mcdm.sys"',
    ):
        require(recovery, needle, "active recovery coverage", errors)

    for needle in (
        "Class=Display",
        "PCI\\VEN_1002&DEV_13FE",
        "AddService=bc250mcdm,",
        "ServiceBinary=%12%\\bc250mcdm.sys",
        "InstalledDisplayDrivers",
        "UserModeDriverName",
        "bc250umd.dll",
    ):
        require(inf, needle, "strict active INF identity", errors)

    require(stage, "audit-bc250mcdm-prebinding.ps1",
            "clean-host pre-binding gate before stage", errors)
    require(stage, "$output = (& pnputil.exe /add-driver $packageInf 2>&1)",
            "stage-only pnputil invocation", errors)
    require(stage, "if (-not $Execute)",
            "zero-write default stage mode", errors)
    for forbidden_stage_path in (
        "UpdateDriverForPlugAndPlayDevices",
        "INSTALLFLAG_FORCE",
        "pnputil.exe /add-driver $packageInf /install 2>&1",
    ):
        forbid(stage, forbidden_stage_path,
               "binding operation in stage-only script", errors)

    require(replace, "Fluxo direto replace desativado",
            "legacy direct replacement mutation block", errors)
    require(replace, "Zero escrita, zero binding",
            "legacy replacement read-only default", errors)
    forbid(replace, "UpdateDriverForPlugAndPlayDevicesW",
           "binding API in blocked legacy replacement", errors)
    forbid(replace, "pnputil.exe /delete-driver",
           "package deletion in blocked legacy replacement", errors)
    forbid(replace, "Remove-Item",
           "file deletion in blocked legacy replacement", errors)
    require(orphan_cleanup, "if (-not $Execute)",
            "zero-write default orphan cleanup", errors)
    require(orphan_cleanup, "if (-not $IAcceptOrphanCleanup)",
            "explicit orphan cleanup acceptance", errors)
    require(orphan_cleanup, "$service.Status -ne 'Stopped'",
            "stopped-service orphan gate", errors)
    require(orphan_cleanup, "if (Test-Path -LiteralPath $driverPath)",
            "experimental SYS orphan gate", errors)
    require(orphan_cleanup, "if ($servicePresent -or $serviceRegistryPresent)",
            "unreferenced-binary gate before SYS quarantine", errors)
    require(orphan_cleanup, "$pendingDriverRenames = Get-DriverPendingRenameEntries",
            "pending rename gate before SYS quarantine", errors)
    require(orphan_cleanup, "if ($lastBootUtc -le $driverWriteUtc)",
            "boot-after-write gate before SYS quarantine", errors)
    require(orphan_cleanup, "if ($matchingPackages.Count -ne 0)",
            "absent MCDM package gate", errors)
    require(orphan_cleanup, "reg.exe export $serviceRegistryNative",
            "recoverable service registry snapshot", errors)
    require(orphan_cleanup, "Move-Item -LiteralPath $markerPath -Destination $quarantinedMarker",
            "recoverable marker quarantine", errors)
    require(orphan_cleanup, "Move-Item -LiteralPath $driverPath -Destination $quarantinedDriver",
            "recoverable orphan SYS quarantine", errors)
    require(orphan_cleanup, "sc.exe delete bc250mcdm",
            "exact orphan service deletion", errors)
    forbid(orphan_cleanup, "Remove-Item",
           "irrecoverable deletion in orphan cleanup", errors)
    forbid(orphan_cleanup, "del ",
           "irrecoverable deletion in orphan cleanup", errors)
    require(recovery,
            'findstr.exe /I /C:"Class=ComputeAccelerator" /C:"Class=Display"',
            "recovery coverage for compute and display package classes", errors)
    require(rollback, "^Class=(ComputeAccelerator|Display)",
            "rollback coverage for compute and display package classes", errors)

    require(bind, "audit-bc250mcdm-staged.ps1",
            "staged-package gate before binding", errors)
    require(bind, "if (-not $Execute)",
            "zero-write default binding mode", errors)
    require(bind, "if (-not $IAcceptBlackScreenRisk)",
            "explicit black-screen risk acceptance", errors)
    require(bind, "[Environment]::Is64BitProcess",
            "64-bit binding process gate", errors)
    require(bind, "UpdateDriverForPlugAndPlayDevicesW(",
            "explicit binding API", errors)
    require(bind, "$installFlagForce = [uint32]0x00000001",
            "explicit INSTALLFLAG_FORCE value", errors)
    require(bind, "C:\\BC250_RECOVERY.cmd",
            "binding recovery instruction", errors)
    require(bind, "hardware_validated = $false",
            "binding evidence remains unvalidated", errors)
    require(bind, "$watchdogProcess = Start-Process",
            "watchdog armed before binding", errors)
    if bind.find("$watchdogProcess = Start-Process") > bind.find(
            "$nativeOk = [Bc250McdmNewDev.NativeMethods]::UpdateDriverForPlugAndPlayDevicesW("):
        errors.append("watchdog is armed after binding mutation")
    require(bind, "Read-Host 'Confirmacao visual'",
            "explicit visual confirmation before watchdog disarm", errors)
    require(bind, "-IConfirmDisplayIsVisible",
            "visual confirmation acknowledgement", errors)

    # EnumVidPnCofuncModality must leave the support field corresponding to a
    # scaling/rotation pivot unchanged.  Violating this DMM contract is a
    # concrete convergence defect, not a hardware hypothesis.
    require(display, "D3DKMDT_EPT_SCALING))",
            "scaling-pivot support preservation", errors)
    require(display, "D3DKMDT_EPT_ROTATION))",
            "rotation-pivot support preservation", errors)

    # The lab candidate reads RenderNode in DriverEntry and the remaining
    # policy in StartDevice. Commit/readback must precede NewDev.
    require(lab_bind, "PRE_BIND_POLICY_APPLIED_READBACK_OK",
            "transactional lab pre-bind policy evidence", errors)
    require(lab_bind, "$parametersBefore.PSObject.Properties[$entry.Key]",
            "missing registry property-safe policy snapshot", errors)
    require(lab_bind,
            "PersistentSubmission = $(if ($EnablePersistentSubmission)",
            "persistent-submission pre-bind policy", errors)
    require(lab_bind, "RenderNode = $(if ($EnableRenderNode)",
            "RenderNode pre-bind policy", errors)
    require(lab_bind, "$probeNow.P1EngineState",
            "dirty persistent-engine state gate", errors)
    require(lab_bind, "$probeNow.P1SelfTestPhysicalReady",
            "dirty P1 physical-ready gate", errors)
    require(lab_bind, "$probeNow.P2PhysicalReady",
            "dirty P2 physical-ready gate", errors)
    require(lab_bind, "if ($hqdBefore -ne 0 -or $wptrLeft -ne 0)",
            "dirty engine gate uses queue residue indicators", errors)
    require(lab_bind, "Probe historico preservado (nao e indicador de fila suja)",
            "P1/P2 readiness is classified as historical telemetry", errors)
    require(lab_bind, "C:\\BC250_REQUIRE_CLEAN_BOOT.json",
            "post-rollback clean-boot marker gate", errors)
    require(lab_bind, "LastBootUpTime",
            "post-rollback boot identity gate", errors)
    policy_index = lab_bind.find("$requestedPolicy = [ordered]@{")
    update_index = lab_bind.find(
        "[Bc250LabNewDev.NativeMethods]::UpdateDriverForPlugAndPlayDevicesW(")
    if policy_index < 0 or update_index < 0 or policy_index > update_index:
        errors.append("lab binding policy is not applied before NewDev")
    require(lab_watchdog, "Write-WatchdogState 'ROLLBACK_STARTED'",
            "lab watchdog rollback transition", errors)
    require(lab_watchdog, "-Execute -IAcceptDisplayInterruptionRisk",
            "lab watchdog bounded rollback invocation", errors)
    require(lab_rollback, "$devices[0].Service -eq 'BasicDisplay'",
            "lab rollback BasicDisplay verification", errors)
    require(lab_rollback, "$devices[0].ConfigManagerErrorCode -eq 0",
            "lab rollback Code 0 verification", errors)
    require(lab_rollback, "if (-not $Execute)",
            "lab rollback Execute parameter gate", errors)
    require(lab_rollback, "if (-not $IAcceptDisplayInterruptionRisk)",
            "lab rollback risk confirmation gate", errors)
    forbid(lab_rollback, "/disable-device $dev.PNPDeviceID /force",
           "unsupported /force on pnputil /disable-device", errors)
    require(lab_rollback, "throw \"Rollback incompleto:",
            "lab rollback fail-closed result", errors)
    require(lab_rollback, "C:\\BC250_REQUIRE_CLEAN_BOOT.json",
            "lab rollback clean-boot marker", errors)
    require(lab_rollback, "rollback_boot_ticks",
            "lab rollback boot identity snapshot", errors)
    require(lab_rollback, "$requiresCleanBoot = -not",
            "lab rollback conditional clean-boot decision", errors)
    require(arm_reboot, "[switch]$RequireRenderNode",
            "render-node reboot guard mode", errors)
    require(arm_reboot, "V5RegistrationMode = 0",
            "render-node pre-reboot KMDOD proof", errors)
    require(confirm_reboot, "$requiredProbe['V5RegistrationMode'] = 1",
            "post-reboot real V5 registration gate", errors)
    require(confirm_reboot, "$requiredProbe['V5StartDeviceStatus'] = 0",
            "post-reboot V5 StartDevice success gate", errors)
    require(confirm_reboot, "$requiredProbe['P2PhysicalReady'] = 1",
            "post-reboot P2 physical proof gate", errors)
    require(watchdog_reboot, "[bool]$confirmation.render_node_validated",
            "watchdog render-node confirmation gate", errors)
    require(render_prepare, "V5RegistrationMode = 0",
            "render promotion starts from proved KMDOD", errors)
    require(render_prepare, "P2PhysicalReady = 1",
            "render promotion requires physical P2", errors)
    require(render_prepare, "Set-ItemProperty -LiteralPath $parameterKey -Name RenderNode -Value 1",
            "render promotion apply", errors)
    require(render_prepare, "render_node_readback = [uint32]$readback",
            "render promotion readback evidence", errors)
    require(render_prepare, "Remove-ItemProperty -LiteralPath $parameterKey -Name RenderNode",
            "render promotion rollback for absent snapshot", errors)
    require(render_inspect, "V5LastDdi",
            "post-reboot V5 DDI trace capture", errors)
    require(render_inspect, "Nenhum rollback automatico foi solicitado.",
            "unguarded render inspection disclosure", errors)
    forbid(bind, "Start-Service bc250mcdm",
           "manual service start outside PnP binding", errors)
    forbid(bind, "pnputil.exe /delete-driver",
           "package deletion in binding harness", errors)
    require(collector, "Services\\bc250mcdm\\Parameters\\Probe",
            "MCDM probe telemetry collection", errors)
    require(collector, "setupapi-after-tail.txt",
            "post-binding SetupAPI evidence", errors)

    require(watchdog, "[ValidateRange(30,600)][int]$TimeoutSeconds",
            "bounded watchdog timeout", errors)
    require(watchdog, "if (-not $Execute)",
            "zero-write default watchdog mode", errors)
    require(watchdog, "$devices[0].ConfigManagerErrorCode -notin @(0,43)",
            "watchdog exact Code 0/43 rollback gate", errors)
    require(watchdog, "$markerLines[0].ToLowerInvariant() -ne $TargetInf.ToLowerInvariant()",
            "watchdog immutable OEM marker gate", errors)
    require(watchdog, "$manifest.PackageInfSha256",
            "watchdog INF hash gate", errors)
    require(watchdog, "$manifest.PackageSysSha256",
            "watchdog SYS hash gate", errors)
    require(watchdog, "-Execute -IAcceptDisplayInterruptionRisk",
            "watchdog invocation of bounded online rollback", errors)
    forbid(watchdog, "pnputil.exe /delete-driver *",
           "broad package deletion in watchdog", errors)
    require(confirm_display, "if (-not $IConfirmDisplayIsVisible)",
            "explicit visible-display confirmation", errors)
    require(confirm_display, "[uint32]$probe.SkipHwBringup -ne 1",
            "display-only probe gate before disarm", errors)
    require(confirm_display, "DmmIsSupportedResult",
            "display confirmation VidPN support gate", errors)
    require(confirm_display, "DmmCommitHasPath",
            "display confirmation committed-path gate", errors)
    require(confirm_display, "DmmPresentEntered",
            "display confirmation present-entry gate", errors)
    require(confirm_display, "DmmPresentStatus",
            "display confirmation present-success gate", errors)
    require(confirm_display, "DmmControlInterruptCount",
            "display confirmation interrupt-control gate", errors)
    require(confirm_display, "$vsyncDeadline = (Get-Date).AddSeconds(20)",
            "bounded wait for post-notify VSync proof", errors)
    require(confirm_display, "DmmVsyncCount",
            "display confirmation real VSync proof gate", errors)
    require(confirm_display, "Move-Item -LiteralPath $temporaryPath -Destination $finalPath",
            "atomic watchdog disarm publication", errors)

    require(arm_reboot, "[ValidateRange(180,600)][int]$TimeoutSeconds",
            "bounded post-reboot confirmation interval", errors)
    require(arm_reboot, "if (-not $Execute)",
            "zero-write default reboot guard mode", errors)
    require(arm_reboot, "if (-not $IAcceptPersistentRebootGuard)",
            "explicit persistent reboot guard authorization", errors)
    require(arm_reboot, "New-ScheduledTaskTrigger -AtStartup",
            "persistent startup trigger", errors)
    require(arm_reboot, "-UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest",
            "startup guard SYSTEM principal", errors)
    require(arm_reboot, "reboot_executed = $false",
            "arm evidence that reboot was not executed", errors)
    require(arm_reboot, "[string]$oldState.state -ne 'DISARMED'",
            "completed predecessor DISARMED gate", errors)
    require(arm_reboot, "Move-Item -LiteralPath $resolvedGuard -Destination $resolvedQuarantine",
            "recoverable completed guard quarantine", errors)
    for forbidden_reboot_command in (
        "Restart-Computer",
        "shutdown.exe",
        "shutdown /",
    ):
        forbid(arm_reboot, forbidden_reboot_command,
               "reboot command in arming phase", errors)
    require(watchdog_reboot,
            "$currentBootTicks -le [long]$config.armed_boot_ticks",
            "new-boot identity gate", errors)
    require(watchdog_reboot, "if (-not $Execute)",
            "zero-write default persistent watchdog mode", errors)
    require(watchdog_reboot, "Test-Path -LiteralPath $attemptPath",
            "single rollback attempt gate", errors)
    require(watchdog_reboot, "$config.rollback_sha256",
            "immutable rollback script hash gate", errors)
    require(watchdog_reboot, "$config.expected_inf_sha256",
            "post-reboot INF hash gate", errors)
    require(watchdog_reboot, "$config.expected_sys_sha256",
            "post-reboot SYS hash gate", errors)
    require(watchdog_reboot, "-Execute -IAcceptDisplayInterruptionRisk",
            "bounded exact online rollback invocation after timeout", errors)
    forbid(watchdog_reboot, "pnputil.exe /delete-driver *",
           "broad package deletion in persistent watchdog", errors)
    require(confirm_reboot, "if (-not $IConfirmDisplayIsVisible)",
            "explicit post-reboot visual confirmation", errors)
    require(confirm_reboot,
            "$currentBootTicks -le [long]$config.armed_boot_ticks",
            "post-reboot confirmation boot identity", errors)
    require(confirm_reboot, "[string]$state.state -ne 'OBSERVING'",
            "active watchdog state before confirmation", errors)
    require(confirm_reboot, "StartDeviceEntered = 1",
            "current driver start path evidence", errors)
    require(confirm_reboot, "DmmPresentStatus = 0",
            "post-reboot present success gate", errors)
    require(confirm_reboot, "$vsyncDeadline = (Get-Date).AddSeconds(20)",
            "bounded post-reboot VSync proof wait", errors)
    require(confirm_reboot, "DmmVsyncCount",
            "post-reboot real VSync proof gate", errors)
    require(confirm_reboot, "Get-WinEvent",
            "post-reboot warning/error gate", errors)

    require(display, "g_Bc250DisplayState.VsyncProofRecorded",
            "single-shot VSync proof state", errors)
    require(display, "vsyncCount > 0 && Bc250CanRecordDisplayProbePassive()",
            "positive VSync and PASSIVE_LEVEL persistence gate", errors)
    require(display, "&g_Bc250DisplayState.VsyncProofRecorded, 1, 0",
            "atomic single-shot VSync proof claim", errors)
    require(display, "BC250_PROBE_DMM_VSYNC_COUNT, (ULONG)vsyncCount",
            "persisted post-notify VSync count", errors)

    require(rollback, "$targetInf = $markerLines[0].ToLowerInvariant()",
            "marker-derived online rollback target", errors)
    forbid(rollback, "$targetInf = 'oem19.inf'",
           "historically pinned online rollback target", errors)
    require(rollback, "$preflightDevices[0].ConfigManagerErrorCode -notin @(0,43)",
            "Code 0/43 state gate before rollback", errors)
    require(rollback, "$installedDriverHash -ne $expectedDriverHash",
            "authorized installed SYS hash gate", errors)
    require(rollback, "$installedInfHash -ne [string]$manifest.PackageInfSha256",
            "authorized installed INF hash gate", errors)
    require(rollback, "$secondHash -ne $installedDriverHash",
            "second immutable-state gate before rollback", errors)
    require(rollback, "if (-not $Execute)",
            "zero-write default rollback mode", errors)
    require(rollback, "if (-not $IAcceptDisplayInterruptionRisk)",
            "explicit online rollback risk acceptance", errors)
    require(rollback, "pnputil.exe /delete-driver $targetInf /uninstall /force",
            "exact package uninstall operation", errors)
    require(rollback, "$driverHash -ne $installedDriverHash",
            "captured installed SYS residual hash gate", errors)
    require(rollback, "$manifest.SignerThumbprint",
            "authorized signer gate for installed SYS", errors)
    require(rollback, "audit-bc250mcdm-prebinding.ps1",
            "clean-host proof after rollback", errors)
    for forbidden_rollback_path in (
        "pnputil.exe /delete-driver *",
        "Remove-Item -Recurse",
        "sc.exe delete bc250v4",
        "sc.exe delete BasicDisplay",
    ):
        forbid(rollback, forbidden_rollback_path,
               "broad or protected online rollback target", errors)

    audit_frame_pointer_flag(errors)
    audit_batch_line_endings(errors)
    audit_gpuva_geometry_single_source(errors)
    audit_submit_path_labels(errors)
    audit_tripod_leg_one(errors)
    audit_indirect_only_tunable(errors)
    audit_ib_data_leg(errors)
    audit_process_gpuva_teardown(errors)
    audit_ring_wrap_arithmetic(errors)
    audit_node1_gate_telemetry(errors)
    audit_tdr_pair_has_backend(errors)
    audit_engine_status_measures(errors)
    audit_induced_hang_is_fail_closed(errors)
    audit_induced_hang_package_order(errors)
    audit_second_preflight_transport_empty(errors)
    audit_process_lifecycle_ddis_speak(errors)
    audit_context_abi_before_ready(errors)
    audit_compute_vmid_isolation(errors)
    audit_firmware_wgp_policy_is_readonly(errors)
    audit_mec_cursor_reconciliation(errors)
    audit_mec_vmid_retarget(errors)
    audit_submit_dispatch_level_safety(errors)
    audit_device_removal_and_unload_safety(errors)
    audit_tlb_invalidation_safety(errors)
    audit_mec_shared_mmio_lifetime(errors)

    audit_probe_field_names(errors)

    if errors:
        print("BC250 MCDM SAFETY AUDIT: FAILED")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("BC250 MCDM SAFETY AUDIT: PASS")
    print("  synthetic ownership/reservations: absent")
    print("  GPUVA geometry (GPUMMUCAPS vs UMDRIVERPRIVATE): single source")
    print("  operational DDIs without hardware backing: fail-closed")
    print("  'IB EXECUTADO' restricted to the INDIRECT path: enforced")
    print("  SDMA tripod leg 1 (RPTR==WPTR) inside the gate: enforced")
    print("  INDIRECT data leg (canary proof): enforced")
    print("  ring-wrap arithmetic on tripod leg 1: enforced")
    print("  Node 1 submission refusal names its reason: enforced")
    print("  TDR reset/restart pair backed by physical quiesce+proof: enforced")
    print("  QueryEngineStatus answers from measurement: enforced")
    print("  induced hang fail-closed and physical: enforced")
    print("  induced hang package/start order: enforced")
    print("  second readonly preflight sees an empty MMIO transport: enforced")
    print("  process/device/context DDIs name their refusals: enforced")
    print("  context ABI populated before ContextAbiReady: enforced")
    print("  24-CU firmware WGP/power policy is read-only: enforced")
    print("  drained MEC cursor reconciliation before planning: enforced")
    print("  per-process GPUVA ranges released on process destroy: enforced")
    print("  bc250mcdm recovery identity: covered")
    print("  submit path at DISPATCH_LEVEL pushlock-free: enforced")
    print("  GFXHUB TLB serialization with spinlock: enforced")
    print("  verdict: NAO VALIDADO EM HARDWARE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
