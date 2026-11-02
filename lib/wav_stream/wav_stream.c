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
};

wav_stream_t *wav_stream_open(const char *filename)
{
    char id[4], type[4];
    uint32_t chunk_size;
    uint16_t audio_format;
    long data_offset = -1;
    uint32_t data_size = 0;
    int found_fmt = 0;
    wav_stream_t *ws = malloc(sizeof(wav_stream_t));
    if (ws)
    {
        ws->stream = fopen(filename, "rb");
        if (!ws->stream)
        {
            free(ws);
            ws = NULL;
        }
        else
        {
            fread(id, 1, 4, ws->stream);
            fseek(ws->stream, 4, SEEK_CUR);
            fread(type, 1, 4, ws->stream);
            if (memcmp(id, "RIFF", 4) != 0 || memcmp(type, "WAVE", 4) != 0)
            {
                wav_stream_close(ws);
                ws = NULL;
            }
            else
            {
                while (fread(id, 1, 4, ws->stream) == 4)
                {
                    fread(&chunk_size, 4, 1, ws->stream);
                    if (memcmp(id, "fmt ", 4) == 0)
                    {
                        fread(&audio_format, 2, 1, ws->stream);
                        if (audio_format != 1 && audio_format != 0xFFFE)
                        {
                            wav_stream_close(ws);
                            return NULL;
                        }
                        else
                        {
                            fread(&ws->channels, 2, 1, ws->stream);
                            fread(&ws->sample_rate, 4, 1, ws->stream);
                            fseek(ws->stream, 6, SEEK_CUR);
                            fread(&ws->bits_per_sample, 2, 1, ws->stream);
                            if (chunk_size > 16)
                                fseek(ws->stream, chunk_size - 16, SEEK_CUR);
                            if (chunk_size % 2)
                                fseek(ws->stream, 1, SEEK_CUR);
                            found_fmt = 1;
                        }
                    }
                    else if (memcmp(id, "data", 4) == 0)
                    {
                        data_offset = ftell(ws->stream);
                        data_size = chunk_size;
                        fseek(ws->stream, chunk_size, SEEK_CUR);
                        if (chunk_size % 2)
                            fseek(ws->stream, 1, SEEK_CUR);
                    }
                    else
                    {
                        fseek(ws->stream, chunk_size, SEEK_CUR);
                        if (chunk_size % 2)
                            fseek(ws->stream, 1, SEEK_CUR);
                    }
                }
                if (!found_fmt || data_offset == -1)
                {
                    wav_stream_close(ws);
                    ws = NULL;
                }
                else
                {
                    ws->num_samples = data_size / (ws->bits_per_sample / 8);
                    fseek(ws->stream, data_offset, SEEK_SET);
                }
            }
        }
    }
    return ws;
}

void wav_stream_close(wav_stream_t *ws)
{
    fclose(ws->stream);
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
    if (ws->bits_per_sample == 16)
    {
        int16_t s;
        if (fread(&s, 2, 1, ws->stream) != 1)
            return WAV_ERR_IO;
        *sample = (int32_t)s;
    }
    else if (ws->bits_per_sample == 24)
    {
        uint8_t buf[3];
        if (fread(buf, 3, 1, ws->stream) != 1)
            return WAV_ERR_IO;
        int32_t v = buf[0] | (buf[1] << 8) | (buf[2] << 16);
        if (v & 0x800000)
            v |= 0xFF000000;
        *sample = v;
    }
    else if (ws->bits_per_sample == 32)
    {
        int32_t s;
        if (fread(&s, 4, 1, ws->stream) != 1)
            return WAV_ERR_IO;
        *sample = s;
    }
    else
    {
        return WAV_ERR_IO;
    }
    return WAV_OK;
}
