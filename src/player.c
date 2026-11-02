#include "audio_decoder.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc < 2)
        return 1;

    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 1;

    audio_decoder_t *dec = audio_decoder_open(f);
    if (!dec)
    {
        fclose(f);
        return 1;
    }

    uint16_t bits = audio_decoder_bits_per_sample(dec);
    int32_t sample;

    while (audio_decode_next(dec, &sample) == 0)
    {
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
