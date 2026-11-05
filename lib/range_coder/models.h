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

typedef struct
{
    uint16_t freq[64];
    uint32_t total;
} slot_model_t;

void slot_model_init(slot_model_t *m);
uint32_t slot_model_cum(slot_model_t *m, int symbol);
uint32_t slot_model_freq(slot_model_t *m, int symbol);
int slot_model_sym(slot_model_t *m, uint32_t target);
void slot_model_update(slot_model_t *m, int symbol);

int slot(uint32_t dist);
int extra_bits(int slot);
uint32_t base_dist(int slot);

typedef struct
{
	uint32_t freq[16];
	uint32_t total;
}prec_model_t;

void prec_model_init(prec_model_t *m);
uint32_t prec_model_cum(prec_model_t *m, uint32_t  symbol);
uint32_t prec_model_freq(prec_model_t *m, uint32_t symbol);
uint32_t prec_model_sym(prec_model_t *m, uint32_t target);

#endif
