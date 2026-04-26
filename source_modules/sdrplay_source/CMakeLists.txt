cmake_minimum_required(VERSION 3.13)
project(sdrplay_source)

# Option to auto-download SDRPlay API
option(DOWNLOAD_SDRPLAY_API "Download and extract SDRPlay API automatically" ON)
set(SDRPLAY_API_URL "https://www.sdrplay.com/software/SDRplay_RSP_API-Linux-3.15.2.run" CACHE STRING "SDRPlay API download URL")
set(SDRPLAY_API_DEST "${CMAKE_BINARY_DIR}/sdrplay_api.run" CACHE STRING "Download destination for SDRPlay API")

if(DOWNLOAD_SDRPLAY_API)
    # Find curl or wget
    find_program(CURL_EXECUTABLE curl)
    find_program(WGET_EXECUTABLE wget)

    if(CURL_EXECUTABLE)
        # Use curl with full verbose output
        add_custom_target(download_sdrplay_api
            COMMAND ${CURL_EXECUTABLE} -vL ${SDRPLAY_API_URL} -o ${SDRPLAY_API_DEST}
            COMMENT "Downloading SDRPlay API using curl..."
            VERBATIM
        )
    elseif(WGET_EXECUTABLE)
        # Fallback to wget
        add_custom_target(download_sdrplay_api
            COMMAND ${WGET_EXECUTABLE} -v ${SDRPLAY_API_URL} -O ${SDRPLAY_API_DEST}
            COMMENT "Downloading SDRPlay API using wget..."
            VERBATIM
        )
    else()
        message(FATAL_ERROR "Neither curl nor wget found. Cannot download SDRPlay API.")
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