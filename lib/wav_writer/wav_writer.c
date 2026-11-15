#include "wav_writer.h"
#include <stdlib.h>
#include <string.h>

#define WW_BUF_SAMPLES 16384

struct wav_writer {
    FILE *f;
    uint16_t bits_per_sample;
    uint32_t num_samples;
    uint32_t written;
    uint8_t *buf;
    uint32_t buf_count;
};

static int wav_writer_flush_buf(wav_writer_t *w)
{
    size_t bytes;
    size_t written;

    if (w->buf_count == 0)
    {
        return 0;
    }

    bytes = (size_t)w->buf_count * (w->bits_per_sample / 8);
    written = fwrite(w->buf, 1, bytes, w->f);
    w->buf_count = 0;

    if (written != bytes)
    {
        return -1;
    }

    return 0;
}

static void write_wav_header(wav_writer_t *w, uint32_t sample_rate, uint16_t channels)
{
    uint32_t data_size;
    uint32_t riff_size;
    uint16_t block_align;
    uint32_t byte_rate;
    uint32_t fmt_size;
    uint16_t audio_format;

    data_size = w->num_samples * (w->bits_per_sample / 8);
    riff_size = 36 + data_size;
    block_align = channels * (w->bits_per_sample / 8);
    byte_rate = sample_rate * block_align;
    fmt_size = 16;
    audio_format = 1;

    fwrite("RIFF", 1, 4, w->f);
    fwrite(&riff_size, 4, 1, w->f);
    fwrite("WAVE", 1, 4, w->f);
    fwrite("fmt ", 1, 4, w->f);
    fwrite(&fmt_size, 4, 1, w->f);
    fwrite(&audio_format, 2, 1, w->f);
    fwrite(&channels, 2, 1, w->f);
    fwrite(&sample_rate, 4, 1, w->f);
    fwrite(&byte_rate, 4, 1, w->f);
    fwrite(&block_align, 2, 1, w->f);
    fwrite(&w->bits_per_sample, 2, 1, w->f);
    fwrite("data", 1, 4, w->f);
    fwrite(&data_size, 4, 1, w->f);
}

wav_writer_t *wav_writer_create(const char *filename, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, uint32_t num_samples)
{
    wav_writer_t *w;

    w = malloc(sizeof(wav_writer_t));
    if (!w)
    {
        return NULL;
    }

    w->f = fopen(filename, "wb");

    if (!w->f)
    {
        free(w);
        return NULL;
    }

    w->bits_per_sample = bits_per_sample;
    w->num_samples = num_samples;
    w->written = 0;
    w->buf_count = 0;
    w->buf = malloc((size_t)WW_BUF_SAMPLES * (bits_per_sample / 8));

    if (!w->buf)
    {
        fclose(w->f);
        free(w);
        return NULL;
    }

    write_wav_header(w, sample_rate, channels);

    return w;
}

void wav_writer_destroy(wav_writer_t *w)
{
    if (!w)
    {
        return;
    }

    wav_writer_flush_buf(w);
    fclose(w->f);
    free(w->buf);
    free(w);
}

int wav_writer_write_sample(wav_writer_t *w, int32_t sample)
{
    int bps;
    uint8_t *p;

    bps = w->bits_per_sample / 8;
    p = w->buf + (size_t)w->buf_count * (size_t)bps;

    if (w->bits_per_sample == 8)
    {
        p[0] = (uint8_t)(sample + 128);
    }
    else if (w->bits_per_sample == 16)
    {
        int16_t s = (int16_t)sample;
        p[0] = (uint8_t)(s & 0xFF);
        p[1] = (uint8_t)((s >> 8) & 0xFF);
    }
    else if (w->bits_per_sample == 24)
    {
        p[0] = (uint8_t)(sample & 0xFF);
        p[1] = (uint8_t)((sample >> 8) & 0xFF);
        p[2] = (uint8_t)((sample >> 16) & 0xFF);
    }
    else if (w->bits_per_sample == 32)
    {
        p[0] = (uint8_t)(sample & 0xFF);
        p[1] = (uint8_t)((sample >> 8) & 0xFF);
        p[2] = (uint8_t)((sample >> 16) & 0xFF);
        p[3] = (uint8_t)((sample >> 24) & 0xFF);
    }
    else
    {
        return -1;
    }

    w->buf_count++;
    w->written++;

    if (w->buf_count >= WW_BUF_SAMPLES)
    {
        if (wav_writer_flush_buf(w) != 0)
        {
            return -1;
        }
    }

    return 0;
}

