#include "stereo.h"

void stereo_encode(stereo_mode_t mode, const int32_t *lr, int32_t *primary, int32_t *secondary, int n)
{
    for (int i = 0; i < n; i++)
    {
        int32_t L = lr[2 * i];
        int32_t R = lr[2 * i + 1];
        switch (mode)
        {
            case STEREO_MODE_INDEPENDENT:
                primary[i] = L;
                secondary[i] = R;
                break;
            case STEREO_MODE_LEFT_SIDE:
                primary[i] = L;
                secondary[i] = L - R;
                break;
            case STEREO_MODE_RIGHT_SIDE:
                primary[i] = R;
                secondary[i] = L - R;
                break;
            case STEREO_MODE_MID_SIDE:
                primary[i] = (L + R) >> 1;
                secondary[i] = L - R;
                break;
        }
    }
}

void stereo_decode(stereo_mode_t mode, const int32_t *primary, const int32_t *secondary, int32_t *lr, int n)
{
    for (int i = 0; i < n; i++)
    {
        int32_t P = primary[i];
        int32_t S = secondary[i];
        int32_t L, R;
        switch (mode)
        {
            case STEREO_MODE_INDEPENDENT:
                L = P;
                R = S;
                break;
            case STEREO_MODE_LEFT_SIDE:
                L = P;
                R = P - S;
                break;
            case STEREO_MODE_RIGHT_SIDE:
                R = P;
                L = P + S;
                break;
            case STEREO_MODE_MID_SIDE:
                L = P + ((S + 1) >> 1);
                R = P - (S >> 1);
                break;
        }
        lr[2 * i] = L;
        lr[2 * i + 1] = R;
    }
}
