cmake_minimum_required(VERSION 3.13)
project(tinycw_decoder)

# glibc < 2.34 keeps pthread_create in libpthread, so an executable that uses
# std::thread needs it on the link line explicitly.
find_package(Threads REQUIRED)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(tinycw_decoder PRIVATE "src/")

# The model is FastCW (src/tinycw_fastcw.c, weights embedded). No ONNX
# Runtime, no model file to fetch: the module builds and runs standalone.
# ---------------------------------------------------------------------------
# Headless entry point: feeds a WAV through the same Engine the module uses.
# ---------------------------------------------------------------------------
if (NOT ANDROID)
    add_executable(tinycw_cli
        cli/main.cpp
        src/tinycw_engine.cpp
        src/tinycw_model.cpp
        src/tinycw_fastcw.c)
    target_include_directories(tinycw_cli PRIVATE "src/")
    target_link_libraries(tinycw_cli PRIVATE sdrpp_core Threads::Threads)
    if (MSVC)
        target_compile_options(tinycw_cli PRIVATE /std:c++17 /EHsc)
    else ()
        target_compile_options(tinycw_cli PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-O2 -std=c++17>)
    endif ()
    if (APPLE)
        set_target_properties(tinycw_cli PROPERTIES
            BUILD_RPATH "$<TARGET_FILE_DIR:sdrpp_core>"
            INSTALL_RPATH "@loader_path")
    elseif (UNIX)
        set_target_properties(tinycw_cli PROPERTIES
            BUILD_RPATH "$<TARGET_FILE_DIR:sdrpp_core>"
            INSTALL_RPATH "$ORIGIN")
    endif ()
endif ()
