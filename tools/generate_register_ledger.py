"""Generate a provenance ledger of register-like constants in old BC-250 sources.

This is intentionally broader than the curated register catalog.  Its output is
an archaeological index, not an endorsement of any address.
"""

from __future__ import annotations

import re
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs" / "05_INVENTARIO_BRUTO_DE_ENDERECOS.md"

SOURCE_ROOTS = [
    ROOT / "dbg",
    ROOT / "referencias" / "wddm",
    ROOT / "referencias" / "BC250-V2",
    ROOT / "referencias" / "BC250-V3",
    ROOT / "referencias" / "AMD-BC-250-Windows-Driver" / "inc",
    ROOT / "referencias" / "AMD-BC-250-Windows-Driver" / "src" / "kmd",
    ROOT / "referencias" / "AMD-BC-250-Windows-Driver" / "src" / "amdbc250_reg",
    ROOT / "referencias" / "AMD-BC-250-Windows-Driver" / "test-tools",
]

SOURCE_SUFFIXES = {".c", ".h", ".cxx", ".hxx"}
SKIP_NAMES = {"wddm_fw.h", "firmware_data.h"}
SKIP_PARTS = {"build", "bin", "package", "output", "opencl"}

DEFINE_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+?)\s*$")
HEX_RE = re.compile(r"0[xX][0-9A-Fa-f]+")
DIRECT_CALL_RE = re.compile(
    r"\b(?:BcReg(?:Read|Write)Raw|BcReg(?:Read|Write)|DreamV3(?:Read|Write)Register|"
    r"READ_REGISTER_ULONG|WRITE_REGISTER_ULONG)\s*\([^\n;]*?\b(0[xX][0-9A-Fa-f]+)"
)

NAME_HINT_RE = re.compile(
    r"(?:^A_|^BC250_REG_|^AMDBC250_REG_|REG_|MMIO|SMN|C2PMSG|MP0|MP1|NBIO|"
    r"RSMU|SMUIO|HDP|GCVM|GCMC|SDMA|GRBM|SCRATCH|SPI_PG|RLC_PG|COMPUTE_|"
    r"CP_|IH_|THM|DOORBELL|UCODE_(?:ADDR|DATA)|BAR[05])",
    re.IGNORECASE,
)


def source_files() -> list[Path]:
    files: list[Path] = []
    for base in SOURCE_ROOTS:
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            if path.name in SKIP_NAMES or any(part.lower() in SKIP_PARTS for part in path.parts):
                continue
            files.append(path)
    return sorted(set(files))


def clean_expression(value: str) -> str:
    value = re.split(r"/\*|//", value, maxsplit=1)[0].strip()
    return re.sub(r"\s+", " ", value)


def main() -> None:
    definitions: dict[tuple[str, str], list[str]] = defaultdict(list)
    direct_calls: dict[str, list[str]] = defaultdict(list)
    scanned = source_files()

    for path in scanned:
        relative = path.relative_to(ROOT).as_posix()
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        for number, line in enumerate(lines, 1):
            match = DEFINE_RE.match(line)
            if match:
                name, expression = match.groups()
                expression = clean_expression(expression)
                if HEX_RE.search(expression) and NAME_HINT_RE.search(name):
                    definitions[(name, expression)].append(f"{relative}:{number}")
            for call in DIRECT_CALL_RE.finditer(line):
                direct_calls[call.group(1).upper()].append(f"{relative}:{number}")

    out: list[str] = []
    out.append("# Inventário bruto de endereços encontrados nos fontes antigos")
    out.append("")
    out.append("> Gerado mecanicamente por `tools/generate_register_ledger.py`. Este arquivo é um índice arqueológico, não uma fonte da verdade. Definições podem ser máscaras, IDs, offsets relativos, endereços de outra geração, código morto ou valores já revogados. Consulte `03_CATALOGO_DE_REGISTRADORES.md` antes de usar qualquer item.")
    out.append("")
    out.append(f"Arquivos-fonte examinados: **{len(scanned)}**. Definições distintas: **{len(definitions)}**. Literais passados diretamente a acessos de registro: **{len(direct_calls)}**.")
    out.append("")
    out.append("## Definições com aparência de registro/endereço")
    out.append("")
    out.append("| Símbolo | Expressão histórica | Proveniência |")
    out.append("|---|---|---|")
    for (name, expression), locations in sorted(definitions.items(), key=lambda item: (item[0][0], item[0][1])):
        shown = locations[:5]
        provenance = "<br>".join(f"`{loc}`" for loc in shown)
        if len(locations) > len(shown):
            provenance += f"<br>+{len(locations) - len(shown)} ocorrência(s)"
        safe_expression = expression.replace("|", "\\|")
        out.append(f"| `{name}` | `{safe_expression}` | {provenance} |")

    out.append("")
    out.append("## Literais usados diretamente em chamadas de MMIO")
    out.append("")
    out.append("| Literal | Proveniência |")
    out.append("|---:|---|")
    for value, locations in sorted(direct_calls.items(), key=lambda item: int(item[0], 16)):
        shown = locations[:8]
        provenance = "<br>".join(f"`{loc}`" for loc in shown)
        if len(locations) > len(shown):
            provenance += f"<br>+{len(locations) - len(shown)} ocorrência(s)"
        out.append(f"| `{value}` | {provenance} |")

    out.append("")
    out.append("## Como promover um item")
    out.append("")
    out.append("Um item deste inventário só pode entrar no catálogo curado após identificar IP/version, header upstream, `mmREG`, `BASE_IDX`, base Cyan Skillfish, unidade e teste seguro. Leituras plausíveis e write-stick isolado não bastam.")
    out.append("")
    OUTPUT.write_text("\n".join(out) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
