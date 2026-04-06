cmake_minimum_required(VERSION 3.13)
project(mpeg_adts_sink)

file(GLOB SRC "src/*.cpp")

set(DEPNAME libmp3lame-CMAKE)
set(DEPROOT ${CMAKE_BINARY_DIR}/${DEPNAME}/${DEPNAME}-root)
set(DEPTAR ${CMAKE_BINARY_DIR}/${DEPNAME}/${DEPNAME}.tar.gz)
set(DEPBUILD ${CMAKE_BINARY_DIR}/${DEPNAME}/build)

if (NOT EXISTS ${DEPTAR})
    message("Downloading ${DEPNAME}")
    file(DOWNLOAD https://codeload.github.com/Iunusov/libmp3lame-CMAKE/tar.gz/master ${DEPTAR})
else ()
    message("Not downloading ${DEPNAME}.")
endif ()

if (NOT EXISTS ${DEPROOT})
    message("Extracting ${DEPNAME}.")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xfz ${DEPTAR} WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${DEPNAME}/)
    file(RENAME ${CMAKE_BINARY_DIR}/${DEPNAME}/${DEPNAME}-master ${DEPROOT})
endif ()

if (EXISTS ${DEPROOT})
    FILE(READ "${DEPROOT}/CMakeLists.txt" TMPF)
    string(REPLACE "VERSION 3.0" "VERSION 3.13" TMPF "${TMPF}")
    FILE(WRITE "${DEPROOT}/CMakeLists.txt" "${TMPF}")
    FILE(READ "${DEPROOT}/Dll/CMakeLists.txt" TMPF)
    string(REPLACE "VERSION 3.0" "VERSION 3.13" TMPF "${TMPF}")
    FILE(WRITE "${DEPROOT}/Dll/CMakeLists.txt" "${TMPF}")
endif ()

if (
    NOT EXISTS ${DEPBUILD}/Debug/mp3lame.lib OR
    NOT EXISTS ${DEPBUILD}/Release/mp3lame.lib OR
    NOT EXISTS ${DEPBUILD}/RelWithDebInfo/mp3lame.lib
)
    file(MAKE_DIRECTORY ${DEPBUILD})
    execute_process(
        COMMAND ${CMAKE_COMMAND} ../${DEPNAME}-root -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
        WORKING_DIRECTORY ${DEPBUILD}
    )
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config Debug WORKING_DIRECTORY ${DEPBUILD})
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config Release WORKING_DIRECTORY ${DEPBUILD})
    execute_process(COMMAND ${CMAKE_COMMAND} --build . --config RelWithDebInfo WORKING_DIRECTORY ${DEPBUILD})
endif ()

include(${SDRPP_MODULE_CMAKE})

target_link_libraries(${PROJECT_NAME} PUBLIC
    $<$<CONFIG:Debug>:${DEPBUILD}/Debug/mp3lame.lib>
    $<$<CONFIG:Release>:${DEPBUILD}/Release/mp3lame.lib>
    $<$<CONFIG:RelWithDebInfo>:${DEPBUILD}/RelWithDebInfo/mp3lame.lib>
    $<$<CONFIG:MinSizeRel>:${DEPBUILD}/MinSizeRel/mp3lame.lib>
)
target_include_directories(${PROJECT_NAME} PUBLIC ${DEPROOT}/include)
