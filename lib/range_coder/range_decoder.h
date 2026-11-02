#ifndef RANGE_DECODER_H__
#define RANGE_DECODER_H__

#include <stdint.h>
#include <stdio.h>

typedef struct range_decoder range_decoder_t;

range_decoder_t *rd_init(FILE *in);
uint32_t rd_target(range_decoder_t *rd, uint32_t total);
void rd_update(range_decoder_t *rd, uint32_t cum_freq, uint32_t freq);
void rd_destroy(range_decoder_t *rd);

#endif
