#!/usr/bin/env python3
"""DX.42: each mutation of the RADV preamble identity must fail audit-radv-wddm-safety.py.

Copies only radv_wddm2_cs.c into a temporary mirror of the Mesa tree (the other
audited files are copied unchanged), applies one mutation at a time and expects
the audit to FAIL. The pristine mirror must PASS first.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

AUDITED = [
    "src/amd/vulkan/winsys/wddm2/radv_wddm2_bo.c",
    "src/amd/vulkan/winsys/wddm2/radv_wddm2_cs.c",
    "src/amd/vulkan/winsys/wddm2/radv_wddm2_wsi.c",
    "src/amd/vulkan/winsys/wddm2/radv_wddm2_winsys.c",
    "src/amd/vulkan/radv_wsi.c",
    "src/amd/vulkan/radv_queue.c",
    "src/vulkan/runtime/vk_wddm2_monitored_fence.c",
]
CS = "src/amd/vulkan/winsys/wddm2/radv_wddm2_cs.c"

MUTATIONS = [
    ("sem serial no create",
     "   cs->bc250_serial = (uint64_t)InterlockedIncrement64(&s_bc250_cs_serial);\n", ""),
    ("finalize sem hash",
     "      cs->bc250_fin_fnv = bc250_fnv64_dwords(_cs->buf, cdw);", "      cs->bc250_fin_fnv = 0;"),
    ("winsys volta ao finalize comum",
     "ws->base.cs_finalize = radv_wddm2_cs_finalize;", "ws->base.cs_finalize = radv_winsys_cs_finalize;"),
    ("evento de identidade removido",
     "event=RADV_PREAMBLE_IDENTITY", "event=RADV_PREAMBLE_X"),
    ("identidade sem VA do BO",
     "ib_va=0x%llx bo_handle", "bo_handle"),
    ("identidade sem hash do finalize",
     "fin_fnv=%016llx src_fnv", "src_fnv"),
]


def run_audit(audit: Path, root: Path) -> int:
    return subprocess.run([sys.executable, str(audit), "--mesa-root", str(root)],
                          capture_output=True, text=True).returncode


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesa-root", type=Path, required=True)
    args = parser.parse_args()
    audit = Path(__file__).resolve().parent / "audit-radv-wddm-safety.py"
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        mirror = Path(tmp)
        for rel in AUDITED:
            dst = mirror / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.mesa_root / rel, dst)
        pristine = (args.mesa_root / CS).read_bytes().decode("utf-8")
        if run_audit(audit, mirror) != 0:
            print("DX.42 MUTATION TEST: FAILED -- mirror pristino nao passa")
            return 1
        for label, old, new in MUTATIONS:
            if old not in pristine:
                print(f"  [ERRO] alvo ausente: {label}")
                failures += 1
                continue
            (mirror / CS).write_bytes(pristine.replace(old, new, 1).encode("utf-8"))
            rc = run_audit(audit, mirror)
            ok = rc != 0
            print(f"  [{'REPROVOU' if ok else 'PASSOU (FALHA DO PORTAO)'}] {label}")
            failures += 0 if ok else 1
        (mirror / CS).write_bytes(pristine.encode("utf-8"))
    print(f"DX.42 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
