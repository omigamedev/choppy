#pragma once
#include "../hlsl_common.h"

struct alignas(64) PerFrameConstants
{
    float4x4 ViewProjection[2];
};

struct alignas(16) PerObjectBuffer
{
    float4x4 ObjectTransform;
};

struct alignas(16) VertexInput
{
    [[vk::location(0)]] float4 position SEM(POSITION);
    [[vk::location(1)]] float2 uvs SEM(UV0);
};

struct PixelInput
{
    float4 position SEM(SV_POSITION);
    [[vk::location(1)]] float2 uvs SEM(UV0);
};
