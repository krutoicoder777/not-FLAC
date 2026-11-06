#ifndef ENTROPY_DECODER_H__
#define ENTROPY_DECODER_H__

#include <stdio.h>
#include <stdint.h>

typedef struct entropy_decoder entropy_decoder_t;

entropy_decoder_t *entropy_d_create_range(FILE *in);
entropy_decoder_t *entropy_d_create_rice(FILE *in);
void entropy_d_destroy(entropy_decoder_t *dec);

uint32_t entropy_decode_sample(entropy_decoder_t *dec);
uint8_t entropy_decode_precision(entropy_decoder_t *dec);
uint8_t entropy_decode_byte(entropy_decoder_t *dec);
int32_t entropy_decode_quotient(entropy_decoder_t *dec);

int entropy_decoder_set_state(entropy_decoder_t *dec);

#endif
