#include "audio_encoder.h"
#include "entropy_encoder.h"
#include "errors.h"
#include "stereo.h"
#include "notflac_format.h"
#include "predictor_encoder.h"
#include "wav_block.h"
#include "wav_stream.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int pick_order(uint32_t sample_rate)
{
    if (sample_rate <= 24000) return 8;
    if (sample_rate <= 48000) return 12;
    return 16;
}

static int pick_block_size(uint32_t sample_rate, uint16_t channels)
{
    (void)sample_rate;
    return 8192 * 2 * channels;
}

static uint64_t lpc_cost(int order, const int32_t *samples, uint32_t n)
{
    predictor_encoder_t *pe = predictor_encoder_create_lpc(order, (int)n);
    if (!pe)
    {
        return UINT64_MAX;
    }
    const uint32_t *res = predictor_encoder_encode_block(pe, samples, (int)n);
    uint64_t sum = 0;
    if (res)
    {
        for (uint32_t i = 0; i < n; i++)
        {
            sum += res[i];
        }
    }
    else
    {
        sum = UINT64_MAX;
    }
    predictor_encoder_destroy(pe);
    return sum;
}

static int write_nf_header(FILE *out, wav_block_reader_t *wb, predictor_method_t predictor, entropy_method_t entropy)
{
    uint32_t num_samples = wav_block_reader_num_samples(wb);
    uint16_t bits = wav_block_reader_bits_per_sample(wb);
    uint32_t sample_rate = wav_block_reader_sample_rate(wb);
    uint16_t channels = wav_block_reader_channels(wb);

    if (bits != 16 && bits != 24 && bits != 32)
    {
        return ERR_BITS_PER_SAMPLE;
    }

    uint32_t magic = NOTFLAC_MAGIC;
    uint8_t version = NOTFLAC_VERSION;
    uint8_t entropy_byte = (uint8_t)entropy;
    uint8_t predictor_byte = (uint8_t)predictor;
    uint8_t reserved = 0;

    fwrite(&magic, 4, 1, out);
    fwrite(&version, 1, 1, out);
    fwrite(&entropy_byte, 1, 1, out);
    fwrite(&predictor_byte, 1, 1, out);
    fwrite(&reserved, 1, 1, out);
    fwrite(&sample_rate, 4, 1, out);
    fwrite(&channels, 2, 1, out);
    fwrite(&bits, 2, 1, out);
    fwrite(&num_samples, 4, 1, out);

    uint32_t block_size = (uint32_t)pick_block_size(sample_rate, channels);
    fwrite(&block_size, 4, 1, out);

    uint64_t table_offset = 0;
    fwrite(&table_offset, 8, 1, out);

    if (predictor == PREDICTOR_LPC)
    {
        uint32_t order = (uint32_t)pick_order(sample_rate);
        fwrite(&order, 4, 1, out);
    }
    return OK;
}

static int encode_one_stream(FILE *out, predictor_method_t predictor, entropy_method_t entropy,
                             int16_t bps, int order,
                             const int32_t *samples, uint32_t n)
{
    predictor_encoder_t *pe;
    entropy_encoder_t *enc = NULL;

    if (predictor == PREDICTOR_LPC)
    {
        pe = predictor_encoder_create_lpc(order, (int)n);
    }
    else
    {
        pe = predictor_encoder_create_delta(bps, (int)n);
    }
    if (!pe)
    {
        return ERR_MEM;
    }

    if (entropy == ENTROPY_RICE_CODER)
    {
        enc = entropy_create_rice(out);
    }
    else
    {
        enc = entropy_create_range(out);
    }
    if (!enc)
    {
        predictor_encoder_destroy(pe);
        return ERR_MEM;
    }

    const uint32_t *residuals = predictor_encoder_encode_block(pe, samples, (int)n);
    if (!residuals)
    {
        entropy_destroy(enc);
        predictor_encoder_destroy(pe);
        return ERR_IO;
    }

    if (entropy == ENTROPY_RICE_CODER)
    {
        int rc = entropy_set_state(enc, residuals, (int)n);
        if (rc < 0)
        {
            entropy_destroy(enc);
            predictor_encoder_destroy(pe);
            return ERR_IO;
        }
    }

    if (predictor == PREDICTOR_LPC)
    {
        const int32_t *quotients = predictor_encoder_quotients(pe);
        int precision = predictor_encoder_precision(pe);
        entropy_encode_precision(enc, (uint8_t)precision);
        for (int j = 0; j < order; j++)
        {
            entropy_encode_quotient(enc, quotients[j]);
        }
    }

    for (uint32_t j = 0; j < n; j++)
    {
        entropy_encode_sample(enc, residuals[j]);
    }

    entropy_destroy(enc);
    predictor_encoder_destroy(pe);
    return OK;
}

