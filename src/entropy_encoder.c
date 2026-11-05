#include "models.h"
#include "notflac_format.h"
#include "range_coder.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct entropy_encoder entropy_encoder_t;

struct entropy_encoder
{
    FILE *out;
    entropy_method_t method;
    // Range coder
    range_coder_t *range_c;
    slot_model_t sample_m;
    slot_model_t quot_m;
    model_t byte_m;
    prec_model_t precision_m;
};

static uint32_t zigzag(int32_t n)
{
    return ((uint32_t)n << 1) ^ (uint32_t)(n >> 31);
}

entropy_encoder_t *entropy_create_range(FILE *out)
{
    entropy_encoder_t *enc = malloc(sizeof(entropy_encoder_t));
    if (enc)
    {
        enc->out = out;
        enc->range_c = rc_init(out);
        if (!enc->range_c)
        {
            free(enc);
            enc = NULL;
        }
        else
        {
            enc->method = ENTROPY_RANGE_CODER;
            model_init(&enc->byte_m);
            prec_model_init(&enc->precision_m);
            slot_model_init(&enc->quot_m);
            slot_model_init(&enc->sample_m);
        }
    }
    return enc;
}

void entropy_destroy(entropy_encoder_t *enc)
{
    if (enc)
    {
        if (enc->range_c)
        {
            rc_flush(enc->range_c);
            rc_destroy(enc->range_c);
        }
        free(enc);
    }
}

void entropy_encode_sample(entropy_encoder_t *enc, uint32_t sample) // sample is already zigzag`ed
{
    if (enc->method == ENTROPY_RANGE_CODER)
    {
        int s = slot(sample);
        int b = base_dist(s);
        int eb = extra_bits(s);
        uint32_t offset = sample - b;
        rc_update(enc->range_c, slot_model_cum(&enc->sample_m, s), slot_model_freq(&enc->sample_m, s),
                  enc->sample_m.total);
        slot_model_update(&enc->sample_m, s);
        for (int i = eb - 1; i >= 0; i--)
        {
            int bit = (offset >> i) & 1;
            rc_update(enc->range_c, bit, 1, 2);
        }
    }
}

void entropy_encode_precision(entropy_encoder_t *enc, uint8_t precision)
{
    if (enc->method == ENTROPY_RANGE_CODER)
    {
        if (precision > 15)
        {
            precision = 15;
        }
        rc_update(enc->range_c, prec_model_cum(&enc->precision_m, precision),
                  prec_model_freq(&enc->precision_m, precision), enc->precision_m.total);
    }
}

void entropy_encode_byte(entropy_encoder_t *enc, uint8_t byte)
{
    if (enc->method == ENTROPY_RANGE_CODER)
    {
        rc_update(enc->range_c, model_cum(&enc->byte_m, byte), model_freq(&enc->byte_m, byte), enc->byte_m.total);
        model_update(&enc->byte_m, byte);
    }
}

void entropy_encode_quotient(entropy_encoder_t *enc, int32_t quotient) // quotient is not zigzag`ed
{
    if (enc->method == ENTROPY_RANGE_CODER)
    {
        uint32_t zz = zigzag(quotient);
        int s = slot(zz);
        int b = base_dist(s);
        int eb = extra_bits(s);
        uint32_t offset = zz - b;
        rc_update(enc->range_c, slot_model_cum(&enc->quot_m, s), slot_model_freq(&enc->quot_m, s), enc->quot_m.total);
        slot_model_update(&enc->quot_m, s);
        for (int i = eb - 1; i >= 0; i--)
        {
            int bit = (offset >> i) & 1;
            rc_update(enc->range_c, bit, 1, 2);
        }
    }
}
