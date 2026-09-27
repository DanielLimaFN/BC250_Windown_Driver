"""Verify that the DX.56 safety gate rejects loss of durable paging evidence."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
CALLBACKS = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
ACTIVE = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"

MUTATIONS = [
    (CALLBACKS, "sem flush", "status = ZwFlushBuffersFile(g_Bc250PagingTraceFile, &ioStatus);",
     "status = STATUS_SUCCESS;"),
    (CALLBACKS, "sem write-through", "FILE_SYNCHRONOUS_IO_NONALERT | FILE_WRITE_THROUGH,",
     "FILE_SYNCHRONOUS_IO_NONALERT,"),
    (CALLBACKS, "sem checksum", "Record->Checksum = Bc250PagingTraceChecksum(Record);",
     "Record->Checksum = 0;"),
    (ACTIVE, "sem start", "status = Bc250PagingTraceStart();",
     "status = STATUS_SUCCESS;"),
    (ACTIVE, "sem teardown", "    Bc250PagingTraceStop();\n",
     "    (void)0;\n"),
    (CALLBACKS, "sem espera pelo fim da thread",
     "(void)ZwWaitForSingleObject(g_Bc250PagingTraceThreadHandle, FALSE, NULL);",
     "(void)STATUS_SUCCESS;"),
]


def audit() -> bool:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True).returncode == 0


def main() -> int:
    if not audit():
        print("DX.56: arvore inicial nao passou na auditoria")
        return 1
    failures = 0
    for path, name, old, new in MUTATIONS:
        original = path.read_bytes()
        source = original.decode("latin-1")
        if old not in source and "\r\n" in source:
            old, new = old.replace("\n", "\r\n"), new.replace("\n", "\r\n")
        if old not in source:
            print(f"DX.56 alvo ausente: {name}")
            failures += 1
            continue
        try:
            path.write_bytes(source.replace(old, new, 1).encode("latin-1"))
            caught = not audit()
        finally:
            path.write_bytes(original)
        print(f"DX.56 {name}: {'REPROVOU' if caught else 'FALHA DO PORTAO'}")
        if not caught:
            failures += 1
    if not audit():
        print("DX.56: restauracao nao passou na auditoria")
        return 1
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
