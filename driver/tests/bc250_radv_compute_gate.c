#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

#define BC250_VENDOR_ID 0x1002u
#define BC250_DEVICE_ID 0x13FEu
#define BC250_POISON 0x43DAFFFEu
#define BC250_RESULT 0xBC250C01u

static HMODULE g_icd_module;
static PFN_vk_icdGetInstanceProcAddr g_icd_get_instance_proc_addr;
static PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;

static void GateStage(const char *stage)
{
    char path[32768];
    FILE *file = NULL;
    DWORD length = GetEnvironmentVariableA(
        "BC250_GATE_STAGE_FILE", path, ARRAYSIZE(path));

    if (length == 0 || length >= ARRAYSIZE(path)) return;
    if (fopen_s(&file, path, "ab") != 0 || file == NULL) return;
    fprintf(file, "%llu pid=%lu %s\r\n",
        (unsigned long long)GetTickCount64(), GetCurrentProcessId(), stage);
    fflush(file);
    fclose(file);
}

static void GateVkStage(const char *edge, const char *call, VkResult value)
{
    char line[1024];
    if (value == VK_SUCCESS) {
        (void)sprintf_s(line, sizeof(line), "%s %s", edge, call);
    } else {
        (void)sprintf_s(line, sizeof(line), "%s %s result=%d", edge, call, value);
    }
    GateStage(line);
}

#define DECLARE_VK(Name) static PFN_##Name Name
DECLARE_VK(vkCreateInstance);
DECLARE_VK(vkDestroyInstance);
DECLARE_VK(vkEnumeratePhysicalDevices);
DECLARE_VK(vkGetPhysicalDeviceProperties);
DECLARE_VK(vkGetPhysicalDeviceQueueFamilyProperties);
DECLARE_VK(vkGetPhysicalDeviceMemoryProperties);
DECLARE_VK(vkCreateDevice);
DECLARE_VK(vkDestroyDevice);
DECLARE_VK(vkGetDeviceQueue);
DECLARE_VK(vkCreateBuffer);
DECLARE_VK(vkDestroyBuffer);
DECLARE_VK(vkGetBufferMemoryRequirements);
DECLARE_VK(vkAllocateMemory);
DECLARE_VK(vkFreeMemory);
DECLARE_VK(vkBindBufferMemory);
DECLARE_VK(vkMapMemory);
DECLARE_VK(vkUnmapMemory);
DECLARE_VK(vkCreateDescriptorSetLayout);
DECLARE_VK(vkDestroyDescriptorSetLayout);
DECLARE_VK(vkCreatePipelineLayout);
DECLARE_VK(vkDestroyPipelineLayout);
DECLARE_VK(vkCreateShaderModule);
DECLARE_VK(vkDestroyShaderModule);
DECLARE_VK(vkCreateComputePipelines);
DECLARE_VK(vkDestroyPipeline);
DECLARE_VK(vkCreateDescriptorPool);
DECLARE_VK(vkDestroyDescriptorPool);
DECLARE_VK(vkAllocateDescriptorSets);
DECLARE_VK(vkUpdateDescriptorSets);
DECLARE_VK(vkCreateCommandPool);
DECLARE_VK(vkDestroyCommandPool);
DECLARE_VK(vkAllocateCommandBuffers);
DECLARE_VK(vkBeginCommandBuffer);
DECLARE_VK(vkEndCommandBuffer);
DECLARE_VK(vkCmdBindPipeline);
DECLARE_VK(vkCmdBindDescriptorSets);
DECLARE_VK(vkCmdDispatch);
DECLARE_VK(vkCreateFence);
DECLARE_VK(vkDestroyFence);
DECLARE_VK(vkQueueSubmit);
DECLARE_VK(vkWaitForFences);
DECLARE_VK(vkDeviceWaitIdle);
#undef DECLARE_VK

