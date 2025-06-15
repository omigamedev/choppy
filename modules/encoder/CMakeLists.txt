include(FetchContent)
FetchContent_Declare(libyuv
    GIT_REPOSITORY https://chromium.googlesource.com/libyuv/libyuv
    GIT_TAG main)
FetchContent_MakeAvailable(libyuv)
add_library(encoder video_encoder.cpp audio_encoder.cpp)
target_include_directories(encoder PRIVATE ${libyuv_SOURCE_DIR}/include)
target_include_directories(encoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(encoder PRIVATE mediandk log yuv)
