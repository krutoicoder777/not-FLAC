#ifndef MODELS_H__
#define MODELS_H__

#include <stdint.h>

#define MODEL_TOTAL_MAX 65536

typedef struct
{
    uint16_t freq[256];
    uint32_t total;
} model_t;

void model_init(model_t *m);
uint32_t model_cum(model_t *m, int symbol);
uint32_t model_freq(model_t *m, int symbol);
int model_sym(model_t *m, uint32_t target);
void model_update(model_t *m, int symbol);

typedef struct
{
    model_t ctx[256];
} lit_model_t;

void lit_model_init(lit_model_t *m);
uint32_t lit_model_cum(lit_model_t *m, uint8_t prev_byte, int symbol);
uint32_t lit_model_freq(lit_model_t *m, uint8_t prev_byte, int symbol);
int lit_model_sym(lit_model_t *m, uint8_t prev_byte, uint32_t target);
void lit_model_update(lit_model_t *m, uint8_t prev_byte, int symbol);

#endif
