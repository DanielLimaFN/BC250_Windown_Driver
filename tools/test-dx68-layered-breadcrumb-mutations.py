"""DX.68: each removed outer layer must fail the safety audit; restore exact bytes."""

import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WDK = ROOT / "driver" / "wdk"
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
CASES = [
    ("bank count", WDK / "bc250_mmio_breadcrumb.h",
     b"#define BC250_CRUMB_BANKS         3u",
     b"#define BC250_CRUMB_BANKS         1u"),
    ("paging scope", WDK / "bc250_mcdm_callbacks.c",
     b"BC250_CRUMB_SITE_PAGING_DDI, operation, BC250_CRUMB_BANK_PAGING",
     b"BC250_CRUMB_SITE_PAGING_DDI, operation, BC250_CRUMB_BANK_INNER"),
    ("submit scope", WDK / "bc250_active_miniport.c",
     b"BC250_CRUMB_SITE_SUBMIT_DDI,",
     b"BC250_CRUMB_SITE_TRANSPORT_READ,"),
    ("report layer", WDK / "bc250_active_miniport.c",
     b"best % BC250_CRUMB_CPUS,",
     b"best,")
]


def passes() -> bool:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True).returncode == 0


def main() -> int:
    if not passes():
        print("DX.68: auditoria inicial falhou")
        return 1
    failures = 0
    for label, path, old, new in CASES:
        original = path.read_bytes()
        digest = hashlib.sha256(original).digest()
        if original.count(old) != 1:
            print(f"DX.68 {label}: alvo ambiguo ou ausente")
            failures += 1
            continue
        try:
            path.write_bytes(original.replace(old, new, 1))
            caught = not passes()
        finally:
            path.write_bytes(original)
        if hashlib.sha256(path.read_bytes()).digest() != digest:
            print(f"DX.68 {label}: restauracao falhou")
            return 2
        print(f"DX.68 {label}: {'REPROVOU' if caught else 'FALHA DO PORTAO'}")
        failures += 0 if caught else 1
    print(f"DX.68: {len(CASES) - failures}/{len(CASES)} mutacoes reprovadas")
    return 0 if failures == 0 and passes() else 1


if __name__ == "__main__":
    raise SystemExit(main())
