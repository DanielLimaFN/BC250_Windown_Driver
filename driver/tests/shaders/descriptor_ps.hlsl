cbuffer Color : register(b0) { float4 tint; };
Texture2D<float4> image : register(t3);
SamplerState sampleState : register(s2);
float4 main(float4 position : SV_Position) : SV_Target
{ return image.Sample(sampleState, position.xy * 0.01) * tint; }
