cmake_minimum_required(VERSION 3.13)
project(dragonlabs_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    target_include_directories(dragonlabs_source PRIVATE "C:/Program Files/DragonLabs/CR/include")
    target_link_directories(dragonlabs_source PRIVATE "C:/Program Files/DragonLabs/CR/lib")
    target_link_libraries(dragonlabs_source PRIVATE dlcr)
elseif (ANDROID)
    # Not supported yet...
else (MSVC)
    find_package(PkgConfig)

    pkg_check_modules(LIBDLCR REQUIRED libdlcr)

    target_include_directories(dragonlabs_source PRIVATE ${LIBDLCR_INCLUDE_DIRS})
    target_link_directories(dragonlabs_source PRIVATE ${LIBDLCR_LIBRARY_DIRS})
    target_link_libraries(dragonlabs_source PRIVATE ${LIBDLCR_LIBRARIES})
endif ()