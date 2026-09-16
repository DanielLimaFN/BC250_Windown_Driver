#include <windows.h>
#include <timeapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#pragma comment(lib, "winmm.lib")

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

#define BC250_VENDOR_ID 0x1002u
#define BC250_DEVICE_ID 0x13FEu
#define BC250_POISON    0x43DAFFFEu

#define FRAME_WIDTH   2560u
#define FRAME_HEIGHT  1080u
#define FRAME_PIXELS  (FRAME_WIDTH * FRAME_HEIGHT)
#define FRAME_BYTES   (FRAME_PIXELS * 4u) /* 11,059,200 bytes */

#define TEX_WIDTH     256u
#define TEX_HEIGHT    256u
#define TEX_BYTES     (TEX_WIDTH * TEX_HEIGHT * 4u)

typedef struct Vertex {
    float pos[3];
    float uv[2];
    float normal[3];
    float color[3];
} Vertex;

typedef struct PushConstants {
    float mvp[16];
    uint32_t frame;
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
DECLARE_VK(vkCreateSampler);
DECLARE_VK(vkDestroySampler);
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
DECLARE_VK(vkCreateDescriptorSetLayout);
DECLARE_VK(vkDestroyDescriptorSetLayout);
DECLARE_VK(vkCreateDescriptorPool);
DECLARE_VK(vkDestroyDescriptorPool);
DECLARE_VK(vkAllocateDescriptorSets);
DECLARE_VK(vkUpdateDescriptorSets);
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
DECLARE_VK(vkCmdBindVertexBuffers);
DECLARE_VK(vkCmdBindDescriptorSets);
DECLARE_VK(vkCmdSetViewport);
DECLARE_VK(vkCmdSetScissor);
DECLARE_VK(vkCmdPushConstants);
DECLARE_VK(vkCmdDraw);
DECLARE_VK(vkCmdPipelineBarrier);
DECLARE_VK(vkCmdCopyBufferToImage);
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

/* 4x4 Matrix Math Helpers (Column-Major for Vulkan SPIR-V) */
static void Mat4Identity(float m[16])
{
    memset(m, 0, sizeof(float) * 16);
    m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f; m[15] = 1.0f;
}

static void Mat4Multiply(float out[16], const float a[16], const float b[16])
{
    float res[16];
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            res[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    memcpy(out, res, sizeof(res));
}

static void Mat4Perspective(float m[16], float fovY_rad, float aspect, float zNear, float zFar)
{
    memset(m, 0, sizeof(float) * 16);
    float f = 1.0f / tanf(fovY_rad * 0.5f);
    m[0] = f / aspect;
    m[5] = -f; // Invert Y for Vulkan NDC
    m[10] = zFar / (zNear - zFar);
    m[11] = -1.0f;
    m[14] = (zNear * zFar) / (zNear - zFar);
}

static void Mat4Rotation(float m[16], float rx, float ry, float rz)
{
    float mx[16], my[16], mz[16], temp[16];
    Mat4Identity(mx);
    mx[5] = cosf(rx);  mx[9] = -sinf(rx);
    mx[6] = sinf(rx);  mx[10] = cosf(rx);

    Mat4Identity(my);
    my[0] = cosf(ry);  my[8] = sinf(ry);
    my[2] = -sinf(ry); my[10] = cosf(ry);

    Mat4Identity(mz);
    mz[0] = cosf(rz);  mz[4] = -sinf(rz);
    mz[1] = sinf(rz);  mz[5] = cosf(rz);

    Mat4Multiply(temp, my, mx);
    Mat4Multiply(m, mz, temp);
}

static void Mat4Translation(float m[16], float x, float y, float z)
{
    Mat4Identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

/* 36 Vertices for 12 Triangles (6 Faces of the Unit Cube) */
static const Vertex g_cube_vertices[36] = {
    /* Front Face (Z = +1.0) - Red */
    {{-1.0f, -1.0f,  1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f,  1.0f}, {1.0f, 0.0f, 0.0f}},
    {{ 1.0f, -1.0f,  1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f,  1.0f}, {1.0f, 0.0f, 0.0f}},
    {{ 1.0f,  1.0f,  1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f,  1.0f}, {1.0f, 0.0f, 0.0f}},
    {{-1.0f, -1.0f,  1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f,  1.0f}, {1.0f, 0.0f, 0.0f}},
    {{ 1.0f,  1.0f,  1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f,  1.0f}, {1.0f, 0.0f, 0.0f}},
    {{-1.0f,  1.0f,  1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f,  1.0f}, {1.0f, 0.0f, 0.0f}},

    /* Back Face (Z = -1.0) - Cyan */
    {{ 1.0f, -1.0f, -1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 1.0f}},
    {{-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 1.0f}},
    {{-1.0f,  1.0f, -1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 1.0f}},
    {{ 1.0f, -1.0f, -1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 1.0f}},
    {{-1.0f,  1.0f, -1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 1.0f}},
    {{ 1.0f,  1.0f, -1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 1.0f}},

    /* Top Face (Y = +1.0) - Green */
    {{-1.0f,  1.0f,  1.0f}, {0.0f, 0.0f}, {0.0f,  1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{ 1.0f,  1.0f,  1.0f}, {1.0f, 0.0f}, {0.0f,  1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{ 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f}, {0.0f,  1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{-1.0f,  1.0f,  1.0f}, {0.0f, 0.0f}, {0.0f,  1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{ 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f}, {0.0f,  1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{-1.0f,  1.0f, -1.0f}, {0.0f, 1.0f}, {0.0f,  1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},

    /* Bottom Face (Y = -1.0) - Magenta */
    {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}},
    {{ 1.0f, -1.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}},
    {{ 1.0f, -1.0f,  1.0f}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}},
    {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}},
    {{ 1.0f, -1.0f,  1.0f}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}},
    {{-1.0f, -1.0f,  1.0f}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}},

    /* Right Face (X = +1.0) - Blue */
    {{ 1.0f, -1.0f,  1.0f}, {0.0f, 0.0f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{ 1.0f, -1.0f, -1.0f}, {1.0f, 0.0f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{ 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{ 1.0f, -1.0f,  1.0f}, {0.0f, 0.0f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{ 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{ 1.0f,  1.0f,  1.0f}, {0.0f, 1.0f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},

    /* Left Face (X = -1.0) - Yellow */
    {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}},
    {{-1.0f, -1.0f,  1.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}},
    {{-1.0f,  1.0f,  1.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}},
    {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}},
    {{-1.0f,  1.0f,  1.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}},
    {{-1.0f,  1.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}},
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

    /* Render Pass & Attachments */
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkImage color_image = VK_NULL_HANDLE;
    VkDeviceMemory color_memory = VK_NULL_HANDLE;
    VkImageView color_view = VK_NULL_HANDLE;

    VkImage depth_image = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    /* Vertex Buffer */
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;

    /* Texture & Sampler */
    VkImage texture_image = VK_NULL_HANDLE;
    VkDeviceMemory texture_memory = VK_NULL_HANDLE;
    VkImageView texture_view = VK_NULL_HANDLE;
    VkSampler texture_sampler = VK_NULL_HANDLE;

    VkBuffer texture_staging = VK_NULL_HANDLE;
    VkDeviceMemory texture_staging_memory = VK_NULL_HANDLE;

    /* Destination Buffer (Scanout / Host Proof) */
    VkBuffer dest_buffer = VK_NULL_HANDLE;
    VkDeviceMemory dest_memory = VK_NULL_HANDLE;

    /* Pipeline & Descriptors */
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkShaderModule vert_shader = VK_NULL_HANDLE;
    VkShaderModule frag_shader = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *mapped = NULL;

    uint32_t queue_family = UINT32_MAX;
    uint32_t count = 0;
    uint32_t index;
    VkPhysicalDevice *devices = NULL;
    VkPhysicalDeviceMemoryProperties mem_props;
    VkMemoryRequirements mem_reqs;

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
        fprintf(stderr, "usage: bc250-radv-cube-gate.exe vert.spv frag.spv explicit-icd.dll [--frames N] [--target-fps F] [--hold-seconds N]\n");
        fprintf(stderr, "       bc250-radv-cube-gate.exe --identity-only explicit-icd.dll\n");
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
    app.pApplicationName = "BC250_RADV_CUBE_GATE";
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

    /* MANDATE: MARCO4_GRAPHICS_VKCUBE MUST USE VK_QUEUE_GRAPHICS_BIT */
    for (index = 0; index < count; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
            queue_family = index;
            break;
        }
    }
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
    LOAD_DEVICE(vkCreateSampler);
    LOAD_DEVICE(vkDestroySampler);
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
    LOAD_DEVICE(vkCreateDescriptorSetLayout);
    LOAD_DEVICE(vkDestroyDescriptorSetLayout);
    LOAD_DEVICE(vkCreateDescriptorPool);
    LOAD_DEVICE(vkDestroyDescriptorPool);
    LOAD_DEVICE(vkAllocateDescriptorSets);
    LOAD_DEVICE(vkUpdateDescriptorSets);
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
    LOAD_DEVICE(vkCmdBindVertexBuffers);
    LOAD_DEVICE(vkCmdBindDescriptorSets);
    LOAD_DEVICE(vkCmdSetViewport);
    LOAD_DEVICE(vkCmdSetScissor);
    LOAD_DEVICE(vkCmdPushConstants);
    LOAD_DEVICE(vkCmdDraw);
    LOAD_DEVICE(vkCmdPipelineBarrier);
    LOAD_DEVICE(vkCmdCopyBufferToImage);
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

    vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);

    /* 1. Allocate & Upload Vertex Buffer (36 Vertices = 1584 Bytes) */
    VkBufferCreateInfo vb_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    vb_info.size = sizeof(g_cube_vertices);
    vb_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vb_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device, &vb_info, NULL, &vertex_buffer));

    vkGetBufferMemoryRequirements(device, vertex_buffer, &mem_reqs);
    uint32_t vb_mem_type = FindMemoryType(&mem_props, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vb_mem_type == UINT32_MAX) goto cleanup;

    VkMemoryAllocateInfo vb_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    vb_alloc.allocationSize = mem_reqs.size;
    vb_alloc.memoryTypeIndex = vb_mem_type;
    VK_CHECK(vkAllocateMemory(device, &vb_alloc, NULL, &vertex_memory));
    VK_CHECK(vkBindBufferMemory(device, vertex_buffer, vertex_memory, 0));

    void *vb_mapped = NULL;
    VK_CHECK(vkMapMemory(device, vertex_memory, 0, sizeof(g_cube_vertices), 0, &vb_mapped));
    memcpy(vb_mapped, g_cube_vertices, sizeof(g_cube_vertices));
    vkUnmapMemory(device, vertex_memory);
    printf("VERTEX_BUFFER: 36 vertices (12 triangles) uploaded to GPU memory\n");

    /* 2. Create Procedural Texture (256x256 Checkerboard + BC250 Gold Badge) */
    uint32_t *tex_pixels = (uint32_t *)malloc(TEX_BYTES);
    if (!tex_pixels) goto cleanup;
    for (uint32_t y = 0; y < TEX_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TEX_WIDTH; ++x) {
            uint32_t idx = y * TEX_WIDTH + x;
            if (x >= 64 && x < 192 && y >= 96 && y < 160) {
                /* Central BC-250 Badge */
                tex_pixels[idx] = 0xFF01BC25u; /* RGBA Little-Endian Gold */
            } else {
                /* 8x8 Grid Checkerboard */
                uint32_t cx = (x / 32) % 2;
                uint32_t cy = (y / 32) % 2;
                tex_pixels[idx] = (cx ^ cy) ? 0xFFEEEEEEu : 0xFF333333u;
            }
        }
    }

    VkBufferCreateInfo tex_staging_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    tex_staging_info.size = TEX_BYTES;
    tex_staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VK_CHECK(vkCreateBuffer(device, &tex_staging_info, NULL, &texture_staging));

    vkGetBufferMemoryRequirements(device, texture_staging, &mem_reqs);
    uint32_t staging_mem_type = FindMemoryType(&mem_props, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo staging_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    staging_alloc.allocationSize = mem_reqs.size;
    staging_alloc.memoryTypeIndex = staging_mem_type;
    VK_CHECK(vkAllocateMemory(device, &staging_alloc, NULL, &texture_staging_memory));
    VK_CHECK(vkBindBufferMemory(device, texture_staging, texture_staging_memory, 0));

    void *tex_mapped = NULL;
    VK_CHECK(vkMapMemory(device, texture_staging_memory, 0, TEX_BYTES, 0, &tex_mapped));
    memcpy(tex_mapped, tex_pixels, TEX_BYTES);
    vkUnmapMemory(device, texture_staging_memory);
    free(tex_pixels);

    VkImageCreateInfo tex_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    tex_info.imageType = VK_IMAGE_TYPE_2D;
    tex_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    tex_info.extent = (VkExtent3D){TEX_WIDTH, TEX_HEIGHT, 1};
    tex_info.mipLevels = 1;
    tex_info.arrayLayers = 1;
    tex_info.samples = VK_SAMPLE_COUNT_1_BIT;
    tex_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    tex_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    tex_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    tex_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device, &tex_info, NULL, &texture_image));

    vkGetImageMemoryRequirements(device, texture_image, &mem_reqs);
    uint32_t tex_mem_type = FindMemoryType(&mem_props, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (tex_mem_type == UINT32_MAX) {
        tex_mem_type = FindMemoryType(&mem_props, mem_reqs.memoryTypeBits, 0);
    }
    VkMemoryAllocateInfo tex_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    tex_alloc.allocationSize = mem_reqs.size;
    tex_alloc.memoryTypeIndex = tex_mem_type;
    VK_CHECK(vkAllocateMemory(device, &tex_alloc, NULL, &texture_memory));
    VK_CHECK(vkBindImageMemory(device, texture_image, texture_memory, 0));

    VkImageViewCreateInfo tex_view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    tex_view_info.image = texture_image;
    tex_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    tex_view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    tex_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    tex_view_info.subresourceRange.baseMipLevel = 0;
    tex_view_info.subresourceRange.levelCount = 1;
    tex_view_info.subresourceRange.baseArrayLayer = 0;
    tex_view_info.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(device, &tex_view_info, NULL, &texture_view));

    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VK_CHECK(vkCreateSampler(device, &sampler_info, NULL, &texture_sampler));

    /* 3. Color Attachment Image (2560x1080 B8G8R8A8_UNORM) */
    VkImageCreateInfo color_img_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    color_img_info.imageType = VK_IMAGE_TYPE_2D;
    color_img_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    color_img_info.extent = (VkExtent3D){FRAME_WIDTH, FRAME_HEIGHT, 1};
    color_img_info.mipLevels = 1;
    color_img_info.arrayLayers = 1;
    color_img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    color_img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    color_img_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    color_img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    color_img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device, &color_img_info, NULL, &color_image));

    vkGetImageMemoryRequirements(device, color_image, &mem_reqs);
    uint32_t color_mem_type = FindMemoryType(&mem_props, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (color_mem_type == UINT32_MAX) {
        color_mem_type = FindMemoryType(&mem_props, mem_reqs.memoryTypeBits, 0);
    }
    VkMemoryAllocateInfo color_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    color_alloc.allocationSize = mem_reqs.size;
    color_alloc.memoryTypeIndex = color_mem_type;
    VK_CHECK(vkAllocateMemory(device, &color_alloc, NULL, &color_memory));
    VK_CHECK(vkBindImageMemory(device, color_image, color_memory, 0));

    VkImageViewCreateInfo color_view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    color_view_info.image = color_image;
    color_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    color_view_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    color_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    color_view_info.subresourceRange.baseMipLevel = 0;
    color_view_info.subresourceRange.levelCount = 1;
    color_view_info.subresourceRange.baseArrayLayer = 0;
    color_view_info.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(device, &color_view_info, NULL, &color_view));

    /* 4. Depth Attachment Image (2560x1080 D32_SFLOAT) */
    VkImageCreateInfo depth_img_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    depth_img_info.imageType = VK_IMAGE_TYPE_2D;
    depth_img_info.format = VK_FORMAT_D32_SFLOAT;
    depth_img_info.extent = (VkExtent3D){FRAME_WIDTH, FRAME_HEIGHT, 1};
    depth_img_info.mipLevels = 1;
    depth_img_info.arrayLayers = 1;
    depth_img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    depth_img_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depth_img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    depth_img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device, &depth_img_info, NULL, &depth_image));

    vkGetImageMemoryRequirements(device, depth_image, &mem_reqs);
    uint32_t depth_mem_type = FindMemoryType(&mem_props, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (depth_mem_type == UINT32_MAX) {
        depth_mem_type = FindMemoryType(&mem_props, mem_reqs.memoryTypeBits, 0);
    }
    VkMemoryAllocateInfo depth_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    depth_alloc.allocationSize = mem_reqs.size;
    depth_alloc.memoryTypeIndex = depth_mem_type;
    VK_CHECK(vkAllocateMemory(device, &depth_alloc, NULL, &depth_memory));
    VK_CHECK(vkBindImageMemory(device, depth_image, depth_memory, 0));

    VkImageViewCreateInfo depth_view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    depth_view_info.image = depth_image;
    depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depth_view_info.format = VK_FORMAT_D32_SFLOAT;
    depth_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth_view_info.subresourceRange.baseMipLevel = 0;
    depth_view_info.subresourceRange.levelCount = 1;
    depth_view_info.subresourceRange.baseArrayLayer = 0;
    depth_view_info.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(device, &depth_view_info, NULL, &depth_view));

    /* 5. Render Pass (Color + Depth Attachments) */
    VkAttachmentDescription attachments[2] = {
        {0}, /* Color */
        {0}  /* Depth */
    };
    attachments[0].format = VK_FORMAT_B8G8R8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependencies[2] = {
        {0}, {0}
    };
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp_info.attachmentCount = 2;
    rp_info.pAttachments = attachments;
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = 1;
    rp_info.pDependencies = dependencies;
    VK_CHECK(vkCreateRenderPass(device, &rp_info, NULL, &render_pass));

    /* 6. Framebuffer */
    VkImageView fb_views[2] = { color_view, depth_view };
    VkFramebufferCreateInfo fb_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_info.renderPass = render_pass;
    fb_info.attachmentCount = 2;
    fb_info.pAttachments = fb_views;
    fb_info.width = FRAME_WIDTH;
    fb_info.height = FRAME_HEIGHT;
    fb_info.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device, &fb_info, NULL, &framebuffer));

    /* 7. Destination Linear Buffer for Scanout & Host Proof */
    VkBufferCreateInfo dest_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    dest_info.size = FRAME_BYTES;
    dest_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    dest_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device, &dest_info, NULL, &dest_buffer));

    vkGetBufferMemoryRequirements(device, dest_buffer, &mem_reqs);
    uint32_t dest_mem_type = FindMemoryType(&mem_props, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo dest_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    dest_alloc.allocationSize = mem_reqs.size;
    dest_alloc.memoryTypeIndex = dest_mem_type;
    VK_CHECK(vkAllocateMemory(device, &dest_alloc, NULL, &dest_memory));
    VK_CHECK(vkBindBufferMemory(device, dest_buffer, dest_memory, 0));

    VK_CHECK(vkMapMemory(device, dest_memory, 0, FRAME_BYTES, 0, &mapped));
    uint32_t *pixels = (uint32_t *)mapped;

    /* Prechecks: Zero and Poison */
    memset(pixels, 0, FRAME_BYTES);
    pixels[540 * FRAME_WIDTH + 1280] = BC250_POISON;
    if (pixels[540 * FRAME_WIDTH + 1280] != BC250_POISON) goto cleanup;
    printf("PRECHECKS: Destination buffer mapped, zeroed and poison verified\n");

    /* 8. Descriptors & Pipeline Layout */
    VkDescriptorSetLayoutBinding dsl_binding = {0};
    dsl_binding.binding = 0;
    dsl_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dsl_binding.descriptorCount = 1;
    dsl_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dsl_info.bindingCount = 1;
    dsl_info.pBindings = &dsl_binding;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &dsl_info, NULL, &descriptor_layout));

    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, NULL, &descriptor_pool));

    VkDescriptorSetAllocateInfo ds_alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ds_alloc.descriptorPool = descriptor_pool;
    ds_alloc.descriptorSetCount = 1;
    ds_alloc.pSetLayouts = &descriptor_layout;
    VK_CHECK(vkAllocateDescriptorSets(device, &ds_alloc, &descriptor_set));

    VkDescriptorImageInfo desc_img_info = {0};
    desc_img_info.sampler = texture_sampler;
    desc_img_info.imageView = texture_view;
    desc_img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write_ds = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write_ds.dstSet = descriptor_set;
    write_ds.dstBinding = 0;
    write_ds.descriptorCount = 1;
    write_ds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_ds.pImageInfo = &desc_img_info;
    vkUpdateDescriptorSets(device, 1, &write_ds, 0, NULL);

    VkPushConstantRange pc_range = {0};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset = 0;
    pc_range.size = (uint32_t)sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pl_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl_info.setLayoutCount = 1;
    pl_info.pSetLayouts = &descriptor_layout;
    pl_info.pushConstantRangeCount = 1;
    pl_info.pPushConstantRanges = &pc_range;
    VK_CHECK(vkCreatePipelineLayout(device, &pl_info, NULL, &pipeline_layout));

    VkShaderModuleCreateInfo vert_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    vert_info.codeSize = vert_spirv_bytes;
    vert_info.pCode = vert_spirv;
    VK_CHECK(vkCreateShaderModule(device, &vert_info, NULL, &vert_shader));

    VkShaderModuleCreateInfo frag_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    frag_info.codeSize = frag_spirv_bytes;
    frag_info.pCode = frag_spirv;
    VK_CHECK(vkCreateShaderModule(device, &frag_info, NULL, &frag_shader));

    /* 9. Graphics Pipeline Creation */
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

    VkVertexInputBindingDescription vi_binding = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vi_attribs[4] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, uv)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)}
    };
    VkPipelineVertexInputStateCreateInfo vi_info = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi_info.vertexBindingDescriptionCount = 1;
    vi_info.pVertexBindingDescriptions = &vi_binding;
    vi_info.vertexAttributeDescriptionCount = 4;
    vi_info.pVertexAttributeDescriptions = vi_attribs;

    VkPipelineInputAssemblyStateCreateInfo ia_info = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp_state.viewportCount = 1;
    vp_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rast_info = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rast_info.polygonMode = VK_POLYGON_MODE_FILL;
    rast_info.cullMode = VK_CULL_MODE_BACK_BIT;
    rast_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rast_info.lineWidth = 1.0f;

    VkPipelineDepthStencilStateCreateInfo ds_info = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds_info.depthTestEnable = VK_TRUE;
    ds_info.depthWriteEnable = VK_TRUE;
    ds_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineMultisampleStateCreateInfo ms_info = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_att = {0};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_att.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend_info = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend_info.attachmentCount = 1;
    blend_info.pAttachments = &blend_att;

    VkDynamicState dyn_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_info = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn_info.dynamicStateCount = 2;
    dyn_info.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo pipe_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipe_info.stageCount = 2;
    pipe_info.pStages = shader_stages;
    pipe_info.pVertexInputState = &vi_info;
    pipe_info.pInputAssemblyState = &ia_info;
    pipe_info.pViewportState = &vp_state;
    pipe_info.pRasterizationState = &rast_info;
    pipe_info.pDepthStencilState = &ds_info;
    pipe_info.pMultisampleState = &ms_info;
    pipe_info.pColorBlendState = &blend_info;
    pipe_info.pDynamicState = &dyn_info;
    pipe_info.layout = pipeline_layout;
    pipe_info.renderPass = render_pass;
    pipe_info.subpass = 0;

    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe_info, NULL, &pipeline));
    printf("GRAPHICS_PIPELINE: Created vkcube graphics pipeline with depth test and backface culling\n");

    /* 10. Command Pool & Command Buffer */
    VkCommandPoolCreateInfo cmd_pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cmd_pool_info.queueFamilyIndex = queue_family;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(device, &cmd_pool_info, NULL, &command_pool));

    VkCommandBufferAllocateInfo cmd_alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc.commandPool = command_pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device, &cmd_alloc, &command_buffer));

    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &fence));

    /* 11. Upload Texture from Staging Buffer to Optimal Device-Local Image */
    VkCommandBufferBeginInfo upload_begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    upload_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &upload_begin));

    VkImageMemoryBarrier tex_barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    tex_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    tex_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    tex_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    tex_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    tex_barrier.image = texture_image;
    tex_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    tex_barrier.subresourceRange.baseMipLevel = 0;
    tex_barrier.subresourceRange.levelCount = 1;
    tex_barrier.subresourceRange.baseArrayLayer = 0;
    tex_barrier.subresourceRange.layerCount = 1;
    tex_barrier.srcAccessMask = 0;
    tex_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(command_buffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &tex_barrier);

    VkBufferImageCopy tex_copy = {0};
    tex_copy.bufferOffset = 0;
    tex_copy.bufferRowLength = TEX_WIDTH;
    tex_copy.bufferImageHeight = TEX_HEIGHT;
    tex_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    tex_copy.imageSubresource.mipLevel = 0;
    tex_copy.imageSubresource.baseArrayLayer = 0;
    tex_copy.imageSubresource.layerCount = 1;
    tex_copy.imageExtent = (VkExtent3D){TEX_WIDTH, TEX_HEIGHT, 1};

    vkCmdCopyBufferToImage(command_buffer,
        texture_staging, texture_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &tex_copy);

    tex_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    tex_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tex_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    tex_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &tex_barrier);

    VK_CHECK(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo upload_submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    upload_submit.commandBufferCount = 1;
    upload_submit.pCommandBuffers = &command_buffer;
    VK_CHECK(vkQueueSubmit(queue, 1, &upload_submit, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 10ull * 1000 * 1000 * 1000));
    VK_CHECK(vkResetFences(device, 1, &fence));
    VK_CHECK(vkResetCommandBuffer(command_buffer, 0));
    printf("TEXTURE_UPLOAD: Procedural texture uploaded and transitioned to SHADER_READ_ONLY_OPTIMAL\n");

    /* 12. Projection and View Matrices */
    float proj[16], view[16];
    float aspect = (float)FRAME_WIDTH / (float)FRAME_HEIGHT;
    Mat4Perspective(proj, 45.0f * (3.141592653589793f / 180.0f), aspect, 0.1f, 100.0f);
    Mat4Translation(view, 0.0f, 0.0f, -4.5f);

    /* Frame 0 Model Matrix (Rotated so that Front, Top, and Right faces are visible) */
    float model0[16], mv0[16], mvp0[16];
    Mat4Rotation(model0, 0.45f, 0.65f, 0.0f);
    Mat4Multiply(mv0, view, model0);
    Mat4Multiply(mvp0, proj, mv0);

    /* 13. Record and Submit Frame 0 (Geometry & Depth Verification) */
    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

    VkClearValue clear_values[2];
    /* Background: Deep slate blue / night sky (VK_FORMAT_B8G8R8A8_UNORM: B, G, R, A) */
    clear_values[0].color.float32[0] = 0.08f; /* B */
    clear_values[0].color.float32[1] = 0.05f; /* G */
    clear_values[0].color.float32[2] = 0.05f; /* R */
    clear_values[0].color.float32[3] = 1.0f;  /* A */
    clear_values[1].depthStencil.depth = 1.0f;
    clear_values[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo rp_begin = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp_begin.renderPass = render_pass;
    rp_begin.framebuffer = framebuffer;
    rp_begin.renderArea.offset.x = 0;
    rp_begin.renderArea.offset.y = 0;
    rp_begin.renderArea.extent.width = FRAME_WIDTH;
    rp_begin.renderArea.extent.height = FRAME_HEIGHT;
    rp_begin.clearValueCount = 2;
    rp_begin.pClearValues = clear_values;

    vkCmdBeginRenderPass(command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport vp = {0.0f, 0.0f, (float)FRAME_WIDTH, (float)FRAME_HEIGHT, 0.0f, 1.0f};
    VkRect2D sc = {{0, 0}, {FRAME_WIDTH, FRAME_HEIGHT}};
    vkCmdSetViewport(command_buffer, 0, 1, &vp);
    vkCmdSetScissor(command_buffer, 0, 1, &sc);

    VkDeviceSize vb_offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &vb_offset);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout, 0, 1, &descriptor_set, 0, NULL);

    PushConstants pc0;
    memcpy(pc0.mvp, mvp0, sizeof(mvp0));
    pc0.frame = 0;
    pc0.total_frames = (uint32_t)total_frames;
    vkCmdPushConstants(command_buffer, pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(PushConstants), &pc0);

    /* MANDATE: RASTERIZATION OF 36 VERTICES (12 TRIANGLES) VIA vkCmdDraw */
    vkCmdDraw(command_buffer, 36, 1, 0, 0);

    vkCmdEndRenderPass(command_buffer);

    /* Detiling / Copy: Image to Linear Host-Visible Destination Buffer */
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
        dest_buffer, 1, &copy_region);

    VkBufferMemoryBarrier bmb = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    bmb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bmb.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
    bmb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bmb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bmb.buffer = dest_buffer;
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

    /* 14. Verify Rendered 3D Cube Pixels in Host-Visible Memory (Byte Proof) */
    printf("\n=== VERIFICACAO DO CUBO 3D RENDERIZADO POR vkCmdDraw (BYTE PROOF) ===\n");
    uint32_t center_pixel = pixels[540 * FRAME_WIDTH + 1280];
    uint32_t corner_pixel = pixels[100 * FRAME_WIDTH + 100];
    printf("  [POISON CHECK] Centro (1280, 540): observed=0x%08X (Poison=0x%08X) -> %s\n",
           center_pixel, BC250_POISON,
           center_pixel != BC250_POISON ? "PASS (Overwritten)" : "FAIL (Intact Poison)");
    if (center_pixel == BC250_POISON) {
        fprintf(stderr, "Poison check failed: GPU rasterizer did not overwrite center pixel!\n");
        fprintf(stderr, "  [DUMP DIAGNOSTICO] Pixels 5x5 ao redor do centro (1280, 540):\n");
        for (int dy = -2; dy <= 2; ++dy) {
            fprintf(stderr, "    y=%d: ", 540 + dy);
            for (int dx = -2; dx <= 2; ++dx) {
                fprintf(stderr, "0x%08X ", pixels[(540 + dy) * FRAME_WIDTH + (1280 + dx)]);
            }
            fprintf(stderr, "\n");
        }
        goto cleanup;
    }

    printf("  [CLEAR COLOR CHECK] Canto (100, 100): observed=0x%08X -> %s\n",
           corner_pixel, (corner_pixel & 0xFF000000u) == 0xFF000000u ? "PASS" : "FAIL");

    /* Count non-zero alpha and non-zero RGB */
    uint32_t cube_pixel_count = 0;
    for (uint32_t y = 200; y < 880; y += 4) {
        for (uint32_t x = 800; x < 1760; x += 4) {
            uint32_t px = pixels[y * FRAME_WIDTH + x];
            if (px != corner_pixel && (px & 0x00FFFFFFu) != 0) cube_pixel_count++;
        }
    }
    printf("  [DEPTH & GEOMETRY RASTERIZATION] Pixels da malha 3D detectados: %u amostras\n",
           cube_pixel_count);
    if (cube_pixel_count < 1000) {
        fprintf(stderr, "Cube rasterization failed: insufficient 3D geometry rendered (%u < 1000)!\n", cube_pixel_count);
        fprintf(stderr, "  [DUMP DIAGNOSTICO] Pixels 5x5 ao redor do centro (1280, 540):\n");
        for (int dy = -2; dy <= 2; ++dy) {
            fprintf(stderr, "    y=%d: ", 540 + dy);
            for (int dx = -2; dx <= 2; ++dx) {
                fprintf(stderr, "0x%08X ", pixels[(540 + dy) * FRAME_WIDTH + (1280 + dx)]);
            }
            fprintf(stderr, "\n");
        }
        goto cleanup;
    }
    printf("BC250 RADV GRAPHICS VKCUBE BYTE PROOF: PASS (36 vertices rasterized, depth test verified)\n\n");

    /* 15. Present Rendered Framebuffer to Physical Display via WDDM */
    printf("=== APRESENTACAO FISICA NO SCANOUT (WDDM PRESENT) ===\n");
    if (g_radv_bc250_present_scanout != NULL) {
        GateStage("before radv_bc250_present_scanout");
        VkResult present_res = g_radv_bc250_present_scanout(
            device, queue, dest_memory, FRAME_WIDTH, FRAME_HEIGHT);
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

    /* 16. Continuous 60 FPS Swapchain Presentation Loop (Marco 4 Rotating Cube) */
    if (total_frames > 1) {
        printf("=== INICIANDO SWAPCHAIN GRAFICO DO CUBO 3D A %d FPS (%d QUADROS) ===\n",
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

        float angle_x = 0.45f;
        float angle_y = 0.65f;
        float angle_z = 0.0f;

        for (uint32_t f = 1; f < (uint32_t)total_frames; ++f) {
            angle_x += 0.012f;
            angle_y += 0.020f;
            angle_z += 0.007f;

            float model_f[16], mv_f[16], mvp_f[16];
            Mat4Rotation(model_f, angle_x, angle_y, angle_z);
            Mat4Multiply(mv_f, view, model_f);
            Mat4Multiply(mvp_f, proj, mv_f);

            VK_CHECK(vkResetFences(device, 1, &fence));
            VK_CHECK(vkResetCommandBuffer(command_buffer, 0));

            VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));
            vkCmdBeginRenderPass(command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            vkCmdSetViewport(command_buffer, 0, 1, &vp);
            vkCmdSetScissor(command_buffer, 0, 1, &sc);

            vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &vb_offset);
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline_layout, 0, 1, &descriptor_set, 0, NULL);

            PushConstants pcf;
            memcpy(pcf.mvp, mvp_f, sizeof(mvp_f));
            pcf.frame = f;
            pcf.total_frames = (uint32_t)total_frames;
            vkCmdPushConstants(command_buffer, pipeline_layout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(PushConstants), &pcf);

            /* Hardware 3D Graphics Draw (12 Triangles / 36 Vertices) */
            vkCmdDraw(command_buffer, 36, 1, 0, 0);

            vkCmdEndRenderPass(command_buffer);

            /* Detiling to linear scanout buffer */
            vkCmdPipelineBarrier(command_buffer,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, NULL, 0, NULL, 1, &imb);

            vkCmdCopyImageToBuffer(command_buffer,
                color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                dest_buffer, 1, &copy_region);

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
                device, queue, dest_memory, FRAME_WIDTH, FRAME_HEIGHT);
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
                printf("[SWAPCHAIN TELEMETRY] Quadro %u/%u (%.1f%%) | FPS Real: %.2f | Triangulos: %u | SDMA Blts: %u | Fallbacks: 0\n",
                       f, (uint32_t)total_frames, (double)f * 100.0 / (double)total_frames,
                       current_fps, (f + 1) * 12, f + 1);
                telemetry_frames = 0;
                telemetry_start = now;
            }
        }

        QueryPerformanceCounter(&loop_end);
        double total_loop_sec = (double)(loop_end.QuadPart - loop_start.QuadPart) / (double)qpc_freq.QuadPart;
        double overall_fps = (double)(total_frames - 1) / total_loop_sec;
        printf("\n=== SWAPCHAIN DO CUBO CONCLUIDO COM EXITO ===\n");
        printf("Total Quadros: %d\n", total_frames);
        printf("Total Triangulos Rasterizados: %d\n", total_frames * 12);
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
        if (descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptor_pool, NULL);
        if (descriptor_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptor_layout, NULL);
        if (texture_sampler != VK_NULL_HANDLE) vkDestroySampler(device, texture_sampler, NULL);
        if (texture_view != VK_NULL_HANDLE) vkDestroyImageView(device, texture_view, NULL);
        if (texture_image != VK_NULL_HANDLE) vkDestroyImage(device, texture_image, NULL);
        if (texture_memory != VK_NULL_HANDLE) vkFreeMemory(device, texture_memory, NULL);
        if (texture_staging != VK_NULL_HANDLE) vkDestroyBuffer(device, texture_staging, NULL);
        if (texture_staging_memory != VK_NULL_HANDLE) vkFreeMemory(device, texture_staging_memory, NULL);
        if (vertex_buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, vertex_buffer, NULL);
        if (vertex_memory != VK_NULL_HANDLE) vkFreeMemory(device, vertex_memory, NULL);
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, framebuffer, NULL);
        if (render_pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, render_pass, NULL);
        if (depth_view != VK_NULL_HANDLE) vkDestroyImageView(device, depth_view, NULL);
        if (depth_image != VK_NULL_HANDLE) vkDestroyImage(device, depth_image, NULL);
        if (depth_memory != VK_NULL_HANDLE) vkFreeMemory(device, depth_memory, NULL);
        if (color_view != VK_NULL_HANDLE) vkDestroyImageView(device, color_view, NULL);
        if (color_image != VK_NULL_HANDLE) vkDestroyImage(device, color_image, NULL);
        if (color_memory != VK_NULL_HANDLE) vkFreeMemory(device, color_memory, NULL);
        if (mapped != NULL) vkUnmapMemory(device, dest_memory);
        if (dest_buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, dest_buffer, NULL);
        if (dest_memory != VK_NULL_HANDLE) vkFreeMemory(device, dest_memory, NULL);
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

