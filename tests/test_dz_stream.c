#include "dz_processor.h"
#include "wav_stream.h"
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static double get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int test_file(const char *path)
{
    wav_stream_t *ws = wav_stream_open(path);
    if (!ws)
    {
        printf("  [SKIP] %s (open failed)\n", path);
        return 0;
    }

    uint32_t n = wav_stream_num_samples(ws);
    uint16_t bits = wav_stream_bits_per_sample(ws);

    dz_encoder_t *enc = dze_init(bits);
    dz_decoder_t *dec = dzd_init(bits);
    if (!enc || !dec)
    {
        printf("  [FAIL] %s (init failed)\n", path);
        wav_stream_close(ws);
        if (enc)
            dze_destroy(enc);
        if (dec)
            dzd_destroy(dec);
        return 1;
    }

    double start = get_time();
    int errors = 0;
    int32_t original_sample, decoded_sample;
    uint32_t encoded;

    for (uint32_t i = 0; i < n; i++)
    {
        if (wav_stream_next_sample(ws, &original_sample) != WAV_OK)
        {
            errors++;
            break;
        }
        encoded = dze_next(enc, original_sample);
        decoded_sample = dzd_next(dec, encoded);
        if (original_sample != decoded_sample)
        {
            if (errors < 5)
                printf("  Mismatch at %u: %d -> %u -> %d\n", i, original_sample, encoded, decoded_sample);
            errors++;
        }
    }
    double end = get_time();

    wav_stream_close(ws);
    dze_destroy(enc);
    dzd_destroy(dec);

    if (errors == 0)
    {
        printf("  [OK] %s (%u samples, %.3fs)\n", path, n, end - start);
        return 0;
    }
    else
    {
        printf("  [FAIL] %s (%d errors)\n", path, errors);
        return 1;
    }
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

    printf("=== DZ STREAM ROUNDTRIP TEST ===\n\n");

    struct dirent *entry;
    int total = 0, failed = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", data_dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        {
            if (test_file(path))
                failed++;
            total++;
        }
    }
    closedir(dir);

    printf("\n=== %d files tested, %d failed ===\n", total, failed);
    return failed > 0;
}
