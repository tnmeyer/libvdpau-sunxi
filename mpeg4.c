/*
 * Copyright (c) 2013 Jens Kuske <jenskuske@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <string.h>
#include "vdpau_private.h"
#include "ve.h"
#include <time.h>
#include <assert.h>
#include "bitstream.h"
#include "mpeg4.h"
#include "stdlib.h"
#include "mp4_vars.h"

#define TIMEMEAS 1

static int mpeg4_calcResyncMarkerLength(mp4_private_t *decoder_p);
uint32_t show_bits_aligned(bitstream *bs, int n, int aligned);

static int find_startcode(bitstream *bs)
{
	unsigned int pos, zeros = 0;
	for (pos = bs->bitpos / 8; pos < bs->length; pos++)
	{
		if (bs->data[pos] == 0x00)
			zeros++;
		else if (bs->data[pos] == 0x01 && zeros >= 2)
		{
			bs->bitpos = (pos + 1) * 8;
			return 1;
		}
		else
			zeros = 0;
	}

	return 0;
}

uint32_t show_bits(bitstream *bs, int n)
{
    return show_bits_aligned(bs, n, 0);

#if 0
    uint32_t bits = 0;
    int remaining_bits = n;
    int save_bitpos = bs->bitpos;

    while (remaining_bits > 0)
    {
            int bits_in_current_byte = 8 - (bs->bitpos & 7);

            int trash_bits = 0;
            if (remaining_bits < bits_in_current_byte)
                    trash_bits = bits_in_current_byte - remaining_bits;

            int useful_bits = bits_in_current_byte - trash_bits;

            bits = (bits << useful_bits) | (bs->data[bs->bitpos / 8] >> trash_bits);

            remaining_bits -= useful_bits;
            bs->bitpos += useful_bits;
    }

    bs->bitpos = save_bitpos;

    return bits & ((1 << n) - 1);
#endif
}

uint32_t show_bits_aligned(bitstream *bs, int n, int aligned)
{
    uint32_t bits = 0;
    int remaining_bits = n;
    int bitpos;

    if(aligned)
        bitpos = (bs->bitpos+7) & ~7;
    else
        bitpos = bs->bitpos;

    while (remaining_bits > 0)
    {
        int bits_in_current_byte = 8 - (bs->bitpos & 7);

        int trash_bits = 0;
        if (remaining_bits < bits_in_current_byte)
                trash_bits = bits_in_current_byte - remaining_bits;

        int useful_bits = bits_in_current_byte - trash_bits;

        bits = (bits << useful_bits) | (bs->data[bitpos / 8] >> trash_bits);

        remaining_bits -= useful_bits;
        bitpos += useful_bits;
    }

    return bits & ((1 << n) - 1);
}

uint32_t get_bits(bitstream *bs, int n)
{
    uint32_t bits = 0;
    int remaining_bits = n;

    while (remaining_bits > 0)
    {
            int bits_in_current_byte = 8 - (bs->bitpos & 7);

            int trash_bits = 0;
            if (remaining_bits < bits_in_current_byte)
                    trash_bits = bits_in_current_byte - remaining_bits;

            int useful_bits = bits_in_current_byte - trash_bits;

            bits = (bits << useful_bits) | (bs->data[bs->bitpos / 8] >> trash_bits);

            remaining_bits -= useful_bits;
            bs->bitpos += useful_bits;
    }

    return bits & ((1 << n) - 1);
}

int bytealign(bitstream *bs)
{
    bs->bitpos = ((bs->bitpos+7) >> 3) << 3;
    if(bs->bitpos > bs->length * 8)
    {
        bs->bitpos = bs->length * 8;
        return 1;
    }
    return 0;
}
int  bytealigned(bitstream *bs, int nbit)
{
    return (((bs->bitpos + nbit) % 8) == 0);
}
void flush_bits(bitstream *bs, int nbit)
{
    bs->bitpos += nbit;
}
int bits_left(bitstream *bs)
{
    return(bs->bitpos/8 < bs->length);
}

int nextbits_bytealigned(bitstream *bs, int nbit)
{
        int code;
        int skipcnt = 0;

        if (bytealigned(bs, skipcnt))
        {
                // stuffing bits
                if (show_bits(bs, 8) == 127) {
                        skipcnt += 8;
                }
        }
        else
        {
                // bytealign
                while (! bytealigned(bs, skipcnt)) {
                        skipcnt += 1;
                }
        }

        code = show_bits(bs, nbit + skipcnt);
        return ((code << skipcnt) >> skipcnt);
}

int decode012(bitstream *bs)
{
    int n;
    n = get_bits(bs, 1);
    if (n == 0)
        return 0;
    else
        return get_bits(bs, 1) + 1;
}

static void dumpData(char* data)
{
    int pos=0;
    printf("data[%d]=%X data[+1]=%X data[+2]=%X data[+3]=%X data[+4]=%X\n",
        pos, data[pos], data[pos+1], data[pos+2], data[pos+3],data[pos+4]);
}

static int find_resynccode(bitstream *bs, int resync_length)
{
    unsigned int pos, zeros = 0;
    
    bytealign(bs);
    
    for (pos = bs->bitpos / 8; pos < bs->length; pos++)
    {
        if(bs->data[pos])
            continue;
    
        bs->bitpos = pos * 8;
        int resync_code = show_bits(bs, resync_length);
        if(resync_code == 1)
            return 1;
    }

    return 0;
}

static void mp4_private_free(decoder_ctx_t *decoder)
{
    mp4_private_t *decoder_p = (mp4_private_t *)decoder->private;
    ve_free(decoder_p->mbh_buffer);
    ve_free(decoder_p->dcac_buffer);
    ve_free(decoder_p->ncf_buffer);
    free(decoder_p);
}

static VLCtabMb MCBPCtabIntra[] = {
	{-1,0},
	{20,6}, {36,6}, {52,6}, {4,4}, {4,4}, {4,4}, 
	{4,4}, {19,3}, {19,3}, {19,3}, {19,3}, {19,3}, 
	{19,3}, {19,3}, {19,3}, {35,3}, {35,3}, {35,3}, 
	{35,3}, {35,3}, {35,3}, {35,3}, {35,3}, {51,3}, 
	{51,3}, {51,3}, {51,3}, {51,3}, {51,3}, {51,3}, 
	{51,3},
};

static VLCtabMb MCBPCtabInter[] = {
	{-1,0}, 
	{255,9}, {52,9}, {36,9}, {20,9}, {49,9}, {35,8}, {35,8}, {19,8}, {19,8},
	{50,8}, {50,8}, {51,7}, {51,7}, {51,7}, {51,7}, {34,7}, {34,7}, {34,7},
	{34,7}, {18,7}, {18,7}, {18,7}, {18,7}, {33,7}, {33,7}, {33,7}, {33,7}, 
	{17,7}, {17,7}, {17,7}, {17,7}, {4,6}, {4,6}, {4,6}, {4,6}, {4,6}, 
	{4,6}, {4,6}, {4,6}, {48,6}, {48,6}, {48,6}, {48,6}, {48,6}, {48,6}, 
	{48,6}, {48,6}, {3,5}, {3,5}, {3,5}, {3,5}, {3,5}, {3,5}, {3,5}, 
	{3,5}, {3,5}, {3,5}, {3,5}, {3,5}, {3,5}, {3,5}, {3,5}, {3,5}, 
	{32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {32,4}, 
	{32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {32,4}, 
	{32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {32,4}, 
	{32,4}, {32,4}, {32,4}, {32,4}, {32,4}, {16,4}, {16,4}, {16,4}, {16,4}, 
	{16,4}, {16,4}, {16,4}, {16,4}, {16,4}, {16,4}, {16,4}, {16,4}, {16,4}, 
	{16,4}, {16,4}, {16,4}, {16,4}, {16,4}, {16,4}, {16,4}, {16,4}, {16,4}, 
	{16,4}, {16,4}, {16,4}, {16,4}, {16,4}, {16,4}, {16,4}, {16,4}, {16,4}, 
	{16,4}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, 
	{2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, 
	{2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, 
	{2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, 
	{2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, 
	{2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, 
	{2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, {2,3}, 
	{2,3}, {2,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, 
	{1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, 
	{1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, 
	{1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, 
	{1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, 
	{1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, 
	{1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, {1,3}, 
	{1,3}, {1,3}, {1,3}, 
};
static VLCtabMb CBPYtab[48] = 
{ 
	{-1,0}, {-1,0}, {6,6},  {9,6},  {8,5},  {8,5},  {4,5},  {4,5},
	{2,5},  {2,5},  {1,5},  {1,5},  {0,4},  {0,4},  {0,4},  {0,4}, 
  {12,4}, {12,4}, {12,4}, {12,4}, {10,4}, {10,4}, {10,4}, {10,4},
  {14,4}, {14,4}, {14,4}, {14,4}, {5,4},  {5,4},  {5,4},  {5,4},
  {13,4}, {13,4}, {13,4}, {13,4}, {3,4},  {3,4},  {3,4},  {3,4}, 
  {11,4}, {11,4}, {11,4}, {11,4}, {7,4},  {7,4},  {7,4},  {7,4}, 
};
static int DQtab[4] = {
	-1, -2, 1, 2
};
static VLCtabMb MVtab0[] =
{
	{3,4}, {-3,4}, {2,3}, {2,3}, {-2,3}, {-2,3}, {1,2}, {1,2}, {1,2}, {1,2},
	{-1,2}, {-1,2}, {-1,2}, {-1,2}
};

static VLCtabMb MVtab1[] = 
{
	{12,10}, {-12,10}, {11,10}, {-11,10}, {10,9}, {10,9}, {-10,9}, {-10,9},
	{9,9}, {9,9}, {-9,9}, {-9,9}, {8,9}, {8,9}, {-8,9}, {-8,9}, {7,7}, {7,7},
	{7,7}, {7,7}, {7,7}, {7,7}, {7,7}, {7,7}, {-7,7}, {-7,7}, {-7,7}, {-7,7},
	{-7,7}, {-7,7}, {-7,7}, {-7,7}, {6,7}, {6,7}, {6,7}, {6,7}, {6,7}, {6,7},
	{6,7}, {6,7}, {-6,7}, {-6,7}, {-6,7}, {-6,7}, {-6,7}, {-6,7}, {-6,7},
	{-6,7}, {5,7}, {5,7}, {5,7}, {5,7}, {5,7}, {5,7}, {5,7}, {5,7}, {-5,7},
	{-5,7}, {-5,7}, {-5,7}, {-5,7}, {-5,7}, {-5,7}, {-5,7}, {4,6}, {4,6}, {4,6},
	{4,6}, {4,6}, {4,6}, {4,6}, {4,6}, {4,6}, {4,6}, {4,6}, {4,6}, {4,6}, {4,6},
	{4,6}, {4,6}, {-4,6}, {-4,6}, {-4,6}, {-4,6}, {-4,6}, {-4,6}, {-4,6},
	{-4,6}, {-4,6}, {-4,6}, {-4,6}, {-4,6}, {-4,6}, {-4,6}, {-4,6}, {-4,6}
};

static VLCtabMb MVtab2[] = 
{
	{32,12}, {-32,12}, {31,12}, {-31,12}, {30,11}, {30,11}, {-30,11}, {-30,11},
	{29,11}, {29,11}, {-29,11}, {-29,11}, {28,11}, {28,11}, {-28,11}, {-28,11},
	{27,11}, {27,11}, {-27,11}, {-27,11}, {26,11}, {26,11}, {-26,11}, {-26,11},
	{25,11}, {25,11}, {-25,11}, {-25,11}, {24,10}, {24,10}, {24,10}, {24,10},
	{-24,10}, {-24,10}, {-24,10}, {-24,10}, {23,10}, {23,10}, {23,10}, {23,10},
	{-23,10}, {-23,10}, {-23,10}, {-23,10}, {22,10}, {22,10}, {22,10}, {22,10},
	{-22,10}, {-22,10}, {-22,10}, {-22,10}, {21,10}, {21,10}, {21,10}, {21,10},
	{-21,10}, {-21,10}, {-21,10}, {-21,10}, {20,10}, {20,10}, {20,10}, {20,10},
	{-20,10}, {-20,10}, {-20,10}, {-20,10}, {19,10}, {19,10}, {19,10}, {19,10},
	{-19,10}, {-19,10}, {-19,10}, {-19,10}, {18,10}, {18,10}, {18,10}, {18,10},
	{-18,10}, {-18,10}, {-18,10}, {-18,10}, {17,10}, {17,10}, {17,10}, {17,10},
	{-17,10}, {-17,10}, {-17,10}, {-17,10}, {16,10}, {16,10}, {16,10}, {16,10},
	{-16,10}, {-16,10}, {-16,10}, {-16,10}, {15,10}, {15,10}, {15,10}, {15,10},
	{-15,10}, {-15,10}, {-15,10}, {-15,10}, {14,10}, {14,10}, {14,10}, {14,10},
	{-14,10}, {-14,10}, {-14,10}, {-14,10}, {13,10}, {13,10}, {13,10}, {13,10},
	{-13,10}, {-13,10}, {-13,10}, {-13,10}
};

static int get_gob_height(int height)
{
    int gob_height;
    if(height <= 400)
        gob_height = 1;
    else if(height <= 800)
        gob_height = 2;
    else
        gob_height = 4;
    return gob_height;
}

static int getMCBPC(bitstream *bs, mp4_private_t *priv)
{
    vop_header_t *h = &priv->vop_header;

	if (h->vop_coding_type == VOP_I)
	{
		int code = show_bits(bs, 9);

		if (code == 1) {
			get_bits(bs, 9); // stuffing
			return 0;
		}
		else if (code < 8) {
			return -1;
		}

		code >>= 3;
		if (code >= 32) {
			get_bits(bs, 1);
			return 3;
		}
		
		get_bits(bs, MCBPCtabIntra[code].len);
		return MCBPCtabIntra[code].val;
	}
	else
	{
		int code = show_bits(bs, 9);

		if (code == 1) {
			get_bits(bs, 9); // stuffing
			return 0;
		}
		else if (code == 0)	{
			return -1;
		}
		
		if (code >= 256)
		{
			get_bits(bs, 1);
			return 0;
		}
		
		get_bits(bs, MCBPCtabInter[code].len);
		return MCBPCtabInter[code].val;
	}
}

static int getCBPY(bitstream *bs, mp4_private_t *priv)
{
    vop_header_t *h = &priv->vop_header;
    int cbpy;
    int code = show_bits(bs, 6);

    if (code < 2) {
        return -1;
    }
                        
    if (code >= 48) {
        get_bits(bs,2);
        cbpy = 15;
    } else {
        get_bits(bs, CBPYtab[code].len);
        cbpy = CBPYtab[code].val;
    }

    if (!((h->derived_mb_type == 3) ||
        (h->derived_mb_type == 4)))
            cbpy = 15 - cbpy;

  return cbpy;
}
static int getMVdata(bitstream *bs, mp4_private_t *priv)
{
	int code;

	if (get_bits(bs, 1)) {
		return 0; // hor_mv_data == 0
  }
	
	code = show_bits(bs, 12);

	if (code >= 512)
  {
		code = (code >> 8) - 2;
		get_bits(bs, MVtab0[code].len);
		return MVtab0[code].val;
  }
	
	if (code >= 128)
  {
		code = (code >> 2) - 32;
		get_bits(bs, MVtab1[code].len);
		return MVtab1[code].val;
  }

	code -= 4; 

	assert(code >= 0);

	get_bits(bs, MVtab2[code].len);
	return MVtab2[code].val;
}
static int find_pmv (bitstream *bs, mp4_private_t *priv, int block, int comp)
{
  int p1, p2, p3;
  int xin1, xin2, xin3;
  int yin1, yin2, yin3;
  int vec1, vec2, vec3;
  vop_header_t *h = &priv->vop_header;
  video_packet_header_t *vp = &priv->vop_header;

	int x = vp->mb_xpos;
	int y = vp->mb_ypos;
	
	if ((y == 0) && ((block == 0) || (block == 1)))
	{
		if ((x == 0) && (block == 0))
			return 0;
		else if (block == 1)
			return priv->MV[comp][0][y+1][x+1];
		else // block == 0
			return priv->MV[comp][1][y+1][x+1-1];
	}
	else
	{
		// considerate border (avoid increment inside each single array index)
		x++;
		y++;

		switch (block)
		{
			case 0: 
				vec1 = 1;	yin1 = y;		xin1 = x-1;
				vec2 = 2;	yin2 = y-1;	xin2 = x;
				vec3 = 2;	yin3 = y-1;	xin3 = x+1;
				break;
			case 1:
				vec1 = 0;	yin1 = y;		xin1 = x;
				vec2 = 3;	yin2 = y-1;	xin2 = x;
				vec3 = 2;	yin3 = y-1;	xin3 = x+1;
				break;
			case 2:
				vec1 = 3;	yin1 = y;		xin1 = x-1;
				vec2 = 0;	yin2 = y;	  xin2 = x;
				vec3 = 1;	yin3 = y;		xin3 = x;
				break;
			default: // case 3
				vec1 = 2;	yin1 = y;		xin1 = x;
				vec2 = 0;	yin2 = y;		xin2 = x;
				vec3 = 1;	yin3 = y;		xin3 = x;
				break;
		}
		p1 = priv->MV[comp][vec1][yin1][xin1];
		p2 = priv->MV[comp][vec2][yin2][xin2];
		p3 = priv->MV[comp][vec3][yin3][xin3];

		// return p1 + p2 + p3 - mmax (p1, mmax (p2, p3)) - mmin (p1, mmin (p2, p3));
		return mmin(mmax(p1, p2), mmin(mmax(p2, p3), mmax(p1, p3)));
	}
}

static int setMV(bitstream *bs, mp4_private_t *priv, int block_num)
{
    vop_header_t *h = &priv->vop_header;
    video_packet_header_t* vp = &priv->pkt_hdr;
    
	int hor_mv_data, ver_mv_data, hor_mv_res, ver_mv_res;
	int scale_fac = 1 << (h->fcode_forward - 1);
	int high = (32 * scale_fac) - 1;
	int low = ((-32) * scale_fac);
	int range = (64 * scale_fac);

	int mvd_x, mvd_y, pmv_x, pmv_y, mv_x, mv_y;

/***

	[hor_mv_data]
	if ((vop_fcode_forward != 1) && (hor_mv_data != 0))
		[hor_mv_residual]

***/	

    hor_mv_data = getMVdata(bs, priv); // mv data

	if ((scale_fac == 1) || (hor_mv_data == 0))
		mvd_x = hor_mv_data;
	else {
		hor_mv_res = get_bits(bs, h->fcode_forward-1); // mv residual
		mvd_x = ((abs(hor_mv_data) - 1) * scale_fac) + hor_mv_res + 1;
		if (hor_mv_data < 0)
			mvd_x = - mvd_x;
	}

    ver_mv_data = getMVdata(bs, priv); 

	if ((scale_fac == 1) || (ver_mv_data == 0))
		mvd_y = ver_mv_data;
	else {
		ver_mv_res = get_bits(bs, h->fcode_forward-1);
		mvd_y = ((abs(ver_mv_data) - 1) * scale_fac) + ver_mv_res + 1;
		if (ver_mv_data < 0)
			mvd_y = - mvd_y;
	}

	if (block_num == -1) {
		pmv_x = find_pmv(bs, priv, 0, 0);
		pmv_y = find_pmv(bs, priv, 0, 1);
	}
	else {
		pmv_x = find_pmv(bs, priv, block_num, 0);
		pmv_y = find_pmv(bs, priv, block_num, 1);
	}

