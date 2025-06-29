add_library(platform)
if(WIN32)
    set(PLATFORM_SRC windows.cppm)
else()
    set(PLATFORM_SRC android.cppm)
endif()
target_sources(platform PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES platform.cppm ${PLATFORM_SRC})
