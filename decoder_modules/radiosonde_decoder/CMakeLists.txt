cmake_minimum_required(VERSION 3.13)
project(radiosonde_decoder C CXX)

set(SONDEDUMP_COMMIT "528655564f34a42e464334fced4f7e89f9bfee46")
set(SONDEDUMP_SHA256 "ca2f42b003886654c8684bc477e309ac1aca612b3cf7d078d6db503183a7a099")
set(DLROOT ${CMAKE_BINARY_DIR})

if (EXISTS "${DLROOT}/sondedump")
    message("Not downloading sondedump")
else ()
    message("Downloading sondedump (${SONDEDUMP_COMMIT})")
    file(DOWNLOAD
        "https://codeload.github.com/dbdexter-dev/sondedump/tar.gz/${SONDEDUMP_COMMIT}"
        "${DLROOT}/sondedump.tar.gz"
        EXPECTED_HASH SHA256=${SONDEDUMP_SHA256}
        STATUS SONDEDUMP_DL_STATUS
    )
    list(GET SONDEDUMP_DL_STATUS 0 SONDEDUMP_DL_ERR)
    if (NOT SONDEDUMP_DL_ERR EQUAL 0)
        file(REMOVE "${DLROOT}/sondedump.tar.gz")
        message(FATAL_ERROR "Failed to download sondedump: ${SONDEDUMP_DL_STATUS}")
    endif ()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xfz "${DLROOT}/sondedump.tar.gz"
        WORKING_DIRECTORY "${DLROOT}"
        RESULT_VARIABLE SONDEDUMP_TAR_RES
    )
    if (NOT SONDEDUMP_TAR_RES EQUAL 0)
        message(FATAL_ERROR "Failed to extract sondedump.tar.gz")
    endif ()

    if (EXISTS "${DLROOT}/sondedump-${SONDEDUMP_COMMIT}")
        file(RENAME "${DLROOT}/sondedump-${SONDEDUMP_COMMIT}" "${DLROOT}/sondedump")
    else ()
        file(GLOB SONDEDUMP_DIRS "${DLROOT}/sondedump-*")
        list(FILTER SONDEDUMP_DIRS EXCLUDE REGEX "\\.tar\\.gz$")
        list(GET SONDEDUMP_DIRS 0 SONDEDUMP_EXTRACTED)
        file(RENAME "${SONDEDUMP_EXTRACTED}" "${DLROOT}/sondedump")
    endif ()
    file(REMOVE "${DLROOT}/sondedump.tar.gz")
endif ()