int wav_writer_write_block(wav_writer_t *w, const int32_t *samples, uint32_t count)
{
    uint32_t i;
    int bps;
    uint8_t *p;
    uint32_t space;

    if (w->bits_per_sample == 8)
    {
        for (i = 0; i < count; i++)
        {
            if (w->buf_count >= WW_BUF_SAMPLES)
            {
                if (wav_writer_flush_buf(w) != 0)
                {
                    return -1;
                }
            }

            w->buf[w->buf_count] = (uint8_t)(samples[i] + 128);
            w->buf_count++;
            w->written++;
        }

        return 0;
    }

    if (w->bits_per_sample == 16)
    {
        i = 0;

        while (i < count)
        {
            space = WW_BUF_SAMPLES - w->buf_count;

            if (space > count - i)
            {
                space = count - i;
            }

            p = w->buf + (size_t)w->buf_count * 2;

            for (uint32_t j = 0; j < space; j++)
            {
                int16_t s = (int16_t)samples[i + j];
                p[2 * j] = (uint8_t)(s & 0xFF);
                p[2 * j + 1] = (uint8_t)((s >> 8) & 0xFF);
            }

            w->buf_count += space;
            w->written += space;
            i += space;

            if (w->buf_count >= WW_BUF_SAMPLES)
            {
                if (wav_writer_flush_buf(w) != 0)
                {
                    return -1;
                }
            }
        }

        return 0;
    }

    if (w->bits_per_sample == 24)
    {
        i = 0;

        while (i < count)
        {
            space = WW_BUF_SAMPLES - w->buf_count;

            if (space > count - i)
            {
                space = count - i;
            }

            p = w->buf + (size_t)w->buf_count * 3;

            for (uint32_t j = 0; j < space; j++)
            {
                int32_t s = samples[i + j];
                p[3 * j] = (uint8_t)(s & 0xFF);
                p[3 * j + 1] = (uint8_t)((s >> 8) & 0xFF);
                p[3 * j + 2] = (uint8_t)((s >> 16) & 0xFF);
            }

            w->buf_count += space;
            w->written += space;
            i += space;

            if (w->buf_count >= WW_BUF_SAMPLES)
            {
                if (wav_writer_flush_buf(w) != 0)
                {
                    return -1;
                }
            }
        }

        return 0;
    }

    if (w->bits_per_sample == 32)
    {
        i = 0;

        while (i < count)
        {
            space = WW_BUF_SAMPLES - w->buf_count;

            if (space > count - i)
            {
                space = count - i;
            }

            p = w->buf + (size_t)w->buf_count * 4;

            for (uint32_t j = 0; j < space; j++)
            {
                int32_t s = samples[i + j];
                p[4 * j] = (uint8_t)(s & 0xFF);
                p[4 * j + 1] = (uint8_t)((s >> 8) & 0xFF);
                p[4 * j + 2] = (uint8_t)((s >> 16) & 0xFF);
                p[4 * j + 3] = (uint8_t)((s >> 24) & 0xFF);
            }

            w->buf_count += space;
            w->written += space;
            i += space;

            if (w->buf_count >= WW_BUF_SAMPLES)
            {
                if (wav_writer_flush_buf(w) != 0)
                {
                    return -1;
                }
            }
        }

        return 0;
    }

    return -1;
}
