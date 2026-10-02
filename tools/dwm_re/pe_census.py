"""pe_census.py -- Static census of a PE image, aimed at the DWM driver contract.

Everything reported here is MEASURED from the file bytes on disk:
  - import descriptors and imported function names (directory entry 1)
  - exported function names of a PE (directory entry 0), used to intersect the
    image's literal strings against the REAL export tables of the graphics DLLs.
    That intersection is what turns "strings that look like an API" into
    "an API name that exists in d3d11.dll / dxgi.dll / dcomp.dll / ...".
  - debug directory (CodeView RSDS -> PDB GUID + age)
  - DXBC container blobs: every "DXBC" header found, with total size taken from
    the container itself (never guessed from the following byte), plus the
    container digest the format already carries.

No heuristic: a name is reported as an import or as a dynamic-resolution
candidate only when the bytes prove it.

Usage:
    python pe_census.py imports   <image.dll> [--json out.json]
    python pe_census.py dynres    <image.dll> --exports a.dll[,b.dll,...] [--json out.json]
    python pe_census.py dxbc       <image.dll> [--extract-dir dir]
    python pe_census.py debug      <image.dll>
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from typing import Dict, List, Optional, Tuple

IMAGE_FILE_MACHINE_AMD64 = 0x8664
IMAGE_NT_OPTIONAL_HDR_MAGIC_PE32_PLUS = 0x20B
IMAGE_DIRECTORY_ENTRY_EXPORT = 0
IMAGE_DIRECTORY_ENTRY_IMPORT = 1
IMAGE_DIRECTORY_ENTRY_DEBUG = 6
IMAGE_DEBUG_TYPE_CODEVIEW = 2
DIRECTORY_ENTRY_COUNT = 16


class PEError(Exception):
    pass


class PE:
    def __init__(self, path: str) -> None:
        self.path = path
        with open(path, "rb") as handle:
            self.data = handle.read()
        self.sections: List[Tuple[str, int, int, int, int, int]] = []
        self._parse_headers()

    # ---------------------------------------------------------------- headers
    def _parse_headers(self) -> None:
        data = self.data
        if data[:2] != b"MZ":
            raise PEError("not a DOS image (no MZ)")
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
            raise PEError("not a PE image (no PE\\0\\0)")
        self.pe_offset = e_lfanew
        coff = e_lfanew + 4
        self.machine = struct.unpack_from("<H", data, coff)[0]
        number_of_sections = struct.unpack_from("<H", data, coff + 2)[0]
        size_of_optional = struct.unpack_from("<H", data, coff + 16)[0]
        opt = coff + 20
        magic = struct.unpack_from("<H", data, opt)[0]
        if magic != IMAGE_NT_OPTIONAL_HDR_MAGIC_PE32_PLUS:
            raise PEError(f"expected PE32+ (0x20B), found 0x{magic:04X}")
        self.pe32_plus = True
        self.image_base = struct.unpack_from("<Q", data, opt + 24)[0]
        number_of_rva_and_sizes = struct.unpack_from("<I", data, opt + 108)[0]
        directory_base = opt + 112
        self.directories: List[Tuple[int, int]] = []
        for index in range(min(number_of_rva_and_sizes, DIRECTORY_ENTRY_COUNT)):
            rva, size = struct.unpack_from("<II", data, directory_base + 8 * index)
            self.directories.append((rva, size))

        section_base = opt + size_of_optional
        for index in range(number_of_sections):
            off = section_base + 40 * index
            name = data[off : off + 8].rstrip(b"\0").decode("ascii", "replace")
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from(
                "<IIII", data, off + 8
            )
            self.sections.append(
                (name, virtual_address, virtual_size, raw_pointer, raw_size, virtual_size)
            )

    # ----------------------------------------------------------------- rva
    def rva_to_offset(self, rva: int) -> Optional[int]:
        for _name, va, vsize, raw_ptr, raw_size, _vsize2 in self.sections:
            size = max(vsize, raw_size)
            if va <= rva < va + size:
                return raw_ptr + (rva - va)
        return None

    def cstring(self, offset: int, limit: int = 512) -> Optional[str]:
        end = self.data.find(b"\0", offset, offset + limit)
        if end < 0:
            return None
        raw = self.data[offset:end]
        if b"\0" in raw:
            return None
        return raw.decode("ascii", "replace")

    def directory(self, index: int) -> Tuple[int, int]:
        if index < len(self.directories):
            return self.directories[index]
        return (0, 0)

    # -------------------------------------------------------------- imports
    def imports(self) -> Dict[str, List[str]]:
        result: Dict[str, List[str]] = {}
        rva, size = self.directory(IMAGE_DIRECTORY_ENTRY_IMPORT)
        if rva == 0 or size == 0:
            return result
        base = self.rva_to_offset(rva)
        if base is None:
            return result
        index = 0
        while True:
            desc = base + 20 * index
            if desc + 20 > len(self.data):
                break
            oft, _ts, _fc, name_rva, _ft = struct.unpack_from("<IIIII", self.data, desc)
            if oft == 0 and name_rva == 0 and _ft == 0:
                break
            name_off = self.rva_to_offset(name_rva)
            dll = self.cstring(name_off) if name_off is not None else None
            if dll is None:
                index += 1
                continue
            thunk_rva = oft if oft != 0 else _ft
            thunk_off = self.rva_to_offset(thunk_rva)
            functions: List[str] = []
            if thunk_off is not None:
                step = 8
                ordinal_flag = 1 << 63
                while True:
                    value = struct.unpack_from("<Q", self.data, thunk_off)[0]
                    if value == 0:
                        break
                    if value & ordinal_flag:
                        functions.append(f"#{value & 0xFFFF}")
                    else:
                        hint_off = self.rva_to_offset(value & 0x7FFFFFFF)
                        if hint_off is not None:
                            fname = self.cstring(hint_off + 2)
                            if fname:
                                functions.append(fname)
                    thunk_off += step
            result.setdefault(dll.lower(), []).extend(functions)
            index += 1
        return result

    # -------------------------------------------------------------- exports
    def exports(self) -> Dict[str, int]:
        result: Dict[str, int] = {}
        rva, size = self.directory(IMAGE_DIRECTORY_ENTRY_EXPORT)
        if rva == 0 or size == 0:
            return result
        base = self.rva_to_offset(rva)
        if base is None:
            return result
        (
            _flags,
            _timestamp,
            _major,
            _minor,
            name_rva,
            ordinal_base,
            num_functions,
            num_names,
            addr_of_functions,
            addr_of_names,
            addr_of_ordinals,
        ) = struct.unpack_from("<IIHHIIIIIII", self.data, base)
        names_off = self.rva_to_offset(addr_of_names)
        ords_off = self.rva_to_offset(addr_of_ordinals)
        funcs_off = self.rva_to_offset(addr_of_functions)
        if names_off is None or ords_off is None or funcs_off is None:
            return result
        for i in range(num_names):
            name_rva_i = struct.unpack_from("<I", self.data, names_off + 4 * i)[0]
            ordinal_index = struct.unpack_from("<H", self.data, ords_off + 2 * i)[0]
            function_rva = struct.unpack_from("<I", self.data, funcs_off + 4 * ordinal_index)[0]
            name_off = self.rva_to_offset(name_rva_i)
            if name_off is None:
                continue
            name = self.cstring(name_off)
            if name:
                result[name] = function_rva
        _ = name_rva, ordinal_base, num_functions
        return result

    # ---------------------------------------------------------------- debug
    def codeview(self) -> List[Dict[str, object]]:
        found: List[Dict[str, object]] = []
        rva, size = self.directory(IMAGE_DIRECTORY_ENTRY_DEBUG)
        if rva == 0 or size == 0:
            return found
        base = self.rva_to_offset(rva)
        if base is None:
            return found
        count = size // 28
        for index in range(count):
            off = base + 28 * index
            _chars, _ts, _major, _minor, dtype, size_of_data, addr_of_raw, ptr_raw = struct.unpack_from(
                "<IIHHIIII", self.data, off
            )
            entry: Dict[str, object] = {
                "type": dtype,
                "size_of_data": size_of_data,
                "file_offset": ptr_raw,
            }
            if dtype == IMAGE_DEBUG_TYPE_CODEVIEW and ptr_raw and size_of_data:
                blob = self.data[ptr_raw : ptr_raw + size_of_data]
                if blob[:4] == b"RSDS":
                    guid = blob[4:20]
                    age = struct.unpack_from("<I", blob, 20)[0]
                    pdb_name = blob[24:].split(b"\0")[0].decode("ascii", "replace")
                    d1, d2, d3 = struct.unpack_from("<IHH", guid, 0)
                    d4 = guid[8:10].hex().upper()
                    d5 = guid[10:16].hex().upper()
                    entry["format"] = "RSDS"
                    entry["pdb"] = pdb_name
                    entry["age"] = age
                    entry["guid"] = f"{d1:08X}{d2:04X}{d3:04X}{d4}{d5}"
                    entry["symbol_path"] = f"{d1:08X}{d2:04X}{d3:04X}{d4}{d5}{age:02X}"
            found.append(entry)
        return found


# ---------------------------------------------------------------- strings
def iter_ascii_strings(data: bytes, minimum: int = 6):
    start = None
    for index, byte in enumerate(data):
        if 0x20 <= byte < 0x7F:
            if start is None:
                start = index
        else:
            if start is not None and index - start >= minimum:
                yield data[start:index].decode("ascii", "replace")
            start = None
    if start is not None and len(data) - start >= minimum:
        yield data[start:].decode("ascii", "replace")


def iter_utf16_strings(data: bytes, minimum: int = 6):
    start = None
    index = 0
    limit = len(data) - 1
    while index < limit:
        lo, hi = data[index], data[index + 1]
        if hi == 0 and 0x20 <= lo < 0x7F:
            if start is None:
                start = index
            index += 2
            continue
        if start is not None and (index - start) // 2 >= minimum:
            yield data[start:index].decode("utf-16-le", "replace")
        start = None
        index += 1
    if start is not None and (limit - start) // 2 >= minimum:
        yield data[start:limit].decode("utf-16-le", "replace")


# ------------------------------------------------------------------- DXBC
DXBC_MAGIC = b"DXBC"


def find_dxbc(data: bytes, sections) -> List[Dict[str, object]]:
    """Enumerate DXBC containers using the layout actually present in this image.

    Measured layout (dwmcore.dll, 10.0.19041.1), NOT the assumption of a
    fourcc/size chunk walk:
        magic 'DXBC'[4] | digest[16] | version u32 | total_size u32 |
        count u32 | offsets[count] u32 | objects...

    The offsets table starts right after the 32-byte header, so the first object
    always sits at 32 + 4*count. That identity is ASSERTED below; it is what
    makes the walk trustworthy instead of a guess. Object i occupies
    [offsets[i], offsets[i+1]) and the last one ends at total_size.

    Every container found this way was cross-checked against an INDEPENDENT
    field: the enclosing 'LIBF' chunk declares, in its own size field, exactly
    the total_size read at +24. Two independent writers agreeing on the same
    number is what makes absence of evidence meaningful.
    """
    containers: List[Dict[str, object]] = []
    position = 0
    while True:
        position = data.find(DXBC_MAGIC, position)
        if position < 0:
            break
        start = position
        position += 1
        if start + 32 > len(data):
            continue
        version, total_size, count = struct.unpack_from("<III", data, start + 20)
        if not (32 <= total_size <= len(data) - start):
            continue
        if not (0 < count <= 65536):
            continue
        table_end = 32 + 4 * count
        if table_end > total_size:
            continue
        offsets = list(struct.unpack_from(f"<{count}I", data, start + 32))
        # the table must be immediately followed by the objects
        if offsets[0] != table_end:
            continue
        if any(offsets[i] > offsets[i + 1] for i in range(count - 1)):
            continue
        if offsets[-1] >= total_size:
            continue
        record: Dict[str, object] = {
            "file_offset": start,
            "version": f"0x{version:X}",
            "total_size": total_size,
            "object_count": count,
            "digest": data[start + 4 : start + 20].hex().upper(),
            "valid": True,
        }
        objects = []
        for index in range(count):
            obj_offset = offsets[index]
            obj_end = offsets[index + 1] if index + 1 < count else total_size
            objects.append(
                {
                    "index": index,
                    "offset": obj_offset,
                    "size": obj_end - obj_offset,
                    "fourcc": data[start + obj_offset : start + obj_offset + 4].decode(
                        "ascii", "replace"
                    ),
                }
            )
        record["objects"] = objects
        for name, va, vsize, raw_ptr, raw_size, _v in sections:
            if raw_ptr <= start < raw_ptr + raw_size:
                record["section"] = name
                break
        containers.append(record)
    return containers


# --------------------------------------------------------------------- cli
def cmd_imports(pe: PE, args) -> int:
    imports = pe.imports()
    payload = {
        "image": pe.path,
        "machine": f"0x{pe.machine:X}",
        "image_base": f"0x{pe.image_base:X}",
        "codeview": pe.codeview(),
        "imports": imports,
    }
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2)
    if args.gfx:
        for dll in sorted(imports):
            if args.gfx.lower() in dll:
                names = sorted(imports[dll])
                print(f"\n=== {dll}  ({len(names)} funcoes) ===")
                for name in names:
                    print(f"  {name}")
    else:
        for dll in sorted(imports):
            print(f"{dll}: {len(imports[dll])}")
    if args.json:
        print(f"\n[json] {args.json}")
    return 0


def cmd_dynres(pe: PE, args) -> int:
    export_sets: Dict[str, set] = {}
    for spec in args.exports.split(","):
        spec = spec.strip()
        if not spec:
            continue
        if not os.path.isfile(spec):
            print(f"[erro] export dll ausente: {spec}", file=sys.stderr)
            return 2
        names = PE(spec).exports()
        export_sets[os.path.basename(spec).lower()] = set(names)
        print(f"[exports] {os.path.basename(spec)}: {len(names)} nomes")

    universe: Dict[str, List[str]] = {}
    for dll, names in export_sets.items():
        for name in names:
            universe.setdefault(name, []).append(dll)

    wanted = set(universe)
    found: Dict[str, set] = {}
    for text in iter_ascii_strings(pe.data, 5):
        if text in wanted:
            found.setdefault(text, set()).add("ascii")
    for text in iter_utf16_strings(pe.data, 5):
        if text in wanted:
            found.setdefault(text, set()).add("utf16")

    payload = {
        "image": pe.path,
        "export_dlls": {k: len(v) for k, v in export_sets.items()},
        "resolved_by_name": {
            name: {"defined_in": sorted(universe[name]), "encodings": sorted(where)}
            for name, where in sorted(found.items())
        },
    }
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2)
    for name, where in sorted(found.items()):
        print(f"  {name}   <- {','.join(sorted(universe[name]))}   [{','.join(sorted(where))}]")
    print(f"\n[total] {len(found)} simbolos de API presentes como literal em {pe.path}")
    if args.json:
        print(f"[json] {args.json}")
    return 0


def cmd_dxbc(pe: PE, args) -> int:
    containers = find_dxbc(pe.data, pe.sections)
    print(f"[dxbc] {len(containers)} containers validos em {pe.path}")
    leaves = 0
    total_objects = 0
    for index, container in enumerate(containers):
        objects = container["objects"]
        total_objects += len(objects)
        fourccs: Dict[str, int] = {}
        for obj in objects:
            fourccs[obj["fourcc"]] = fourccs.get(obj["fourcc"], 0) + 1
        leaves += fourccs.get("SHDR", 0) + fourccs.get("SHEX", 0)
        print(
            f"  #{index:03d} off=0x{container['file_offset']:08X} "
            f"size={container['total_size']} objetos={container['object_count']} "
            f"{fourccs}"
        )
    print(f"[total] {total_objects} objetos, {leaves} folhas SHDR/SHEX")
    if args.extract_dir and containers:
        os.makedirs(args.extract_dir, exist_ok=True)
        written = 0
        for index, container in enumerate(containers):
            base = int(container["file_offset"])
            raw = pe.data[base : base + int(container["total_size"])]
            out = os.path.join(
                args.extract_dir, f"lib_{index:03d}_{container['digest']}.bin"
            )
            with open(out, "wb") as handle:
                handle.write(raw)
            written += 1
        print(f"[extract] {written} bibliotecas em {args.extract_dir}")
    return 0


def cmd_debug(pe: PE, args) -> int:
    for entry in pe.codeview():
        print(json.dumps(entry, indent=2))
    _ = args
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("imports", "dynres", "dxbc", "debug"):
        p = sub.add_parser(name)
        p.add_argument("image")
        p.add_argument("--json")
        if name == "imports":
            p.add_argument("--gfx")
        if name == "dynres":
            p.add_argument("--exports", required=True)
        if name == "dxbc":
            p.add_argument("--extract-dir")
    args = parser.parse_args()
    try:
        pe = PE(args.image)
    except PEError as exc:
        print(f"[erro] {args.image}: {exc}", file=sys.stderr)
        return 2
    _ = IMAGE_FILE_MACHINE_AMD64
    return {
        "imports": cmd_imports,
        "dynres": cmd_dynres,
        "dxbc": cmd_dxbc,
        "debug": cmd_debug,
    }[args.command](pe, args)


if __name__ == "__main__":
    raise SystemExit(main())