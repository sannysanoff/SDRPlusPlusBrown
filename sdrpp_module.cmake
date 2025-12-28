# Get needed values depending on if this is in-tree or out-of-tree
if (NOT SDRPP_CORE_ROOT)
    set(SDRPP_CORE_ROOT "@SDRPP_CORE_ROOT@")
endif ()
if (NOT SDRPP_MODULE_COMPILER_FLAGS)
    set(SDRPP_MODULE_COMPILER_FLAGS @SDRPP_MODULE_COMPILER_FLAGS@)
endif ()

# Created shared lib and link to core
add_library(${PROJECT_NAME} SHARED ${SRC})
target_link_libraries(${PROJECT_NAME} PRIVATE sdrpp_core)
target_include_directories(${PROJECT_NAME} PRIVATE "${SDRPP_CORE_ROOT}/src/")
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
if(MSVC)
    add_compile_options(/wd4996)
else()
    add_compile_options(-Wno-deprecated-declarations)
endif()
# Set compile arguments; avoid applying C++-only flags to C sources.
set(_sdrpp_module_c_flags ${SDRPP_MODULE_COMPILER_FLAGS})
list(REMOVE_ITEM _sdrpp_module_c_flags -std=c++17 /std:c++17 /EHsc)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${SDRPP_MODULE_COMPILER_FLAGS}>
    $<$<COMPILE_LANGUAGE:C>:${_sdrpp_module_c_flags}>
)

# Install directives
install(TARGETS ${PROJECT_NAME} DESTINATION lib/sdrpp/plugins)
