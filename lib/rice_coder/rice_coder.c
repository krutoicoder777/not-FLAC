#include "rice_coder.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_P 5
#define MAX_K (1 << MAX_P)
#define K_RANGE 30
#define VERBATIM (K_RANGE + 1)

typedef struct rice_coder rice_coder_t;

struct rice_coder
{
    FILE *out;
    uint8_t p;
    uint8_t k[MAX_K];
    uint32_t cur_idx;
    uint32_t part_size;
    uint16_t bps;
    uint8_t bits;
    uint8_t bits_to_write;
};

rice_coder_t *rice_coder_create(FILE *out, uint16_t bits_per_sample)
{
    rice_coder_t *rice = malloc(sizeof(rice_coder_t));
    if (rice)
    {
        if (bits_per_sample > 32 || bits_per_sample == 0)
        {
            free(rice);
            return NULL;
        }
        rice->bps = bits_per_sample;
        rice->out = out;
        rice->bits = 0;
        rice->bits_to_write = 0;
        rice->cur_idx = 0;
        rice->part_size = 0;
    }
    return rice;
}

void rice_coder_destroy(rice_coder_t *rice)
{
    free(rice);
}

static uint8_t optimal_k(const uint32_t *distribution, int distribution_size, uint64_t *cost)
{
    if (distribution_size == 0)
    {
        return 0;
    }
    uint8_t best_k = 0;
    uint64_t best_bits = UINT64_MAX;
    for (uint8_t k = 0; k <= K_RANGE; k++)
    {
        uint64_t bits = 0;
        for (uint32_t i = 0; i < distribution_size; i++)
        {
            bits += (distribution[i] >> k) + 1 + k;
        }
        if (bits < best_bits)
        {
            best_bits = bits;
            best_k = k;
        }
    }
    *cost = best_bits;
    return best_k;
}

void optimal_params(rice_coder_t *rice, const uint32_t *distribution, int distribution_size)
{
    uint64_t cost, tmp, raw_cost, best_cost = UINT64_MAX;
    uint8_t karr[MAX_K];
    int num_parts, part_size, n, start;
    for (uint8_t p = 0; p <= MAX_P; p++)
    {
        num_parts = 1 << p;
        part_size = (distribution_size + num_parts - 1) / num_parts;
        cost = 8;
        for (int i = 0; i < num_parts; i++)
        {
            start = i * part_size;
            n = (start + part_size > distribution_size) ? (distribution_size - start) : part_size;
            if (n <= 0)
                break;
            raw_cost = (uint64_t)n * rice->bps;
            karr[i] = optimal_k(distribution + start, n, &tmp);
            if (tmp < raw_cost)
            {
                cost += 8 + tmp;
            }
            else
            {
                cost += 8 + raw_cost;
                karr[i] = VERBATIM;
            }
        }
        if (cost < best_cost)
        {
            best_cost = cost;
            rice->p = p;
            memcpy(rice->k, karr, num_parts);
        }
    }

    rice->part_size = (distribution_size + (1 << rice->p) - 1) / (1 << rice->p);
}

int rice_coder_set_distribution(rice_coder_t *rice, const uint32_t *distribution, int distribution_size)
{
    int rc = 0;
    if (rice->bits_to_write != 0)
    {
        rice_coder_flush(rice);
        rc = 1;
    }
    optimal_params(rice, distribution, distribution_size);
    fputc(rice->p, rice->out);
    for (int i = 0; i < (1 << rice->p); i++)
    {
        fputc(rice->k[i], rice->out);
    }
    rice->cur_idx = 0;
    return rc;
}

static void queue_bit(rice_coder_t *rice, uint8_t bit)
{
    if (bit > 1)
    {
        bit = 1;
    }

    rice->bits_to_write++;
    rice->bits <<= 1;
    rice->bits |= bit;

    if (rice->bits_to_write == 8)
    {
        fputc(rice->bits, rice->out);
        rice->bits_to_write = 0;
        rice->bits = 0;
    }
}

