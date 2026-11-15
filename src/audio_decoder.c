#include "audio_decoder.h"
#include "entropy_decoder.h"
#include "errors.h"
#include "stereo.h"
#include "notflac_format.h"
#include "predictor_decoder.h"
#include "rice_coder.h"
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
    int lpc_order_default;
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

static uint32_t zigzag(int32_t n)
{
    return ((uint32_t)n << 1) ^ (uint32_t)(n >> 31);
}

static int read_nf_header(audio_decoder_t *dec)
{
    uint32_t magic;
    uint8_t version;
    uint8_t entropy_byte;
    uint8_t predictor_byte;
    uint8_t reserved;
    uint64_t table_offset;
    uint32_t order;
    uint32_t i;

    if (fread(&magic, 4, 1, dec->in) != 1) return ERR_IO;
    if (magic != NOTFLAC_MAGIC) return ERR_ARGS;

    if (fread(&version, 1, 1, dec->in) != 1) return ERR_IO;
    if (version != NOTFLAC_VERSION) return ERR_ARGS;

    if (fread(&entropy_byte, 1, 1, dec->in) != 1) return ERR_IO;
    if (fread(&predictor_byte, 1, 1, dec->in) != 1) return ERR_IO;
    if (fread(&reserved, 1, 1, dec->in) != 1) return ERR_IO;

    if (fread(&dec->sample_rate, 4, 1, dec->in) != 1) return ERR_IO;
    if (fread(&dec->channels, 2, 1, dec->in) != 1) return ERR_IO;
    if (fread(&dec->bits_per_sample, 2, 1, dec->in) != 1) return ERR_IO;
    if (fread(&dec->num_samples, 4, 1, dec->in) != 1) return ERR_IO;
    if (fread(&dec->block_size, 4, 1, dec->in) != 1) return ERR_IO;
    if (fread(&table_offset, 8, 1, dec->in) != 1) return ERR_IO;

    if (entropy_byte != ENTROPY_RICE_CODER) return ERR_ARGS;
    if (predictor_byte != PREDICTOR_LPC) return ERR_ARGS;

    if (fread(&order, 4, 1, dec->in) != 1) return ERR_IO;
    if (order < 1 || order > 32) return ERR_ARGS;

    dec->lpc_order_default = (int)order;
    dec->entropy = (entropy_method_t)entropy_byte;
    dec->predictor = (predictor_method_t)predictor_byte;
    dec->num_blocks = (dec->num_samples + dec->block_size - 1) / dec->block_size;

    dec->blocks_offset = malloc(sizeof(uint64_t) * (size_t)dec->num_blocks);
    dec->blocks_size = malloc(sizeof(uint32_t) * (size_t)dec->num_blocks);
    if (!dec->blocks_offset || !dec->blocks_size) return ERR_MEM;

    if (fseek(dec->in, (long)table_offset, SEEK_SET) != 0) return ERR_IO;

    for (i = 0; i < dec->num_blocks; i++)
    {
        if (fread(&dec->blocks_offset[i], 8, 1, dec->in) != 1) return ERR_IO;
        if (fread(&dec->blocks_size[i], 4, 1, dec->in) != 1) return ERR_IO;
    }
    return OK;
}

