cmake_minimum_required(VERSION 3.13)
project(dsdcc_decoder)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

add_library(dsdcc_decoder SHARED ${SRC})
target_link_libraries(dsdcc_decoder PRIVATE sdrpp_core)
set_target_properties(dsdcc_decoder PROPERTIES PREFIX "")

target_include_directories(dsdcc_decoder PRIVATE "src/")

if (MSVC)
    target_compile_options(dsdcc_decoder PRIVATE /O2 /Ob2 /std:c++17 /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(dsdcc_decoder PRIVATE -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
else ()
    target_compile_options(dsdcc_decoder PRIVATE -O3 -std=c++17)
endif ()

if (EXISTS ${CMAKE_BINARY_DIR}/mbelib/mbelib.tar.gz)
    message("Not downloading MBELIB.")
else ()
    message("Downloading MBELIB")
    file(DOWNLOAD https://codeload.github.com/szechyjs/mbelib/tar.gz/master ${CMAKE_BINARY_DIR}/mbelib/mbelib.tar.gz)
    message("Extracting MBELIB.")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xfz ${CMAKE_BINARY_DIR}/mbelib/mbelib.tar.gz WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/mbelib/)
    file(RENAME ${CMAKE_BINARY_DIR}/mbelib/mbelib-master ${CMAKE_BINARY_DIR}/mbelib/mbelib-root)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/mbelib/build)
    execute_process(COMMAND ${CMAKE_COMMAND} ../mbelib-root -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/mbelib/build)
    execute_process(COMMAND ${CMAKE_COMMAND} --build  . --target install --config Release WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/mbelib/build)

    #    file(COPY lz4.cc DESTINATION ${CMAKE_BINARY_DIR}/lz4/lz4-root/lib/)
endif ()

if (EXISTS ${CMAKE_BINARY_DIR}/dsdcc/dsdcc.tar.gz)
    message("Not downloading dsdcc.")
else ()
    message("Downloading dsdcc")
    file(DOWNLOAD https://codeload.github.com/f4exb/dsdcc/tar.gz/master ${CMAKE_BINARY_DIR}/dsdcc/dsdcc.tar.gz)
    message("Extracting dsdcc.")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xfz ${CMAKE_BINARY_DIR}/dsdcc/dsdcc.tar.gz WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/dsdcc/)
    file(RENAME ${CMAKE_BINARY_DIR}/dsdcc/dsdcc-master ${CMAKE_BINARY_DIR}/dsdcc/dsdcc-root)
    #    file(COPY lz4.cc DESTINATION ${CMAKE_BINARY_DIR}/lz4/lz4-root/lib/)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/dsdcc/build)
    execute_process(COMMAND ${CMAKE_COMMAND} ../dsdcc-root -DCMAKE_BUILD_TYPE=Release -DUSE_MBELIB=ON -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DLIBMBE_INCLUDE_DIR=${CMAKE_INSTALL_PREFIX}/include -DLIBMBE_LIBRARY="mbe -L${CMAKE_INSTALL_PREFIX}/lib" WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/dsdcc/build)
    execute_process(COMMAND ${CMAKE_COMMAND} --build  . --target install  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/dsdcc/build)
endif ()


#set(LZ4_SOURCE ${CMAKE_BINARY_DIR}/lz4/lz4-root/lib/lz4.cc)
#set(LZ4_INCLUDE ${CMAKE_BINARY_DIR}/lz4/lz4-root/lib)

target_include_directories(dsdcc_decoder PRIVATE ${CMAKE_INSTALL_PREFIX}/include)
target_link_directories(dsdcc_decoder PRIVATE ${CMAKE_INSTALL_PREFIX}/lib)
#target_link_libraries(dsdcc_decoder PRIVATE dsdcc)



#if (MSVC)
#    # Lib path
#    target_include_directories(dsdcc_decoder PRIVATE "C:/Program Files/codec2/include/")
#    target_link_directories(dsdcc_decoder PRIVATE "C:/Program Files/codec2/lib")
#
#    target_link_libraries(dsdcc_decoder PRIVATE libcodec2)
#elseif (ANDROID)
#    target_include_directories(dsdcc_decoder PUBLIC
#        /mnt/android_sdr/codec2/src
#        /mnt/android_sdr/output/codec2/${ANDROID_ABI}/
#    )
#
#    target_link_libraries(dsdcc_decoder PUBLIC
#        /mnt/android_sdr/output/codec2/${ANDROID_ABI}/libcodec2.so
#    )
#else ()
#    find_package(PkgConfig)
#
#    pkg_check_modules(LIBCODEC2 REQUIRED codec2)
#
#    target_include_directories(dsdcc_decoder PRIVATE ${LIBCODEC2_INCLUDE_DIRS})
#    target_link_directories(dsdcc_decoder PRIVATE ${LIBCODEC2_LIBRARY_DIRS})
#    target_link_libraries(dsdcc_decoder PRIVATE ${LIBCODEC2_LIBRARIES})
#
#    # Include it because for some reason pkgconfig doesn't look here?
#    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
#        target_include_directories(dsdcc_decoder PRIVATE "/usr/local/include")
#    endif()
#
#endif ()
#


# Install directives
install(TARGETS dsdcc_decoder DESTINATION lib/sdrpp_brown/plugins)