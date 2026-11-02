#ifndef RANGE_CODER_H__
#define RANGE_CODER_H__

#include <stdint.h>
#include <stdio.h>

typedef struct range_coder range_coder_t;

range_coder_t *rc_init(FILE *out);
void rc_update(range_coder_t *rc, uint32_t cum_freq, uint32_t freq, uint32_t total);
void rc_flush(range_coder_t *rc);
void rc_destroy(range_coder_t *rc);

#endif
