#version 450

/*
 * BC-250 RADV Cube Graphics Gate - Fragment Shader (MARCO4_GRAPHICS_VKCUBE)
 * Target: AMD BC-250 APU (1002:13FE, Cyan Skillfish 2 / GFX1013)
 * Output Format: VK_FORMAT_B8G8R8A8_UNORM (2560 x 1080)
 *
 * Exercises:
 *  - Hardware Texture Sampler (descriptor set 0 binding 0)
 *  - Diffuse surface normal lighting (dot(N, L))
 *  - Perspective-correct UV texture interpolation
 *  - Face color discrimination
 */

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    uint frame;
    uint total_frames;
} pc;

void main()
{
    // Directional light from camera-relative angle
    vec3 lightDir = normalize(vec3(0.4, 0.8, 0.6));
    float diff = max(dot(normalize(inNormal), lightDir), 0.25);

    // Sample from hardware texture sampler
    vec4 texColor = texture(texSampler, inUv);

    // Combine surface color, texture sample, and lighting
    vec3 finalRgb = inColor * texColor.rgb * diff;

    outColor = vec4(finalRgb, 1.0);
}
