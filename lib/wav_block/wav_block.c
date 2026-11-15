#include "wav_stream.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct wav_block_reader wav_block_reader_t;

struct wav_block_reader
{
    wav_stream_t *ws;
    FILE *fp;
    int32_t *current_block;
    int current_block_size;
    int read_blocks;
    int num_blocks;
    int block_size;
    int bytes_per_sample;
    uint8_t *raw_buf;
};

static void decode_raw_to_block(wav_block_reader_t *wb, int samples_to_read)
{
    int bps;
    const uint8_t *p;
    int i;

    bps = wb->bytes_per_sample;
    p = wb->raw_buf;

    if (bps == 1)
    {
        for (i = 0; i < samples_to_read; i++)
        {
            wb->current_block[i] = (int32_t)p[i] - 128;
        }
    }
    else if (bps == 2)
    {
        for (i = 0; i < samples_to_read; i++)
        {
            uint32_t v = (uint32_t)p[2 * i] | ((uint32_t)p[2 * i + 1] << 8);
            wb->current_block[i] = (int32_t)(int16_t)v;
        }
    }
    else if (bps == 3)
    {
        for (i = 0; i < samples_to_read; i++)
        {
            uint32_t v = (uint32_t)p[3 * i] |
                         ((uint32_t)p[3 * i + 1] << 8) |
                         ((uint32_t)p[3 * i + 2] << 16);

            if (v & 0x800000)
            {
                v |= 0xFF000000;
            }

            wb->current_block[i] = (int32_t)v;
        }
    }
    else
    {
        for (i = 0; i < samples_to_read; i++)
        {
            uint32_t v = (uint32_t)p[4 * i] |
                         ((uint32_t)p[4 * i + 1] << 8) |
                         ((uint32_t)p[4 * i + 2] << 16) |
                         ((uint32_t)p[4 * i + 3] << 24);
            wb->current_block[i] = (int32_t)v;
        }
    }
}

wav_block_reader_t *wav_block_reader_create(const char *filename, int block_size)
{
    wav_stream_t *ws;
    wav_block_reader_t *wb;
    long off;

    ws = wav_stream_open(filename);
    if (!ws)
    {
        return NULL;
    }

    wb = malloc(sizeof(*wb));
    if (!wb)
    {
        wav_stream_close(ws);
        return NULL;
    }

    wb->ws = ws;
    wb->fp = fopen(filename, "rb");

    if (!wb->fp)
    {
        wav_stream_close(ws);
        free(wb);
        return NULL;
    }

    wb->block_size = block_size;
    wb->bytes_per_sample = wav_stream_bits_per_sample(ws) / 8;
    wb->current_block = malloc(sizeof(int32_t) * (size_t)block_size);
    wb->raw_buf = malloc((size_t)block_size * (size_t)wb->bytes_per_sample);

    if (!wb->current_block || !wb->raw_buf)
    {
        fclose(wb->fp);
        wav_stream_close(ws);
        free(wb->current_block);
        free(wb->raw_buf);
        free(wb);
        return NULL;
    }

    wb->current_block_size = 0;
    wb->read_blocks = 0;
    wb->num_blocks = (int)((wav_stream_num_samples(ws) + (uint32_t)block_size - 1) / (uint32_t)block_size);

    off = wav_stream_data_offset(ws);

    if (off < 0 || fseek(wb->fp, off, SEEK_SET) != 0)
    {
        fclose(wb->fp);
        wav_stream_close(ws);
        free(wb->current_block);
        free(wb->raw_buf);
        free(wb);
        return NULL;
    }

    return wb;
}

void wav_block_reader_destroy(wav_block_reader_t *wb)
{
    if (!wb)
    {
        return;
    }

    if (wb->fp)
    {
        fclose(wb->fp);
    }

    wav_stream_close(wb->ws);
    free(wb->current_block);
    free(wb->raw_buf);
    free(wb);
}

const int32_t *wav_block_reader_next(wav_block_reader_t *wb)
{
    int total;
    int remaining;
    int samples_to_read;
    size_t bytes_to_read;
    size_t got;

    if (wb->read_blocks >= wb->num_blocks)
    {
        wb->current_block_size = 0;
        return NULL;
    }

    total = (int)wav_stream_num_samples(wb->ws);
    remaining = total - wb->read_blocks * wb->block_size;
    samples_to_read = (remaining < wb->block_size) ? remaining : wb->block_size;

    if (samples_to_read <= 0)
    {
        wb->current_block_size = 0;
        return NULL;
    }

    bytes_to_read = (size_t)samples_to_read * (size_t)wb->bytes_per_sample;
    got = fread(wb->raw_buf, 1, bytes_to_read, wb->fp);

    if (got != bytes_to_read)
    {
        wb->current_block_size = 0;
        return NULL;
    }

    decode_raw_to_block(wb, samples_to_read);

    wb->current_block_size = samples_to_read;
    wb->read_blocks++;

    return wb->current_block;
}

int wav_block_reader_count(wav_block_reader_t *wb)
{
    return wb->current_block_size;
}

uint32_t wav_block_reader_sample_rate(wav_block_reader_t *wb)
{
    return wav_stream_sample_rate(wb->ws);
}

uint16_t wav_block_reader_channels(wav_block_reader_t *wb)
{
    return wav_stream_channels(wb->ws);
}

uint16_t wav_block_reader_bits_per_sample(wav_block_reader_t *wb)
{
    return wav_stream_bits_per_sample(wb->ws);
}

uint32_t wav_block_reader_num_samples(wav_block_reader_t *wb)
{
    return wav_stream_num_samples(wb->ws);
}

uint32_t wav_block_reader_num_blocks(wav_block_reader_t *wb)
{
    return (uint32_t)wb->num_blocks;
}
