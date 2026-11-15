#ifndef ENTROPY_DECODER_H__
#define ENTROPY_DECODER_H__

#include "notflac_format.h"
#include "rice_coder.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct entropy_decoder entropy_decoder_t;

entropy_decoder_t *entropy_d_create_rice(FILE *in, uint16_t bps);
void entropy_d_destroy(entropy_decoder_t *dec);
int entropy_decoder_set_state(entropy_decoder_t *dec, int block_size);
uint32_t entropy_decode_sample(entropy_decoder_t *dec);
void entropy_decode_block(entropy_decoder_t *dec, uint32_t *out, int count);
uint8_t entropy_decode_precision(entropy_decoder_t *dec);
int entropy_decode_quotients(entropy_decoder_t *dec, int32_t *quotients, int order);
void entropy_decoder_set_num_warmup(entropy_decoder_t *dec, uint32_t num_warmup);

#endif
