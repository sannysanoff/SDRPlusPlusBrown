cmake_minimum_required(VERSION 3.13)
project(ft8_decoder)


file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c" ../../misc_modules/recorder/src/wav.cpp ../../misc_modules/recorder/src/riff.cpp)
list(FILTER SRC EXCLUDE REGEX "sdrpp_ft8_mshv_main.cpp")
#list(FILTER SRC EXCLUDE REGEX "ft8_etc/*" )

add_library(ft8_decoder SHARED ${SRC})
target_link_libraries(ft8_decoder PRIVATE sdrpp_core)
set_target_properties(ft8_decoder PROPERTIES PREFIX "")
if (ANDROID)
    target_link_libraries(ft8_decoder PRIVATE log)
endif()

target_include_directories(ft8_decoder PRIVATE "src/")

if (MSVC)
    target_compile_options(ft8_decoder PRIVATE /std:c++17 /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(ft8_decoder PRIVATE -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
else ()
    target_compile_options(ft8_decoder PRIVATE -O3 -std=c++17)
endif ()

# Install directives
install(TARGETS ft8_decoder DESTINATION lib/sdrpp/plugins)

if (NEVER)

    file(GLOB_RECURSE SDRPP_FT8MSHV_SRC src/*.cpp src/*.c ../../core/src/utils/networking.cpp ../../core/src/utils/flog.cpp EXCLUDE main.c)
    list(FILTER SDRPP_FT8MSHV_SRC EXCLUDE REGEX "main.c*")

    if (MSVC)
        # winmain-type code
        add_executable(sdrpp_ft8_mshv WIN32 "src/sdrpp_ft8_mshv_main.cpp" "src/sdrpp_ft8_mshv.cpp" ${SDRPP_FT8MSHV_SRC})
    else ()
        add_executable(sdrpp_ft8_mshv "src/sdrpp_ft8_mshv_main.cpp" "src/sdrpp_ft8_mshv.cpp" ${SDRPP_FT8MSHV_SRC})
    endif ()

    target_include_directories(sdrpp_ft8_mshv PUBLIC "../../core/src")

    if (MSVC)
        # Lib path
        target_link_directories(sdrpp_ft8_mshv PUBLIC "C:/Program Files/PothosSDR/lib/")

        # Misc headers
        target_include_directories(sdrpp_ft8_mshv PUBLIC "C:/Program Files/PothosSDR/include/")

        # Volk
        target_link_libraries(sdrpp_ft8_mshv PUBLIC volk)

        # FFTW3
        find_package(FFTW3f CONFIG REQUIRED)
        target_link_libraries(sdrpp_ft8_mshv PUBLIC FFTW3::fftw3f)

        # WinSock2
        target_link_libraries(sdrpp_ft8_mshv PUBLIC wsock32 ws2_32 iphlpapi)

    elseif (ANDROID)

        target_include_directories(sdrpp_ft8_mshv PUBLIC
                ${SDR_KIT_ROOT}/${ANDROID_ABI}/include
                )
        target_link_libraries(sdrpp_ft8_mshv PUBLIC
                ${SDR_KIT_ROOT}/${ANDROID_ABI}/lib/libvolk.so
                ${SDR_KIT_ROOT}/${ANDROID_ABI}/lib/libfftw3f.so
                android log
                )

    else ()
        find_package(PkgConfig)
        pkg_check_modules(FFTW3 REQUIRED fftw3f)
        pkg_check_modules(VOLK REQUIRED volk)
        target_link_libraries(sdrpp_ft8_mshv PUBLIC fftw3f volk pthread)
        target_include_directories(sdrpp_ft8_mshv PUBLIC
                ${FFTW3_INCLUDE_DIRS}
                ${VOLK_INCLUDE_DIRS}
                )

        target_link_directories(sdrpp_ft8_mshv PUBLIC
                ${FFTW3_LIBRARY_DIRS}
                ${VOLK_LIBRARY_DIRS}
                )

        target_link_libraries(sdrpp_ft8_mshv PUBLIC
                ${FFTW3_LIBRARIES}
                ${VOLK_LIBRARIES}
                )

    endif ()

    #target_link_options(test0 PUBLIC -fsanitize=memory)

    install(TARGETS sdrpp_ft8_mshv DESTINATION lib)

    if (ANDROID)
        # this script will copy the binary to the android project. and gradle will pack it with other libs.
        add_custom_command(TARGET sdrpp_ft8_mshv POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E time ${CMAKE_CURRENT_LIST_DIR}/android_pack.sh $<TARGET_FILE:sdrpp_ft8_mshv> ${CMAKE_CURRENT_LIST_DIR}
                )
    endif ()

    set(TARGETS sdrpp_ft8_mshv)
    foreach (TGT IN LISTS TARGETS)
        if (MSVC)
            target_compile_options(${TGT} PRIVATE /std:c++17 /EHsc)
        elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${TGT} PRIVATE -std=c++17 -O3 -Wno-unused-command-line-argument -undefined dynamic_lookup)
        else ()
            target_compile_options(${TGT} PRIVATE -std=c++17 -O3)
        endif ()
    endforeach ()

endif ()