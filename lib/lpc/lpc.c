#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "lpc.h"

struct lpc_encoder {
    int order;
    const int32_t *current_block;
    int current_block_size;
    int block_tail;
    int32_t *quotients;
    int precision;
};

struct lpc_decoder {
    int order;
    int32_t *quotients;
    int precision;
    int32_t *history;
    int history_fill;
};

static uint32_t zigzag(int32_t n)
{
    return ((uint32_t)n << 1) ^ (uint32_t)(n >> 31);
}

lpc_encoder_t *lpc_encoder_create(int order)
{
    if (order < 1 || order > 32)
    {
        return NULL;
    }
    lpc_encoder_t *enc = malloc(sizeof(lpc_encoder_t));
    if (!enc)
    {
        return NULL;
    }
    enc->quotients = malloc(sizeof(int32_t) * order);
    if (!enc->quotients)
    {
        free(enc);
        return NULL;
    }
    enc->order = order;
    enc->current_block = NULL;
    enc->current_block_size = 0;
    enc->block_tail = 0;
    enc->precision = 0;
    return enc;
}

void lpc_encoder_destroy(lpc_encoder_t *enc)
{
    if (enc)
    {
        free(enc->quotients);
        free(enc);
    }
}

static int64_t *autocorrelation(const int32_t *block, int order, int n)
{
    int64_t *r = malloc(sizeof(int64_t) * (order + 1));
    if (!r)
    {
        return NULL;
    }
    for (int k = 0; k <= order; k++)
    {
        int64_t sum = 0;
        for (int i = k; i < n; i++)
        {
            sum += (int64_t)block[i] * block[i - k];
        }
        r[k] = sum;
    }
    return r;
}

static void levinson_durbin(const int64_t *r, int order, double *a)
{
    double E = (double)r[0];
    for (int i = 0; i < order; i++)
    {
        a[i] = 0.0;
    }
    if (E <= 0.0)
    {
        return;
    }
    for (int m = 1; m <= order; m++)
    {
        double k = (double)r[m];
        for (int j = 1; j < m; j++)
        {
            k -= a[j - 1] * (double)r[m - j];
        }
        k /= E;
        if (k > 1.0) k = 1.0;
        if (k < -1.0) k = -1.0;
        double a_prev[32];
        for (int j = 0; j < m - 1; j++)
        {
            a_prev[j] = a[j];
        }
        a[m - 1] = k;
        for (int j = 1; j < m; j++)
        {
            a[j - 1] = a_prev[j - 1] - k * a_prev[m - j - 1];
        }
        E *= (1.0 - k * k);
        if (E <= 0.0)
        {
            break;
        }
    }
}

static uint64_t residual_cost(const int32_t *block, int n, int order,
                              const int32_t *quotients, int precision)
{
    uint64_t cost = 0;
    for (int i = order; i < n; i++)
    {
        int64_t pred = 0;
        for (int j = 0; j < order; j++)
        {
            pred += (int64_t)quotients[j] * block[i - 1 - j];
        }
        int32_t hat = (int32_t)(pred >> precision);
        int32_t res = block[i] - hat;
        cost += (uint64_t)(res < 0 ? -(int64_t)res : res);
    }
    return cost;
}

