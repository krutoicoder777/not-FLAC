#include "rice_coder.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_P 7
#define MAX_K (1 << MAX_P)
#define K_RANGE 14
#define VERBATIM 15
#define RICE_IN_CAP 8192

typedef struct rice_coder rice_coder_t;

struct rice_coder
{
    FILE *out;
    uint8_t p;
    uint8_t k[MAX_K];
    uint32_t cur_idx;
    uint32_t part_size;
    uint16_t bps;
    uint64_t bitbuf;
    int bitcnt;
    uint32_t num_warmup;
};

typedef struct rice_decoder rice_decoder_t;

struct rice_decoder
{
    FILE *in;
    uint64_t bitbuf;
    int bitcnt;
    int pad_bits;
    uint8_t k[MAX_K];
    uint8_t p;
    uint32_t cur_idx;
    uint32_t part_size;
    uint32_t part_i;
    uint32_t part_lim;
    uint16_t bps;
    uint32_t num_warmup;
    size_t in_pos;
    size_t in_len;
    uint8_t in_buf[RICE_IN_CAP];
};

static inline void rc_flush_8bytes(rice_coder_t *r)
{
    fputc((int)((r->bitbuf >> 56) & 0xFF), r->out);
    fputc((int)((r->bitbuf >> 48) & 0xFF), r->out);
    fputc((int)((r->bitbuf >> 40) & 0xFF), r->out);
    fputc((int)((r->bitbuf >> 32) & 0xFF), r->out);
    fputc((int)((r->bitbuf >> 24) & 0xFF), r->out);
    fputc((int)((r->bitbuf >> 16) & 0xFF), r->out);
    fputc((int)((r->bitbuf >> 8) & 0xFF), r->out);
    fputc((int)(r->bitbuf & 0xFF), r->out);
    r->bitbuf = 0;
    r->bitcnt = 0;
}

static inline void rc_put_bits(rice_coder_t *r, uint32_t v, int n)
{
    uint64_t mask;

    if (n <= 0)
    {
        return;
    }

    if (n >= 64)
    {
        r->bitbuf = (uint64_t)v;
        r->bitcnt = 64;
        rc_flush_8bytes(r);
        return;
    }

    if (r->bitcnt + n <= 64)
    {
        mask = ((uint64_t)1 << n) - 1;
        r->bitbuf = (r->bitbuf << n) | ((uint64_t)v & mask);
        r->bitcnt += n;
        if (r->bitcnt == 64)
        {
            rc_flush_8bytes(r);
        }
        return;
    }

    {
        int first = 64 - r->bitcnt;
        int rest = n - first;
        uint64_t first_mask = ((uint64_t)1 << first) - 1;
        uint64_t rest_mask = ((uint64_t)1 << rest) - 1;

        r->bitbuf = (r->bitbuf << first) | (((uint64_t)v >> rest) & first_mask);
        r->bitcnt = 64;
        rc_flush_8bytes(r);
        r->bitbuf = (uint64_t)v & rest_mask;
        r->bitcnt = rest;
    }
}

static inline void rc_put_unary_remainder(rice_coder_t *r, uint32_t q, uint32_t rem, int k)
{
    uint64_t total;

    total = (uint64_t)q + 1 + (uint64_t)k;

    if (total <= 64 && r->bitcnt + (int)total <= 64)
    {
        uint64_t pattern = ((uint64_t)1 << k) | (uint64_t)rem;

        if (total == 64)
        {
            r->bitbuf = pattern;
            r->bitcnt = 64;
            rc_flush_8bytes(r);
        }
        else
        {
            r->bitbuf = (r->bitbuf << total) | pattern;
            r->bitcnt += (int)total;
            if (r->bitcnt == 64)
            {
                rc_flush_8bytes(r);
            }
        }
        return;
    }

    while (q > 0)
    {
        int space = 64 - r->bitcnt;
        uint32_t chunk;

        chunk = (q < (uint32_t)space) ? q : (uint32_t)space;

        if (chunk >= 64)
        {
            r->bitbuf = 0;
            r->bitcnt = 64;
        }
        else
        {
            r->bitbuf <<= chunk;
            r->bitcnt += (int)chunk;
        }

        q -= chunk;

        if (r->bitcnt == 64)
        {
            rc_flush_8bytes(r);
        }
    }

    rc_put_bits(r, 1, 1);
    rc_put_bits(r, rem, k);
}

