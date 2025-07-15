#pragma once
#include "../hlsl_common.h"

struct VertexInput
{
    float3 position SEM(POSITION);
};

struct PixelInput
{
    float4 position SEM(SV_POSITION);
};
