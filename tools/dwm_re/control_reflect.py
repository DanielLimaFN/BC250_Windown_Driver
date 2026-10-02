"""control_reflect.py -- Control experiment for shader_reflect.py's harness.

Compiles a known HLSL shader with the system's D3DCompile, then reflects and
disassembles the resulting bytecode with the SAME reflect_blob() used against
the DWM blobs. If the control fails, the harness is wrong and any negative
result about dwmcore.dll means nothing.

CORRECAO 30/09 (doc 544 §9): a versao anterior deste controle NAO compilava
(HLSL com `g_cb.uvScale` -> X3004, e entrypoint `main` para a funcao `PSMain`),
reusava o IID de ID3DBlob no D3DReflect e a assinatura errada do D3DDisassemble.
O controle "falhou" por defeito proprio, e o doc 544 concluiu que a reflexao
estava inoperante na maquina. Agora o controle SAI COM ERRO se qualquer passo
falhar, em vez de so' imprimir.

Usage: python control_reflect.py
"""

from __future__ import annotations

import ctypes
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shader_reflect import _blob_read, load_compiler, reflect_blob  # noqa: E402

HLSL = """
Texture2D    g_tex : register(t0);
SamplerState g_smp : register(s0);
cbuffer      g_cb  : register(b0) { float4 tint; float2 uvScale; };
struct PSIn  { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 PSMain(PSIn input) : SV_Target
{
    return g_tex.Sample(g_smp, input.uv * uvScale) * tint;
}
"""
ENTRYPOINT = b"PSMain"


def compile_hlsl(dll, target: str):
    source = HLSL.encode("ascii")
    code_out = ctypes.c_void_p()
    errors_out = ctypes.c_void_p()
    hr = dll.D3DCompile(
        source,
        len(source),
        b"control.hlsl",
        None,  # pDefines
        None,  # pInclude
        ENTRYPOINT,
        target.encode("ascii"),
        0,  # Flags1
        0,  # Flags2
        ctypes.byref(code_out),
        ctypes.byref(errors_out),
    )
    print(f"[D3DCompile {target}] hr=0x{hr & 0xFFFFFFFF:08X}")
    if errors_out:
        errors = _blob_read(errors_out)
        if errors:
            print("[errors]", errors.decode("latin-1", "replace"))
    if hr < 0:
        return None
    blob = _blob_read(code_out)
    print(f"[blob] {len(blob)} bytes, magic={blob[:4]!r}")
    return blob


def main() -> int:
    dll = load_compiler(r"C:\Windows\System32\d3dcompiler_47.dll")
    dll.D3DCompile.restype = ctypes.c_long
    # 11 parameters, the documented D3DCompile order.
    dll.D3DCompile.argtypes = [
        ctypes.c_char_p,  # pSrcData
        ctypes.c_size_t,  # SrcDataSize
        ctypes.c_char_p,  # pFileName
        ctypes.c_void_p,  # pDefines
        ctypes.c_void_p,  # pInclude
        ctypes.c_char_p,  # pEntrypoint
        ctypes.c_char_p,  # pTarget
        ctypes.c_uint32,  # Flags1
        ctypes.c_uint32,  # Flags2
        ctypes.POINTER(ctypes.c_void_p),  # ppCode
        ctypes.POINTER(ctypes.c_void_p),  # ppErrorMsgs
    ]
    failures = 0
    for target in ("ps_4_0", "ps_5_0", "ps_4_0_level_9_1"):
        blob = compile_hlsl(dll, target)
        if blob is None:
            print(f"CONTROLE REPROVADO: {target} nao compilou")
            failures += 1
            continue
        meta, _assembly, disasm = reflect_blob(blob, dll)
        print(f"[{target}] reflect={meta['reflect_hr']} ({meta['reflect_kind']}) disasm={meta['disasm_hr']}")
        if meta["reflect_hr"] != "0x00000000" or meta["disasm_hr"] != "0x00000000":
            print(f"CONTROLE REPROVADO: {target} nao refletiu ou nao desmontou")
            failures += 1
            continue
        if "tint" not in disasm or "sample" not in disasm:
            print(f"CONTROLE REPROVADO: {target} desmontou sem o cbuffer/sample esperados")
            failures += 1
            continue
        print("    " + "\n    ".join(disasm.splitlines()[:6]))
    if failures:
        print(f"CONTROLE: {failures} falha(s) -- o harness NAO serve para concluir nada sobre o dwmcore")
        return 1
    print("CONTROLE PASS: compilar, refletir e desmontar funcionam nesta maquina")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
