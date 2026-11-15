#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include "audio_encoder.h"
#include "audio_decoder.h"
#include "notflac_format.h"
#include "wav_stream.h"
#include "errors.h"

static double get_time(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static long get_file_size(const char *path)
{
    struct stat st;

    if (stat(path, &st) == 0)
    {
        return st.st_size;
    }

    return -1;
}

static int test_file(const char *path)
{
    long original_size;
    const char *combo_name;
    char comp[512 + 32];
    double start;
    int res;
    double enc_time;
    long comp_size;
    double ratio;
    FILE *in;
    audio_decoder_t *dec;
    wav_stream_t *ws;
    int errors;
    uint32_t n;
    uint32_t n_orig;
    uint32_t i;

    if (strstr(path, ".wav") == NULL)
    {
        return 0;
    }

    original_size = get_file_size(path);

    if (original_size <= 0)
    {
        printf("  [SKIP] %s\n", path);
        return 0;
    }

    printf("  [FILE] %s (%ld B)\n", path, original_size);

    combo_name = "LPC+Rice";
    snprintf(comp, sizeof(comp), "%s.notflac", path);

    start = get_time();
    res = encode_audio(PREDICTOR_LPC, ENTROPY_RICE_CODER, path, comp);
    enc_time = get_time() - start;
    comp_size = get_file_size(comp);

    if (res != OK || comp_size <= 0)
    {
        printf("    [FAIL] %-12s encode error\n", combo_name);
        remove(comp);
        return 1;
    }

    ratio = (double)comp_size / original_size * 100.0;
    in = fopen(comp, "rb");

    if (!in)
    {
        printf("    [FAIL] %-12s cannot open\n", combo_name);
        remove(comp);
        return 1;
    }

    dec = audio_decoder_open(in);

    if (!dec)
    {
        printf("    [FAIL] %-12s decoder open failed\n", combo_name);
        fclose(in);
        remove(comp);
        return 1;
    }

    ws = wav_stream_open(path);

    if (!ws)
    {
        printf("    [FAIL] %-12s wav open failed\n", combo_name);
        audio_decoder_close(dec);
        fclose(in);
        remove(comp);
        return 1;
    }

    errors = 0;
    n = audio_decoder_num_samples(dec);
    n_orig = wav_stream_num_samples(ws);

    if (n != n_orig)
    {
        printf("    [FAIL] %-12s num_samples %u vs %u\n", combo_name, n, n_orig);
        errors++;
    }
    else
    {
        for (i = 0; i < n; i++)
        {
            int32_t s_dec;
            int32_t s_orig;

            if (audio_decode_next(dec, &s_dec) != 0)
            {
                if (errors < 1)
                {
                    printf("    [FAIL] %-12s decode_next at %u\n", combo_name, i);
                }

                errors++;
                break;
            }

            if (wav_stream_next_sample(ws, &s_orig) != WAV_OK)
            {
                if (errors < 1)
                {
                    printf("    [FAIL] %-12s wav_stream at %u\n", combo_name, i);
                }

                errors++;
                break;
            }

            if (s_dec != s_orig)
            {
                if (errors < 1)
                {
                    printf("    [FAIL] %-12s mismatch at %u: %d vs %d\n", combo_name, i, s_dec, s_orig);
                }

                errors++;
            }
        }
    }

    wav_stream_close(ws);
    audio_decoder_close(dec);
    fclose(in);
    remove(comp);

    if (errors == 0)
    {
        printf("    [OK]   %-12s %7ld B (%5.1f%%) | E: %.3fs\n",
               combo_name, comp_size, ratio, enc_time);
        return 0;
    }

    printf("    [FAIL] %-12s round-trip (%d errors)\n", combo_name, errors);

    return 1;
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
            total++;

            if (test_file(path))
            {
                failed++;
            }
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
