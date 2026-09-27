"""Mutation tests on temporary source copies. Never modify the working tree."""
import importlib.util
from pathlib import Path
import tempfile
import sys
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("safety", ROOT / "tools/audit-bc250mcdm-safety.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
paths = [
    "driver/umd/bc250_umd_descriptors.c", "driver/umd/bc250_d3d_umd.c",
    "driver/umd/bc250_umd_vulkan_bridge.c", "driver/umd/bc250_vkd3d_shader_adapter.c",
    "driver/core/bc250_gfx_ring_transaction.c", "driver/umd/bc250_umd_pipeline_cache.c",
    "driver/umd/bc250_umd_resource_vk.c",
]
mutations = [
    (0, "vkCreateDescriptorPool(", "vkCreateDescriptorPool_REMOVED("),
    (0, "vkAllocateDescriptorSets(", "vkAllocateDescriptorSets_REMOVED("),
    (0, "vkUpdateDescriptorSets(", "vkUpdateDescriptorSets_REMOVED("),
    (0, "vkCmdBindDescriptorSets(", "vkCmdBindDescriptorSets_REMOVED("),
    (3, "info.next = &interfaceInfo;", "info.next = NULL;"),
    (3, "stage * BC250_DESCRIPTOR_STAGE_STRIDE +", "0 * BC250_DESCRIPTOR_STAGE_STRIDE +"),
    (1, "if (!Bc250UmdBindDrawDescriptors", "if (!Bc250UmdBindDrawDescriptors_REMOVED"),
    (2, "Bc250UmdVkReleaseDescriptorBatches(pBridge);", "Bc250UmdVkReleaseDescriptorBatches_REMOVED(pBridge);"),
    (4, "WaveStatus != 0xBEBEBEEFu", "WaveStatus != 0u"),
    (4, "WaveStatus & 0x00010000u", "WaveStatus & 0xFFFFFFFFu"),
    (4, "Evidence->ReadsIssued >= ReadBudget", "Evidence->ReadsIssued > ReadBudget + 6u"),
    (4, "Evidence->SelectorReadback == BC250_GRBM_GFX_INDEX_BROADCAST_ALL", "1"),
    (5, "pShader->NumBindings == UINT32_MAX", "pShader->NumBindings == 0"),
    (6, "res = pBridge->vk.vkFlushMappedMemoryRanges(", "pBridge->vk.vkFlushMappedMemoryRanges("),
]
with tempfile.TemporaryDirectory(prefix="bc250-contract-mutations-") as directory:
    audit.ROOT = Path(directory)
    originals = {}
    for name in paths:
        target = audit.ROOT / name
        target.parent.mkdir(parents=True, exist_ok=True)
        originals[name] = (ROOT / name).read_text(encoding="utf-8")
        target.write_text(originals[name], encoding="utf-8")
    errors = []
    audit.audit_descriptor_and_wave_contracts(errors)
    assert not errors, errors
    for index, (file_index, old, new) in enumerate(mutations, 1):
        name = paths[file_index]
        assert old in originals[name], old
        target = audit.ROOT / name
        target.write_text(originals[name].replace(old, new), encoding="utf-8")
        errors = []
        audit.audit_descriptor_and_wave_contracts(errors)
        target.write_text(originals[name], encoding="utf-8")
        assert errors, f"Mutation {index} escaped: {old}"
        print(f"M{index}: REJECTED: {errors[0]}")
print(f"DESCRIPTOR_WAVE_MUTATIONS: PASS ({len(mutations)} rejected)")
