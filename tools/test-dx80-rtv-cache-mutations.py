#!/usr/bin/env python3
"""DX.80: each mutation of RTV framebuffer cache must make audit-bc250mcdm-safety.py FAIL.

Covers: cache size definition, cache array in RTV struct, cache initialization at creation,
cache comparison on follow, hit counter, cache telemetry, and teardown destruction of framebuffers/views.
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
HEADER = UMD / "bc250_d3d_umd.h"

MUTATIONS = [
    (HEADER, "tamanho maximo do cache removido",
     "#define BC250_RTV_MAX_CACHED_FBS 8u", "#define BC250_RTV_MAX_CACHED_FBS_X 8u"),
    (HEADER, "array de cache removido da struct RTV",
     "} FbCache[BC250_RTV_MAX_CACHED_FBS];", "} FbCacheX[BC250_RTV_MAX_CACHED_FBS];"),
    (RES, "cache nao inicializado no CreateRtv",
     "pRtv->FbCacheCount = 1u;", "(void)0;"),
    (RES, "comparacao de imagem do cache corrompida",
     "pRtv->FbCache[i].Image == pRes->VkImg", "pRtv->FbCache[i].Image == VK_NULL_HANDLE"),
    (RES, "contador de cache hits removido",
     "pRtv->FollowHitCount++;", "(void)0;"),
    (RES, "telemetria de cache removida",
     "[DX80_CACHE]", "[DX80_CACHE_X]"),
    (RES, "destruicao de framebuffers em cache removida do DestroyRtv",
     "pBridge->vk.vkDestroyFramebuffer(pBridge->Device, pRtv->FbCache[i].Fb, NULL);", "(void)0;"),
    (RES, "destruicao de views em cache removida do DestroyRtv",
     "pBridge->vk.vkDestroyImageView(pBridge->Device, pRtv->FbCache[i].View, NULL);", "(void)0;"),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.80 MUTATION TEST: FAILED -- arvore pristina nao passa")
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
        print("DX.80 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.80 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
