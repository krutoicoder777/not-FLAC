#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "rice_coder.h"

typedef struct rice_coder rice_coder_t;

struct rice_coder
{
	FILE *out;
	uint8_t k;
	uint8_t bits;
	uint8_t bits_to_write;
};

rice_coder_t *rice_coder_create(FILE *out)
{
	rice_coder_t *rice = malloc(sizeof(rice_coder_t));
	if (rice)
	{
		rice->out = out;
		rice->k = 0;
		rice->bits = 0;
		rice->bits_to_write = 0;
	}
	return rice;
}

void rice_coder_destroy(rice_coder_t *rice)
{
	free(rice);
}

static uint8_t optimal_k(const uint32_t *distribution, int distribution_size)
{
	if (distribution_size == 0)
	{
		return 0;
	}
	uint8_t best_k = 0;
	uint64_t best_bits = UINT64_MAX;
	for (uint8_t k = 0; k <= 30; k++)
	{
		uint64_t bits = 0;
		for (uint32_t i = 0; i < distribution_size; i++)
		{
			bits += (distribution[i] >> k) + 1 + k;
		}
		if (bits < best_bits)
		{
			best_bits = bits;
			best_k = k;
		}
	}
	return best_k;
}

//set block counts optimal k for the distribution and writes it to file
int rice_coder_set_distribution(rice_coder_t *rice, const uint32_t *distribution, int distribution_size)
{
	int rc = 0;
	if (rice->bits_to_write != 0)
	{
		rc = 1;
		rice_coder_flush(rice);
	}
	rice->k = optimal_k(distribution, distribution_size);
	fputc(rice->k, rice->out);
	return rc;
}

static void queue_bit(rice_coder_t *rice, uint8_t bit)
{
	if (bit > 1)
	{
		bit = 1;
	}

	rice->bits_to_write++;
	rice->bits<<=1;
	rice->bits|=bit;

	if (rice->bits_to_write == 8)
	{
		fputc(rice->bits, rice->out);
		rice->bits_to_write = 0;
		rice->bits = 0;
	}
}

void rice_coder_encode_elem(rice_coder_t *rice, uint32_t res)
{
	uint32_t q = res >> rice->k;
	uint32_t r = res & ((1u << rice->k) - 1);

	for (uint32_t i = 0; i < q; i++)
	{
		queue_bit(rice, 0);
	}
	queue_bit(rice, 1);
	for (int i = rice->k - 1; i >= 0; i--)
	{
		queue_bit(rice, (r >> i)&1);
	}
}

void rice_coder_flush(rice_coder_t *rice)
{
	if (rice->bits_to_write != 0)
	{
		fputc(rice->bits << (8 - rice->bits_to_write), rice->out);
		rice->bits_to_write = 0;
		rice->bits = 0;
	}
}

typedef struct rice_decoder rice_decoder_t;

struct rice_decoder
{
	FILE *in;
	uint8_t k;
	uint8_t bits;
	uint8_t bits_in_buf;	
};

rice_decoder_t *rice_decoder_create(FILE *in)
{
	rice_decoder_t *rice = malloc(sizeof(rice_decoder_t));
	if (rice)
	{
		rice->in = in;
		rice->k = 0;
		rice->bits = 0;
		rice->bits_in_buf = 0;
	}
	return rice;
}

void rice_decoder_destroy(rice_decoder_t *rice)
{
	free(rice);
}

//reads k value from file
int rice_decoder_set_distribution(rice_decoder_t *rice)
{
	rice->bits = 0;
	rice->bits_in_buf = 0;
	int c = fgetc(rice->in);
	if (c == EOF)
	{
		return -1;
	}
	rice->k = (uint8_t)c;
	return 0;
}

static uint8_t dequeue_bit(rice_decoder_t *rice)
{
	if (rice->bits_in_buf == 0)
	{
		int c = fgetc(rice->in);
		if (c == EOF)
		{
			rice->bits_in_buf = 0;
			rice->bits = 0;
			return 1;
		}
		rice->bits_in_buf = 8;
		rice->bits = (uint8_t)c;
	}
	rice->bits_in_buf--;
	uint8_t bit = (rice->bits >> rice->bits_in_buf)&1;
	return bit;	
}

uint32_t rice_decoder_decode_elem(rice_decoder_t *rice)
{
	uint32_t elem = 0;
	uint32_t q = 0;
	uint32_t r = 0;
	while (dequeue_bit(rice) != 1)
	{
		q++;
	}
	for (int i = rice->k - 1; i>=0; i--)
	{
		r |= dequeue_bit(rice) << i;
	}
	return (q<<rice->k)|r;
}
