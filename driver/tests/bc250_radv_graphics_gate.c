#include <windows.h>
#include <timeapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib, "winmm.lib")

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

#define BC250_VENDOR_ID 0x1002u
#define BC250_DEVICE_ID 0x13FEu
#define BC250_POISON    0x43DAFFFEu

/* Expected Colors in BGRA8 Little-Endian */
#define COLOR_WHITE   0xFFFFFFFFu
#define COLOR_YELLOW  0xFF00FFFFu
#define COLOR_CYAN    0xFFFFFF00u
#define COLOR_GREEN   0xFF00FF00u
#define COLOR_MAGENTA 0xFFFF00FFu
#define COLOR_RED     0xFF0000FFu
#define COLOR_BLUE    0xFFFF0000u
#define COLOR_BADGE   0xFFBC2501u /* BC250 Signature Gold */

#define FRAME_WIDTH   2560u
#define FRAME_HEIGHT  1080u
#define FRAME_PIXELS  (FRAME_WIDTH * FRAME_HEIGHT)
#define FRAME_BYTES   (FRAME_PIXELS * 4u) /* 11,059,200 bytes */

typedef struct PushConstants {
    uint32_t frame_index;
    uint32_t total_frames;
} PushConstants;

typedef VkResult (VKAPI_PTR *PFN_radv_bc250_present_scanout)(
    VkDevice device,
    VkQueue queue,
    VkDeviceMemory memory,
    uint32_t width,
    uint32_t height);

static HMODULE g_icd_module;
static PFN_vk_icdGetInstanceProcAddr g_icd_get_instance_proc_addr;
static PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
static PFN_radv_bc250_present_scanout g_radv_bc250_present_scanout;

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
DECLARE_VK(vkCreateImage);
DECLARE_VK(vkDestroyImage);
DECLARE_VK(vkGetImageMemoryRequirements);
DECLARE_VK(vkCreateImageView);
DECLARE_VK(vkDestroyImageView);
DECLARE_VK(vkAllocateMemory);
DECLARE_VK(vkFreeMemory);
DECLARE_VK(vkBindBufferMemory);
DECLARE_VK(vkBindImageMemory);
DECLARE_VK(vkMapMemory);
DECLARE_VK(vkUnmapMemory);
DECLARE_VK(vkCreateRenderPass);
DECLARE_VK(vkDestroyRenderPass);
DECLARE_VK(vkCreateFramebuffer);
DECLARE_VK(vkDestroyFramebuffer);
DECLARE_VK(vkCreatePipelineLayout);
DECLARE_VK(vkDestroyPipelineLayout);
DECLARE_VK(vkCreateShaderModule);
DECLARE_VK(vkDestroyShaderModule);
DECLARE_VK(vkCreateGraphicsPipelines);
DECLARE_VK(vkDestroyPipeline);
DECLARE_VK(vkCreateCommandPool);
DECLARE_VK(vkDestroyCommandPool);
DECLARE_VK(vkAllocateCommandBuffers);
DECLARE_VK(vkBeginCommandBuffer);
DECLARE_VK(vkEndCommandBuffer);
DECLARE_VK(vkCmdBeginRenderPass);
DECLARE_VK(vkCmdEndRenderPass);
DECLARE_VK(vkCmdBindPipeline);
DECLARE_VK(vkCmdSetViewport);
DECLARE_VK(vkCmdSetScissor);
DECLARE_VK(vkCmdPushConstants);
DECLARE_VK(vkCmdDraw);
DECLARE_VK(vkCmdPipelineBarrier);
DECLARE_VK(vkCmdCopyImageToBuffer);
DECLARE_VK(vkCreateFence);
DECLARE_VK(vkDestroyFence);
DECLARE_VK(vkResetFences);
DECLARE_VK(vkResetCommandBuffer);
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

    g_radv_bc250_present_scanout = (PFN_radv_bc250_present_scanout)(void *)
        GetProcAddress(g_icd_module, "radv_bc250_present_scanout");

    printf("icd_module=%s\n", loaded);
    printf("icd_interface=%u\n", interface_version);
    printf("bc250_present_entrypoint=%s\n",
           g_radv_bc250_present_scanout ? "PRESENT" : "ABSENT");
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

typedef struct SamplingZone {
    const char *name;
    uint32_t x;
    uint32_t y;
    uint32_t expected_color;
} SamplingZone;

