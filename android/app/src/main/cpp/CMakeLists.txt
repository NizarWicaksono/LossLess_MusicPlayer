cmake_minimum_required(VERSION 3.22.1)

project(native_audio)

# ============================================================
# C++ Standard
# ============================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# ============================================================
# Build libFLAC
# ============================================================

# Kita hanya membutuhkan library C libFLAC.
# Komponen lain tidak diperlukan untuk Milestone 4A.

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(BUILD_CXXLIBS OFF CACHE BOOL "" FORCE)

set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

set(BUILD_DOCS OFF CACHE BOOL "" FORCE)

set(WITH_OGG OFF CACHE BOOL "" FORCE)

set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)

set(INSTALL_PKGCONFIG_MODULES OFF CACHE BOOL "" FORCE)

set(INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)


# ============================================================
# Tambahkan source FLAC
# ============================================================

add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/flac
        ${CMAKE_CURRENT_BINARY_DIR}/flac
        EXCLUDE_FROM_ALL
)


# ============================================================
# Native Audio Library
# ============================================================

add_library(
        native_audio
        SHARED
        native_audio.cpp
)


# ============================================================
# Android Log
# ============================================================

find_library(
        log-lib
        log
)


# ============================================================
# Android AAudio
# ============================================================

find_library(
        aaudio-lib
        aaudio
)


# ============================================================
# Include Directory
# ============================================================

target_include_directories(
        native_audio
        PRIVATE

        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/flac/include
)


# ============================================================
# Link Libraries
# ============================================================

target_link_libraries(
        native_audio

        FLAC

        ${log-lib}

        ${aaudio-lib}
)