static int LoadExplicitIcd(const char *path)
{
    char requested[MAX_PATH];
    char loaded[MAX_PATH];
    DWORD requested_length;
    DWORD loaded_length;
    PFN_vk_icdNegotiateLoaderICDInterfaceVersion negotiate;
    uint32_t interface_version = CURRENT_LOADER_ICD_INTERFACE_VERSION;
    VkResult negotiate_result;

    requested_length = GetFullPathNameA(path, ARRAYSIZE(requested), requested, NULL);
    if (requested_length == 0 || requested_length >= ARRAYSIZE(requested)) {
        fprintf(stderr, "invalid ICD path: %s\n", path);
        return 0;
    }
    g_icd_module = LoadLibraryExA(requested, NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (g_icd_module == NULL) {
        fprintf(stderr, "LoadLibraryExA ICD failed: path=%s win32=%lu\n",
            requested, GetLastError());
        return 0;
    }
    loaded_length = GetModuleFileNameA(g_icd_module, loaded, ARRAYSIZE(loaded));
    if (loaded_length == 0 || loaded_length >= ARRAYSIZE(loaded) ||
        _stricmp(requested, loaded) != 0) {
        fprintf(stderr, "ICD identity mismatch: requested=%s loaded=%s\n",
            requested, loaded_length == 0 ? "<unknown>" : loaded);
        return 0;
    }

    negotiate = (PFN_vk_icdNegotiateLoaderICDInterfaceVersion)(void *)
        GetProcAddress(g_icd_module, "vk_icdNegotiateLoaderICDInterfaceVersion");
    g_icd_get_instance_proc_addr = (PFN_vk_icdGetInstanceProcAddr)(void *)
        GetProcAddress(g_icd_module, "vk_icdGetInstanceProcAddr");
    if (negotiate == NULL || g_icd_get_instance_proc_addr == NULL) {
        fprintf(stderr, "ICD loader entrypoints absent: path=%s\n", loaded);
        return 0;
    }
    negotiate_result = negotiate(&interface_version);
    if (negotiate_result != VK_SUCCESS || interface_version < 2u ||
        interface_version > CURRENT_LOADER_ICD_INTERFACE_VERSION) {
        fprintf(stderr, "ICD interface negotiation failed: result=%d version=%u\n",
            negotiate_result, interface_version);
        return 0;
    }

    vkCreateInstance = (PFN_vkCreateInstance)
        g_icd_get_instance_proc_addr(VK_NULL_HANDLE, "vkCreateInstance");
    if (vkCreateInstance == NULL) {
        fprintf(stderr, "ICD vkCreateInstance entrypoint absent\n");
        return 0;
    }
    printf("icd_module=%s\n", loaded);
    printf("icd_interface=%u\n", interface_version);
    return 1;
}

#define LOAD_INSTANCE(Name) do { \
    Name = (PFN_##Name)g_icd_get_instance_proc_addr(instance, #Name); \
    if (Name == NULL) { \
        fprintf(stderr, "ICD instance entrypoint absent: %s\n", #Name); \
        goto cleanup; \
    } \
} while (0)

#define LOAD_DEVICE(Name) do { \
    Name = (PFN_##Name)vkGetDeviceProcAddr(device, #Name); \
    if (Name == NULL) { \
        fprintf(stderr, "ICD device entrypoint absent: %s\n", #Name); \
        goto cleanup; \
    } \
} while (0)

#define VK_CHECK(Call) do { \
    GateVkStage("before", #Call, VK_SUCCESS); \
    VkResult check_result = (Call); \
    GateVkStage(check_result == VK_SUCCESS ? "after" : "failed", \
        #Call, check_result); \
    if (check_result != VK_SUCCESS) { \
        fprintf(stderr, "%s failed: %d\n", #Call, check_result); \
        result = 1; \
        goto cleanup; \
    } \
} while (0)

static int ReadSpirv(const char *path, uint32_t **words, size_t *bytes)
{
    FILE *file = NULL;
    long length;
    size_t read_count;

    *words = NULL;
    *bytes = 0;
    if (fopen_s(&file, path, "rb") != 0 || file == NULL) return 0;
    if (fseek(file, 0, SEEK_END) != 0) goto failed;
    length = ftell(file);
    if (length <= 0 || (length & 3) != 0) goto failed;
    if (fseek(file, 0, SEEK_SET) != 0) goto failed;
    *words = (uint32_t *)malloc((size_t)length);
    if (*words == NULL) goto failed;
    read_count = fread(*words, 1, (size_t)length, file);
    if (read_count != (size_t)length) goto failed;
    fclose(file);
    *bytes = (size_t)length;
    return 1;

failed:
    if (file != NULL) fclose(file);
    free(*words);
    *words = NULL;
    return 0;
}

static uint32_t FindMemoryType(
    const VkPhysicalDeviceMemoryProperties *properties,
    uint32_t allowed,
    VkMemoryPropertyFlags required)
{
    uint32_t index;
    for (index = 0; index < properties->memoryTypeCount; ++index) {
        if ((allowed & (1u << index)) != 0u &&
            (properties->memoryTypes[index].propertyFlags & required) == required) {
            return index;
        }
    }
    return UINT32_MAX;
}

int main(int argc, char **argv)
{
    VkResult vk_result;
    int result = 1;
    uint32_t *spirv = NULL;
    size_t spirv_bytes = 0;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *mapped = NULL;
    uint32_t queue_family = UINT32_MAX;
    uint32_t memory_type;
    uint32_t count = 0;
    uint32_t index;
    VkPhysicalDevice *devices = NULL;
    VkPhysicalDeviceMemoryProperties memory_properties;
    VkMemoryRequirements memory_requirements;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    GateStage("process_start");
    if (argc == 3 && strcmp(argv[1], "--identity-only") == 0) {
        result = LoadExplicitIcd(argv[2]) ? 0 : 1;
        if (g_icd_module != NULL) FreeLibrary(g_icd_module);
        return result;
    }
    if (argc != 3 || !ReadSpirv(argv[1], &spirv, &spirv_bytes)) {
        fprintf(stderr, "usage: bc250-radv-compute-gate.exe shader.spv explicit-icd.dll\n");
        fprintf(stderr, "       bc250-radv-compute-gate.exe --identity-only explicit-icd.dll\n");
        return 2;
    }
    GateStage("before LoadExplicitIcd");
    if (!LoadExplicitIcd(argv[2])) goto cleanup;
    GateStage("after LoadExplicitIcd");

    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    app.pApplicationName = "BC250_RADV_COMPUTE_TRIPOD";
    app.apiVersion = VK_API_VERSION_1_1;
    instance_info.pApplicationInfo = &app;
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    LOAD_INSTANCE(vkDestroyInstance);
    LOAD_INSTANCE(vkEnumeratePhysicalDevices);
    LOAD_INSTANCE(vkGetPhysicalDeviceProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD_INSTANCE(vkGetPhysicalDeviceMemoryProperties);
    LOAD_INSTANCE(vkCreateDevice);
    LOAD_INSTANCE(vkGetDeviceProcAddr);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    if (count == 0) goto cleanup;
    devices = (VkPhysicalDevice *)calloc(count, sizeof(*devices));
    if (devices == NULL) goto cleanup;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
    for (index = 0; index < count; ++index) {
        VkPhysicalDeviceProperties properties;
        GateStage("before vkGetPhysicalDeviceProperties");
        vkGetPhysicalDeviceProperties(devices[index], &properties);
        GateStage("after vkGetPhysicalDeviceProperties");
        if (properties.vendorID == BC250_VENDOR_ID &&
            properties.deviceID == BC250_DEVICE_ID) {
            physical = devices[index];
            printf("adapter=%s vendor=0x%04X device=0x%04X\n",
                properties.deviceName, properties.vendorID, properties.deviceID);
            break;
        }
    }
    if (physical == VK_NULL_HANDLE) {
        fprintf(stderr, "BC-250 1002:13FE not exposed by selected ICD\n");
        goto cleanup;
    }

    GateStage("before vkGetPhysicalDeviceQueueFamilyProperties(count)");
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    GateStage("after vkGetPhysicalDeviceQueueFamilyProperties(count)");
    VkQueueFamilyProperties *families =
        (VkQueueFamilyProperties *)calloc(count, sizeof(*families));
    if (families == NULL) goto cleanup;
    GateStage("before vkGetPhysicalDeviceQueueFamilyProperties(data)");
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    GateStage("after vkGetPhysicalDeviceQueueFamilyProperties(data)");
    for (index = 0; index < count; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u) {
            queue_family = index;
            break;
        }
    }
    free(families);
    if (queue_family == UINT32_MAX) goto cleanup;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    VK_CHECK(vkCreateDevice(physical, &device_info, NULL, &device));
    LOAD_DEVICE(vkDestroyDevice);
    LOAD_DEVICE(vkGetDeviceQueue);
    LOAD_DEVICE(vkCreateBuffer);
    LOAD_DEVICE(vkDestroyBuffer);
    LOAD_DEVICE(vkGetBufferMemoryRequirements);
    LOAD_DEVICE(vkAllocateMemory);
    LOAD_DEVICE(vkFreeMemory);
    LOAD_DEVICE(vkBindBufferMemory);
    LOAD_DEVICE(vkMapMemory);
    LOAD_DEVICE(vkUnmapMemory);
    LOAD_DEVICE(vkCreateDescriptorSetLayout);
    LOAD_DEVICE(vkDestroyDescriptorSetLayout);
    LOAD_DEVICE(vkCreatePipelineLayout);
    LOAD_DEVICE(vkDestroyPipelineLayout);
    LOAD_DEVICE(vkCreateShaderModule);
    LOAD_DEVICE(vkDestroyShaderModule);
    LOAD_DEVICE(vkCreateComputePipelines);
    LOAD_DEVICE(vkDestroyPipeline);
    LOAD_DEVICE(vkCreateDescriptorPool);
    LOAD_DEVICE(vkDestroyDescriptorPool);
    LOAD_DEVICE(vkAllocateDescriptorSets);
    LOAD_DEVICE(vkUpdateDescriptorSets);
    LOAD_DEVICE(vkCreateCommandPool);
    LOAD_DEVICE(vkDestroyCommandPool);
    LOAD_DEVICE(vkAllocateCommandBuffers);
    LOAD_DEVICE(vkBeginCommandBuffer);
    LOAD_DEVICE(vkEndCommandBuffer);
    LOAD_DEVICE(vkCmdBindPipeline);
    LOAD_DEVICE(vkCmdBindDescriptorSets);
    LOAD_DEVICE(vkCmdDispatch);
    LOAD_DEVICE(vkCreateFence);
    LOAD_DEVICE(vkDestroyFence);
    LOAD_DEVICE(vkQueueSubmit);
    LOAD_DEVICE(vkWaitForFences);
    LOAD_DEVICE(vkDeviceWaitIdle);
    GateStage("before vkGetDeviceQueue");
    vkGetDeviceQueue(device, queue_family, 0, &queue);
    GateStage("after vkGetDeviceQueue");
    if (queue == VK_NULL_HANDLE) goto cleanup;

    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = 4096;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
    GateStage("before vkGetBufferMemoryRequirements");
    vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);
    GateStage("after vkGetBufferMemoryRequirements");
    GateStage("before vkGetPhysicalDeviceMemoryProperties");
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    GateStage("after vkGetPhysicalDeviceMemoryProperties");
    memory_type = FindMemoryType(&memory_properties,
        memory_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX) {
        fprintf(stderr, "no HOST_VISIBLE|HOST_COHERENT memory type\n");
        goto cleanup;
    }
    VkMemoryAllocateInfo memory_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memory_info.allocationSize = memory_requirements.size;
    memory_info.memoryTypeIndex = memory_type;
    VK_CHECK(vkAllocateMemory(device, &memory_info, NULL, &memory));
    VK_CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    VK_CHECK(vkMapMemory(device, memory, 0, 4096, 0, &mapped));
    *(volatile uint32_t *)mapped = BC250_POISON;

    VkDescriptorSetLayoutBinding binding = {0};
    VkDescriptorSetLayoutCreateInfo descriptor_layout_info =
        {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    descriptor_layout_info.bindingCount = 1;
    descriptor_layout_info.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptor_layout_info,
        NULL, &descriptor_layout));

    VkPipelineLayoutCreateInfo pipeline_layout_info =
        {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_layout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info,
        NULL, &pipeline_layout));

    VkShaderModuleCreateInfo shader_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader_info.codeSize = spirv_bytes;
    shader_info.pCode = spirv;
    VK_CHECK(vkCreateShaderModule(device, &shader_info, NULL, &shader));
    VkComputePipelineCreateInfo pipeline_info =
        {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = shader;
    pipeline_info.stage.pName = "main";
    pipeline_info.layout = pipeline_layout;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
        &pipeline_info, NULL, &pipeline));

    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, NULL, &descriptor_pool));
    VkDescriptorSetAllocateInfo set_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_info.descriptorPool = descriptor_pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &descriptor_layout;
    VK_CHECK(vkAllocateDescriptorSets(device, &set_info, &descriptor_set));
    VkDescriptorBufferInfo descriptor_buffer = {buffer, 0, 4096};
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptor_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &descriptor_buffer;
    GateStage("before vkUpdateDescriptorSets");
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
    GateStage("after vkUpdateDescriptorSets");

    VkCommandPoolCreateInfo command_pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    command_pool_info.queueFamilyIndex = queue_family;
    VK_CHECK(vkCreateCommandPool(device, &command_pool_info, NULL, &command_pool));
    VkCommandBufferAllocateInfo command_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_info.commandPool = command_pool;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device, &command_info, &command_buffer));
    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));
    GateStage("before command_recording");
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline_layout, 0, 1, &descriptor_set, 0, NULL);
    vkCmdDispatch(command_buffer, 1, 1, 1);
    GateStage("after command_recording");
    VK_CHECK(vkEndCommandBuffer(command_buffer));
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer;
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));
    GateStage("before vkWaitForFences");
    vk_result = vkWaitForFences(device, 1, &fence, VK_TRUE, 10ull * 1000 * 1000 * 1000);
    GateVkStage(vk_result == VK_SUCCESS ? "after" : "failed",
        "vkWaitForFences", vk_result);
    if (vk_result != VK_SUCCESS) {
        fprintf(stderr, "vkWaitForFences failed: %d\n", vk_result);
        goto cleanup;
    }
    printf("poison=0x%08X observed=0x%08X expected=0x%08X\n",
        BC250_POISON, *(volatile uint32_t *)mapped, BC250_RESULT);
    if (*(volatile uint32_t *)mapped != BC250_RESULT) {
        fprintf(stderr, "GPU byte proof failed\n");
        goto cleanup;
    }
    printf("BC250 RADV COMPUTE BYTE PROOF: PASS\n");
    result = 0;

cleanup:
    GateStage("cleanup_enter");
    if (device != VK_NULL_HANDLE && vkDeviceWaitIdle != NULL) {
        GateStage("before cleanup vkDeviceWaitIdle");
        (void)vkDeviceWaitIdle(device);
        GateStage("after cleanup vkDeviceWaitIdle");
    }
    if (mapped != NULL) vkUnmapMemory(device, memory);
    if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, NULL);
    if (command_pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, command_pool, NULL);
    if (descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, NULL);
    if (shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, shader, NULL);
    if (pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    if (descriptor_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptor_layout, NULL);
    if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, NULL);
    if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, NULL);
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, NULL);
    free(devices);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, NULL);
    if (g_icd_module != NULL) FreeLibrary(g_icd_module);
    free(spirv);
    GateStage("process_exit");
    return result;
}

#undef LOAD_INSTANCE
#undef LOAD_DEVICE
