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

void slot_model_init(slot_model_t *m)
{
    for (int i = 0; i < 64; i++)
    {
        m->freq[i] = 1;
    }
    m->total = 64;
}

uint32_t slot_model_cum(slot_model_t *m, int symbol)
{
    uint32_t cum = 0;
    for (int i = 0; i < symbol; i++)
    {
        cum += m->freq[i];
    }
    return cum;
}

uint32_t slot_model_freq(slot_model_t *m, int symbol)
{
    return m->freq[symbol];
}

int slot_model_sym(slot_model_t *m, uint32_t target)
{
    uint32_t cum = 0;
    for (int i = 0; i < 64; i++)
    {
        cum += m->freq[i];
        if (target < cum)
        {
            return i;
        }
    }
    return 63;
}

void slot_model_update(slot_model_t *m, int symbol)
{
    m->freq[symbol]++;
    m->total++;
    if (m->total >= 65536)
    {
        m->total = 0;
        for (int i = 0; i < 64; i++)
        {
            m->freq[i] = (m->freq[i] + 1) / 2;
            m->total += m->freq[i];
        }
    }
}

static const uint32_t slot_base[64] = {
    0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192,
    256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192, 12288,
    16384, 24576, 32768, 49152, 65536, 98304, 131072, 196608, 262144, 393216,
    524288, 786432, 1048576, 1572864, 2097152, 3145728, 4194304, 6291456,
    8388608, 12582912, 16777216, 25165824, 33554432, 50331648, 67108864,
    100663296, 134217728, 201326592, 268435456, 402653184, 536870912,
    805306368, 1073741824, 1610612736, 2147483648, 3221225472
};

int slot(uint32_t dist)
{
    if (dist < 4)
    {
        return dist;
    }
    int s = 4;
    while (s < 63 && dist >= slot_base[s + 1])
    {
        s++;
    }
    return s;
}

uint32_t base_dist(int s)
{
    return slot_base[s];
}

int extra_bits(int s)
{
    if (s < 4)
    {
        return 0;
    }
    return (s >> 1) - 1;
}

void prec_model_init(prec_model_t *m)
{
    m->freq[15] = 3;
    m->freq[14] = 3;
    m->freq[13] = 3;
    m->freq[12] = 3;
    m->freq[11] = 2;
    for (int i = 0; i < 11; i++)
    {
        m->freq[i] = 1;
    }
    m->total = 25;
}

uint32_t prec_model_cum(prec_model_t *m, uint32_t symbol)
{
    uint32_t cum = 0;
    for (uint32_t i = 0; i < symbol; i++)
    {
        cum += m->freq[i];
    }
    return cum;
}

uint32_t prec_model_freq(prec_model_t *m, uint32_t symbol)
{
    return m->freq[symbol];
}

uint32_t prec_model_sym(prec_model_t *m, uint32_t target)
{
    uint32_t cum = 0;
    for (uint32_t i = 0; i < 16; i++)
    {
        cum += m->freq[i];
        if (target < cum)
        {
            return i;
        }
    }
    return 15;
}

