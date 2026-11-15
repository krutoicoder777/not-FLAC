#include "predictor_decoder.h"
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
    lpc_decoder_t *lpc;
    int fixed_order;
    int fixed_count;
    int32_t *fixed_history;
};

static int32_t unzigzag(uint32_t z)
{
    return (int32_t)((z >> 1) ^ -(int32_t)(z & 1));
}

static int32_t decode_fixed_sample(predictor_decoder_t *dec, uint32_t zresidual)
{
    int32_t residual;
    int order;
    int i;
    int32_t hat;
    int32_t sample;
    int j;

    residual = unzigzag(zresidual);
    order = dec->fixed_order;
    i = dec->fixed_count;
    hat = 0;

    if (i >= order && order > 0)
    {
        hat = sp_decode_prediction(order, dec->fixed_history, i);
    }

    sample = residual + hat;

    if (order > 0)
    {
        for (j = order - 1; j > 0; j--)
        {
            dec->fixed_history[j] = dec->fixed_history[j - 1];
        }

        dec->fixed_history[0] = sample;
    }

    dec->fixed_count++;

    return sample;
}

predictor_decoder_t *predictor_decoder_create_lpc(int order, int block_size)
{
    predictor_decoder_t *dec;

    dec = malloc(sizeof(predictor_decoder_t));
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

    dec->samples = malloc(sizeof(int32_t) * (size_t)block_size);

    if (!dec->samples)
    {
        lpc_decoder_destroy(dec->lpc);
        free(dec);
        return NULL;
    }

    dec->fixed_order = 0;
    dec->fixed_count = 0;
    dec->fixed_history = NULL;

    return dec;
}

predictor_decoder_t *predictor_decoder_create_fixed(int fixed_order, int block_size)
{
    predictor_decoder_t *dec;

    if (fixed_order < 0 || fixed_order > 6)
    {
        return NULL;
    }

    dec = malloc(sizeof(predictor_decoder_t));
    if (!dec)
    {
        return NULL;
    }

    dec->method = PREDICTOR_FIXED;
    dec->max_block_size = block_size;
    dec->cur_block_size = 0;
    dec->fixed_order = fixed_order;
    dec->fixed_count = 0;
    dec->fixed_history = calloc(fixed_order > 0 ? (size_t)fixed_order : 1, sizeof(int32_t));

    if (!dec->fixed_history)
    {
        free(dec);
        return NULL;
    }

    dec->samples = malloc(sizeof(int32_t) * (size_t)block_size);

    if (!dec->samples)
    {
        free(dec->fixed_history);
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

const int32_t *predictor_decoder_decode_block(predictor_decoder_t *dec, const uint32_t *residuals, int block_size)
{
    int i;

    if (block_size > dec->max_block_size)
    {
        block_size = dec->max_block_size;
    }

    dec->cur_block_size = block_size;

    if (dec->method == PREDICTOR_LPC)
    {
        for (i = 0; i < block_size; i++)
        {
            dec->samples[i] = lpc_decoder_decode_sample(dec->lpc, residuals[i]);
        }
    }
    else if (dec->method == PREDICTOR_FIXED)
    {
        for (i = 0; i < block_size; i++)
        {
            dec->samples[i] = decode_fixed_sample(dec, residuals[i]);
        }
    }

    return dec->samples;
}
