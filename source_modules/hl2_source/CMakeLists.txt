cmake_minimum_required(VERSION 3.13)
project(hl2_source)

file(GLOB SRC "src/*.cpp" "src/*.h" "src/*.c")

add_library(hl2_source SHARED ${SRC})
target_link_libraries(hl2_source PRIVATE sdrpp_core)
set_target_properties(hl2_source PROPERTIES PREFIX "")

target_include_directories(hl2_source PRIVATE "src/")

if (MSVC)
    target_compile_options(hl2_source PRIVATE ${OPTIMIZE_OPTIONS} /std:c++17 /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(hl2_source PRIVATE ${OPTIMIZE_OPTIONS} -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
else ()
    target_compile_options(hl2_source PRIVATE ${OPTIMIZE_OPTIONS} -std=c++17)
endif ()

if (MSVC)
    # Lib path
#    target_link_directories(hl2_source PRIVATE "C:/Program Files/PothosSDR/bin/")
    target_link_libraries(hl2_source PUBLIC iphlpapi.lib )

#    target_include_directories(hl2_source PUBLIC "C:/Program Files/PothosSDR/include/libairspyhf/")

#    target_link_libraries(hl2_source PRIVATE airspyhf)
else (MSVC)
    find_package(PkgConfig)

#    pkg_check_modules(LIBAIRSPYHF REQUIRED libairspyhf)

#    target_include_directories(hl2_source PRIVATE )
#    target_link_directories(hl2_source PRIVATE )

    # Include it because for some reason pkgconfig doesn't look here?
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_include_directories(hl2_source PRIVATE "/usr/local/include")
    endif()
    
endif ()

# Install directives
install(TARGETS hl2_source DESTINATION lib/sdrpp/plugins)