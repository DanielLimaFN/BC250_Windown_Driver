#!/usr/bin/env python3
"""Fail the offline gate when B7 regresses to cosmetic Node 1 progress.

This audit deliberately checks semantics that the generic callback-count tests
cannot see: Draw must reject while no shader/draw backend exists, a command
buffer must be uploaded to its real WDDM allocation, and submission must be
routed through a context created for the requested node.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
UMD = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
WINSYS = ROOT / "driver" / "umd" / "bc250_wddm_winsys.c"
HEADER = ROOT / "driver" / "umd" / "bc250_d3d_umd.h"
POLICY = ROOT / "driver" / "umd" / "bc250_umd_caps_probe.c"
HARNESS = ROOT / "tools" / "bc250-umd-node1-tripod.c"


def body(text: str, function: str) -> str:
    match = re.search(r"\b" + re.escape(function) + r"\s*\([^;]*?\)\s*\{", text, re.S)
    if not match:
        raise ValueError(f"funcao ausente: {function}")
    start = text.find("{", match.start())
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:index]
    raise ValueError(f"corpo truncado: {function}")


def evaluate(umd: str, winsys: str, header: str, policy: str, harness: str) -> list[str]:
    failures: list[str] = []

    if not re.search(r"#define\s+BC250_D3D10_11_NATIVE_READY\s+0\b", header):
        failures.append("gate BC250_D3D10_11_NATIVE_READY nao esta fail-closed em 0")

    try:
        authorization = body(policy, "Bc250UmdNode1TripodAuthorize")
        for token in ("Armed", "BC250_NODE1_TRIPOD_HARNESS_EXE",
                      "RegistryToken", "EnvironmentToken", "_stricmp"):
            if token not in authorization:
                failures.append(f"autorizacao do tripe sem {token}")
    except ValueError as error:
        failures.append(str(error))

    for token in ("BC250_UMD_NODE1_TRIPOD_TOKEN", "D3D10CreateDevice1",
                  "CreateDevice retornou sucesso", "NODE1_TRIPOD_RESULT",
                  "Poison=1 Submitted=1 Fence=1 Data=1 Cleanup=1"):
        if token not in harness:
            failures.append(f"harness fisico sem {token}")

    try:
        create_device = body(umd, "Bc250Umd10CreateDevice")
        for token in ("Bc250UmdNode1TripodAuthorized",
                      "InterlockedCompareExchange(&g_Bc250UmdProofTriggered",
                      "Bc250UmdRunNode1ProofTransaction", "return E_NOTIMPL"):
            if token not in create_device:
                failures.append(f"CreateDevice do modo de prova sem {token}")
    except ValueError as error:
        failures.append(str(error))

    for name in (
        "Bc250Umd10Draw",
        "Bc250Umd10DrawIndexed",
        "Bc250Umd10DrawInstanced",
        "Bc250Umd10DrawIndexedInstanced",
        "Bc250Umd10DrawAuto",
    ):
        try:
            draw = body(umd, name)
        except ValueError as error:
            failures.append(str(error))
            continue
        if "Bc250UmdRejectUnbackedDraw" not in draw:
            failures.append(f"{name} nao rejeita explicitamente o draw sem backend")
        if "DrawCount++" in draw or "TotalVertexCount +=" in draw or "TotalIndexCount +=" in draw:
            failures.append(f"{name} ainda fabrica progresso por contador CPU")

    required_flush = (
        "Bc250WinsysWaitPaging",
        "Bc250WinsysUploadAllocation",
        "Bc250WinsysSubmitGpuVaForNode",
    )
    try:
        flush = body(umd, "Bc250UmdCommandBufferFlush")
        for token in required_flush:
            if token not in flush:
                failures.append(f"CommandBufferFlush sem {token}")
        if "UNREFERENCED_PARAMETER(NodeOrdinal)" in flush:
            failures.append("CommandBufferFlush ignora NodeOrdinal")
        if flush.find("pCmd->CursorDwords = 0") < flush.find("wsStatus != BC250_WINSYS_OK"):
            failures.append("CommandBufferFlush descarta o payload antes de confirmar submit")
    except ValueError as error:
        failures.append(str(error))

    try:
        init = body(umd, "Bc250UmdCommandBufferInit")
        for token in ("Bc250UmdNextCommandToken", "Bc250WinsysMapAllocation", "Bc250WinsysMakeResident"):
            if token not in init:
                failures.append(f"CommandBufferInit sem {token}")
        if re.search(r"Bc250WinsysAllocate\s*\([^;]*?,\s*0\s*,", init, re.S):
            failures.append("CommandBufferInit voltou a usar AllocationToken zero")
    except ValueError as error:
        failures.append(str(error))

    try:
        proof = body(umd, "Bc250UmdBuildNode1ProofCommand")
        for token in ("Bc250Pm4EmitWriteData", "Bc250Pm4EmitReleaseMemEop32"):
            if token not in proof:
                failures.append(f"payload de prova Node 1 sem {token}")
    except ValueError as error:
        failures.append(str(error))

    try:
        routed = body(winsys, "Bc250WinsysSubmitGpuVaForNode")
        if "Context->NodeOrdinal != ExpectedNodeOrdinal" not in routed:
            failures.append("submit rotulado por Node nao confere o contexto real")
    except ValueError as error:
        failures.append(str(error))

    try:
        upload = body(winsys, "Bc250WinsysUploadAllocation")
        for token in ("pfnLockCb", "memcpy", "pfnUnlockCb"):
            if token not in upload:
                failures.append(f"upload da alocacao WDDM sem {token}")
    except ValueError as error:
        failures.append(str(error))

    try:
        readback = body(winsys, "Bc250WinsysReadAllocation")
        for token in ("pfnLockCb", "memcpy", "pfnUnlockCb"):
            if token not in readback:
                failures.append(f"readback da alocacao WDDM sem {token}")
    except ValueError as error:
        failures.append(str(error))

    try:
        transaction = body(umd, "Bc250UmdRunNode1ProofTransaction")
        required_transaction = (
            "Bc250WinsysAllocate",
            "Bc250WinsysMapAllocation",
            "Bc250WinsysMakeResident",
            "Bc250WinsysUploadAllocation",
            "PoisonVerifiedBeforeSubmit",
            "Bc250WinsysCreateMonitoredFence",
            "Bc250UmdSubmitNode1Proof",
            "Bc250WinsysWaitForFenceFromCpu",
            "FenceObserved == BC250_UMD_NODE1_PROOF_FENCE_VALUE",
            "Bc250WinsysReadAllocation",
            "memcmp(pResult->Observed, pResult->Expected",
            "Bc250WinsysDestroyMonitoredFence",
            "Bc250WinsysDeallocate",
            "CleanupComplete",
        )
        for token in required_transaction:
            if token not in transaction:
                failures.append(f"transacao do tripe Node 1 sem {token}")
        if "pResult->OperationHr = S_OK" not in transaction:
            failures.append("transacao do tripe nunca publica sucesso completo")
    except ValueError as error:
        failures.append(str(error))

    return failures


def main() -> int:
    umd = UMD.read_text(encoding="utf-8", errors="replace")
    winsys = WINSYS.read_text(encoding="utf-8", errors="replace")
    header = HEADER.read_text(encoding="utf-8", errors="replace")
    policy = POLICY.read_text(encoding="utf-8", errors="replace")
    harness = HARNESS.read_text(encoding="utf-8", errors="replace")
    failures = evaluate(umd, winsys, header, policy, harness)

    # Mutation proof: removing the real upload must make the same gate fail.
    mutated = umd.replace("Bc250WinsysUploadAllocation(", "Bc250WinsysUpload_REMOVED(", 1)
    mutation_failures = evaluate(mutated, winsys, header, policy, harness)
    if not any("CommandBufferFlush sem Bc250WinsysUploadAllocation" in item
               for item in mutation_failures):
        failures.append("autoteste de mutacao nao detectou remocao do upload")

    mutated_tripod = umd.replace(
        "memcmp(pResult->Observed, pResult->Expected",
        "memcmp_PROOF_REMOVED(pResult->Observed, pResult->Expected", 1)
    tripod_mutation_failures = evaluate(mutated_tripod, winsys, header, policy, harness)
    if not any("transacao do tripe Node 1 sem memcmp" in item
               for item in tripod_mutation_failures):
        failures.append("autoteste de mutacao nao detectou remocao da prova de dados")

    if failures:
        for failure in failures:
            print(f"FALHA: {failure}")
        print("UMD NODE1 SUBMISSION AUDIT: REPROVADO")
        return 1

    print("UMD NODE1 SUBMISSION AUDIT: PASS")
    print("  Draw sem backend: recusa explicita; zero progresso fabricado")
    print("  Command buffer: token nao nulo + map + residency + Lock/Unlock upload")
    print("  Submit: contexto confere NodeOrdinal; payload WRITE_DATA + RELEASE_MEM")
    print("  Tripe: poison pre-submit + fence exata + readback byte a byte + teardown")
    print("  Acionador: registro + executavel exato + token 128-bit; device segue fail-closed")
    print("  Mutacoes sem upload/prova de dados: detectadas")
    print("  Veredito fisico: NAO VALIDADO EM HARDWARE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
