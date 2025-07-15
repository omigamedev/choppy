module;
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/compatibility.hpp"
export module ce.shaders.solidflat;
import ce.shaders;
import glm;
using namespace glm;
export namespace ce::shaders::solidflat
{
    #include "solid-flat.h"

}
