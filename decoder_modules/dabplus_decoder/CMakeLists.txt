cmake_minimum_required(VERSION 3.13)
project(dabplus_decoder)

# DAB/DAB+ as a radio mode: src/main.cpp registers the demodulator with every
# RadioModuleInterface, src/dab_demod.h implements the mode, src/dab_engine.cpp
# adapts the pinned upstream DAB-Radio engine. See THIRD_PARTY.md and
# src/deps.cmake.

# dabplus_fft_core.cpp is where the engine's transforms reach the core's FFT;
# it is compiled here, in the target that already links the core.
set(SRC "src/main.cpp" "src/dabplus_fft_core.cpp")

# Every static library this module builds (the engine, faad2, mpg123) is linked
# into the plugin's shared library, so all of it has to be position independent.
# GCC defaults to non-PIC objects for a static library, and the link then fails
# with "relocation R_X86_64_PC32 ... can not be used when making a shared
# object". CMake propagates this property to the subdirectories below.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(${SDRPP_MODULE_CMAKE})

# ---------------------------------------------------------------------------
# Dependencies: upstream commits, not vendored copies
# ---------------------------------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/src/deps.cmake)

# fmt: header-only, nothing to build or install
add_library(dabplus_fmt INTERFACE)
target_include_directories(dabplus_fmt INTERFACE "${fmt_stuff_SOURCE_DIR}/include")
target_compile_definitions(dabplus_fmt INTERFACE FMT_HEADER_ONLY=1)

# ViterbiDecoderCpp: header-only soft-decision decoder used by the FIC/MSC
add_library(dabplus_viterbi INTERFACE)
target_include_directories(dabplus_viterbi INTERFACE "${viterbi_stuff_SOURCE_DIR}/include")

