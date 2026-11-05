#include "wav_block.h"
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static int test_file(const char *path)
{
    wav_block_reader_t *wb = wav_block_reader_create(path, 4096);
    if (!wb)
    {
        printf("  [SKIP] %s\n", path);
        return 0;
    }

    uint32_t num_blocks = wav_block_reader_num_blocks(wb);
    uint32_t total_samples = 0;
    const int32_t *block;

    while ((block = wav_block_reader_next(wb)) != NULL)
    {
        int n = wav_block_reader_count(wb);
        if (n == 0)
            break;
        total_samples += n;
    }

    printf("  [OK] %s: %u blocks, %u samples\n", path, num_blocks, total_samples);

    wav_block_reader_destroy(wb);
    return 0;
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

    printf("=== WAV BLOCK TEST ===\n\n");
    struct dirent *entry;
    int total = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", data_dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        {
            test_file(path);
            total++;
        }
    }
    closedir(dir);

    printf("\n=== %d files tested ===\n", total);
    return 0;
}