static uint8_t optimal_k(const uint32_t *distribution, int distribution_size, uint64_t *cost)
{
    uint64_t sum;
    uint64_t mean;
    int k0;
    int k_lo;
    int k_hi;
    uint8_t best_k;
    uint64_t best_bits;
    int k;
    int i;

    if (distribution_size <= 0)
    {
        *cost = 0;
        return 0;
    }

    sum = 0;

    for (i = 0; i < distribution_size; i++)
    {
        sum += distribution[i];
    }

    mean = sum / (uint64_t)distribution_size;
    k0 = 0;

    while (((uint64_t)1 << k0) < mean && k0 < K_RANGE)
    {
        k0++;
    }

    k_lo = (k0 > 1) ? k0 - 1 : 0;
    k_hi = (k0 + 1 < K_RANGE) ? k0 + 1 : K_RANGE;
    best_k = 0;
    best_bits = UINT64_MAX;

    for (k = k_lo; k <= k_hi; k++)
    {
        uint64_t bits = 0;

        for (i = 0; i < distribution_size; i++)
        {
            bits += (distribution[i] >> k) + 1 + (uint64_t)k;
        }

        if (bits < best_bits)
        {
            best_bits = bits;
            best_k = (uint8_t)k;
        }
    }

    *cost = best_bits;

    return best_k;
}

rice_coder_t *rice_coder_create(FILE *out, uint16_t bits_per_sample)
{
    rice_coder_t *rice;

    if (bits_per_sample > 32 || bits_per_sample == 0)
    {
        return NULL;
    }

    rice = malloc(sizeof(rice_coder_t));
    if (!rice)
    {
        return NULL;
    }

    rice->bps = bits_per_sample;
    rice->out = out;
    rice->bitbuf = 0;
    rice->bitcnt = 0;
    rice->cur_idx = 0;
    rice->part_size = 0;
    rice->p = 0;
    rice->num_warmup = 0;

    return rice;
}

void rice_coder_destroy(rice_coder_t *rice)
{
    free(rice);
}

void rice_coder_set_num_warmup(rice_coder_t *rice, uint32_t num_warmup)
{
    rice->num_warmup = num_warmup;
}

int rice_coder_set_distribution(rice_coder_t *rice, const uint32_t *distribution, int distribution_size)
{
    int rc;
    uint64_t cost;
    uint64_t best_cost;
    uint64_t prev_cost;
    uint8_t karr[MAX_K];
    int num_parts;
    int part_size;
    int n;
    int start;
    int i;
    int p;

    rc = 0;

    if (rice->bitcnt != 0)
    {
        rice_coder_flush(rice);
        rc = 1;
    }

    if (distribution_size <= 0)
    {
        rice->p = 0;
        rice->k[0] = 0;

        rc_put_bits(rice, 0, 9);

        rice->part_size = 0;
        rice->cur_idx = 0;

        return rc;
    }

    best_cost = UINT64_MAX;
    prev_cost = UINT64_MAX;

    for (p = 0; p <= MAX_P; p++)
    {
        num_parts = 1 << p;
        part_size = (distribution_size + num_parts - 1) / num_parts;
        cost = 5 + 4 * (uint64_t)num_parts;

        for (i = 0; i < num_parts; i++)
        {
            uint32_t warmup_in_part;
            uint32_t data_in_part;
            uint64_t raw_cost;
            uint64_t warmup_raw;
            uint64_t data_bits;
            uint8_t k_choice;
            uint64_t part_total;

            start = i * part_size;

            if (start >= distribution_size)
            {
                break;
            }

            n = (start + part_size > distribution_size) ? (distribution_size - start) : part_size;

            warmup_in_part = 0;

            if ((uint32_t)start < rice->num_warmup)
            {
                uint32_t end = (uint32_t)start + (uint32_t)n;

                if (end > rice->num_warmup)
                {
                    end = rice->num_warmup;
                }

                warmup_in_part = end - (uint32_t)start;
            }

            data_in_part = (uint32_t)n - warmup_in_part;
            raw_cost = (uint64_t)n * rice->bps;
            warmup_raw = (uint64_t)warmup_in_part * rice->bps;

            data_bits = 0;
            k_choice = VERBATIM;

            if (data_in_part > 0)
            {
                k_choice = optimal_k(distribution + start + warmup_in_part, (int)data_in_part, &data_bits);

                if (data_bits >= (uint64_t)data_in_part * rice->bps)
                {
                    k_choice = VERBATIM;
                    data_bits = (uint64_t)data_in_part * rice->bps;
                }
            }

            part_total = warmup_raw + data_bits;

            if (warmup_in_part == 0 && part_total >= raw_cost)
            {
                karr[i] = VERBATIM;
                cost += raw_cost;
            }
            else
            {
                karr[i] = k_choice;
                cost += part_total;
            }
        }

        if (cost < best_cost)
        {
            best_cost = cost;
            rice->p = (uint8_t)p;
            memcpy(rice->k, karr, (size_t)num_parts);
        }

        if (p > 0 && cost >= prev_cost)
        {
            break;
        }

        prev_cost = cost;
    }

    rice->part_size = (distribution_size + (1 << rice->p) - 1) / (1 << rice->p);

    rc_put_bits(rice, rice->p, 5);

    for (i = 0; i < (1 << rice->p); i++)
    {
        rc_put_bits(rice, rice->k[i], 4);
    }

    rice->cur_idx = 0;

    return rc;
}

