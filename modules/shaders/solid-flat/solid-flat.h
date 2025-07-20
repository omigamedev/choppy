#pragma once
#include "../hlsl_common.h"

cbuffer PerFrameConstants REGISTER(b0)
{
    float4x4 WorldViewProjection[2];
};

struct VertexInput
{
    [[vk::location(0)]] float3 position SEM(POSITION0);
};

struct PixelInput
{
    float4 position SEM(SV_POSITION);
};
