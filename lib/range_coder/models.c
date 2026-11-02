#include "models.h"

void model_init(model_t *m)
{
    for (int i = 0; i < 256; i++)
    {
        m->freq[i] = 1;
    }
    m->total = 256;
}

uint32_t model_cum(model_t *m, int symbol)
{
    uint32_t cum = 0;
    for (int i = 0; i < symbol; i++)
    {
        cum += m->freq[i];
    }
    return cum;
}

uint32_t model_freq(model_t *m, int symbol)
{
    return m->freq[symbol];
}

int model_sym(model_t *m, uint32_t target)
{
    uint32_t cum = 0;
    for (int i = 0; i < 256; i++)
    {
        cum += m->freq[i];
        if (target < cum)
        {
            return i;
        }
    }
    return 255;
}

void model_update(model_t *m, int symbol)
{
    m->freq[symbol]++;
    m->total++;
    if (m->total >= MODEL_TOTAL_MAX)
    {
        m->total = 0;
        for (int i = 0; i < 256; i++)
        {
            m->freq[i] = (m->freq[i] + 1) / 2;
            m->total += m->freq[i];
        }
    }
}

void lit_model_init(lit_model_t *m)
{
    for (int i = 0; i < 256; i++)
    {
        model_init(&m->ctx[i]);
    }
}

uint32_t lit_model_cum(lit_model_t *m, uint8_t prev_byte, int symbol)
{
    return model_cum(&m->ctx[prev_byte], symbol);
}

uint32_t lit_model_freq(lit_model_t *m, uint8_t prev_byte, int symbol)
{
    return model_freq(&m->ctx[prev_byte], symbol);
}

int lit_model_sym(lit_model_t *m, uint8_t prev_byte, uint32_t target)
{
    return model_sym(&m->ctx[prev_byte], target);
}

void lit_model_update(lit_model_t *m, uint8_t prev_byte, int symbol)
{
    model_update(&m->ctx[prev_byte], symbol);
}
