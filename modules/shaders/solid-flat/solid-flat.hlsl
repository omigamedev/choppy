#include "solid-flat.h"

PixelInput VSMain(VertexInput input, uint ViewIndex : SV_ViewID)
{
    PixelInput output;
    const float4x4 WorldViewProjection = mul(ObjectTransform, ViewProjection[ViewIndex]);
    output.position = mul(input.position, WorldViewProjection);
    // output.color = selected ? float4(0, 0, 0, 1) : input.color + ObjectColor;
    output.color = input.color;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return (input.color);
}
