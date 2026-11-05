#include "audio_decoder.h"
#include "errors.h"
#include "notflac_format.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <file.notflac> [start_sec] [end_sec]\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f)
    {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    audio_decoder_t *dec = audio_decoder_open(f);
    if (!dec)
    {
        fprintf(stderr, "invalid notflac\n");
        fclose(f);
        return 1;
    }

    uint32_t sample_rate = audio_decoder_sample_rate(dec);
    uint16_t bits = audio_decoder_bits_per_sample(dec);
    uint16_t channels = audio_decoder_channels(dec);
    uint32_t num_samples = audio_decoder_num_samples(dec);
    uint32_t total_frames = num_samples / channels;

    uint32_t start_frame = 0;
    uint32_t end_frame = total_frames;
    if (argc >= 3)
    {
        double start_sec = atof(argv[2]);
        start_frame = (uint32_t)(start_sec * sample_rate);
        if (start_frame >= total_frames)
            start_frame = 0;
    }
    if (argc >= 4)
    {
        double end_sec = atof(argv[3]);
        end_frame = (uint32_t)(end_sec * sample_rate);
        if (end_frame > total_frames)
            end_frame = total_frames;
    }

    uint32_t start_sample = start_frame * channels;
    uint32_t start_block = start_sample / audio_decoder_block_size(dec);
    if (audio_decoder_seek_block(dec, start_block) != OK)
    {
        fprintf(stderr, "seek failed\n");
        audio_decoder_close(dec);
        fclose(f);
        return 1;
    }

    int n = 0;
    const int32_t *samples = audio_decoder_decode_block(dec, &n);
    if (!samples)
    {
        audio_decoder_close(dec);
        fclose(f);
        return 1;
    }
    uint32_t block_start = start_block * audio_decoder_block_size(dec);
    uint32_t skip = start_sample - block_start;

    fprintf(stderr, "Playing %s from %.2fs to %.2fs, %u Hz, %u ch, %u bits\n", argv[1],
            (double)start_frame / sample_rate, (double)end_frame / sample_rate, sample_rate, channels, bits);

    uint32_t sample_idx = start_sample;
    while (sample_idx < end_frame * channels)
    {
        if (skip >= (uint32_t)n)
        {
            uint32_t next_block = start_block + 1;
            if (audio_decoder_seek_block(dec, next_block) != OK)
                break;
            samples = audio_decoder_decode_block(dec, &n);
            if (!samples)
                break;
            skip = 0;
            start_block = next_block;
            continue;
        }
        int32_t sample = samples[skip];
        skip++;
        sample_idx++;
        if (bits == 16)
        {
            int16_t s = (int16_t)sample;
            fwrite(&s, 2, 1, stdout);
        }
        else if (bits == 24)
        {
            uint8_t buf[3];
            buf[0] = sample & 0xFF;
            buf[1] = (sample >> 8) & 0xFF;
            buf[2] = (sample >> 16) & 0xFF;
            fwrite(buf, 3, 1, stdout);
        }
        else if (bits == 32)
        {
            fwrite(&sample, 4, 1, stdout);
        }
    }

    audio_decoder_close(dec);
    fclose(f);
    return 0;
}