void lpc_encoder_encode_block(lpc_encoder_t *enc, const int32_t *block, int count)
{
    enc->current_block = block;
    enc->current_block_size = count;
    enc->block_tail = count;
    for (int i = 0; i < enc->order; i++)
    {
        enc->quotients[i] = 0;
    }
    enc->precision = 0;
    if (count <= enc->order)
    {
        return;
    }
    int64_t *r = autocorrelation(block, enc->order, count);
    if (!r)
    {
        enc->current_block_size = 0;
        return;
    }
    double *a = malloc(sizeof(double) * enc->order);
    if (!a)
    {
        free(r);
        enc->current_block_size = 0;
        return;
    }
    levinson_durbin(r, enc->order, a);
    double amax = 0.0;
    for (int i = 0; i < enc->order; i++)
    {
        double v = fabs(a[i]);
        if (v > amax)
        {
            amax = v;
        }
    }
    int S_base = 0;
    if (amax > 0.0)
    {
        S_base = 15 - 1 - (int)ceil(log2(amax));
        if (S_base < 0) S_base = 0;
        if (S_base > 15) S_base = 15;
    }

    int32_t tmp_q[32];
    int best_S = S_base;
    uint64_t best_cost = UINT64_MAX;

    for (int dS = 0; dS <= 0; dS++)
    {
        int S = S_base + dS;
        if (S < 0 || S > 15) continue;

        for (int i = 0; i < enc->order; i++)
        {
            tmp_q[i] = (int32_t)round(a[i] * (1 << S));
        }

        uint64_t cost = residual_cost(block, count, enc->order, tmp_q, S);
        cost += (uint64_t)enc->order * 32;

        if (cost < best_cost)
        {
            best_cost = cost;
            best_S = S;
        }
    }

    for (int i = 0; i < enc->order; i++)
    {
        enc->quotients[i] = (int32_t)round(a[i] * (1 << best_S));
    }
    enc->precision = best_S;

    free(a);
    free(r);
}

uint32_t lpc_encoder_pop(lpc_encoder_t *enc)
{
    if (enc->block_tail <= 0)
    {
        return 0;
    }
    int idx = enc->current_block_size - enc->block_tail;
    if (idx < enc->order)
    {
        enc->block_tail--;
        return zigzag(enc->current_block[idx]);
    }
    int64_t pred = 0;
    for (int j = 0; j < enc->order; j++)
    {
        pred += (int64_t)enc->quotients[j] * enc->current_block[idx - 1 - j];
    }
    int32_t hat = (int32_t)(pred >> enc->precision);
    int32_t residual = enc->current_block[idx] - hat;
    enc->block_tail--;
    return zigzag(residual);
}

const int32_t *lpc_encoder_quotients(lpc_encoder_t *enc)
{
    return enc->quotients;
}

int lpc_encoder_precision(lpc_encoder_t *enc)
{
    return enc->precision;
}

lpc_decoder_t *lpc_decoder_create(int order)
{
    if (order < 1 || order > 32)
    {
        return NULL;
    }
    lpc_decoder_t *dec = malloc(sizeof(lpc_decoder_t));
    if (!dec)
    {
        return NULL;
    }
    dec->quotients = malloc(sizeof(int32_t) * order);
    dec->history = calloc(order, sizeof(int32_t));
    if (!dec->quotients || !dec->history)
    {
        free(dec->quotients);
        free(dec->history);
        free(dec);
        return NULL;
    }
    dec->order = order;
    dec->precision = 0;
    dec->history_fill = 0;
    return dec;
}

void lpc_decoder_destroy(lpc_decoder_t *dec)
{
    if (dec)
    {
        free(dec->quotients);
        free(dec->history);
        free(dec);
    }
}

void lpc_decoder_set_quotients(lpc_decoder_t *dec, const int32_t *quotients, int precision)
{
    memcpy(dec->quotients, quotients, dec->order * sizeof(int32_t));
    dec->precision = precision;
    dec->history_fill = 0;
    memset(dec->history, 0, dec->order * sizeof(int32_t));
}

static int32_t unzigzag(uint32_t z)
{
    return (int32_t)((z >> 1) ^ -(int32_t)(z & 1));
}

int32_t lpc_decoder_decode_sample(lpc_decoder_t *dec, uint32_t zresidual)
{
    int32_t residual = unzigzag(zresidual);
    int64_t pred = 0;
    if (dec->history_fill >= dec->order)
    {
        for (int j = 0; j < dec->order; j++)
        {
            pred += (int64_t)dec->quotients[j] * dec->history[j];
        }
    }
    int32_t hat = (int32_t)(pred >> dec->precision);
    int32_t sample = residual + hat;
    for (int j = dec->order - 1; j > 0; j--)
    {
        dec->history[j] = dec->history[j - 1];
    }
    dec->history[0] = sample;
    if (dec->history_fill < dec->order)
    {
        dec->history_fill++;
    }
    return sample;
}
