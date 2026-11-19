# Pinned third-party references for dabplus_decoder.
#
# No third-party source is copied into this repository. Every dependency is
# fetched from its upstream at the commit recorded in THIRD_PARTY.md, the
# checked-out commit is verified against that pin, and the DAB-Radio engine
# patch is hash-checked before it is applied. Nothing is fetched when the
# source and the patch are already in the build tree, so an offline rebuild of
# a configured build directory works.

include(FetchContent)

set(DABPLUS_DAB_RADIO_URL    "https://github.com/williamyang98/DAB-Radio.git")
set(DABPLUS_DAB_RADIO_COMMIT "0a09ba34a40286cf77e0c48aa47cac64b86341ef")
set(DABPLUS_VITERBI_URL      "https://github.com/williamyang98/ViterbiDecoderCpp.git")
set(DABPLUS_VITERBI_COMMIT   "5749af98be5654771154d58aa7c1283bba792fb4")
set(DABPLUS_FMT_URL          "https://github.com/fmtlib/fmt.git")
set(DABPLUS_FMT_COMMIT       "0147e08225db261f5689fc17a986ede7f1db56f0")
set(DABPLUS_FAAD2_URL        "https://github.com/knik0/faad2.git")
set(DABPLUS_FAAD2_COMMIT     "693690e37f417d394a5e65296c75c2d971ffec58")
set(DABPLUS_MPG123_URL       "https://github.com/madebr/mpg123.git")
set(DABPLUS_MPG123_COMMIT    "31d1d478177ce923260cb6c69f037b9932c4dae5")

# Engine patches, applied in this order after the pinned checkout. Each one is
# verified against its recorded hash before it is applied.
set(DABPLUS_ENGINE_PATCHES
    "dab-radio-0a09ba34-vdk-engine.patch|af3c7050ebf59899eb25d8aedadfba411de36c54687aa229701a207eea561a1c|CoherentDemapper"
    "dab-radio-0a09ba34-silence-guard.patch|97dc90892edc7a39674c394e5516f8d9f8c14a467e966063c22a04a1175eef4e|no peak to interpolate"
    "dab-radio-0a09ba34-core-fft.patch|9c8418b9a95b5b720e6912f89e9cc9203e12c17df06a504445ae90d3e52eee43|dabplus::FFT"
)

# True when src_dir is itself a git checkout at commit. A directory that is not
# a repository of its own must not be read with git rev-parse: git walks up the
# tree and would happily report the enclosing SDR++ checkout's commit instead.
function(dabplus_checkout_matches commit src_dir out_var)
    set(matches FALSE)
    if(IS_DIRECTORY "${src_dir}/.git")
        execute_process(
            COMMAND git rev-parse HEAD
            WORKING_DIRECTORY "${src_dir}"
            OUTPUT_VARIABLE _head
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _rc
        )
        if(_rc EQUAL 0 AND _head STREQUAL commit)
            set(matches TRUE)
        endif()
    endif()
    set(${out_var} ${matches} PARENT_SCOPE)
endfunction()

function(dabplus_fetch_pinned name url commit)
    FetchContent_Declare(${name}
        GIT_REPOSITORY "${url}"
        GIT_TAG "${commit}"
        GIT_SHALLOW FALSE
        UPDATE_DISCONNECTED TRUE
        GIT_PROGRESS FALSE
    )
    FetchContent_GetProperties(${name})
    if(NOT ${name}_POPULATED)
        # Pre-clean before the single populate attempt: a download stamp from an
        # earlier configure survives the removal of the -src directory, so
        # FetchContent then "succeeds" without cloning and leaves an empty tree.
        if(EXISTS "${CMAKE_BINARY_DIR}/_deps/${name}-subbuild")
            dabplus_checkout_matches("${commit}" "${CMAKE_BINARY_DIR}/_deps/${name}-src" pre_ok)
            if(NOT pre_ok)
                message(STATUS "[dabplus_decoder] ${name}: discarding stale download, fetching again")
                file(REMOVE_RECURSE
                    "${CMAKE_BINARY_DIR}/_deps/${name}-src"
                    "${CMAKE_BINARY_DIR}/_deps/${name}-build"
                    "${CMAKE_BINARY_DIR}/_deps/${name}-subbuild"
                )
            endif()
        endif()
        message(STATUS "[dabplus_decoder] fetching ${name} @ ${commit}")
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(${name})
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
    endif()

    dabplus_checkout_matches("${commit}" "${${name}_SOURCE_DIR}" checkout_ok)
    if(NOT checkout_ok)
        message(FATAL_ERROR
            "[dabplus_decoder] ${name}: expected commit ${commit} in "
            "${${name}_SOURCE_DIR}; delete the build directory and reconfigure.")
    endif()

    # FetchContent_Populate writes <name>_SOURCE_DIR into this function's scope;
    # export it so the caller can add the sources and includes.
    set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
    set(${name}_BINARY_DIR "${${name}_BINARY_DIR}" PARENT_SCOPE)
