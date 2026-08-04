cmake_minimum_required(VERSION 3.13)
project(usrp_source)

file(GLOB SRC "src/*.cpp")

# 1. Initialize the module first
include(${SDRPP_MODULE_CMAKE})

# 2. Windows Environment
if (MSVC)
    # Source-built UHD (see .github/workflows/build_all.yml). Override with -DUHD_DIR=...
    if (NOT DEFINED UHD_DIR)
        if (EXISTS "C:/Program Files/uhd/lib/uhd.lib")
            set(UHD_DIR "C:/Program Files/uhd")
        else()
            set(UHD_DIR "C:/Program Files/PothosSDR")
        endif()
    endif ()
    target_link_directories(usrp_source PRIVATE "${UHD_DIR}/lib")
    target_include_directories(usrp_source PRIVATE "${UHD_DIR}/include")
    target_link_libraries(usrp_source PRIVATE uhd)

    # UHD public headers include boost headers; vcpkg toolchain provides them,
    # but link boost explicitly to satisfy MSVC auto-linking on boost_thread.
    find_package(Boost REQUIRED)
    target_include_directories(usrp_source PRIVATE ${Boost_INCLUDE_DIRS})

# 3. Unix Environment (macOS, Linux)
else ()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUHD REQUIRED uhd)

    # Clean up the libraries list to strip out broken plain boost strings exported by the .pc file
    set(CLEANED_UHD_LIBS "")
    foreach(lib ${LIBUHD_LIBRARIES})
        if (NOT lib MATCHES "^boost_")
            list(APPEND CLEANED_UHD_LIBS ${lib})
        endif()
    endforeach()

    target_include_directories(usrp_source PRIVATE ${LIBUHD_INCLUDE_DIRS})
    target_link_directories(usrp_source PRIVATE ${LIBUHD_LIBRARY_DIRS})
    
    # Link with the cleaned list
    target_link_libraries(usrp_source PRIVATE ${CLEANED_UHD_LIBS})

    # Find the proper Boost components natively matching the system layout
    find_package(Boost REQUIRED COMPONENTS system thread)
    if (Boost_FOUND)
        target_include_directories(usrp_source PRIVATE ${Boost_INCLUDE_DIRS})
        target_link_libraries(usrp_source PRIVATE Boost::system Boost::thread)
    endif ()
endif ()
