cmake_minimum_required(VERSION 3.13)
project(tci_server)

file(GLOB SRC "src/*.cpp")

add_library(tci_server SHARED ${SRC})
target_link_libraries(tci_server PRIVATE sdrpp_core)
set_target_properties(tci_server PROPERTIES PREFIX "")

target_include_directories(tci_server PRIVATE "src/")
target_include_directories(tci_server PRIVATE "../recorder/src")
target_include_directories(tci_server PRIVATE "../../decoder_modules/meteor_demodulator/src")
target_include_directories(tci_server PRIVATE "../../decoder_modules/radio/src")

if (MSVC)
    target_compile_options(tci_server PRIVATE /O2 /Ob2 /std:c++17 /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(tci_server PRIVATE -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
else ()
    target_compile_options(tci_server PRIVATE -O3 -std=c++17)
endif ()

# Install directives
install(TARGETS tci_server DESTINATION lib/sdrpp_brown/plugins)
