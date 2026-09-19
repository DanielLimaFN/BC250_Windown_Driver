#!/usr/bin/env python3
"""Espelha e VERIFICA o delta do fork Mesa/RADV dentro deste repositorio.

POR QUE ISTO EXISTE
===================
O `vulkan_radeon.dll` que a BC-250 carrega nao e construido aqui. Ele sai de uma
arvore Mesa que vive FORA do repositorio, e ate 19/09/2026 nada neste projeto
registrava onde ela estava, de que commit partia, nem o que tinhamos mudado nela.
Consequencias medidas naquele dia:

  - `tools/audit-radv-wddm-safety.py` exige `--mesa-root` e NUNCA tinha rodado;
  - uma auditoria minha declarou a arvore "inexistente na maquina" e classificou a
    correcao de GCVM de 34 bits como nao auditavel -- falso, e o erro so foi pego
    porque o caminho estava embutido em texto dentro do proprio DLL;
  - o delta inteiro (o driver Vulkan da placa) existia em UM diretorio sem copia.

Esta ferramenta nao compila nada. Ela faz duas coisas e as duas importam:

  --verify  (padrao) confere que o arquivo espelhado aqui ainda corresponde a
            arvore externa, que a arvore esta no commit base declarado e que o
            DLL implantado foi construido DAQUELA arvore (sha256 identico).
  --update  regenera o espelho a partir da arvore externa.

VEREDITO: nada aqui prova execucao. NAO VALIDADO EM HARDWARE.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MIRROR_DIR = ROOT / "external" / "radv-wddm"
PATCH = MIRROR_DIR / "bc250-radv-wddm.patch"
PROVENANCE = MIRROR_DIR / "PROVENANCE.md"

DEFAULT_MESA_ROOT = Path(r"C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm")
DEFAULT_DEPLOYED_DLL = Path(r"C:\BC250_LIB\vulkan_radeon.dll")
BUILT_DLL_REL = Path("build/src/amd/vulkan/vulkan_radeon.dll")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def git(mesa_root: Path, *args: str) -> str:
    out = subprocess.run(
        ["git", "-C", str(mesa_root), *args],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    if out.returncode != 0:
        raise SystemExit(f"RECUSADO: git {' '.join(args)} falhou em {mesa_root}:\n{out.stderr}")
    return out.stdout


def read_provenance_field(name: str) -> str | None:
    if not PROVENANCE.is_file():
        return None
    text = PROVENANCE.read_text(encoding="utf-8", errors="replace")
    m = re.search(rf"^- \*\*{re.escape(name)}:\*\*\s*`([^`]+)`", text, re.M)
    return m.group(1) if m else None


def do_update(mesa_root: Path, deployed: Path) -> int:
    base = git(mesa_root, "rev-parse", "HEAD").strip()
    subject = git(mesa_root, "log", "-1", "--format=%s").strip()
    remote = git(mesa_root, "remote", "get-url", "origin").strip()
    diff = git(mesa_root, "diff")

    MIRROR_DIR.mkdir(parents=True, exist_ok=True)
    PATCH.write_text(diff, encoding="utf-8", newline="\n")

    built = mesa_root / BUILT_DLL_REL
    built_hash = sha256(built) if built.is_file() else "ARQUIVO AUSENTE"
    deployed_hash = sha256(deployed) if deployed.is_file() else "ARQUIVO AUSENTE"

    print(f"espelho atualizado: {PATCH.relative_to(ROOT)}")
    print(f"  base            : {base} ({subject})")
    print(f"  origin          : {remote}")
    print(f"  patch sha256    : {sha256(PATCH)}")
    print(f"  DLL construido  : {built_hash}")
    print(f"  DLL implantado  : {deployed_hash}")
    print()
    print("Atualize external/radv-wddm/PROVENANCE.md com estes valores.")
    return 0


def do_verify(mesa_root: Path, deployed: Path) -> int:
    errors: list[str] = []
    warnings: list[str] = []

    if not PATCH.is_file():
        print("RECUSADO: espelho do delta ausente -- o driver Vulkan nao tem copia neste repo")
        return 2
    if PATCH.stat().st_size == 0:
        print("RECUSADO: espelho do delta vazio")
        return 2

    declared_base = read_provenance_field("Commit base")
    declared_patch = read_provenance_field("SHA-256 do patch")
    declared_dll = read_provenance_field("SHA-256 do DLL")

    if declared_base is None or declared_patch is None or declared_dll is None:
        errors.append("PROVENANCE.md nao declara commit base, hash do patch e hash do DLL")

    actual_patch = sha256(PATCH)
    if declared_patch and declared_patch != actual_patch:
        errors.append(
            f"patch espelhado mudou sem a proveniencia acompanhar: "
            f"declarado {declared_patch[:16]}..., real {actual_patch[:16]}...")

    # A arvore externa pode nao existir nesta maquina; isso e AVISO, nao erro,
    # porque o espelho continua valendo como copia de seguranca.
    if not (mesa_root / ".git").exists():
        warnings.append(
            f"arvore externa ausente em {mesa_root}: o espelho nao pode ser conferido "
            f"contra a fonte nesta maquina (o patch em si continua integro)")
    else:
        base = git(mesa_root, "rev-parse", "HEAD").strip()
        if declared_base and base != declared_base:
            errors.append(
                f"arvore externa saiu do commit base declarado: "
                f"HEAD={base[:12]}, PROVENANCE={declared_base[:12]}")
        live = git(mesa_root, "diff")
        if live.replace("\r\n", "\n") != PATCH.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n"):
            errors.append(
                "o delta da arvore externa DIVERGE do espelho: rode --update e "
                "atualize PROVENANCE.md, senao o repo guarda um driver que nao e o real")

        built = mesa_root / BUILT_DLL_REL
        if not built.is_file():
            warnings.append(f"DLL construido ausente em {built}")
        elif deployed.is_file():
            bh, dh = sha256(built), sha256(deployed)
            if bh != dh:
                errors.append(
                    f"o DLL implantado NAO foi construido desta arvore: "
                    f"build={bh[:16]}..., implantado={dh[:16]}...")
            elif declared_dll and dh != declared_dll:
                errors.append(
                    f"DLL implantado mudou sem a proveniencia acompanhar: "
                    f"declarado {declared_dll[:16]}..., real {dh[:16]}...")
        else:
            warnings.append(f"DLL implantado ausente em {deployed}")

    for w in warnings:
        print(f"AVISO  {w}")
    if errors:
        for e in errors:
            print(f"FALHA  {e}")
        print("RADV FORK DELTA: REPROVADO")
        return 1

    print("RADV FORK DELTA: PASS")
    print(f"  espelho        : {PATCH.relative_to(ROOT)} ({PATCH.stat().st_size} bytes)")
    print(f"  patch sha256   : {actual_patch}")
    if declared_base:
        print(f"  commit base    : {declared_base}")
    print("  verdict: NAO VALIDADO EM HARDWARE")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mesa-root", type=Path, default=DEFAULT_MESA_ROOT)
    ap.add_argument("--deployed-dll", type=Path, default=DEFAULT_DEPLOYED_DLL)
    ap.add_argument("--update", action="store_true",
                    help="regenera o espelho a partir da arvore externa")
    args = ap.parse_args()

    if args.update:
        return do_update(args.mesa_root, args.deployed_dll)
    return do_verify(args.mesa_root, args.deployed_dll)


if __name__ == "__main__":
    sys.exit(main())
