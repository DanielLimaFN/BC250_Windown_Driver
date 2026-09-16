#version 450

/*
 * BC-250 RADV Cube Graphics Gate - Vertex Shader (MARCO4_GRAPHICS_VKCUBE)
 * Target: AMD BC-250 APU (1002:13FE, Cyan Skillfish 2 / GFX1013)
 *
 * Receives 3D vertex attributes and transforms via Model-View-Projection (MVP) matrix.
 */

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    uint frame;
    uint total_frames;
} pc;

void main()
{
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    outUv = inUv;
    outNormal = inNormal;
    outColor = inColor;
}
