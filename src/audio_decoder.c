#include "audio_decoder.h"
#include "entropy_decoder.h"
#include "errors.h"
#include "stereo.h"
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
    int lpc_order;
    predictor_method_t predictor;
    entropy_method_t entropy;
    uint64_t *blocks_offset;
    uint32_t *blocks_size;
    uint32_t cur_block_index;
    uint32_t cur_block_samples;
    uint32_t cur_sample_idx;
    int32_t *cur_samples;
    int32_t *scratch;
    uint32_t scratch_capacity;
};

audio_decoder_t *audio_decoder_open(FILE *in)
{
    audio_decoder_t *dec = malloc(sizeof(audio_decoder_t));
    if (!dec) return NULL;
    memset(dec, 0, sizeof(audio_decoder_t));
    dec->in = in;

    uint32_t magic;
    uint8_t version, entropy, predictor, reserved;
    if (fread(&magic, 4, 1, in) != 1) goto fail;
    if (magic != NOTFLAC_MAGIC) goto fail;
    if (fread(&version, 1, 1, in) != 1) goto fail;
    if (version != NOTFLAC_VERSION) goto fail;
    if (fread(&entropy, 1, 1, in) != 1) goto fail;
    if (fread(&predictor, 1, 1, in) != 1) goto fail;
    if (fread(&reserved, 1, 1, in) != 1) goto fail;
    if (fread(&dec->sample_rate, 4, 1, in) != 1) goto fail;
    if (fread(&dec->channels, 2, 1, in) != 1) goto fail;
    if (fread(&dec->bits_per_sample, 2, 1, in) != 1) goto fail;
    if (fread(&dec->num_samples, 4, 1, in) != 1) goto fail;
    if (fread(&dec->block_size, 4, 1, in) != 1) goto fail;

    uint64_t table_offset;
    if (fread(&table_offset, 8, 1, in) != 1) goto fail;

    uint32_t order = 0;
    if (predictor == PREDICTOR_LPC)
    {
        if (fread(&order, 4, 1, in) != 1) goto fail;
        if (order != 8 && order != 12 && order != 16) goto fail;
        dec->lpc_order = (int)order;
    }

    dec->entropy = (entropy_method_t)entropy;
    dec->predictor = (predictor_method_t)predictor;
    dec->num_blocks = (dec->num_samples + dec->block_size - 1) / dec->block_size;

    dec->blocks_offset = malloc(sizeof(uint64_t) * dec->num_blocks);
    dec->blocks_size = malloc(sizeof(uint32_t) * dec->num_blocks);
    if (!dec->blocks_offset || !dec->blocks_size) goto fail;

    if (fseek(in, (long)table_offset, SEEK_SET) != 0) goto fail;
    for (uint32_t i = 0; i < dec->num_blocks; i++)
    {
        if (fread(&dec->blocks_offset[i], 8, 1, in) != 1) goto fail;
        if (fread(&dec->blocks_size[i], 4, 1, in) != 1) goto fail;
    }

    if (audio_decoder_seek_block(dec, 0) != OK) goto fail;
    return dec;

fail:
    audio_decoder_close(dec);
    return NULL;
}

void audio_decoder_close(audio_decoder_t *dec)
{
    if (!dec) return;
    free(dec->blocks_offset);
    free(dec->blocks_size);
    free(dec->scratch);
    free(dec);
}

uint32_t audio_decoder_num_samples(audio_decoder_t *dec) { return dec->num_samples; }
uint16_t audio_decoder_bits_per_sample(audio_decoder_t *dec) { return dec->bits_per_sample; }
uint32_t audio_decoder_sample_rate(audio_decoder_t *dec) { return dec->sample_rate; }
uint16_t audio_decoder_channels(audio_decoder_t *dec) { return dec->channels; }
uint32_t audio_decoder_num_blocks(audio_decoder_t *dec) { return dec->num_blocks; }
uint32_t audio_decoder_block_size(audio_decoder_t *dec) { return dec->block_size; }

int audio_decoder_seek_block(audio_decoder_t *dec, uint32_t block_index)
{
    if (block_index >= dec->num_blocks) return ERR_ARGS;
    if (fseek(dec->in, (long)dec->blocks_offset[block_index], SEEK_SET) != 0) return ERR_IO;
    dec->cur_block_index = block_index;
    dec->cur_block_samples = dec->blocks_size[block_index];
    dec->cur_sample_idx = 0;
    dec->cur_samples = NULL;
    return OK;
}

