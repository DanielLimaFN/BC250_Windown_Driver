#ifndef BC250_VKD3D_SHADER_ABI_H
#define BC250_VKD3D_SHADER_ABI_H

#include <stddef.h>
#include <stdint.h>

/* Minimal provider ABI mirrored from the public Wine vkd3d_shader.h API. */
#define BC250_VKD3D_STRUCTURE_TYPE_COMPILE_INFO 0
#define BC250_VKD3D_SOURCE_DXBC_TPF 1
#define BC250_VKD3D_TARGET_SPIRV_BINARY 1

typedef struct BC250_VKD3D_CODE {
    const void *code;
    size_t size;
} BC250_VKD3D_CODE;

typedef struct BC250_VKD3D_COMPILE_INFO {
    int type;
    const void *next;
    BC250_VKD3D_CODE source;
    int source_type;
    int target_type;
    const void *options;
    unsigned int option_count;
    int log_level;
    const char *source_name;
} BC250_VKD3D_COMPILE_INFO;

typedef struct BC250_VKD3D_DXBC_SECTION_DESC {
    uint32_t tag;
    BC250_VKD3D_CODE data;
} BC250_VKD3D_DXBC_SECTION_DESC;

typedef int (__cdecl *BC250_PFN_VKD3D_COMPILE)(
    const BC250_VKD3D_COMPILE_INFO *, BC250_VKD3D_CODE *, char **);
typedef void (__cdecl *BC250_PFN_VKD3D_FREE_CODE)(BC250_VKD3D_CODE *);
typedef const char *(__cdecl *BC250_PFN_VKD3D_GET_VERSION)(
    unsigned int *, unsigned int *);
typedef int (__cdecl *BC250_PFN_VKD3D_SERIALIZE_DXBC)(
    size_t, const BC250_VKD3D_DXBC_SECTION_DESC *, BC250_VKD3D_CODE *, char **);

#if defined(_WIN64)
static_assert(sizeof(BC250_VKD3D_CODE) == 16, "vkd3d code ABI drift");
static_assert(sizeof(BC250_VKD3D_COMPILE_INFO) == 64, "vkd3d compile ABI drift");
static_assert(offsetof(BC250_VKD3D_COMPILE_INFO, source) == 16, "vkd3d source ABI drift");
static_assert(offsetof(BC250_VKD3D_COMPILE_INFO, source_name) == 56, "vkd3d name ABI drift");
static_assert(sizeof(BC250_VKD3D_DXBC_SECTION_DESC) == 24, "vkd3d section desc ABI drift");
static_assert(offsetof(BC250_VKD3D_DXBC_SECTION_DESC, data) == 8, "vkd3d section desc data offset drift");
#endif

#endif
