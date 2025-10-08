#include "solid-flat.h"

[[vk::binding(0, 0)]] cbuffer PerFrameConstants { PerFrameConstants Frame; };
[[vk::binding(0, 1)]] StructuredBuffer<PerObjectBuffer> ObjectsData;

PixelInput VSMain(VertexInput input,
    uint ViewIndex : SV_ViewID,
    [[vk::builtin("DrawIndex")]] uint drawIndex : SV_InstanceID)
{
    // Unpack the 32-bit integer
    float x = float(input.data & 0x3F);
    float y = float((input.data >> 6) & 0x3F);
    float z = float((input.data >> 12) & 0x3F);
    float u = float((input.data >> 18) & 0x3F) / 2.0;
    float v = float((input.data >> 24) & 0x3F) / 4.0;
    uint face_id = (input.data >> 30) & 0x03;

    const float4x4 WorldViewProjection = mul(ObjectsData[drawIndex].ObjectTransform, Frame.ViewProjection[ViewIndex]);
    const float3 Position = float3(x, y, z) * 0.5;

    PixelInput output;
    output.position = mul(float4(Position, 1.0), WorldViewProjection);
    output.uvs = float2(u, v);
    return output;
}

[[vk::binding(1, 0)]] Texture2D myTexture;   // bound to descriptor set
[[vk::binding(1, 0)]] SamplerState mySampler; // bound to sampler

float4 PSMain(PixelInput input) : SV_TARGET
{
    const float4 c = myTexture.Sample(mySampler, input.uvs);
    return float4(c.x, c.y, c.z, 1.0);
}
