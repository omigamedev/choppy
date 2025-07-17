#include "solid-flat.h"

PixelInput VSMain(VertexInput input)
{
    PixelInput output;

    // Promote the 3D local-space position to a 4D vector for matrix math.
    float4 pos = float4(input.position, 1.0f);

    // Transform the vertex position by the WorldViewProjection matrix.
    // This converts the vertex from local model space all the way to clip space.
    output.position = mul(pos, WorldViewProjection);

    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    // For a flat solid color, we just return a hard-coded color value.
    // The format is (Red, Green, Blue, Alpha).
    // This will make every pixel of the object solid red.
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
