#include "textured.h"

[[vk::binding(0, 0)]] cbuffer PerFrameConstants { PerFrameConstants Frame; };
[[vk::binding(0, 1)]] StructuredBuffer<PerObjectBuffer> ObjectsData;

PixelInput VSMain(VertexInput input,
    uint ViewIndex : SV_ViewID,
    [[vk::builtin("DrawIndex")]] uint drawIndex : SV_InstanceID)
{
    PixelInput output;
    const float4x4 WorldViewProjection = mul(ObjectsData[drawIndex].ObjectTransform, Frame.ViewProjection[ViewIndex]);
    output.position = mul(input.position, WorldViewProjection);
    output.uvs = input.uvs;
    return output;
}

[[vk::binding(0, 2)]] Texture2D myTexture;   // bound to descriptor set
[[vk::binding(0, 2)]] SamplerState mySampler; // bound to sampler

float4 PSMain(PixelInput input) : SV_TARGET
{
    const float4 textureColor = myTexture.Sample(mySampler, input.uvs);
    return textureColor;
}
