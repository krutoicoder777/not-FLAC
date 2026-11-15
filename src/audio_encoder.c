#include "audio_encoder.h"
#include "entropy_encoder.h"
#include "errors.h"
#include "stereo.h"
#include "notflac_format.h"
#include "predictor_encoder.h"
#include "rice_coder.h"
#include "simple_predictors.h"
#include "wav_block.h"
#include "wav_stream.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int pick_order(uint32_t sample_rate, uint16_t bits, uint16_t channels)
{
    (void)sample_rate;
    if (bits >= 24) return 28;
    if (channels == 1) return 24;
    return 24;
}

static int pick_block_size(uint32_t sample_rate, uint16_t channels)
{
    if (sample_rate <= 48000) return 4096 * channels;
    return 8192 * channels;
}

static uint64_t fixed2_cost_estimate(const int32_t *samples, uint32_t n)
{
    uint64_t cost = 0;
    uint32_t i;
    for (i = 2; i < n; i++)
    {
        int32_t hat = 2 * samples[i - 1] - samples[i - 2];
        int32_t res = samples[i] - hat;
        uint32_t zz = ((uint32_t)res << 1) ^ (uint32_t)(res >> 31);
        cost += zz;
    }
    return cost;
}

static void split_stereo(const int32_t *block, uint32_t n,
                         int32_t *L, int32_t *R, int32_t *M, int32_t *S)
{
    uint32_t i;
    for (i = 0; i < n; i++)
    {
        L[i] = block[2 * i];
        R[i] = block[2 * i + 1];
        M[i] = (L[i] + R[i]) >> 1;
        S[i] = L[i] - R[i];
    }
}

static stereo_mode_t choose_stereo_mode(const int32_t *L, const int32_t *R,
                                        const int32_t *M, const int32_t *S,
                                        uint32_t n)
{
    uint64_t cL = fixed2_cost_estimate(L, n);
    uint64_t cR = fixed2_cost_estimate(R, n);
    uint64_t cM = fixed2_cost_estimate(M, n);
    uint64_t cS = fixed2_cost_estimate(S, n);
    uint64_t cI = cL + cR;
    uint64_t cMS = cM + cS;
    uint64_t cLS = cL + cS;
    uint64_t cRS = cR + cS;
    stereo_mode_t mode = STEREO_MODE_INDEPENDENT;
    uint64_t best = cI;

    if (cLS < best) { best = cLS; mode = STEREO_MODE_LEFT_SIDE; }
    if (cRS < best) { best = cRS; mode = STEREO_MODE_RIGHT_SIDE; }
    if (cMS < best) { mode = STEREO_MODE_MID_SIDE; }
    return mode;
}

static void select_streams(stereo_mode_t mode,
                           const int32_t *L, const int32_t *R,
                           const int32_t *M, const int32_t *S,
                           const int32_t **out_prim, const int32_t **out_sec)
{
    switch (mode)
    {
        case STEREO_MODE_INDEPENDENT: *out_prim = L; *out_sec = R; break;
        case STEREO_MODE_LEFT_SIDE:   *out_prim = L; *out_sec = S; break;
        case STEREO_MODE_RIGHT_SIDE:  *out_prim = R; *out_sec = S; break;
        case STEREO_MODE_MID_SIDE:    *out_prim = M; *out_sec = S; break;
    }
}

static int write_warmup_raw(FILE *out, const int32_t *samples, int n, int bps)
{
    int i;

    if (bps != 8 && bps != 16 && bps != 24 && bps != 32) return ERR_ARGS;

    for (i = 0; i < n; i++)
    {
        if (bps == 8)
        {
            int8_t s = (int8_t)samples[i];
            if (fwrite(&s, 1, 1, out) != 1) return ERR_IO;
        }
        else if (bps == 16)
        {
            int16_t s = (int16_t)samples[i];
            if (fwrite(&s, 2, 1, out) != 1) return ERR_IO;
        }
        else if (bps == 24)
        {
            uint8_t buf[3];
            buf[0] = (uint8_t)(samples[i] & 0xFF);
            buf[1] = (uint8_t)((samples[i] >> 8) & 0xFF);
            buf[2] = (uint8_t)((samples[i] >> 16) & 0xFF);
            if (fwrite(buf, 3, 1, out) != 1) return ERR_IO;
        }
        else
        {
            if (fwrite(&samples[i], 4, 1, out) != 1) return ERR_IO;
        }
    }
    return OK;
}

