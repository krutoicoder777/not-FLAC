#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct range_decoder range_decoder_t;

struct range_decoder
{
    uint32_t low;
    uint32_t range;
    uint32_t code;
    FILE *in;
};

range_decoder_t *rd_init(FILE *in)
{
    int sym;
    range_decoder_t *rd = malloc(sizeof(range_decoder_t));
    if (rd)
    {
        rd->low = 0;
        rd->range = 0xFFFFFFFF;
        rd->code = 0;
        rd->in = in;
        if (fgetc(in) == EOF)
        {
            free(rd);
            return NULL;
        }
        for (size_t i = 0; i < 4; i++)
        {
            sym = fgetc(rd->in);
            if (sym == EOF)
            {
                free(rd);
                return NULL;
            }
            rd->code = (rd->code << 8) | (uint32_t)sym;
        }
    }
    return rd;
}

uint32_t rd_target(range_decoder_t *rd, uint32_t total)
{
    rd->range /= total;
    uint32_t res = (uint32_t)((rd->code - rd->low) / rd->range);
    return res;
}

void rd_update(range_decoder_t *rd, uint32_t cum_freq, uint32_t freq)
{
    int sym;
    rd->low = rd->low + cum_freq * rd->range;
    rd->range = freq * rd->range;
    while (rd->range < 1 << 24)
    {
        rd->range = rd->range << 8;
        rd->low = rd->low << 8;
        sym = fgetc(rd->in);
        if (sym == EOF)
        {
            sym = 0;
        }
        rd->code = (rd->code << 8) | (uint32_t)sym;
    }
}

void rd_destroy(range_decoder_t *rd)
{
    free(rd);
    return;
}
