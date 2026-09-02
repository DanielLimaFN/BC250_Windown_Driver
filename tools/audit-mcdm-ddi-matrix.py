#!/usr/bin/env python3
"""Audit the structural MCDM/GPUVA/WDDM callback matrix for the BC-250 KMD.

This gate proves only that required function pointers are populated and that
display-only/prohibited entries remain absent in the MCDM profile. It does not prove the semantic
implementation of any DDI or authorize StartDevice to succeed.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]

MINIMUM_MCDM = {
    "DxgkDdiAddDevice",
    "DxgkDdiBuildPagingBuffer",
    "DxgkDdiCalibrateGpuClock",
    "DxgkDdiCloseAllocation",
    "DxgkDdiCollectDbgInfo",
    "DxgkDdiCreateAllocation",
    "DxgkDdiCreateContext",
    "DxgkDdiCreateDevice",
    "DxgkDdiDescribeAllocation",
    "DxgkDdiDestroyAllocation",
    "DxgkDdiDestroyContext",
    "DxgkDdiDestroyDevice",
    "DxgkDdiDpcRoutine",
    "DxgkDdiFormatHistoryBuffer",
    "DxgkDdiGetNodeMetadata",
    "DxgkDdiGetStandardAllocationDriverData",
    "DxgkDdiInterruptRoutine",
    "DxgkDdiOpenAllocation",
    "DxgkDdiPreemptCommand",
    "DxgkDdiQueryAdapterInfo",
    "DxgkDdiQueryDependentEngineGroup",
    "DxgkDdiQueryDeviceDescriptor",
    "DxgkDdiQueryEngineStatus",
    "DxgkDdiRemoveDevice",
    "DxgkDdiResetDevice",
    "DxgkDdiResetEngine",
    "DxgkDdiResetFromTimeout",
    "DxgkDdiRestartFromTimeout",
    "DxgkDdiSetPowerState",
    "DxgkDdiSetStablePowerState",
    "DxgkDdiSetVirtualMachineData",
    "DxgkDdiStartDevice",
    "DxgkDdiStopDevice",
    "DxgkDdiUnload",
    "DxgkDdiQueryChildStatus",
    "DxgkDdiQueryChildRelations",
    "DxgkDdiQueryConnectionChange",
}

# MCDM requires virtual addressing. These five callbacks are the WDDM GPUVA
# extension to the minimum table.
GPUVA_REQUIRED = {
    "DxgkDdiCreateProcess",
    "DxgkDdiDestroyProcess",
    "DxgkDdiGetRootPageTableSize",
    "DxgkDdiSetRootPageTable",
    "DxgkDdiSubmitCommandVirtual",
}

LOCAL_COMPATIBILITY = {"DxgkDdiDispatchIoRequest"}

# Prohibited in pure MCDM (readonly) image
PROHIBITED = {
    "DxgkDdiAcquireSwizzlingRange",
    "DxgkDdiCheckMultiPlaneOverlaySupport",
    "DxgkDdiCheckMultiPlaneOverlaySupport2",
    "DxgkDdiCheckMultiPlaneOverlaySupport3",
    "DxgkDdiCommitVidPn",
    "DxgkDdiControlInterrupt",
    "DxgkDdiControlInterrupt2",
    "DxgkDdiCreateOverlay",
    "DxgkDdiDestroyOverlay",
    "DxgkDdiDisplayDetectControl",
    "DxgkDdiEnumVidPnCofuncModality",
    "DxgkDdiFlipOverlay",
    "DxgkDdiGetScanLine",
    "DxgkDdiIsSupportedVidPn",
    "DxgkDdiPresent",
    "DxgkDdiQueryCurrentFence",
    "DxgkDdiSetVidPnSourceAddress",
    "DxgkDdiSetVidPnSourceVisibility",
}

WDDM_RENDER_NODE_REQUIRED = {
    "DxgkDdiAcquireSwizzlingRange",
    "DxgkDdiReleaseSwizzlingRange",
    "DxgkDdiPatch",
    "DxgkDdiRender",
    "DxgkDdiSubmitCommand",
    "DxgkDdiQueryCurrentFence",
    "DxgkDdiEscape",
}

WDDM_DISPLAY_REQUIRED = {
    "DxgkDdiCommitVidPn",
    "DxgkDdiControlInterrupt",
    "DxgkDdiControlInterrupt2",
    "DxgkDdiIsSupportedVidPn",
    "DxgkDdiEnumVidPnCofuncModality",
    "DxgkDdiSetVidPnSourceAddress",
    "DxgkDdiSetVidPnSourceVisibility",
    "DxgkDdiPresent",
    "DxgkDdiGetScanLine",
    "DxgkDdiQueryInterface",
}

# DDIs OPCIONAIS que este driver deliberadamente NAO anuncia, com o motivo.
#
# Estar aqui e' uma afirmacao mais forte do que simplesmente sair da lista de
# obrigatorias: a auditoria REPROVA se alguma delas voltar a ser anunciada sem
# que a justificativa correspondente seja removida daqui.  Foi assim que o
# 3.74 conseguiu embarcar um STATUS_SUCCESS vazio sem ninguem notar.
WDDM_DELIBERATELY_ABSENT = {}

# 3.76: as duas DDIs abaixo VOLTARAM a ser anunciadas de proposito, para
# congelar a tabela KMDOD na baseline conhecida e deixar o perfil
# RenderOnlyLab como unico eixo novo do candidato. A retirada definitiva
# acontece em candidato proprio, depois de fechado o Gate 1.5.
# Ver ACCEPTED_DEBT em tools/audit-ddi-noop-callbacks.py.
WDDM_RESTORED_AS_DEBT = {
    "DxgkDdiSetDisplayPrivateDriverFormat",
    "DxgkDdiSetPalette",
}


def main() -> int:
    wdk = ROOT / "driver" / "wdk"
    mcdm_only = (wdk / "bc250_mcdm_callbacks.c",)
    wddm_full = (
        wdk / "bc250_active_miniport.c",
        wdk / "bc250_mcdm_callbacks.c",
        wdk / "bc250_display_ddi.c",
    )

    failures = []

    # Perfil MCDM (readonly)
    mcdm_text = "\n".join(p.read_text(encoding="utf-8") for p in mcdm_only)
    render_fn = mcdm_text.find("Bc250McdmPopulateRenderCallbacks(")
    if render_fn != -1:
        end = mcdm_text.find("\n}", render_fn)
        mcdm_text = mcdm_text[:render_fn] + mcdm_text[end:]
    mcdm_assigned = set(
        re.findall(r"InitializationData->(DxgkDdi[A-Za-z0-9_]+)\s*=", mcdm_text))
    mcdm_forbidden = sorted(PROHIBITED & mcdm_assigned)

    # Perfil WDDM (ativo)
    wddm_text = "\n".join(p.read_text(encoding="utf-8") for p in wddm_full)
    wddm_assigned = set(
        re.findall(r"InitializationData->(DxgkDdi[A-Za-z0-9_]+)\s*=", wddm_text))

    wddm_req = MINIMUM_MCDM | GPUVA_REQUIRED | LOCAL_COMPATIBILITY | WDDM_RENDER_NODE_REQUIRED | WDDM_DISPLAY_REQUIRED
    wddm_missing = sorted(wddm_req - wddm_assigned)

    print("BC250 DDI MATRIX")
    print("  perfil MCDM  (imagem readonly, compute-only)")
    print(f"    prohibited respeitadas: {len(PROHIBITED) - len(mcdm_forbidden)}/{len(PROHIBITED)}")
    if mcdm_forbidden:
        for name in mcdm_forbidden:
            print(f"    - prohibited DDI in MCDM: {name}")
        failures.append("perfil MCDM possui callbacks proibidos")

    print("  perfil WDDM  (miniport ativo, display+render)")
    print(f"    minimum MCDM:      {len(MINIMUM_MCDM & wddm_assigned)}/{len(MINIMUM_MCDM)}")
    print(f"    GPUVA extension:   {len(GPUVA_REQUIRED & wddm_assigned)}/{len(GPUVA_REQUIRED)}")
    print(f"    local compat:      {len(LOCAL_COMPATIBILITY & wddm_assigned)}/{len(LOCAL_COMPATIBILITY)}")
    print(f"    render node:       {len(WDDM_RENDER_NODE_REQUIRED & wddm_assigned)}/{len(WDDM_RENDER_NODE_REQUIRED)}")
    print(f"    display:           {len(WDDM_DISPLAY_REQUIRED & wddm_assigned)}/{len(WDDM_DISPLAY_REQUIRED)}")

    debt_missing = sorted(WDDM_RESTORED_AS_DEBT - wddm_assigned)
    print(f"    restauradas como divida (baseline KMDOD): "
          f"{len(WDDM_RESTORED_AS_DEBT) - len(debt_missing)}"
          f"/{len(WDDM_RESTORED_AS_DEBT)}")
    if debt_missing:
        for name in debt_missing:
            print(f"    - divida ausente: {name}")
        print("      O 3.76 congela a tabela KMDOD na baseline para isolar o "
              "Gate 1.5. Se a limpeza ja' foi feita em candidato proprio, "
              "remova o nome de WDDM_RESTORED_AS_DEBT.")
        failures.append("tabela KMDOD divergiu da baseline congelada")

    wddm_unexpected = sorted(set(WDDM_DELIBERATELY_ABSENT) & wddm_assigned)
    print(f"    opcionais ausentes por decisao: "
          f"{len(WDDM_DELIBERATELY_ABSENT) - len(wddm_unexpected)}"
          f"/{len(WDDM_DELIBERATELY_ABSENT)}")
    print("  semantic/hardware validation: NOT PROVEN")

    if wddm_missing:
        for name in wddm_missing:
            print(f"    - missing WDDM DDI: {name}")
        failures.append("perfil WDDM incompleto")

    if wddm_unexpected:
        for name in wddm_unexpected:
            print(f"    - DDI opcional reanunciada: {name}")
            print(f"      motivo registrado para a ausencia: "
                  f"{WDDM_DELIBERATELY_ABSENT[name]}")
            print("      se ela agora possui contrato real, remova a entrada "
                  "de WDDM_DELIBERATELY_ABSENT nesta auditoria.")
        failures.append("DDI opcional anunciada sem retirar a justificativa")

    if failures:
        print("BC250 DDI MATRIX: FAILED")
        return 1

    print("BC250 DDI MATRIX: PASS (STRUCTURAL ONLY)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
