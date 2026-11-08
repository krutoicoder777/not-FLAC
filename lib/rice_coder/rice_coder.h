#ifndef RICE_CODER_H__
#define RICE_CODER_H__

#include <stdint.h>
#include <stdio.h>

typedef struct rice_coder rice_coder_t;

rice_coder_t *rice_coder_create(FILE *out, uint16_t bits_per_sample); 
void rice_coder_destroy(rice_coder_t *rice);
//set block counts optimal k for the distribution and writes it to file
int rice_coder_set_distribution(rice_coder_t *rice, const uint32_t *distribution, int distribution_size);
void rice_coder_encode_elem(rice_coder_t *rice, uint32_t res);
void rice_coder_flush(rice_coder_t *rice);

typedef struct rice_decoder rice_decoder_t;

rice_decoder_t *rice_decoder_create(FILE *in, uint16_t bits_per_sample);
void rice_decoder_destroy(rice_decoder_t *rice);
//reads k value from file
int rice_decoder_set_distribution(rice_decoder_t *rice, int distribution_size);
uint32_t rice_decoder_decode_elem(rice_decoder_t *rice);

#endif
