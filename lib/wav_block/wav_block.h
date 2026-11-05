#ifndef WAV_BLOCK_H__
#define WAV_BLOCK_H__

#include <stdint.h>
#include "wav_stream.h"

typedef struct wav_block_reader wav_block_reader_t;

wav_block_reader_t *wav_block_reader_create(const char *filename, int block_size);
void wav_block_reader_destroy(wav_block_reader_t *wb);

const int32_t *wav_block_reader_next(wav_block_reader_t *wb);

int wav_block_reader_count(wav_block_reader_t *wb);

uint32_t wav_block_reader_sample_rate(wav_block_reader_t *wb);
uint16_t wav_block_reader_channels(wav_block_reader_t *wb);
uint16_t wav_block_reader_bits_per_sample(wav_block_reader_t *wb);
uint32_t wav_block_reader_num_blocks(wav_block_reader_t *wb);
uint32_t wav_block_reader_num_samples(wav_block_reader_t *wb);

#endif
