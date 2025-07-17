#pragma once
#ifdef __hlsl_dx_compiler
    #define SEM(sem) : sem
    #define REGISTER(reg) : register(reg)
#else
    #define cbuffer struct
    #define SEM(sem)
    #define REGISTER(reg)
#endif
