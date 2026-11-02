#ifndef NOTFLAC_FORMAT_H__
#define NOTFLAC_FORMAT_H__

#define NOTFLAC_MAGIC 0x4E464C41
#define NOTFLAC_VERSION 1
#define NOTFLAC_HEADER_SIZE 20

typedef enum
{
    ENTROPY_RANGE_CODER = 0,
} entropy_method_t;

typedef enum
{
    PREDICTOR_DELTA = 0,
} predictor_method_t;

#endif
