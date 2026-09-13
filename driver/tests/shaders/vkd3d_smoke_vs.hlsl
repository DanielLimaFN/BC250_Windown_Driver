struct VsOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VsOut main(uint vertexId : SV_VertexID)
{
    static const float2 positions[3] = {
        float2(0.0, -0.5), float2(0.5, 0.5), float2(-0.5, 0.5)
    };
    VsOut output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.color = float4(1.0, 0.25, 0.0, 1.0);
    return output;
}
