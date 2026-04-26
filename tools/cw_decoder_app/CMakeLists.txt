cmake_minimum_required(VERSION 3.10)
project(cw_decoder_app CXX)

# Standalone CW Decoder App - no SDR++ dependencies
# Requires FFTW3 for FFT processing

find_package(Threads REQUIRED)

# Decoder core sources (from dawson_cw_decoder module)
set(DECODER_SOURCES
    ${CMAKE_SOURCE_DIR}/decoder_modules/dawson_cw_decoder/src/cw_decoder_core.cpp
    ${CMAKE_SOURCE_DIR}/decoder_modules/dawson_cw_decoder/src/cw_classifier.cpp
    ${CMAKE_SOURCE_DIR}/decoder_modules/dawson_cw_decoder/src/morse_data.cpp
)

set(DECODER_HEADERS
    ${CMAKE_SOURCE_DIR}/decoder_modules/dawson_cw_decoder/src/cw_decoder_core.h
    ${CMAKE_SOURCE_DIR}/decoder_modules/dawson_cw_decoder/src/cw_classifier.h
    ${CMAKE_SOURCE_DIR}/decoder_modules/dawson_cw_decoder/src/morse_data.h
)

add_executable(cw_decoder_app
    main.cpp
    ${DECODER_SOURCES}
)

target_include_directories(cw_decoder_app PRIVATE
    ${CMAKE_SOURCE_DIR}/decoder_modules/dawson_cw_decoder/src
)

# Link FFTW3
find_library(FFTW3_LIB fftw3 PATHS
    /opt/data/.local/build_deps/usr/lib/x86_64-linux-gnu
    /usr/lib/x86_64-linux-gnu
)

if(FFTW3_LIB)
    message(STATUS "Found FFTW3: ${FFTW3_LIB}")
    target_link_libraries(cw_decoder_app PRIVATE ${FFTW3_LIB} m)
    target_include_directories(cw_decoder_app PRIVATE
        /opt/data/.local/build_deps/usr/include
    )
else()
    message(FATAL_ERROR "FFTW3 not found")
endif()

target_link_libraries(cw_decoder_app PRIVATE Threads::Threads)

# Use C++17
set_target_properties(cw_decoder_app PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

install(TARGETS cw_decoder_app DESTINATION bin)
