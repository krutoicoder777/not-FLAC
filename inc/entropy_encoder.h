#ifndef ENTROPY_ENCODER_H__
#define ENTROPY_ENCODER_H__

#include "notflac_format.h"
#include "rice_coder.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct entropy_encoder entropy_encoder_t;

entropy_encoder_t *entropy_create_rice(FILE *out, uint16_t bps);
void entropy_destroy(entropy_encoder_t *enc);
void entropy_encode_sample(entropy_encoder_t *enc, uint32_t sample);
void entropy_encode_precision(entropy_encoder_t *enc, uint8_t precision);
void entropy_encode_quotients(entropy_encoder_t *enc, const int32_t *quotients, int order);
int entropy_set_state(entropy_encoder_t *enc, const uint32_t *block, int block_size);
void entropy_set_num_warmup(entropy_encoder_t *enc, uint32_t num_warmup);

#endif
