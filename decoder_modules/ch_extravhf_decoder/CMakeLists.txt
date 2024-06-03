cmake_minimum_required(VERSION 3.13)
project(ch_extravhf_decoder)


message("HERE ${VCPKG_INSTALLED_DIR}")

set(DLROOT ${CMAKE_BINARY_DIR})
if (EXISTS ${DLROOT}/mbelib)
    message("Not downloading mbelib")
else ()
    message("Downloading mbelib")
    file(DOWNLOAD https://codeload.github.com/szechyjs/mbelib/tar.gz/9a04ed5c78176a9965f3d43f7aa1b1f5330e771f ${DLROOT}/mbelib.tar.gz)
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xfz mbelib.tar.gz WORKING_DIRECTORY ${DLROOT})
    FILE(GLOB CONTENT ${DLROOT}/mbelib-*)
    FILE(RENAME ${CONTENT} ${DLROOT}/mbelib)
    FILE(READ "${DLROOT}/mbelib/CMakeLists.txt" TMPF)
    string(REGEX REPLACE "add_custom_target.uninstall" "add_custom_target(mbelib_uninstall" TMPF "${TMPF}")
    string(REGEX REPLACE "OUTPUT_NAME mbe" "" TMPF "${TMPF}")
    string(REGEX REPLACE " OFF" " ON" TMPF "${TMPF}")  # disable test
    FILE(WRITE "${DLROOT}/mbelib/CMakeLists.txt" "${TMPF}")
endif ()

# add_custom_target(uninstall
if(NOT MSVC)
    if (EXISTS ${DLROOT}/itpp)
        message("Not downloading itpp")
    else ()
        message("Downloading itpp")
        file(DOWNLOAD https://codeload.github.com/mmeidlinger/itpp//tar.gz/d29deaf7560ed84818a82aa2cc6e4b9337ee9017 ${DLROOT}/itpp.tar.gz)
        execute_process(COMMAND ${CMAKE_COMMAND} -E tar xfz itpp.tar.gz WORKING_DIRECTORY ${DLROOT})
        FILE(GLOB CONTENT ${DLROOT}/itpp-*)
        FILE(RENAME ${CONTENT} ${DLROOT}/itpp)

        FILE(READ "${DLROOT}/itpp/CMakeLists.txt" TMPF)
        string(REGEX REPLACE "option.ITPP_SHARED_LIB" "option(ZZZZ_ITPP_SHARED_LIB" TMPF "${TMPF}")
        FILE(WRITE "${DLROOT}/itpp/CMakeLists.txt" "${TMPF}")


    endif ()
    FILE(REMOVE ${DLROOT}/itpp/version)
    set(CMAKE_MODULE_PATH "${DLROOT}/itpp/cmake" ${CMAKE_MODULE_PATH})
    add_subdirectory(${DLROOT}/itpp ${DLROOT}/itpp-build)

    if ((CMAKE_BUILD_TYPE STREQUAL Release) OR (NOT CMAKE_BUILD_TYPE))
        set (libitpp_target itpp_static)
    else()
        set (libitpp_target itpp_static_debug)
    endif()
    target_compile_definitions(${libitpp_target} PUBLIC TIME_WITH_SYS_TIME)
    target_compile_options(${libitpp_target} PUBLIC "-fPIC")

endif()

FILE(REMOVE ${DLROOT}/mbelib/config.h)
add_subdirectory(${DLROOT}/mbelib ${DLROOT}/mbelib-build)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.h" "src/*.hpp" "src/*.c")

include(${SDRPP_MODULE_CMAKE})


if (NOT MSVC)
    target_compile_options(mbe-static PUBLIC "-fPIC")
    target_include_directories(ch_extravhf_decoder PUBLIC "${DLROOT}/itpp" "${DLROOT}")
endif()

if(ANDROID)
    target_link_libraries(ch_extravhf_decoder PUBLIC ${SDR_KIT_ROOT}/${ANDROID_ABI}/lib/libfftw3f.so)
elseif(MSVC)
    target_link_directories(ch_extravhf_decoder PUBLIC "C:/Program Files/PothosSDR/lib/")
    target_link_libraries(ch_extravhf_decoder PUBLIC volk)
    target_link_libraries(ch_extravhf_decoder PUBLIC itpp)
    target_link_directories(ch_extravhf_decoder PUBLIC "${VCPKG_INSTALLED_DIR}/x64-windows/lib")
    find_package(FFTW3f CONFIG REQUIRED)
    target_link_libraries(ch_extravhf_decoder PUBLIC FFTW3::fftw3f)
else()
    target_link_libraries(ch_extravhf_decoder PUBLIC fftw3 )
endif()

target_link_libraries(ch_extravhf_decoder PUBLIC ${libitpp_target} mbe-static)
target_include_directories(ch_extravhf_decoder PUBLIC "src/" ${CMAKE_CURRENT_LIST_DIR}/../../core/src/ ${DLROOT}/mbelib)
