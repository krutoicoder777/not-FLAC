#include "predictor_decoder.h"
#include "dz_processor.h"
#include "lpc.h"
#include "notflac_format.h"
#include <stdint.h>
#include <stdlib.h>

struct predictor_decoder
{
    predictor_method_t method;
    int max_block_size;
    int cur_block_size;
    int32_t *samples;
    dz_decoder_t *dzd;
    lpc_decoder_t *lpc;
};

static int32_t unzigzag(uint32_t z)
{
    return (int32_t)((z >> 1) ^ -(int32_t)(z & 1));
}

predictor_decoder_t *predictor_decoder_create_lpc(int order, int block_size)
{
    predictor_decoder_t *dec = malloc(sizeof(predictor_decoder_t));
    if (!dec)
    {
        return NULL;
    }

    dec->method = PREDICTOR_LPC;
    dec->max_block_size = block_size;
    dec->cur_block_size = 0;
    dec->lpc = lpc_decoder_create(order);
    if (!dec->lpc)
    {
        free(dec);
        return NULL;
    }

    dec->samples = malloc(sizeof(int32_t) * block_size);
    if (!dec->samples)
    {
        lpc_decoder_destroy(dec->lpc);
        free(dec);
        return NULL;
    }
    dec->dzd = NULL;
    return dec;
}

predictor_decoder_t *predictor_decoder_create_delta(int bits_per_sample, int block_size)
{
    predictor_decoder_t *dec = malloc(sizeof(predictor_decoder_t));
    if (!dec)
    {
        return NULL;
    }

    dec->method = PREDICTOR_DELTA;
    dec->max_block_size = block_size;
    dec->cur_block_size = 0;
    dec->dzd = dzd_init(bits_per_sample);
    if (!dec->dzd)
    {
        free(dec);
        return NULL;
    }

    dec->samples = malloc(sizeof(int32_t) * block_size);
    if (!dec->samples)
    {
        dzd_destroy(dec->dzd);
        free(dec);
        return NULL;
    }
    dec->lpc = NULL;
    return dec;
}

void predictor_decoder_destroy(predictor_decoder_t *dec)
{
    if (!dec)
    {
        return;
    }
    if (dec->method == PREDICTOR_LPC && dec->lpc)
    {
        lpc_decoder_destroy(dec->lpc);
    }
    if (dec->method == PREDICTOR_DELTA && dec->dzd)
    {
        dzd_destroy(dec->dzd);
    }
    free(dec->samples);
    free(dec);
}

void predictor_decoder_set_quotients(predictor_decoder_t *dec, const int32_t *quotients, int precision)
{
    if (dec->method == PREDICTOR_LPC && dec->lpc)
    {
        lpc_decoder_set_quotients(dec->lpc, quotients, precision);
    }
}

const int32_t *predictor_decoder_decode_block(predictor_decoder_t *dec, const uint32_t *residuals, int block_size)
{
    if (block_size > dec->max_block_size)
    {
        block_size = dec->max_block_size;
    }
    dec->cur_block_size = block_size;

    if (dec->method == PREDICTOR_DELTA)
    {
        for (int i = 0; i < block_size; i++)
        {
            dec->samples[i] = dzd_next(dec->dzd, residuals[i]);
        }
    }
    else if (dec->method == PREDICTOR_LPC)
    {
        for (int i = 0; i < block_size; i++)
        {
            int32_t residual = unzigzag(residuals[i]);
            dec->samples[i] = lpc_decoder_decode_sample(dec->lpc, residual);
        }
    }
    return dec->samples;
}

predictor_method_t predictor_decoder_method(predictor_decoder_t *dec)
{
    return dec->method;
}