# faad2 and mpg123: built from their pinned sources inside this build tree,
# statically, without install rules of their own.
set(BUILD_SHARED_LIBS OFF)
set(FAAD_BUNDLED_MODE ON CACHE BOOL "" FORCE)
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_LIBOUT123 OFF CACHE BOOL "" FORCE)
set(BUILD_LIBSYN123 OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${faad2_stuff_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/faad2" EXCLUDE_FROM_ALL)

# faad2 resolves libm with find_library(MATH_LIBRARY m). Where libm does not
# exist, as on Windows, that failed lookup is cached as MATH_LIBRARY-NOTFOUND,
# and any later subdirectory that links ${MATH_LIBRARY} then aborts the generate
# step with "variables are used in this project, but they are set to NOTFOUND".
# sondedump, fetched by the radiosonde module, is one. Drop the failed lookup
# instead of leaving it in the cache: nothing needs libm where it is not there.
if (MATH_LIBRARY MATCHES "-NOTFOUND$")
    unset(MATH_LIBRARY CACHE)
endif()

add_subdirectory("${mpg123_stuff_SOURCE_DIR}/ports/cmake" "${CMAKE_CURRENT_BINARY_DIR}/mpg123" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Engine library: the DAB-Radio pieces this module needs, plus the adapter
# ---------------------------------------------------------------------------
set(ENGINE_DIR "${dab_radio_SOURCE_DIR}/src")

add_library(dabplus_engine STATIC
    ${ENGINE_DIR}/ofdm/ofdm_demodulator.cpp
    ${ENGINE_DIR}/ofdm/coherent_demapper.cpp
    ${ENGINE_DIR}/ofdm/ofdm_demodulator_threads.cpp
    ${ENGINE_DIR}/ofdm/dab_prs_ref.cpp
    ${ENGINE_DIR}/ofdm/dab_ofdm_params_ref.cpp
    ${ENGINE_DIR}/ofdm/dab_mapper_ref.cpp
    ${ENGINE_DIR}/ofdm/dsp/apply_pll.cpp
    ${ENGINE_DIR}/ofdm/dsp/complex_conj_mul_sum.cpp

    ${ENGINE_DIR}/dab/algorithms/dab_viterbi_decoder.cpp
    ${ENGINE_DIR}/dab/algorithms/reed_solomon_decoder.cpp
    ${ENGINE_DIR}/dab/fic/fic_decoder.cpp
    ${ENGINE_DIR}/dab/fic/fig_processor.cpp
    ${ENGINE_DIR}/dab/constants/charsets.cpp
    ${ENGINE_DIR}/dab/database/dab_database_updater.cpp
    ${ENGINE_DIR}/dab/msc/msc_decoder.cpp
    ${ENGINE_DIR}/dab/msc/cif_deinterleaver.cpp
    ${ENGINE_DIR}/dab/msc/msc_data_group_processor.cpp
    ${ENGINE_DIR}/dab/msc/msc_data_packet_processor.cpp
    ${ENGINE_DIR}/dab/msc/msc_reed_solomon_data_packet_processor.cpp
    ${ENGINE_DIR}/dab/audio/aac_frame_processor.cpp
    ${ENGINE_DIR}/dab/audio/aac_audio_decoder.cpp
    ${ENGINE_DIR}/dab/audio/aac_data_decoder.cpp
    ${ENGINE_DIR}/dab/audio/mp2_audio_decoder.cpp
    ${ENGINE_DIR}/dab/mot/MOT_assembler.cpp
    ${ENGINE_DIR}/dab/mot/MOT_processor.cpp
    ${ENGINE_DIR}/dab/mot/MOT_slideshow_processor.cpp
    ${ENGINE_DIR}/dab/pad/pad_data_group.cpp
    ${ENGINE_DIR}/dab/pad/pad_data_length_indicator.cpp
    ${ENGINE_DIR}/dab/pad/pad_dynamic_label_assembler.cpp
    ${ENGINE_DIR}/dab/pad/pad_dynamic_label.cpp
    ${ENGINE_DIR}/dab/pad/pad_MOT_processor.cpp
    ${ENGINE_DIR}/dab/pad/pad_processor.cpp
    ${ENGINE_DIR}/dab/radio_fig_handler.cpp

    ${ENGINE_DIR}/basic_radio/basic_radio.cpp
    ${ENGINE_DIR}/basic_radio/basic_fic_runner.cpp
    ${ENGINE_DIR}/basic_radio/basic_audio_controls.cpp
    ${ENGINE_DIR}/basic_radio/basic_audio_channel.cpp
    ${ENGINE_DIR}/basic_radio/basic_dab_plus_channel.cpp
    ${ENGINE_DIR}/basic_radio/basic_dab_channel.cpp
    ${ENGINE_DIR}/basic_radio/basic_data_packet_channel.cpp
    ${ENGINE_DIR}/basic_radio/basic_slideshow.cpp

    src/dab_engine.cpp
)

# The engine's transforms go through src/dabplus_fft.h, whose implementation
# (src/dabplus_fft_core.cpp) runs on the FFT the application already uses -
# FFTW3f everywhere, vDSP on macOS when the accelerated FFT is enabled. That
# translation unit is built by the plugin, which links the core anyway, so no FFT
# library is resolved or linked a second time here.
target_include_directories(dabplus_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${ENGINE_DIR}"
    "${mpg123_stuff_SOURCE_DIR}/src/include"
)
target_link_libraries(dabplus_engine PUBLIC
    dabplus_fmt
    dabplus_viterbi
    faad
    libmpg123
)
set_target_properties(dabplus_engine PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
target_link_libraries(dabplus_engine PUBLIC Threads::Threads)

target_link_libraries(${PROJECT_NAME} PRIVATE dabplus_engine)

# ---------------------------------------------------------------------------
# Headless decoder, used by the e2e test and for offline checks:
#   dabplus_decoder_cli <recording.raw|recording.wav> [options]
# ---------------------------------------------------------------------------
if (NOT ANDROID)
    # It builds the same app-facing FFT translation unit as the plugin, so both
    # run the identical transform code against the core's FFT.
    add_executable(dabplus_decoder_cli src/cli.cpp src/dabplus_fft_core.cpp)
    # A standalone executable has to resolve the core symbols the engine calls,
    # so it links the core library explicitly. The plugin does not: its own link
    # against sdrpp_core already resolves them at load time.
    target_link_libraries(dabplus_decoder_cli PRIVATE dabplus_engine sdrpp_core)
    set_target_properties(dabplus_decoder_cli PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
endif ()
