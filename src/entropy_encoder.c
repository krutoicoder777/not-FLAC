#include "entropy_encoder.h"
#include "notflac_format.h"
#include "rice_coder.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct entropy_encoder
{
    FILE *out;
    entropy_method_t method;
    rice_coder_t *rice_c;
};

static uint32_t zigzag(int32_t n)
{
    return ((uint32_t)n << 1) ^ (uint32_t)(n >> 31);
}

entropy_encoder_t *entropy_create_rice(FILE *out, uint16_t bps)
{
    entropy_encoder_t *enc;

    enc = malloc(sizeof(entropy_encoder_t));
    if (!enc)
    {
        return NULL;
    }

    enc->out = out;
    enc->method = ENTROPY_RICE_CODER;
    enc->rice_c = rice_coder_create(out, bps);

    if (!enc->rice_c)
    {
        free(enc);
        return NULL;
    }

    return enc;
}

void entropy_destroy(entropy_encoder_t *enc)
{
    if (!enc)
    {
        return;
    }

    if (enc->rice_c)
    {
        rice_coder_flush(enc->rice_c);
        rice_coder_destroy(enc->rice_c);
    }

    free(enc);
}

void entropy_encode_sample(entropy_encoder_t *enc, uint32_t sample)
{
    rice_coder_encode_elem(enc->rice_c, sample);
}

void entropy_encode_precision(entropy_encoder_t *enc, uint8_t precision)
{
    fputc(precision, enc->out);
}

void entropy_encode_quotients(entropy_encoder_t *enc, const int32_t *quotients, int order)
{
    uint32_t zz[32];
    rice_coder_t *q_coder;
    int j;

    if (order <= 0)
    {
        fputc(0, enc->out);
        return;
    }

    for (j = 0; j < order; j++)
    {
        zz[j] = zigzag(quotients[j]);
    }

    q_coder = rice_coder_create(enc->out, 16);

    if (!q_coder)
    {
        return;
    }

    rice_coder_set_distribution(q_coder, zz, order);

    for (j = 0; j < order; j++)
    {
        rice_coder_encode_elem(q_coder, zz[j]);
    }

    rice_coder_flush(q_coder);
    rice_coder_destroy(q_coder);
}

int entropy_set_state(entropy_encoder_t *enc, const uint32_t *block, int block_size)
{
    return rice_coder_set_distribution(enc->rice_c, block, block_size);
}

void entropy_set_num_warmup(entropy_encoder_t *enc, uint32_t num_warmup)
{
    rice_coder_set_num_warmup(enc->rice_c, num_warmup);
}
