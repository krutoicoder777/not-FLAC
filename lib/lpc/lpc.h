#ifndef LPC_H__
#define LPC_H__

#include <stdint.h>

typedef struct lpc_encoder lpc_encoder_t;
typedef struct lpc_decoder lpc_decoder_t;

lpc_encoder_t *lpc_encoder_create(int order);
void lpc_encoder_destroy(lpc_encoder_t *enc);
void lpc_encoder_encode_block(lpc_encoder_t *enc, const int32_t *block, int count);
int32_t lpc_encoder_pop(lpc_encoder_t *enc);
const int32_t *lpc_encoder_quotients(lpc_encoder_t *enc);
int lpc_encoder_precision(lpc_encoder_t *enc);

lpc_decoder_t *lpc_decoder_create(int order);
void lpc_decoder_destroy(lpc_decoder_t *dec);
void lpc_decoder_set_quotients(lpc_decoder_t *dec, const int32_t *quotients, int precision);
int32_t lpc_decoder_decode_sample(lpc_decoder_t *dec, int32_t residual);

#endif
