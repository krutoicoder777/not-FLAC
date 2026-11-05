#include "dz_processor.h"
#include "lpc.h"
#include "notflac_format.h"
#include <stdint.h>
#include <stdlib.h>

typedef struct predictor_encoder predictor_encoder_t;

struct predictor_encoder
{
    predictor_method_t method;
    int max_block_size;
    int cur_block_size;
    uint32_t *residuals;

    // Delta
    dz_encoder_t *dz;

    // LPC
    lpc_encoder_t *lpc;
};

predictor_encoder_t *predictor_encoder_create_lpc(int order, int block_size)
{
    predictor_encoder_t *pe = malloc(sizeof(predictor_encoder_t));
    if (pe)
    {
        pe->method = PREDICTOR_LPC;
        pe->max_block_size = block_size;
        pe->lpc = lpc_encoder_create(order);
        if (!pe->lpc)
        {
            free(pe);
            return NULL;
        }
        pe->residuals = malloc(sizeof(uint32_t) * block_size);
        if (!pe->residuals)
        {
            lpc_encoder_destroy(pe->lpc);
            free(pe);
            return NULL;
        }
        pe->dz = NULL;
    }
    return pe;
}

predictor_encoder_t *predictor_encoder_create_delta(int16_t bits_per_sample, int block_size)
{
    predictor_encoder_t *pe = malloc(sizeof(predictor_encoder_t));
    if (pe)
    {
        pe->method = PREDICTOR_DELTA;
        pe->max_block_size = block_size;
        pe->dz = dze_init(bits_per_sample);
        if (!pe->dz)
        {
            free(pe);
            return NULL;
        }
        pe->residuals = malloc(sizeof(uint32_t) * block_size);
        if (!pe->residuals)
        {
            dze_destroy(pe->dz);
            free(pe);
            return NULL;
        }
        pe->lpc = NULL;
    }
    return pe;
}

void predictor_encoder_destroy(predictor_encoder_t *pe)
{
    if (pe->method == PREDICTOR_LPC)
    {
        lpc_encoder_destroy(pe->lpc);
    }
    if (pe->method == PREDICTOR_DELTA)
    {
        dze_destroy(pe->dz);
    }
    free(pe->residuals);
    free(pe);
}

static uint32_t zigzag(int32_t n)
{
    return ((uint32_t)n << 1) ^ (uint32_t)(n >> 31);
}

const uint32_t *predictor_encoder_encode_block(predictor_encoder_t *pe, const int32_t *block, int block_size)
{
    if (block_size > pe->max_block_size)
    {
        block_size = pe->max_block_size;
    }
    pe->cur_block_size = block_size;
    if (pe->method == PREDICTOR_DELTA)
    {
        for (int i = 0; i < block_size; i++)
        {
            int32_t sample = block[i];
            uint32_t zz = dze_next(pe->dz, sample);
            pe->residuals[i] = zz;
        }
    }
    else if (pe->method == PREDICTOR_LPC)
    {
        lpc_encoder_encode_block(pe->lpc, block, block_size);
        for (int i = 0; i < block_size; i++)
        {
            int32_t residual = lpc_encoder_pop(pe->lpc);
            pe->residuals[i] = zigzag(residual);
        }
    }
    return pe->residuals;
}

predictor_method_t predictor_encoder_method(predictor_encoder_t *pe)
{
    return pe->method;
}

// LPC
const int32_t *predictor_encoder_quotients(predictor_encoder_t *pe)
{
    if (pe->lpc)
    {
        return lpc_encoder_quotients(pe->lpc);
    }
    return NULL;
}
int predictor_encoder_precision(predictor_encoder_t *pe)
{
    if (pe->lpc)
    {
        return lpc_encoder_precision(pe->lpc);
    }
    return 0;
}
