#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "audio_encoder.h"
#include "notflac_format.h"
#include "wav_stream.h"
#include "dz_processor.h"
#include "range_coder.h"
#include "models.h"

int encode_audio(predictor_method_t predictor, entropy_method_t entropy, const char *in_path, const char *out_path)
{
    if (predictor != PREDICTOR_DELTA || entropy != ENTROPY_RANGE_CODER) {
        return -1;
    }

    wav_stream_t *ws = wav_stream_open(in_path);
    if (!ws) return -1;

    uint32_t num_samples = wav_stream_num_samples(ws);
    uint16_t bits = wav_stream_bits_per_sample(ws);
    uint32_t sample_rate = wav_stream_sample_rate(ws);
    uint16_t channels = wav_stream_channels(ws);

    FILE *out = fopen(out_path, "wb");
    if (!out) { wav_stream_close(ws); return -1; }

    uint32_t magic = NOTFLAC_MAGIC;
    uint8_t version = NOTFLAC_VERSION;
    uint8_t entropy_byte = (uint8_t)entropy;
    uint8_t predictor_byte = (uint8_t)predictor;
    uint8_t reserved = 0;

    fwrite(&magic, 4, 1, out);
    fwrite(&version, 1, 1, out);
    fwrite(&entropy_byte, 1, 1, out);
    fwrite(&predictor_byte, 1, 1, out);
    fwrite(&reserved, 1, 1, out);
    fwrite(&sample_rate, 4, 1, out);
    fwrite(&channels, 2, 1, out);
    fwrite(&bits, 2, 1, out);
    fwrite(&num_samples, 4, 1, out);

    range_coder_t *rc = rc_init(out);
    if (!rc) { fclose(out); wav_stream_close(ws); return -1; }

    model_t high_model, low_model;
    model_init(&high_model);
    model_init(&low_model);

    dz_encoder_t *enc = dze_init(bits);
    if (!enc) { rc_destroy(rc); fclose(out); wav_stream_close(ws); return -1; }

    int32_t sample;
    for (uint32_t i = 0; i < num_samples; i++)
    {
        if (wav_stream_next_sample(ws, &sample) != WAV_OK) break;

        uint32_t encoded = dze_next(enc, sample);
        uint8_t high = (encoded >> 8) & 0xFF;
        uint8_t low  = encoded & 0xFF;

        rc_update(rc, model_cum(&high_model, high), model_freq(&high_model, high), high_model.total);
        model_update(&high_model, high);

        rc_update(rc, model_cum(&low_model, low), model_freq(&low_model, low), low_model.total);
        model_update(&low_model, low);
    }

    rc_flush(rc);
    dze_destroy(enc);
    rc_destroy(rc);
    wav_stream_close(ws);
    fclose(out);

    return 0;
}
