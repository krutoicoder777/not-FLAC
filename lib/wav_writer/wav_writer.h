#ifndef WAV_WRITER_H__
#define WAV_WRITER_H__

#include <stdio.h>
#include <stdint.h>

typedef struct wav_writer wav_writer_t;

wav_writer_t *wav_writer_create(const char *filename, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, uint32_t num_samples);
void wav_writer_destroy(wav_writer_t *w);

int wav_writer_write_sample(wav_writer_t *w, int32_t sample);
int wav_writer_write_samples(wav_writer_t *w, const int32_t *samples, uint32_t count);

#endif
