cmake_minimum_required(VERSION 3.22)

project(freecam_levipack LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# CMake 4 refuses third-party projects that still declare an old minimum
# (nlohmann/json, fmt, ...). Allow them.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

# -----------------------------------------------------------------------------
# preloader-android SDK (headers always, library only for Android builds)
#
# The default ref is the commit LeviLaunchroid's main branch pins as its
# app/src/main/cpp/preloader submodule. Mod Menu structs (ModuleInfo, ...) are
# passed across the library boundary, so build against the same SDK revision as
# the launcher you run. Override with -DFREECAM_PRELOADER_REF=<tag-or-commit>,
# or -DPRELOADER_ANDROID_ROOT=<checkout> to use a local copy.
# -----------------------------------------------------------------------------
set(FREECAM_PRELOADER_REPO
    "https://github.com/LiteLDev/preloader-android.git"
    CACHE STRING "preloader-android git repository")
set(FREECAM_PRELOADER_REF
    "5d108da350ce3e7f13f51415f81e65344ecd5d14"
    CACHE STRING "preloader-android tag or commit to build against")
set(PRELOADER_ANDROID_ROOT
    ""
    CACHE PATH "Existing preloader-android checkout (skips the download)")

include(FetchContent)

if(NOT PRELOADER_ANDROID_ROOT)
    # SOURCE_SUBDIR points at a directory that does not exist, so the SDK is only
    # downloaded; its CMakeLists.txt (Android-only) is added explicitly below.
    FetchContent_Declare(
        preloader_android
        GIT_REPOSITORY "${FREECAM_PRELOADER_REPO}"
        GIT_TAG "${FREECAM_PRELOADER_REF}"
        SOURCE_SUBDIR freecam-do-not-add-subdirectory)
    FetchContent_MakeAvailable(preloader_android)
    set(PRELOADER_ANDROID_ROOT "${preloader_android_SOURCE_DIR}")
endif()

if(NOT EXISTS "${PRELOADER_ANDROID_ROOT}/include/pl/Config.hpp")
    message(FATAL_ERROR
        "PRELOADER_ANDROID_ROOT is not a preloader-android checkout. "
        "Missing: ${PRELOADER_ANDROID_ROOT}/include/pl/Config.hpp")
endif()

# Same dependency versions as the preloader SDK, so both agree on one copy.
set(FREECAM_DEPS)
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE)
    list(APPEND FREECAM_DEPS nlohmann_json)
endif()
if(NOT TARGET Boost::pfr)
    FetchContent_Declare(
        boost_pfr
        GIT_REPOSITORY https://github.com/boostorg/pfr.git
        GIT_TAG boost-1.88.0
        GIT_SHALLOW TRUE)
    list(APPEND FREECAM_DEPS boost_pfr)
endif()
if(NOT TARGET magic_enum::magic_enum)
    FetchContent_Declare(
        magic_enum
        GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
        GIT_TAG v0.9.7
        GIT_SHALLOW TRUE)
    list(APPEND FREECAM_DEPS magic_enum)
endif()
if(NOT TARGET fmt::fmt)
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.2.0
        GIT_SHALLOW TRUE)
    set(FMT_DOC OFF CACHE BOOL "Disable fmt docs" FORCE)
    set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
    set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install rules" FORCE)
    list(APPEND FREECAM_DEPS fmt)
endif()
if(FREECAM_DEPS)
    FetchContent_MakeAvailable(${FREECAM_DEPS})
endif()

# -----------------------------------------------------------------------------
# Platform-independent logic (camera maths, controller, signature parser).
# Built for the host to run the unit tests, and linked into the Android library.
# -----------------------------------------------------------------------------
add_library(freecam_logic STATIC
    src/FreecamCore.cpp
    src/Controller.cpp
    src/SignatureRules.cpp)
