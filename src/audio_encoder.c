#include "audio_encoder.h"
#include "dz_processor.h"
#include "models.h"
#include "notflac_format.h"
#include "range_coder.h"
#include "wav_stream.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int encode_audio(predictor_method_t predictor, entropy_method_t entropy, const char *in_path, const char *out_path)
{
    if (predictor != PREDICTOR_DELTA || entropy != ENTROPY_RANGE_CODER)
        return -1;

    wav_stream_t *ws = wav_stream_open(in_path);
    if (!ws)
        return -1;

    uint32_t num_samples = wav_stream_num_samples(ws);
    uint16_t bits = wav_stream_bits_per_sample(ws);
    uint32_t sample_rate = wav_stream_sample_rate(ws);
    uint16_t channels = wav_stream_channels(ws);

    if (bits != 16 && bits != 24 && bits != 32)
    {
        wav_stream_close(ws);
        return -1;
    }

    FILE *out = fopen(out_path, "wb");
    if (!out)
    {
        wav_stream_close(ws);
        return -1;
    }

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
    if (!rc)
    {
        fclose(out);
        wav_stream_close(ws);
        return -1;
    }

    int num_models = bits / 8;
    model_t *models = malloc(num_models * sizeof(model_t));
    if (!models)
    {
        rc_destroy(rc);
        fclose(out);
        wav_stream_close(ws);
        return -1;
    }
    for (int i = 0; i < num_models; i++)
        model_init(&models[i]);

    dz_encoder_t *enc = dze_init(bits);
    if (!enc)
    {
        free(models);
        rc_destroy(rc);
        fclose(out);
        wav_stream_close(ws);
        return -1;
    }

    int32_t sample;
    for (uint32_t i = 0; i < num_samples; i++)
    {
        if (wav_stream_next_sample(ws, &sample) != WAV_OK)
            break;

        uint32_t encoded = dze_next(enc, sample);
        for (int b = num_models - 1; b >= 0; b--)
        {
            uint8_t byte = (encoded >> (b * 8)) & 0xFF;
            rc_update(rc, model_cum(&models[b], byte), model_freq(&models[b], byte), models[b].total);
            model_update(&models[b], byte);
        }
    }

    rc_flush(rc);
    dze_destroy(enc);
    free(models);
    rc_destroy(rc);
    wav_stream_close(ws);
    fclose(out);
    return 0;
}
