#include "models.h"
#include "notflac_format.h"
#include "range_decoder.h"
#include "rice_coder.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct entropy_decoder entropy_decoder_t;

struct entropy_decoder
{
    FILE *in;
    entropy_method_t method;
    range_decoder_t *range_d;
    slot_model_t sample_m;
    slot_model_t quot_m;
    model_t byte_m;
    prec_model_t precision_m;
    rice_decoder_t *rice_d;
};

entropy_decoder_t *entropy_d_create_range(FILE *in)
{
    entropy_decoder_t *dec = malloc(sizeof(entropy_decoder_t));
    if (!dec)
    {
        return NULL;
    }
    dec->in = in;
    dec->rice_d = NULL;
    dec->range_d = rd_init(in);
    if (!dec->range_d)
    {
        free(dec);
        return NULL;
    }
    dec->method = ENTROPY_RANGE_CODER;
    model_init(&dec->byte_m);
    prec_model_init(&dec->precision_m);
    slot_model_init(&dec->quot_m);
    slot_model_init(&dec->sample_m);
    return dec;
}

entropy_decoder_t *entropy_d_create_rice(FILE *in, uint16_t bits_per_sample)
{
    entropy_decoder_t *dec = malloc(sizeof(entropy_decoder_t));
    if (!dec)
    {
        return NULL;
    }
    dec->in = in;
    dec->range_d = NULL;
    dec->rice_d = rice_decoder_create(in, bits_per_sample);
    if (!dec->rice_d)
    {
        free(dec);
        return NULL;
    }
    dec->method = ENTROPY_RICE_CODER;
    return dec;
}

void entropy_d_destroy(entropy_decoder_t *dec)
{
    if (!dec)
    {
        return;
    }
    if (dec->range_d)
    {
        rd_destroy(dec->range_d);
    }
    if (dec->rice_d)
    {
        rice_decoder_destroy(dec->rice_d);
    }
    free(dec);
}

int entropy_decoder_set_state(entropy_decoder_t *dec, int block_size)
{
    if (dec->method == ENTROPY_RICE_CODER && dec->rice_d)
    {
        return rice_decoder_set_distribution(dec->rice_d, block_size);
    }
    return 0;
}

uint32_t entropy_decode_sample(entropy_decoder_t *dec)
{
    if (dec->method == ENTROPY_RANGE_CODER)
    {
        uint32_t target = rd_target(dec->range_d, dec->sample_m.total);
        int s = slot_model_sym(&dec->sample_m, target);
        rd_update(dec->range_d, slot_model_cum(&dec->sample_m, s), slot_model_freq(&dec->sample_m, s));
        slot_model_update(&dec->sample_m, s);

        int b = base_dist(s);
        int eb = extra_bits(s);
        uint32_t sample = b;
        for (int i = eb - 1; i >= 0; i--)
        {
            int bit = rd_target(dec->range_d, 2);
            sample |= ((uint32_t)bit << i);
            rd_update(dec->range_d, bit, 1);
        }
        return sample;
    }
    else if (dec->method == ENTROPY_RICE_CODER)
    {
        return rice_decoder_decode_elem(dec->rice_d);
    }
    return 0;
}

uint8_t entropy_decode_precision(entropy_decoder_t *dec)
{
    if (dec->method == ENTROPY_RANGE_CODER)
    {
        uint32_t target = rd_target(dec->range_d, dec->precision_m.total);
        int p = prec_model_sym(&dec->precision_m, target);
        rd_update(dec->range_d, prec_model_cum(&dec->precision_m, p), prec_model_freq(&dec->precision_m, p));
        return (uint8_t)p;
    }
    else if (dec->method == ENTROPY_RICE_CODER)
    {
        int c = fgetc(dec->in);
        if (c == EOF)
        {
            return 0;
        }
        return (uint8_t)c;
    }
    return 0;
}

uint8_t entropy_decode_byte(entropy_decoder_t *dec)
{
    if (dec->method == ENTROPY_RANGE_CODER)
    {
        uint32_t target = rd_target(dec->range_d, dec->byte_m.total);
        int b = model_sym(&dec->byte_m, target);
        rd_update(dec->range_d, model_cum(&dec->byte_m, b), model_freq(&dec->byte_m, b));
        model_update(&dec->byte_m, b);
        return (uint8_t)b;
    }
    else if (dec->method == ENTROPY_RICE_CODER)
    {
        int c = fgetc(dec->in);
        if (c == EOF)
        {
            return 0;
        }
        return (uint8_t)c;
    }
    return 0;
}

int32_t entropy_decode_quotient(entropy_decoder_t *dec)
{
    if (dec->method == ENTROPY_RANGE_CODER)
    {
        uint32_t target = rd_target(dec->range_d, dec->quot_m.total);
        int s = slot_model_sym(&dec->quot_m, target);
        rd_update(dec->range_d, slot_model_cum(&dec->quot_m, s), slot_model_freq(&dec->quot_m, s));
        slot_model_update(&dec->quot_m, s);

        int b = base_dist(s);
        int eb = extra_bits(s);
        uint32_t zz = b;
        for (int i = eb - 1; i >= 0; i--)
        {
            int bit = rd_target(dec->range_d, 2);
            zz |= ((uint32_t)bit << i);
            rd_update(dec->range_d, bit, 1);
        }
        return (int32_t)((zz >> 1) ^ -(int32_t)(zz & 1));
    }
    else if (dec->method == ENTROPY_RICE_CODER)
    {
        int32_t q;
        if (fread(&q, 4, 1, dec->in) != 1)
        {
            return 0;
        }
        return q;
    }
    return 0;
}
