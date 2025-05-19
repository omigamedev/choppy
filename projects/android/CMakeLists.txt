# CMakeList.txt : CMake project for cppmod, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.29.0)
include(FetchContent)

project ("choppy-engine" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MODULES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../modules)
add_subdirectory(${MODULES_DIR}/app modules/app)
add_subdirectory(${MODULES_DIR}/std modules/std)
add_subdirectory(${MODULES_DIR}/mylib modules/mylib)
add_subdirectory(${MODULES_DIR}/encoder modules/encoder)
add_subdirectory(${MODULES_DIR}/rtmp modules/rtmp)

FetchContent_Declare(
    VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.4.309)
set(VULKAN_HEADERS_ENABLE_MODULE ON)
FetchContent_MakeAvailable(VulkanHeaders)

find_package(Vulkan REQUIRED)
find_package(game-activity REQUIRED CONFIG)

add_library(main SHARED src/android_main.cpp)
add_dependencies(main build_rust)
target_link_libraries(main PRIVATE
    mylib_static_cpp
    encoder
    rtmp
    app std
    android log
    Vulkan::Vulkan
    Vulkan::VulkanHppModule
    game-activity::game-activity_static)
target_link_options(main PRIVATE
    -u Java_com_google_androidgamesdk_GameActivity_initializeNativeCode)
