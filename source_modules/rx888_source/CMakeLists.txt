cmake_minimum_required(VERSION 3.13)
project(rx888_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # PothosSDR ships libusb, the same copy the other USB sources link against
    target_include_directories(rx888_source PRIVATE "C:/Program Files/PothosSDR/include/libusb-1.0")
    target_link_directories(rx888_source PRIVATE "C:/Program Files/PothosSDR/lib")
    target_link_libraries(rx888_source PRIVATE libusb-1.0)
else (MSVC)
    find_package(PkgConfig)

    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

    target_include_directories(rx888_source PRIVATE ${LIBUSB_INCLUDE_DIRS})
    target_link_directories(rx888_source PRIVATE ${LIBUSB_LIBRARY_DIRS})
    target_link_libraries(rx888_source PRIVATE ${LIBUSB_LIBRARIES})
endif ()

# The FX3 firmware image, and the licence of the project it comes from
install(DIRECTORY firmware DESTINATION share/sdrpp)