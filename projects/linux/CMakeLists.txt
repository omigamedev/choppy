cmake_minimum_required (VERSION 3.28.0)

project("choppy_engine")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DUNICODE -D_UNICODE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")

set(MODULES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../modules)
add_subdirectory(${MODULES_DIR}/app modules/app)
#add_subdirectory(${MODULES_DIR}/mylib modules/mylib)
add_subdirectory(${MODULES_DIR}/vk modules/vk)
add_subdirectory(${MODULES_DIR}/xr modules/xr)
#add_subdirectory(${MODULES_DIR}/encoder modules/encoder)
#add_subdirectory(${MODULES_DIR}/rtmp modules/rtmp)
add_subdirectory(${MODULES_DIR}/platform modules/platform)
add_subdirectory(${MODULES_DIR}/shaders modules/shaders)

add_executable(ce_linux src/linux_main.cpp)
target_link_libraries(ce_linux PRIVATE app vk xr platform shaders)