audio_decoder_t *audio_decoder_open(FILE *in)
{
    audio_decoder_t *dec;
    int rc;

    dec = malloc(sizeof(audio_decoder_t));
    if (!dec) return NULL;
    memset(dec, 0, sizeof(audio_decoder_t));
    dec->in = in;

    rc = read_nf_header(dec);
    if (rc != OK) { audio_decoder_close(dec); return NULL; }
    if (audio_decoder_seek_block(dec, 0) != OK) { audio_decoder_close(dec); return NULL; }
    return dec;
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

int audio_decoder_advance(audio_decoder_t *dec)
{
    if (dec->cur_block_index + 1 >= dec->num_blocks) return ERR_IO;
    return audio_decoder_seek_block(dec, dec->cur_block_index + 1);
}

static int read_warmup_raw(FILE *in, int32_t *out, int n, int bps)
{
    int i;

    if (bps != 8 && bps != 16 && bps != 24 && bps != 32) return ERR_ARGS;

    for (i = 0; i < n; i++)
    {
        if (bps == 8)
        {
            int8_t s;
            if (fread(&s, 1, 1, in) != 1) return ERR_IO;
            out[i] = s;
        }
        else if (bps == 16)
        {
            int16_t s;
            if (fread(&s, 2, 1, in) != 1) return ERR_IO;
            out[i] = s;
        }
        else if (bps == 24)
        {
            uint8_t buf[3];
            int32_t v;
            if (fread(buf, 3, 1, in) != 1) return ERR_IO;
            v = (int32_t)buf[0] | ((int32_t)buf[1] << 8) | ((int32_t)buf[2] << 16);
            if (v & 0x800000) v |= (int32_t)0xFF000000;
            out[i] = v;
        }
        else
        {
            int32_t s;
            if (fread(&s, 4, 1, in) != 1) return ERR_IO;
            out[i] = s;
        }
    }
    return OK;
}

static int decode_one_stream(FILE *in, int bits_per_sample, int lpc_order_default,
                             uint32_t n, int32_t *out)
{
    entropy_decoder_t *ed;
    predictor_decoder_t *pd;
    int pred_type;
    int order;
    int32_t quotients[LPC_ORDER_MAX];
    uint8_t precision;
    uint32_t *residuals;
    const int32_t *samples;
    uint32_t i;
    int bps_for_verbatim;
    int rc;

    pred_type = fgetc(in);
    if (pred_type == EOF) return ERR_IO;

    order = lpc_order_default;
    if (pred_type == 0)
    {
        int o = fgetc(in);
        if (o == EOF) return ERR_IO;
        order = o;
    }

    {
        int v = bits_per_sample + 5;
        if (v > 32) v = 32;
        bps_for_verbatim = v;
    }
    ed = entropy_d_create_rice(in, (uint16_t)bps_for_verbatim);
    if (!ed) return ERR_MEM;

    precision = 0;

    if (pred_type == 0)
    {
        int32_t *warmup_samples;
        int warmup_mode;
        int warmup_count;
        int residuals_count;

        precision = entropy_decode_precision(ed);
        if (entropy_decode_quotients(ed, quotients, order) != 0)
        {
            entropy_d_destroy(ed);
            return ERR_IO;
        }

        warmup_mode = fgetc(in);
        if (warmup_mode == EOF) { entropy_d_destroy(ed); return ERR_IO; }
        if (warmup_mode < 0 || warmup_mode > 1) { entropy_d_destroy(ed); return ERR_IO; }

        warmup_count = order;
        if (warmup_count > (int)n) warmup_count = (int)n;

        residuals_count = (int)n - order;
        if (residuals_count < 0) residuals_count = 0;

        warmup_samples = malloc(sizeof(int32_t) * (size_t)warmup_count);
        if (!warmup_samples) { entropy_d_destroy(ed); return ERR_MEM; }

        if (warmup_mode == 1)
        {
            rice_decoder_t *wrice = rice_decoder_create(in, (uint16_t)bits_per_sample);
            if (!wrice) { free(warmup_samples); entropy_d_destroy(ed); return ERR_MEM; }
            if (rice_decoder_decode_warmup_delta(wrice, warmup_samples, (uint32_t)warmup_count) != 0)
            {
                free(warmup_samples); rice_decoder_destroy(wrice); entropy_d_destroy(ed);
                return ERR_IO;
            }
            rice_decoder_destroy(wrice);
        }
        else
        {
            rc = read_warmup_raw(in, warmup_samples, warmup_count, bits_per_sample);
            if (rc != OK) { free(warmup_samples); entropy_d_destroy(ed); return rc; }
        }

        pd = predictor_decoder_create_lpc(order, (int)n);
        if (!pd) { free(warmup_samples); entropy_d_destroy(ed); return ERR_MEM; }
        predictor_decoder_set_quotients(pd, quotients, precision);

        rc = entropy_decoder_set_state(ed, residuals_count);
        if (rc != 0) { free(warmup_samples); predictor_decoder_destroy(pd); entropy_d_destroy(ed); return ERR_IO; }

        entropy_decoder_set_num_warmup(ed, 0);

        residuals = malloc(sizeof(uint32_t) * (size_t)n);
        if (!residuals) { free(warmup_samples); predictor_decoder_destroy(pd); entropy_d_destroy(ed); return ERR_MEM; }

        for (i = (uint32_t)order; i < n; i++)
            residuals[i] = entropy_decode_sample(ed);

        for (i = 0; i < (uint32_t)warmup_count; i++)
            residuals[i] = zigzag(warmup_samples[i]);

        for (i = (uint32_t)warmup_count; i < (uint32_t)order && i < n; i++)
            residuals[i] = 0;

        free(warmup_samples);
    }
    else if (pred_type >= 1 && pred_type <= 7)
    {
        pd = predictor_decoder_create_fixed(pred_type - 1, (int)n);
        if (!pd) { entropy_d_destroy(ed); return ERR_MEM; }

        rc = entropy_decoder_set_state(ed, (int)n);
        if (rc != 0) { predictor_decoder_destroy(pd); entropy_d_destroy(ed); return ERR_IO; }

        entropy_decoder_set_num_warmup(ed, 0);

        residuals = malloc(sizeof(uint32_t) * (size_t)n);
        if (!residuals) { predictor_decoder_destroy(pd); entropy_d_destroy(ed); return ERR_MEM; }

        for (i = 0; i < n; i++)
            residuals[i] = entropy_decode_sample(ed);
    }
    else
    {
        entropy_d_destroy(ed);
        return ERR_IO;
    }

    samples = predictor_decoder_decode_block(pd, residuals, (int)n);
    if (!samples)
    {
        free(residuals); predictor_decoder_destroy(pd); entropy_d_destroy(ed);
        return ERR_IO;
    }

    memcpy(out, samples, sizeof(int32_t) * (size_t)n);

    free(residuals);
    predictor_decoder_destroy(pd);
    entropy_d_destroy(ed);
    return OK;
}

static int decode_stereo_block(audio_decoder_t *dec, uint32_t n_total, int *out_samples)
{
    stereo_mode_t mode;
    uint32_t n;
    int32_t *prim_buf, *sec_buf;
    int bps_prim, bps_sec;
    int rc1, rc2;
    int flag;

    flag = fgetc(dec->in);
    if (flag == EOF) return ERR_IO;
    if (flag < 0 || flag > 3) return ERR_IO;

    mode = (stereo_mode_t)flag;
    n = n_total / 2;
    prim_buf = malloc(sizeof(int32_t) * (size_t)n);
    sec_buf = malloc(sizeof(int32_t) * (size_t)n);
    if (!prim_buf || !sec_buf) { free(prim_buf); free(sec_buf); return ERR_MEM; }

    bps_prim = dec->bits_per_sample;
    bps_sec = (mode != STEREO_MODE_INDEPENDENT) ? (dec->bits_per_sample + 1) : dec->bits_per_sample;

    rc1 = decode_one_stream(dec->in, bps_prim, dec->lpc_order_default, n, prim_buf);
    if (rc1 != OK) { free(prim_buf); free(sec_buf); return rc1; }

    rc2 = decode_one_stream(dec->in, bps_sec, dec->lpc_order_default, n, sec_buf);
    if (rc2 != OK) { free(prim_buf); free(sec_buf); return rc2; }

    stereo_decode(mode, prim_buf, sec_buf, dec->scratch, (int)n);

    free(prim_buf); free(sec_buf);
    *out_samples = (int)n_total;
    return OK;
}

static int decode_mono_block(audio_decoder_t *dec, uint32_t n_total, int *out_samples)
{
    int flag = fgetc(dec->in);
    int rc;
    if (flag == EOF) return ERR_IO;
    rc = decode_one_stream(dec->in, dec->bits_per_sample, dec->lpc_order_default, n_total, dec->scratch);
    if (rc != OK) return rc;
    *out_samples = (int)n_total;
    return OK;
}

const int32_t *audio_decoder_decode_block(audio_decoder_t *dec, int *cur_samples)
{
    uint32_t n_total;
    int32_t *p;
    int rc;
    int n;

    if (dec->cur_samples != NULL)
    {
        *cur_samples = (int)dec->cur_block_samples;
        return dec->cur_samples;
    }

    n_total = dec->cur_block_samples;
    if (n_total == 0) { *cur_samples = 0; return NULL; }

    if (dec->scratch_capacity < n_total)
    {
        p = realloc(dec->scratch, sizeof(int32_t) * (size_t)n_total);
        if (!p) return NULL;
        dec->scratch = p;
        dec->scratch_capacity = n_total;
    }

    if (dec->channels == 2) rc = decode_stereo_block(dec, n_total, &n);
    else                    rc = decode_mono_block(dec, n_total, &n);

    if (rc != OK) return NULL;

    dec->cur_samples = dec->scratch;
    dec->cur_sample_idx = 0;
    *cur_samples = n;
    return dec->cur_samples;
}

int audio_decode_next(audio_decoder_t *dec, int32_t *sample)
{
    int n;
    if (dec->cur_samples == NULL)
        if (audio_decoder_decode_block(dec, &n) == NULL) return -1;

    if (dec->cur_sample_idx >= dec->cur_block_samples)
    {
        if (audio_decoder_advance(dec) != OK) return -1;
        if (audio_decoder_decode_block(dec, &n) == NULL) return -1;
    }

    *sample = dec->cur_samples[dec->cur_sample_idx];
    dec->cur_sample_idx++;
    return 0;
}
