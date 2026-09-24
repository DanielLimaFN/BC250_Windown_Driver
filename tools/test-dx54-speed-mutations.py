#!/usr/bin/env python3
"""DX.54: each mutation must make audit-bc250mcdm-safety.py FAIL.

Desktop speed: persistent descriptor pool, sampler cache, 4 MB ring behind a
draw threshold, flush timings and sampled Present inspection. Mutates in place
and ALWAYS restores the original bytes.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
DRAW = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
DESC = ROOT / "driver" / "umd" / "bc250_umd_descriptors.c"
BRIDGE = ROOT / "driver" / "umd" / "bc250_umd_vulkan_bridge.c"
HEADER = ROOT / "driver" / "umd" / "bc250_umd_vulkan_bridge.h"

MUTATIONS = [
    (DRAW, "DX.55: lote religado",
     "static const BOOL s_Bc250Dx51DrawBatchEnabled = FALSE;", "static const BOOL s_Bc250Dx51DrawBatchEnabled = TRUE;"),
    (DRAW, "anel nasce em qualquer device",
     "pDevice->NativeVkDrawCount >= BC250_DX54_RING_MIN_DIRECT_DRAWS;", "TRUE;"),
    (HEADER, "anel volta a 16 MB",
     "#define BC250_DX49_DRAW_RING_BYTES (4u * 1024u * 1024u)", "#define BC250_DX49_DRAW_RING_BYTES (16u * 1024u * 1024u)"),
    (DESC, "pool persistente nunca usado",
     "set = Bc250UmdVkAllocPersistentSet(bridge, entry->DescriptorSetLayout);", "set = VK_NULL_HANDLE;"),
    (DESC, "fallback mudo",
     "bridge->DescriptorFallbackPools++;", "(void)0;"),
    (BRIDGE, "pool zerado sem fence",
     "        Bc250UmdVkReleaseDescriptorBatches(pBridge);\n        /* DX.54: conjuntos do pool persistente so' voltam com a fence concluida. */\n        if (pBridge->PersistentPool != VK_NULL_HANDLE && pBridge->PersistentPoolSets != 0u) {\n            if (pBridge->vk.vkResetDescriptorPool(pBridge->Device, pBridge->PersistentPool, 0) == VK_SUCCESS) {",
     "        Bc250UmdVkReleaseDescriptorBatches(pBridge);\n        /* DX.54: conjuntos do pool persistente so' voltam com a fence concluida. */\n        if (pBridge->PersistentPool != VK_NULL_HANDLE && pBridge->PersistentPoolSets != 0u) {\n            if (VK_SUCCESS == VK_SUCCESS) {"),
    (DESC, "sampler do cache entra no lote",
     "images[i].sampler = cached;", "images[i].sampler = batch->Samplers[i] = cached;"),
    (BRIDGE, "flush sem tempo de submit",
     "[DX54_TEMPO] FimCbUs=%llu SubmitUs=%llu", "[DX54_TEMPO] FimCbUs=%llu X=%llu"),
    (DRAW, "Present fora da amostra inventa valor",
     "(%ux%u, NonZeroGrade=NAO_AMOSTRADO)", "(%ux%u, NonZeroGrade=0)"),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.54 MUTATION TEST: FAILED -- arvore pristina nao passa")
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
        print("DX.54 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.54 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
