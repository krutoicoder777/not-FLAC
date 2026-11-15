#ifndef RICE_CODER_H__
#define RICE_CODER_H__

#include <stdint.h>
#include <stdio.h>

typedef struct rice_coder rice_coder_t;

rice_coder_t *rice_coder_create(FILE *out, uint16_t bits_per_sample);
void rice_coder_destroy(rice_coder_t *rice);
int rice_coder_set_distribution(rice_coder_t *rice, const uint32_t *distribution, int distribution_size);
void rice_coder_set_num_warmup(rice_coder_t *rice, uint32_t num_warmup);
void rice_coder_encode_elem(rice_coder_t *rice, uint32_t res);
void rice_coder_flush(rice_coder_t *rice);

uint64_t rice_coder_estimate_delta(const int32_t *samples, uint32_t n, int bits_per_sample);
void rice_coder_encode_warmup_delta(rice_coder_t *rice, const int32_t *samples, uint32_t n);

typedef struct rice_decoder rice_decoder_t;

rice_decoder_t *rice_decoder_create(FILE *in, uint16_t bits_per_sample);
void rice_decoder_destroy(rice_decoder_t *rice);
int rice_decoder_set_distribution(rice_decoder_t *rice, int distribution_size);
void rice_decoder_set_num_warmup(rice_decoder_t *rice, uint32_t num_warmup);
uint32_t rice_decoder_decode_elem(rice_decoder_t *rice);
void rice_decoder_decode_block(rice_decoder_t *rice, uint32_t *out, int count);
int rice_decoder_decode_warmup_delta(rice_decoder_t *rice, int32_t *out, uint32_t n);

#endif
