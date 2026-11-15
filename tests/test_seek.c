#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "audio_encoder.h"
#include "audio_decoder.h"
#include "notflac_format.h"
#include "wav_stream.h"
#include "errors.h"

static int test_seek(const char *path)
{
    char comp[512 + 32];
    FILE *in_comp;
    audio_decoder_t *dec;
    wav_stream_t *ws;
    uint32_t num_blocks;
    uint32_t block_size;
    uint32_t num_samples;
    uint32_t test_blocks[3];
    int errors;
    int t;

    if (strstr(path, ".wav") == NULL)
    {
        return 0;
    }

    snprintf(comp, sizeof(comp), "%s.seek.notflac", path);

    if (encode_audio(PREDICTOR_LPC, ENTROPY_RICE_CODER, path, comp) != OK)
    {
        printf("  [SKIP] %s (encode error)\n", path);
        return 0;
    }

    in_comp = fopen(comp, "rb");

    if (!in_comp)
    {
        printf("  [FAIL] %s (cannot open)\n", path);
        remove(comp);
        return 1;
    }

    dec = audio_decoder_open(in_comp);

    if (!dec)
    {
        printf("  [FAIL] %s (audio_decoder_open)\n", path);
        fclose(in_comp);
        remove(comp);
        return 1;
    }

    ws = wav_stream_open(path);

    if (!ws)
    {
        printf("  [FAIL] %s (wav_stream_open)\n", path);
        audio_decoder_close(dec);
        fclose(in_comp);
        remove(comp);
        return 1;
    }

    num_blocks = audio_decoder_num_blocks(dec);
    block_size = audio_decoder_block_size(dec);
    num_samples = audio_decoder_num_samples(dec);

    errors = 0;
    test_blocks[0] = 0;
    test_blocks[1] = num_blocks / 2;
    test_blocks[2] = num_blocks - 1;

    for (t = 0; t < 3; t++)
    {
        uint32_t b;
        uint32_t start_sample;
        int n;
        const int32_t *samples;
        int i;

        b = test_blocks[t];

        if (audio_decoder_seek_block(dec, b) != OK)
        {
            printf("  [FAIL] %s seek_block(%u)\n", path, b);
            errors++;
            continue;
        }

        n = 0;
        samples = audio_decoder_decode_block(dec, &n);

        if (!samples)
        {
            printf("  [FAIL] %s decode_block after seek(%u)\n", path, b);
            errors++;
            continue;
        }

        start_sample = b * block_size;

        if (start_sample + (uint32_t)n > num_samples)
        {
            n = (int)(num_samples - start_sample);
        }

        if (wav_stream_seek_sample(ws, start_sample) != 0)
        {
            uint32_t k;
            int32_t tmp;

            wav_stream_close(ws);
            ws = wav_stream_open(path);

            for (k = 0; k < start_sample; k++)
            {
                wav_stream_next_sample(ws, &tmp);
            }
        }

        for (i = 0; i < n; i++)
        {
            int32_t s_orig;

            if (wav_stream_next_sample(ws, &s_orig) != WAV_OK)
            {
                errors++;
                break;
            }

            if (samples[i] != s_orig)
            {
                if (errors < 3)
                {
                    printf("  [FAIL] %s block %u sample %d: %d vs %d\n",
                           path, b, i, samples[i], s_orig);
                }

                errors++;
            }
        }
    }

    wav_stream_close(ws);
    audio_decoder_close(dec);
    fclose(in_comp);
    remove(comp);

    return errors > 0;
}

static int test_advance(const char *path)
{
    char comp[512 + 32];
    FILE *in_comp;
    audio_decoder_t *dec;
    uint32_t num_blocks;
    uint32_t b;
    int errors;

    if (strstr(path, ".wav") == NULL)
    {
        return 0;
    }

    snprintf(comp, sizeof(comp), "%s.advance.notflac", path);

    if (encode_audio(PREDICTOR_LPC, ENTROPY_RICE_CODER, path, comp) != OK)
    {
        return 0;
    }

    in_comp = fopen(comp, "rb");

    if (!in_comp)
    {
        remove(comp);
        return 1;
    }

    dec = audio_decoder_open(in_comp);

    if (!dec)
    {
        fclose(in_comp);
        remove(comp);
        return 1;
    }

    num_blocks = audio_decoder_num_blocks(dec);
    errors = 0;
    b = 0;

    while (b < num_blocks)
    {
        int n;
        const int32_t *samples;

        samples = audio_decoder_decode_block(dec, &n);

        if (!samples)
        {
            printf("  [FAIL] %s decode_block at block %u\n", path, b);
            errors++;
            break;
        }

        b++;

        if (b < num_blocks)
        {
            if (audio_decoder_advance(dec) != OK)
            {
                printf("  [FAIL] %s advance failed at block %u\n", path, b);
                errors++;
                break;
            }
        }
    }

    if (b != num_blocks)
    {
        printf("  [FAIL] %s advance stopped at %u/%u\n", path, b, num_blocks);
        errors++;
    }

    audio_decoder_close(dec);
    fclose(in_comp);
    remove(comp);

    return errors > 0;
}

static void run_dir(const char *label, const char *data_dir)
{
    DIR *dir;
    struct dirent *entry;
    int total;
    int failed;

    dir = opendir(data_dir);

    if (!dir)
    {
        printf("Cannot open %s\n", data_dir);
        return;
    }

    printf("########################################################################\n");
    printf("# %s (%s)\n", label, data_dir);
    printf("########################################################################\n\n");

    total = 0;
    failed = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        char path[512];
        struct stat st;

        if (entry->d_name[0] == '.')
        {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", data_dir, entry->d_name);

        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        {
            int seek_err;
            int adv_err;

            printf("  [FILE] %s\n", path);

            seek_err = test_seek(path);
            adv_err = test_advance(path);

            if (seek_err == 0 && adv_err == 0)
            {
                printf("    [OK]\n");
            }
            else
            {
                failed++;
            }

            total++;
        }
    }

    closedir(dir);
    printf("\n=== %s: %d files tested, %d failed ===\n\n", label, total, failed);
}

int main(void)
{
    run_dir("REAL", "tests/data/real");
    run_dir("SYNTH", "tests/data/synth");

    return 0;
}
