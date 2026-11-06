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
    if (strstr(path, ".wav") == NULL)
    {
        return 0;
    }

    long original_size = get_file_size(path);
    if (original_size <= 0)
    {
        printf("  [SKIP] %s\n", path);
        return 0;
    }

    printf("  [FILE] %s (%ld B)\n", path, original_size);

    struct {
        const char *name;
        predictor_method_t pred;
        entropy_method_t ent;
    } combos[] = {
        {"LPC+Rice",    PREDICTOR_LPC,   ENTROPY_RICE_CODER},
        {"LPC+Range",   PREDICTOR_LPC,   ENTROPY_RANGE_CODER},
        {"Delta+Rice",  PREDICTOR_DELTA, ENTROPY_RICE_CODER},
        {"Delta+Range", PREDICTOR_DELTA, ENTROPY_RANGE_CODER},
    };
    int n_combos = (int)(sizeof(combos) / sizeof(combos[0]));
    int total_errors = 0;

    for (int c = 0; c < n_combos; c++)
    {
        char comp[512 + 32];
        snprintf(comp, sizeof(comp), "%s.%d_%d.notflac", path, (int)combos[c].pred, (int)combos[c].ent);

        double start = get_time();
        int res = encode_audio(combos[c].pred, combos[c].ent, path, comp);
        double enc_time = get_time() - start;
        long comp_size = get_file_size(comp);

        if (res != OK || comp_size <= 0)
        {
            printf("    [FAIL] %-12s encode error\n", combos[c].name);
            remove(comp);
            total_errors++;
            continue;
        }

        double ratio = (double)comp_size / original_size * 100.0;

        FILE *in = fopen(comp, "rb");
        if (!in)
        {
            printf("    [FAIL] %-12s cannot open\n", combos[c].name);
            remove(comp);
            total_errors++;
            continue;
        }
        audio_decoder_t *dec = audio_decoder_open(in);
        if (!dec)
        {
            printf("    [FAIL] %-12s decoder open failed\n", combos[c].name);
            fclose(in);
            remove(comp);
            total_errors++;
            continue;
        }
        wav_stream_t *ws = wav_stream_open(path);
        if (!ws)
        {
            printf("    [FAIL] %-12s wav open failed\n", combos[c].name);
            audio_decoder_close(dec);
            fclose(in);
            remove(comp);
            total_errors++;
            continue;
        }

        int errors = 0;
        uint32_t n = audio_decoder_num_samples(dec);
        uint32_t n_orig = wav_stream_num_samples(ws);
        if (n != n_orig)
        {
            printf("    [FAIL] %-12s num_samples %u vs %u\n", combos[c].name, n, n_orig);
            errors++;
        }
        else
        {
            for (uint32_t i = 0; i < n; i++)
            {
                int32_t s_dec, s_orig;
                if (audio_decode_next(dec, &s_dec) != 0)
                {
                    if (errors < 1) printf("    [FAIL] %-12s decode_next at %u\n", combos[c].name, i);
                    errors++;
                    break;
                }
                if (wav_stream_next_sample(ws, &s_orig) != WAV_OK)
                {
                    if (errors < 1) printf("    [FAIL] %-12s wav_stream at %u\n", combos[c].name, i);
                    errors++;
                    break;
                }
                if (s_dec != s_orig)
                {
                    if (errors < 1) printf("    [FAIL] %-12s mismatch at %u: %d vs %d\n", combos[c].name, i, s_dec, s_orig);
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
                   combos[c].name, comp_size, ratio, enc_time);
        }
        else
        {
            printf("    [FAIL] %-12s round-trip (%d errors)\n", combos[c].name, errors);
            total_errors++;
        }
    }

    return total_errors > 0;
}

int main(void)
{
    const char *data_dir = "tests/data";
    DIR *dir = opendir(data_dir);
    if (!dir)
    {
        printf("Cannot open %s\n", data_dir);
        return 1;
    }

    printf("=== NOTFLAC ROUNDTRIP TEST ===\n\n");
    struct dirent *entry;
    int total = 0;
    int failed = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
        {
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", data_dir, entry->d_name);
        struct stat st;
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
    printf("\n=== %d files tested, %d failed ===\n", total, failed);
    return failed > 0;
}