void rice_coder_encode_elem(rice_coder_t *rice, uint32_t res)
{
    uint8_t cur_k = rice->cur_idx / rice->part_size;
    if (rice->k[cur_k] != VERBATIM)
    {
        uint32_t q = res >> rice->k[cur_k];
        uint32_t r = res & ((1u << rice->k[cur_k]) - 1);

        for (uint32_t i = 0; i < q; i++)
        {
            queue_bit(rice, 0);
        }
        queue_bit(rice, 1);
        for (int i = rice->k[cur_k] - 1; i >= 0; i--)
        {
            queue_bit(rice, (r >> i) & 1);
        }
    }
    else
    {
        for (int i = 0; i < rice->bps; i++)
        {
            queue_bit(rice, (res >> (rice->bps - i - 1)) & 1);
        }
    }
    rice->cur_idx++;
}

void rice_coder_flush(rice_coder_t *rice)
{
    if (rice->bits_to_write != 0)
    {
        fputc(rice->bits << (8 - rice->bits_to_write), rice->out);
        rice->bits_to_write = 0;
        rice->bits = 0;
    }
}

typedef struct rice_decoder rice_decoder_t;

struct rice_decoder
{
    FILE *in;
    uint8_t k[MAX_K];
    uint8_t p;
    uint32_t cur_idx;
    uint32_t part_size;
    uint8_t bits;
    uint8_t bits_in_buf;
    uint16_t bps;
};

rice_decoder_t *rice_decoder_create(FILE *in, uint16_t bits_per_sample)
{
    rice_decoder_t *rice = malloc(sizeof(rice_decoder_t));
    if (rice)
    {
        if (bits_per_sample > 32 || bits_per_sample == 0)
        {
            free(rice);
            return NULL;
        }
        rice->bps = bits_per_sample;
        rice->in = in;
        rice->bits = 0;
        rice->bits_in_buf = 0;
        rice->cur_idx = 0;
        rice->part_size = 0;
    }
    return rice;
}

void rice_decoder_destroy(rice_decoder_t *rice)
{
    free(rice);
}

int rice_decoder_set_distribution(rice_decoder_t *rice, int distribution_size)
{
    rice->bits = 0;
    rice->bits_in_buf = 0;
    rice->cur_idx = 0;
    rice->p = fgetc(rice->in);
    if (rice->p == EOF)
    {
        return -1;
    }
    for (int i = 0; i < 1 << rice->p; i++)
    {
        rice->k[i] = fgetc(rice->in);
        if (rice->k[i] == EOF)
        {
            return -1;
        }
    }
    rice->part_size = (distribution_size + (1 << rice->p) - 1) / (1 << rice->p);
    return 0;
}

static uint8_t dequeue_bit(rice_decoder_t *rice)
{
    if (rice->bits_in_buf == 0)
    {
        int c = fgetc(rice->in);
        if (c == EOF)
        {
            rice->bits_in_buf = 0;
            rice->bits = 0;
            return 1;
        }
        rice->bits_in_buf = 8;
        rice->bits = (uint8_t)c;
    }
    rice->bits_in_buf--;
    uint8_t bit = (rice->bits >> rice->bits_in_buf) & 1;
    return bit;
}

uint32_t rice_decoder_decode_elem(rice_decoder_t *rice)
{
    uint32_t q = 0;
    uint32_t r = 0;
    uint32_t cur_k = rice->cur_idx / rice->part_size;
    uint32_t result;
    if (rice->k[cur_k] != VERBATIM)
    {
        while (dequeue_bit(rice) != 1)
        {
            q++;
        }
        for (int i = rice->k[cur_k] - 1; i >= 0; i--)
        {
            r |= dequeue_bit(rice) << i;
        }
        result = (q << rice->k[cur_k]) | r;
    }
    else
    {
        result = 0;
        for (int i = 0; i < rice->bps; i++)
        {
            result |= dequeue_bit(rice) << (rice->bps - i - 1);
        }
    }
    rice->cur_idx++;
    return result;
}
