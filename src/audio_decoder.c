#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "audio_decoder.h"
#include "notflac_format.h"
#include "dz_processor.h"
#include "range_decoder.h"
#include "models.h"

struct audio_decoder {
    range_decoder_t *rd;
    dz_decoder_t *dzd;
    model_t high_model;
    model_t low_model;
    uint32_t num_samples;
    uint32_t samples_read;
    uint16_t bits_per_sample;
    uint32_t sample_rate;
    uint16_t channels;
};

audio_decoder_t *audio_decoder_open(FILE *in)
{
    uint32_t magic;
    uint8_t version, entropy, predictor, reserved;
    uint32_t sample_rate, num_samples;
    uint16_t channels, bits;

    if (fread(&magic, 4, 1, in) != 1) return NULL;
    if (magic != NOTFLAC_MAGIC) return NULL;

    if (fread(&version, 1, 1, in) != 1) return NULL;
    if (version != NOTFLAC_VERSION) return NULL;

    if (fread(&entropy, 1, 1, in) != 1) return NULL;
    if (fread(&predictor, 1, 1, in) != 1) return NULL;
    if (fread(&reserved, 1, 1, in) != 1) return NULL;

    if (entropy != ENTROPY_RANGE_CODER || predictor != PREDICTOR_DELTA) return NULL;

    if (fread(&sample_rate, 4, 1, in) != 1) return NULL;
    if (fread(&channels, 2, 1, in) != 1) return NULL;
    if (fread(&bits, 2, 1, in) != 1) return NULL;
    if (fread(&num_samples, 4, 1, in) != 1) return NULL;

    audio_decoder_t *dec = malloc(sizeof(audio_decoder_t));
    if (!dec) return NULL;

    dec->rd = rd_init(in);
    dec->dzd = dzd_init(bits);
    if (!dec->rd || !dec->dzd) {
        free(dec);
        return NULL;
    }

    model_init(&dec->high_model);
    model_init(&dec->low_model);

    dec->num_samples = num_samples;
    dec->samples_read = 0;
    dec->bits_per_sample = bits;
    dec->sample_rate = sample_rate;
    dec->channels = channels;

    return dec;
}

uint32_t audio_decoder_sample_rate(audio_decoder_t *dec) { return dec->sample_rate; }
uint16_t audio_decoder_channels(audio_decoder_t *dec) { return dec->channels; }
uint16_t audio_decoder_bits_per_sample(audio_decoder_t *dec) { return dec->bits_per_sample; }
uint32_t audio_decoder_num_samples(audio_decoder_t *dec) { return dec->num_samples; }

int audio_decode_next(audio_decoder_t *dec, int32_t *sample)
{
    if (dec->samples_read >= dec->num_samples) return -1;

    uint32_t target = rd_target(dec->rd, dec->high_model.total);
    int high = model_sym(&dec->high_model, target);
    rd_update(dec->rd, model_cum(&dec->high_model, high), model_freq(&dec->high_model, high));
    model_update(&dec->high_model, high);

    target = rd_target(dec->rd, dec->low_model.total);
    int low = model_sym(&dec->low_model, target);
    rd_update(dec->rd, model_cum(&dec->low_model, low), model_freq(&dec->low_model, low));
    model_update(&dec->low_model, low);

    uint32_t encoded = (high << 8) | low;
    *sample = dzd_next(dec->dzd, encoded);
    
    dec->samples_read++;
    return 0;
}

void audio_decoder_close(audio_decoder_t *dec)
{
    if (dec) {
        rd_destroy(dec->rd);
        dzd_destroy(dec->dzd);
        free(dec);
    }
}
