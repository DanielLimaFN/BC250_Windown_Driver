#version 450

/*
 * BC-250 RADV Graphics Gate - Fullscreen Triangle Vertex Shader
 * Target: AMD BC-250 APU (1002:13FE, Cyan Skillfish 2 / GFX1013)
 *
 * Generates an oversized fullscreen triangle from gl_VertexIndex (0..2):
 * Vertex 0: (-1.0, -1.0) -> top-left
 * Vertex 1: ( 3.0, -1.0) -> top-right (extended)
 * Vertex 2: (-1.0,  3.0) -> bottom-left (extended)
 *
 * This completely covers the viewport NDC [-1, 1] without vertex buffer overhead
 * and without rasterization seam artifacts.
 */

layout(location = 0) out vec2 out_uv;

void main()
{
    vec2 pos = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                    (gl_VertexIndex == 2) ? 3.0 : -1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
    out_uv = (pos + vec2(1.0)) * 0.5;
}
