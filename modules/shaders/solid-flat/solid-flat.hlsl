#include "solid-flat.h"

[[vk::binding(0, 0)]] cbuffer PerFrameConstants { PerFrameConstants Frame; };
[[vk::binding(0, 1)]] StructuredBuffer<PerObjectBuffer> ObjectsData;

PixelInput VSMain(VertexInput input,
    uint ViewIndex : SV_ViewID,
    [[vk::builtin("DrawIndex")]] uint drawIndex : SV_InstanceID)
{
    // Unpack the 32-bit integer
    float x = float(input.data & 0x3F);
    float y = float((input.data >> 6) & 0x3F) + ObjectsData[drawIndex].y_offset;
    float z = float((input.data >> 12) & 0x3F);
    float u = float((input.data >> 18) & 0x3F) / 2.0;
    float v = float((input.data >> 24) & 0x3F) / 4.0;
    uint face_id = (input.data >> 30) & 0x03;

    const float4x4 WorldViewProjection = mul(ObjectsData[drawIndex].ObjectTransform, Frame.ViewProjection[ViewIndex]);
    const float3 Position = float3(x, y, z) * 0.5;

    PixelInput output;
    output.position = mul(float4(Position, 1.0), WorldViewProjection);
    output.uvs = float2(u, v);

    // Pass the view-space depth (stored in the w component of clip-space position) to the pixel shader.
    output.viewDepth = output.position.w;

    return output;
}

[[vk::binding(1, 0)]] Texture2D myTexture;   // bound to descriptor set
[[vk::binding(1, 0)]] SamplerState mySampler; // bound to sampler

float4 PSMain(PixelInput input) : SV_TARGET
{
    const float4 textureColor = myTexture.Sample(mySampler, input.uvs);
    float4 finalColor = float4(textureColor.xyz, 1.0);

    if (Frame.fogColor.w > 0.0) // Check if fog is enabled
    {
        // Calculate fog factor (0 = no fog, 1 = full fog)
        float fogFactor = saturate((input.viewDepth - Frame.fogStart) / (Frame.fogEnd - Frame.fogStart));

        // Blend the texture color with the fog color
        finalColor.rgb = lerp(finalColor.rgb, Frame.fogColor.rgb, fogFactor);
    }

    return finalColor;
}