endfunction()

function(dabplus_sources_contain src_dir needle out_var)
    file(GLOB_RECURSE _sources
        "${src_dir}/src/*.h"
        "${src_dir}/src/*.hpp"
        "${src_dir}/src/*.cpp"
    )
    set(_found FALSE)
    foreach(_source IN LISTS _sources)
        file(READ "${_source}" _text)
        string(FIND "${_text}" "${needle}" _pos)
        if(NOT _pos EQUAL -1)
            set(_found TRUE)
            break()
        endif()
    endforeach()
    set(${out_var} ${_found} PARENT_SCOPE)
endfunction()

function(dabplus_apply_pinned_patch src_dir patch_name patch_sha256 marker)
    set(patch_path "${CMAKE_CURRENT_LIST_DIR}/../patches/${patch_name}")
    file(SHA256 "${patch_path}" patch_hash)
    if(NOT patch_hash STREQUAL patch_sha256)
        message(FATAL_ERROR
            "[dabplus_decoder] ${patch_name} hashes to ${patch_hash}, not the recorded "
            "${patch_sha256}; refusing to build an unverified engine.")
    endif()
    # A re-run of cmake finds the patch already applied, which is confirmed by
    # the marker it introduces rather than by ignoring the failure. The search
    # is done in CMake, not with grep: grep does not exist on Windows.
    execute_process(
        COMMAND git apply --check "${patch_path}"
        WORKING_DIRECTORY "${src_dir}"
        RESULT_VARIABLE _check
        OUTPUT_QUIET ERROR_QUIET
    )
    if(_check EQUAL 0)
        execute_process(
            COMMAND git apply "${patch_path}"
            WORKING_DIRECTORY "${src_dir}"
            RESULT_VARIABLE _apply
        )
        if(NOT _apply EQUAL 0)
            message(FATAL_ERROR "[dabplus_decoder] failed to apply ${patch_name}")
        endif()
        message(STATUS "[dabplus_decoder] applied ${patch_name} (sha256 verified)")
        return()
    endif()
    dabplus_sources_contain("${src_dir}" "${marker}" _marker_present)
    if(NOT _marker_present)
        message(FATAL_ERROR
            "[dabplus_decoder] ${patch_name} neither applies nor is present in ${src_dir}; "
            "delete that directory and reconfigure.")
    endif()
endfunction()

dabplus_fetch_pinned(dab_radio      ${DABPLUS_DAB_RADIO_URL} ${DABPLUS_DAB_RADIO_COMMIT})
foreach(entry IN LISTS DABPLUS_ENGINE_PATCHES)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 patch_name)
    list(GET parts 1 patch_sha256)
    list(GET parts 2 marker)
    dabplus_apply_pinned_patch("${dab_radio_SOURCE_DIR}" "${patch_name}" "${patch_sha256}" "${marker}")
endforeach()
dabplus_fetch_pinned(viterbi_stuff  ${DABPLUS_VITERBI_URL}   ${DABPLUS_VITERBI_COMMIT})
dabplus_fetch_pinned(fmt_stuff      ${DABPLUS_FMT_URL}       ${DABPLUS_FMT_COMMIT})
dabplus_fetch_pinned(faad2_stuff    ${DABPLUS_FAAD2_URL}     ${DABPLUS_FAAD2_COMMIT})
dabplus_fetch_pinned(mpg123_stuff   ${DABPLUS_MPG123_URL}    ${DABPLUS_MPG123_COMMIT})
