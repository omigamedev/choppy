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
add_subdirectory(${MODULES_DIR}/vk modules/vk)

find_package(game-activity REQUIRED CONFIG)

add_library(main SHARED src/android_main.cpp)
add_dependencies(main build_rust)
target_link_libraries(main PRIVATE
    mylib_static_cpp
    encoder rtmp app std vk
    android log
    game-activity::game-activity_static)
target_compile_definitions(main PRIVATE "YT_KEY=\"$ENV{YT_KEY}\"")
target_link_options(main PRIVATE
    -u Java_com_google_androidgamesdk_GameActivity_initializeNativeCode)
