#version 450
// Conta cada invocacao de fragmento: separa "zero fragmento" de "fragmento sem escrita".
layout(location = 0) out vec4 outColor;
layout(std430, set = 0, binding = 0) buffer Counter { uint fragments; };
void main() { atomicAdd(fragments, 1u); outColor = vec4(1.0, 1.0, 1.0, 1.0); }
