cmake_minimum_required(VERSION 3.13)
project(tinycw_decoder)

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
    target_link_libraries(tinycw_cli PRIVATE sdrpp_core)
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

    # Single-channel debug probe for the statistical (multi-window) decoder.
    add_executable(tinycw_focus_probe
        cli/focus_probe.cpp
        src/tinycw_model.cpp
        src/tinycw_fastcw.c)
    target_include_directories(tinycw_focus_probe PRIVATE "src/")
    target_link_libraries(tinycw_focus_probe PRIVATE sdrpp_core)
    if (MSVC)
        target_compile_options(tinycw_focus_probe PRIVATE /std:c++17 /EHsc)
    else ()
        target_compile_options(tinycw_focus_probe PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-O2 -std=c++17>)
    endif ()
    if (APPLE)
        set_target_properties(tinycw_focus_probe PROPERTIES
            BUILD_RPATH "$<TARGET_FILE_DIR:sdrpp_core>"
            INSTALL_RPATH "@loader_path")
    elseif (UNIX)
        set_target_properties(tinycw_focus_probe PROPERTIES
            BUILD_RPATH "$<TARGET_FILE_DIR:sdrpp_core>"
            INSTALL_RPATH "$ORIGIN")
    endif ()

    # Whole-clip check of the single-band chain against DeepFist (Python).
    add_executable(tinycw_fastcw_check
        cli/fastcw_check.cpp
        src/tinycw_model.cpp
        src/tinycw_fastcw.c)
    target_include_directories(tinycw_fastcw_check PRIVATE "src/")
    if (NOT MSVC)
        target_compile_options(tinycw_fastcw_check PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-O2 -std=c++17>)
    endif ()
endif ()
