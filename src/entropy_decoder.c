#include "models.h"
#include "notflac_format.h"
#include "range_decoder.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct entropy_decoder entropy_decoder_t;

struct entropy_decoder
{
    FILE *in;
    entropy_method_t method;
    // Range coder
    range_decoder_t *range_d;
    slot_model_t sample_m;
    slot_model_t quot_m;
    model_t byte_m;
    prec_model_t precision_m;
};

entropy_decoder_t *entropy_d_create_range(FILE *in)
{
    entropy_decoder_t *dec = malloc(sizeof(entropy_decoder_t));
    if (dec)
    {
        dec->in = in;
        dec->range_d = rd_init(in);
        if (!dec->range_d)
        {
            free(dec);
            dec = NULL;
        }
        else
        {
            dec->method = ENTROPY_RANGE_CODER;
            model_init(&dec->byte_m);
            prec_model_init(&dec->precision_m);
            slot_model_init(&dec->quot_m);
            slot_model_init(&dec->sample_m);
        }
    }
    return dec;
}

void entropy_d_destroy(entropy_decoder_t *dec)
{
    if (dec)
    {
        if (dec->range_d)
        {
            rd_destroy(dec->range_d);
        }
        free(dec);
    }
}

uint32_t entropy_decode_sample(entropy_decoder_t *dec) // returned sample is zigzag`ed
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
    else
    {
        return 0;
    }
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
    else
    {
        return 0;
    }
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
    else
    {
        return 0;
    }
}

int32_t entropy_decode_quotient(entropy_decoder_t *dec) // returned quotient is not zigzag`ed
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
    else
    {
        return 0;
    }
}
