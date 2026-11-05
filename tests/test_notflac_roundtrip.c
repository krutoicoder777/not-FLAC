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
    char comp_lpc[512 + 32], comp_delta[512 + 32];
    snprintf(comp_lpc, sizeof(comp_lpc), "%s.lpc.notflac", path);
    snprintf(comp_delta, sizeof(comp_delta), "%s.delta.notflac", path);

    long original_size = get_file_size(path);
    if (original_size <= 0)
    {
        printf("  [SKIP] %s\n", path);
        return 0;
    }

    double start = get_time();
    int res_lpc = encode_audio(PREDICTOR_LPC, ENTROPY_RANGE_CODER, path, comp_lpc);
    double lpc_time = get_time() - start;
    long lpc_size = get_file_size(comp_lpc);

    start = get_time();
    int res_delta = encode_audio(PREDICTOR_DELTA, ENTROPY_RANGE_CODER, path, comp_delta);
    double delta_time = get_time() - start;
    long delta_size = get_file_size(comp_delta);

    if (res_lpc != OK || res_delta != OK)
    {
        printf("  [FAIL] %s (encode error)\n", path);
        remove(comp_lpc);
        remove(comp_delta);
        return 1;
    }

    double lpc_ratio = (double)lpc_size / original_size * 100.0;
    double delta_ratio = (double)delta_size / original_size * 100.0;
    double improvement = (double)(delta_size - lpc_size) / delta_size * 100.0;

    printf("  [OK] %s\n", path);
    printf("       Original: %ld B\n", original_size);
    printf("       LPC:   %ld B (%.1f%%) | C: %.3fs\n", lpc_size, lpc_ratio, lpc_time);
    printf("       Delta: %ld B (%.1f%%) | C: %.3fs\n", delta_size, delta_ratio, delta_time);
    printf("       LPC vs Delta: %.1f%% smaller\n", improvement);

    // Round-trip LPC
    int errors = 0;
    FILE *in = fopen(comp_lpc, "rb");
    if (!in)
    {
        printf("  [FAIL] cannot open %s\n", comp_lpc);
        remove(comp_lpc);
        remove(comp_delta);
        return 1;
    }
    audio_decoder_t *dec = audio_decoder_open(in);
    if (!dec)
    {
        printf("  [FAIL] audio_decoder_open failed\n");
        fclose(in);
        remove(comp_lpc);
        remove(comp_delta);
        return 1;
    }
    wav_stream_t *ws = wav_stream_open(path);
    if (!ws)
    {
        printf("  [FAIL] wav_stream_open failed\n");
        audio_decoder_close(dec);
        fclose(in);
        remove(comp_lpc);
        remove(comp_delta);
        return 1;
    }

    uint32_t n = audio_decoder_num_samples(dec);
    uint32_t n_orig = wav_stream_num_samples(ws);
    if (n != n_orig)
    {
        printf("  [FAIL] num_samples mismatch: %u vs %u\n", n, n_orig);
        errors++;
    }
    else
    {
        for (uint32_t i = 0; i < n; i++)
        {
            int32_t s_dec, s_orig;
            if (audio_decode_next(dec, &s_dec) != 0)
            {
                if (errors < 3) fprintf(stderr, "  [FAIL] decode_next at %u\n", i);
                errors++;
                break;
            }
            if (wav_stream_next_sample(ws, &s_orig) != WAV_OK)
            {
                if (errors < 3) fprintf(stderr, "  [FAIL] wav_stream at %u\n", i);
                errors++;
                break;
            }
            if (s_dec != s_orig)
            {
                if (errors < 3) fprintf(stderr, "  [FAIL] mismatch at %u: %d vs %d\n", i, s_dec, s_orig);
                errors++;
            }
        }
    }

    wav_stream_close(ws);
    audio_decoder_close(dec);
    fclose(in);

    if (errors == 0)
    {
        printf("       Round-trip: SUCCESS\n");
    }
    else
    {
        printf("       Round-trip: FAILED (%d errors)\n", errors);
    }

    remove(comp_lpc);
    remove(comp_delta);
    return errors > 0;
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