void rice_coder_encode_elem(rice_coder_t *rice, uint32_t res)
{
    uint32_t data_idx;
    uint8_t cur_k;
    int k;

    if (rice->part_size == 0)
    {
        return;
    }

    if (rice->cur_idx < rice->num_warmup)
    {
        rc_put_bits(rice, res, (int)rice->bps);
        rice->cur_idx++;
        return;
    }

    data_idx = rice->cur_idx - rice->num_warmup;
    cur_k = (uint8_t)(data_idx / rice->part_size);

    if (cur_k >= MAX_K)
    {
        cur_k = MAX_K - 1;
    }

    k = rice->k[cur_k];

    if (k != VERBATIM)
    {
        uint32_t q = res >> k;
        uint32_t r = res & ((1u << k) - 1);

        rc_put_unary_remainder(rice, q, r, k);
    }
    else
    {
        rc_put_bits(rice, res, (int)rice->bps);
    }

    rice->cur_idx++;
}

void rice_coder_flush(rice_coder_t *rice)
{
    if (rice->bitcnt > 0)
    {
        int pad = (8 - (rice->bitcnt % 8)) % 8;

        if (pad > 0)
        {
            rice->bitbuf <<= pad;
            rice->bitcnt += pad;
        }

        while (rice->bitcnt > 0)
        {
            rice->bitcnt -= 8;
            fputc((int)((rice->bitbuf >> rice->bitcnt) & 0xFF), rice->out);
        }

        rice->bitbuf = 0;
        rice->bitcnt = 0;
    }
}

static void rice_consume(rice_decoder_t *r, int n)
{
    int real;

    if (n <= 0)
    {
        return;
    }

    if (n >= 64)
    {
        r->bitbuf = 0;
        r->bitcnt = 0;
        r->pad_bits = 0;
        return;
    }

    real = r->bitcnt - r->pad_bits;

    if (real < 0)
    {
        real = 0;
    }

    if (n > real)
    {
        r->pad_bits = real + r->pad_bits - n;

        if (r->pad_bits < 0)
        {
            r->pad_bits = 0;
        }
    }

    r->bitbuf <<= n;
    r->bitcnt -= n;
}

static void rice_need(rice_decoder_t *r, int n)
{
    while (r->bitcnt < n)
    {
        if (r->bitcnt > 56)
        {
            r->bitcnt += 8;
            r->pad_bits += 8;

            if (r->bitcnt > 64)
            {
                int over;

                over = r->bitcnt - 64;
                r->bitcnt = 64;
                r->pad_bits -= over;

                if (r->pad_bits < 0)
                {
                    r->pad_bits = 0;
                }
            }

            continue;
        }

        if (r->in_pos >= r->in_len)
        {
            r->in_len = fread(r->in_buf, 1, sizeof r->in_buf, r->in);
            r->in_pos = 0;

            if (r->in_len == 0)
            {
                r->bitcnt += 8;
                r->pad_bits += 8;

                if (r->bitcnt > 64)
                {
                    int over;

                    over = r->bitcnt - 64;
                    r->bitcnt = 64;
                    r->pad_bits -= over;

                    if (r->pad_bits < 0)
                    {
                        r->pad_bits = 0;
                    }
                }

                continue;
            }
        }

        r->bitbuf |= (uint64_t)r->in_buf[r->in_pos] << (56 - r->bitcnt);
        r->in_pos++;
        r->bitcnt += 8;
    }
}

