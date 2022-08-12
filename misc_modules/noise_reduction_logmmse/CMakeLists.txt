cmake_minimum_required(VERSION 3.13)
project(noise_reduction_logmmse)

file(GLOB SRC "src/*.cpp")

add_library(noise_reduction_logmmse SHARED ${SRC})
target_link_libraries(noise_reduction_logmmse PRIVATE sdrpp_core)
set_target_properties(noise_reduction_logmmse PROPERTIES PREFIX "")

target_include_directories(noise_reduction_logmmse PRIVATE "src/")
target_include_directories(noise_reduction_logmmse PRIVATE "../../decoder_modules/radio/src")

if (MSVC)
    target_compile_options(noise_reduction_logmmse PRIVATE /O2 /Ob2 /std:c++17 /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(noise_reduction_logmmse PRIVATE -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
else ()
    target_compile_options(noise_reduction_logmmse PRIVATE -O3 -std=c++17)
endif ()

# Install directives
install(TARGETS noise_reduction_logmmse DESTINATION lib/sdrpp/plugins)