#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include "wav_stream.h"

static double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int test_file(const char *path) {
    wav_stream_t *ws = wav_stream_open(path);
    if (!ws) { printf("  [SKIP] %s (open failed)\n", path); return 0; }

    uint32_t n = wav_stream_num_samples(ws);
    uint16_t bits = wav_stream_bits_per_sample(ws);
    uint32_t rate = wav_stream_sample_rate(ws);
    uint16_t ch = wav_stream_channels(ws);

    printf("  [OK] %s\n", path);
    printf("       Rate: %u Hz, Channels: %u, Bits: %u, Samples: %u\n", rate, ch, bits, n);

    double start = get_time();
    int32_t sample;
    int errors = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (wav_stream_next_sample(ws, &sample) != WAV_OK) { errors++; break; }
    }
    double end = get_time();

    printf("       Read time: %.3fs\n", end - start);

    wav_stream_close(ws);
    return errors > 0;
}

int main(void) {
    const char *data_dir = "tests/data";
    DIR *dir = opendir(data_dir);
    if (!dir) { printf("Cannot open %s\n", data_dir); return 1; }

    printf("=== WAV STREAM TEST ===\n\n");

    struct dirent *entry;
    int total = 0, failed = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", data_dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            if (test_file(path)) failed++;
            total++;
        }
    }
    closedir(dir);

    printf("\n=== %d files tested, %d failed ===\n", total, failed);
    return failed > 0;
}
