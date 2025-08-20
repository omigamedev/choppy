#pragma once
#include "../hlsl_common.h"

[[vk::binding(0, 0)]] cbuffer PerFrameConstants REGISTER(b0)
{
    float4x4 ViewProjection[2];
};

[[vk::binding(1, 0)]] cbuffer alignas(64) PerObjectBuffer REGISTER(b1)
{
    float4x4 ObjectTransform;
    float4 ObjectColor;
    bool selected;
};

struct VertexInput
{
    [[vk::location(0)]] float4 position SEM(POSITION);
    [[vk::location(1)]] float4 color SEM(COLOR);
};

struct PixelInput
{
    float4 position SEM(SV_POSITION);
    [[vk::location(1)]] float4 color SEM(COLOR);
};
