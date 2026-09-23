#!/usr/bin/env python3
"""Fail-closed source audit for the external Mesa RADV WDDM2 fork."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesa-root", type=Path, required=True)
    args = parser.parse_args()
    root = args.mesa_root.resolve()

    paths = {
        "bo": root / "src/amd/vulkan/winsys/wddm2/radv_wddm2_bo.c",
        "cs": root / "src/amd/vulkan/winsys/wddm2/radv_wddm2_cs.c",
        "wsi": root / "src/amd/vulkan/winsys/wddm2/radv_wddm2_wsi.c",
        "radv_wsi": root / "src/amd/vulkan/radv_wsi.c",
        "fence": root / "src/vulkan/runtime/vk_wddm2_monitored_fence.c",
    }
    missing = [str(path) for path in paths.values() if not path.is_file()]
    if missing:
        print("RADV WDDM SAFETY AUDIT: FAILED")
        for path in missing:
            print(f"  - missing: {path}")
        return 1

    text = {name: path.read_text(encoding="utf-8") for name, path in paths.items()}
    errors: list[str] = []

    forbidden = {
        "fake allocation handles": "fake_hAlloc",
        "CPU-invented GPU virtual addresses": "next_virtual_va",
        "CPU-invented mapped GPUVA": "static uint64_t next_va",
        "host allocation impersonating a GPU BO": "calloc(1, bo->base.size)",
        "device handle impersonating a context": "create_context.hContext = ws->device_h",
        "CPU-authored monitored fence": "p_atomic_set(mf->value_map",
        "software WSI advertisement": "wsi->sw = true",
        "direct PM4 escape bypass": "RADV_BC250_DIRECT_PM4_ESCAPE",
    }
    corpus = "\n".join(text.values())
    for label, needle in forbidden.items():
        if needle in corpus:
            errors.append(label)

    required = {
        "BC-250 allocation ABI magic": ("bo", "BC250_ALLOCATION_PRIVATE_MAGIC"),
        "BC-250 allocation token": ("bo", ".allocation_token = (uint64_t)(uintptr_t)bo"),
        "real CreateAllocation2 failure": ("bo", "CreateAllocation2 failed"),
        "real GPUVA map failure": ("bo", "map.VirtualAddress == 0"),
        "BC-250 node 1 selection": ("cs", "is_bc250 ? 1 : 0"),
        "BC-250 HW queue disabled": ("cs", ".HwQueueSupported = !is_bc250"),
        "context failure propagation": ("cs", "return VK_ERROR_INITIALIZATION_FAILED;"),
        "submit failure propagation": ("cs", "return VK_ERROR_DEVICE_LOST;"),
        "kernel fence wait": ("fence", "WaitForSynchronizationObjectFromCpu"),
        "hardware WSI on Windows": ("radv_wsi", "bool sw = false;"),
    }
    for label, (name, needle) in required.items():
        if needle not in text[name]:
            errors.append(f"missing {label}")

    # DX.22 (22/09/2026). MEDIDO na Corrida 67: todo IB saia da VA do CS do
    # preambulo, crescendo 392 DW por submissao com prefixo identico -- o ramo
    # BC-250 copiava o CS principal para dentro do preambulo e deixava o cdw
    # dele crescido, e a GPU reexecutava todos os draws anteriores. O CS de
    # origem nao pode mais ser escrito pela concatenacao.
    cs_text = text["cs"]
    for label, needle in {
        "concatenacao cresce o cdw do CS de origem": "cs_list[0]->cdw =",
        "concatenacao reescreve o tamanho do IB de origem": "ib_buffers[0].cdw =",
        "concatenacao copia para dentro do CS de origem": "memcpy(cs_list[0]->buf",
        "concatenacao escreve NOP no CS de origem": "cs_list[0]->buf[cur_offset",
    }.items():
        if needle in cs_text:
            errors.append(f"DX.22: {label} ({needle})")
    acquire = cs_text.find("slot = radv_wddm2_bc250_concat_acquire(ctx, hw_ip, total_aligned)")
    grown = cs_text.rfind("num_ib_buffers > 1", 0, acquire if acquire >= 0 else 0)
    if acquire < 0 or grown < 0:
        errors.append("DX.22: concatenacao sem pool proprio ou sem a guarda de CS crescido")
    fail = cs_text.find('"D3DKMTSubmitCommandToHwQueue"')
    seq = cs_text.find("concat_slot->seq = ++ctx->per_ip[submit->ip_type].concat_seq;")
    cover = cs_text.find("slots[i].fence_known = true;")
    if fail < 0 or seq < 0 or cover < 0 or not (fail < seq < cover):
        errors.append("DX.22: slot tem de ser preso SO' depois da submissao aceita e coberto pela fence sinalizada depois")
    if "radv_wddm2_bc250_concat_release_all(ctx);" not in cs_text:
        errors.append("DX.22: ctx_destroy nao libera os BOs do pool de concatenacao")

    # DX.26 (22/09/2026). Sem ip_discovery_version o Mesa aplica a correcao de
    # versao do kernel e o GFX1013 vira GFX10_3 (MEDIDO: IB da Corrida 71 escrevia
    # PA_CL_VRS_CNTL, DB_VRS_OVERRIDE_CNTL e SX_PS_DOWNCONVERT_CONTROL). Os dois
    # blocos hw_ip do winsys tem de declarar 10.1.3.
    ws_text = (root / "src/amd/vulkan/winsys/wddm2/radv_wddm2_winsys.c").read_text(encoding="utf-8")
    for block in ("hw_ip_gfx = {", "hw_ip_compute = {"):
        start = ws_text.find(block)
        body = ws_text[start:ws_text.find("};", start)] if start >= 0 else ""
        if ".ip_discovery_version = (10u << 16) | (1u << 8) | 3u," not in body:
            errors.append(f"DX.26: {block.split(' ')[0]} sem ip_discovery_version 10.1.3 -- o GFX1013 volta a ser tratado como GFX10_3")
    if "BC-250 IDENTIDADE: gfx_level=" not in ws_text:
        errors.append("DX.26: winsys nao registra a identidade efetiva (gfx_level) no stderr")

    # DX.29 (22/09/2026). O Mesa upstream NAO emite PREAMBLE_CNTL em radv_emit_graphics;
    # o fork inseria PKT3(0x4A) com payload 0/1, que nao e' BEGIN/END_CLEAR_STATE
    # ((2<<28)/(3<<28), nvd.h:395-396). Removido; nao pode voltar.
    q_text = (root / "src/amd/vulkan/radv_queue.c").read_text(encoding="utf-8")
    ge = q_text.find("radv_emit_graphics(struct radv_device *device")
    ge_body = q_text[ge:q_text.find("\n}\n", ge)] if ge >= 0 else ""
    if not ge_body:
        errors.append("DX.29: radv_emit_graphics nao encontrado")
    elif "PKT3(0x4A" in ge_body:
        errors.append("DX.29: radv_emit_graphics voltou a emitir PREAMBLE_CNTL (0x4A) -- o upstream nao emite")

    if errors:
        print("RADV WDDM SAFETY AUDIT: FAILED")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("RADV WDDM SAFETY AUDIT: PASS")
    print("  fabricated handles/GPUVA/fences: absent")
    print("  WARP/software WSI advertisement: absent")
    print("  WDDM callback failures: fail-closed")
    print("  DX.22 BC-250 concatenation in own pooled BO, source CS untouched: enforced")
    print("  DX.26 GFX1013 identified as GC 10.1.3 (ip_discovery_version), not GFX10_3: enforced")
    print("  DX.29 no PREAMBLE_CNTL in radv_emit_graphics (matches upstream Mesa): enforced")
    print("  verdict: NAO VALIDADO EM HARDWARE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
