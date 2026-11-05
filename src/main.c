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
    printf("  %s compress <in.wav> <out.notflac> [predictor] [entropy]\n", prog);
    printf("  %s decompress <in.notflac> <out.wav>\n", prog);
    printf("  predictor: 0 = Delta, 1 = LPC (default: 1)\n");
    printf("  entropy:   0 = Range Coder (default: 0)\n");
}

static int do_compress(const char *in, const char *out, uint8_t pred, uint8_t ent)
{
    return encode_audio((predictor_method_t)pred, (entropy_method_t)ent, in, out);
}

static int do_decompress(const char *in, const char *out)
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
    wav_writer_t *w = wav_writer_create(out, audio_decoder_sample_rate(dec), audio_decoder_channels(dec),
                                        audio_decoder_bits_per_sample(dec), audio_decoder_num_samples(dec));
    if (!w)
    {
        printf("Error: cannot create %s\n", out);
        audio_decoder_close(dec);
        fclose(f);
        return 1;
    }

    int32_t sample;
    while (audio_decode_next(dec, &sample) == 0)
    {
        wav_writer_write_sample(w, sample);
    }

    wav_writer_destroy(w);
    audio_decoder_close(dec);
    fclose(f);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        print_usage(argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "compress") == 0)
    {
        uint8_t pred = PREDICTOR_LPC;
        uint8_t ent = ENTROPY_RANGE_CODER;
        if (argc >= 5)
            pred = (uint8_t)atoi(argv[4]);
        if (argc >= 6)
            ent = (uint8_t)atoi(argv[5]);
        return do_compress(argv[2], argv[3], pred, ent);
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
