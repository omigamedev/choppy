cmake_minimum_required (VERSION 3.28.0)

project("choppy_engine")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MODULES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../modules)
add_subdirectory(${MODULES_DIR}/app modules/app)
add_subdirectory(${MODULES_DIR}/std modules/std)
add_subdirectory(${MODULES_DIR}/mylib modules/mylib)

include(FetchContent)
FetchContent_Declare(
    VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.4.309)
set(VULKAN_HEADERS_ENABLE_MODULE ON)
FetchContent_MakeAvailable(VulkanHeaders)

find_package(Vulkan)

add_executable(ce_windows
    src/win_main.cpp)
#target_link_directories(ce_windows PRIVATE $ENV{VULKAN_SDK}/Include
target_link_libraries(ce_windows PRIVATE app std Vulkan::Vulkan Vulkan::VulkanHppModule mylib_static_cpp)
target_include_directories(ce_windows PRIVATE "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}/cppwinrt")
set_target_properties(ce_windows PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