static uint32_t rice_bits(rice_decoder_t *r, int n)
{
    uint32_t v;

    if (n <= 0)
    {
        return 0;
    }

    if (r->bitcnt < n)
    {
        rice_need(r, n);
    }

    v = (uint32_t)(r->bitbuf >> (64 - n));
    rice_consume(r, n);

    return v;
}

static uint32_t rice_unary(rice_decoder_t *r)
{
    uint32_t q;
    int real;

    q = 0;

    for (;;)
    {
        if (r->bitcnt < 8)
        {
            rice_need(r, 8);
        }

        if (r->bitbuf != 0)
        {
            int z;

            z = (int)__builtin_clzll(r->bitbuf);
            real = r->bitcnt - r->pad_bits;

            if (real < 0)
            {
                real = 0;
            }

            if (z < real)
            {
                rice_consume(r, z + 1);
                return q + (uint32_t)z;
            }
        }

        real = r->bitcnt - r->pad_bits;

        if (real < 0)
        {
            real = 0;
        }

        if (real == 0)
        {
            return q;
        }

        q += (uint32_t)real;
        rice_consume(r, real);
    }
}

static void rice_sync_cursor(rice_decoder_t *rice, uint32_t n)
{
    uint32_t parts;
    uint32_t idx;

    rice->cur_idx = n;

    if (rice->part_size == 0)
    {
        return;
    }

    parts = 1u << rice->p;

    if (n <= rice->num_warmup)
    {
        rice->part_i = 0;
        rice->part_lim = rice->num_warmup + rice->part_size;
        return;
    }

    idx = (n - rice->num_warmup) / rice->part_size;

    if (idx >= parts)
    {
        idx = parts - 1;
    }

    if (idx >= MAX_K)
    {
        idx = MAX_K - 1;
    }

    rice->part_i = idx;
    rice->part_lim = rice->num_warmup + (idx + 1) * rice->part_size;
}

static void rice_rewind(rice_decoder_t *rice)
{
    int real_bits;
    int held;
    int give;

    real_bits = rice->bitcnt - rice->pad_bits;

    if (real_bits < 0)
    {
        real_bits = 0;
    }

    held = (real_bits + 7) >> 3;

    if (real_bits == 0 || (real_bits & 7) == 0)
    {
        give = held;
    }
    else
    {
        give = held - 1;
    }

    give += (int)(rice->in_len - rice->in_pos);

    if (give > 0 && rice->in != NULL)
    {
        fseek(rice->in, -give, SEEK_CUR);
    }
}

rice_decoder_t *rice_decoder_create(FILE *in, uint16_t bits_per_sample)
{
    rice_decoder_t *rice;

    if (bits_per_sample > 32 || bits_per_sample == 0)
    {
        return NULL;
    }

    rice = malloc(sizeof(rice_decoder_t));

    if (!rice)
    {
        return NULL;
    }

    memset(rice, 0, sizeof(*rice));
    rice->bps = bits_per_sample;
    rice->in = in;

    return rice;
}

void rice_decoder_destroy(rice_decoder_t *rice)
{
    if (!rice)
    {
        return;
    }

    rice_rewind(rice);
    free(rice);
}

void rice_decoder_set_num_warmup(rice_decoder_t *rice, uint32_t num_warmup)
{
    rice->num_warmup = num_warmup;

    if (rice->part_size > 0)
    {
        rice->part_i = 0;
        rice->part_lim = num_warmup + rice->part_size;
    }
}

