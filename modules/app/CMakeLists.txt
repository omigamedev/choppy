include(FetchContent)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 0af55ccecd98d4e5a8d1fad7de25ba429d60e863) # 1.0.1
FetchContent_Declare(perlin
    GIT_REPOSITORY https://github.com/Reputeless/PerlinNoise.git
    GIT_TAG 39ca74069057407a04c46e2ec06baf57c6fc4b3f) # v3.0.0
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG fede005abaf93d9d7f3a679d1999b2db341b360f) # Sep 7, 2025
FetchContent_Declare(tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG c556831ddc6fe26d2fce01c14c97205a9dad46d5) # v0.12.2
FetchContent_MakeAvailable(perlin glm stb tracy)

add_library(perlin INTERFACE)
target_include_directories(perlin INTERFACE ${perlin_SOURCE_DIR})

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

add_library(glmcppm OBJECT)
target_link_libraries(glmcppm PUBLIC glm::glm)
target_compile_definitions(glmcppm PUBLIC
    GLM_ENABLE_EXPERIMENTAL
    #GLM_FORCE_RADIANS
    #GLM_FORCE_DEPTH_ZERO_TO_ONE
)
target_sources(glmcppm PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${glm_SOURCE_DIR}/glm
    FILES ${glm_SOURCE_DIR}/glm/glm.cppm)

if (WIN32)
    add_custom_target(copy_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/assets)
else()
    add_custom_target(copy_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        ${CMAKE_SOURCE_DIR}/assets_gen)
endif()

add_library(app OBJECT)
target_link_libraries(app PUBLIC platform xr vk glmcppm shaders perlin stb)
add_dependencies(app copy_assets)
target_sources(app PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES app.cppm grid.cppm cube.cppm utils.cppm chunkgen.cppm chunkmesh.cppm)
