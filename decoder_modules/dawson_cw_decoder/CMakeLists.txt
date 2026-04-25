cmake_minimum_required(VERSION 3.13)
project(dawson_cw_decoder)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

add_library(dawson_cw_decoder SHARED ${SRC})
target_link_libraries(dawson_cw_decoder PRIVATE sdrpp_core)
set_target_properties(dawson_cw_decoder PROPERTIES PREFIX "")

if (ANDROID)
    target_link_libraries(dawson_cw_decoder PRIVATE log)
endif()

target_include_directories(dawson_cw_decoder PRIVATE "src/")

if (MSVC)
    target_compile_options(dawson_cw_decoder PRIVATE /std:c++17 /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(dawson_cw_decoder PRIVATE -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
else ()
    target_compile_options(dawson_cw_decoder PRIVATE -O3 -std=c++17)
endif ()

# Install directives
install(TARGETS dawson_cw_decoder DESTINATION lib/sdrpp/plugins)
