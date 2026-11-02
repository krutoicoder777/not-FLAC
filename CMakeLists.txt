cmake_minimum_required(VERSION 3.10)
project(not_flac C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(lib/dz_processor lib/wav_stream)

add_library(dz_processor STATIC lib/dz_processor/dz_processor.c)
add_library(wav_stream STATIC lib/wav_stream/wav_stream.c)

add_executable(test_wav_stream tests/test_wav_stream.c)
target_link_libraries(test_wav_stream wav_stream)

add_executable(test_dz_stream tests/test_dz_stream.c)
target_link_libraries(test_dz_stream wav_stream dz_processor)
