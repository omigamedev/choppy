#pragma once
#ifdef __hlsl_dx_compiler
    #define SEM(sem) : sem
    #define REGISTER(reg) : register(reg)
#else
    #include "hlsl_types.h"
    #ifdef __clang__
        #pragma clang diagnostic ignored "-Wunknown-attributes"
    #endif
    #define cbuffer struct
    #define SEM(sem)
    #define REGISTER(reg)
#endif
