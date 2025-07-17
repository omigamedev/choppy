#pragma once
#include "../hlsl_common.h"

cbuffer PerFrameConstants REGISTER(b0)
{
    float4x4 WorldViewProjection;
};

struct VertexInput
{
    float3 position SEM(POSITION);
};

struct PixelInput
{
    float4 position SEM(SV_POSITION);
};
