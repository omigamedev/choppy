#include "solid-flat.h"

PixelInput VSMain(VertexInput input, uint ViewIndex : SV_ViewID)
{
    PixelInput output;
    output.position = mul(input.position, WorldViewProjection[ViewIndex]);
    output.color = input.color;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return input.color;
}
