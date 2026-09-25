"""Verify that the safe-bind audit rejects loss of the real UMD tail."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
WATCHER = ROOT / "tools" / "watch-bc250mcdm-safe-bind.ps1"
BINDER = ROOT / "tools" / "bind-bc250mcdm-lab.ps1"

MUTATIONS = [
    (BINDER, "snapshot sem rotulo intermediario",
     "kind = 'INTERMEDIATE_BIND_SNAPSHOT'", "kind = 'FINAL_UMD_TAIL'"),
    (WATCHER, "sem chamada de coleta apos recovery",
     "Archive-Bc250UmdLogsAfterRecovery\n\nif ($exitCode -eq 0)",
     "# coleta omitida\n\nif ($exitCode -eq 0)"),
    (WATCHER, "cauda sem hash",
     "sha256 = (Get-FileHash -LiteralPath $copyPath -Algorithm SHA256).Hash",
     "sha256 = '<nao calculado>'"),
    (WATCHER, "cauda sem ACL legivel",
     "& icacls.exe $destination /reset /T /Q | Out-Null",
     "# ACL nao restaurada"),
]


def audit() -> bool:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True).returncode == 0


def main() -> int:
    if not audit():
        print("DX.57: arvore inicial nao passou na auditoria")
        return 1
    failures = 0
    for path, name, old, new in MUTATIONS:
        original = path.read_bytes()
        source = original.decode("latin-1")
        if old not in source and "\r\n" in source:
            old, new = old.replace("\n", "\r\n"), new.replace("\n", "\r\n")
        if old not in source:
            print(f"DX.57 alvo ausente: {name}")
            failures += 1
            continue
        try:
            path.write_bytes(source.replace(old, new, 1).encode("latin-1"))
            caught = not audit()
        finally:
            path.write_bytes(original)
        print(f"DX.57 {name}: {'REPROVOU' if caught else 'FALHA DO PORTAO'}")
        if not caught:
            failures += 1
    if not audit():
        print("DX.57: restauracao nao passou na auditoria")
        return 1
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
