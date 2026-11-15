#include "entropy_decoder.h"
#include "notflac_format.h"
#include "rice_coder.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct entropy_decoder
{
    FILE *in;
    entropy_method_t method;
    rice_decoder_t *rice_d;
};

entropy_decoder_t *entropy_d_create_rice(FILE *in, uint16_t bps)
{
    entropy_decoder_t *dec;

    dec = malloc(sizeof(entropy_decoder_t));
    if (!dec)
    {
        return NULL;
    }

    dec->in = in;
    dec->rice_d = rice_decoder_create(in, bps);

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

    if (dec->rice_d)
    {
        rice_decoder_destroy(dec->rice_d);
    }

    free(dec);
}

int entropy_decoder_set_state(entropy_decoder_t *dec, int block_size)
{
    return rice_decoder_set_distribution(dec->rice_d, block_size);
}

void entropy_decoder_set_num_warmup(entropy_decoder_t *dec, uint32_t num_warmup)
{
    rice_decoder_set_num_warmup(dec->rice_d, num_warmup);
}

uint32_t entropy_decode_sample(entropy_decoder_t *dec)
{
    return rice_decoder_decode_elem(dec->rice_d);
}

void entropy_decode_block(entropy_decoder_t *dec, uint32_t *out, int count)
{
    rice_decoder_decode_block(dec->rice_d, out, count);
}

uint8_t entropy_decode_precision(entropy_decoder_t *dec)
{
    int c;

    c = fgetc(dec->in);

    if (c == EOF)
    {
        return 0;
    }

    return (uint8_t)c;
}

int entropy_decode_quotients(entropy_decoder_t *dec, int32_t *quotients, int order)
{
    rice_decoder_t *q_dec;
    int j;

    if (order <= 0)
    {
        return 0;
    }

    q_dec = rice_decoder_create(dec->in, 16);

    if (!q_dec)
    {
        return -1;
    }

    if (rice_decoder_set_distribution(q_dec, order) != 0)
    {
        rice_decoder_destroy(q_dec);
        return -1;
    }

    for (j = 0; j < order; j++)
    {
        uint32_t zz;

        zz = rice_decoder_decode_elem(q_dec);
        quotients[j] = (int32_t)((zz >> 1) ^ -(int32_t)(zz & 1));
    }

    rice_decoder_destroy(q_dec);

    return 0;
}
