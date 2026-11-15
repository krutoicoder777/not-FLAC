#ifndef SIMPLE_PREDICTORS_H__
#define SIMPLE_PREDICTORS_H__

#include <stdint.h>

int32_t sp_decode_prediction(int order, const int32_t *history, int history_fill);

uint32_t sp_encode_elem(int order, const int32_t *samples, int idx);

#endif
