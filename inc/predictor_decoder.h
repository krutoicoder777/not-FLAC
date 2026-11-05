#ifndef PREDICTOR_DECODER_H__
#define PREDICTOR_DECODER_H__

#include <stdint.h>
#include "notflac_format.h"

typedef struct predictor_decoder predictor_decoder_t;

predictor_decoder_t *predictor_decoder_create_lpc(int order, int block_size);
predictor_decoder_t *predictor_decoder_create_delta(int bits_per_sample, int block_size);
void predictor_decoder_destroy(predictor_decoder_t *dec);

void predictor_decoder_set_quotients(predictor_decoder_t *dec, const int32_t *quotients, int precision);

const int32_t *predictor_decoder_decode_block(
    predictor_decoder_t *dec,
    const uint32_t *residuals,
    int block_size
);

predictor_method_t predictor_decoder_method(predictor_decoder_t *dec);

#endif