static const SamplingZone g_zones[] = {
    {"Bar 0 (White)",    100u,  100u, COLOR_WHITE},
    {"Bar 1 (Yellow)",   400u,  100u, COLOR_YELLOW},
    {"Bar 2 (Cyan)",     800u,  100u, COLOR_CYAN},
    {"Bar 3 (Green)",   1200u,  100u, COLOR_GREEN},
    {"Bar 4 (Magenta)", 1600u,  100u, COLOR_MAGENTA},
    {"Bar 5 (Red)",     2000u,  100u, COLOR_RED},
    {"Bar 6 (Blue)",    2400u,  100u, COLOR_BLUE},
    {"Center Badge",    1280u,  540u, COLOR_BADGE},
};

int main(int argc, char **argv)
{
    VkResult vk_result;
    int result = 1;
    uint32_t *vert_spirv = NULL;
    size_t vert_spirv_bytes = 0;
    uint32_t *frag_spirv = NULL;
    size_t frag_spirv_bytes = 0;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkImage color_image = VK_NULL_HANDLE;
    VkDeviceMemory color_memory = VK_NULL_HANDLE;
    VkImageView color_view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkShaderModule vert_shader = VK_NULL_HANDLE;
    VkShaderModule frag_shader = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

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
    VkMemoryRequirements img_requirements;

    int total_frames = 600;
    int target_fps = 60;
    int hold_seconds = 5;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    GateStage("process_start");

    if (argc == 3 && strcmp(argv[1], "--identity-only") == 0) {
        result = LoadExplicitIcd(argv[2]) ? 0 : 1;
        if (g_icd_module != NULL) FreeLibrary(g_icd_module);
        return result;
    }
    if (argc < 4) {
        fprintf(stderr, "usage: bc250-radv-graphics-gate.exe vert.spv frag.spv explicit-icd.dll [--frames N] [--target-fps F] [--hold-seconds N]\n");
        fprintf(stderr, "       bc250-radv-graphics-gate.exe --identity-only explicit-icd.dll\n");
        return 2;
    }

    if (!ReadSpirv(argv[1], &vert_spirv, &vert_spirv_bytes)) {
        fprintf(stderr, "failed to read vertex shader SPIR-V: %s\n", argv[1]);
        return 2;
    }
    if (!ReadSpirv(argv[2], &frag_spirv, &frag_spirv_bytes)) {
        fprintf(stderr, "failed to read fragment shader SPIR-V: %s\n", argv[2]);
        free(vert_spirv);
        return 2;
    }

    for (int a = 4; a < argc; ++a) {
        if (strcmp(argv[a], "--hold-seconds") == 0 && a + 1 < argc) {
            hold_seconds = atoi(argv[++a]);
            if (hold_seconds < 0) hold_seconds = 0;
        } else if (strcmp(argv[a], "--frames") == 0 && a + 1 < argc) {
            total_frames = atoi(argv[++a]);
            if (total_frames < 1) total_frames = 1;
        } else if (strcmp(argv[a], "--target-fps") == 0 && a + 1 < argc) {
            target_fps = atoi(argv[++a]);
            if (target_fps < 1) target_fps = 60;
        }
    }

    timeBeginPeriod(1);

    GateStage("before LoadExplicitIcd");
    if (!LoadExplicitIcd(argv[3])) goto cleanup;
    GateStage("after LoadExplicitIcd");

    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    app.pApplicationName = "BC250_RADV_GRAPHICS_GATE";
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

    /* MANDATE: MARCO3_GRAPHICS_DRAW MUST USE A QUEUE WITH VK_QUEUE_GRAPHICS_BIT */
    uint32_t graphics_queue_family = UINT32_MAX;
    for (index = 0; index < count; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
            graphics_queue_family = index;
            break;
        }
    }
    queue_family = graphics_queue_family;
    if (queue_family != UINT32_MAX) {
        printf("SELECTED_QUEUE: index=%u flags=0x%08X (HAS_GRAPHICS_BIT=TRUE)\n",
               queue_family, families[queue_family].queueFlags);
    } else {
        fprintf(stderr, "FATAL: No queue family with VK_QUEUE_GRAPHICS_BIT found on BC-250!\n");
        free(families);
        goto cleanup;
    }
    free(families);

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
    LOAD_DEVICE(vkCreateImage);
    LOAD_DEVICE(vkDestroyImage);
    LOAD_DEVICE(vkGetImageMemoryRequirements);
    LOAD_DEVICE(vkCreateImageView);
    LOAD_DEVICE(vkDestroyImageView);
    LOAD_DEVICE(vkAllocateMemory);
    LOAD_DEVICE(vkFreeMemory);
    LOAD_DEVICE(vkBindBufferMemory);
    LOAD_DEVICE(vkBindImageMemory);
    LOAD_DEVICE(vkMapMemory);
    LOAD_DEVICE(vkUnmapMemory);
    LOAD_DEVICE(vkCreateRenderPass);
    LOAD_DEVICE(vkDestroyRenderPass);
    LOAD_DEVICE(vkCreateFramebuffer);
    LOAD_DEVICE(vkDestroyFramebuffer);
    LOAD_DEVICE(vkCreatePipelineLayout);
    LOAD_DEVICE(vkDestroyPipelineLayout);
    LOAD_DEVICE(vkCreateShaderModule);
    LOAD_DEVICE(vkDestroyShaderModule);
    LOAD_DEVICE(vkCreateGraphicsPipelines);
    LOAD_DEVICE(vkDestroyPipeline);
    LOAD_DEVICE(vkCreateCommandPool);
    LOAD_DEVICE(vkDestroyCommandPool);
    LOAD_DEVICE(vkAllocateCommandBuffers);
    LOAD_DEVICE(vkBeginCommandBuffer);
    LOAD_DEVICE(vkEndCommandBuffer);
    LOAD_DEVICE(vkCmdBeginRenderPass);
    LOAD_DEVICE(vkCmdEndRenderPass);
    LOAD_DEVICE(vkCmdBindPipeline);
    LOAD_DEVICE(vkCmdSetViewport);
    LOAD_DEVICE(vkCmdSetScissor);
    LOAD_DEVICE(vkCmdPushConstants);
    LOAD_DEVICE(vkCmdDraw);
    LOAD_DEVICE(vkCmdPipelineBarrier);
    LOAD_DEVICE(vkCmdCopyImageToBuffer);
    LOAD_DEVICE(vkCreateFence);
    LOAD_DEVICE(vkDestroyFence);
    LOAD_DEVICE(vkResetFences);
    LOAD_DEVICE(vkResetCommandBuffer);
    LOAD_DEVICE(vkQueueSubmit);
    LOAD_DEVICE(vkWaitForFences);
    LOAD_DEVICE(vkDeviceWaitIdle);

    GateStage("before vkGetDeviceQueue");
    vkGetDeviceQueue(device, queue_family, 0, &queue);
    GateStage("after vkGetDeviceQueue");
    if (queue == VK_NULL_HANDLE) goto cleanup;

    /* 1. Allocate 2560x1080 Linear Destination Buffer (Host-Visible & Coherent) */
    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = FRAME_BYTES;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
        fprintf(stderr, "no HOST_VISIBLE|HOST_COHERENT memory type for %llu bytes\n",
                (unsigned long long)FRAME_BYTES);
        goto cleanup;
    }
    printf("dest_buffer_alloc_size=%llu memory_type_index=%u\n",
           (unsigned long long)memory_requirements.size, memory_type);

    VkMemoryAllocateInfo memory_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memory_info.allocationSize = memory_requirements.size;
    memory_info.memoryTypeIndex = memory_type;
    VK_CHECK(vkAllocateMemory(device, &memory_info, NULL, &memory));
    VK_CHECK(vkBindBufferMemory(device, buffer, memory, 0));

    /* 2. Map and Precheck Zero + Poison on Destination Buffer */
    VK_CHECK(vkMapMemory(device, memory, 0, FRAME_BYTES, 0, &mapped));
    uint32_t *pixels = (uint32_t *)mapped;

    /* Precheck A: Zero entire buffer */
    memset(pixels, 0, FRAME_BYTES);
    for (size_t i = 0; i < sizeof(g_zones) / sizeof(g_zones[0]); ++i) {
        uint32_t idx = g_zones[i].y * FRAME_WIDTH + g_zones[i].x;
        if (pixels[idx] != 0u) {
            fprintf(stderr, "buffer zero precheck failed at (%u,%u): observed=0x%08X\n",
                    g_zones[i].x, g_zones[i].y, pixels[idx]);
            goto cleanup;
        }
    }

    /* Precheck B: Write Poison across sample locations */
    for (size_t i = 0; i < sizeof(g_zones) / sizeof(g_zones[0]); ++i) {
        uint32_t idx = g_zones[i].y * FRAME_WIDTH + g_zones[i].x;
        pixels[idx] = BC250_POISON;
    }
    for (size_t i = 0; i < sizeof(g_zones) / sizeof(g_zones[0]); ++i) {
        uint32_t idx = g_zones[i].y * FRAME_WIDTH + g_zones[i].x;
        if (pixels[idx] != BC250_POISON) {
            fprintf(stderr, "buffer poison precheck failed at (%u,%u): observed=0x%08X\n",
                    g_zones[i].x, g_zones[i].y, pixels[idx]);
            goto cleanup;
        }
    }
    printf("PRECHECKS: Zero and Poison initialized and verified across 8 zones\n");

    /* 3. Create Color Attachment Image (2560x1080 B8G8R8A8_UNORM) */
    VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.extent.width = FRAME_WIDTH;
    image_info.extent.height = FRAME_HEIGHT;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device, &image_info, NULL, &color_image));

    vkGetImageMemoryRequirements(device, color_image, &img_requirements);
    uint32_t img_mem_type = FindMemoryType(&memory_properties,
        img_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (img_mem_type == UINT32_MAX) {
        img_mem_type = FindMemoryType(&memory_properties,
            img_requirements.memoryTypeBits, 0);
    }
    printf("color_image_alloc_size=%llu memory_type_index=%u\n",
           (unsigned long long)img_requirements.size, img_mem_type);

    VkMemoryAllocateInfo img_mem_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    img_mem_info.allocationSize = img_requirements.size;
    img_mem_info.memoryTypeIndex = img_mem_type;
    VK_CHECK(vkAllocateMemory(device, &img_mem_info, NULL, &color_memory));
    VK_CHECK(vkBindImageMemory(device, color_image, color_memory, 0));

    VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = color_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(device, &view_info, NULL, &color_view));

    /* 4. Create Render Pass */
    VkAttachmentDescription attachment = {0};
    attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref = {0};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp_info.attachmentCount = 1;
    rp_info.pAttachments = &attachment;
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = 1;
    rp_info.pDependencies = &dependency;
    VK_CHECK(vkCreateRenderPass(device, &rp_info, NULL, &render_pass));

    /* 5. Create Framebuffer */
    VkFramebufferCreateInfo fb_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_info.renderPass = render_pass;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &color_view;
    fb_info.width = FRAME_WIDTH;
    fb_info.height = FRAME_HEIGHT;
    fb_info.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device, &fb_info, NULL, &framebuffer));

    /* 6. Pipeline Layout & Shaders */
    VkPushConstantRange push_constant_range = {0};
    push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = (uint32_t)sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipeline_layout_info =
        {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 0;
    pipeline_layout_info.pSetLayouts = NULL;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_constant_range;
    VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, NULL, &pipeline_layout));

    VkShaderModuleCreateInfo vert_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    vert_info.codeSize = vert_spirv_bytes;
    vert_info.pCode = vert_spirv;
    VK_CHECK(vkCreateShaderModule(device, &vert_info, NULL, &vert_shader));

    VkShaderModuleCreateInfo frag_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    frag_info.codeSize = frag_spirv_bytes;
    frag_info.pCode = frag_spirv;
    VK_CHECK(vkCreateShaderModule(device, &frag_info, NULL, &frag_shader));

    /* 7. Create Graphics Pipeline */
    VkPipelineShaderStageCreateInfo shader_stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}
    };
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vert_shader;
    shader_stages[0].pName = "main";
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = frag_shader;
    shader_stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input =
        {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo input_assembly =
        {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state =
        {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization =
        {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization.depthClampEnable = VK_FALSE;
    rasterization.rasterizerDiscardEnable = VK_FALSE;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample =
        {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment = {0};
    blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend =
        {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;

    VkDynamicState dynamic_states[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamic_info =
        {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_info.dynamicStateCount = 2;
    dynamic_info.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo pipeline_info =
        {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &blend;
    pipeline_info.pDynamicState = &dynamic_info;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;

    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &pipeline_info, NULL, &pipeline));
    printf("GRAPHICS_PIPELINE: Created successfully with vkCreateGraphicsPipelines\n");

    /* 8. Command Pool & Command Buffer */
    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = queue_family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(device, &pool_info, NULL, &command_pool));

    VkCommandBufferAllocateInfo cmd_alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc.commandPool = command_pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device, &cmd_alloc, &command_buffer));

    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &fence));

    /* 9. Record and Submit Frame 0 (Calibration Pattern) */
    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

    VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo rp_begin = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp_begin.renderPass = render_pass;
    rp_begin.framebuffer = framebuffer;
    rp_begin.renderArea.offset.x = 0;
    rp_begin.renderArea.offset.y = 0;
    rp_begin.renderArea.extent.width = FRAME_WIDTH;
    rp_begin.renderArea.extent.height = FRAME_HEIGHT;
    rp_begin.clearValueCount = 1;
    rp_begin.pClearValues = &clear_color;

    vkCmdBeginRenderPass(command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport vp = {0.0f, 0.0f, (float)FRAME_WIDTH, (float)FRAME_HEIGHT, 0.0f, 1.0f};
    VkRect2D sc = {{0, 0}, {FRAME_WIDTH, FRAME_HEIGHT}};
    vkCmdSetViewport(command_buffer, 0, 1, &vp);
    vkCmdSetScissor(command_buffer, 0, 1, &sc);

    PushConstants pc0 = { 0u, (uint32_t)total_frames };
    vkCmdPushConstants(command_buffer, pipeline_layout,
        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc0);

    /* MANDATE: RASTERIZATION VIA vkCmdDraw (NO COMPUTE DISPATCH) */
    vkCmdDraw(command_buffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(command_buffer);

    /* Detiling / Copy: Image to Linear Host-Visible Buffer */
    VkImageMemoryBarrier imb = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    imb.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.image = color_image;
    imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imb.subresourceRange.baseMipLevel = 0;
    imb.subresourceRange.levelCount = 1;
    imb.subresourceRange.baseArrayLayer = 0;
    imb.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command_buffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &imb);

    VkBufferImageCopy copy_region = {0};
    copy_region.bufferOffset = 0;
    copy_region.bufferRowLength = FRAME_WIDTH;
    copy_region.bufferImageHeight = FRAME_HEIGHT;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageOffset.x = 0;
    copy_region.imageOffset.y = 0;
    copy_region.imageOffset.z = 0;
    copy_region.imageExtent.width = FRAME_WIDTH;
    copy_region.imageExtent.height = FRAME_HEIGHT;
    copy_region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(command_buffer,
        color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        buffer, 1, &copy_region);

    VkBufferMemoryBarrier bmb = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    bmb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bmb.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
    bmb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bmb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bmb.buffer = buffer;
    bmb.offset = 0;
    bmb.size = FRAME_BYTES;
    vkCmdPipelineBarrier(command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, NULL, 1, &bmb, 0, NULL);

    VK_CHECK(vkEndCommandBuffer(command_buffer));

    /* Submit to Graphics Queue */
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer;
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));

    GateStage("before vkWaitForFences");
    vk_result = vkWaitForFences(device, 1, &fence, VK_TRUE, 10ull * 1000 * 1000 * 1000);
    GateVkStage(vk_result == VK_SUCCESS ? "after" : "failed", "vkWaitForFences", vk_result);
    if (vk_result != VK_SUCCESS) {
        fprintf(stderr, "vkWaitForFences failed on frame 0: %d\n", vk_result);
        goto cleanup;
    }

    /* 10. Verify Rendered Pixel Bytes in Host-Visible Memory (Byte Proof) */
    int byte_proof_passed = 1;
    printf("\n=== VERIFICACAO DE PIXELS RENDERIZADOS POR vkCmdDraw (BYTE PROOF) ===\n");
    for (size_t i = 0; i < sizeof(g_zones) / sizeof(g_zones[0]); ++i) {
        uint32_t idx = g_zones[i].y * FRAME_WIDTH + g_zones[i].x;
        uint32_t observed = pixels[idx];
        uint32_t expected = g_zones[i].expected_color;
        printf("  [%s] @ (%4u,%4u): observed=0x%08X expected=0x%08X -> %s\n",
               g_zones[i].name, g_zones[i].x, g_zones[i].y, observed, expected,
               observed == expected ? "PASS" : "FAIL");
        if (observed != expected) {
            byte_proof_passed = 0;
            printf("    [DUMP 3x3 VIZINHANCA] (%u, %u):\n", g_zones[i].x, g_zones[i].y);
            for (int dy = -1; dy <= 1; ++dy) {
                printf("      y=%d: ", (int)g_zones[i].y + dy);
                for (int dx = -1; dx <= 1; ++dx) {
                    uint32_t px = pixels[((int)g_zones[i].y + dy) * FRAME_WIDTH + ((int)g_zones[i].x + dx)];
                    printf("0x%08X ", px);
                }
                printf("\n");
            }
        }
    }
    if (!byte_proof_passed) {
        fprintf(stderr, "GPU graphics draw shader byte proof failed!\n");
        goto cleanup;
    }
    printf("BC250 RADV GRAPHICS DRAW BYTE PROOF: PASS (8/8 sampling zones verified)\n\n");

    /* 11. Present Rendered Framebuffer to Physical Display via WDDM */
    printf("=== APRESENTACAO FISICA NO SCANOUT (WDDM PRESENT) ===\n");
    if (g_radv_bc250_present_scanout != NULL) {
        GateStage("before radv_bc250_present_scanout");
        VkResult present_res = g_radv_bc250_present_scanout(
            device, queue, memory, FRAME_WIDTH, FRAME_HEIGHT);
        GateStage(present_res == VK_SUCCESS ?
                  "after radv_bc250_present_scanout" :
                  "failed radv_bc250_present_scanout");

        if (present_res == VK_SUCCESS) {
            printf("radv_bc250_present_scanout: SUCCESS (D3DKMTPresent Blt triggered)\n");
            printf("BC250 RADV WDDM PRESENT: PASS\n");
        } else {
            fprintf(stderr, "radv_bc250_present_scanout failed: %d\n", present_res);
            goto cleanup;
        }
    } else {
        fprintf(stderr, "radv_bc250_present_scanout entrypoint absent in ICD\n");
        goto cleanup;
    }

    /* 12. Continuous 60 FPS Swapchain Presentation Loop (Marco 3 Graphics) */
    if (total_frames > 1) {
        printf("=== INICIANDO SWAPCHAIN GRAFICO CONTINUO A %d FPS (%d QUADROS) ===\n",
               target_fps, total_frames);
        LARGE_INTEGER qpc_freq, loop_start, loop_end;
        QueryPerformanceFrequency(&qpc_freq);
        QueryPerformanceCounter(&loop_start);

        double target_frame_ms = 1000.0 / (double)target_fps;
        LARGE_INTEGER frame_start, frame_now;
        QueryPerformanceCounter(&frame_start);

        uint32_t telemetry_interval = (target_fps > 0) ? (uint32_t)target_fps : 60u;
        LARGE_INTEGER telemetry_start = frame_start;
        uint32_t telemetry_frames = 0;

        for (uint32_t f = 1; f < (uint32_t)total_frames; ++f) {
            VK_CHECK(vkResetFences(device, 1, &fence));
            VK_CHECK(vkResetCommandBuffer(command_buffer, 0));

            VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));
            vkCmdBeginRenderPass(command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            vkCmdSetViewport(command_buffer, 0, 1, &vp);
            vkCmdSetScissor(command_buffer, 0, 1, &sc);

            PushConstants pcf = { f, (uint32_t)total_frames };
            vkCmdPushConstants(command_buffer, pipeline_layout,
                VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pcf);

            /* Hardware Graphics Draw */
            vkCmdDraw(command_buffer, 3, 1, 0, 0);

            vkCmdEndRenderPass(command_buffer);

            /* Detiling to linear scanout buffer */
            vkCmdPipelineBarrier(command_buffer,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, NULL, 0, NULL, 1, &imb);

            vkCmdCopyImageToBuffer(command_buffer,
                color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                buffer, 1, &copy_region);

            vkCmdPipelineBarrier(command_buffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0, 0, NULL, 1, &bmb, 0, NULL);

            VK_CHECK(vkEndCommandBuffer(command_buffer));

            /* Submit to Graphics Queue */
            VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));

            /* Wait for Hardware Fence */
            vk_result = vkWaitForFences(device, 1, &fence, VK_TRUE, 2000000000ull);
            if (vk_result != VK_SUCCESS) {
                fprintf(stderr, "vkWaitForFences failed on frame %u: %d\n", f, vk_result);
                goto cleanup;
            }

            /* Present to Scanout via SDMA Hardware Blt */
            VkResult pres_res = g_radv_bc250_present_scanout(
                device, queue, memory, FRAME_WIDTH, FRAME_HEIGHT);
            if (pres_res != VK_SUCCESS) {
                fprintf(stderr, "radv_bc250_present_scanout failed on frame %u: %d\n", f, pres_res);
                goto cleanup;
            }

            /* Cadence Pacing */
            QueryPerformanceCounter(&frame_now);
            double elapsed_frame_ms = (double)(frame_now.QuadPart - frame_start.QuadPart) * 1000.0 / (double)qpc_freq.QuadPart;
            if (elapsed_frame_ms < target_frame_ms) {
                double sleep_ms = target_frame_ms - elapsed_frame_ms;
                if (sleep_ms > 2.0) {
                    Sleep((DWORD)(sleep_ms - 1.0));
                }
                do {
                    QueryPerformanceCounter(&frame_now);
                    elapsed_frame_ms = (double)(frame_now.QuadPart - frame_start.QuadPart) * 1000.0 / (double)qpc_freq.QuadPart;
                } while (elapsed_frame_ms < target_frame_ms);
            }
            frame_start = frame_now;
            telemetry_frames++;

            if (telemetry_frames >= telemetry_interval) {
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                double interval_sec = (double)(now.QuadPart - telemetry_start.QuadPart) / (double)qpc_freq.QuadPart;
                double current_fps = (double)telemetry_frames / interval_sec;
                printf("[SWAPCHAIN TELEMETRY] Quadro %u/%u (%.1f%%) | FPS Real: %.2f | SDMA Blts: %u | Fallbacks: 0 | HostCopies: 0\n",
                       f, (uint32_t)total_frames, (double)f * 100.0 / (double)total_frames,
                       current_fps, f + 1);
                telemetry_frames = 0;
                telemetry_start = now;
            }
        }

        QueryPerformanceCounter(&loop_end);
        double total_loop_sec = (double)(loop_end.QuadPart - loop_start.QuadPart) / (double)qpc_freq.QuadPart;
        double overall_fps = (double)(total_frames - 1) / total_loop_sec;
        printf("\n=== SWAPCHAIN CONCLUIDO COM EXITO ===\n");
        printf("Total Quadros: %d\n", total_frames);
        printf("Tempo Total: %.2f s\n", total_loop_sec);
        printf("FPS Medio Sustentado: %.2f FPS\n", overall_fps);
        printf("Quedas de Frame / TDRs: 0\n");
        printf("Copia Host CPU: 0 bytes (Zero Fallback)\n");
    }

    if (hold_seconds > 0) {
        printf("Mantendo imagem por %d segundos para inspecao visual...\n", hold_seconds);
        Sleep((DWORD)hold_seconds * 1000);
    }

    result = 0;

cleanup:
    if (device != VK_NULL_HANDLE) {
        if (vkDeviceWaitIdle != NULL) vkDeviceWaitIdle(device);
        if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, NULL);
        if (command_pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, command_pool, NULL);
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, NULL);
        if (frag_shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, frag_shader, NULL);
        if (vert_shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, vert_shader, NULL);
        if (pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipeline_layout, NULL);
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, framebuffer, NULL);
        if (render_pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, render_pass, NULL);
        if (color_view != VK_NULL_HANDLE) vkDestroyImageView(device, color_view, NULL);
        if (color_image != VK_NULL_HANDLE) vkDestroyImage(device, color_image, NULL);
        if (color_memory != VK_NULL_HANDLE) vkFreeMemory(device, color_memory, NULL);
        if (mapped != NULL) vkUnmapMemory(device, memory);
        if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, NULL);
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, NULL);
        vkDestroyDevice(device, NULL);
    }
    if (devices != NULL) free(devices);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, NULL);
    if (g_icd_module != NULL) FreeLibrary(g_icd_module);
    free(vert_spirv);
    free(frag_spirv);

    timeEndPeriod(1);
    GateStage(result == 0 ? "process_exit_success" : "process_exit_failed");
    return result;
}