int rice_decoder_set_distribution(rice_decoder_t *rice, int distribution_size)
{
    int i;
    int parts;

    rice->bitbuf = 0;
    rice->bitcnt = 0;
    rice->pad_bits = 0;
    rice->cur_idx = 0;
    rice->part_i = 0;
    rice->p = (uint8_t)rice_bits(rice, 5);

    if (rice->p > MAX_P)
    {
        return -1;
    }

    parts = 1 << rice->p;

    for (i = 0; i < parts; i++)
    {
        rice->k[i] = (uint8_t)rice_bits(rice, 4);
    }

    if (distribution_size <= 0)
    {
        rice->part_size = 1;
    }
    else
    {
        rice->part_size = (uint32_t)((distribution_size + parts - 1) / parts);
    }

    if (rice->part_size == 0)
    {
        rice->part_size = 1;
    }

    rice->part_lim = rice->num_warmup + rice->part_size;

    return 0;
}

uint32_t rice_decoder_decode_elem(rice_decoder_t *rice)
{
    uint32_t v;
    uint32_t q;
    uint32_t remv;
    uint32_t parts;
    int kk;

    if (rice->part_size == 0)
    {
        return 0;
    }

    if (rice->cur_idx < rice->num_warmup)
    {
        v = rice_bits(rice, (int)rice->bps);
        rice->cur_idx++;
        return v;
    }

    if (rice->cur_idx >= rice->part_lim)
    {
        parts = 1u << rice->p;

        if (rice->part_i + 1 < parts && rice->part_i + 1 < MAX_K)
        {
            rice->part_i++;
            rice->part_lim += rice->part_size;
        }
        else
        {
            rice->part_lim = 0xffffffffu;
        }
    }

    kk = (int)rice->k[rice->part_i];

    if (kk != VERBATIM)
    {
        q = rice_unary(rice);
        remv = rice_bits(rice, kk);
        v = (q << kk) | remv;
    }
    else
    {
        v = rice_bits(rice, (int)rice->bps);
    }

    rice->cur_idx++;

    return v;
}

void rice_decoder_decode_block(rice_decoder_t *rice, uint32_t *out, int count)
{
    uint32_t parts;
    uint32_t ps;
    uint32_t warm;
    uint32_t end_u;
    uint32_t q;
    uint32_t remv;
    int i;
    int end;
    int pi;
    int kk;
    int bps;
    int z;
    int take;

    if (count <= 0)
    {
        return;
    }

    if (rice->part_size == 0)
    {
        for (i = 0; i < count; i++)
        {
            out[i] = 0;
        }

        return;
    }

    bps = (int)rice->bps;
    parts = 1u << rice->p;
    ps = rice->part_size;

    if (rice->num_warmup > (uint32_t)count)
    {
        warm = (uint32_t)count;
    }
    else
    {
        warm = rice->num_warmup;
    }

    i = 0;

    while (i < (int)warm)
    {
        out[i] = rice_bits(rice, bps);
        i++;
    }

    pi = 0;

    while (i < count && (uint32_t)pi < parts)
    {
        end_u = rice->num_warmup + ((uint32_t)pi + 1u) * ps;

        if (end_u < rice->num_warmup || end_u > (uint32_t)count)
        {
            end_u = (uint32_t)count;
        }

        end = (int)end_u;

        if (end < i)
        {
            end = i;
        }

        kk = (int)rice->k[pi];

        if (kk == VERBATIM)
        {
            while (i < end)
            {
                out[i] = rice_bits(rice, bps);
                i++;
            }
        }
        else
        {
            while (i < end)
            {
                if (rice->bitcnt < 48)
                {
                    rice_need(rice, 48);
                }

                if (rice->pad_bits == 0 && rice->bitbuf != 0)
                {
                    z = (int)__builtin_clzll(rice->bitbuf);
                    take = z + 1 + kk;

                    if (z + 1 < 64 && take <= rice->bitcnt)
                    {
                        rice->bitbuf <<= (z + 1);
                        rice->bitcnt -= z + 1;
                        remv = 0;

                        if (kk > 0)
                        {
                            remv = (uint32_t)(rice->bitbuf >> (64 - kk));
                            rice->bitbuf <<= kk;
                            rice->bitcnt -= kk;
                        }

                        out[i] = ((uint32_t)z << kk) | remv;
                        i++;
                        continue;
                    }
                }

                q = rice_unary(rice);
                remv = rice_bits(rice, kk);
                out[i] = (q << kk) | remv;
                i++;
            }
        }

        pi++;
    }

    if (i < count)
    {
        if (parts == 0)
        {
            kk = 0;
        }
        else
        {
            kk = (int)rice->k[parts - 1];
        }

        if (kk == VERBATIM)
        {
            while (i < count)
            {
                out[i] = rice_bits(rice, bps);
                i++;
            }
        }
        else
        {
            while (i < count)
            {
                q = rice_unary(rice);
                remv = rice_bits(rice, kk);
                out[i] = (q << kk) | remv;
                i++;
            }
        }
    }

    rice_sync_cursor(rice, (uint32_t)count);
}

