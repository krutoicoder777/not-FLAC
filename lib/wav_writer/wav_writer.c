#include "wav_writer.h"
#include <stdlib.h>
#include <string.h>

struct wav_writer {
    FILE *f;
    uint16_t bits_per_sample;
    uint32_t num_samples;
    uint32_t written;
};

wav_writer_t *wav_writer_create(const char *filename, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, uint32_t num_samples)
{
    wav_writer_t *w = malloc(sizeof(wav_writer_t));
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

    uint32_t data_size = num_samples * (bits_per_sample / 8);
    uint32_t riff_size = 36 + data_size;
    uint16_t block_align = channels * (bits_per_sample / 8);
    uint32_t byte_rate = sample_rate * block_align;

    fwrite("RIFF", 1, 4, w->f);
    fwrite(&riff_size, 4, 1, w->f);
    fwrite("WAVE", 1, 4, w->f);
    fwrite("fmt ", 1, 4, w->f);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, w->f);
    uint16_t audio_format = 1;
    fwrite(&audio_format, 2, 1, w->f);
    fwrite(&channels, 2, 1, w->f);
    fwrite(&sample_rate, 4, 1, w->f);
    fwrite(&byte_rate, 4, 1, w->f);
    fwrite(&block_align, 2, 1, w->f);
    fwrite(&bits_per_sample, 2, 1, w->f);
    fwrite("data", 1, 4, w->f);
    fwrite(&data_size, 4, 1, w->f);

    return w;
}

void wav_writer_destroy(wav_writer_t *w)
{
    if (w)
    {
        fclose(w->f);
        free(w);
    }
}

int wav_writer_write_sample(wav_writer_t *w, int32_t sample)
{
    if (w->bits_per_sample == 16)
    {
        int16_t s = (int16_t)sample;
        if (fwrite(&s, 2, 1, w->f) != 1)
        {
            return -1;
        }
    }
    else if (w->bits_per_sample == 24)
    {
        uint8_t buf[3];
        buf[0] = (uint8_t)(sample & 0xFF);
        buf[1] = (uint8_t)((sample >> 8) & 0xFF);
        buf[2] = (uint8_t)((sample >> 16) & 0xFF);
        if (fwrite(buf, 3, 1, w->f) != 1)
        {
            return -1;
        }
    }
    else if (w->bits_per_sample == 32)
    {
        if (fwrite(&sample, 4, 1, w->f) != 1)
        {
            return -1;
        }
    }
    else
    {
        return -1;
    }
    w->written++;
    return 0;
}

int wav_writer_write_samples(wav_writer_t *w, const int32_t *samples, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (wav_writer_write_sample(w, samples[i]) != 0)
        {
            return -1;
        }
    }
    return 0;
}
