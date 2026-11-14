cmake_minimum_required(VERSION 3.13)
project(osa103_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

# No FTDI SDK at build time: libftd2xx is dlopen()ed at runtime.
if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE dl)
endif ()

target_include_directories(${PROJECT_NAME} PRIVATE "src/")
