cmake_minimum_required(VERSION 3.10.2)

project(native_audio)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(
        native_audio
        SHARED
        native_audio.cpp
)

find_library(
        log-lib
        log
)

find_library(
        aaudio-lib
        aaudio
)

target_link_libraries(
        native_audio
        ${log-lib}
        ${aaudio-lib}
)