cmake_minimum_required (VERSION 3.28.0)

project("choppy_engine")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DUNICODE -D_UNICODE)

set(MODULES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../modules)
add_subdirectory(${MODULES_DIR}/app modules/app)
#add_subdirectory(${MODULES_DIR}/mylib modules/mylib)
add_subdirectory(${MODULES_DIR}/vk modules/vk)
add_subdirectory(${MODULES_DIR}/xr modules/xr)
add_subdirectory(${MODULES_DIR}/encoder modules/encoder)
add_subdirectory(${MODULES_DIR}/rtmp modules/rtmp)
add_subdirectory(${MODULES_DIR}/platform modules/platform)
add_subdirectory(${MODULES_DIR}/shaders modules/shaders)

add_executable(ce_windows src/win_main.cpp)
target_link_libraries(ce_windows PRIVATE app vk xr rtmp platform shaders gameinput)
target_include_directories(ce_windows PRIVATE "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}/cppwinrt")

# --- Copy DLLs to output directory ---
add_custom_command(TARGET ce_windows POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:datachannel>"
    "$<TARGET_FILE_DIR:ce_windows>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE_DIR:datachannel>/libssl-3-x64.dll"
    "$<TARGET_FILE_DIR:ce_windows>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE_DIR:datachannel>/libcrypto-3-x64.dll"
    "$<TARGET_FILE_DIR:ce_windows>"
    COMMENT "Copying libdatachannel DLL to output directory"
)
