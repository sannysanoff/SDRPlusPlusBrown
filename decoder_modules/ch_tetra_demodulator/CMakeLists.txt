cmake_minimum_required(VERSION 3.13)
project(tetra_demodulator)

set(CMAKE_INSTALL_PREFIX "/usr/")
include(GNUInstallDirs)

set(ETSI_CODEC_URL "http://www.etsi.org/deliver/etsi_en/300300_300399/30039502/01.03.01_60/en_30039502v010301p0.zip")
set(ETSI_CODEC_MD5 "a8115fe68ef8f8cc466f4192572a1e3e")
set(ETSI_CODEC_DIR "${CMAKE_CURRENT_BINARY_DIR}/etsi_codec")
set(ETSI_CODEC_ZIP "${ETSI_CODEC_DIR}/etsi_tetra_codec.zip")
set(ETSI_CODEC_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/decoder/etsi_codec-patches")
set(ETSI_CODEC_MARKER "${ETSI_CODEC_DIR}/.patched")
set(ETSI_CODEC_EXTRACTED_DIR "${ETSI_CODEC_DIR}/en_30039502v010301p0")

if (NOT EXISTS "${ETSI_CODEC_MARKER}")
    file(MAKE_DIRECTORY "${ETSI_CODEC_DIR}")
    if (NOT EXISTS "${ETSI_CODEC_ZIP}")
        message("Downloading ETSI TETRA codec")
        file(DOWNLOAD "${ETSI_CODEC_URL}" "${ETSI_CODEC_ZIP}" EXPECTED_MD5 "${ETSI_CODEC_MD5}")
    else ()
        message("Not downloading ETSI TETRA codec.")
    endif ()
    message("Extracting ETSI TETRA codec.")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${ETSI_CODEC_ZIP}"
        WORKING_DIRECTORY "${ETSI_CODEC_DIR}"
    )
    if (EXISTS "${ETSI_CODEC_EXTRACTED_DIR}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${ETSI_CODEC_EXTRACTED_DIR}" "${ETSI_CODEC_DIR}"
        )
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ETSI_CODEC_EXTRACTED_DIR}"
        )
    endif ()
    if (EXISTS "${ETSI_CODEC_DIR}/C-CODE")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E rename "${ETSI_CODEC_DIR}/C-CODE" "${ETSI_CODEC_DIR}/c-code"
        )
    endif ()
    file(STRINGS "${ETSI_CODEC_PATCH_DIR}/series" ETSI_PATCHES)
    foreach (PATCH_FILE IN LISTS ETSI_PATCHES)
        execute_process(
            COMMAND patch -p1 --batch --forward -i "${ETSI_CODEC_PATCH_DIR}/${PATCH_FILE}"
            WORKING_DIRECTORY "${ETSI_CODEC_DIR}"
            RESULT_VARIABLE ETSI_PATCH_RESULT
        )
        if (NOT ETSI_PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to apply ETSI patch: ${PATCH_FILE}")
        endif ()
    endforeach ()
    file(WRITE "${ETSI_CODEC_MARKER}" "ok\n")
endif ()

file(GLOB SRC LIST_DIRECTORIES false 
    "src/*.cpp"
    "src/*.c"
    "src/dsp/*.cpp"
    "src/decoder/src/*.cpp"
    "src/decoder/src/*.c"
    "src/decoder/src/crypto/*.cpp"
    "src/decoder/src/crypto/*.c"
    "src/decoder/src/lower_mac/*.cpp"
    "src/decoder/src/lower_mac/*.c"
    "src/decoder/src/phy/*.cpp"
    "src/decoder/src/phy/*.c"
    )

add_library(etsi_codec STATIC
    ${ETSI_CODEC_DIR}/c-code/cdec_tet.c
    ${ETSI_CODEC_DIR}/c-code/sub_cd.c
    ${ETSI_CODEC_DIR}/c-code/tetra_op.c
    ${ETSI_CODEC_DIR}/c-code/sdec_tet.c
    ${ETSI_CODEC_DIR}/c-code/sub_sc_d.c
    ${ETSI_CODEC_DIR}/c-code/sub_dsp.c
    ${ETSI_CODEC_DIR}/c-code/fbas_tet.c
    ${ETSI_CODEC_DIR}/c-code/fexp_tet.c
    ${ETSI_CODEC_DIR}/c-code/fmat_tet.c
)

set_target_properties(etsi_codec PROPERTIES LINKER_LANGUAGE C)

target_include_directories(etsi_codec PRIVATE "${ETSI_CODEC_DIR}/c-code" "${ETSI_CODEC_DIR}/amr-code")

set_source_files_properties(
    src/decoder/src/tetra_tdma.c
    src/decoder/src/tetra_upper_mac.c
    src/decoder/src/tetra_common.c
    src/decoder/src/tetra_mle.c
    src/decoder/src/tetra_mle_pdu.c
    src/decoder/src/tetra_mm_pdu.c
    src/decoder/src/tetra_sndcp_pdu.c
    src/decoder/src/tetra_cmce_pdu.c
    src/decoder/src/tetra_mac_pdu.c
    src/decoder/src/crypto/hurdle.c
    src/decoder/src/crypto/taa1.c
    src/decoder/src/crypto/tea1.c
    src/decoder/src/crypto/tea2.c
    src/decoder/src/crypto/tea3.c
    src/decoder/src/crypto/tetra_crypto.c
    src/decoder/src/lower_mac/osmo_conv.c
    src/decoder/src/lower_mac/crc_simple.c
    src/decoder/src/lower_mac/tch_reordering.c
    src/decoder/src/lower_mac/tetra_conv_enc.c
    src/decoder/src/lower_mac/tetra_interleave.c
    src/decoder/src/lower_mac/tetra_lower_mac.c
    src/decoder/src/lower_mac/tetra_rm3014.c
    src/decoder/src/lower_mac/tetra_scramb.c
    src/decoder/src/lower_mac/viterbi.c
    src/decoder/src/lower_mac/viterbi_cch.c
    src/decoder/src/lower_mac/viterbi_tch.c
    src/decoder/src/phy/tetra_burst.c
    src/decoder/src/phy/tetra_burst_sync.c
    PROPERTIES LANGUAGE C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")

if (NOT SDRPP_MODULE_CMAKE)
    set(SDRPP_MODULE_CMAKE "/usr/share/cmake/Modules/sdrpp_module.cmake")
endif ()


include(${SDRPP_MODULE_CMAKE})

if(MSVC)
  # target_compile_options(tetra_demodulator PRIVATE /W4 /WX)
else()
  # target_compile_options(tetra_demodulator PRIVATE -Wall -Wextra)
endif()

target_include_directories(tetra_demodulator PRIVATE BEFORE "src/" "src/decoder/src" "${ETSI_CODEC_DIR}" "${ETSI_CODEC_DIR}/c-code" )

target_link_libraries(tetra_demodulator PRIVATE etsi_codec)
