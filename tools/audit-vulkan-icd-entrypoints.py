#!/usr/bin/env python3
"""Auditoria estatica dos entry points do ICD Vulkan da BC-250.

Carrega bc250_vulkan.dll e resolve, via vk_icdGetInstanceProcAddr, o conjunto
de comandos que o backend ggml-vulkan do llama.cpp precisa. Nao chama
vkCreateInstance e nao toca o hardware: o vkGetInstanceProcAddr do ICD e uma
tabela plana de nomes que ignora o parametro instance, portanto sondar com
instance=NULL e seguro e conclusivo.

Uso:
    python tools\\audit-vulkan-icd-entrypoints.py [caminho\\para\\bc250_vulkan.dll]

Saida: relatorio por grupo e codigo de saida 0 sempre que a DLL carrega. O
numero de resolvidos e o indicador a acompanhar entre candidatos; ele esta
registrado em ROADMAP_VULKAN_100.md.
"""
import ctypes
import os
import sys

DEFAULT_DLL = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "driver", "umd", "output", "bc250_vulkan.dll")

GROUPS = [
    ("Negociacao do loader", [
        "vk_icdNegotiateLoaderICDInterfaceVersion",
        "vk_icdGetInstanceProcAddr",
        "vk_icdGetPhysicalDeviceProcAddr",
        "vkGetInstanceProcAddr",
        "vkGetDeviceProcAddr",
        "vkEnumerateInstanceVersion",
        "vkEnumerateInstanceExtensionProperties",
        "vkEnumerateInstanceLayerProperties",
    ]),
    ("Instancia e enumeracao", [
        "vkCreateInstance",
        "vkDestroyInstance",
        "vkEnumeratePhysicalDevices",
        "vkEnumeratePhysicalDeviceGroups",
        "vkEnumerateDeviceExtensionProperties",
        "vkEnumerateDeviceLayerProperties",
    ]),
    ("Propriedades do dispositivo fisico", [
        "vkGetPhysicalDeviceProperties",
        "vkGetPhysicalDeviceProperties2",
        "vkGetPhysicalDeviceFeatures",
        "vkGetPhysicalDeviceFeatures2",
        "vkGetPhysicalDeviceMemoryProperties",
        "vkGetPhysicalDeviceMemoryProperties2",
        "vkGetPhysicalDeviceQueueFamilyProperties",
        "vkGetPhysicalDeviceQueueFamilyProperties2",
        "vkGetPhysicalDeviceFormatProperties",
    ]),
    ("Device e filas", [
        "vkCreateDevice",
        "vkDestroyDevice",
        "vkGetDeviceQueue",
        "vkGetDeviceQueue2",
        "vkDeviceWaitIdle",
        "vkQueueWaitIdle",
    ]),
    ("Memoria e buffers", [
        "vkAllocateMemory",
        "vkFreeMemory",
        "vkMapMemory",
        "vkUnmapMemory",
        "vkFlushMappedMemoryRanges",
        "vkInvalidateMappedMemoryRanges",
        "vkCreateBuffer",
        "vkDestroyBuffer",
        "vkGetBufferMemoryRequirements",
        "vkGetBufferMemoryRequirements2",
        "vkBindBufferMemory",
        "vkBindBufferMemory2",
        "vkCreateBufferView",
        "vkDestroyBufferView",
    ]),
    ("Descritores", [
        "vkCreateDescriptorSetLayout",
        "vkDestroyDescriptorSetLayout",
        "vkCreateDescriptorPool",
        "vkDestroyDescriptorPool",
        "vkResetDescriptorPool",
        "vkAllocateDescriptorSets",
        "vkFreeDescriptorSets",
        "vkUpdateDescriptorSets",
    ]),
    ("Pipeline de compute", [
        "vkCreateShaderModule",
        "vkDestroyShaderModule",
        "vkCreatePipelineLayout",
        "vkDestroyPipelineLayout",
        "vkCreatePipelineCache",
        "vkDestroyPipelineCache",
        "vkGetPipelineCacheData",
        "vkCreateComputePipelines",
        "vkDestroyPipeline",
    ]),
    ("Command buffers", [
        "vkCreateCommandPool",
        "vkDestroyCommandPool",
        "vkResetCommandPool",
        "vkAllocateCommandBuffers",
        "vkFreeCommandBuffers",
        "vkBeginCommandBuffer",
        "vkEndCommandBuffer",
        "vkResetCommandBuffer",
    ]),
    ("Comandos gravados no CB", [
        "vkCmdBindPipeline",
        "vkCmdBindDescriptorSets",
        "vkCmdPushConstants",
        "vkCmdDispatch",
        "vkCmdDispatchIndirect",
        "vkCmdPipelineBarrier",
        "vkCmdCopyBuffer",
        "vkCmdFillBuffer",
        "vkCmdUpdateBuffer",
        "vkCmdWriteTimestamp",
        "vkCmdResetQueryPool",
    ]),
    ("Sincronizacao", [
        "vkQueueSubmit",
        "vkCreateFence",
        "vkDestroyFence",
        "vkResetFences",
        "vkGetFenceStatus",
        "vkWaitForFences",
        "vkCreateSemaphore",
        "vkDestroySemaphore",
        "vkCreateEvent",
        "vkDestroyEvent",
        "vkGetEventStatus",
        "vkSetEvent",
        "vkResetEvent",
    ]),
    ("Query pool", [
        "vkCreateQueryPool",
        "vkDestroyQueryPool",
        "vkGetQueryPoolResults",
    ]),
]


def main(argv):
    dll_path = argv[1] if len(argv) > 1 else DEFAULT_DLL

    try:
        lib = ctypes.WinDLL(dll_path)
    except OSError as exc:
        print("FALHA ao carregar %s: %s" % (dll_path, exc))
        return 2

    try:
        gipa = lib.vk_icdGetInstanceProcAddr
    except AttributeError:
        print("FALHA: a DLL nao exporta vk_icdGetInstanceProcAddr")
        return 2

    gipa.restype = ctypes.c_void_p
    gipa.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

    print("=" * 78)
    print("AUDITORIA DE ENTRY POINTS DO ICD VULKAN BC-250")
    print("DLL: %s" % dll_path)
    print("=" * 78)

    total = 0
    resolved_total = 0
    for title, names in GROUPS:
        missing = []
        for name in names:
            if gipa(None, name.encode("ascii")):
                resolved_total += 1
            else:
                missing.append(name)
        total += len(names)
        print()
        print("[%s]  %d/%d resolvidos" % (title, len(names) - len(missing), len(names)))
        for name in missing:
            print("   AUSENTE  %s" % name)

    print()
    print("=" * 78)
    print("TOTAL: %d/%d resolvidos  |  %d AUSENTES"
          % (resolved_total, total, total - resolved_total))
    print("=" * 78)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
