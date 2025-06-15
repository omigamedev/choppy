add_library(std SHARED)
if (WIN32)
    message("using MSVC std modules")
    target_sources(std PUBLIC 
        FILE_SET CXX_MODULES
        BASE_DIRS "$ENV{VCToolsInstallDir}/modules/"
        FILES "$ENV{VCToolsInstallDir}/modules/std.ixx")
    #set_target_properties(std PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
else()
    message("using custom std modules")
    target_sources(std PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES std.cppm)
endif()
