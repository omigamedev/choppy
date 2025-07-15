include(FetchContent)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1)
FetchContent_MakeAvailable(glm)

add_library(glmcppm)
target_link_libraries(glmcppm PUBLIC glm::glm)
target_sources(glmcppm PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${glm_SOURCE_DIR}/glm
    FILES ${glm_SOURCE_DIR}/glm/glm.cppm)

add_library(app)
target_link_libraries(app PUBLIC platform xr vk glmcppm shaders)
target_sources(app PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES app.cppm)