static int decode_one_stream(FILE *in, predictor_method_t predictor, entropy_method_t entropy,
                             int bits_per_sample, int order,
                             uint32_t n, int32_t *out)
{
    entropy_decoder_t *ed = NULL;
    predictor_decoder_t *pd = NULL;

    if (entropy == ENTROPY_RICE_CODER)
        ed = entropy_d_create_rice(in, bits_per_sample);
    else if (entropy == ENTROPY_RANGE_CODER)
        ed = entropy_d_create_range(in);
	else
		return ERR_ARGS;
    if (!ed) return ERR_MEM;

    if (entropy == ENTROPY_RICE_CODER)
    {
        int rc = entropy_decoder_set_state(ed, (int)n);
        if (rc != 0)
        {
            entropy_d_destroy(ed);
            return ERR_IO;
        }
    }

    int32_t quotients[LPC_ORDER_MAX];
    uint8_t precision = 0;
    if (predictor == PREDICTOR_LPC)
    {
        precision = entropy_decode_precision(ed);
        for (int j = 0; j < order; j++)
            quotients[j] = entropy_decode_quotient(ed);
    }

    uint32_t *residuals = malloc(sizeof(uint32_t) * n);
    if (!residuals)
    {
        entropy_d_destroy(ed);
        return ERR_MEM;
    }
    for (uint32_t i = 0; i < n; i++)
        residuals[i] = entropy_decode_sample(ed);

    if (predictor == PREDICTOR_LPC)
        pd = predictor_decoder_create_lpc(order, (int)n);
    else
        pd = predictor_decoder_create_delta(bits_per_sample, (int)n);
    if (!pd)
    {
        free(residuals);
        entropy_d_destroy(ed);
        return ERR_MEM;
    }

    if (predictor == PREDICTOR_LPC)
        predictor_decoder_set_quotients(pd, quotients, precision);

    const int32_t *samples = predictor_decoder_decode_block(pd, residuals, (int)n);
    if (!samples)
    {
        free(residuals);
        predictor_decoder_destroy(pd);
        entropy_d_destroy(ed);
        return ERR_IO;
    }
    memcpy(out, samples, sizeof(int32_t) * n);

    free(residuals);
    predictor_decoder_destroy(pd);
    entropy_d_destroy(ed);
    return OK;
}

const int32_t *audio_decoder_decode_block(audio_decoder_t *dec, int *cur_samples)
{
    if (dec->cur_samples != NULL)
    {
        *cur_samples = (int)dec->cur_block_samples;
        return dec->cur_samples;
    }

    uint32_t n_total = dec->cur_block_samples;
    if (n_total == 0)
    {
        *cur_samples = 0;
        return NULL;
    }

    if (dec->scratch_capacity < n_total)
    {
        int32_t *p = realloc(dec->scratch, sizeof(int32_t) * n_total);
        if (!p) return NULL;
        dec->scratch = p;
        dec->scratch_capacity = n_total;
    }

    int flag = fgetc(dec->in);
    if (flag == EOF) return NULL;

    if (dec->channels == 2)
    {
        if (flag < 0 || flag > 3) return NULL;
        stereo_mode_t mode = (stereo_mode_t)flag;
        uint32_t n = n_total / 2;
        int32_t *prim_buf = malloc(sizeof(int32_t) * n);
        int32_t *sec_buf = malloc(sizeof(int32_t) * n);
        if (!prim_buf || !sec_buf)
        {
            free(prim_buf);
            free(sec_buf);
            return NULL;
        }
        int rc1 = decode_one_stream(dec->in, dec->predictor, dec->entropy,
                                    dec->bits_per_sample, dec->lpc_order, n, prim_buf);
        if (rc1 != OK)
        {
            free(prim_buf);
            free(sec_buf);
            return NULL;
        }
        int rc2 = decode_one_stream(dec->in, dec->predictor, dec->entropy,
                                    dec->bits_per_sample, dec->lpc_order, n, sec_buf);
        if (rc2 != OK)
        {
            free(prim_buf);
            free(sec_buf);
            return NULL;
        }
        stereo_decode(mode, prim_buf, sec_buf, dec->scratch, (int)n);
        free(prim_buf);
        free(sec_buf);
    }
    else
    {
        int rc1 = decode_one_stream(dec->in, dec->predictor, dec->entropy,
                                    dec->bits_per_sample, dec->lpc_order, n_total, dec->scratch);
        if (rc1 != OK) return NULL;
    }

    dec->cur_samples = dec->scratch;
    dec->cur_sample_idx = 0;
    *cur_samples = (int)n_total;
    return dec->cur_samples;
}

int audio_decode_next(audio_decoder_t *dec, int32_t *sample)
{
    if (dec->cur_samples == NULL)
    {
        int n;
        if (audio_decoder_decode_block(dec, &n) == NULL) return -1;
    }
    if (dec->cur_sample_idx >= dec->cur_block_samples)
    {
        if (dec->cur_block_index + 1 >= dec->num_blocks) return -1;
        if (audio_decoder_seek_block(dec, dec->cur_block_index + 1) != OK) return -1;
        int n;
        if (audio_decoder_decode_block(dec, &n) == NULL) return -1;
    }
    *sample = dec->cur_samples[dec->cur_sample_idx];
    dec->cur_sample_idx++;
    return 0;
}
