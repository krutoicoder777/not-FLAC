#include "predictor_encoder.h"
#include "lpc.h"
#include "notflac_format.h"
#include "simple_predictors.h"
#include <stdint.h>
#include <stdlib.h>

struct predictor_encoder
{
    predictor_method_t method;
    int max_block_size;
    int cur_block_size;
    uint32_t *residuals;
    lpc_encoder_t *lpc;
    int fixed_order;
};

predictor_encoder_t *predictor_encoder_create_lpc(int order, int block_size)
{
    predictor_encoder_t *pe;

    pe = malloc(sizeof(predictor_encoder_t));
    if (!pe)
    {
        return NULL;
    }

    pe->method = PREDICTOR_LPC;
    pe->max_block_size = block_size;
    pe->cur_block_size = 0;
    pe->lpc = lpc_encoder_create(order);

    if (!pe->lpc)
    {
        free(pe);
        return NULL;
    }

    pe->residuals = malloc(sizeof(uint32_t) * (size_t)block_size);

    if (!pe->residuals)
    {
        lpc_encoder_destroy(pe->lpc);
        free(pe);
        return NULL;
    }

    pe->fixed_order = 0;

    return pe;
}

predictor_encoder_t *predictor_encoder_create_fixed(int fixed_order, int block_size)
{
    predictor_encoder_t *pe;

    if (fixed_order < 0 || fixed_order > 6)
    {
        return NULL;
    }

    pe = malloc(sizeof(predictor_encoder_t));
    if (!pe)
    {
        return NULL;
    }

    pe->method = PREDICTOR_FIXED;
    pe->max_block_size = block_size;
    pe->cur_block_size = 0;
    pe->fixed_order = fixed_order;
    pe->residuals = malloc(sizeof(uint32_t) * (size_t)block_size);

    if (!pe->residuals)
    {
        free(pe);
        return NULL;
    }

    pe->lpc = NULL;

    return pe;
}

void predictor_encoder_destroy(predictor_encoder_t *pe)
{
    if (!pe)
    {
        return;
    }

    if (pe->method == PREDICTOR_LPC)
    {
        lpc_encoder_destroy(pe->lpc);
    }

    free(pe->residuals);
    free(pe);
}

const uint32_t *predictor_encoder_encode_block(predictor_encoder_t *pe, const int32_t *block, int block_size)
{
    int i;

    if (block_size > pe->max_block_size)
    {
        block_size = pe->max_block_size;
    }

    pe->cur_block_size = block_size;

    if (pe->method == PREDICTOR_LPC)
    {
        lpc_encoder_encode_block(pe->lpc, block, block_size);

        for (i = 0; i < block_size; i++)
        {
            pe->residuals[i] = lpc_encoder_pop(pe->lpc);
        }
    }
    else if (pe->method == PREDICTOR_FIXED)
    {
        for (i = 0; i < block_size; i++)
        {
            pe->residuals[i] = sp_encode_elem(pe->fixed_order, block, i);
        }
    }

    return pe->residuals;
}

const uint32_t *predictor_encoder_encode_block_fixed(predictor_encoder_t *pe, const int32_t *block, int block_size)
{
    int i;

    if (block_size > pe->max_block_size)
    {
        block_size = pe->max_block_size;
    }

    pe->cur_block_size = block_size;

    for (i = 0; i < block_size; i++)
    {
        pe->residuals[i] = sp_encode_elem(pe->fixed_order, block, i);
    }

    return pe->residuals;
}

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

int predictor_encoder_order(predictor_encoder_t *pe)
{
    if (pe->lpc)
    {
        return lpc_encoder_order(pe->lpc);
    }

    return 0;
}
