set(MYLIB_BIN ${CMAKE_BINARY_DIR}/rust/mylib)
set(MYLIB_H   ${MYLIB_BIN}/lib.h)
set(MYLIB_CPP ${MYLIB_BIN}/lib.cpp)
if (MSVC)
    set(MYLIB_LIBS ws2_32 ntdll userenv)
    set(ARCH x86_64-pc-windows-msvc)
    set(MYLIB_LIB ${MYLIB_BIN}/debug/mylib_static.lib)
    set(CARGO_CMD cargo)
else()
    set(ARCH aarch64-linux-android)
    set(MYLIB_LIB ${MYLIB_BIN}/${ARCH}/debug/libmylib_static.a)
    set(CARGO_CMD cargo ndk --platform 35 --target ${ARCH})
endif()
file(GLOB_RECURSE RUST_DEP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*)
add_custom_command(
    OUTPUT ${MYLIB_LIB} ${MYLIB_H} ${MYLIB_CPP}
    COMMAND ${CMAKE_COMMAND} -E env RUSTFLAGS="-Clink-args=/MDd" CARGO_LOG=cargo::core::compiler::job=info ${CARGO_CMD} build --target-dir ${MYLIB_BIN} --verbose
    COMMAND cxxbridge src/lib.rs -o ${MYLIB_H}
    COMMAND cxxbridge src/lib.rs -o ${MYLIB_CPP}
    DEPENDS ${RUST_DEP_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_custom_target(build_rust DEPENDS ${MYLIB_LIB} ${MYLIB_H} ${MYLIB_CPP})
add_library(mylib_static STATIC IMPORTED)
add_dependencies(mylib_static build_rust)
set_target_properties(mylib_static PROPERTIES IMPORTED_LOCATION ${MYLIB_LIB})
#target_include_directories(build_rust INTERFACE ${MYLIB_BIN})
add_library(mylib_static_cpp ${MYLIB_CPP})
target_link_libraries(mylib_static_cpp PUBLIC mylib_static ${MYLIB_LIBS})
target_include_directories(mylib_static_cpp PUBLIC ${MYLIB_BIN})
set_target_properties(mylib_static_cpp PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

file(GLOB_RECURSE RUST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.rs
    ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
)
add_custom_target(RustSources SOURCES ${RUST_SOURCES})
