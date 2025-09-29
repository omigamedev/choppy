#pragma once
#include "../hlsl_common.h"

struct alignas(64) PerFrameConstants
{
    float4x4 ViewProjection[2];
};

struct alignas(16) PerObjectBuffer
{
    float4x4 ObjectTransform;
    float4 Color;
};

struct VertexInput
{
    [[vk::location(0)]] float4 position SEM(POSITION);
};

struct PixelInput
{
    float4 position SEM(SV_POSITION);
    [[vk::location(1)]] float4 color SEM(COLOR);
};
