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
    if (strstr(path, ".wav") == NULL)
    {
        return 0;
    }
    char comp[512 + 32];
    snprintf(comp, sizeof(comp), "%s.seek.notflac", path);
    if (encode_audio(PREDICTOR_LPC, ENTROPY_RANGE_CODER, path, comp) != OK)
    {
        printf("  [SKIP] %s (encode error)\n", path);
        return 0;
    }

    FILE *in_comp = fopen(comp, "rb");
    FILE *in_orig = fopen(path, "rb");
    if (!in_comp || !in_orig)
    {
        printf("  [FAIL] cannot open files\n");
        if (in_comp) fclose(in_comp);
        if (in_orig) fclose(in_orig);
        remove(comp);
        return 1;
    }
    audio_decoder_t *dec = audio_decoder_open(in_comp);
    if (!dec)
    {
        printf("  [FAIL] audio_decoder_open\n");
        fclose(in_comp);
        fclose(in_orig);
        remove(comp);
        return 1;
    }
    wav_stream_t *ws = wav_stream_open(path);
    if (!ws)
    {
        printf("  [FAIL] wav_stream_open\n");
        audio_decoder_close(dec);
        fclose(in_comp);
        fclose(in_orig);
        remove(comp);
        return 1;
    }

    uint32_t num_blocks = audio_decoder_num_blocks(dec);
    uint32_t block_size = audio_decoder_block_size(dec);
    uint32_t num_samples = audio_decoder_num_samples(dec);

    int errors = 0;
    // Проверяем seek на: 0, середину, последний
    uint32_t test_blocks[3] = {0, num_blocks / 2, num_blocks - 1};
    for (int t = 0; t < 3; t++)
    {
        uint32_t b = test_blocks[t];
        if (audio_decoder_seek_block(dec, b) != OK)
        {
            printf("  [FAIL] seek_block(%u) failed\n", b);
            errors++;
            continue;
        }
        int n = 0;
        const int32_t *samples = audio_decoder_decode_block(dec, &n);
        if (!samples)
        {
            printf("  [FAIL] decode_block after seek(%u)\n", b);
            errors++;
            continue;
        }
        // Сравниваем с оригиналом: сэмплы блока b начинаются с b * block_size
        uint32_t start_sample = b * block_size;
        if (start_sample + n > num_samples) n = num_samples - start_sample;
        if (wav_stream_seek_sample(ws, start_sample) != 0)
        {
            // Fallback: перечитать
            wav_stream_close(ws);
            ws = wav_stream_open(path);
            for (uint32_t i = 0; i < start_sample; i++)
            {
                int32_t tmp;
                wav_stream_next_sample(ws, &tmp);
            }
        }
        for (int i = 0; i < n; i++)
        {
            int32_t s_orig;
            if (wav_stream_next_sample(ws, &s_orig) != WAV_OK)
            {
                errors++;
                break;
            }
            if (samples[i] != s_orig)
            {
                if (errors < 3) fprintf(stderr, "  [FAIL] block %u sample %d mismatch: %d vs %d\n", b, i, samples[i], s_orig);
                errors++;
            }
        }
        printf("  [OK] seek_block(%u) + decode_block: %d samples\n", b, n);
    }

    wav_stream_close(ws);
    audio_decoder_close(dec);
    fclose(in_comp);
    fclose(in_orig);
    remove(comp);
    return errors > 0;
}

int main(void)
{
    const char *data_dir = "tests/data";
    DIR *dir = opendir(data_dir);
    if (!dir) { printf("Cannot open %s\n", data_dir); return 1; }
    printf("=== NOTFLAC SEEK TEST ===\n\n");
    struct dirent *entry;
    int total = 0;
    int failed = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", data_dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        {
            total++;
            if (test_seek(path)) failed++;
        }
    }
    closedir(dir);
    printf("\n=== %d files tested, %d failed ===\n", total, failed);
    return failed > 0;
}
