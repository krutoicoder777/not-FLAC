#ifndef SIMPLE_PREDICTORS_H__
#define SIMPLE_PREDICTORS_H__

#include <stdint.h>

uint32_t sp_encode_elem(int order, const int32_t *samples, int i);
int32_t sp_decode_prediction(int order, const int32_t *history, int count);
uint64_t sp_cost(int order, const int32_t *samples, int n);

#endif
