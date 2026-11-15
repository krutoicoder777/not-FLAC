#ifndef PREDICTOR_ENCODER_H__
#define PREDICTOR_ENCODER_H__

#include "notflac_format.h"
#include <stdint.h>

typedef struct predictor_encoder predictor_encoder_t;

predictor_encoder_t *predictor_encoder_create_lpc(int order, int block_size);
predictor_encoder_t *predictor_encoder_create_fixed(int fixed_order, int block_size);
void predictor_encoder_destroy(predictor_encoder_t *pe);

const uint32_t *predictor_encoder_encode_block(predictor_encoder_t *pe, const int32_t *block, int block_size);
const uint32_t *predictor_encoder_encode_block_fixed(predictor_encoder_t *pe, const int32_t *block, int block_size);

const int32_t *predictor_encoder_quotients(predictor_encoder_t *pe);
int predictor_encoder_precision(predictor_encoder_t *pe);
int predictor_encoder_order(predictor_encoder_t *pe);

#endif
