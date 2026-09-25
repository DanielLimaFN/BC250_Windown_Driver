"""DX.61: cada mutacao do guardiao tem de reprovar a auditoria de seguranca.

Restauracao por bytes com conferencia de SHA-256: na suite DX.49 uma falha de
restauracao deixou um fonte mutado e so' a auditoria seguinte percebeu.
"""

from __future__ import annotations

import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
WATCH = ROOT / "tools" / "watch-bc250mcdm-safe-bind.ps1"

MUTATIONS = [
    ("journal de amostras removido",
     b"[IO.File]::AppendAllText($samplesJournalPath,",
     b"[void]($samplesJournalPath) -and ("),
    ("criterio afrouxado para 4 amostras",
     b"$healthy = ($samples.Count -eq 5)",
     b"$healthy = ($samples.Count -ge 4)"),
    ("codigo diferente de zero aceito",
     b"$s.service -ne 'BasicDisplay' -or $s.code -ne 0",
     b"$s.service -ne 'BasicDisplay' -or $s.code -gt 99"),
    ("copia por tentativa da recovery removida",
     b'"safe-bind-recovery-attempt{0}.txt"',
     b'"safe-bind-recovery.txt"'),
    ("janela de assentamento removida",
     b"$settleDeadlineUtc = $recoveryFinishedUtc.AddSeconds(45)",
     b"$settleDeadlineUtc = $recoveryFinishedUtc"),
    ("rodada saudavel nao encerra",
     b"if ($afterVerdict.healthy) { break }",
     b"if ($afterVerdict.healthy) { }"),
    ("BLOCKED volta a sobrescrever state.json",
     b"limite de 2 boots' 11 -JournalOnly",
     b"limite de 2 boots' 11"),
]


def audit() -> bool:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True).returncode == 0


def main() -> int:
    original = WATCH.read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    if not audit():
        print("DX.61: arvore inicial nao passou na auditoria")
        return 1
    failures = 0
    for name, good, bad in MUTATIONS:
        if original.count(good) != 1:
            print(f"DX.61 {name}: alvo ausente ou ambiguo ({original.count(good)})")
            failures += 1
            continue
        try:
            WATCH.write_bytes(original.replace(good, bad, 1))
            caught = not audit()
        finally:
            WATCH.write_bytes(original)
        if hashlib.sha256(WATCH.read_bytes()).hexdigest() != digest:
            print("DX.61: RESTAURACAO FALHOU -- guardiao mutado em disco")
            return 2
        print(f"DX.61 {name}: {'REPROVOU' if caught else 'FALHA DO PORTAO'}")
        failures += 0 if caught else 1
    if not audit():
        print("DX.61: auditoria nao voltou a passar apos restaurar")
        return 1
    print(f"DX.61: {len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
