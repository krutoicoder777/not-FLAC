#ifndef AUDIO_DECODER_H__
#define AUDIO_DECODER_H__

#include <stdio.h>
#include <stdint.h>
#include "notflac_format.h"

typedef struct audio_decoder audio_decoder_t;

audio_decoder_t *audio_decoder_open(FILE *in);
int audio_decode_next(audio_decoder_t *dec, int32_t *sample);
void audio_decoder_close(audio_decoder_t *dec);

uint32_t audio_decoder_sample_rate(audio_decoder_t *dec);
uint16_t audio_decoder_channels(audio_decoder_t *dec);
uint16_t audio_decoder_bits_per_sample(audio_decoder_t *dec);
uint32_t audio_decoder_num_samples(audio_decoder_t *dec);

#endif
