#include "lpc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define ORDER 8
#define BLOCK_SIZE 4096

static double theoretical_a1(double w)
{
    return 2.0 * cos(w);
}

int main(void)
{
    int32_t *samples = malloc(BLOCK_SIZE * sizeof(int32_t));
    for (int i = 0; i < BLOCK_SIZE; i++)
    {
        samples[i] = (int32_t)(sin(0.1 * i) * 10000.0);
    }

    lpc_encoder_t *enc = lpc_encoder_create(ORDER);
    lpc_decoder_t *dec = lpc_decoder_create(ORDER);
    if (!enc || !dec)
    {
        printf("init failed\n");
        return 1;
    }

    lpc_encoder_encode_block(enc, samples, BLOCK_SIZE);
    const int32_t *q = lpc_encoder_quotients(enc);
    int precision = lpc_encoder_precision(enc);

    printf("Precision: %d\n", precision);
    printf("Quotients: ");
    for (int i = 0; i < ORDER; i++)
        printf("%d ", q[i]);
    printf("\n");

    double a1 = (double)q[0] / (1 << precision);
    double a2 = (double)q[1] / (1 << precision);
    printf("Theoretical a1 (for sin(0.1n)): %.3f, got: %.3f\n", theoretical_a1(0.1), a1);
    printf("Theoretical a2: -1.0, got: %.3f\n", a2);

    lpc_decoder_set_quotients(dec, q, precision);

    int errors = 0;
    for (int i = 0; i < BLOCK_SIZE; i++)
    {
        int32_t residual = lpc_encoder_pop(enc);
        int32_t restored = lpc_decoder_decode_sample(dec, residual);
        if (restored != samples[i])
        {
            if (errors < 5)
                printf("Mismatch at %d: %d vs %d (residual %d)\n", i, samples[i], restored, residual);
            errors++;
        }
    }

    lpc_encoder_destroy(enc);
    lpc_decoder_destroy(dec);
    free(samples);

    if (errors == 0)
        printf("SUCCESS! %d samples round-tripped.\n", BLOCK_SIZE);
    else
        printf("FAILED! %d errors.\n", errors);
    return errors > 0;
}
