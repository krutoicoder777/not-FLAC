#include "range_coder.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct range_coder range_coder_t;

struct range_coder
{
    uint64_t low;
    uint32_t range;
    uint8_t pending_byte;
    uint32_t pending_count;
    FILE *out;
};

range_coder_t *rc_init(FILE *out)
{
    range_coder_t *rc = malloc(sizeof(range_coder_t));
    if (rc)
    {
        rc->low = 0;
        rc->range = 0xFFFFFFFF;
        rc->pending_byte = 0;
        rc->pending_count = 1;
        rc->out = out;
    }
    return rc;
}

static void shift_low(range_coder_t *rc)
{
    if ((uint32_t)rc->low < 0xFF000000u || (rc->low >> 32) != 0)
    {
        uint8_t carry = (rc->low >> 32) & 1;
        do
        {
            fputc(rc->pending_byte + carry, rc->out);
            rc->pending_byte = 0xFF;
        } while (--rc->pending_count > 0);
        rc->pending_byte = (rc->low >> 24) & 0xFF;
    }
    rc->pending_count++;
    rc->low = ((uint32_t)rc->low << 8);
}

void rc_update(range_coder_t *rc, uint32_t cum_freq, uint32_t freq, uint32_t total)
{
    uint32_t step = rc->range / total;
    rc->low = rc->low + cum_freq * step;
    rc->range = freq * step;
    while (rc->range < 1 << 24)
    {
        shift_low(rc);
        rc->range <<= 8;
    }
}

void rc_flush(range_coder_t *rc)
{
    for (int i = 0; i < 5; i++)
    {
        shift_low(rc);
    }
    fflush(rc->out);
}

void rc_destroy(range_coder_t *rc)
{
    free(rc);
}
