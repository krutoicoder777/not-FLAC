cmake_minimum_required(VERSION 3.10)
project(not_flac C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(
    inc
    lib/dz_processor
    lib/lpc
    lib/range_coder
    lib/rice_coder
    lib/wav_block
    lib/wav_stream
    lib/wav_writer
)

add_library(dz_processor STATIC lib/dz_processor/dz_processor.c)
add_library(wav_stream STATIC lib/wav_stream/wav_stream.c)
add_library(wav_block STATIC lib/wav_block/wav_block.c)
add_library(wav_writer STATIC lib/wav_writer/wav_writer.c)
add_library(lpc STATIC lib/lpc/lpc.c)
add_library(rice_coder STATIC lib/rice_coder/rice_coder.c)
add_library(entropy_coder STATIC
    lib/range_coder/range_coder.c
    lib/range_coder/range_decoder.c
    lib/range_coder/models.c
    src/entropy_encoder.c
    src/entropy_decoder.c
)
add_library(predictor STATIC
    src/predictor_encoder.c
    src/predictor_decoder.c
)

target_link_libraries(wav_block wav_stream)
target_link_libraries(entropy_coder rice_coder m)
target_link_libraries(lpc m)
target_link_libraries(predictor lpc dz_processor)

set(CORE_SRCS
    src/audio_encoder.c
    src/audio_decoder.c
)

add_executable(notflac src/main.c ${CORE_SRCS})
target_link_libraries(notflac wav_block wav_writer predictor entropy_coder m)

add_executable(notflac_player src/player.c ${CORE_SRCS})
target_link_libraries(notflac_player wav_block predictor entropy_coder m)

add_executable(test_wav_stream tests/test_wav_stream.c)
target_link_libraries(test_wav_stream wav_stream)

add_executable(test_wav_block tests/test_wav_block.c)
target_link_libraries(test_wav_block wav_block)

add_executable(test_dz_stream tests/test_dz_stream.c)
target_link_libraries(test_dz_stream wav_stream dz_processor)

add_executable(test_lpc tests/test_lpc.c)
target_link_libraries(test_lpc lpc m)

add_executable(test_notflac_roundtrip tests/test_notflac_roundtrip.c ${CORE_SRCS})
target_link_libraries(test_notflac_roundtrip wav_block predictor entropy_coder m)

add_executable(test_seek tests/test_seek.c ${CORE_SRCS})
target_link_libraries(test_seek wav_block predictor entropy_coder m)