static int encode_one_stream(FILE *out, int16_t bps, int lpc_max_order,
                             const int32_t *samples, uint32_t n)
{
    int pred_type = 0;
    int chosen_order = lpc_max_order;
    predictor_encoder_t *pe;
    const uint32_t *residuals;
    uint64_t lpc_cost = 0;
    uint64_t fixed_cost;
    int bps_for_verbatim;
    entropy_encoder_t *enc;
    uint32_t j;

    pe = predictor_encoder_create_lpc(lpc_max_order, (int)n);
    if (!pe) return ERR_MEM;

    residuals = predictor_encoder_encode_block(pe, samples, (int)n);
    if (!residuals) { predictor_encoder_destroy(pe); return ERR_IO; }

    chosen_order = predictor_encoder_order(pe);
    for (j = (uint32_t)chosen_order; j < n; j++) lpc_cost += residuals[j];

    fixed_cost = fixed2_cost_estimate(samples, n);

    if (fixed_cost < lpc_cost)
    {
        predictor_encoder_destroy(pe);
        pe = predictor_encoder_create_fixed(2, (int)n);
        if (!pe) return ERR_MEM;
        residuals = predictor_encoder_encode_block_fixed(pe, samples, (int)n);
        if (!residuals) { predictor_encoder_destroy(pe); return ERR_IO; }
        pred_type = 3;
    }

    fputc(pred_type, out);
    if (pred_type == 0) fputc(chosen_order, out);

    {
        int v = (int)bps + 5;
        if (v > 32) v = 32;
        bps_for_verbatim = v;
    }

    enc = entropy_create_rice(out, (uint16_t)bps_for_verbatim);
    if (!enc) { predictor_encoder_destroy(pe); return ERR_MEM; }

    if (pred_type == 0)
    {
        const int32_t *quotients;
        int precision;
        int real_order;
        int warmup_count;
        int residuals_count;
        uint64_t cost_raw;
        uint64_t cost_delta;
        int warmup_mode;
        int rc;

        quotients = predictor_encoder_quotients(pe);
        precision = predictor_encoder_precision(pe);
        real_order = predictor_encoder_order(pe);

        entropy_encode_precision(enc, (uint8_t)precision);
        entropy_encode_quotients(enc, quotients, real_order);

        warmup_count = real_order;
        if (warmup_count > (int)n) warmup_count = (int)n;

        residuals_count = (int)n - real_order;
        if (residuals_count < 0) residuals_count = 0;

        cost_raw = UINT64_MAX;
        if (bps == 8 || bps == 16 || bps == 24 || bps == 32)
            cost_raw = (uint64_t)warmup_count * (uint64_t)bps;

        cost_delta = rice_coder_estimate_delta(samples, (uint32_t)warmup_count, bps);
        warmup_mode = (cost_delta < cost_raw) ? 1 : 0;

        fputc(warmup_mode, out);

        if (warmup_mode == 1)
        {
            rice_coder_t *wrice = rice_coder_create(out, (uint16_t)bps);
            if (!wrice) { entropy_destroy(enc); predictor_encoder_destroy(pe); return ERR_MEM; }
            rice_coder_encode_warmup_delta(wrice, samples, (uint32_t)warmup_count);
            rice_coder_flush(wrice);
            rice_coder_destroy(wrice);
        }
        else
        {
            rc = write_warmup_raw(out, samples, warmup_count, bps);
            if (rc != OK) { entropy_destroy(enc); predictor_encoder_destroy(pe); return rc; }
        }

        entropy_set_num_warmup(enc, 0);
        rc = entropy_set_state(enc, residuals + real_order, residuals_count);
        if (rc < 0) { entropy_destroy(enc); predictor_encoder_destroy(pe); return ERR_IO; }

        for (j = (uint32_t)real_order; j < n; j++)
            entropy_encode_sample(enc, residuals[j]);
    }
    else
    {
        int rc = entropy_set_state(enc, residuals, (int)n);
        if (rc < 0) { entropy_destroy(enc); predictor_encoder_destroy(pe); return ERR_IO; }
        for (j = 0; j < n; j++) entropy_encode_sample(enc, residuals[j]);
    }

    entropy_destroy(enc);
    predictor_encoder_destroy(pe);
    return OK;
}

static int write_nf_header(FILE *out, wav_block_reader_t *wb,
                           predictor_method_t predictor, entropy_method_t entropy)
{
    uint32_t num_samples = wav_block_reader_num_samples(wb);
    uint16_t bits = wav_block_reader_bits_per_sample(wb);
    uint32_t sample_rate = wav_block_reader_sample_rate(wb);
    uint16_t channels = wav_block_reader_channels(wb);
    uint32_t magic = NOTFLAC_MAGIC;
    uint8_t version = NOTFLAC_VERSION;
    uint8_t entropy_byte = (uint8_t)entropy;
    uint8_t predictor_byte = (uint8_t)predictor;
    uint8_t reserved = 0;
    uint32_t block_size;
    uint64_t table_offset = 0;

    if (bits != 8 && bits != 16 && bits != 24) return ERR_BITS_PER_SAMPLE;

    fwrite(&magic, 4, 1, out);
    fwrite(&version, 1, 1, out);
    fwrite(&entropy_byte, 1, 1, out);
    fwrite(&predictor_byte, 1, 1, out);
    fwrite(&reserved, 1, 1, out);
    fwrite(&sample_rate, 4, 1, out);
    fwrite(&channels, 2, 1, out);
    fwrite(&bits, 2, 1, out);
    fwrite(&num_samples, 4, 1, out);
    block_size = (uint32_t)pick_block_size(sample_rate, channels);
    fwrite(&block_size, 4, 1, out);
    fwrite(&table_offset, 8, 1, out);

    if (predictor == PREDICTOR_LPC)
    {
        uint32_t order = (uint32_t)pick_order(sample_rate, bits, channels);
        fwrite(&order, 4, 1, out);
    }
    return OK;
}

