"""shader_reflect.py -- Reflect and disassemble the DXBC blobs inside a PE.

Uses the system's own d3dcompiler_47.dll (D3DReflect / D3DDisassemble) so the
container format and the bytecode semantics are decoded by Microsoft's code,
not by a reimplementation of mine. Everything printed is measured output of
those two APIs over bytes taken from the image on disk.

Usage:
    python shader_reflect.py <image.dll> --out-dir <dir> [--summary-only]
"""

from __future__ import annotations

import ctypes
import argparse
import json
import os
import re
import sys
from ctypes import CDLL, POINTER, Structure, byref, c_size_t, c_uint32, c_void_p, c_char_p
from ctypes import HRESULT, c_ulonglong
from typing import Dict, List, Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_census import PE, find_dxbc  # noqa: E402

DISASM_INSTRUCTION = 0x1
DISASM_BYTECODE = 0x2


class D3D_SHADER_BYTECODE(Structure):
    _fields_ = [("pShaderBytecode", c_void_p), ("BytecodeLength", c_size_t)]


class GUID(Structure):
    _fields_ = [
        ("Data1", ctypes.c_uint32),
        ("Data2", ctypes.c_uint16),
        ("Data3", ctypes.c_uint16),
        ("Data4", ctypes.c_ubyte * 8),
    ]


# IID_ID3DBlob = {8A5E7CDA-4587-4EE7-8E1B-DBB6C8F6B0E2}
IID_ID3DBLOB = GUID(
    0x8A5E7CDA,
    0x4587,
    0x4EE7,
    (ctypes.c_ubyte * 8)(0x8E, 0x1B, 0xDB, 0xB6, 0xC8, 0xF6, 0xB0, 0xE2),
)

# CORRECAO 30/09 (doc 544 §9): D3DReflect exige a interface de REFLEXAO, nao a de
# blob. Com IID_ID3DBLOB toda chamada devolvia 0x80070057 e o doc 544 concluiu,
# errado, que a reflexao estava "inoperante nesta maquina".
# IID_ID3D11ShaderReflection (d3dcompiler_47) = {8D536CA1-0CCA-4956-A837-786963755584}
IID_ID3D11SHADERREFLECTION = GUID(
    0x8D536CA1,
    0x0CCA,
    0x4956,
    (ctypes.c_ubyte * 8)(0xA8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84),
)
# Bibliotecas (lib_*) refletem por D3DReflectLibrary com
# IID_ID3D11LibraryReflection = {54384F1B-5B3E-4BB7-AE01-60BA3097CBB6}
IID_ID3D11LIBRARYREFLECTION = GUID(
    0x54384F1B,
    0x5B3E,
    0x4BB7,
    (ctypes.c_ubyte * 8)(0xAE, 0x01, 0x60, 0xBA, 0x30, 0x97, 0xCB, 0xB6),
)


class Blob(Structure):
    pass


def _vtable_method(interface, index, restype):
    """Resolve an ID3DBlob vtable slot by INDEX, not by exported name.

    The blob is a COM interface: the first pointer-sized field is the vtable.
    Reading it directly avoids depending on a mangled export name, which the
    name-based ctypes form refuses when an index is supplied.
    """
    vtable = ctypes.cast(interface, ctypes.POINTER(ctypes.c_void_p))[0]
    address = ctypes.cast(vtable, ctypes.POINTER(ctypes.c_void_p))[index]
    return ctypes.CFUNCTYPE(restype, ctypes.c_void_p)(address)


def _blob_read(blob_ptr) -> bytes:
    if not blob_ptr:
        return b""
    get_buffer_pointer = _vtable_method(blob_ptr, 3, ctypes.c_void_p)
    get_buffer_size = _vtable_method(blob_ptr, 4, c_size_t)
    return ctypes.string_at(get_buffer_pointer(blob_ptr), get_buffer_size(blob_ptr))


def _release(interface) -> None:
    if interface:
        _vtable_method(interface, 2, ctypes.c_ulong)(interface)


