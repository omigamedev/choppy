#include "solid-flat.h"

[[vk::binding(0, 0)]] cbuffer PerFrameConstants { PerFrameConstants Frame; };
[[vk::binding(0, 1)]] StructuredBuffer<PerObjectBuffer> ObjectsData;
[[vk::binding(1, 1)]] StructuredBuffer<PerObjectArgs> ObjectIndices;
[[vk::push_constant]] PushBuffer PushArgs;

PixelInput VSMain(VertexInput input,
    uint ViewIndex : SV_ViewID,
    [[vk::builtin("DrawIndex")]] uint drawIndex : SV_InstanceID)
{
    PixelInput output;
    //uint objIndex = ObjectIndices[drawIndex].ObjectIndex;
    //const float4x4 WorldViewProjection = mul(ObjectsData[PushArgs.DrawIndex].ObjectTransform, Frame.ViewProjection[ViewIndex]);
    const float4x4 WorldViewProjection = mul(ObjectsData[drawIndex].ObjectTransform, Frame.ViewProjection[ViewIndex]);
    output.position = mul(input.position, WorldViewProjection);
    // output.color = selected ? float4(0, 0, 0, 1) : input.color + ObjectColor;
    output.color = input.color;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return (input.color);
}