int encode_audio(predictor_method_t predictor, entropy_method_t entropy, const char *in_path, const char *out_path)
{
    if ((predictor != PREDICTOR_DELTA && predictor != PREDICTOR_LPC) ||
        (entropy != ENTROPY_RANGE_CODER && entropy != ENTROPY_RICE_CODER))
    {
        return ERR_ARGS;
    }

    wav_block_reader_t *wb_probe = wav_block_reader_create(in_path, BLOCK_SIZE);
    if (!wb_probe)
    {
        return ERR_MEM;
    }
    uint32_t sample_rate = wav_block_reader_sample_rate(wb_probe);
    uint16_t channels = wav_block_reader_channels(wb_probe);
    wav_block_reader_destroy(wb_probe);

    uint32_t file_block_size = (uint32_t)pick_block_size(sample_rate, channels);

    wav_block_reader_t *wb = wav_block_reader_create(in_path, (int)file_block_size);
    if (!wb)
    {
        return ERR_MEM;
    }

    FILE *out = fopen(out_path, "wb");
    if (!out)
    {
        wav_block_reader_destroy(wb);
        return ERR_IO;
    }

    int rc = write_nf_header(out, wb, predictor, entropy);
    if (rc != OK)
    {
        wav_block_reader_destroy(wb);
        fclose(out);
        return rc;
    }

    int16_t bps = (int16_t)wav_block_reader_bits_per_sample(wb);
    int order = pick_order(sample_rate);
    uint32_t num_blocks = wav_block_reader_num_blocks(wb);

    uint64_t *blocks_offset = malloc(sizeof(uint64_t) * num_blocks);
    if (!blocks_offset)
    {
        wav_block_reader_destroy(wb);
        fclose(out);
        return ERR_MEM;
    }

    uint32_t last_block_size = 0;

    for (uint32_t i = 0; i < num_blocks; i++)
    {
        const int32_t *block = wav_block_reader_next(wb);
        uint32_t block_size = (uint32_t)wav_block_reader_count(wb);
        if (block_size == 0)
        {
            break;
        }
        if (block_size != file_block_size && i != num_blocks - 1)
        {
            free(blocks_offset);
            wav_block_reader_destroy(wb);
            fclose(out);
            return ERR_IO;
        }

        blocks_offset[i] = (uint64_t)ftell(out);
        last_block_size = block_size;

        if (channels == 2)
        {
            uint32_t n = block_size / 2;
            int32_t *L = malloc(n * sizeof(int32_t));
            int32_t *R = malloc(n * sizeof(int32_t));
            int32_t *M = malloc(n * sizeof(int32_t));
            int32_t *S = malloc(n * sizeof(int32_t));
            if (!L || !R || !M || !S)
            {
                free(L); free(R); free(M); free(S);
                free(blocks_offset);
                wav_block_reader_destroy(wb);
                fclose(out);
                return ERR_MEM;
            }

            for (uint32_t j = 0; j < n; j++)
            {
                L[j] = block[2 * j];
                R[j] = block[2 * j + 1];
                M[j] = (L[j] + R[j]) >> 1;
                S[j] = L[j] - R[j];
            }

            uint64_t cL = lpc_cost(order, L, n);
            uint64_t cR = lpc_cost(order, R, n);
            uint64_t cM = lpc_cost(order, M, n);
            uint64_t cS = lpc_cost(order, S, n);

            stereo_mode_t mode = STEREO_MODE_INDEPENDENT;
            uint64_t best = cL + cR;
            if (cL + cS < best) { best = cL + cS; mode = STEREO_MODE_LEFT_SIDE; }
            if (cR + cS < best) { best = cR + cS; mode = STEREO_MODE_RIGHT_SIDE; }
            if (cM + cS < best) { best = cM + cS; mode = STEREO_MODE_MID_SIDE; }

            const int32_t *prim;
            const int32_t *sec;
            switch (mode)
            {
                case STEREO_MODE_INDEPENDENT: prim = L; sec = R; break;
                case STEREO_MODE_LEFT_SIDE:   prim = L; sec = S; break;
                case STEREO_MODE_RIGHT_SIDE:  prim = R; sec = S; break;
                case STEREO_MODE_MID_SIDE:    prim = M; sec = S; break;
                default:                      prim = L; sec = R; break;
            }

            fputc((int)mode, out);
            int rc1 = encode_one_stream(out, predictor, entropy, bps, order, prim, n);
            int rc2 = encode_one_stream(out, predictor, entropy, bps, order, sec, n);

            free(L); free(R); free(M); free(S);

            if (rc1 != OK || rc2 != OK)
            {
                free(blocks_offset);
                wav_block_reader_destroy(wb);
                fclose(out);
                return (rc1 != OK) ? rc1 : rc2;
            }
        }
        else
        {
            fputc(0, out);
            int rc1 = encode_one_stream(out, predictor, entropy, bps, order, block, block_size);
            if (rc1 != OK)
            {
                free(blocks_offset);
                wav_block_reader_destroy(wb);
                fclose(out);
                return rc1;
            }
        }
    }

    uint64_t table_offset = (uint64_t)ftell(out);
    fseek(out, OFFSET_OF_TABLE_OFFSET_IN_HEADER, SEEK_SET);
    fwrite(&table_offset, 8, 1, out);
    fseek(out, 0, SEEK_END);

    for (uint32_t i = 0; i < num_blocks; i++)
    {
        fwrite(&blocks_offset[i], 8, 1, out);
        uint32_t bsize = (i == num_blocks - 1) ? last_block_size : file_block_size;
        fwrite(&bsize, 4, 1, out);
    }

    free(blocks_offset);
    wav_block_reader_destroy(wb);
    fclose(out);
    return OK;
}
