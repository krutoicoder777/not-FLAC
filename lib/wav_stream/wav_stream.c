#include "wav_stream.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct wav_stream wav_stream_t;
struct wav_stream
{
    FILE *stream;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t num_samples;
    long data_offset;
};

static int skip_bytes(FILE *f, uint32_t n)
{
    if (n == 0)
    {
        return WAV_OK;
    }
    if (fseek(f, (long)n, SEEK_CUR) != 0)
    {
        return WAV_ERR_SEEK;
    }
    return WAV_OK;
}

wav_stream_t *wav_stream_open(const char *filename)
{
    char id[4];
    char type[4];
    uint32_t chunk_size;
    uint16_t audio_format;
    long data_offset = -1;
    uint32_t data_size = 0;
    int found_data = 0;
    int found_fmt = 0;
    int status = WAV_ERR_FORMAT;

    wav_stream_t *ws = malloc(sizeof(wav_stream_t));
    if (!ws)
    {
        return NULL;
    }
    memset(ws, 0, sizeof(*ws));

    ws->stream = fopen(filename, "rb");
    if (!ws->stream)
    {
        free(ws);
        return NULL;
    }

    do
    {
        if (fread(id, 1, 4, ws->stream) != 4)
        {
            status = WAV_ERR_IO;
            break;
        }
        if (fread(&chunk_size, 4, 1, ws->stream) != 1)
        {
            status = WAV_ERR_IO;
            break;
        }
        if (fread(type, 1, 4, ws->stream) != 4)
        {
            status = WAV_ERR_IO;
            break;
        }
        if (memcmp(id, "RIFF", 4) != 0 || memcmp(type, "WAVE", 4) != 0)
        {
            status = WAV_ERR_FORMAT;
            break;
        }

        int chunks_ok = 1;
        int chunks_status = WAV_ERR_FORMAT;
        while (fread(id, 1, 4, ws->stream) == 4)
        {
            if (fread(&chunk_size, 4, 1, ws->stream) != 1)
            {
                chunks_ok = 0;
                chunks_status = WAV_ERR_IO;
                break;
            }

            if (memcmp(id, "fmt ", 4) == 0)
            {
                if (fread(&audio_format, 2, 1, ws->stream) != 1)
                {
                    chunks_ok = 0;
                    chunks_status = WAV_ERR_IO;
                    break;
                }
                if (audio_format != 1 && audio_format != 0xFFFE)
                {
                    chunks_ok = 0;
                    chunks_status = WAV_ERR_FORMAT;
                    break;
                }
                if (fread(&ws->channels, 2, 1, ws->stream) != 1)
                {
                    chunks_ok = 0;
                    chunks_status = WAV_ERR_IO;
                    break;
                }
                if (fread(&ws->sample_rate, 4, 1, ws->stream) != 1)
                {
                    chunks_ok = 0;
                    chunks_status = WAV_ERR_IO;
                    break;
                }
                if (skip_bytes(ws->stream, 6) != WAV_OK)
                {
                    chunks_ok = 0;
                    chunks_status = WAV_ERR_SEEK;
                    break;
                }
                if (fread(&ws->bits_per_sample, 2, 1, ws->stream) != 1)
                {
                    chunks_ok = 0;
                    chunks_status = WAV_ERR_IO;
                    break;
                }
                if (ws->channels == 0)
                {
                    chunks_ok = 0;
                    chunks_status = WAV_ERR_FORMAT;
                    break;
                }
                if (ws->bits_per_sample != 16 && ws->bits_per_sample != 24 && ws->bits_per_sample != 32)
                {
                    chunks_ok = 0;
                    chunks_status = WAV_ERR_FORMAT;
                    break;
                }
                if (chunk_size > 16)
                {
                    if (skip_bytes(ws->stream, chunk_size - 16) != WAV_OK)
                    {
                        chunks_ok = 0;
                        chunks_status = WAV_ERR_SEEK;
                        break;
                    }
                }
                if (chunk_size % 2)
                {
                    if (skip_bytes(ws->stream, 1) != WAV_OK)
                    {
                        chunks_ok = 0;
                        chunks_status = WAV_ERR_SEEK;
                        break;
                    }
                }
                found_fmt = 1;
            }
            else if (memcmp(id, "data", 4) == 0)
            {
                long pos = ftell(ws->stream);
                if (pos < 0)
                {
                    chunks_ok = 0;
                    chunks_status = WAV_ERR_IO;
                    break;
                }
                data_offset = pos;
                data_size = chunk_size;
                found_data = 1;
                if (skip_bytes(ws->stream, chunk_size) != WAV_OK)
                {
                    chunks_ok = 0;
                    chunks_status = WAV_ERR_SEEK;
                    break;
                }
                if (chunk_size % 2)
                {
                    if (skip_bytes(ws->stream, 1) != WAV_OK)
                    {
                        chunks_ok = 0;
                        chunks_status = WAV_ERR_SEEK;
                        break;
                    }
                }
            }
            else
            {
                if (skip_bytes(ws->stream, chunk_size) != WAV_OK)
                {
                    chunks_ok = 0;
                    chunks_status = WAV_ERR_SEEK;
                    break;
                }
                if (chunk_size % 2)
                {
                    if (skip_bytes(ws->stream, 1) != WAV_OK)
                    {
                        chunks_ok = 0;
                        chunks_status = WAV_ERR_SEEK;
                        break;
                    }
                }
            }
        }
        if (!chunks_ok)
        {
            status = chunks_status;
            break;
        }
        if (!found_fmt || !found_data)
        {
            status = WAV_ERR_FORMAT;
            break;
        }

        uint32_t bytes_per_sample = ws->bits_per_sample / 8;
        if (bytes_per_sample == 0)
        {
            status = WAV_ERR_FORMAT;
            break;
        }
        if (data_size % bytes_per_sample != 0)
        {
            status = WAV_ERR_FORMAT;
            break;
        }
        ws->num_samples = data_size / bytes_per_sample;
        if (ws->num_samples == 0)
        {
            status = WAV_ERR_FORMAT;
            break;
        }

        ws->data_offset = data_offset;
        if (fseek(ws->stream, ws->data_offset, SEEK_SET) != 0)
        {
            status = WAV_ERR_SEEK;
            break;
        }
        status = WAV_OK;
    } while (0);

    if (status != WAV_OK)
    {
        fclose(ws->stream);
        free(ws);
        return NULL;
    }
    return ws;
}