static int encode_stereo_block(FILE *out, int16_t bps, int lpc_max_order,
                               const int32_t *block, uint32_t block_size)
{
    uint32_t n = block_size / 2;
    int32_t *L = malloc(sizeof(int32_t) * n);
    int32_t *R = malloc(sizeof(int32_t) * n);
    int32_t *M = malloc(sizeof(int32_t) * n);
    int32_t *S = malloc(sizeof(int32_t) * n);
    stereo_mode_t mode;
    const int32_t *prim, *sec;
    int rc1, rc2;

    if (!L || !R || !M || !S) { free(L); free(R); free(M); free(S); return ERR_MEM; }

    split_stereo(block, n, L, R, M, S);
    mode = choose_stereo_mode(L, R, M, S, n);
    select_streams(mode, L, R, M, S, &prim, &sec);

    fputc((int)mode, out);

    rc1 = encode_one_stream(out, bps, lpc_max_order, prim, n);
    rc2 = encode_one_stream(out, (mode != STEREO_MODE_INDEPENDENT) ? (int16_t)(bps + 1) : bps, lpc_max_order, sec, n);

    free(L); free(R); free(M); free(S);
    if (rc1 != OK) return rc1;
    if (rc2 != OK) return rc2;
    return OK;
}

static int encode_mono_block(FILE *out, int16_t bps, int lpc_max_order,
                             const int32_t *block, uint32_t block_size)
{
    fputc(0, out);
    return encode_one_stream(out, bps, lpc_max_order, block, block_size);
}

int encode_audio(predictor_method_t predictor, entropy_method_t entropy,
                 const char *in_path, const char *out_path)
{
    wav_block_reader_t *wb_probe;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_probe;
    uint32_t file_block_size;
    wav_block_reader_t *wb;
    FILE *out;
    int rc;
    int16_t bps;
    int lpc_max_order;
    uint32_t num_blocks;
    uint64_t *blocks_offset;
    uint32_t last_block_size;
    uint32_t i;
    uint64_t table_offset;

    if (predictor != PREDICTOR_LPC) return ERR_ARGS;
    if (entropy != ENTROPY_RICE_CODER) return ERR_ARGS;

    wb_probe = wav_block_reader_create(in_path, BLOCK_SIZE);
    if (!wb_probe) return ERR_MEM;

    sample_rate = wav_block_reader_sample_rate(wb_probe);
    channels = wav_block_reader_channels(wb_probe);
    bits_probe = wav_block_reader_bits_per_sample(wb_probe);
    wav_block_reader_destroy(wb_probe);

    file_block_size = (uint32_t)pick_block_size(sample_rate, channels);
    wb = wav_block_reader_create(in_path, (int)file_block_size);
    if (!wb) return ERR_MEM;

    out = fopen(out_path, "wb");
    if (!out) { wav_block_reader_destroy(wb); return ERR_IO; }

    rc = write_nf_header(out, wb, predictor, entropy);
    if (rc != OK) { wav_block_reader_destroy(wb); fclose(out); return rc; }

    bps = (int16_t)wav_block_reader_bits_per_sample(wb);
    lpc_max_order = pick_order(sample_rate, bits_probe, channels);
    num_blocks = wav_block_reader_num_blocks(wb);
    blocks_offset = malloc(sizeof(uint64_t) * (size_t)num_blocks);
    if (!blocks_offset) { wav_block_reader_destroy(wb); fclose(out); return ERR_MEM; }

    last_block_size = 0;

    for (i = 0; i < num_blocks; i++)
    {
        const int32_t *block = wav_block_reader_next(wb);
        uint32_t block_size = (uint32_t)wav_block_reader_count(wb);

        if (block_size == 0) break;

        if (block_size != file_block_size && i != num_blocks - 1)
        {
            free(blocks_offset); wav_block_reader_destroy(wb); fclose(out);
            return ERR_IO;
        }

        blocks_offset[i] = (uint64_t)ftell(out);
        last_block_size = block_size;

        if (channels == 2) rc = encode_stereo_block(out, bps, lpc_max_order, block, block_size);
        else               rc = encode_mono_block(out, bps, lpc_max_order, block, block_size);

        if (rc != OK) { free(blocks_offset); wav_block_reader_destroy(wb); fclose(out); return rc; }
    }

    table_offset = (uint64_t)ftell(out);
    fseek(out, OFFSET_OF_TABLE_OFFSET_IN_HEADER, SEEK_SET);
    fwrite(&table_offset, 8, 1, out);
    fseek(out, 0, SEEK_END);

    for (i = 0; i < num_blocks; i++)
    {
        uint32_t bsize = (i == num_blocks - 1) ? last_block_size : file_block_size;
        fwrite(&blocks_offset[i], 8, 1, out);
        fwrite(&bsize, 4, 1, out);
    }

    free(blocks_offset);
    wav_block_reader_destroy(wb);
    fclose(out);
    return OK;
}
