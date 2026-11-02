#include "audio_decoder.h"
#include "audio_encoder.h"
#include "notflac_format.h"
#include "wav_stream.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_wav_header(FILE *out, uint32_t rate, uint16_t ch, uint16_t bits, uint32_t num_samples)
{
    uint32_t data_size = num_samples * (bits / 8);
    uint32_t riff_size = 36 + data_size;
    uint16_t block_align = ch * (bits / 8);
    uint32_t byte_rate = rate * block_align;

    fwrite("RIFF", 1, 4, out);
    fwrite(&riff_size, 4, 1, out);
    fwrite("WAVE", 1, 4, out);
    fwrite("fmt ", 1, 4, out);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, out);
    uint16_t audio_format = 1;
    fwrite(&audio_format, 2, 1, out);
    fwrite(&ch, 2, 1, out);
    fwrite(&rate, 4, 1, out);
    fwrite(&byte_rate, 4, 1, out);
    fwrite(&block_align, 2, 1, out);
    fwrite(&bits, 2, 1, out);
    fwrite("data", 1, 4, out);
    fwrite(&data_size, 4, 1, out);
}

static int do_encode(const char *in, const char *out, uint8_t pred, uint8_t ent)
{
    return encode_audio((predictor_method_t)pred, (entropy_method_t)ent, in, out);
}

static int do_decode(const char *in, const char *out)
{
    FILE *f = fopen(in, "rb");
    if (!f)
    {
        printf("Error: cannot open %s\n", in);
        return 1;
    }

    audio_decoder_t *dec = audio_decoder_open(f);
    if (!dec)
    {
        printf("Error: invalid notflac file\n");
        fclose(f);
        return 1;
    }

    FILE *wav = fopen(out, "wb");
    if (!wav)
    {
        printf("Error: cannot create %s\n", out);
        audio_decoder_close(dec);
        fclose(f);
        return 1;
    }

    uint32_t num_samples = audio_decoder_num_samples(dec);
    uint16_t bits = audio_decoder_bits_per_sample(dec);
    uint32_t rate = audio_decoder_sample_rate(dec);
    uint16_t ch = audio_decoder_channels(dec);

    write_wav_header(wav, rate, ch, bits, num_samples);

    int32_t sample;
    uint8_t buf[4];
    for (uint32_t i = 0; i < num_samples; i++)
    {
        if (audio_decode_next(dec, &sample) != 0)
            break;
        if (bits == 16)
        {
            int16_t s = (int16_t)sample;
            fwrite(&s, 2, 1, wav);
        }
        else if (bits == 24)
        {
            buf[0] = sample & 0xFF;
            buf[1] = (sample >> 8) & 0xFF;
            buf[2] = (sample >> 16) & 0xFF;
            fwrite(buf, 3, 1, wav);
        }
        else if (bits == 32)
        {
            fwrite(&sample, 4, 1, wav);
        }
    }

    audio_decoder_close(dec);
    fclose(f);
    fclose(wav);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        printf("Usage:\n");
        printf("  %s encode <in.wav> <out.notflac> [predictor] [entropy]\n", argv[0]);
        printf("  %s decode <in.notflac> <out.wav>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "encode") == 0)
    {
        uint8_t pred = 0;
        uint8_t ent = 0;
        if (argc >= 5)
            pred = (uint8_t)atoi(argv[4]);
        if (argc >= 6)
            ent = (uint8_t)atoi(argv[5]);
        return do_encode(argv[2], argv[3], pred, ent);
    }
    else if (strcmp(argv[1], "decode") == 0)
    {
        return do_decode(argv[2], argv[3]);
    }
    else
    {
        printf("Unknown command: %s\n", argv[1]);
        return 1;
    }
}
