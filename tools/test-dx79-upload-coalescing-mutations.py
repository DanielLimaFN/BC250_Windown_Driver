#!/usr/bin/env python3
"""DX.79: each mutation of texture upload coalescing must make audit-bc250mcdm-safety.py FAIL.

Covers: staging ring head in header, 256B alignment, bufferOffset in VkBufferImageCopy,
cursor advance, conditional flush on wrap, and reset on flush/shutdown.
Mutates the working tree in place and ALWAYS restores the original bytes.
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
HEADER = UMD / "bc250_umd_vulkan_bridge.h"

MUTATIONS = [
    (HEADER, "cabeca do anel removida do header",
     "VkDeviceSize UploadStagingHead;", "VkDeviceSize UploadStagingHeadX;"),
    (HEADER, "alinhamento do anel removido",
     "#define BC250_DX79_UPLOAD_ALIGN 256u", "#define BC250_DX79_UPLOAD_ALIGN_X 256u"),
    (RES, "alinhamento do anel nao utilizado no upload",
     "(BC250_DX79_UPLOAD_ALIGN - 1u)) &\n                      ~(VkDeviceSize)(BC250_DX79_UPLOAD_ALIGN - 1u);",
     "(0u)) &\n                      ~(VkDeviceSize)(0u);"),
    (RES, "bufferOffset volta a ser fixo em 0",
     "region.bufferOffset = uploadOffset;", "region.bufferOffset = 0;"),
    (RES, "cursor do anel de upload nao e avancado",
     "pBridge->UploadStagingHead = uploadOffset + (VkDeviceSize)ByteSize;", "(void)0;"),
    (RES, "flush incondicional volta ao upload",
     "if (wrapNeeded) {", "if (TRUE) {"),
    (BRIDGE, "UploadStagingHead nao zerada no flush com fence",
     "pBridge->UploadStagingHead = 0;\n        pBridge->BatchFlushCount++;",
     "(void)0;\n        pBridge->BatchFlushCount++;"),
    (BRIDGE, "UploadStagingHead nao zerada no shutdown",
     "pBridge->UploadStagingHead = 0;\n        if (pBridge->LinearPresentBuf",
     "(void)0;\n        if (pBridge->LinearPresentBuf"),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.79 MUTATION TEST: FAILED -- arvore pristina nao passa")
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
        if not ok:
            failures += 1
    if failures:
        print(f"DX.79 MUTATION TEST: FAILED ({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
        return 1
    print(f"DX.79 MUTATION TEST: PASS ({len(MUTATIONS)}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