static uint64_t rice_zz64(int32_t v)
{
    return ((uint64_t)v << 1) ^ (uint64_t)(v >> 31);
}

static uint64_t rice_estimate_one(const int32_t *deltas, uint32_t n)
{
    uint64_t sum, mean, best;
    int k0, k, i;

    if (n == 0) return 0;

    sum = 0;
    for (i = 0; i < n; i++) sum += rice_zz64(deltas[i]);
    mean = sum / n;

    k0 = 0;
    while (((uint64_t)1 << (k0 + 1)) <= mean && k0 < K_RANGE) k0++;

    best = UINT64_MAX;

    for (k = (k0 > 3 ? k0 - 3 : 0); k <= (k0 + 3 < K_RANGE ? k0 + 3 : K_RANGE); k++)
    {
        uint64_t bits = 0;
        for (i = 0; i < n; i++)
        {
            uint64_t zz = rice_zz64(deltas[i]);
            bits += (zz >> k) + 1 + (uint64_t)k;
        }
        if (bits < best) best = bits;
    }

    return best + 4;
}

uint64_t rice_coder_estimate_delta(const int32_t *samples, uint32_t n, int bits_per_sample)
{
    int32_t *deltas;
    uint64_t cost;
    uint32_t i;

    (void)bits_per_sample;

    if (n == 0) return 0;

    deltas = malloc(sizeof(int32_t) * n);
    if (!deltas) return UINT64_MAX;

    for (i = 0; i < n; i++)
    {
        if (i == 0) deltas[i] = samples[i];
        else deltas[i] = samples[i] - samples[i - 1];
    }

    cost = rice_estimate_one(deltas, n);
    free(deltas);

    return cost;
}

void rice_coder_encode_warmup_delta(rice_coder_t *rice, const int32_t *samples, uint32_t n)
{
    int32_t *deltas;
    uint32_t i;
    uint64_t sum, mean;
    int k;
    int j;

    if (n == 0) return;

    deltas = malloc(sizeof(int32_t) * n);
    if (!deltas) return;

    for (i = 0; i < n; i++)
    {
        if (i == 0) deltas[i] = samples[i];
        else deltas[i] = samples[i] - samples[i - 1];
    }

    sum = 0;
    for (i = 0; i < n; i++) sum += rice_zz64(deltas[i]);
    mean = sum / n;

    k = 0;
    while (((uint64_t)1 << (k + 1)) <= mean && k < K_RANGE) k++;
    if (k > K_RANGE) k = K_RANGE;

    rc_put_bits(rice, (uint32_t)k, 4);

    for (i = 0; i < n; i++)
    {
        uint64_t zz = rice_zz64(deltas[i]);
        uint32_t q = (uint32_t)(zz >> k);
        uint32_t r = (uint32_t)(zz & ((1u << k) - 1));

        rc_put_unary_remainder(rice, q, r, k);
    }

    free(deltas);
}

int rice_decoder_decode_warmup_delta(rice_decoder_t *rice, int32_t *out, uint32_t n)
{
    int k;
    int j;
    uint32_t i;

    if (n == 0) return 0;

    k = 0;
    for (j = 0; j < 4; j++)
    {
        uint32_t b = rice_bits(rice, 1);
        k = (k << 1) | (int)b;
    }
    if (k > K_RANGE) return -1;

    for (i = 0; i < n; i++)
    {
        uint32_t q = rice_unary(rice);
        uint32_t r = rice_bits(rice, k);
        uint64_t zz = ((uint64_t)q << k) | r;
        int32_t delta = (int32_t)((zz >> 1) ^ -(int64_t)(zz & 1));

        if (i == 0) out[0] = delta;
        else out[i] = out[i - 1] + delta;
    }

    return 0;
}
