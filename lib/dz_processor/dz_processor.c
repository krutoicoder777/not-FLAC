#include "dz_processor.h"
#include <stdint.h>
#include <stdlib.h>

typedef struct dz_encoder dz_encoder_t;
struct dz_encoder
{
    uint16_t bits_per_sample;
    int32_t last_sample;
};

typedef struct dz_decoder dz_decoder_t;
struct dz_decoder
{
    uint16_t bits_per_sample;
    int32_t last_sample;
};

dz_encoder_t *dze_init(uint16_t bits_per_sample)
{
    if (bits_per_sample != 16 && bits_per_sample != 24 && bits_per_sample != 32)
    {
        return NULL;
    }
    dz_encoder_t *enc = malloc(sizeof(dz_encoder_t));
    if (enc)
    {
        enc->bits_per_sample = bits_per_sample;
        enc->last_sample = 0;
    }
    return enc;
}

void dze_destroy(dz_encoder_t *enc)
{
    free(enc);
}

uint32_t dze_next(dz_encoder_t *enc, int32_t sample)
{
    int32_t delta = sample - enc->last_sample;
    uint32_t zigzag = ((uint32_t)delta << 1) ^ (uint32_t)(delta >> 31);
    enc->last_sample = sample;
    return zigzag;
}

dz_decoder_t *dzd_init(uint16_t bits_per_sample)
{
    if (bits_per_sample != 16 && bits_per_sample != 24 && bits_per_sample != 32)
    {
        return NULL;
    }
    dz_decoder_t *dec = malloc(sizeof(dz_decoder_t));
    if (dec)
    {
        dec->bits_per_sample = bits_per_sample;
        dec->last_sample = 0;
    }
    return dec;
}

void dzd_destroy(dz_decoder_t *dec)
{
    free(dec);
}

int32_t dzd_next(dz_decoder_t *dec, uint32_t dz_code)
{
    int32_t delta = (int32_t)((dz_code >> 1) ^ -(int32_t)(dz_code & 1));
    dec->last_sample += delta;
    return dec->last_sample;
}
