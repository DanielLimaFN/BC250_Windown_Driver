#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../driver/umd/bc250_vulkan.h"

/* GFX10 compute shader bytecode: s_endpgm */
static const uint32_t s_vkTestShader[4] = {
    0xBF810000u, /* s_endpgm */
    0xBF800000u, /* s_nop */
    0xBF800000u, /* s_nop */
    0xBF800000u  /* s_nop */
};

int main(void)
{
    VkResult res;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t physDevCount = 0;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceMemoryProperties memProps;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *mappedData = NULL;

    printf("================================================================================\n");
    printf("   AMD BC-250 — VULKAN 1.1 ICD COMPUTE TEST HARNESS                             \n");
    printf("================================================================================\n\n");

    /* 1. Criar Instancia Vulkan */
    printf("[1/8] Criando VkInstance...\n");
    VkInstanceCreateInfo instInfo;
    memset(&instInfo, 0, sizeof(instInfo));
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    res = vkCreateInstance(&instInfo, NULL, &instance);
    if (res != VK_SUCCESS) {
        printf("  [-] Falha ao criar VkInstance (Resultado: %d).\n", res);
        printf("  [-] Verifique se o driver/servico de kernel esta acessivel.\n");
        return 1;
    }
    printf("  [+] VkInstance criada com sucesso!\n\n");

    /* 2. Enumerar Physical Devices */
    printf("[2/8] Enumerando Physical Devices (GPUs)...\n");
    res = vkEnumeratePhysicalDevices(instance, &physDevCount, NULL);
    if (res != VK_SUCCESS || physDevCount == 0) {
        printf("  [!] Nenhuma GPU Vulkan encontrada.\n");
        vkDestroyInstance(instance, NULL);
        return 1;
    }
    res = vkEnumeratePhysicalDevices(instance, &physDevCount, &physicalDevice);
    printf("  [+] Encontrada(s) %u GPU(s) compativel(is)!\n\n", physDevCount);

    /* 3. Consultar Propriedades e Memoria do BC-250 */
    printf("[3/8] Consultando propriedades do silicio BC-250...\n");
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    printf("      Dispositivo:        %s\n", props.deviceName);
    printf("      Vendor ID:          0x%04X (AMD)\n", props.vendorID);
    printf("      Device ID:          0x%04X (BC-250)\n", props.deviceID);
    printf("      Driver Version:     %u.%u.%u\n",
           props.driverVersion >> 22, (props.driverVersion >> 12) & 0x3FF, props.driverVersion & 0xFFF);
    printf("      VRAM Heap 0:        %llu MiB (GDDR6)\n",
           memProps.memoryHeaps[0].size / (1024ull * 1024ull));
    printf("      Max Compute LDS:    %u bytes\n", props.limits.maxComputeSharedMemorySize);
    printf("      Max Workgroup Size: [%u, %u, %u]\n\n",
           props.limits.maxComputeWorkGroupSize[0],
           props.limits.maxComputeWorkGroupSize[1],
           props.limits.maxComputeWorkGroupSize[2]);

    /* 4. Criar Logical Device e Fila de Computacao */
    printf("[4/8] Criando VkDevice e VkQueue de computacao...\n");
    VkDeviceCreateInfo devInfo;
    memset(&devInfo, 0, sizeof(devInfo));
    devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    res = vkCreateDevice(physicalDevice, &devInfo, NULL, &device);
    if (res != VK_SUCCESS) {
        printf("  [!] Falha ao criar VkDevice (Resultado: %d).\n", res);
        vkDestroyInstance(instance, NULL);
        return 1;
    }
    vkGetDeviceQueue(device, 0, 0, &queue);
    printf("  [+] VkDevice e VkQueue (Compute MEC2) instanciados!\n\n");

    /* 5. Alocar Buffer em VRAM e Mapear */
    printf("[5/8] Alocando VkBuffer (4096 bytes) em VRAM...\n");
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = 4096;
    bufInfo.usage = 0x00000020; /* VK_BUFFER_USAGE_STORAGE_BUFFER_BIT */
    vkCreateBuffer(device, &bufInfo, NULL, &buffer);

    VkMemoryAllocateInfo allocInfo;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = 4096;
    allocInfo.memoryTypeIndex = 1; /* Host Visible VRAM */
    vkAllocateMemory(device, &allocInfo, NULL, &memory);
    vkBindBufferMemory(device, buffer, memory, 0);

    vkMapMemory(device, memory, 0, 4096, 0, &mappedData);
    if (mappedData != NULL) {
        uint32_t *pU32 = (uint32_t *)mappedData;
        pU32[0] = 0xBC250C01u;
        printf("  [+] Buffer mapeado na VRAM, payload inicializado: 0x%08X\n\n", pU32[0]);
    }
    vkUnmapMemory(device, memory);

    /* 6. Criar Shader Module e Pipeline de Computacao */
    printf("[6/8] Criando VkShaderModule e VkComputePipeline (GFX10)...\n");
    VkShaderModuleCreateInfo smInfo;
    memset(&smInfo, 0, sizeof(smInfo));
    smInfo.sType = 16; /* VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO */
    smInfo.codeSize = sizeof(s_vkTestShader);
    smInfo.pCode = s_vkTestShader;
    vkCreateShaderModule(device, &smInfo, NULL, &shaderModule);

    VkComputePipelineCreateInfo pipeInfo;
    memset(&pipeInfo, 0, sizeof(pipeInfo));
    pipeInfo.sType = 29; /* VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO */
    pipeInfo.stage.module = shaderModule;
    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, NULL, &pipeline);
    printf("  [+] Pipeline de computacao pronto!\n\n");

    /* 7. Gravar e Submeter Command Buffer */
    printf("[7/8] Gravando Command Buffer e submetendo a VkQueue (vkQueueSubmit)...\n");
    VkCommandPoolCreateInfo cpInfo;
    memset(&cpInfo, 0, sizeof(cpInfo));
    cpInfo.sType = 39; /* VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO */
    vkCreateCommandPool(device, &cpInfo, NULL, &cmdPool);

    VkCommandBufferAllocateInfo cbInfo;
    memset(&cbInfo, 0, sizeof(cbInfo));
    cbInfo.sType = 40; /* VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO */
    cbInfo.commandPool = cmdPool;
    cbInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cbInfo, &cmdBuffer);

    vkBeginCommandBuffer(cmdBuffer, NULL);
    vkCmdBindPipeline(cmdBuffer, 1 /* VK_PIPELINE_BIND_POINT_COMPUTE */, pipeline);
    vkCmdDispatch(cmdBuffer, 1, 1, 1);
    vkEndCommandBuffer(cmdBuffer);

    VkFenceCreateInfo fInfo;
    memset(&fInfo, 0, sizeof(fInfo));
    fInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(device, &fInfo, NULL, &fence);

    VkSubmitInfo submitInfo;
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    res = vkQueueSubmit(queue, 1, &submitInfo, fence);

    if (res == VK_SUCCESS) {
        printf("  [+] vkQueueSubmit executado com sucesso no Vulkan ICD!\n\n");
    } else {
        printf("  [-] vkQueueSubmit retornou resultado: %d\n\n", res);
    }

    /* 8. Teardown Limpo */
    printf("[8/8] Liberando recursos Vulkan...\n");
    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, cmdPool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyShaderModule(device, shaderModule, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    printf("================================================================================\n");
    printf("   TESTE DO VULKAN ICD FINALIZADO COM EXITO!\n");
    printf("================================================================================\n");
    return 0;
}
