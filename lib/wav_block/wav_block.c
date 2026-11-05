#include "wav_stream.h"
#include <stdint.h>
#include <stdlib.h>

typedef struct wav_block_reader wav_block_reader_t;

struct wav_block_reader
{
    wav_stream_t *ws;
    int32_t *current_block;
    int current_block_size;
    int read_blocks;
    int num_blocks;
    int block_size;
};

wav_block_reader_t *wav_block_reader_create(const char *filename, int block_size)
{
    wav_stream_t *ws = wav_stream_open(filename);
    if (ws == NULL)
    {
        return NULL;
    }
    wav_block_reader_t *wb = malloc(sizeof(wav_block_reader_t));
    if (wb == NULL)
    {
        wav_stream_close(ws);
        return NULL;
    }
    wb->ws = ws;
    wb->current_block = malloc(block_size * sizeof(int32_t));
    if (wb->current_block == NULL)
    {
        wav_stream_close(ws);
        free(wb);
        return NULL;
    }
    wb->current_block_size = 0;
    wb->block_size = block_size;
    wb->read_blocks = 0;
    wb->num_blocks = (wav_stream_num_samples(wb->ws) - 1) / block_size + 1;
    return wb;
}

void wav_block_reader_destroy(wav_block_reader_t *wb)
{
    wav_stream_close(wb->ws);
    free(wb->current_block);
    free(wb);
}

const int32_t *wav_block_reader_next(wav_block_reader_t *wb)
{
    int cur_size = 0;
    if (wb->read_blocks == wb->num_blocks)
    {
        wb->current_block_size = 0;
        return NULL;
    }
    int samples_to_read = wb->read_blocks == wb->num_blocks - 1
                              ? wav_stream_num_samples(wb->ws) - wb->read_blocks * wb->block_size
                              : wb->block_size;
    int32_t sample;
    int rc = WAV_OK;
    while (cur_size < samples_to_read && (rc = wav_stream_next_sample(wb->ws, &sample)) == WAV_OK)
    {
        wb->current_block[cur_size] = sample;
        cur_size++;
    }
    if (rc == WAV_ERR_IO)
    {
        wb->current_block_size = 0;
        return NULL;
    }
    wb->current_block_size = cur_size;
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
    return wb->num_blocks;
}
