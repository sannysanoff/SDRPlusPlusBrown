cmake_minimum_required(VERSION 3.13)
project(voice_commands)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(voice_commands PRIVATE "src/")
#target_link_libraries(voice_commands PRIVATE "picovoice")
#target_link_directories(voice_commands PRIVATE "lib/linux-x86_64")
install(FILES "lib/linux-x86_64/libpicovoice.so" DESTINATION "./lib/sdrpp/plugins")