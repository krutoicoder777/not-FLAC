#ifndef AUDIO_ENCODER_H__
#define AUDIO_ENCODER_H__

#include <stdio.h>
#include <stdint.h>
#include "notflac_format.h"

int encode_audio(predictor_method_t predictor, entropy_method_t entropy, const char *in_path, const char *out_path);

#endif