//	_Print("Hor MotV Pred: %d\n", pmv_x);
//	_Print("Ver MorV Pred: %d\n", pmv_y);

	mv_x = pmv_x + mvd_x;

	if (mv_x < low)
		mv_x += range;
	if (mv_x > high)
		mv_x -= range;

	mv_y = pmv_y + mvd_y;

	if (mv_y < low)
		mv_y += range;
	if (mv_y > high)
		mv_y -= range;

	// put [mv_x, mv_y] in MV struct
	if (block_num == -1) {
		int i;
		for (i = 0; i < 4; i++) {
			priv->MV[0][i][vp->mb_ypos+1][vp->mb_xpos+1] = mv_x;
			priv->MV[1][i][vp->mb_ypos+1][vp->mb_xpos+1] = mv_y;
		}
	}
	else {
		priv->MV[0][block_num][vp->mb_ypos+1][vp->mb_xpos+1] = mv_x;
		priv->MV[1][block_num][vp->mb_ypos+1][vp->mb_xpos+1] = mv_y;
	}

//  _Print("Hor MotV: %d\n", mv_x);
//	_Print("Ver MotV: %d\n", mv_y);

  return 1;
}

static int macroblock(bitstream *bs, mp4_private_t *priv)
{
    int j;
    int intraFlag, interFlag;
    vop_header_t *h = &priv->vop_header;
    VdpDecoderMpeg4VolHeader *vol = &priv->mpeg4VolHdr;
    video_packet_header_t* vp = &priv->pkt_hdr;

//	_Print("-Macroblock %d\n", mp4_state->hdr.mba);
//	_Break(93, 426);

    if(h->vop_coding_type != VOP_B) {
        if(vol->video_object_layer_shape != BIN_ONLY_SHAPE) {
            if (h->vop_coding_type != VOP_I)
                    h->not_coded = get_bits(bs, 1);
    
            // coded macroblock or I-VOP
            if (! h->not_coded || h->vop_coding_type == VOP_I) {
    
                    h->mcbpc = getMCBPC(bs, priv); // mcbpc
                    h->derived_mb_type = h->mcbpc & 7;
                    h->cbpc = (h->mcbpc >> 4) & 3;
    
    #if 0
                    mp4_state->modemap[mp4_state->hdr.mb_ypos+1][mp4_state->hdr.mb_xpos+1] = 
                            mp4_state->hdr.derived_mb_type; // [Review] used only in P-VOPs
    #endif
                    intraFlag = ((h->derived_mb_type == INTRA) || 
                            (h->derived_mb_type == INTRA_Q)) ? 1 : 0;
                    interFlag = (! intraFlag);
    
                    if (intraFlag)
                            h->ac_pred_flag = get_bits(bs, 1);
                    if (h->derived_mb_type != STUFFING) {
    
                            h->cbpy = getCBPY(bs, priv); // cbpy
                            h->cbp = (h->cbpy << 2) | h->cbpc;
                    }
                    else
                            return 1;
    
    //		_Print("Mb type: %d\n", mp4_state->hdr.derived_mb_type);
    //		_Print("Pattern Chr: %d\n", mp4_state->hdr.cbpc);
    //		_Print("Pattern Luma: %d\n", mp4_state->hdr.cbpy);
    
                    if (h->derived_mb_type == INTER_Q ||
                            h->derived_mb_type == INTRA_Q) {
    
                            h->dquant = get_bits(bs, 2);
                            h->quantizer += DQtab[h->dquant];
                            if (h->quantizer > 31) {
                                    h->quantizer = 31;
                                    //printf("limiting quantizer to 31\n");
                            }
                            else if (h->quantizer < 1) {
                                    h->quantizer = 1;
                                    //printf("limiting quantizer to 1\n");
                            }
                    }
                    if (h->derived_mb_type == INTER ||
                            h->derived_mb_type == INTER_Q) {
    
                            setMV(bs, priv, -1); // mv
                    }
                    else if (h->derived_mb_type == INTER4V) {
    #if 1
                            for (j = 0; j < 4; j++) {
    
                                    setMV(bs, priv, j); // mv
                            }
    #endif
                    }
                    else { // intra
                            if (h->vop_coding_type == VOP_P) {
                                    int i;
    #if 1
                                    for (i = 0; i < 4; i++) {
                                            priv->MV[0][i][vp->mb_ypos+1][vp->mb_xpos+1] = 0;
                                            priv->MV[1][i][vp->mb_ypos+1][vp->mb_xpos+1] = 0;
                                    }
    #endif
                            }
                    }
    
    #if 1
                    // motion compensation
                    if (interFlag) 
                    {
    
    //			reconstruct(mp4_state->hdr.mb_xpos, mp4_state->hdr.mb_ypos, mp4_state->hdr.derived_mb_type);
    
                            // texture decoding add
                            for (j = 0; j < 6; j++) {
                                    int coded = h->cbp & (1 << (5 - j));
    
                                    if (coded) { 
                                            blockInter(bs, priv, j, (coded != 0));
                                            //addblockInter(j, vp->mb_xpos, vp->mb_ypos);
                                    }
                            }
                    }
                    else 
                    {
                            // texture decoding add
                            for (j = 0; j < 6; j++) {
                                    int coded = h->cbp & (1 << (5 - j));
    
                                    blockIntra(bs, priv, j, (coded != 0));
                                    //addblockIntra(j, vp->mb_xpos, vp->mb_ypos);
                            }
                    }
    #endif
            }
    
            // not coded macroblock
            else {
                    priv->MV[0][0][vp->mb_ypos+1][vp->mb_xpos+1] = 
            priv->MV[0][1][vp->mb_ypos+1][vp->mb_xpos+1] =
                    priv->MV[0][2][vp->mb_ypos+1][vp->mb_xpos+1] = 
            priv->MV[0][3][vp->mb_ypos+1][vp->mb_xpos+1] = 0;
                    priv->MV[1][0][vp->mb_ypos+1][vp->mb_xpos+1] = 
            priv->MV[1][1][vp->mb_ypos+1][vp->mb_xpos+1] =
                    priv->MV[1][2][vp->mb_ypos+1][vp->mb_xpos+1] = 
            priv->MV[1][3][vp->mb_ypos+1][vp->mb_xpos+1] = 0;
    
    #if 0
                    //mp4_state->modemap[mp4_state->hdr.mb_ypos+1][mp4_state->hdr.mb_xpos+1] = NOT_CODED; // [Review] used only in P-VOPs
    
                    //reconstruct(mp4_state->hdr.mb_xpos, mp4_state->hdr.mb_ypos, mp4_state->hdr.derived_mb_type);
    #endif
            }
    
            //mp4_state->quant_store[mp4_state->hdr.mb_ypos+1][mp4_state->hdr.mb_xpos+1] = mp4_state->hdr.quantizer;
    
            if (priv->pkt_hdr.mb_xpos < (priv->pkt_hdr.mb_width-1)) {
                    priv->pkt_hdr.mb_xpos++;
            }
            else {
                    priv->pkt_hdr.mb_ypos++;
                    priv->pkt_hdr.mb_xpos = 0;
            }
        }
    }
    return 1;
}

