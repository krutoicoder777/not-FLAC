#ifndef PREDICTOR_DECODER_H__
#define PREDICTOR_DECODER_H__

#include "notflac_format.h"
#include <stdint.h>

typedef struct predictor_decoder predictor_decoder_t;

predictor_decoder_t *predictor_decoder_create_lpc(int order, int block_size);
predictor_decoder_t *predictor_decoder_create_fixed(int fixed_order, int block_size);
void predictor_decoder_destroy(predictor_decoder_t *dec);

void predictor_decoder_set_quotients(predictor_decoder_t *dec, const int32_t *quotients, int precision);
const int32_t *predictor_decoder_decode_block(predictor_decoder_t *dec, const uint32_t *residuals, int block_size);

#endif
