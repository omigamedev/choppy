#include "solid-color.h"

[[vk::binding(0, 0)]] cbuffer PerFrameConstants { PerFrameConstants Frame; };
[[vk::binding(0, 1)]] StructuredBuffer<PerObjectBuffer> ObjectsData;

PixelInput VSMain(VertexInput input,
    uint ViewIndex : SV_ViewID,
    [[vk::builtin("DrawIndex")]] uint drawIndex : SV_InstanceID)
{
    PixelInput output;
    const float4x4 WorldViewProjection = mul(ObjectsData[drawIndex].ObjectTransform, Frame.ViewProjection[ViewIndex]);
    output.position = mul(input.position, WorldViewProjection);
    output.color = ObjectsData[drawIndex].Color;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return input.color;
}
