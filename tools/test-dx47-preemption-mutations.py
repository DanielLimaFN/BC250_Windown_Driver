#!/usr/bin/env python3
"""DX.47: each mutation of the preemption completion must make audit-bc250mcdm-safety.py FAIL.

Mutates the working tree in place and ALWAYS restores the original bytes.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
CB = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
MINI = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
DISP = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"

MUTATIONS = [
    (CB, "DDI nao delega ao backend",
     "    if (g_Bc250PreemptHandler != NULL) {", "    if (0) {"),
    (MINI, "notifica sem checar no drenado",
     "        if (inFlight != 0u || reserved != 0u) {\n            return TRUE; /* ainda nao drenou",
     "        if (0) {\n            return TRUE; /* ainda nao drenou"),
    (MINI, "ignora a fila do SDMA no Node 0",
     "            inFlight += g_Bc250SdmaPresent.InFlightCount;\n", ""),
    (MINI, "LastCompleted errado (fence do pedido)",
     "        data.DmaPreempted.LastCompletedFenceId = lastDone;",
     "        data.DmaPreempted.LastCompletedFenceId = sync->SubmissionFenceId;"),
    (MINI, "retorno antes da preempcao no caso ocioso",
     "        const BOOLEAN drained = (NT_SUCCESS(status) && notified && sync.Notified != 0u) ? TRUE : FALSE;\n",
     "        const BOOLEAN drained = (NT_SUCCESS(status) && notified && sync.Notified != 0u) ? TRUE : FALSE;\n        if (!drained) { InterlockedExchange(&Context->RetireBusy, 0); return; }\n"),
    (MINI, "handler notifica de dentro da DDI",
     "    InterlockedExchange(&s_Bc250PreemptPending[node], 1);\n",
     "    InterlockedExchange(&s_Bc250PreemptPending[node], 1);\n    active->Core.DxgkInterface.DxgkCbNotifyDpc(active->Core.DxgkInterface.DeviceHandle);\n"),
    (MINI, "handler nao instalado",
     "    Bc250McdmInstallPreemptHandler(Bc250ActivePreemptHandler);\n", ""),
    (MINI, "TDR nao limpa o Node 1",
     "        const LONG pending1 = InterlockedExchange(&s_Bc250PreemptPending[1], 0);",
     "        const LONG pending1 = 0;"),
    (DISP, "PREEMPT_DONE amostrado fora",
     'Bc250LogTextContains(Text, "PREEMPT_DONE") ||', ""),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.47 MUTATION TEST: FAILED -- arvore pristina nao passa")
        return 1
    failures = 0
    for path, label, old, new in MUTATIONS:
        original = path.read_bytes()
        text = original.decode("utf-8")
        target, repl = old, new
        if target not in text and "\r\n" in text:
            target, repl = old.replace("\n", "\r\n"), new.replace("\n", "\r\n")
        if target not in text:
            print(f"  [ERRO] alvo ausente: {label}")
            failures += 1
            continue
        try:
            path.write_bytes(text.replace(target, repl, 1).encode("utf-8"))
            ok = audit() != 0
        finally:
            path.write_bytes(original)
        print(f"  [{'REPROVOU' if ok else 'PASSOU (FALHA DO PORTAO)'}] {label}")
        failures += 0 if ok else 1
    if audit() != 0:
        print("DX.47 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.47 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
