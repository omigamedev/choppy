add_library(app
    app_base.cpp)
target_link_libraries(app PUBLIC  xr vk)
target_sources(app PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES app.cppm)
