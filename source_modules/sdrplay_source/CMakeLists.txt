cmake_minimum_required(VERSION 3.13)
project(sdrplay_source)

message(STATUS "[SDRPLAY_SOURCE] ===== Configuring sdrplay_source module =====")

# Option to auto-download SDRPlay API
option(DOWNLOAD_SDRPLAY_API "Download and extract SDRPlay API automatically" ON)

message(STATUS "[SDRPLAY_SOURCE] DOWNLOAD_SDRPLAY_API = ${DOWNLOAD_SDRPLAY_API}")
set(SDRPLAY_API_URL "https://www.sdrplay.com/software/SDRplay_RSP_API-Linux-3.15.2.run" CACHE STRING "SDRPlay API download URL")
set(SDRPLAY_API_DEST "${CMAKE_BINARY_DIR}/sdrplay_api.run" CACHE STRING "Download destination for SDRPlay API")

if(DOWNLOAD_SDRPLAY_API)
    # Find curl or wget
    find_program(CURL_EXECUTABLE curl)
    find_program(WGET_EXECUTABLE wget)

    if(NOT EXISTS ${SDRPLAY_API_DEST})
        message(STATUS "SDRPlay API not found, downloading from ${SDRPLAY_API_URL}")
        if(CURL_EXECUTABLE)
            message(STATUS "Using curl to download SDRPlay API (verbose output follows)...")
            execute_process(
                COMMAND ${CURL_EXECUTABLE} -vL ${SDRPLAY_API_URL} -o ${SDRPLAY_API_DEST}
                RESULT_VARIABLE DOWNLOAD_RESULT
                OUTPUT_VARIABLE DOWNLOAD_OUTPUT
                ERROR_VARIABLE DOWNLOAD_ERROR
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_STRIP_TRAILING_WHITESPACE
            )
            message(STATUS "Curl stdout: ${DOWNLOAD_OUTPUT}")
            message(STATUS "Curl stderr: ${DOWNLOAD_ERROR}")
            if(NOT DOWNLOAD_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to download SDRPlay API using curl (exit code: ${DOWNLOAD_RESULT})")
            endif()
        elseif(WGET_EXECUTABLE)
            message(STATUS "Using wget to download SDRPlay API (verbose output follows)...")
            execute_process(
                COMMAND ${WGET_EXECUTABLE} -v ${SDRPLAY_API_URL} -O ${SDRPLAY_API_DEST}
                RESULT_VARIABLE DOWNLOAD_RESULT
                OUTPUT_VARIABLE DOWNLOAD_OUTPUT
                ERROR_VARIABLE DOWNLOAD_ERROR
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_STRIP_TRAILING_WHITESPACE
            )
            message(STATUS "Wget stdout: ${DOWNLOAD_OUTPUT}")
            message(STATUS "Wget stderr: ${DOWNLOAD_ERROR}")
            if(NOT DOWNLOAD_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to download SDRPlay API using wget (exit code: ${DOWNLOAD_RESULT})")
            endif()
        else()
            message(FATAL_ERROR "Neither curl nor wget found. Cannot download SDRPlay API.")
        endif()
        message(STATUS "SDRPlay API downloaded to: ${SDRPLAY_API_DEST}")
    else()
        message(STATUS "SDRPlay API already exists at: ${SDRPLAY_API_DEST}")
    endif()
endif()

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # Lib path
    target_link_directories(sdrplay_source PRIVATE "C:/Program Files/SDRplay/API/x64")
    target_include_directories(sdrplay_source PRIVATE "C:/Program Files/SDRplay/API/inc")

    target_link_libraries(sdrplay_source PRIVATE sdrplay_api)
else (MSVC)
    find_package(PkgConfig)

    # Include it because for some reason pkgconfig doesn't look here?
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_include_directories(sdrplay_source PRIVATE "/usr/local/include")
        target_link_directories(sdrplay_source PRIVATE "/usr/local/lib/")
    endif()

    target_link_libraries(sdrplay_source PRIVATE sdrplay_api)
endif ()

message(STATUS "[SDRPLAY_SOURCE] ===== sdrplay_source module configured =====")