cmake_minimum_required(VERSION 3.13)
project(m17_decoder)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(m17_decoder PRIVATE "src/")

if (MSVC)
    # Lib path
    target_include_directories(m17_decoder PRIVATE "C:/Program Files/codec2/include/")
    target_link_directories(m17_decoder PRIVATE "C:/Program Files/codec2/lib")

    target_link_libraries(m17_decoder PRIVATE libcodec2)
elseif (ANDROID)
    target_include_directories(m17_decoder PUBLIC
            ${SDR_KIT_ROOT}/${ANDROID_ABI}/include/codec2
    )

    target_link_libraries(m17_decoder PUBLIC
            ${SDR_KIT_ROOT}/${ANDROID_ABI}/lib/libcodec2.so
    )
else ()
    find_package(PkgConfig)

    pkg_check_modules(LIBCODEC2 REQUIRED codec2)

    target_include_directories(m17_decoder PRIVATE ${LIBCODEC2_INCLUDE_DIRS})
    target_include_directories(m17_decoder PRIVATE "/usr/include/codec2")
    target_link_directories(m17_decoder PRIVATE ${LIBCODEC2_LIBRARY_DIRS})
    target_link_libraries(m17_decoder PRIVATE ${LIBCODEC2_LIBRARIES})

    # Include it because for some reason pkgconfig doesn't look here?
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_include_directories(m17_decoder PRIVATE "/usr/local/include")
    endif()
endif ()