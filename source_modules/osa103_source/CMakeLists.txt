cmake_minimum_required(VERSION 3.13)
project(osa103_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

# No FTDI SDK at build time: libftd2xx is dlopen()ed at runtime.
if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE dl)
endif ()

target_include_directories(${PROJECT_NAME} PRIVATE "src/")

# Headless stream test / bring-up tool (see README, "Stream test tool"). Off by
# default because it is a diagnostic binary, not part of the module.
option(OPT_BUILD_OSA103_STREAM_TEST "Build the standalone OSA103 stream test tool" OFF)
if (OPT_BUILD_OSA103_STREAM_TEST)
    add_executable(osa103_stream_test
        tools/osa103_stream_test.cpp
        src/osa103_proto.cpp
        src/ftdi_d2xx.cpp
    )
    target_include_directories(osa103_stream_test PRIVATE "src/")
    if (UNIX AND NOT APPLE)
        target_link_libraries(osa103_stream_test PRIVATE dl)
    endif ()
endif ()
