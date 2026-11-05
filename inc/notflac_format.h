#ifndef NOTFLAC_FORMAT_H__
#define NOTFLAC_FORMAT_H__

#define NOTFLAC_MAGIC 0x4E464C41
#define NOTFLAC_VERSION 2
#define NOTFLAC_HEADER_SIZE 20

#define OFFSET_OF_TABLE_OFFSET_IN_HEADER 24

typedef enum
{
    ENTROPY_RANGE_CODER = 0,
} entropy_method_t;

typedef enum
{
    PREDICTOR_DELTA = 0,
	PREDICTOR_LPC,
} predictor_method_t;

#define BLOCK_SIZE 4096
#define LPC_ORDER 16

#endif
