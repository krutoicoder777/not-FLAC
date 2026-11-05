#ifndef AUDIO_DECODER_H__
#define AUDIO_DECODER_H__

#include <stdio.h>
#include <stdint.h>
#include "notflac_format.h"

typedef struct audio_decoder audio_decoder_t;

audio_decoder_t *audio_decoder_open(FILE *in);
void audio_decoder_close(audio_decoder_t *dec);

uint32_t audio_decoder_num_samples(audio_decoder_t *dec);
uint16_t audio_decoder_bits_per_sample(audio_decoder_t *dec);
uint32_t audio_decoder_sample_rate(audio_decoder_t *dec);
uint16_t audio_decoder_channels(audio_decoder_t *dec);
uint32_t audio_decoder_num_blocks(audio_decoder_t *dec);
uint32_t audio_decoder_block_size(audio_decoder_t *dec);

int audio_decoder_seek_block(audio_decoder_t *dec, uint32_t block_index);

const int32_t *audio_decoder_decode_block(audio_decoder_t *dec, int *cur_samples);

int audio_decode_next(audio_decoder_t *dec, int32_t *sample);

#endif
