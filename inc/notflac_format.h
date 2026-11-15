#ifndef NOTFLAC_FORMAT_H__
#define NOTFLAC_FORMAT_H__

#define NOTFLAC_MAGIC 0x4E464C41
#define NOTFLAC_VERSION 10
#define NOTFLAC_HEADER_SIZE 20

#define OFFSET_OF_TABLE_OFFSET_IN_HEADER 24

#define BLOCK_SIZE 4096
#define LPC_ORDER_MAX 32

typedef enum
{
    ENTROPY_RICE_CODER = 0,
} entropy_method_t;

typedef enum
{
    PREDICTOR_LPC = 0,
    PREDICTOR_FIXED = 1,
} predictor_method_t;

#endif
