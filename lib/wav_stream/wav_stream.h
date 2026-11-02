#ifndef WAV_STREAM_H__
#define WAV_STREAM_H__

#include <stdint.h>
#include <stdio.h>

#define WAV_OK 0
#define WAV_ERR_IO 1

typedef struct wav_stream wav_stream_t;

wav_stream_t *wav_stream_open(const char *filename);
void wav_stream_close(wav_stream_t *ws);

uint32_t wav_stream_sample_rate(wav_stream_t *ws);
uint16_t wav_stream_channels(wav_stream_t *ws);
uint16_t wav_stream_bits_per_sample(wav_stream_t *ws);
uint32_t wav_stream_num_samples(wav_stream_t *ws);

int wav_stream_next_sample(wav_stream_t *ws, int32_t *sample);

#endif
