#include "audio_decoder.h"
#include "entropy_decoder.h"
#include "errors.h"
#include "notflac_format.h"
#include "predictor_decoder.h"
#include "wav_stream.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct audio_decoder
{
    FILE *in;

    uint32_t num_samples;
    uint16_t bits_per_sample;
    uint32_t sample_rate;
    uint16_t channels;
    uint32_t num_blocks;
    uint32_t block_size;

    predictor_method_t predictor;
    entropy_method_t entropy;

    uint64_t *blocks_offset;
    uint32_t *blocks_size;

    predictor_decoder_t *pd;
    entropy_decoder_t *ed;

    uint32_t cur_block_index;
    uint32_t cur_block_samples;
    uint32_t cur_sample_idx;
    const int32_t *cur_samples;
};

audio_decoder_t *audio_decoder_open(FILE *in)
{
    audio_decoder_t *dec = malloc(sizeof(audio_decoder_t));
    if (!dec)
        return NULL;
    memset(dec, 0, sizeof(audio_decoder_t));
    dec->in = in;

    uint32_t magic;
    uint8_t version, entropy, predictor, reserved;
    if (fread(&magic, 4, 1, in) != 1)
        goto fail;
    if (magic != NOTFLAC_MAGIC)
        goto fail;
    if (fread(&version, 1, 1, in) != 1)
        goto fail;
    if (version != NOTFLAC_VERSION)
        goto fail;
    if (fread(&entropy, 1, 1, in) != 1)
        goto fail;
    if (fread(&predictor, 1, 1, in) != 1)
        goto fail;
    if (fread(&reserved, 1, 1, in) != 1)
        goto fail;
    if (fread(&dec->sample_rate, 4, 1, in) != 1)
        goto fail;
    if (fread(&dec->channels, 2, 1, in) != 1)
        goto fail;
    if (fread(&dec->bits_per_sample, 2, 1, in) != 1)
        goto fail;
    if (fread(&dec->num_samples, 4, 1, in) != 1)
        goto fail;
    if (fread(&dec->block_size, 4, 1, in) != 1)
        goto fail;

    uint64_t table_offset;
    if (fread(&table_offset, 8, 1, in) != 1)
        goto fail;

    uint32_t order = 0;
    if (predictor == PREDICTOR_LPC)
    {
        if (fread(&order, 4, 1, in) != 1)
            goto fail;
        if (order != LPC_ORDER)
            goto fail;
    }

    dec->entropy = (entropy_method_t)entropy;
    dec->predictor = (predictor_method_t)predictor;
    dec->num_blocks = (dec->num_samples + dec->block_size - 1) / dec->block_size;

    dec->blocks_offset = malloc(sizeof(uint64_t) * dec->num_blocks);
    dec->blocks_size = malloc(sizeof(uint32_t) * dec->num_blocks);
    if (!dec->blocks_offset || !dec->blocks_size)
        goto fail;

    if (fseek(in, table_offset, SEEK_SET) != 0)
        goto fail;
    for (uint32_t i = 0; i < dec->num_blocks; i++)
    {
        if (fread(&dec->blocks_offset[i], 8, 1, in) != 1)
            goto fail;
        if (fread(&dec->blocks_size[i], 4, 1, in) != 1)
            goto fail;
    }

    dec->cur_block_index = 0;
    dec->cur_block_samples = 0;
    dec->cur_sample_idx = 0;
    dec->cur_samples = NULL;
    dec->pd = NULL;
    dec->ed = NULL;

    if (audio_decoder_seek_block(dec, 0) != OK)
        goto fail;
    return dec;

fail:
    audio_decoder_close(dec);
    return NULL;
}

void audio_decoder_close(audio_decoder_t *dec)
{
    if (!dec)
        return;
    if (dec->pd)
        predictor_decoder_destroy(dec->pd);
    if (dec->ed)
        entropy_d_destroy(dec->ed);
    free(dec->blocks_offset);
    free(dec->blocks_size);
    free(dec);
}

