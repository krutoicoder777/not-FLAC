#ifndef ENTROPY_ENCODER_H__
#define ENTROPY_ENCODER_H__

#include <stdio.h>
#include <stdint.h>

typedef struct entropy_encoder entropy_encoder_t;

entropy_encoder_t *entropy_create_range(FILE *out);
void entropy_destroy(entropy_encoder_t *enc);

void entropy_encode_sample(entropy_encoder_t *enc, uint32_t sample); //sample is already zigzag`ed
void entropy_encode_precision(entropy_encoder_t *enc, uint8_t precision);
void entropy_encode_byte(entropy_encoder_t *enc, uint8_t byte);
void entropy_encode_quotient(entropy_encoder_t *enc, int32_t quotient); //quotient is not zigzag`ed

#endif
