#include "simple_predictors.h"
#include <stdint.h>

static uint32_t zigzag(int32_t n)
{
    return ((uint32_t)n << 1) ^ (uint32_t)(n >> 31);
}

uint32_t sp_encode_elem(int order, const int32_t *samples, int i)
{
    if (i < order)
    {
        return zigzag(samples[i]);
    }
    int64_t hat = 0;
    switch (order)
    {
    case 0:
        hat = 0;
        break;
    case 1:
        hat = samples[i - 1];
        break;
    case 2:
        hat = 2 * (int64_t)samples[i - 1] - samples[i - 2];
        break;
    case 3:
        hat = 3 * (int64_t)samples[i - 1] - 3 * (int64_t)samples[i - 2] + samples[i - 3];
        break;
    case 4:
        hat = 4 * (int64_t)samples[i - 1] - 6 * (int64_t)samples[i - 2]
            + 4 * (int64_t)samples[i - 3] - samples[i - 4];
        break;
    }
    return zigzag(samples[i] - (int32_t)hat);
}

int32_t sp_decode_prediction(int order, const int32_t *history, int count)
{
    if (count < order)
    {
        return 0;
    }
    int64_t hat = 0;
    switch (order)
    {
    case 0:
        hat = 0;
        break;
    case 1:
        hat = history[0];
        break;
    case 2:
        hat = 2 * (int64_t)history[0] - history[1];
        break;
    case 3:
        hat = 3 * (int64_t)history[0] - 3 * (int64_t)history[1] + history[2];
        break;
    case 4:
        hat = 4 * (int64_t)history[0] - 6 * (int64_t)history[1]
            + 4 * (int64_t)history[2] - history[3];
        break;
    }
    return (int32_t)hat;
}

uint64_t sp_cost(int order, const int32_t *samples, int n)
{
    uint64_t sum = 0;
    for (int i = 0; i < n; i++)
    {
        sum += sp_encode_elem(order, samples, i);
    }
    return sum;
}