def load_compiler(path: str):
    dll = CDLL(path)
    dll.D3DReflect.restype = ctypes.c_long
    dll.D3DReflect.argtypes = [c_void_p, c_size_t, POINTER(GUID), POINTER(c_void_p)]
    dll.D3DReflectLibrary.restype = ctypes.c_long
    dll.D3DReflectLibrary.argtypes = [c_void_p, c_size_t, POINTER(GUID), POINTER(c_void_p)]

    # CORRECAO 30/09: a assinatura documentada e'
    #   D3DDisassemble(LPCVOID pSrcData, SIZE_T SrcDataSize, UINT Flags,
    #                  LPCSTR szComments, ID3DBlob **ppDisassembly)
    # A versao anterior passava um D3D_SHADER_BYTECODE no lugar do ponteiro e as
    # flags no lugar do tamanho, e toda chamada falhava com 0x80004005.
    dll.D3DDisassemble.restype = ctypes.c_long
    dll.D3DDisassemble.argtypes = [
        c_void_p,
        c_size_t,
        c_uint32,
        c_char_p,
        POINTER(c_void_p),
    ]
    return dll


PROGRAM_TYPES = {
    0: "ps_1_0", 1: "ps_1_1", 2: "ps_1_2", 3: "ps_1_3", 4: "ps_1_4",
    6: "vs_1_0", 7: "vs_1_1", 8: "vs_2_0", 9: "vs_3_0",
    10: "ps_2_0", 11: "ps_2_1", 12: "cs_4_0",
    13: "ps_3_0", 14: "vs_3_1",
    15: "??15", 16: "gs_4_0", 17: "hs_5_0", 18: "ds_5_0",
    19: "ps_4_0", 20: "vs_4_0", 21: "gs_4_1", 22: "ps_4_1", 23: "cs_4_1",
    24: "vs_4_1", 25: "??25",
    26: "??26",
    27: "??27",
}

DECL_RE = re.compile(r"^\s*dcl_(\w+)(?:\s+(.*?))?\s*$", re.MULTILINE)
INSTR_RE = re.compile(r"^\s*(\d+):\s*(\w+)\s*(.*?)\s*$", re.MULTILINE)


