"""DX.66: cada mutacao das migalhas de MMIO tem de reprovar a auditoria de seguranca.

Restauracao por bytes com conferencia de SHA-256 por arquivo.
"""

from __future__ import annotations

import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
WDK = ROOT / "driver" / "wdk"
HEADER = WDK / "bc250_mmio_breadcrumb.h"
ACTIVE = WDK / "bc250_active_miniport.c"
TRANSPORT = WDK / "bc250_mmio_topology_transport.c"
SDMA = WDK / "bc250_sdma_present.c"
DISPLAY = WDK / "bc250_display_ddi.c"

MUTATIONS = [
    ("pagina fora da reserva", HEADER,
     b"#define BC250_CRUMB_VRAM_OFFSET   0x0FA00000ull",
     b"#define BC250_CRUMB_VRAM_OFFSET   0x0FD80000ull"),
    ("leitura do transporte sem migalha", TRANSPORT,
     b"ULONG crumb = Bc250CrumbBefore(BC250_CRUMB_SITE_TRANSPORT_READ, ByteOffset);",
     b"ULONG crumb = 0xFFFFFFFFu;"),
    ("leitura SDMA sem migalha", SDMA,
     b"crumb = Bc250CrumbBefore(BC250_CRUMB_SITE_SDMA_READ, RegOffset);",
     b"crumb = 0xFFFFFFFFu;"),
    ("leitura OTG sem migalha", DISPLAY,
     b"ULONG otgCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_OTG_READ, 0x14030u);",
     b"ULONG otgCrumb = 0xFFFFFFFFu;"),
    ("pagina write-combined", ACTIVE,
     b"page = (volatile ULONG *)MmMapIoSpaceEx(phys, BC250_CRUMB_PAGE_BYTES, PAGE_READWRITE | PAGE_NOCACHE);\r\n    if (page == NULL) {\r\n        Bc250DisplayLog(\"[DX66_MIGALHA] ARME",
     b"page = (volatile ULONG *)MmMapIoSpaceEx(phys, BC250_CRUMB_PAGE_BYTES, PAGE_READWRITE | PAGE_WRITECOMBINE);\r\n    if (page == NULL) {\r\n        Bc250DisplayLog(\"[DX66_MIGALHA] ARME"),
    ("sobreposicao ignorada", ACTIVE,
     b"if (region->InUse != 0u &&",
     b"if (FALSE &&"),
    ("arme sem exigir sucesso do StartDevice", ACTIVE,
     b"    if (NT_SUCCESS(status)) {\r\n        Bc250CrumbArm(",
     b"    {\r\n        Bc250CrumbArm("),
    ("leitura anterior removida", ACTIVE,
     b"    Bc250CrumbReportPrevious(DxgkInterface);\r\n",
     b"    /* sem leitura */\r\n"),
    ("StopDevice sem estado limpo", ACTIVE,
     b"    Bc250CrumbMarkClean();   /* DX.66 */",
     b"    /* sem estado limpo */"),
]


def audit() -> bool:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True).returncode == 0


def main() -> int:
    originals = {p: p.read_bytes() for p in {m[1] for m in MUTATIONS}}
    digests = {p: hashlib.sha256(b).hexdigest() for p, b in originals.items()}
    if not audit():
        print("DX.66: arvore inicial nao passou na auditoria")
        return 1
    failures = 0
    for name, path, good, bad in MUTATIONS:
        original = originals[path]
        if original.count(good) != 1:
            print(f"DX.66 {name}: alvo ausente ou ambiguo ({original.count(good)})")
            failures += 1
            continue
        try:
            path.write_bytes(original.replace(good, bad, 1))
            caught = not audit()
        finally:
            path.write_bytes(original)
        if hashlib.sha256(path.read_bytes()).hexdigest() != digests[path]:
            print(f"RESTAURACAO FALHOU: {path} mutado em disco")
            return 2
        print(f"DX.66 {name}: {'REPROVOU' if caught else 'FALHA DO PORTAO'}")
        failures += 0 if caught else 1
    if not audit():
        print("DX.66: auditoria nao voltou a passar apos restaurar")
        return 1
    print(f"DX.66: {len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
