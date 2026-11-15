#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "lpc.h"

#define TUK_ALPHA 0.5
#define TUK_CACHE_MAX 8192
#define LPC_ORDER_MAX_INTERNAL 32

static double g_tukey_cache[TUK_CACHE_MAX];
static int g_tukey_ready = 0;

static void init_tukey(void)
{
    int i;
    double pos;
    double w;

    for (i = 0; i < TUK_CACHE_MAX; i++)
    {
        pos = (double)i / (double)TUK_CACHE_MAX;
        w = 1.0;

        if (pos < TUK_ALPHA * 0.5)
            w = 0.5 * (1.0 + cos(M_PI * (2.0 * pos / TUK_ALPHA - 1.0)));
        else if (pos > 1.0 - TUK_ALPHA * 0.5)
            w = 0.5 * (1.0 + cos(M_PI * (2.0 * pos / TUK_ALPHA - 2.0 / TUK_ALPHA + 1.0)));

        g_tukey_cache[i] = w;
    }

    g_tukey_ready = 1;
}

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
    int32_t *reversed_q;
    int precision;
    int32_t *history;
    int history_fill;
    int write_pos;
};

static uint32_t zigzag(int32_t n)
{
    return ((uint32_t)n << 1) ^ (uint32_t)(n >> 31);
}

static int32_t unzigzag(uint32_t z)
{
    return (int32_t)((z >> 1) ^ -(int32_t)(z & 1));
}

lpc_encoder_t *lpc_encoder_create(int order)
{
    lpc_encoder_t *enc;

    if (order < 1 || order > 32) return NULL;

    enc = malloc(sizeof(lpc_encoder_t));
    if (!enc) return NULL;

    enc->quotients = malloc(sizeof(int32_t) * (size_t)order);
    if (!enc->quotients) { free(enc); return NULL; }

    enc->order = order;
    enc->current_block = NULL;
    enc->current_block_size = 0;
    enc->block_tail = 0;
    enc->precision = 0;

    if (!g_tukey_ready) init_tukey();

    return enc;
}

void lpc_encoder_destroy(lpc_encoder_t *enc)
{
    if (enc) { free(enc->quotients); free(enc); }
}