target_include_directories(freecam_logic PUBLIC "${CMAKE_CURRENT_LIST_DIR}/src")
target_link_libraries(freecam_logic PUBLIC nlohmann_json::nlohmann_json)
target_compile_options(freecam_logic PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wall -Wextra -Wpedantic>)
if(ANDROID)
    # Keep the static library's symbols out of libfreecam.so's export table.
    target_compile_options(freecam_logic PRIVATE
        -fvisibility=hidden -fvisibility-inlines-hidden)
endif()

function(freecam_link_sdk_headers target_name)
    target_include_directories("${target_name}" PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${PRELOADER_ANDROID_ROOT}/include")
    target_link_libraries("${target_name}" PRIVATE
        nlohmann_json::nlohmann_json
        Boost::pfr
        magic_enum::magic_enum
        fmt::fmt)
    target_compile_options("${target_name}" PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wall -Wextra -Wpedantic>)
endfunction()

if(NOT ANDROID)
    # ---- host: unit tests + default config generator -------------------------
    enable_testing()

    add_executable(freecam_core_tests tests/CoreTests.cpp)
    target_link_libraries(freecam_core_tests PRIVATE freecam_logic)
    add_test(NAME freecam_core_tests COMMAND freecam_core_tests)

    add_executable(freecam_config_gen src/GenerateConfig.cpp)
    freecam_link_sdk_headers(freecam_config_gen)
    target_compile_definitions(freecam_config_gen PRIVATE PL_CONFIG_NO_RUNTIME)

    # Mod lifecycle + Mod Menu wiring, driven against a fake preloader runtime.
    # FreecamMod.cpp is #included by the test, GameBackend.cpp is linked as-is.
    add_executable(freecam_lifecycle_tests
        tests/LifecycleTests.cpp
        tests/FakePreloader.cpp
        src/GameBackend.cpp)
    freecam_link_sdk_headers(freecam_lifecycle_tests)
    target_include_directories(freecam_lifecycle_tests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/tests"
        "${CMAKE_CURRENT_LIST_DIR}/tests/host_stubs") # stand-ins for jni.h, android/log.h
    target_link_libraries(freecam_lifecycle_tests PRIVATE freecam_logic)
    target_compile_definitions(freecam_lifecycle_tests PRIVATE
        FREECAM_SHIPPED_SIGNATURES="${CMAKE_CURRENT_LIST_DIR}/data/signatures.json")
    add_test(NAME freecam_lifecycle_tests COMMAND freecam_lifecycle_tests)
endif()

if(ANDROID)
    # ---- Android: the native mod itself --------------------------------------
    option(FREECAM_LINK_PRELOADER
           "Build the preloader SDK from source and link the mod against it"
           ON)

    if(FREECAM_LINK_PRELOADER AND NOT TARGET preloader)
        add_subdirectory("${PRELOADER_ANDROID_ROOT}"
                         "${CMAKE_BINARY_DIR}/_preloader_android")
    endif()

    if(ANDROID_ABI)
        set(FREECAM_OUTPUT_ABI "${ANDROID_ABI}")
    else()
        set(FREECAM_OUTPUT_ABI "android")
    endif()
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/out/${FREECAM_OUTPUT_ABI}")

    find_library(FREECAM_LOG_LIB log)

    add_library(freecam SHARED
        src/FreecamMod.cpp
        src/GameBackend.cpp)
    set_target_properties(freecam PROPERTIES OUTPUT_NAME "freecam")
    freecam_link_sdk_headers(freecam)
    target_link_libraries(freecam PRIVATE freecam_logic)
    target_compile_options(freecam PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-fvisibility=hidden -fvisibility-inlines-hidden>)

    if(FREECAM_LOG_LIB)
        target_link_libraries(freecam PRIVATE "${FREECAM_LOG_LIB}")
    endif()

    if(TARGET preloader)
        target_link_libraries(freecam PRIVATE preloader)
    else()
        target_link_options(freecam PRIVATE
            -Wl,--unresolved-symbols=ignore-all)
    endif()
endif()