if (NOT EXISTS "${DLROOT}/sondedump/.sdrpp_patched_v1")
    # Patch sondedump CMakeLists.txt for cross-platform safety (Windows/MSVC, macOS, Linux, Android)
    file(READ "${DLROOT}/sondedump/CMakeLists.txt" SD_CMAKE)
    string(REPLACE "VERSION 3.10" "VERSION 3.13" SD_CMAKE "${SD_CMAKE}")
    string(REPLACE "/Wall /std:c11" "/W1 /std:c11 /D_CRT_SECURE_NO_WARNINGS" SD_CMAKE "${SD_CMAKE}")
    string(REPLACE "-flto" "" SD_CMAKE "${SD_CMAKE}")
    string(REPLACE "if (is_arm EQUAL \"0\")" "if (FALSE)" SD_CMAKE "${SD_CMAKE}")
    string(REPLACE "execute_process(COMMAND \"\${CMAKE_C_COMPILER}\" \"-dumpmachine\" COMMAND \"grep\" \"arm\" OUTPUT_QUIET RESULT_VARIABLE is_arm)" "# arm probe disabled for cross-builds" SD_CMAKE "${SD_CMAKE}")
    string(REPLACE "-DSOURCE_BASE_PATH_SIZE=\${SOURCE_BASE_PATH_SIZE}" "-DSOURCE_BASE_PATH_SIZE=0" SD_CMAKE "${SD_CMAKE}")
    string(REPLACE "add_definitions(-D__global=__attribute__\\(\\(visibility\\(\\\"default\\\"\\)\\)\\))" "add_definitions(-D__global=)" SD_CMAKE "${SD_CMAKE}")
    file(WRITE "${DLROOT}/sondedump/CMakeLists.txt" "${SD_CMAKE}")

    # Disable hardcoded /tmp/* debug file creation in Debug builds
    foreach(SD_DBG_FILE
        demod/afsk.c
        demod/gfsk.c
        sonde/dfm09/dfm09.c
        sonde/mrz-n1/mrzn1.c
        sonde/rs41/rs41.c
        sonde/imet4/imet4.c
        sonde/c50/c50.c
        sonde/m10/m10.c
        sonde/ims100/ims100.c
    )
        file(READ "${DLROOT}/sondedump/${SD_DBG_FILE}" SD_SRC)
        string(REPLACE "fopen(\"/tmp/" "NULL; // fopen(\"/tmp/" SD_SRC "${SD_SRC}")
        file(WRITE "${DLROOT}/sondedump/${SD_DBG_FILE}" "${SD_SRC}")
    endforeach()

    # Add bounds and alignment guards on RF-decoded fragment indices
    file(READ "${DLROOT}/sondedump/sonde/rs41/rs41.c" SD_RS41)
    string(REPLACE
        "frag_offset = s->frag_seq * LEN(s->frag_data);"
        "if (s->frag_seq >= RS41_CALIB_FRAGCOUNT) return;\n\tfrag_offset = s->frag_seq * LEN(s->frag_data);\n\tif (frag_offset + LEN(s->frag_data) > sizeof(m->data) || s->frag_seq / 8 >= LEN(m->bitmask)) return;"
        SD_RS41 "${SD_RS41}"
    )
    string(REPLACE
        "if (crc16_ccitt_false(subframe->data, subframe->len) == *(uint16_t*)&subframe->data[subframe->len]) {"
        "uint16_t rx_crc = 0;\n\t\tmemcpy(&rx_crc, &subframe->data[subframe->len], sizeof(rx_crc));\n\t\tif (crc16_ccitt_false(subframe->data, subframe->len) == rx_crc) {"
        SD_RS41 "${SD_RS41}"
    )
    file(WRITE "${DLROOT}/sondedump/sonde/rs41/rs41.c" "${SD_RS41}")

    file(READ "${DLROOT}/sondedump/sonde/mrz-n1/mrzn1.c" SD_MRZN1)
    string(REPLACE
        "memcpy(((uint8_t*)&calib->data) + MRZN1_CALIB_FRAGSIZE * seq, data, MRZN1_CALIB_FRAGSIZE);"
        "if (seq < 0 || seq >= MRZN1_CALIB_FRAGCOUNT) return;\n\tmemcpy(((uint8_t*)&calib->data) + MRZN1_CALIB_FRAGSIZE * seq, data, MRZN1_CALIB_FRAGSIZE);"
        SD_MRZN1 "${SD_MRZN1}"
    )
    file(WRITE "${DLROOT}/sondedump/sonde/mrz-n1/mrzn1.c" "${SD_MRZN1}")

    file(WRITE "${DLROOT}/sondedump/.sdrpp_patched_v1" "1\n")
endif ()

set(UNINSTALL_TARGET_SAVED "${UNINSTALL_TARGET}")
set(UNINSTALL_TARGET OFF CACHE INTERNAL "")
set(ENABLE_TUI OFF CACHE INTERNAL "")
set(ENABLE_AUDIO OFF CACHE INTERNAL "")
add_subdirectory("${DLROOT}/sondedump" "${DLROOT}/sondedump-build" EXCLUDE_FROM_ALL)
set(UNINSTALL_TARGET "${UNINSTALL_TARGET_SAVED}")
set_target_properties(radiosonde PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(SRC
    src/decode/common.hpp
    src/decode/decoder.hpp
    src/gpx.cpp src/gpx.hpp
    src/ptu.cpp src/ptu.hpp
    src/utils.cpp src/utils.hpp
    src/main.cpp src/main.hpp
)

include(${SDRPP_MODULE_CMAKE})

target_include_directories(radiosonde_decoder PRIVATE "src/")
target_link_libraries(radiosonde_decoder PRIVATE radiosonde)
if (ANDROID)
    target_link_libraries(radiosonde_decoder PRIVATE log)
endif ()
