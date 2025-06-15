add_library(platform)
target_link_libraries(platform PUBLIC std)
target_sources(platform PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES platform.cppm android.cppm windows.cppm)