uint32_t audio_decoder_num_samples(audio_decoder_t *dec)
{
    return dec->num_samples;
}
uint16_t audio_decoder_bits_per_sample(audio_decoder_t *dec)
{
    return dec->bits_per_sample;
}
uint32_t audio_decoder_sample_rate(audio_decoder_t *dec)
{
    return dec->sample_rate;
}
uint16_t audio_decoder_channels(audio_decoder_t *dec)
{
    return dec->channels;
}
uint32_t audio_decoder_num_blocks(audio_decoder_t *dec)
{
    return dec->num_blocks;
}
uint32_t audio_decoder_block_size(audio_decoder_t *dec)
{
    return dec->block_size;
}

int audio_decoder_seek_block(audio_decoder_t *dec, uint32_t block_index)
{
    if (block_index >= dec->num_blocks)
        return ERR_ARGS;
    if (fseek(dec->in, dec->blocks_offset[block_index], SEEK_SET) != 0)
        return ERR_IO;

    if (dec->pd)
    {
        predictor_decoder_destroy(dec->pd);
        dec->pd = NULL;
    }
    if (dec->ed)
    {
        entropy_d_destroy(dec->ed);
        dec->ed = NULL;
    }

    uint32_t actual_samples = dec->blocks_size[block_index];
    if (dec->predictor == PREDICTOR_LPC)
    {
        dec->pd = predictor_decoder_create_lpc(LPC_ORDER, dec->block_size);
    }
    else
    {
        dec->pd = predictor_decoder_create_delta(dec->bits_per_sample, dec->block_size);
    }
    if (!dec->pd)
        return ERR_MEM;

    if (dec->entropy == ENTROPY_RANGE_CODER)
    {
        dec->ed = entropy_d_create_range(dec->in);
    }
    if (!dec->ed)
        return ERR_MEM;

    dec->cur_block_index = block_index;
    dec->cur_block_samples = actual_samples;
    dec->cur_sample_idx = 0;
    dec->cur_samples = NULL;
    return OK;
}

const int32_t *audio_decoder_decode_block(audio_decoder_t *dec, int *cur_samples)
{
    if (dec->cur_samples != NULL)
    {
        *cur_samples = dec->cur_block_samples;
        return dec->cur_samples;
    }

    if (dec->predictor == PREDICTOR_LPC)
    {
        int32_t quotients[LPC_ORDER];
        uint8_t precision = entropy_decode_precision(dec->ed);
        for (int j = 0; j < LPC_ORDER; j++)
        {
            quotients[j] = entropy_decode_quotient(dec->ed);
        }
        predictor_decoder_set_quotients(dec->pd, quotients, precision);
    }

    uint32_t *residuals = malloc(sizeof(uint32_t) * dec->cur_block_samples);
    if (!residuals)
        return NULL;
    for (uint32_t i = 0; i < dec->cur_block_samples; i++)
    {
        residuals[i] = entropy_decode_sample(dec->ed);
    }

    dec->cur_samples = predictor_decoder_decode_block(dec->pd, residuals, dec->cur_block_samples);
    free(residuals);

    *cur_samples = dec->cur_block_samples;
    return dec->cur_samples;
}

int audio_decode_next(audio_decoder_t *dec, int32_t *sample)
{
    if (dec->cur_samples == NULL)
    {
        int n;
        if (audio_decoder_decode_block(dec, &n) == NULL)
            return -1;
    }
    if (dec->cur_sample_idx >= dec->cur_block_samples)
    {
        if (dec->cur_block_index + 1 >= dec->num_blocks)
        {
            return -1;
        }
        if (audio_decoder_seek_block(dec, dec->cur_block_index + 1) != OK)
        {
            return -1;
        }
        int n;
        if (audio_decoder_decode_block(dec, &n) == NULL)
            return -1;
    }
    *sample = dec->cur_samples[dec->cur_sample_idx];
    dec->cur_sample_idx++;
    return 0;
}
