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
    video_packet_header_t* vp = &priv->pkt_hdr;

//	_Print("-Macroblock %d\n", mp4_state->hdr.mba);
//	_Break(93, 426);

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

	return 1;
}

static int decode_vop_header(bitstream *bs, VdpPictureInfoMPEG4Part2 const *info, mp4_private_t *priv)
{
    int dummy;
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

	// rounding_type
	if (h->vop_coding_type == VOP_P)
		get_bits(bs, 1);

	h->intra_dc_vlc_thr = get_bits(bs, 3);

    if(vol->video_object_layer_shape != BIN_ONLY_SHAPE) {
        // assume default size of 5 bits
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
            //video_packet_header()
            //motion_shape_texture()
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
        }
    }
    else {
        //combined_motion_shape_texture() 
    }
	return 1;
}
#if 1
static int mpeg4_decode_packet_header(bitstream *gb, VdpPictureInfoMPEG4Part2 const *info, decoder_ctx_t *decoder, mp4_private_t *priv)
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

            if (vol->video_object_type_indication == AV_PICTURE_TYPE_S &&
                vol->sprite_enable == GMC_SPRITE) {
#if 0
                if (mpeg4_decode_sprite_trajectory(ctx, &s->gb) < 0)
                    return AVERROR_INVALIDDATA;
#endif
                printf("untested\n");
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

#if 0
static int decode_vol_header(bitstream *gb, vol_header_t *s)
{
    int vo_ver_id;

    /* vol header */
    get_bits(gb, 1);                   /* random access */
    s->vo_type = get_bits(gb, 8);
    if (get_bits(gb,1) != 0) {         /* is_ol_id */
        vo_ver_id = get_bits(gb, 4);   /* vo_ver_id */
        get_bits(gb, 3);               /* vo_priority */
    } else {
        vo_ver_id = 1;
    }
    s->aspect_ratio_info = get_bits(gb, 4);
#if 0
    if (s->aspect_ratio_info == FF_ASPECT_EXTENDED) {
        printf("extended aspect not implemented\n");
    } else {
        printf("aspect not implemented\n");
    }
#endif

    if ((s->vol_control_parameters = get_bits(gb,1))) { /* vol control parameter */
        int chroma_format = get_bits(gb, 2);
#if 0
        if (chroma_format != CHROMA_420)
            av_log(s->avctx, AV_LOG_ERROR, "illegal chroma format\n");
#endif

        s->low_delay = get_bits(gb,1);
        if (get_bits(gb,1)) {    /* vbv parameters */
            get_bits(gb, 15);   /* first_half_bitrate */
            get_bits(gb,1);     /* marker */
            get_bits(gb, 15);   /* latter_half_bitrate */
            get_bits(gb,1);     /* marker */
            get_bits(gb, 15);   /* first_half_vbv_buffer_size */
            get_bits(gb,1);     /* marker */
            get_bits(gb, 3);    /* latter_half_vbv_buffer_size */
            get_bits(gb, 11);   /* first_half_vbv_occupancy */
            get_bits(gb,1);     /* marker */
            get_bits(gb, 15);   /* latter_half_vbv_occupancy */
            get_bits(gb,1);     /* marker */
        }
    } else {
        /* is setting low delay flag only once the smartest thing to do?
         * low delay detection won't be overriden. */
        if (s->picture_number == 0)
            s->low_delay = 0;
    }

    s->shape = get_bits(gb, 2); /* vol shape */
    if (s->shape != RECT_SHAPE)
        printf("only rectangular vol supported\n");
    if (s->shape == GRAY_SHAPE && vo_ver_id != 1) {
        printf("Gray shape not supported\n");
        get_bits(gb, 4);  /* video_object_layer_shape_extension */
    }

    get_bits(gb, 1);

    s->time_base_den = get_bits(gb, 16);
#if 0
    if (!s->time_base.den) {
        av_log(s->avctx, AV_LOG_ERROR, "time_base.den==0\n");
        s->avctx->time_base.num = 0;
        return -1;
    }
#endif

    s->time_increment_bits = (31 - __builtin_clz((s->time_base_den - 1)|1)) + 1;
    if (s->time_increment_bits < 1)
        s->time_increment_bits = 1;

    get_bits(gb, 1);

    if (get_bits(gb,1) != 0)     /* fixed_vop_rate  */
        s->time_base_num = get_bits(gb, s->time_increment_bits);
    else
        s->time_base_num = 1;

    //ctx->t_frame = 0;

    if (s->shape != BIN_ONLY_SHAPE) {
        if (s->shape == RECT_SHAPE) {
            get_bits(gb,1);   /* marker */
            s->width = get_bits(gb, 13);
            get_bits(gb,1);   /* marker */
            s->height = get_bits(gb, 13);
            get_bits(gb,1);   /* marker */
        }

        get_bits(gb,1) ^ 1;
        get_bits(gb,1);
        if (vo_ver_id == 1)
            s->vol_sprite_usage = get_bits(gb,1);    /* vol_sprite_usage */
        else
            s->vol_sprite_usage = get_bits(gb, 2);  /* vol_sprite_usage */

        if (s->vol_sprite_usage == STATIC_SPRITE ||
            s->vol_sprite_usage == GMC_SPRITE) {
            if (s->vol_sprite_usage == STATIC_SPRITE) {
                get_bits(gb, 13); // sprite_width
                get_bits(gb,1); /* marker */
                get_bits(gb, 13); // sprite_height
                get_bits(gb,1); /* marker */
                get_bits(gb, 13); // sprite_left
                get_bits(gb,1); /* marker */
                get_bits(gb, 13); // sprite_top
                get_bits(gb,1); /* marker */
            }
            s->num_sprite_warping_points = get_bits(gb, 6);
            s->sprite_warping_accuracy  = get_bits(gb, 2);
            s->sprite_brightness_change = get_bits(gb,1);
            if (s->vol_sprite_usage == STATIC_SPRITE)
                get_bits(gb,1); // low_latency_sprite
        }
        // FIXME sadct disable bit if verid!=1 && shape not rect

        if (get_bits(gb,1) == 1) {                   /* not_8_bit */
            s->quant_precision = get_bits(gb, 4);   /* quant_precision */
            if (get_bits(gb, 4) != 8)               /* bits_per_pixel */
                printf("N-bit not supported\n");
            if (s->quant_precision != 5)
                printf("quant precision %d\n", s->quant_precision);
        } else {
            s->quant_precision = 5;
        }

        // FIXME a bunch of grayscale shape things

        if ((s->mpeg_quant = get_bits(gb,1))) { /* vol_quant_type */
            int i, v;

#if 0
            /* load default matrixes */
            for (i = 0; i < 64; i++) {
                int j = s->dsp.idct_permutation[i];
                v = ff_mpeg4_default_intra_matrix[i];
                s->intra_matrix[j]        = v;
                s->chroma_intra_matrix[j] = v;

                v = ff_mpeg4_default_non_intra_matrix[i];
                s->inter_matrix[j]        = v;
                s->chroma_inter_matrix[j] = v;
            }
#endif

            /* load custom intra matrix */
            if (get_bits(gb,1)) {
                int last = 0;
                for (i = 0; i < 64; i++) {
                    int j;
                    v = get_bits(gb, 8);
                    if (v == 0)
                        break;

#if 0
                    last = v;
                    j = s->dsp.idct_permutation[ff_zigzag_direct[i]];
                    s->intra_matrix[j]        = last;
                    s->chroma_intra_matrix[j] = last;
#endif
                }

#if 0
                /* replicate last value */
                for (; i < 64; i++) {
                    int j = s->dsp.idct_permutation[ff_zigzag_direct[i]];
                    s->intra_matrix[j]        = last;
                    s->chroma_intra_matrix[j] = last;
                }
#endif
            }

            /* load custom non intra matrix */
            if (get_bits(gb,1)) {
                int last = 0;
                for (i = 0; i < 64; i++) {
                    int j;
                    v = get_bits(gb, 8);
                    if (v == 0)
                        break;

#if 0
                    last = v;
                    j = s->dsp.idct_permutation[ff_zigzag_direct[i]];
                    s->inter_matrix[j]        = v;
                    s->chroma_inter_matrix[j] = v;
#endif
                }

#if 0
                /* replicate last value */
                for (; i < 64; i++) {
                    int j = s->dsp.idct_permutation[ff_zigzag_direct[i]];
                    s->inter_matrix[j]        = last;
                    s->chroma_inter_matrix[j] = last;
                }
#endif
            }

            // FIXME a bunch of grayscale shape things
        }

        if (vo_ver_id != 1)
            s->quarter_sample = get_bits(gb,1);
        else
            s->quarter_sample = 0;

        if (!get_bits(gb,1)) {
            int pos               = gb->bitpos;
            int estimation_method = get_bits(gb, 2);
            if (estimation_method < 2) {
                if (!get_bits(gb,1)) {
                    s->cplx_estimation_trash_i += 8 * get_bits(gb,1);  /* opaque */
                    s->cplx_estimation_trash_i += 8 * get_bits(gb,1);  /* transparent */
                    s->cplx_estimation_trash_i += 8 * get_bits(gb,1);  /* intra_cae */
                    s->cplx_estimation_trash_i += 8 * get_bits(gb,1);  /* inter_cae */
                    s->cplx_estimation_trash_i += 8 * get_bits(gb,1);  /* no_update */
                    s->cplx_estimation_trash_i += 8 * get_bits(gb,1);  /* upampling */
                }
                if (!get_bits(gb,1)) {
                    s->cplx_estimation_trash_i += 8 * get_bits(gb,1);  /* intra_blocks */
                    s->cplx_estimation_trash_p += 8 * get_bits(gb,1);  /* inter_blocks */
                    s->cplx_estimation_trash_p += 8 * get_bits(gb,1);  /* inter4v_blocks */
                    s->cplx_estimation_trash_i += 8 * get_bits(gb,1);  /* not coded blocks */
                }
                if (!get_bits(gb, 1)) {
                    //skip_bits_long(gb, pos - get_bits_count(gb));
                    gb->bitpos += pos - gb->bitpos;
                    goto no_cplx_est;
                }
                if (!get_bits(gb,1)) {
                    s->cplx_estimation_trash_i += 8 * get_bits(gb,1);  /* dct_coeffs */
                    s->cplx_estimation_trash_i += 8 * get_bits(gb,1);  /* dct_lines */
                    s->cplx_estimation_trash_i += 8 * get_bits(gb,1);  /* vlc_syms */
                    s->cplx_estimation_trash_i += 4 * get_bits(gb,1);  /* vlc_bits */
                }
                if (!get_bits(gb,1)) {
                    s->cplx_estimation_trash_p += 8 * get_bits(gb,1);  /* apm */
                    s->cplx_estimation_trash_p += 8 * get_bits(gb,1);  /* npm */
                    s->cplx_estimation_trash_b += 8 * get_bits(gb,1);  /* interpolate_mc_q */
                    s->cplx_estimation_trash_p += 8 * get_bits(gb,1);  /* forwback_mc_q */
                    s->cplx_estimation_trash_p += 8 * get_bits(gb,1);  /* halfpel2 */
                    s->cplx_estimation_trash_p += 8 * get_bits(gb,1);  /* halfpel4 */
                }
                if (!get_bits(gb, 1)) {
                    //skip_bits_long(gb, pos - get_bits_count(gb));
                    gb->bitpos += pos-gb->bitpos;
                    goto no_cplx_est;
                }
                if (estimation_method == 1) {
                    s->cplx_estimation_trash_i += 8 * get_bits(gb,1);  /* sadct */
                    s->cplx_estimation_trash_p += 8 * get_bits(gb,1);  /* qpel */
                }
            } else
                printf("Invalid Complexity estimation method %d\n",
                       estimation_method);
        } else {

no_cplx_est:
            s->cplx_estimation_trash_i =
            s->cplx_estimation_trash_p =
            s->cplx_estimation_trash_b = 0;
        }

        s->resync_marker = !get_bits(gb,1); /* resync_marker_disabled */

        s->data_partitioning = get_bits(gb,1);
        if (s->data_partitioning)
            s->rvlc = get_bits(gb,1);

        if (vo_ver_id != 1) {
            s->new_pred = get_bits(gb,1);
            if (s->new_pred) {
                printf("new pred not supported\n");
                get_bits(gb, 2); /* requested upstream message type */
                get_bits(gb,1);   /* newpred segment type */
            }
            if (get_bits(gb,1)) // reduced_res_vop
                printf("reduced resolution VOP not supported\n");
        } else {
            s->new_pred = 0;
        }

        s->scalability = get_bits(gb,1);

        if (s->scalability) {
            bitstream bak = *gb;
            int h_sampling_factor_n;
            int h_sampling_factor_m;
            int v_sampling_factor_n;
            int v_sampling_factor_m;

            get_bits(gb, 1);    // hierarchy_type
            get_bits(gb, 4);  /* ref_layer_id */
            get_bits(gb, 1);    /* ref_layer_sampling_dir */
            h_sampling_factor_n = get_bits(gb, 5);
            h_sampling_factor_m = get_bits(gb, 5);
            v_sampling_factor_n = get_bits(gb, 5);
            v_sampling_factor_m = get_bits(gb, 5);
            s->enhancement_type = get_bits(gb, 1);

            if (h_sampling_factor_n == 0 || h_sampling_factor_m == 0 ||
                v_sampling_factor_n == 0 || v_sampling_factor_m == 0) {
                /* illegal scalability header (VERY broken encoder),
                 * trying to workaround */
                s->scalability = 0;
                *gb            = bak;
            } else
                printf("scalability not supported\n");

            // bin shape stuff FIXME
        }
    }

}
#endif

static unsigned long num_pics=0;
static unsigned long num_longs=0;
int mpeg4_decode(decoder_ctx_t *decoder, VdpPictureInfoMPEG4Part2 const *_info, const int len, video_surface_ctx_t *output)
{
	VdpPictureInfoMPEG4Part2 const *info = (VdpPictureInfoMPEG4Part2 const *)_info;
	mp4_private_t *decoder_p = (mp4_private_t *)decoder->private;

    uint32_t    startcode;
    int        more_mbs = 1;
    uint32_t   mp4mbaAddr_reg = 0;

    if(!decoder_p->mpeg4VolHdrSet)
    {
        VDPAU_DBG("MPEG4 VOL Header must be set prior decoding of frames! Sorry");
		return VDP_STATUS_ERROR;
    }

	int i;
	void *ve_regs = ve_get_regs();
	bitstream bs = { .data = decoder->data, .length = len, .bitpos = 0 };
    
	while (find_startcode(&bs))
	{
        startcode = get_bits(&bs, 8);
#if 0
        if( startcode >= 0x20 && startcode <= 0x2f)
        {
            decode_vol_header(&bs, &decoder_p->vol_hdr);
            continue;
        }
		else 
#endif
        if ( startcode != 0xb6)
			continue;

        memset(&decoder_p->vop_header, 0, sizeof(decoder_p->vop_header));
        decoder_p->vop_header.fcode_forward = 1;
        decoder_p->vop_header.fcode_backward = 1;
		if (!decode_vop_header(&bs, info, decoder_p))
			continue;

            // activate MPEG engine
            writel((readl(ve_regs + VE_CTRL) & ~0xf) | 0x0, ve_regs + VE_CTRL);
            
#if 0
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
                writel(ve_virt2phys(forward->data), ve_regs + VE_MPEG_FWD_LUMA);
                writel(ve_virt2phys(forward->data + forward->plane_size), ve_regs + VE_MPEG_FWD_CHROMA);
            }
            if (info->backward_reference != VDP_INVALID_HANDLE)
            {
                video_surface_ctx_t *backward = handle_get(info->backward_reference);
                writel(ve_virt2phys(backward->data), ve_regs + VE_MPEG_BACK_LUMA);
                writel(ve_virt2phys(backward->data + backward->plane_size), ve_regs + VE_MPEG_BACK_CHROMA);
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
                writel((info->trb[1] << 16) | (info->trd[1] << 0), ve_regs + VE_MPEG_TRBTRD_FIELD);
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

            decoder_p->pkt_hdr.mb_xpos = 0;
            decoder_p->pkt_hdr.mb_ypos = 0;
            uint32_t mba_reg = 0x0;
            
            while(more_mbs == 1) {

                uint32_t vop_hdr = 0;
                vop_hdr |= (info->interlaced & 0x1) << 30;
                vop_hdr |= (decoder_p->vop_header.vop_coding_type == VOP_B ? 0x1 : 0x0) << 28;
                vop_hdr |= (info->quant_type & 0x1) << 24;
                vop_hdr |= (info->quarter_sample & 0x1) << 23;
                vop_hdr |= (info->resync_marker_disable & 0x1) << 22; //error_res_disable
                vop_hdr |= (decoder_p->vop_header.vop_coding_type & 0x3) << 18;
                vop_hdr |= (info->rounding_control &0x1) << 17;
                vop_hdr |= (decoder_p->vop_header.intra_dc_vlc_thr & 0x7) << 8;
                vop_hdr |= (info->top_field_first& 0x1) << 7;
                vop_hdr	|= (info->alternate_vertical_scan_flag & 0x1) << 6;
                vop_hdr	|= (decoder_p->vop_header.vop_coding_type != VOP_I ? 
                                info->vop_fcode_forward & 0x7 : 0) << 3;
                vop_hdr	|= (decoder_p->vop_header.vop_coding_type == VOP_B ? 
                                info->vop_fcode_backward & 0x7 : 0) << 0;
                writel(vop_hdr, ve_regs + VE_MPEG_VOP_HDR);
                
                writel(decoder_p->vop_header.vop_quant, ve_regs + VE_MPEG_QP_INPUT);
                
                //mba_reg = readl(ve_regs + VE_MPEG_MBA);
                //if(!info->resync_marker_disable)
                //    mba_reg &= 0xff;    
                //writel( readl(ve_regs + VE_MPEG_MBA) /* & 0xff */, ve_regs + VE_MPEG_MBA);
                writel(mba_reg, ve_regs + VE_MPEG_MBA);
                
                //clean up everything
                writel(0xffffffff, ve_regs + VE_MPEG_STATUS);
                     
                // set input offset in bits
                writel(bs.bitpos, ve_regs + VE_MPEG_VLD_OFFSET);

                // set input length in bits
                writel((len*8 - bs.bitpos), ve_regs + VE_MPEG_VLD_LEN);

                // input end
                uint32_t input_addr = ve_virt2phys(decoder->data);
                writel(input_addr + VBV_SIZE - 1, ve_regs + VE_MPEG_VLD_END);

                // set input buffer
                writel((input_addr & 0x0ffffff0) | (input_addr >> 28) | (0x7 << 28), ve_regs + VE_MPEG_VLD_ADDR);

                writel(0x0, ve_regs + VE_MPEG_MSMPEG4_HDR);
                writel(0x0, ve_regs + VE_MPEG_CTR_MB);
            
                // trigger
                int vbv_size = width * height;
                uint32_t mpeg_trigger = 0;
                mpeg_trigger |= vbv_size << 8;
                mpeg_trigger |= 0xd;
                mpeg_trigger |= (1 << 31);
                mpeg_trigger |= (0x4000000);
                writel(mpeg_trigger, ve_regs + VE_MPEG_TRIGGER);

                // wait for interrupt
#ifdef TIMEMEAS
                ++num_pics;
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
                int veCurPos = readl(ve_regs + VE_MPEG_VLD_OFFSET);
                int byteCurPos = (veCurPos+7) / 8;

                more_mbs = 0;
                if (veCurPos < (len*8) && !info->resync_marker_disable)
                {
                    bs.bitpos = veCurPos / 8 * 8;
                    if(bytealign(&bs) == 0)
                    {
                        int marker_length=0;
                        
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
                        if(find_resynccode(&bs, marker_length)) {
                            //get_bits(&bs, marker_length);
                            mpeg4_decode_packet_header(&bs, 
                                                 info,
                                                 decoder, 
                                                 decoder_p);
                            int result;
                            bitstream bs1 = bs;
                            decoder_p->vop_header.quantizer = decoder_p->vop_header.vop_quant;

                            int mba=0;
                            int num_macroblock_in_gob = decoder_p->pkt_hdr.mb_width * 
                                            get_gob_height(decoder_p->mpeg4VolHdr.video_object_layer_height);
                            do {
                                result = macroblock(&bs, decoder_p);
                                mba++;
                            } while(bits_left(&bs) && (nextbits_bytealigned(&bs, 23) != 0) &&
                                    mba <= num_macroblock_in_gob);
                            bs = bs1;
                            more_mbs=1;
                            mba_reg = decoder_p->pkt_hdr.mb_y | (decoder_p->pkt_hdr.mb_x << 8);
                        }
                    }
                }
                writel(readl(ve_regs + VE_MPEG_CTRL) | 0x7C, ve_regs + VE_MPEG_CTRL);            
            }
            // stop MPEG engine
            writel((readl(ve_regs + VE_CTRL) & ~0xf) | 0x7, ve_regs + VE_CTRL);
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