static int64_t *autocorrelation(const int32_t *block, int max_order, int n)
{
    int64_t *r;
    double *windowed;
    int i;
    int k;

    r = malloc(sizeof(int64_t) * (size_t)(max_order + 1));
    if (!r) return NULL;

    windowed = malloc(sizeof(double) * (size_t)n);
    if (!windowed) { free(r); return NULL; }

    for (i = 0; i < n; i++)
    {
        double w = g_tukey_cache[(i * TUK_CACHE_MAX) / n];
        windowed[i] = (double)block[i] * w;
    }

    for (k = 0; k <= max_order; k++)
    {
        double sum = 0.0;
        #pragma omp simd reduction(+:sum)
        for (i = k; i < n; i++) sum += windowed[i] * windowed[i - k];
        r[k] = (int64_t)sum;
    }

    free(windowed);
    return r;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"

static void levinson_durbin_all(const int64_t *r, int max_order,
                                double a_all[33][32], double E_all[33])
{
    double a[32];
    double a_prev[33];
    double E;
    double E0;
    int m, i, j;

    E = (double)r[0];
    E0 = E;

    for (i = 0; i < max_order; i++) a[i] = 0.0;
    for (m = 0; m <= max_order; m++) E_all[m] = 0.0;
    E_all[0] = E;

    if (E <= 0.0) return;

    for (m = 1; m <= max_order; m++)
    {
        double k = (double)r[m];
        for (j = 1; j < m; j++) k -= a[j - 1] * (double)r[m - j];
        k /= E;
        if (k > 1.0) k = 1.0;
        if (k < -1.0) k = -1.0;

        for (j = 0; j < m - 1; j++) a_prev[j] = a[j];
        a[m - 1] = k;
        for (j = 1; j < m; j++) a[j - 1] = a_prev[j - 1] - k * a_prev[m - j - 1];

        E *= (1.0 - k * k);
        E_all[m] = E;
        for (j = 0; j < m; j++) a_all[m][j] = a[j];

        if (E <= 0.0) break;
        if (E < 1e-6 * E0) break;
    }
}

#pragma GCC diagnostic pop

static int pick_order_aic(const double *E_all, int n, int max_order)
{
    int best_order = 1;
    int order;
    double best_aic = 1e300;
    double penalty = 22.0;

    if (n <= 0) return 1;

    {
        const char *e = getenv("NOTFLAC_AIC_PENALTY");
        if (e)
        {
            double v = atof(e);
            if (v > 0.0) penalty = v;
        }
    }

    for (order = 1; order <= max_order; order++)
    {
        double E, aic;
        if (order >= n) break;
        E = E_all[order];
        if (E <= 0.0) continue;
        aic = (double)n * log(E / (double)n) + penalty * (double)order;
        if (aic < best_aic) { best_aic = aic; best_order = order; }
    }

    return best_order;
}

static uint64_t lpc_rice_estimate(const int32_t *residuals, uint32_t n)
{
    uint64_t sum, mean, best;
    int k0, k;
    uint32_t i;

    if (n == 0) return 0;

    sum = 0;
    for (i = 0; i < n; i++)
    {
        uint64_t zz = ((uint64_t)residuals[i] << 1) ^ (uint64_t)(residuals[i] >> 31);
        sum += zz;
    }
    mean = sum / n;

    k0 = 0;
    while (((uint64_t)1 << (k0 + 1)) <= mean && k0 < 14) k0++;

    best = UINT64_MAX;
    for (k = (k0 > 3 ? k0 - 3 : 0); k <= (k0 + 3 < 14 ? k0 + 3 : 14); k++)
    {
        uint64_t bits = 0;
        for (i = 0; i < n; i++)
        {
            uint64_t zz = ((uint64_t)residuals[i] << 1) ^ (uint64_t)(residuals[i] >> 31);
            bits += (zz >> k) + 1 + (uint64_t)k;
        }
        if (bits < best) best = bits;
    }
    return best + 4;
}

static void quantize_coefficients(const int32_t *block, int n, int order,
                                  const double *a, int32_t *out_quotients, int *out_precision)
{
    double amax = 0.0;
    int S_base;
    int32_t tmp_q[32];
    int32_t best_q[32];
    int best_S;
    uint64_t best_cost;
    double error;
    int i, j, S;
    int32_t *residuals;
    uint64_t rice_cost;

    for (i = 0; i < order; i++)
    {
        double v = fabs(a[i]);
        if (v > amax) amax = v;
    }

    S_base = 0;
    if (amax > 0.0)
    {
        S_base = 15 - 1 - (int)ceil(log2(amax));
        if (S_base < 0) S_base = 0;
        if (S_base > 15) S_base = 15;
    }

    residuals = malloc(sizeof(int32_t) * (size_t)(n > 0 ? n : 1));
    if (!residuals)
    {
        for (i = 0; i < order; i++) out_quotients[i] = 0;
        *out_precision = 0;
        return;
    }

    best_S = S_base;
    best_cost = UINT64_MAX;
    memset(best_q, 0, sizeof(best_q));

    for (S = S_base - 1; S <= S_base + 1; S++)
    {
        int s_eff = S;
        if (s_eff < 0) s_eff = 0;
        if (s_eff > 15) s_eff = 15;

        error = 0.0;
        for (i = 0; i < order; i++)
        {
            double desired = a[i] * (double)(1 << s_eff) + error;
            int32_t q = (int32_t)round(desired);
            if (q > 16383) q = 16383;
            if (q < -16384) q = -16384;
            tmp_q[i] = q;
            error = desired - (double)q;
        }

        for (i = order; i < n; i++)
        {
            int64_t pred = 0;
            for (j = 0; j < order; j++)
                pred += (int64_t)tmp_q[j] * block[i - 1 - j];
            int32_t hat = (int32_t)(pred >> s_eff);
            residuals[i] = block[i] - hat;
        }

        rice_cost = lpc_rice_estimate(residuals + order, (uint32_t)(n - order));
        rice_cost += (uint64_t)order * 8;

        if (rice_cost < best_cost)
        {
            best_cost = rice_cost;
            best_S = s_eff;
            memcpy(best_q, tmp_q, sizeof(int32_t) * (size_t)order);
        }
    }

    free(residuals);
    memcpy(out_quotients, best_q, sizeof(int32_t) * (size_t)order);
    *out_precision = best_S;
}

void lpc_encoder_encode_block(lpc_encoder_t *enc, const int32_t *block, int count)
{
    int max_order;
    int64_t *r;
    double a_all[33][32];
    double E_all[33];
    int chosen_order;
    double a_final[32];
    int32_t q[32];
    int precision;
    int i;

    enc->current_block = block;
    enc->current_block_size = count;
    enc->block_tail = count;

    for (i = 0; i < enc->order; i++) enc->quotients[i] = 0;
    enc->precision = 0;

    if (count <= 1) return;

    max_order = enc->order;
    if (max_order > count - 1) max_order = count - 1;
    if (max_order > LPC_ORDER_MAX_INTERNAL) max_order = LPC_ORDER_MAX_INTERNAL;

    r = autocorrelation(block, max_order, count);
    if (!r) { enc->current_block_size = 0; return; }

    levinson_durbin_all(r, max_order, a_all, E_all);
    free(r);

    chosen_order = pick_order_aic(E_all, count, max_order);
    if (chosen_order < 1) chosen_order = 1;
    if (chosen_order > max_order) chosen_order = max_order;

    for (i = 0; i < chosen_order; i++) a_final[i] = a_all[chosen_order][i];

    precision = 0;
    quantize_coefficients(block, count, chosen_order, a_final, q, &precision);

    for (i = 0; i < chosen_order; i++) enc->quotients[i] = q[i];
    enc->order = chosen_order;
    enc->precision = precision;
}

uint32_t lpc_encoder_pop(lpc_encoder_t *enc)
{
    int idx;
    int64_t p0, p1;
    const int32_t *q;
    const int32_t *b;
    int j;
    int64_t pred;
    int32_t hat, residual;

    if (enc->block_tail <= 0) return 0;
    idx = enc->current_block_size - enc->block_tail;

    if (idx < enc->order)
    {
        enc->block_tail--;
        return zigzag(enc->current_block[idx]);
    }

    p0 = 0; p1 = 0;
    q = enc->quotients;
    b = enc->current_block + idx - 1;
    j = 0;

    for (; j + 15 < enc->order; j += 16)
    {
        p0 += (int64_t)q[j] * b[-j];
        p1 += (int64_t)q[j + 1] * b[-j - 1];
        p0 += (int64_t)q[j + 2] * b[-j - 2];
        p1 += (int64_t)q[j + 3] * b[-j - 3];
        p0 += (int64_t)q[j + 4] * b[-j - 4];
        p1 += (int64_t)q[j + 5] * b[-j - 5];
        p0 += (int64_t)q[j + 6] * b[-j - 6];
        p1 += (int64_t)q[j + 7] * b[-j - 7];
        p0 += (int64_t)q[j + 8] * b[-j - 8];
        p1 += (int64_t)q[j + 9] * b[-j - 9];
        p0 += (int64_t)q[j + 10] * b[-j - 10];
        p1 += (int64_t)q[j + 11] * b[-j - 11];
        p0 += (int64_t)q[j + 12] * b[-j - 12];
        p1 += (int64_t)q[j + 13] * b[-j - 13];
        p0 += (int64_t)q[j + 14] * b[-j - 14];
        p1 += (int64_t)q[j + 15] * b[-j - 15];
    }
    for (; j + 7 < enc->order; j += 8)
    {
        p0 += (int64_t)q[j] * b[-j];
        p1 += (int64_t)q[j + 1] * b[-j - 1];
        p0 += (int64_t)q[j + 2] * b[-j - 2];
        p1 += (int64_t)q[j + 3] * b[-j - 3];
        p0 += (int64_t)q[j + 4] * b[-j - 4];
        p1 += (int64_t)q[j + 5] * b[-j - 5];
        p0 += (int64_t)q[j + 6] * b[-j - 6];
        p1 += (int64_t)q[j + 7] * b[-j - 7];
    }
    for (; j + 3 < enc->order; j += 4)
    {
        p0 += (int64_t)q[j] * b[-j];
        p1 += (int64_t)q[j + 1] * b[-j - 1];
        p0 += (int64_t)q[j + 2] * b[-j - 2];
        p1 += (int64_t)q[j + 3] * b[-j - 3];
    }
    for (; j < enc->order; j++) p0 += (int64_t)q[j] * b[-j];

    pred = p0 + p1;
    hat = (int32_t)(pred >> enc->precision);
    residual = enc->current_block[idx] - hat;
    enc->block_tail--;
    return zigzag(residual);
}

const int32_t *lpc_encoder_quotients(lpc_encoder_t *enc) { return enc->quotients; }
int lpc_encoder_precision(lpc_encoder_t *enc) { return enc->precision; }
int lpc_encoder_order(lpc_encoder_t *enc) { return enc->order; }

lpc_decoder_t *lpc_decoder_create(int order)
{
    lpc_decoder_t *dec;
    if (order < 1 || order > 32) return NULL;
    dec = malloc(sizeof(lpc_decoder_t));
    if (!dec) return NULL;
    dec->quotients = malloc(sizeof(int32_t) * (size_t)order);
    dec->reversed_q = malloc(sizeof(int32_t) * (size_t)order);
    dec->history = calloc((size_t)(2 * order), sizeof(int32_t));
    if (!dec->quotients || !dec->reversed_q || !dec->history)
    {
        free(dec->quotients); free(dec->reversed_q); free(dec->history); free(dec);
        return NULL;
    }
    dec->order = order;
    dec->precision = 0;
    dec->history_fill = 0;
    dec->write_pos = 0;
    return dec;
}

void lpc_decoder_destroy(lpc_decoder_t *dec)
{
    if (dec)
    {
        free(dec->quotients);
        free(dec->reversed_q);
        free(dec->history);
        free(dec);
    }
}

void lpc_decoder_set_quotients(lpc_decoder_t *dec, const int32_t *quotients, int precision)
{
    int i;
    for (i = 0; i < dec->order; i++)
    {
        dec->quotients[i] = quotients[i];
        dec->reversed_q[i] = quotients[dec->order - 1 - i];
    }
    dec->precision = precision;
    dec->history_fill = 0;
    dec->write_pos = 0;
    memset(dec->history, 0, (size_t)(2 * dec->order) * sizeof(int32_t));
}

int32_t lpc_decoder_decode_sample(lpc_decoder_t *dec, uint32_t zresidual)
{
    int32_t residual;
    int64_t pred;
    int32_t hat, sample;
    int j;

    residual = unzigzag(zresidual);
    pred = 0;

    if (dec->history_fill >= dec->order)
    {
        int64_t p0, p1;
        const int32_t *qr, *h;
        p0 = 0; p1 = 0;
        qr = dec->reversed_q;
        h = dec->history + dec->write_pos;
        j = 0;
        for (; j + 15 < dec->order; j += 16)
        {
            p0 += (int64_t)qr[j] * h[j];
            p1 += (int64_t)qr[j + 1] * h[j + 1];
            p0 += (int64_t)qr[j + 2] * h[j + 2];
            p1 += (int64_t)qr[j + 3] * h[j + 3];
            p0 += (int64_t)qr[j + 4] * h[j + 4];
            p1 += (int64_t)qr[j + 5] * h[j + 5];
            p0 += (int64_t)qr[j + 6] * h[j + 6];
            p1 += (int64_t)qr[j + 7] * h[j + 7];
            p0 += (int64_t)qr[j + 8] * h[j + 8];
            p1 += (int64_t)qr[j + 9] * h[j + 9];
            p0 += (int64_t)qr[j + 10] * h[j + 10];
            p1 += (int64_t)qr[j + 11] * h[j + 11];
            p0 += (int64_t)qr[j + 12] * h[j + 12];
            p1 += (int64_t)qr[j + 13] * h[j + 13];
            p0 += (int64_t)qr[j + 14] * h[j + 14];
            p1 += (int64_t)qr[j + 15] * h[j + 15];
        }
        for (; j + 7 < dec->order; j += 8)
        {
            p0 += (int64_t)qr[j] * h[j];
            p1 += (int64_t)qr[j + 1] * h[j + 1];
            p0 += (int64_t)qr[j + 2] * h[j + 2];
            p1 += (int64_t)qr[j + 3] * h[j + 3];
            p0 += (int64_t)qr[j + 4] * h[j + 4];
            p1 += (int64_t)qr[j + 5] * h[j + 5];
            p0 += (int64_t)qr[j + 6] * h[j + 6];
            p1 += (int64_t)qr[j + 7] * h[j + 7];
        }
        for (; j + 3 < dec->order; j += 4)
        {
            p0 += (int64_t)qr[j] * h[j];
            p1 += (int64_t)qr[j + 1] * h[j + 1];
            p0 += (int64_t)qr[j + 2] * h[j + 2];
            p1 += (int64_t)qr[j + 3] * h[j + 3];
        }
        for (; j < dec->order; j++) p0 += (int64_t)qr[j] * h[j];
        pred = p0 + p1;
    }

    hat = (int32_t)(pred >> dec->precision);
    sample = residual + hat;

    dec->history[dec->write_pos] = sample;
    dec->history[dec->write_pos + dec->order] = sample;
    dec->write_pos++;
    if (dec->write_pos >= dec->order) dec->write_pos = 0;
    if (dec->history_fill < dec->order) dec->history_fill++;
    return sample;
}
