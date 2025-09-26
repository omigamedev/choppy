#include "solid-flat.h"

[[vk::binding(0, 0)]] cbuffer PerFrameConstants { PerFrameConstants Frame; };
[[vk::binding(0, 1)]] StructuredBuffer<PerObjectBuffer> ObjectsData;

PixelInput VSMain(VertexInput input,
    uint ViewIndex : SV_ViewID,
    [[vk::builtin("DrawIndex")]] uint drawIndex : SV_InstanceID)
{
    PixelInput output;
    const float4x4 WorldViewProjection = mul(ObjectsData[drawIndex].ObjectTransform, Frame.ViewProjection[ViewIndex]);
    output.position = mul(input.position, WorldViewProjection);
    output.color = input.color;
    return output;
}

[[vk::binding(1, 0)]] Texture2D myTexture;   // bound to descriptor set
[[vk::binding(1, 0)]] SamplerState mySampler; // bound to sampler

float4 PSMain(PixelInput input) : SV_TARGET
{
    const float4 c = myTexture.Sample(mySampler, input.color.xy);
    return float4(c.x, c.y, c.z, input.color.a);
}
