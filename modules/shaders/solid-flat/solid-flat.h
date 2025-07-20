#pragma once
#include "../hlsl_common.h"

cbuffer PerFrameConstants REGISTER(b0)
{
    float4x4 WorldViewProjection[2];
};

struct VertexInput
{
    [[vk::location(0)]] float4 position SEM(POSITION);
    [[vk::location(1)]] float4 color SEM(COLOR);
};

struct PixelInput
{
    float4 position SEM(SV_POSITION);
    float4 color SEM(COLOR);
};
