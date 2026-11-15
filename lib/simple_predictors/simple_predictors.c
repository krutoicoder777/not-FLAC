#include "simple_predictors.h"
#include <stdint.h>

static uint32_t zigzag(int32_t n)
{
    return ((uint32_t)n << 1) ^ (uint32_t)(n >> 31);
}

static int32_t predict_fixed(int order, const int32_t *history)
{
    if (order == 0)
    {
        return 0;
    }

    if (order == 1)
    {
        return history[0];
    }

    if (order == 2)
    {
        return 2 * history[0] - history[1];
    }

    if (order == 3)
    {
        return 3 * history[0] - 3 * history[1] + history[2];
    }

    if (order == 4)
    {
        return 4 * history[0] - 6 * history[1] + 4 * history[2] - history[3];
    }

    {
        int32_t h0 = history[0];
        int32_t h1 = history[1];
        int32_t h2 = history[2];
        int32_t h3 = history[3];
        int32_t h4 = history[4];

        if (order == 5)
        {
            return 5 * h0 - 10 * h1 + 10 * h2 - 5 * h3 + h4;
        }

        {
            int32_t h5 = history[5];
            return 6 * h0 - 15 * h1 + 20 * h2 - 15 * h3 + 6 * h4 - h5;
        }
    }
}

int32_t sp_decode_prediction(int order, const int32_t *history, int history_fill)
{
    if (order <= 0 || history_fill < order)
    {
        return 0;
    }

    return predict_fixed(order, history);
}

uint32_t sp_encode_elem(int order, const int32_t *samples, int idx)
{
    int32_t hat;
    int32_t residual;

    if (idx < order)
    {
        return zigzag(samples[idx]);
    }

    if (order == 0)
    {
        hat = 0;
    }
    else if (order == 1)
    {
        hat = samples[idx - 1];
    }
    else if (order == 2)
    {
        hat = 2 * samples[idx - 1] - samples[idx - 2];
    }
    else if (order == 3)
    {
        hat = 3 * samples[idx - 1] - 3 * samples[idx - 2] + samples[idx - 3];
    }
    else if (order == 4)
    {
        hat = 4 * samples[idx - 1] - 6 * samples[idx - 2] + 4 * samples[idx - 3] - samples[idx - 4];
    }
    else if (order == 5)
    {
        hat = 5 * samples[idx - 1] - 10 * samples[idx - 2] + 10 * samples[idx - 3]
            - 5 * samples[idx - 4] + samples[idx - 5];
    }
    else
    {
        hat = 6 * samples[idx - 1] - 15 * samples[idx - 2] + 20 * samples[idx - 3]
            - 15 * samples[idx - 4] + 6 * samples[idx - 5] - samples[idx - 6];
    }

    residual = samples[idx] - hat;

    return zigzag(residual);
}
