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

static double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int compare_samples(const char *f1, const char *f2) {
    wav_stream_t *a = wav_stream_open(f1);
    wav_stream_t *b = wav_stream_open(f2);
    if (!a || !b) { if(a) wav_stream_close(a); if(b) wav_stream_close(b); return 1; }

    uint32_t na = wav_stream_num_samples(a);
    uint32_t nb = wav_stream_num_samples(b);
    if (na != nb) {
        printf("  Sample count mismatch: %u vs %u\n", na, nb);
        wav_stream_close(a);
        wav_stream_close(b);
        return 1;
    }

    int errors = 0;
    int32_t sa, sb;
    for (uint32_t i = 0; i < na; i++) {
        if (wav_stream_next_sample(a, &sa) != WAV_OK) { errors++; break; }
        if (wav_stream_next_sample(b, &sb) != WAV_OK) { errors++; break; }
        if (sa != sb) {
            if (errors < 5) printf("  Mismatch at sample %u: %d vs %d\n", i, sa, sb);
            errors++;
        }
    }

    wav_stream_close(a);
    wav_stream_close(b);
    return errors > 0;
}

static void write_wav_header(FILE *out, uint32_t rate, uint16_t ch, uint16_t bits, uint32_t num_samples) {
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

static int test_file(const char *path) {
    char comp_path[512], restored_path[512];
    snprintf(comp_path, sizeof(comp_path), "%s.notflac", path);
    snprintf(restored_path, sizeof(restored_path), "%s.restored.wav", path);

    wav_stream_t *ws = wav_stream_open(path);
    if (!ws) { printf("  [SKIP] %s\n", path); return 0; }
    uint32_t num_samples = wav_stream_num_samples(ws);
    uint16_t bits = wav_stream_bits_per_sample(ws);
    uint32_t rate = wav_stream_sample_rate(ws);
    uint16_t ch = wav_stream_channels(ws);
    wav_stream_close(ws);

    double start = get_time();
    int res = encode_audio(PREDICTOR_DELTA, ENTROPY_RANGE_CODER, path, comp_path);
    double comp_time = get_time() - start;

    if (res != 0) {
        printf("  [FAIL] %s (encode error)\n", path);
        remove(comp_path);
        return 1;
    }

    FILE *in = fopen(comp_path, "rb");
    FILE *out = fopen(restored_path, "wb");
    if (!in || !out) { if(in)fclose(in); if(out)fclose(out); remove(comp_path); return 0; }

    audio_decoder_t *dec = audio_decoder_open(in);
    if (!dec) { printf("  [FAIL] %s (decoder open)\n", path); fclose(in); fclose(out); remove(comp_path); return 1; }

    write_wav_header(out, rate, ch, bits, num_samples);

    start = get_time();
    int32_t sample;
    uint8_t buf[4];
    int decode_errors = 0;

    for (uint32_t i = 0; i < num_samples; i++) {
        if (audio_decode_next(dec, &sample) != 0) { decode_errors++; break; }
        if (bits == 16) { int16_t s = (int16_t)sample; fwrite(&s, 2, 1, out); }
        else if (bits == 24) { buf[0] = sample & 0xFF; buf[1] = (sample >> 8) & 0xFF; buf[2] = (sample >> 16) & 0xFF; fwrite(buf, 3, 1, out); }
        else if (bits == 32) { fwrite(&sample, 4, 1, out); }
    }
    double decomp_time = get_time() - start;

    audio_decoder_close(dec);
    fclose(in);
    fclose(out);

    int diff = compare_samples(path, restored_path);

    if (diff == 0 && decode_errors == 0) {
        printf("  [OK] %s | C: %.3fs, D: %.3fs\n", path, comp_time, decomp_time);
    } else {
        printf("  [FAIL] %s | diff=%d, decode_errors=%d\n", path, diff, decode_errors);
    }

    remove(comp_path);
    remove(restored_path);
    return diff != 0 || decode_errors > 0;
}

int main(void) {
    const char *data_dir = "tests/data";
    DIR *dir = opendir(data_dir);
    if (!dir) { printf("Cannot open %s\n", data_dir); return 1; }

    printf("=== NOTFLAC ROUNDTRIP TEST ===\n\n");
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
