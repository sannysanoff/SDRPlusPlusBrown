cmake_minimum_required(VERSION 3.13)
project(brown_audio_sink)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # RtAudio install prefix (the upstream CI default is kept as fallback).
    set(RTAUDIO_DIR "C:/Program Files (x86)/RtAudio" CACHE PATH "RtAudio install prefix")

    # Lib path
    target_link_directories(brown_audio_sink PRIVATE "${RTAUDIO_DIR}/lib")

    # Misc headers
    target_include_directories(brown_audio_sink PRIVATE "${RTAUDIO_DIR}/include/rtaudio")

    target_link_libraries(brown_audio_sink PRIVATE rtaudio)
else (MSVC)
    find_package(PkgConfig)

    pkg_check_modules(RTAUDIO REQUIRED rtaudio)

    target_include_directories(brown_audio_sink PRIVATE ${RTAUDIO_INCLUDE_DIRS})
    target_link_directories(brown_audio_sink PRIVATE ${RTAUDIO_LIBRARY_DIRS})
    target_link_libraries(brown_audio_sink PRIVATE ${RTAUDIO_LIBRARIES})

endif ()

if (APPLE)
    target_link_libraries(brown_audio_sink PRIVATE "-framework CoreAudio" "-framework CoreFoundation")
endif ()