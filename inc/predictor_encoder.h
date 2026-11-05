#ifndef PREDICTOR_ENCODER_H__
#define PREDICTOR_ENCODER_H__

#include <stdint.h>
#include "notflac_format.h"

typedef struct predictor_encoder predictor_encoder_t;

predictor_encoder_t *predictor_encoder_create_lpc(int order, int block_size);
predictor_encoder_t *predictor_encoder_create_delta(int bits_per_sample, int block_size);
void predictor_encoder_destroy(predictor_encoder_t *pe);

const uint32_t *predictor_encoder_encode_block(
    predictor_encoder_t *pe,
    const int32_t *block,
    int block_size
);

predictor_method_t predictor_encoder_method(predictor_encoder_t *pe);

const int32_t *predictor_encoder_quotients(predictor_encoder_t *pe);
int predictor_encoder_precision(predictor_encoder_t *pe);

#endif
