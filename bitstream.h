#ifndef _BITSTREAM_H_
#define _BITSTREAM_H_

typedef struct
{
	const uint8_t *data;
	unsigned int length;
	unsigned int bitpos;
} bitstream;

uint32_t show_bits(bitstream *bs, int n);
uint32_t get_bits(bitstream *bs, int n);
int bytealign(bitstream *bs);
int  bytealigned(bitstream *bs, int nbit);
void flush_bits(bitstream *bs, int nbit);
int nextbits_bytealigned(bitstream *bs, int nbit);
int bits_left(bitstream *bs);
int decode012(bitstream *bs);

#endif
