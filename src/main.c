#include "audio_decoder.h"
#include "audio_encoder.h"
#include "notflac_format.h"
#include "wav_writer.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s compress <in.wav> <out.notflac> [predictor]\n", prog);
    printf("  %s decompress <in.notflac> <out.wav>\n", prog);
    printf("  predictor: 0 = LPC (default: 0)\n");
}

static int do_compress(const char *in, const char *out, uint8_t pred)
{
    return encode_audio((predictor_method_t)pred, ENTROPY_RICE_CODER, in, out);
}

static int do_decompress(const char *in, const char *out)
{
    FILE *f;
    audio_decoder_t *dec;
    wav_writer_t *w;
    int n;
    const int32_t *samples;

    f = fopen(in, "rb");

    if (!f)
    {
        printf("Error: cannot open %s\n", in);
        return 1;
    }

    dec = audio_decoder_open(f);

    if (!dec)
    {
        printf("Error: invalid notflac file\n");
        fclose(f);
        return 1;
    }

    w = wav_writer_create(out,
                          audio_decoder_sample_rate(dec),
                          audio_decoder_channels(dec),
                          audio_decoder_bits_per_sample(dec),
                          audio_decoder_num_samples(dec));

    if (!w)
    {
        printf("Error: cannot create %s\n", out);
        audio_decoder_close(dec);
        fclose(f);
        return 1;
    }

    while ((samples = audio_decoder_decode_block(dec, &n)) != NULL)
    {
        wav_writer_write_block(w, samples, (uint32_t)n);

        if (audio_decoder_advance(dec) != 0)
        {
            break;
        }
    }

    wav_writer_destroy(w);
    audio_decoder_close(dec);
    fclose(f);

    return 0;
}

int main(int argc, char *argv[])
{
    uint8_t pred;

    if (argc < 4)
    {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "compress") == 0)
    {
        pred = PREDICTOR_LPC;

        if (argc >= 5)
        {
            pred = (uint8_t)atoi(argv[4]);

            if (pred != PREDICTOR_LPC && pred != PREDICTOR_FIXED)
            {
                printf("Error: invalid predictor '%s' (use 0 = LPC, 1 = Fixed)\n", argv[4]);
                return 1;
            }
        }

        return do_compress(argv[2], argv[3], pred);
    }
    else if (strcmp(argv[1], "decompress") == 0)
    {
        return do_decompress(argv[2], argv[3]);
    }
    else
    {
        printf("Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }
}