void wav_stream_close(wav_stream_t *ws)
{
    if (!ws)
    {
        return;
    }
    if (ws->stream)
    {
        fclose(ws->stream);
    }
    free(ws);
}

uint32_t wav_stream_sample_rate(wav_stream_t *ws)
{
    return ws->sample_rate;
}

uint16_t wav_stream_channels(wav_stream_t *ws)
{
    return ws->channels;
}

uint16_t wav_stream_bits_per_sample(wav_stream_t *ws)
{
    return ws->bits_per_sample;
}

uint32_t wav_stream_num_samples(wav_stream_t *ws)
{
    return ws->num_samples;
}

int wav_stream_next_sample(wav_stream_t *ws, int32_t *sample)
{
    if (!ws || !ws->stream || !sample)
    {
        return WAV_ERR_ARGS;
    }

    if (ws->bits_per_sample == 16)
    {
        int16_t s;
        if (fread(&s, 2, 1, ws->stream) != 1)
        {
            return WAV_ERR_IO;
        }
        *sample = (int32_t)s;
    }
    else if (ws->bits_per_sample == 24)
    {
        uint8_t buf[3];
        if (fread(buf, 3, 1, ws->stream) != 1)
        {
            return WAV_ERR_IO;
        }
        int32_t v = (int32_t)buf[0] | ((int32_t)buf[1] << 8) | ((int32_t)buf[2] << 16);
        if (v & 0x800000)
        {
            v |= (int32_t)0xFF000000;
        }
        *sample = v;
    }
    else if (ws->bits_per_sample == 32)
    {
        int32_t s;
        if (fread(&s, 4, 1, ws->stream) != 1)
        {
            return WAV_ERR_IO;
        }
        *sample = s;
    }
    else
    {
        return WAV_ERR_FORMAT;
    }
    return WAV_OK;
}

int wav_stream_seek_sample(wav_stream_t *ws, uint32_t sample_index)
{
    if (!ws || !ws->stream)
    {
        return WAV_ERR_ARGS;
    }
    if (sample_index >= ws->num_samples)
    {
        return WAV_ERR_ARGS;
    }
    long offset = ws->data_offset + (long)sample_index * (long)(ws->bits_per_sample / 8);
    if (fseek(ws->stream, offset, SEEK_SET) != 0)
    {
        return WAV_ERR_SEEK;
    }
    return WAV_OK;
}
