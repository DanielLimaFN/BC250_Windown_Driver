#!/usr/bin/env python3
"""DX.49: each mutation of the desktop-throughput band must make audit-bc250mcdm-safety.py FAIL.

Covers the four parts: persistent upload staging, draw batching (ring, drains,
flush that closes the render pass), light trace sink and named dirty-rect
refusal. Mutates the working tree in place and ALWAYS restores the original
bytes. Files are handled as latin-1 so cp1252 sources round-trip byte for byte.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
UMD = ROOT / "driver" / "umd"
RES = UMD / "bc250_umd_resource_vk.c"
BRIDGE = UMD / "bc250_umd_vulkan_bridge.c"
DRAW = UMD / "bc250_d3d_umd.c"
DESC = UMD / "bc250_umd_descriptors.c"
CACHE = UMD / "bc250_umd_pipeline_cache.c"
BLT = UMD / "bc250_umd_blt.inl"
WINSYS = UMD / "bc250_wddm_winsys.c"
DISP = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"

MUTATIONS = [
    (RES, "staging volta a ser por chamada",
     "Bc250UmdVkEnsureUploadStaging(pBridge, (VkDeviceSize)ByteSize",
     "Bc250UmdVkEnsureUploadStagingX(pBridge, (VkDeviceSize)ByteSize"),
    (RES, "barreira do upload sem esperar o lote (TOP_OF_PIPE)",
     "pBridge->vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,",
     "pBridge->vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,"),
    (BRIDGE, "staging cresce sem drenar",
     'Bc250UmdVkDrainPending(pBridge, "crescer o staging persistente")',
     "VK_SUCCESS"),
    (DRAW, "flush incondicional antes do draw",
     "    if (!Bc250UmdPipelineCacheGetOrCreate(",
     "    (void)Bc250UmdVkFlushCommands(&pDevice->VkBridge);\n    if (!Bc250UmdPipelineCacheGetOrCreate("),
    (DRAW, "flush depois de todo draw",
     "if (flushNow) {", "if (TRUE) {"),
    (DRAW, "VB do lote fora do anel",
     "Bc250UmdVkDrawRingPut(&pDevice->VkBridge, vb->pCpuShadow",
     "Bc250UmdVkDrawRingPutX(&pDevice->VkBridge, vb->pCpuShadow"),
    (DRAW, "trace do draw afirma fence concluida",
     "DRAW GRAVADO NO LOTE; pixels", "DRAW fence concluida; pixels"),
    (DESC, "CB do lote fora do anel",
     "if (device->DrawUsesRing) {", "if (FALSE) {"),
    (BRIDGE, "flush nao fecha o render pass",
     "*pBridge->pRenderPassOpen = FALSE;", "(void)0;"),
    (BRIDGE, "anel volta ao inicio mesmo com fence falha",
     "pBridge->PendingDraws = 0;", "pBridge->PendingDraws = 0; pBridge->DrawRingHead = 0;"),
    (RES, "destruir recurso sem drenar",
     '(void)Bc250UmdVkDrainPending(pBridge, "destruir recurso");', ""),
    (CACHE, "despejar pipeline sem drenar",
     '(void)Bc250UmdVkDrainPending(pBridge, "despejar pipeline do cache");', ""),
    (BLT, "BltViaKmd sem drenar",
     'Bc250UmdVkDrainPending(&d->VkBridge, "composicao pelo KMD (BltViaKmd)")', "VK_SUCCESS"),
    (WINSYS, "trace sem write-through (DX.49b)",
     "FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);", "FILE_ATTRIBUTE_NORMAL, NULL);"),
    (WINSYS, "descarga de metadados sem teto de tempo (DX.49b)",
     "if (now - last < (LONG64)BC250_TRACE_FLUSH_MS) return;", ""),
    (WINSYS, "descarga periodica nunca chamada (DX.49b)",
     "    Bc250WinsysTraceFlushMetadataIfDue();\n}", "}"),
    (WINSYS, "trace com flush por linha",
     "(void)WriteFile(g_Bc250TraceSink.File[i], line, (DWORD)lineLength, &written, NULL);",
     "(void)WriteFile(g_Bc250TraceSink.File[i], line, (DWORD)lineLength, &written, NULL); FlushFileBuffers(g_Bc250TraceSink.File[i]);"),
    (DISP, "recusa de dirty rect sem motivo",
     "SubRectCnt=%u motivo=%s", "SubRectCnt=%u m=%s"),
    (DISP, "Src/Dst vazios voltam a ser recusados",
     "bothEmpty = Bc250DisplayRectIsEmpty(&Present->SrcRect) && Bc250DisplayRectIsEmpty(&Present->DstRect);",
     "bothEmpty = FALSE;"),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.49 MUTATION TEST: FAILED -- arvore pristina nao passa")
        return 1
    failures = 0
    for path, label, old, new in MUTATIONS:
        original = path.read_bytes()
        text = original.decode("latin-1")
        if old not in text and "\n" in old and "\r\n" in text:
            old, new = old.replace("\n", "\r\n"), new.replace("\n", "\r\n")
        if old not in text:
            print(f"  [ERRO] alvo ausente: {label}")
            failures += 1
            continue
        try:
            path.write_bytes(text.replace(old, new, 1).encode("latin-1"))
            ok = audit() != 0
        finally:
            path.write_bytes(original)
        print(f"  [{'REPROVOU' if ok else 'PASSOU (FALHA DO PORTAO)'}] {label}")
        failures += 0 if ok else 1
    if audit() != 0:
        print("DX.49 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.49 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
