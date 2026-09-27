#version 450
// Triangulo de tela cheia so' com gl_VertexIndex: sem VB, sem IB, sem CB.
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
