cmake_minimum_required(VERSION 3.13)
project(voice_commands)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(voice_commands PRIVATE "src/")
target_include_directories(voice_commands PRIVATE "/home/san/Fun/vosk-install/vosk-api/src")
install(FILES "lib/linux-x86_64/libpicovoice.so" DESTINATION "./lib")

# add libvosk.a to the build
add_library(vosk STATIC IMPORTED)
set_target_properties(vosk PROPERTIES IMPORTED_LOCATION "/home/san/Fun/SDRPlusPlus/misc_modules/voice_commands/lib/linux-x86_64/libvosk.a")
target_link_libraries(voice_commands PRIVATE vosk)

#install(FILES "lib/linux-x86_64/libvosk.so" DESTINATION "./lib/")