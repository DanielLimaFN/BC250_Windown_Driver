# Rebuild the Windows icon from the canonical SVG, without changing application code.
import subprocess
from pathlib import Path
subprocess.run(['node','tools/render-icon.cjs'],cwd=Path(__file__).resolve().parent,check=True)