def reflect_blob(blob: bytes, dll):
    buffer = (c_ulonglong * (len(blob) // 8 + 2))()
    ctypes_ulong = ctypes
    address = ctypes_ulong.cast(buffer, c_void_p)
    for index in range(0, len(blob), 8):
        chunk = blob[index : index + 8].ljust(8, b"\0")
        buffer[index // 8] = int.from_bytes(chunk, "little")
    result: Dict[str, object] = {"size": len(blob)}
    # D3DReflect devolve uma INTERFACE de reflexao, nao texto. Aqui so' se mede
    # se ela e' obtida (e se solta); o texto vem do D3DDisassemble. Shader que nao
    # reflete como programa e' tentado como biblioteca (lib_*).
    out = c_void_p()
    hr = dll.D3DReflect(address, len(blob), byref(IID_ID3D11SHADERREFLECTION), byref(out))
    kind = "shader"
    if hr < 0:
        _release(out)
        out = c_void_p()
        hr_lib = dll.D3DReflectLibrary(address, len(blob), byref(IID_ID3D11LIBRARYREFLECTION), byref(out))
        if hr_lib >= 0:
            hr, kind = hr_lib, "library"
    _release(out)
    result["reflect_hr"] = f"0x{hr & 0xFFFFFFFF:08X}"
    result["reflect_kind"] = kind if hr >= 0 else None
    out2 = c_void_p()
    hr2 = dll.D3DDisassemble(address, len(blob), 0, None, byref(out2))
    disasm = b""
    if hr2 >= 0 and out2:
        disasm = _blob_read(out2)
    _release(out2)
    result["disasm_hr"] = f"0x{hr2 & 0xFFFFFFFF:08X}"
    text = disasm.decode("latin-1", "replace")
    # O desmonte ja' traz declaracoes (dcl_*) e o cabecalho de RDEF; ele serve de
    # "assembly" para summarize().
    return result, text, text


def summarize(assembly: str, disasm: str) -> Dict[str, object]:
    summary: Dict[str, object] = {}
    m = re.search(r"dcl_input(?:ps)?\s+([^\s;]+)", assembly)
    m2 = re.search(r"dcl_output(?:ps)?\s+([^\s;]+)", assembly)
    m3 = re.search(r"dcl_sampler(?:s)?\s+([^\s;]+)", assembly)
    summary["stage_hint"] = {
        "dcl_input": m.group(1) if m else None,
        "dcl_output": m2.group(1) if m2 else None,
        "dcl_sampler": m3.group(1) if m3 else None,
    }
    textures = sorted(set(re.findall(r"dcl_resource_texture(\dD)", assembly)))
    summary["resource_texture_dims"] = textures
    summary["constant_buffers"] = sorted(
        set(re.findall(r"dcl_constantbuffer\s+\S+\s+,\s*(\S+)", assembly))
    )
    summary["samplers"] = sorted(
        set(re.findall(r"dcl_sampler\s+(\S+)", assembly))
    )
    summary["inputs"] = sorted(
        set(re.findall(r"dcl_input(?:ps)?\s+(\S+)\s+(?:v\d+[a-z]*\d*|v\d+)", assembly))
    )
    summary["outputs"] = sorted(
        set(re.findall(r"dcl_output(?:ps|o)?\s+(\S+)\s+(?:o\d+[a-z]*\d*|o\d+)", assembly))
    )
    summary["input_sematic_count"] = len(re.findall(r"dcl_input", assembly))
    summary["output_sematic_count"] = len(re.findall(r"dcl_output", assembly))
    instructions = re.findall(r"^\s*\d+:\s*(\w+)", disasm, re.MULTILINE)
    summary["instruction_count"] = len(instructions)
    summary["opcodes_used"] = sorted(set(instructions))
    # fxc annotations carry the variable names of the original HLSL
    annotations = re.findall(r"^\s*\d+:\s*\w+.*?;\s*//\s*(.+)$", disasm, re.MULTILINE)
    if not annotations:
        annotations = re.findall(r"//\s*(f\d+|v\d+|t\d+|cb\d+|x\d+|vp\d+|o\d+)\b", disasm)
    summary["annotations_sample"] = annotations[:12]
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--summary-only", action="store_true")
    parser.add_argument(
        "--compiler", default=r"C:\Windows\System32\d3dcompiler_47.dll"
    )
    args = parser.parse_args()

    dll = load_compiler(args.compiler)
    print(f"[reflect] usando {args.compiler}")

    pe = PE(args.image)
    containers = find_dxbc(pe.data, pe.sections)
    os.makedirs(args.out_dir, exist_ok=True)

    catalog: List[Dict[str, object]] = []
    index = 0
    for container in containers:
        fourccs = {obj["fourcc"] for obj in container["objects"]}
        if not ({"SHDR", "SHEX"} & fourccs):
            continue
        base = int(container["file_offset"])
        blob = pe.data[base : base + int(container["total_size"])]
        meta, assembly, disasm = reflect_blob(blob, dll)
        item: Dict[str, object] = {
            "index": index,
            "file_offset": base,
            "digest": container["digest"],
            "chunks": sorted(fourccs),
            **meta,
        }
        item.update(summarize(assembly, disasm))
        catalog.append(item)
        if not args.summary_only:
            stem = f"shader_{index:03d}_{container['digest'][:8]}"
            with open(os.path.join(args.out_dir, stem + ".asm"), "w", encoding="latin-1") as handle:
                handle.write(assembly)
            with open(os.path.join(args.out_dir, stem + ".disasm"), "w", encoding="latin-1") as handle:
                handle.write(disasm)
        index += 1

    json_path = os.path.join(args.out_dir, "shader_catalog.json")
    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(
            {
                "image": args.image,
                "reflector": args.compiler,
                "shader_count": len(catalog),
                "shaders": catalog,
            },
            handle,
            indent=2,
        )

    ok = [c for c in catalog if str(c["reflect_hr"]) == "0x00000000"]
    print(f"[total] {len(catalog)} folhas de shader; {len(ok)} refletidas com sucesso")
    profiles: Dict[str, int] = {}
    for c in catalog:
        key = str(c["stage_hint"].get("dcl_output") or c["stage_hint"].get("dcl_input") or "?")
        profiles[key] = profiles.get(key, 0) + 1
    for key in sorted(profiles):
        print(f"   {key:28s} {profiles[key]}")
    print(f"[json] {json_path}")
    return 0


def _unused(_=Optional):  # pragma: no cover
    return None


if __name__ == "__main__":
    raise SystemExit(main())