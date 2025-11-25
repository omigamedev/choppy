#include "solid-flat.h"

[[vk::binding(0, 0)]] cbuffer PerFrameConstants { PerFrameConstants Frame; };
[[vk::binding(0, 1)]] StructuredBuffer<PerObjectBuffer> ObjectsData;

PixelInput VSMain(VertexInput input,
    uint ViewIndex : SV_ViewID,
    [[vk::builtin("DrawIndex")]] uint drawIndex : SV_InstanceID)
{
    // Unpack the 32-bit integer
    // Layout: [1b v | 1b u | 12b layer | 6b z | 6b y | 6b x]
    float x = float(input.data & 0x3F);
    float y = float((input.data >> 6) & 0x3F) + ObjectsData[drawIndex].y_offset;
    float z = float((input.data >> 12) & 0x3F);
    float layer = float((input.data >> 18) & 0xFFF);
    float u = float((input.data >> 30) & 0x1);
    float v = float((input.data >> 31) & 0x1);
    // Layout: [27b free | 4b occlusion | 3b face]
    int face = input.data_ext & 0x07;
    int occ = (input.data_ext >> 3) & 0x0F;

    const float4x4 WorldViewProjection = mul(ObjectsData[drawIndex].ObjectTransform, Frame.ViewProjection[ViewIndex]);
    const float3 Position = float3(x, y, z) * 0.5;

    PixelInput output;
    output.position = mul(float4(Position, 1.0), WorldViewProjection);
    output.uvs = float2(u, v) * float(ObjectsData[drawIndex].lod);
    output.layer = layer;
    output.face = face;
    output.occ = occ;

    // Pass the view-space depth (stored in the w component of clip-space position) to the pixel shader.
    output.viewDepth = output.position.w;

    return output;
}

[[vk::binding(1, 0)]] Texture2DArray myTexture;   // bound to descriptor set
[[vk::binding(1, 0)]] SamplerState mySampler; // bound to sampler

float4 PSMain(PixelInput input) : SV_TARGET
{
    static const float3 sun = normalize(float3(1, 1, 1));
    static const float3 FaceNormal[6] = {
        float3( 0, 1, 0), // U
        float3( 0,-1, 0), // D
        float3( 0, 0, 1), // F
        float3( 0, 0,-1), // B
        float3(-1, 0, 0), // L
        float3( 1, 0, 0), // R
    };
    float3 normal = FaceNormal[input.face];
    float3 light = lerp(0.5, 1.0, step(0.0, dot(normal, sun)));
    float3 occlusion = max(0.1, float(input.occ) / 7.0);
    //return float4(normal, 1);
    const float4 textureColor = myTexture.Sample(mySampler, float3(input.uvs, input.layer));
    float4 finalColor = float4(textureColor.xyz, 1.0);

    if (Frame.fogColor.w > 0.0) // Check if fog is enabled
    {
        // Calculate fog factor (0 = no fog, 1 = full fog)
        float fogFactor = saturate((input.viewDepth - Frame.fogStart) / (Frame.fogEnd - Frame.fogStart));

        // Blend the texture color with the fog color
        finalColor.rgb = lerp(finalColor.rgb, Frame.fogColor.rgb, fogFactor);
    }

    return finalColor * Frame.tint * float4(light, 1.0) * float4(occlusion, 1.0);
}