static inline long ROUNDED_DIV(long a, long b)
{
  return ((a)>0 ? (((a) + ((b)>>1))/(b)) : (((a) - ((b)>>1))/(b)));
}

static inline long long SIGNED_ROUNDED_DIV(long long a, long b)
{
    return (((a) + (b>>1))/b);
}

#define LOG2CEIL(n) (n <= 1 ? 0 : 32 - __builtin_clz(n - 1))
int read_dmv_length(bitstream *gb)
{
    int value = get_bits(gb, 2);
    if(value == 2)
    {
        value = get_bits(gb, 1) + 3;
    }
    else if( value == 1)
    {
        value = get_bits(gb, 1) + 1;
    }
    else if( value == 3) 
    {
        int i = 3;
        value = 5;
        while (value < 14)
        {
            int value2 = get_bits(gb, 1);
            if(value2 == 0)
                break;
            value++;
        }
        //if(i == 13)
        //    value = -1;
    }

    return value;
}
int read_dmv_code (bitstream *gb, int length)
{
    int code=0;

    code = get_bits(gb, length);
    int first_bit = (1 << (length-1));
    if((code & first_bit) == 0)
    {
        code = ((~(1 << length))+1) - ~code;
    }
    return code;
}

static int mpeg4_decode_sprite_trajectory(bitstream *gb, mp4_private_t *priv)
{
    vop_header_t *vop = &priv->vop_header;
    VdpDecoderMpeg4VolHeader *vol = &priv->mpeg4VolHdr;

    int i;
    int a= 2<<vol->sprite_warping_accuracy;
    int rho= 3-vol->sprite_warping_accuracy;
    int r=16/a;
    const int vop_ref[4][2]= {  {0,0}, 
                                {vol->video_object_layer_width,0}, 
                                {0, vol->video_object_layer_height}, 
                                {vol->video_object_layer_width, vol->video_object_layer_height}}; // only true for rectangle shapes
    int d[4][2]={{0,0}, {0,0}, {0,0}, {0,0}};
    int sprite_ref[4][2];
    int virtual_ref[2][2];
    int w2, h2, w3, h3;
    int alpha=0, beta=0;
    int w= vol->video_object_layer_width;
    int h= vol->video_object_layer_height;
    int min_ab;
    int temp, term1, term2,term3,term4;

    if (w <= 0 || h <= 0)
        return 1;

    for(i=0; i<vol->no_of_sprite_warping_points; i++){
        int length;
        int x=0, y=0;

//        length= get_vlc2(gb, sprite_trajectory.table, SPRITE_TRAJ_VLC_BITS, 3);
        length = read_dmv_length(gb);
        if(length){
//            x= get_xbits(gb, length);
            x = read_dmv_code(gb, length);
        }
        //if(!(s->divx_version==500 && s->divx_build==413)) skip_bits(gb,1); /* marker bit */
	if (get_bits(gb, 1) != 1)
		VDPAU_DBG("vop header marker error");

//        length= get_vlc2(gb, sprite_trajectory.table, SPRITE_TRAJ_VLC_BITS, 3);
            length= read_dmv_length(gb);
        if(length){
//            y=get_xbits(gb, length);
            y=read_dmv_code(gb, length);
        }
	if (get_bits(gb, 1) != 1)
		VDPAU_DBG("vop header marker error");
        vop->sprite_traj[i][0]= d[i][0]= x;
        vop->sprite_traj[i][1]= d[i][1]= y;
    }
    for(; i<4; i++)
        vop->sprite_traj[i][0]= vop->sprite_traj[i][1]= 0;

    while((1<<alpha)<w) alpha++;
    while((1<<beta )<h) beta++; // there seems to be a typo in the mpeg4 std for the definition of w' and h'
    w2= 1<<alpha;
    h2= 1<<beta;

// Note, the 4th point isn't used for GMC
#if 0
    if(s->divx_version==500 && s->divx_build==413){
        sprite_ref[0][0]= a*vop_ref[0][0] + d[0][0];
        sprite_ref[0][1]= a*vop_ref[0][1] + d[0][1];
        sprite_ref[1][0]= a*vop_ref[1][0] + d[0][0] + d[1][0];
        sprite_ref[1][1]= a*vop_ref[1][1] + d[0][1] + d[1][1];
        sprite_ref[2][0]= a*vop_ref[2][0] + d[0][0] + d[2][0];
        sprite_ref[2][1]= a*vop_ref[2][1] + d[0][1] + d[2][1];
    } else {
#endif
        int temp_i0 = (2*vop_ref[0][0] + d[0][0]);
        sprite_ref[0][0]= (a>>1)*temp_i0;
        int temp_j0 = (2*vop_ref[0][1] + d[0][1]);
        sprite_ref[0][1]= (a>>1)*temp_j0;
        int temp_i1 = (2*vop_ref[1][0] + d[0][0] + d[1][0]);
        sprite_ref[1][0]= (a>>1)*temp_i1;
        int temp_j1 = (2*vop_ref[1][1] + d[0][1] + d[1][1]);
        sprite_ref[1][1]= (a>>1)*temp_j1;
        int temp_i2 = (2*vop_ref[2][0] + d[0][0] + d[2][0]);
        sprite_ref[2][0]= (a>>1)*temp_i2;
        int temp_j2 = (2*vop_ref[2][1] + d[0][1] + d[2][1]);
        sprite_ref[2][1]= (a>>1)*temp_j2;
//    }
    vop->sprite_ref[0][0] = sprite_ref[0][0];
    vop->sprite_ref[0][1] = sprite_ref[0][1];
    vop->sprite_ref[1][0] = sprite_ref[1][0];
    vop->sprite_ref[1][1] = sprite_ref[1][1];
    vop->sprite_ref[2][0] = sprite_ref[2][0];
    vop->sprite_ref[2][1] = sprite_ref[2][1];

/*    sprite_ref[3][0]= (a>>1)*(2*vop_ref[3][0] + d[0][0] + d[1][0] + d[2][0] + d[3][0]);
    sprite_ref[3][1]= (a>>1)*(2*vop_ref[3][1] + d[0][1] + d[1][1] + d[2][1] + d[3][1]); */

// this is mostly identical to the mpeg4 std (and is totally unreadable because of that ...)
// perhaps it should be reordered to be more readable ...
// the idea behind this virtual_ref mess is to be able to use shifts later per pixel instead of divides
// so the distance between points is converted from w&h based to w2&h2 based which are of the 2^x form

    long temp_v1_1 = 16*(vop_ref[0][0] + w2);
    long temp_v1_2_1 = (w - w2);
    long temp_v1_2_2 = (r*sprite_ref[0][0] - 16*vop_ref[0][0]);
    long temp_v1_2 = temp_v1_2_1 * temp_v1_2_2;
    long temp_v1_3_2 = 16 * -vop_ref[1][0];
    long temp_v1_3_1 = (r*sprite_ref[1][0] + temp_v1_3_2);
    long temp_v1_3 = w2 * temp_v1_3_1;
    long temp_v1_4 = ( temp_v1_2 + temp_v1_3);
    long temp_v1_5 = ROUNDED_DIV(temp_v1_4,w);
    virtual_ref[0][0]= temp_v1_1 + temp_v1_5;

    long temp_v2_1 = 16*vop_ref[0][1];
    long temp_v2_2_1 = (w - w2);
    long temp_v2_2_2 = (r * sprite_ref[0][1] - 16*vop_ref[0][1]);
    long temp_v2_2 = temp_v2_2_1 * temp_v2_2_2;
    long temp_v2_3 = w2*(r*sprite_ref[1][1] - 16*vop_ref[1][1]);
    long temp_v2_4 = (temp_v2_2 + temp_v2_3);
    long temp_v2_5 = ROUNDED_DIV(temp_v2_4,w);
    virtual_ref[0][1]= temp_v2_1 + temp_v2_5;

    long temp_v3_1 = 16*vop_ref[0][0];
    long temp_v3_2_1 = (h - h2);
    long temp_v3_2_2 = (r* sprite_ref[0][0] - 16*vop_ref[0][0]);
    long temp_v3_2 = temp_v3_2_1 * temp_v3_2_2;
    long temp_v3_3 = h2*(r* sprite_ref[2][0] - 16*vop_ref[2][0]);
    long temp_v3_4 = ( temp_v3_2 + temp_v3_3);
    long temp_v3_5 = ROUNDED_DIV(temp_v3_4,h);
    virtual_ref[1][0]= temp_v3_1 + temp_v3_5;

    long temp_v4_1 = 16*(vop_ref[0][1] + h2);
    long temp_v4_2_1 = (h - h2);
    long temp_v4_2_2 = (r * sprite_ref[0][1] - 16*vop_ref[0][1]);
    long temp_v4_2 = temp_v4_2_1 * temp_v4_2_2;
    long temp_v4_3 = h2*(r*sprite_ref[2][1] - 16*vop_ref[2][1]);
    long temp_v4_4 = ( temp_v4_2 + temp_v4_3);
    long temp_v4_5 = ROUNDED_DIV(temp_v4_4,h);
    virtual_ref[1][1]= temp_v4_1 + temp_v4_5;

    //warping
    long w2_h2_r = w2*h2*r;
    long temp_v1_6 = (-r)*sprite_ref[0][0] + virtual_ref[0][0];
    long temp_v1_7 = (-r)*sprite_ref[0][0] + virtual_ref[1][0];
    long virtual_ref_0_0_adv_1 = h2 * temp_v1_6;
    long virtual_ref_0_0_adv_2 = w2 * temp_v1_7;
    long virtual_ref_0_0_adv = virtual_ref_0_0_adv_1 + virtual_ref_0_0_adv_2;
    vop->virtual_ref[0][0] = SIGNED_ROUNDED_DIV(virtual_ref_0_0_adv,w2_h2_r) + sprite_ref[0][0];

    long temp_v2_6 = (-r)*sprite_ref[0][1] + virtual_ref[0][1];
    long temp_v2_7 = (-r)*sprite_ref[0][1] + virtual_ref[1][1];
    long virtual_ref_0_1_adv_1 = h2 * temp_v2_6;
    long virtual_ref_0_1_adv_2 = w2 * temp_v2_7;
    long virtual_ref_0_1_adv = virtual_ref_0_1_adv_1 + virtual_ref_0_1_adv_2;
    vop->virtual_ref[0][1] = SIGNED_ROUNDED_DIV(virtual_ref_0_1_adv,w2_h2_r) + sprite_ref[0][1];

    long temp_v3_6 = (-r)*sprite_ref[0][0] + virtual_ref[0][0];
    long temp_v3_7 = (-r)*sprite_ref[0][0] + virtual_ref[1][0];
    long virtual_ref_1_0_adv_1 = w2 * temp_v3_6;
    long virtual_ref_1_0_adv_2 = h2 * temp_v3_7;
    long virtual_ref_1_0_adv_3 = 2*w2_h2_r*sprite_ref[0][0];
    long virtual_ref_1_0_adv_4 = 16*w2*h2;
    long virtual_ref_1_0_adv = virtual_ref_1_0_adv_1 + virtual_ref_1_0_adv_2 + 
                                    virtual_ref_1_0_adv_3 - virtual_ref_1_0_adv_4;
    vop->virtual_ref[1][0] = SIGNED_ROUNDED_DIV(virtual_ref_1_0_adv,4*w2_h2_r);

    long temp_v4_6 = (-r)*sprite_ref[0][1] + virtual_ref[0][1];
    long temp_v4_7 = (-r)*sprite_ref[0][1] + virtual_ref[1][1];
    long virtual_ref_1_1_adv_1 = w2 * temp_v4_6;
    long virtual_ref_1_1_adv_2 = h2 * temp_v4_7;
    long virtual_ref_1_1_adv_3 = 2*w2_h2_r*sprite_ref[0][1];
    long virtual_ref_1_1_adv_4 = 16*w2*h2;
    long virtual_ref_1_1_adv = virtual_ref_1_1_adv_1 + virtual_ref_1_1_adv_2 +
                                    virtual_ref_1_1_adv_3 - virtual_ref_1_1_adv_4;
    vop->virtual_ref[1][1] = SIGNED_ROUNDED_DIV(virtual_ref_1_1_adv,4*w2_h2_r);


    int runs = LOG2CEIL(w2*h2*r);
    unsigned int mask = 1 << runs;
    long normalize_save = virtual_ref_0_0_adv_2 | virtual_ref_0_1_adv_1 | virtual_ref_0_0_adv_1 | virtual_ref_0_1_adv_2;
    unsigned int normalize = normalize_save | mask;
    long save_virtual_ref_0_0_adv_2 = virtual_ref_0_0_adv_2;
    long save_virtual_ref_0_1_adv_1 = virtual_ref_0_1_adv_1;
    long save_virtual_ref_0_0_adv_1 = virtual_ref_0_0_adv_1;
    long save_virtual_ref_0_1_adv_2 = virtual_ref_0_1_adv_2;

    while (/*(runs > 0) && */((normalize & 0x1) == 0 ))
    {
        virtual_ref_0_0_adv_2 >>= 1;
        virtual_ref_0_1_adv_1 >>= 1;
        virtual_ref_0_0_adv_1 >>= 1;
        virtual_ref_0_1_adv_2 >>= 1;
        mask >>= 1;
        normalize = virtual_ref_0_0_adv_2 | virtual_ref_0_1_adv_1 | 
                    virtual_ref_0_0_adv_1 | virtual_ref_0_1_adv_2 | mask;
        runs -= 1;
    }
    int w2_4 = w2 * 4;
    int w2_2 = w2 * 2;

    int foo1 = r * h2 * w2_4;
    int runs2 = LOG2CEIL(foo1);
    int runs2_1 = runs2 - 1;
    int mask2 = 1 << runs2_1;
    unsigned long long l_mask2 = mask2;

    int foo3 = h2 * temp_v1_1;
    int foo4 = h2 * w2_2;

    long long l_foo3 = foo3;
    long long l_foo4 = foo4;

    long long l_normalize_save = normalize_save;
    unsigned long long l_mask = l_normalize_save | l_mask2;
    l_foo4 = r * l_foo4;
    long long l_foo5 = l_foo4 * sprite_ref[0][0];
    long long l_foo6 = l_foo4 * temp_v4_2_2;
    long long sub_1 = l_foo5 - l_foo3;
    sub_1 += l_foo4;
    l_foo6 -= l_foo3;
    unsigned long long l_mask3 = l_mask | sub_1;
    l_foo6 += l_foo4;

    unsigned long long testmask = l_foo6 | l_normalize_save;
    unsigned int mask4;
#if 1
    while(runs2 > 0 && ((testmask & 0x1) == 0))
    {
        save_virtual_ref_0_0_adv_2 >>= 1;
        save_virtual_ref_0_1_adv_1 >>= 1;
        save_virtual_ref_0_0_adv_1 >>= 1;
        save_virtual_ref_0_1_adv_2 >>= 1;
        l_foo6 >>= 1;
        l_foo3 >>= 1; 
        sub_1 >>= 1;
        mask2 >>= 1;
        mask4 = save_virtual_ref_0_0_adv_2 | save_virtual_ref_0_1_adv_1 | save_virtual_ref_0_0_adv_1 | save_virtual_ref_0_1_adv_2;
        testmask = l_foo6 | sub_1 | (long long)mask2 | (long long)mask4;
        runs2--;
    }
    vop->virtual_ref2[0][0] = save_virtual_ref_0_0_adv_1;
    vop->virtual_ref2[0][1] = save_virtual_ref_0_0_adv_2;
    vop->virtual_ref2[1][0] = save_virtual_ref_0_1_adv_1;
    vop->virtual_ref2[1][1] = save_virtual_ref_0_1_adv_2;
    vop->socx = (int)sub_1;
    vop->socy = (int)l_foo6;
    vop->mask2 = mask2;
#endif

    switch(vol->no_of_sprite_warping_points)
    {
        case 0:
            vop->sprite_offset[0][0]= 0;
            vop->sprite_offset[0][1]= 0;
            vop->sprite_offset[1][0]= 0;
            vop->sprite_offset[1][1]= 0;
            vop->sprite_delta[0][0]= a;
            vop->sprite_delta[0][1]= 0;
            vop->sprite_delta[1][0]= 0;
            vop->sprite_delta[1][1]= a;
            vop->sprite_shift[0]= 0;
            vop->sprite_shift[1]= 0;
            break;
        case 1: //GMC only
            vop->sprite_offset[0][0]= sprite_ref[0][0] - a*vop_ref[0][0];
            vop->sprite_offset[0][1]= sprite_ref[0][1] - a*vop_ref[0][1];
            vop->sprite_offset[1][0]= ((sprite_ref[0][0]>>1)|(sprite_ref[0][0]&1)) - a*(vop_ref[0][0]/2);
            vop->sprite_offset[1][1]= ((sprite_ref[0][1]>>1)|(sprite_ref[0][1]&1)) - a*(vop_ref[0][1]/2);
            vop->sprite_delta[0][0]= a;
            vop->sprite_delta[0][1]= 0;
            vop->sprite_delta[1][0]= 0;
            vop->sprite_delta[1][1]= a;
            vop->sprite_shift[0]= 0;
            vop->sprite_shift[1]= 0;
            break;
        case 2:
            vop->sprite_offset[0][0]= (sprite_ref[0][0]<<(alpha+rho))
                                                  + (-r*sprite_ref[0][0] + virtual_ref[0][0])*(-vop_ref[0][0])
                                                  + ( r*sprite_ref[0][1] - virtual_ref[0][1])*(-vop_ref[0][1])
                                                  + (1<<(alpha+rho-1));
            vop->sprite_offset[0][1]= (sprite_ref[0][1]<<(alpha+rho))
                                                  + (-r*sprite_ref[0][1] + virtual_ref[0][1])*(-vop_ref[0][0])
                                                  + (-r*sprite_ref[0][0] + virtual_ref[0][0])*(-vop_ref[0][1])
                                                  + (1<<(alpha+rho-1));
            vop->sprite_offset[1][0]= ( (-r*sprite_ref[0][0] + virtual_ref[0][0])*(-2*vop_ref[0][0] + 1)
                                     +( r*sprite_ref[0][1] - virtual_ref[0][1])*(-2*vop_ref[0][1] + 1)
                                     +2*w2*r*sprite_ref[0][0]
                                     - 16*w2
                                     + (1<<(alpha+rho+1)));
            vop->sprite_offset[1][1]= ( (-r*sprite_ref[0][1] + virtual_ref[0][1])*(-2*vop_ref[0][0] + 1)
                                     +(-r*sprite_ref[0][0] + virtual_ref[0][0])*(-2*vop_ref[0][1] + 1)
                                     +2*w2*r*sprite_ref[0][1]
                                     - 16*w2
                                     + (1<<(alpha+rho+1)));
            vop->sprite_delta[0][0]=   (-r*sprite_ref[0][0] + virtual_ref[0][0]);
            vop->sprite_delta[0][1]=   (+r*sprite_ref[0][1] - virtual_ref[0][1]);
            vop->sprite_delta[1][0]=   (-r*sprite_ref[0][1] + virtual_ref[0][1]);
            vop->sprite_delta[1][1]=   (-r*sprite_ref[0][0] + virtual_ref[0][0]);

            vop->sprite_shift[0]= alpha+rho;
            vop->sprite_shift[1]= alpha+rho+2;
            break;
        case 3:
            min_ab= alpha < beta ? alpha : beta;
            w3= w2>>min_ab;
            h3= h2>>min_ab;
#if 0
            vop->sprite_offset[0][0]=  (sprite_ref[0][0]<<(alpha+beta+rho-min_ab))
                                   + (-r*sprite_ref[0][0] + virtual_ref[0][0])*h3*(-vop_ref[0][0])
                                   + (-r*sprite_ref[0][0] + virtual_ref[1][0])*w3*(-vop_ref[0][1])
                                   + (1<<(alpha+beta+rho-min_ab-1));
#endif
            term1  = (sprite_ref[0][0]<<(alpha+beta+rho-min_ab));
            temp  = (-r*sprite_ref[0][0] + virtual_ref[0][0]);
            term2  = temp * h3 * (-vop_ref[0][0]);
            temp  = (-r*sprite_ref[0][0] + virtual_ref[1][0]);
            term3  = temp * w3 * (-vop_ref[0][1]);
            term4  = (1<<(alpha+beta+rho-min_ab-1));
            vop->sprite_offset[0][0]=  term1
                                   + term2
                                   + term3
                                   + term4;

            int temp_sp_1_1 = (sprite_ref[0][1]<<(alpha+beta+rho-min_ab));
            int temp_sp_1_2 = (-r*sprite_ref[0][1] + virtual_ref[0][1])*h3*(-vop_ref[0][0]);
            int temp_sp_1_3 = (-r*sprite_ref[0][1] + virtual_ref[1][1])*w3*(-vop_ref[0][1]);
            int temp_sp_1_4 = (1<<(alpha+beta+rho-min_ab-1));
            vop->sprite_offset[0][1]=  temp_sp_1_1
                                   + temp_sp_1_2
                                   + temp_sp_1_3
                                   + temp_sp_1_4;

            int temp_sp_2_1 = (-r*sprite_ref[0][0] + virtual_ref[0][0])*h3*(-2*vop_ref[0][0] + 1);
            int temp_sp_2_2 = (-r*sprite_ref[0][0] + virtual_ref[1][0])*w3*(-2*vop_ref[0][1] + 1);
            int temp_sp_2_3 = 2*w2*h3*r*sprite_ref[0][0];
            int temp_sp_2_4 = 16*w2*h3;
            int temp_sp_2_5 = (1<<(alpha+beta+rho-min_ab+1));
            vop->sprite_offset[1][0]=  temp_sp_2_1
                                   + temp_sp_2_2
                                   + temp_sp_2_3
                                   - temp_sp_2_4
                                   + temp_sp_2_5;

            int temp_sp_3_1 = (-r*sprite_ref[0][1] + virtual_ref[0][1])*h3*(-2*vop_ref[0][0] + 1);
            int temp_sp_3_2 = (-r*sprite_ref[0][1] + virtual_ref[1][1])*w3*(-2*vop_ref[0][1] + 1);
            int temp_sp_3_3 = 2*w2*h3*r*sprite_ref[0][1];
            int temp_sp_3_4 = 16*w2*h3;
            int temp_sp_3_5 = (1<<(alpha+beta+rho-min_ab+1));
            vop->sprite_offset[1][1]=  temp_sp_3_1
                                   + temp_sp_3_2
                                   + temp_sp_3_3
                                   - temp_sp_3_4
                                   + temp_sp_3_5;

            vop->sprite_delta[0][0]=   (-r*sprite_ref[0][0] + virtual_ref[0][0])*h3;
            vop->sprite_delta[0][1]=   (-r*sprite_ref[0][0] + virtual_ref[1][0])*w3;
            vop->sprite_delta[1][0]=   (-r*sprite_ref[0][1] + virtual_ref[0][1])*h3;
            vop->sprite_delta[1][1]=   (-r*sprite_ref[0][1] + virtual_ref[1][1])*w3;

            vop->sprite_shift[0]= runs; //alpha + beta + rho - min_ab;
            vop->sprite_shift[1]= runs2; //alpha + beta + rho - min_ab + 2;

            vop->mv5_upper = vop->sprite_ref[0][0] & 0x7fff;
            vop->mv5_lower = vop->sprite_ref[0][1] & 0x7fff; 
            vop->mv6_upper = (((vop->socx + save_virtual_ref_0_0_adv_1 + save_virtual_ref_0_0_adv_2) >> vop->sprite_shift[1]) << rho) & 0x7fff;
            vop->mv6_lower = (((vop->socy + save_virtual_ref_0_1_adv_1 + save_virtual_ref_0_1_adv_2) >> vop->sprite_shift[1]) << rho) & 0x7fff;

            break;
    }
#if 1
    /* try to simplify the situation */
    if(   vop->sprite_delta[0][0] == a<<vop->sprite_shift[0]
       && vop->sprite_delta[0][1] == 0
       && vop->sprite_delta[1][0] == 0
       && vop->sprite_delta[1][1] == a<<vop->sprite_shift[0])
    {
        vop->sprite_offset_impr[0][0]>>=vop->sprite_shift[0];
        vop->sprite_offset_impr[0][1]>>=vop->sprite_shift[0];
        vop->sprite_offset_impr[1][0]>>=vop->sprite_shift[1];
        vop->sprite_offset_impr[1][1]>>=vop->sprite_shift[1];
        vop->sprite_delta_impr[0][0]= a;
        vop->sprite_delta_impr[0][1]= 0;
        vop->sprite_delta_impr[1][0]= 0;
        vop->sprite_delta_impr[1][1]= a;
        vop->sprite_shift_impr[0]= 0;
        vop->sprite_shift_impr[1]= 0;
        vop->real_sprite_warping_points=1;
    }
    else{
        int shift_y= 16 - vop->sprite_shift[0];
        int shift_c= 16 - vop->sprite_shift[1];
        for(i=0; i<2; i++){
            vop->sprite_offset_impr[0][i] = vop->sprite_offset[0][i] << shift_y;
            vop->sprite_offset_impr[1][i] = vop->sprite_offset[0][i] << shift_c;
            vop->sprite_delta_impr[0][i] = vop->sprite_delta[0][i] << shift_y;
            vop->sprite_delta_impr[1][i] = vop->sprite_delta[0][i] << shift_y;
            vop->sprite_shift_impr[i]= 16;
        }
        vop->real_sprite_warping_points=vol->no_of_sprite_warping_points;
    }
#endif
    return 0;
}
static int mpeg4_process_macroblock(bitstream *bs, decoder_ctx_t *decoder)
{
    mp4_private_t *priv = (mp4_private_t *)decoder->private;
    vop_header_t *h = &priv->vop_header;

    int marker_length = mpeg4_calcResyncMarkerLength(priv);
    int mba = 0;
    int result;
    do {
        result = macroblock(bs, priv);
        mba++;
    } while(bits_left(bs) && (nextbits_bytealigned(bs, 23) != 0) &&
            nextbits_bytealigned(bs, marker_length) != 1);
    h->num_gop_mbas = mba;
}

static int decode_vop_header(bitstream *bs, VdpPictureInfoMPEG4Part2 const *info, decoder_ctx_t *decoder)
{
    int dummy;
    mp4_private_t *priv = (mp4_private_t *)decoder->private;
    vop_header_t *h = &priv->vop_header;
    VdpDecoderMpeg4VolHeader *vol = &priv->mpeg4VolHdr;

	h->vop_coding_type = get_bits(bs, 2);

	// modulo_time_base
	while (get_bits(bs, 1) != 0);

	if (get_bits(bs, 1) != 1)
		VDPAU_DBG("vop header marker error");

	// vop_time_increment
	get_bits(bs, 32 - __builtin_clz(info->vop_time_increment_resolution));

	if (get_bits(bs, 1) != 1)
		VDPAU_DBG("vop header marker error");

	// vop_coded
	if (!get_bits(bs, 1))
		return 0;

        if(vol->newpred_enable)
        {
            //t.b.d
        }
        if((vol->video_object_layer_shape != BIN_ONLY_SHAPE) &&
            (h->vop_coding_type == VOP_P ||
            (h->vop_coding_type == VOP_S && vol->sprite_enable == GMC_SPRITE)))
		get_bits(bs, 1);

        if ((vol->reduced_resolution_vop_enable) &&
            (vol->video_object_layer_shape == RECT_SHAPE) &&
            ((h->vop_coding_type == VOP_P) || (h->vop_coding_type == VOP_I)))
                h->vop_reduced_resolution  = get_bits(bs,1);

        if (vol->video_object_layer_shape != RECT_SHAPE) { 
            if(!(vol->sprite_enable == STATIC_SPRITE && h->vop_coding_type == VOP_I)) { 
                h->vop_width = get_bits(bs, 13);
                if (get_bits(bs, 1) != 1)
                        VDPAU_DBG("vop header marker error");
                h->vop_height = get_bits(bs, 13);
                if (get_bits(bs, 1) != 1)
                        VDPAU_DBG("vop header marker error");
                h->vop_horizontal_mc_spatial_ref =get_bits(bs, 13);
                if (get_bits(bs, 1) != 1)
                        VDPAU_DBG("vop header marker error");
                h->vop_vertical_mc_spatial_ref =get_bits(bs, 13);
                if (get_bits(bs, 1) != 1)
                        VDPAU_DBG("vop header marker error");
            } 
            if ((vol->video_object_layer_shape != BIN_ONLY_SHAPE) &&
                vol->scalability && vol->enhancement_type)
                    h->background_composition = get_bits(bs, 1);
            h->change_conv_ratio_disable =get_bits(bs, 1);
            h->vop_constant_alpha = get_bits(bs, 1);
            if (h->vop_constant_alpha) 
                h->vop_constant_alpha_value = get_bits(bs, 8);
        } 
        if (vol->video_object_layer_shape != BIN_ONLY_SHAPE) 
            if (!vol->complexity_estimation_disable) {
                //read_vop_complexity_estimation_header();
            }
        if (vol->video_object_layer_shape != BIN_ONLY_SHAPE) { 
            h->intra_dc_vlc_thr = get_bits(bs, 3);
            if (vol->interlaced) { 
                h->top_field_first = get_bits(bs, 1);
                h->alternate_vertical_scan_flag = get_bits(bs, 1);
            } 
        } 
        if ((vol->sprite_enable == STATIC_SPRITE || vol->sprite_enable==GMC_SPRITE) &&
            h->vop_coding_type == VOP_S) {
            if (vol->no_of_sprite_warping_points > 0) 
                mpeg4_decode_sprite_trajectory(bs, priv); 
#if 0
            if (vol->sprite_brightness_change)
                brightness_change_factor() 
            if(vol->sprite_enable == STATIC_SPRITE) { 
                if (h->sprite_transmit_mode != “stop”
                    && vol->low_latency_sprite_enable) {
                    do { 
                        h->sprite_transmit_mode = get_bits(bs, 2);
                        if ((h->sprite_transmit_mode == “piece”) ||
                            (h->sprite_transmit_mode == “update”))
                            decode_sprite_piece() 
                    } while (h->sprite_transmit_mode != “stop” &&
                        h->sprite_transmit_mode != “pause”)
                } 
                next_start_code() 
                return() 
            } 
#endif
        } 

        if(vol->video_object_layer_shape != BIN_ONLY_SHAPE) {
            h->vop_quant = get_bits(bs, vol->quant_precision);

            // vop_fcode_forward
            if (h->vop_coding_type != VOP_I)
                h->fcode_forward=get_bits(bs, 3);
    
            // vop_fcode_backward
            if (h->vop_coding_type == VOP_B)
                h->fcode_backward=get_bits(bs, 3);
    
            if(!vol->scalability) {
                if(vol->video_object_layer_shape != RECT_SHAPE && 
                        (h->vop_coding_type != VOP_I)) {
                    int vop_shape_coding_type = get_bits(bs, 1);
                }
                //motion_shape_texture()
                //mpeg4_process_macroblock(bs, decoder);
                //video_packet_header()
		/*
		bitstream bs_saved = *bs;
		mp4_private_t _priv = *priv;
		int marker_length = mpeg4_calcResyncMarkerLength(priv);
		if(find_resynccode(bs, marker_length))
                	mpeg4_decode_packet_header(bs, 
                                        info,
                                        decoder, 
                                        &_priv);
		priv->pkt_hdr.curr_mb_num = _priv.pkt_hdr.mb_num;

		*bs = bs_saved;
		*/
                //motion_shape_texture()
                //mpeg4_process_macroblock(bs, decoder);
            }
            else {
                if(vol->enhancement_type) {
                    int load_backward_shape = get_bits(bs, 1);
                    if(load_backward_shape) {
                        int backward_shape_width = get_bits(bs, 13);
                        //marker_bits
                        get_bits(bs,1);
                        int backward_shape_height = get_bits(bs, 13);
                        //marker_bits
                        get_bits(bs,1);
                        int backward_shape_horizontal_mc_spatial_ref = get_bits(bs, 13);
                        //marker_bits
                        get_bits(bs,1);
                        int backward_shape_vertical_mc_spatial_ref = get_bits(bs, 13);
                        //marker_bits
                        get_bits(bs,1);
                        //backward_shape()
                        int load_forward_shape = get_bits(bs, 1);
                        if(load_forward_shape) {
                            int forward_shape_width = get_bits(bs, 13);
                            //marker_bits
                            get_bits(bs,1);
                            int forward_shape_height = get_bits(bs, 13);
                            //marker_bits
                            get_bits(bs,1);
                            int forward_shape_horizontal_mc_spatial_ref = get_bits(bs,13);
                            //marker_bits
                            get_bits(bs,1);
                            int forward_shape_vertical_mc_spatial_ref = get_bits(bs,13);
                            //forward_shape()
                        }
                    }
                }
                int ref_select_code = get_bits(bs, 2);
                //combined_motion_shape_texture() 
                //mpeg4_process_macroblock(bs, decoder);
            }
        }
        else {
		printf("unimplemented leg\n");
            //combined_motion_shape_texture() 
        }
	return 1;
}
#if 1
int mpeg4_decode_packet_header(bitstream *gb, VdpPictureInfoMPEG4Part2 const *info, decoder_ctx_t *decoder, mp4_private_t *priv)
{
    video_packet_header_t *h = &priv->pkt_hdr;
    VdpDecoderMpeg4VolHeader *vol = &priv->mpeg4VolHdr;
    int mb_width = (vol->video_object_layer_width+15)/16;
    int mb_height = (vol->video_object_layer_height+15)/16;
    int mb_num_calc = mb_width * mb_height;
    
    h->mb_width = mb_width;
    h->mb_height = mb_height;
    
    int mb_num_bits      = (31 - __builtin_clz((mb_num_calc - 1)|1)) + 1;
    int header_extension = 0, mb_num, len;

#if 0
    /* is there enough space left for a video packet + header */
    if (get_bits_count(gb) > s->gb.size_in_bits - 20)
        return -1;
#endif

    for (len = 0; len < 32; len++)
        if (get_bits(gb,1))
            break;
#if 0
    if (len != ff_mpeg4_get_video_packet_prefix_length(s)) {
        av_log(s->avctx, AV_LOG_ERROR, "marker does not match f_code\n");
        return -1;
    }
#endif

    if (vol->video_object_layer_shape != RECT_SHAPE) {
        header_extension = get_bits(gb,1);
        // FIXME more stuff here
    }

    mb_num = get_bits(gb, mb_num_bits);
    if (mb_num >= mb_num_calc) {
        printf("illegal mb_num in video packet (%d %d) \n", mb_num, mb_num_calc);
        return -1;
    }
    h->mb_num = mb_num;
    
    h->mb_x = mb_num % mb_width;
    h->mb_y = mb_num / mb_width;

    if (vol->video_object_layer_shape != BIN_ONLY_SHAPE) {
        int qscale = get_bits(gb, vol->quant_precision);
#if 1
        if (qscale) {
            //vol->chroma_qscale = vol->qscale = qscale;
            priv->vop_header.vop_quant = qscale;
        }
#endif
    }

    if (vol->video_object_layer_shape == RECT_SHAPE)
        header_extension = get_bits(gb,1);

    if (header_extension) {
        int time_incr = 0;

        while (get_bits(gb,1) != 0)
            time_incr++;

        get_bits(gb, 1);
        //get_bits(gb, vol->time_increment_bits);      /*        time_increment */
        int time_increment_bits = (31 - __builtin_clz((vol->vop_time_increment_resolution - 1)|1)) + 1;
        get_bits(gb, time_increment_bits);
        
        get_bits(gb, 1);

        get_bits(gb, 2); /* vop coding type */
        // FIXME not rect stuff here

        if (vol->video_object_layer_shape != BIN_ONLY_SHAPE) {
            get_bits(gb, 3); /* intra dc vlc threshold */
            // FIXME don't just ignore everything

            if (priv->vop_header.vop_coding_type == VOP_S &&
                vol->sprite_enable == GMC_SPRITE) {
#if 1
                if (mpeg4_decode_sprite_trajectory(gb, priv) < 0)
                    return 1;
#endif
            }

            // FIXME reduced res stuff here

            if (vol->video_object_type_indication != AV_PICTURE_TYPE_I) {
                int f_code = get_bits(gb, 3);       /* fcode_for */
                if (f_code == 0)
                    printf("Error, video packet header damaged (f_code=0)\n");
            }
            if (vol->video_object_type_indication == AV_PICTURE_TYPE_B) {
                int b_code = get_bits(gb, 3);
                if (b_code == 0)
                    printf("Error, video packet header damaged (b_code=0)\n");
            }
        }
    }
}
#endif

static int mpeg4_calcResyncMarkerLength(mp4_private_t *decoder_p)
{
    int marker_length = 0;

    if(decoder_p->mpeg4VolHdr.video_object_layer_shape == BIN_SHAPE)
        marker_length=17;
    else {
        if(decoder_p->vop_header.vop_coding_type == VOP_I)
            marker_length = 17;
        else if(decoder_p->vop_header.vop_coding_type == VOP_B) {
            int fcode = ((decoder_p->vop_header.fcode_forward) > (decoder_p->vop_header.fcode_backward) ?  
                (decoder_p->vop_header.fcode_forward) : (decoder_p->vop_header.fcode_backward));
            marker_length=15+fcode < 17 ? 15+fcode : 17;
            //add +1 for 1 bit
            marker_length +=1;
        }
        else if(decoder_p->vop_header.vop_coding_type == VOP_P) {
            marker_length = 15+decoder_p->vop_header.fcode_forward + 1;
        }
    }
    return marker_length;
}

static unsigned long num_pics=0;
static unsigned long num_longs=0;
int mpeg4_decode(decoder_ctx_t *decoder, VdpPictureInfoMPEG4Part2 const *_info, const int len, video_surface_ctx_t *output)
{
    VdpPictureInfoMPEG4Part2 const *info = (VdpPictureInfoMPEG4Part2 const *)_info;
    mp4_private_t *decoder_p = (mp4_private_t *)decoder->private;
    VdpDecoderMpeg4VolHeader *vol = &decoder_p->mpeg4VolHdr;

    uint32_t    startcode;
    int        more_mbs = 1;
    uint32_t   mp4mbaAddr_reg = 0;
    decoder_p->pkt_hdr.mb_xpos = 0;
    decoder_p->pkt_hdr.mb_ypos = 0;
    uint32_t mba_reg = 0x0;
    static int vop_s_frame_seen = 0;
    int last_mba = 0;

#if 1
    if(!decoder_p->mpeg4VolHdrSet)
    {
        VDPAU_DBG("MPEG4 VOL Header must be set prior decoding of frames! Sorry");
        return VDP_STATUS_ERROR;
    }
#endif
/*
	if(info->resync_marker_disable)
	{
	        VDPAU_DBG("video without resync marker not supported! Sorry");
        	return VDP_STATUS_ERROR;
	}
*/
	int i;
	void *ve_regs = ve_get_regs();
	bitstream bs = { .data = decoder->data, .length = len, .bitpos = 0 };
    
	while (find_startcode(&bs))
	{
            startcode = get_bits(&bs, 8);
            if ( startcode != 0xb6)
                            continue;

            if (!decode_vop_header(&bs, info, decoder))
                    continue;

#if 0
            bitstream bs1 = bs;
            macroblock(&bs, decoder_p);
            bs=bs1;
#endif
            ve_regs = ve_get(VE_ENGINE_MPEG, 0);
            // activate MPEG engine
            writel((readl(ve_regs + VE_CTRL) & ~0xf) | 0x0, ve_regs + VE_CTRL);
            
#if 1
            // set quantisation tables
            for (i = 0; i < 64; i++)
                writel((uint32_t)(64 + i) << 8 | info->intra_quantizer_matrix[i], ve_regs + VE_MPEG_IQ_MIN_INPUT);
            for (i = 0; i < 64; i++)
                writel((uint32_t)(i) << 8 | info->non_intra_quantizer_matrix[i], ve_regs + VE_MPEG_IQ_MIN_INPUT);
#endif

            // set forward/backward predicion buffers
            if (info->forward_reference != VDP_INVALID_HANDLE)
            {
                video_surface_ctx_t *forward = handle_get(info->forward_reference);
                if(forward)
                {
                   writel(ve_virt2phys(forward->data), ve_regs + VE_MPEG_FWD_LUMA);
                   writel(ve_virt2phys(forward->data + forward->plane_size), ve_regs + VE_MPEG_FWD_CHROMA);
                }
            }
            if (info->backward_reference != VDP_INVALID_HANDLE)
            {
                video_surface_ctx_t *backward = handle_get(info->backward_reference);
                if(backward)
                {
                   writel(ve_virt2phys(backward->data), ve_regs + VE_MPEG_BACK_LUMA);
                   writel(ve_virt2phys(backward->data + backward->plane_size), ve_regs + VE_MPEG_BACK_CHROMA);
                }
            }
            else
            {
                writel(0x0, ve_regs + VE_MPEG_BACK_LUMA);
                writel(0x0, ve_regs + VE_MPEG_BACK_CHROMA);
            }

            // set trb/trd
            if (decoder_p->vop_header.vop_coding_type == VOP_B)
            {
                writel((info->trb[0] << 16) | (info->trd[0] << 0), ve_regs + VE_MPEG_TRBTRD_FRAME);
                // unverified:
                //writel((info->trb[1] << 16) | (info->trd[1] << 0), ve_regs + VE_MPEG_TRBTRD_FIELD);
                writel(0, ve_regs + VE_MPEG_TRBTRD_FIELD);
            }
            // set size
            uint16_t width  = (decoder_p->mpeg4VolHdr.video_object_layer_width + 15) / 16;
            uint16_t height = (decoder_p->mpeg4VolHdr.video_object_layer_height + 15) / 16;
            if(width == 0 || height == 0) {
                //some videos do not have a VOL, at least at the right time
                //try with the following parameters
                width = ((decoder->width + 15) / 16);
                height = ((decoder->height + 15) / 16);
            }

            writel((width <<16) | (width << 8) | height, ve_regs + VE_MPEG_SIZE);
            writel(((width * 16) << 16) | (height * 16), ve_regs + VE_MPEG_FRAME_SIZE);

            // set buffers
            writel(ve_virt2phys(decoder_p->mbh_buffer), ve_regs + VE_MPEG_MBH_ADDR);
            writel(ve_virt2phys(decoder_p->dcac_buffer), ve_regs + VE_MPEG_DCAC_ADDR);
            writel(ve_virt2phys(decoder_p->ncf_buffer), ve_regs + VE_MPEG_NCF_ADDR);

            // set output buffers (Luma / Croma)
            writel(ve_virt2phys(output->data), ve_regs + VE_MPEG_REC_LUMA);
            writel(ve_virt2phys(output->data + output->plane_size), ve_regs + VE_MPEG_REC_CHROMA);
            writel(ve_virt2phys(output->data), ve_regs + VE_MPEG_ROT_LUMA);
            writel(ve_virt2phys(output->data + output->plane_size), ve_regs + VE_MPEG_ROT_CHROMA);

            uint32_t rotscale = 0;
            //bit 0-3: rotate_angle
            //bit 4-5: horiz_scale_ratio
            //bit 6-7: vert_scale_ratio
            //bit 8-11: scale_down_enable
            //bit 12-14: ?
            //bit 16-19: ?
            //bit 20-24: value 6
            //bit 30: 1 
            //bit 31: 0
            const int no_scale = 2;
            const int no_rotate = 6;
            rotscale |= 0x40620000;
            writel(rotscale, ve_regs + VE_MPEG_SDROT_CTRL);

                        // ??
            uint32_t ve_control = 0;
            ve_control |= 0x80000000;
            if(decoder_p->vop_header.vop_coding_type != VOP_I && vol->quarter_sample != 0)
                ve_control |= (1 << 20);

            ve_control |= (1 << 19);
            //if not divx then 1, else 2 (but divx version dependent)
            ve_control |= (1 << 14);
            // if p-frame and some other conditions
            //if(p-frame && 
            ve_control |= (decoder_p->vop_header.vop_coding_type == VOP_P ? 0x1 : 0x0) << 12;
            ve_control |= (1 << 8);
            ve_control |= (1 << 7);
            ve_control |= (1 << 4);
            ve_control |= (1 << 3);
            //ve_control = 0x80084198;
            writel(ve_control, ve_regs + VE_MPEG_CTRL);
#if 0
            writel(0x800001b8, ve_regs + VE_MPEG_CTRL);
#endif
            mp4mbaAddr_reg = 0;
            writel(mp4mbaAddr_reg, ve_regs + VE_MPEG_MBA);

            int marker_length = mpeg4_calcResyncMarkerLength(decoder_p);

            while(more_mbs == 1) {

                //workaround: currently it is unclear what the meaning of bit 20/21 is
                if(decoder_p->vop_header.vop_coding_type == VOP_S)
                    vop_s_frame_seen = 3;

                uint32_t vop_hdr = 0;
                vop_hdr |= (info->interlaced & 0x1) << 30;
                int vop_coding;
                switch(decoder_p->vop_header.vop_coding_type) {
                    case VOP_B:
                        vop_coding = 1;
                        if(decoder_p->vop_header.last_coding_type == VOP_S)
                            vop_coding = 3;
                        break;
                    default:
                        vop_coding = 0;
                }
                vop_hdr |= vop_coding << 28;
                vop_hdr |= (vol->no_of_sprite_warping_points & 0x3) << 25;
                vop_hdr |= (info->quant_type & 0x1) << 24;
                vop_hdr |= (info->quarter_sample & 0x1) << 23;
                vop_hdr |= (info->resync_marker_disable & 0x1) << 22; //error_res_disable
                vop_hdr |= (vop_s_frame_seen & 0x3) << 20;
                vop_hdr |= (decoder_p->vop_header.vop_coding_type & 0x3) << 18;
                vop_hdr |= (info->rounding_control &0x1) << 17;
                if(vol->sprite_enable != GMC_SPRITE) 
                    vop_hdr |= (decoder_p->vop_header.intra_dc_vlc_thr & 0x7) << 8;
                vop_hdr |= (info->top_field_first& 0x1) << 7;
                vop_hdr	|= (info->alternate_vertical_scan_flag & 0x1) << 6;
                vop_hdr	|= (decoder_p->vop_header.vop_coding_type != VOP_I ? 
                                info->vop_fcode_forward & 0x7 : 0) << 3;
                //vop_hdr	|= (decoder_p->vop_header.vop_coding_type == VOP_B ? 
                //                info->vop_fcode_backward & 0x7 : 0) << 0;
                vop_hdr	|= decoder_p->vop_header.fcode_backward & 0x7 << 0;
                writel(vop_hdr, ve_regs + VE_MPEG_VOP_HDR);

                decoder_p->vop_header.last_coding_type = decoder_p->vop_header.vop_coding_type;

                writel(decoder_p->vop_header.vop_quant, ve_regs + VE_MPEG_QP_INPUT);

                writel(mba_reg, ve_regs + VE_MPEG_MBA);

                //clean up everything
                writel(0xffffffff, ve_regs + VE_MPEG_STATUS);

                // set input offset in bits
                writel(bs.bitpos, ve_regs + VE_MPEG_VLD_OFFSET);

                // set input length in bits
                writel(((len*8 - bs.bitpos)+31) & ~0x1f, ve_regs + VE_MPEG_VLD_LEN);

                // input end
                uint32_t input_addr = ve_virt2phys(decoder->data);
                writel(input_addr + VBV_SIZE - 1, ve_regs + VE_MPEG_VLD_END);

                // set input buffer
                writel((input_addr & 0x0ffffff0) | (input_addr >> 28) | (0x7 << 28), ve_regs + VE_MPEG_VLD_ADDR);

                writel(0x0, ve_regs + VE_MPEG_MSMPEG4_HDR);
                writel(0x0, ve_regs + VE_MPEG_CTR_MB);

                if(decoder_p->vop_header.vop_coding_type == VOP_S)
                {
                    uint32_t mpeg_sdlx = decoder_p->vop_header.virtual_ref2[0][0]<<16 | 
                            (decoder_p->vop_header.virtual_ref2[1][0] & 0xffff);
                    writel(mpeg_sdlx, ve_regs + VE_MPEG_SDLX);
                    writel(mpeg_sdlx, ve_regs + VE_MPEG_SDCX);
    
                    int32_t mpeg_sdly =  decoder_p->vop_header.virtual_ref2[0][1]<<16 | 
                            (decoder_p->vop_header.virtual_ref2[1][1] & 0xffff);
                    writel(mpeg_sdly, ve_regs + VE_MPEG_SDLY);
                    writel(mpeg_sdly, ve_regs + VE_MPEG_SDCY);
                
                    uint32_t mpeg_spriteshift = (decoder_p->vop_header.sprite_shift[0] & 0xff);
                    mpeg_spriteshift |= ((decoder_p->vop_header.sprite_shift[1] & 0xff) << 8);
                    writel(mpeg_spriteshift, ve_regs + VE_MPEG_SPRITESHIFT);

                    uint32_t mpeg_sol = decoder_p->vop_header.sprite_ref[0][0] << 16;
                    mpeg_sol |= (decoder_p->vop_header.sprite_ref[0][1] & 0xffff);
                    writel(mpeg_sol, ve_regs + VE_MPEG_SOL);

                    writel(decoder_p->vop_header.socx, ve_regs + VE_MPEG_SOCX);
                    writel(decoder_p->vop_header.socy, ve_regs + VE_MPEG_SOCY);

                    int mv5 = decoder_p->vop_header.mv5_upper << 16 | decoder_p->vop_header.mv5_lower;
                    int mv6 = decoder_p->vop_header.mv6_upper << 16 | decoder_p->vop_header.mv6_lower;
                    writel(mv5, ve_regs + VE_MPEG_MV5);
                    writel(mv6, ve_regs + VE_MPEG_MV6);
                }
                // trigger
                bitstream bs_saved = bs;
                int marker_length = mpeg4_calcResyncMarkerLength(decoder_p);
                mp4_private_t _priv = *decoder_p;
                if(find_resynccode(&bs, marker_length))
                        mpeg4_decode_packet_header(&bs,
                                        info,
                                        decoder,
                                        &_priv);
                decoder_p->pkt_hdr.curr_mb_num = _priv.pkt_hdr.mb_num;
                bs = bs_saved;

                int num_mba = decoder_p->pkt_hdr.curr_mb_num; 
		if(num_mba == 0)
			num_mba = height * width;
                int vbv_size = num_mba - last_mba; // * width;
		if(vbv_size == 0)
		{
			num_mba = height * width;
			vbv_size = num_mba - last_mba;
		}
		last_mba = num_mba;
                uint32_t mpeg_trigger = 0;
                uint32_t error_disable = 1;
                mpeg_trigger |= vbv_size << 8;
                mpeg_trigger |= 0xd;
                mpeg_trigger |= (error_disable << 31);
                mpeg_trigger |= (0x4000000);
                writel(mpeg_trigger, ve_regs + VE_MPEG_TRIGGER);

                // wait for interrupt
#ifdef TIMEMEAS
            uint64_t tv, tv2;
                tv = get_time();
#endif
                ve_wait(1);
#ifdef TIMEMEAS                
                tv2 = get_time();
                if (tv2-tv > 10000000) {
                    printf("ve_wait, longer than 10ms:%lld, pics=%ld, longs=%ld\n", tv2-tv, num_pics, ++num_longs);
                }
#endif
                // clean interrupt flag
                writel(0x0000c00f, ve_regs + VE_MPEG_STATUS);
                int error = readl(ve_regs + VE_MPEG_ERROR);
                if(error)
                    printf("got error=%d while decoding frame=%ld\n", error, num_pics);
                writel(0x0, ve_regs + VE_MPEG_ERROR);

                ++num_pics;

                int veCurPos = readl(ve_regs + VE_MPEG_VLD_OFFSET);
                int byteCurPos = (veCurPos+7) / 8;

                more_mbs = 0;
                if (veCurPos < (len*8) && !info->resync_marker_disable)
                {
                    bs.bitpos = veCurPos / 8 * 8;
                    if(bytealign(&bs) == 0)
                    {
                        if(find_resynccode(&bs, marker_length)) {
                            mpeg4_decode_packet_header(&bs, 
                                                 info,
                                                 decoder, 
                                                 decoder_p);
                            int result;
                            bitstream bs1 = bs;
                            decoder_p->vop_header.quantizer = decoder_p->vop_header.vop_quant;

/*
                            int mba=0;
                            int num_macroblock_in_gob = decoder_p->pkt_hdr.mb_width * 
                                            get_gob_height(decoder_p->mpeg4VolHdr.video_object_layer_height);
                            do {
                                result = macroblock(&bs, decoder_p);
                                mba++;
                            } while(bits_left(&bs) && (nextbits_bytealigned(&bs, 23) != 0) &&
                                    mba <= num_macroblock_in_gob);
                            bs = bs1;
*/
                            more_mbs=1;
                            //last_mba = mba_reg;
                            mba_reg = decoder_p->pkt_hdr.mb_y | (decoder_p->pkt_hdr.mb_x << 8);
                        }
                    }
                }
                writel(readl(ve_regs + VE_MPEG_CTRL) | 0x7C, ve_regs + VE_MPEG_CTRL);            
            }
            // stop MPEG engine
            writel((readl(ve_regs + VE_CTRL) & ~0xf) | 0x7, ve_regs + VE_CTRL);
            ve_put();
    	}
	return VDP_STATUS_OK;
}
VdpStatus mpeg4_setVideoControlData(decoder_ctx_t *decoder, VdpDecoderControlDataId id, VdpDecoderControlData *data)
{
	mp4_private_t *decoder_p = (mp4_private_t *)decoder->private;
    VdpStatus status = VDP_STATUS_ERROR;
    
    if(id == VDP_MPEG4_VOL_HEADER)
    { 
        if(data->mpeg4VolHdr.struct_version != VDP_MPEG4_STRUCT_VERSION)
            return VDP_STATUS_INVALID_STRUCT_VERSION;

        decoder_p->mpeg4VolHdr = data->mpeg4VolHdr;
        decoder_p->mpeg4VolHdrSet = 1;
        
        status = VDP_STATUS_OK;
    }
    return status;
}

VdpStatus new_decoder_mpeg4(decoder_ctx_t *decoder)
{
	mp4_private_t *decoder_p = calloc(1, sizeof(mp4_private_t));
    memset(decoder_p, 0, sizeof(*decoder_p));
	if (!decoder_p)
		goto err_priv;

	int width = ((decoder->width + 15) / 16);
	int height = ((decoder->height + 15) / 16);

	decoder_p->mbh_buffer = ve_malloc(height * 2048);
	if (!decoder_p->mbh_buffer)
		goto err_mbh;

	decoder_p->dcac_buffer = ve_malloc(width * height * 2);
	if (!decoder_p->dcac_buffer)
		goto err_dcac;

	decoder_p->ncf_buffer = ve_malloc(4 * 1024);
	if (!decoder_p->ncf_buffer)
		goto err_ncf;

	decoder->decode = mpeg4_decode;
	decoder->private = decoder_p;
	decoder->private_free = mp4_private_free;
    decoder->setVideoControlData = mpeg4_setVideoControlData;

    save_tables(&decoder_p->tables);

	return VDP_STATUS_OK;

err_ncf:
	ve_free(decoder_p->dcac_buffer);
err_dcac:
	ve_free(decoder_p->mbh_buffer);
err_mbh:
	free(decoder_p);
err_priv:
	return VDP_STATUS_RESOURCES;
}
