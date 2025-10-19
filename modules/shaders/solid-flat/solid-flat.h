#pragma once
#include "../hlsl_common.h"

struct alignas(64) PerFrameConstants
{
    float4x4 ViewProjection[2];
    float4 fogColor; // x, y, z = color, w = enabled
    float fogStart;
    float fogEnd;
};

struct alignas(16) PerObjectBuffer
{
    float4x4 ObjectTransform;
    float y_offset;
};

struct VertexInput
{
    [[vk::location(0)]] uint data SEM(DATA);
};

struct PixelInput
{
    float4 position SEM(SV_POSITION);
    [[vk::location(1)]] float2 uvs SEM(UV0);
    [[vk::location(2)]] float viewDepth SEM(TEXCOORD0);
};
