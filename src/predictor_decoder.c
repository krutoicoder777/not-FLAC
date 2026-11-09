#include "predictor_decoder.h"
#include "dz_processor.h"
#include "lpc.h"
#include "notflac_format.h"
#include "simple_predictors.h"
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

    int fixed_order;
    int fixed_count;
    int32_t *fixed_history;
};

predictor_decoder_t *predictor_decoder_create_lpc(int order, int block_size)
{
    predictor_decoder_t *dec = malloc(sizeof(predictor_decoder_t));
    if (!dec) return NULL;
    dec->method = PREDICTOR_LPC;
    dec->max_block_size = block_size;
    dec->cur_block_size = 0;
    dec->lpc = lpc_decoder_create(order);
    if (!dec->lpc) { free(dec); return NULL; }
    dec->samples = malloc(sizeof(int32_t) * block_size);
    if (!dec->samples) { lpc_decoder_destroy(dec->lpc); free(dec); return NULL; }
    dec->dzd = NULL;
    dec->fixed_order = 0;
    dec->fixed_count = 0;
    dec->fixed_history = NULL;
    return dec;
}

predictor_decoder_t *predictor_decoder_create_delta(int bits_per_sample, int block_size)
{
    predictor_decoder_t *dec = malloc(sizeof(predictor_decoder_t));
    if (!dec) return NULL;
    dec->method = PREDICTOR_DELTA;
    dec->max_block_size = block_size;
    dec->cur_block_size = 0;
    dec->dzd = dzd_init(bits_per_sample);
    if (!dec->dzd) { free(dec); return NULL; }
    dec->samples = malloc(sizeof(int32_t) * block_size);
    if (!dec->samples) { dzd_destroy(dec->dzd); free(dec); return NULL; }
    dec->lpc = NULL;
    dec->fixed_order = 0;
    dec->fixed_count = 0;
    dec->fixed_history = NULL;
    return dec;
}

predictor_decoder_t *predictor_decoder_create_fixed(int fixed_order, int block_size)
{
    if (fixed_order < 0 || fixed_order > 4) return NULL;
    predictor_decoder_t *dec = malloc(sizeof(predictor_decoder_t));
    if (!dec) return NULL;
    dec->method = PREDICTOR_FIXED;
    dec->max_block_size = block_size;
    dec->cur_block_size = 0;
    dec->fixed_order = fixed_order;
    dec->fixed_count = 0;
    dec->fixed_history = calloc(fixed_order > 0 ? fixed_order : 1, sizeof(int32_t));
    if (!dec->fixed_history) { free(dec); return NULL; }
    dec->samples = malloc(sizeof(int32_t) * block_size);
    if (!dec->samples) { free(dec->fixed_history); free(dec); return NULL; }
    dec->lpc = NULL;
    dec->dzd = NULL;
    return dec;
}

void predictor_decoder_destroy(predictor_decoder_t *dec)
{
    if (!dec) return;
    if (dec->method == PREDICTOR_LPC && dec->lpc) lpc_decoder_destroy(dec->lpc);
    if (dec->method == PREDICTOR_DELTA && dec->dzd) dzd_destroy(dec->dzd);
    free(dec->samples);
    free(dec->fixed_history);
    free(dec);
}

void predictor_decoder_set_quotients(predictor_decoder_t *dec, const int32_t *quotients, int precision)
{
    if (dec->method == PREDICTOR_LPC && dec->lpc)
    {
        lpc_decoder_set_quotients(dec->lpc, quotients, precision);
    }
}

static int32_t unzigzag(uint32_t z)
{
    return (int32_t)((z >> 1) ^ -(int32_t)(z & 1));
}

static int32_t decode_fixed_sample(predictor_decoder_t *dec, uint32_t zresidual)
{
    int32_t residual = unzigzag(zresidual);
    int order = dec->fixed_order;
    int i = dec->fixed_count;
    int32_t hat = 0;
    if (i >= order && order > 0)
    {
        hat = sp_decode_prediction(order, dec->fixed_history, i);
    }
    int32_t sample = residual + hat;
    if (order > 0)
    {
        for (int j = order - 1; j > 0; j--)
        {
            dec->fixed_history[j] = dec->fixed_history[j - 1];
        }
        dec->fixed_history[0] = sample;
    }
    dec->fixed_count++;
    return sample;
}

const int32_t *predictor_decoder_decode_block(predictor_decoder_t *dec, const uint32_t *residuals, int block_size)
{
    if (block_size > dec->max_block_size) block_size = dec->max_block_size;
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
            dec->samples[i] = lpc_decoder_decode_sample(dec->lpc, residuals[i]);
        }
    }
    else if (dec->method == PREDICTOR_FIXED)
    {
        for (int i = 0; i < block_size; i++)
        {
            dec->samples[i] = decode_fixed_sample(dec, residuals[i]);
        }
    }
    return dec->samples;
}

predictor_method_t predictor_decoder_method(predictor_decoder_t *dec)
{
    return dec->method;
}
