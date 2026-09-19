cbuffer Transform : register(b0) { float4 offset; };
float4 main(float4 position : POSITION) : SV_Position { return position + offset; }
