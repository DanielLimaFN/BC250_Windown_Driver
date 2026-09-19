#version 450
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = vec4(max(in_uv.x, 0.2), max(in_uv.y, 0.3), 0.85, 1.0);
}
