add_library(platform OBJECT)
if(WIN32)
    set(PLATFORM_SRC windows.cppm)
else()
    set(PLATFORM_SRC android.cppm)
    target_link_libraries(platform PUBLIC game-activity::game-activity_static)
endif()
target_sources(platform PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES platform.cppm globals.cppm ${PLATFORM_SRC})
