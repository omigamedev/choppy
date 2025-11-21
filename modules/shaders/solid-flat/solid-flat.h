#pragma once
#include "../hlsl_common.h"

struct alignas(64) PerFrameConstants
{
    float4x4 ViewProjection[2];
    float4 tint;
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
    // Layout: [1b v | 1b u | 12b layer | 6b z | 6b y | 6b x]
    [[vk::location(0)]] uint data SEM(DATA);
    // Layout: [27b free | 2b occlusion | 3b face]
    [[vk::location(1)]] uint data_ext SEM(DATA_EXT);
};

struct PixelInput
{
    float4 position SEM(SV_POSITION);
    float2 uvs SEM(UVS);
    nointerpolation float layer SEM(LAYER);
    float viewDepth SEM(VIEW_DEPTH);
    nointerpolation int occ SEM(OCCLUSION);
    nointerpolation int face SEM(FACE_ID);
};
