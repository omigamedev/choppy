#pragma once
#ifdef __hlsl_dx_compiler
    #define SEM(sem) : sem
#else
    #define SEM(sem)
#endif
