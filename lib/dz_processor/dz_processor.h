#ifndef DZ_PROCESSOR_H__
#define DZ_PROCESSOR_H__

#include <stdint.h>

typedef struct dz_encoder dz_encoder_t;
typedef struct dz_decoder dz_decoder_t;

dz_encoder_t *dze_init(uint16_t bits_per_sample);
void dze_destroy(dz_encoder_t *enc);
uint32_t dze_next(dz_encoder_t *enc, int32_t sample);

dz_decoder_t *dzd_init(uint16_t bits_per_sample);
void dzd_destroy(dz_decoder_t *dec);
int32_t dzd_next(dz_decoder_t *dec, uint32_t dz_code);

#endif
