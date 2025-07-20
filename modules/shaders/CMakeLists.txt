set(HLSL_PROFILE_VERSION "6_2")

# Find the required shader compilation tools
set(VULKAN_SDK_PATH $ENV{VULKAN_SDK})
if(VULKAN_SDK_PATH)
    message(STATUS "Vulkan SDK found at: ${VULKAN_SDK_PATH}")
    set(DXC_EXECUTABLE "${VULKAN_SDK_PATH}/Bin/dxc.exe")
    set(SPIRV_REFLECT_EXECUTABLE "${VULKAN_SDK_PATH}/Bin/spirv-reflect.exe")
else()
    message(WARNING "VULKAN_SDK environment variable not set."
        "Will search for tools in system PATH. This might find the wrong versions.")
    find_program(DXC_EXECUTABLE dxc.exe HINTS "${VULKAN_SDK_PATH}/Bin")
    find_program(SPIRV_REFLECT_EXECUTABLE spirv-reflect.exe)
endif()

if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "dxc.exe not found."
        "Please ensure the Vulkan SDK is installed and its bin directory is in your PATH.")
endif()
if(NOT SPIRV_REFLECT_EXECUTABLE)
    message(FATAL_ERROR "spirv-reflect.exe not found."
        "Please ensure the Vulkan SDK is installed and its bin directory is in your PATH.")
endif()

# Initialize the list that subdirectories will populate with metadata.
set(SHADER_METADATA_LIST)
set(SHADER_MODULES_LIST)
add_subdirectory(solid-flat)

# A list to hold all the files we will generate.
set(HLSL_GENERATED_FILES)

# It assumes SHADER_METADATA_LIST is a flat list of items and processes it in chunks of 3 (file;profile;entry).
list(LENGTH SHADER_METADATA_LIST LIST_LENGTH)

# Check that the list length is a multiple of 3
math(EXPR MULTIPLES_OF_THREE "${LIST_LENGTH} % 3")
if(NOT(LIST_LENGTH GREATER 0 AND MULTIPLES_OF_THREE EQUAL 0))
    message(FATAL_ERROR "SHADER_METADATA_LIST has an invalid number of elements (${LIST_LENGTH})."
        "It must be a multiple of 3.")
endif()

math(EXPR NUM_ENTRIES "${LIST_LENGTH} / 3")
math(EXPR MAX_INDEX "${NUM_ENTRIES} - 1")

foreach(ENTRY_INDEX RANGE ${MAX_INDEX})
    math(EXPR OFFSET "${ENTRY_INDEX} * 3")

    # Use SUBLIST to grab the 3 elements for this shader entry. This is the key insight.
    list(SUBLIST SHADER_METADATA_LIST ${OFFSET} 3 METADATA_PAIR)

    # Parse the file path and profile from the pair.
    list(GET METADATA_PAIR 0 FILE)
    list(GET METADATA_PAIR 1 HLSL_PROFILE)
    list(GET METADATA_PAIR 2 ENTRY_POINT)

    #message(STATUS "Setting up build commands for HLSL file: ${FILE} with profile ${HLSL_PROFILE}")

    # Get the base filename without the extension
    get_filename_component(FILE_WE ${FILE} NAME_WE)
    get_filename_component(FILE_DIR ${FILE} DIRECTORY)
    file(GLOB SHADER_SOURCES "${FILE_DIR}/*.h" ${FILE})
    message(STATUS "Shader ${FILE} in ${FILE_DIR} depends on ${SHADER_SOURCES}")

    # Append the profile to the output filename to prevent VS/PS builds
    # from the same source file from overwriting each other.
    if (WIN32)
        set(SPIRV_OUTPUT ${CMAKE_BINARY_DIR}/assets/shaders/${FILE_WE}-${HLSL_PROFILE}.spv)
        set(REFLECTION_OUTPUT ${CMAKE_BINARY_DIR}/assets/shaders/${FILE_WE}-${HLSL_PROFILE}.spv.txt)
    else()
        set(SPIRV_OUTPUT ${CMAKE_SOURCE_DIR}/assets_gen/shaders/${FILE_WE}-${HLSL_PROFILE}.spv)
        set(REFLECTION_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${FILE_WE}-${HLSL_PROFILE}.spv.txt)
    endif()

    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT} ${REFLECTION_OUTPUT}
        COMMAND ${DXC_EXECUTABLE} -spirv -T ${HLSL_PROFILE}_${HLSL_PROFILE_VERSION}
            -E ${ENTRY_POINT} $<IF:$<CONFIG:DEBUG>,-Od,-O1> -Zi -Fo ${SPIRV_OUTPUT} ${FILE}
        COMMAND ${SPIRV_REFLECT_EXECUTABLE} ${SPIRV_OUTPUT} > ${REFLECTION_OUTPUT}
        DEPENDS ${SHADER_SOURCES}
        COMMENT "Compiling HLSL: ${FILE} (${HLSL_PROFILE}) to ${SPIRV_OUTPUT}"
        VERBATIM)

    # Add the generated files to our list.
    list(APPEND HLSL_GENERATED_FILES ${SPIRV_OUTPUT} ${REFLECTION_OUTPUT})
endforeach()

add_custom_target(shaders-spv ALL
    DEPENDS ${HLSL_GENERATED_FILES} ${SHADER_HEADERS_LIST}
    COMMENT "Building all shaders")

add_library(shaders OBJECT)
add_dependencies(shaders shaders-spv)
target_link_libraries(shaders PUBLIC glmcppm vk)
target_sources(shaders PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    FILES shaders.cppm ${SHADER_MODULES_LIST})
