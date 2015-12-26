/**************************************************************************
 *                                                                        *
 * This code has been developed by Andrea Graziani. This software is an   *
 * implementation of a part of one or more MPEG-4 Video tools as          *
 * specified in ISO/IEC 14496-2 standard.  Those intending to use this    *
 * software module in hardware or software products are advised that its  *
 * use may infringe existing patents or copyrights, and any such use      *
 * would be at such party's own risk.  The original developer of this     *
 * software module and his/her company, and subsequent editors and their  *
 * companies (including Project Mayo), will have no liability for use of  *
 * this software or modifications or derivatives thereof.                 *
 *                                                                        *
 * Project Mayo gives users of the Codec a license to this software       *
 * module or modifications thereof for use in hardware or software        *
 * products claiming conformance to the MPEG-4 Video Standard as          *
 * described in the Open DivX license.                                    *
 *                                                                        *
 * The complete Open DivX license can be found at                         *
 * http://www.projectmayo.com/opendivx/license.php                        *
 *                                                                        *
 **************************************************************************/
/**
*  Copyright (C) 2001 - Project Mayo
 *
 * Andrea Graziani (Ag)
 *
 * DivX Advanced Research Center <darc@projectmayo.com>
*
**/
#ifndef _MP4_VARS_H_
#define _MP4_VARS_H_

// mp4_vars.h //

#include "portab.h"
//#include "decore.h"

//#include "mp4_header.h"
#include "mp4_vld.h"
//#include "postprocess.h"
#include "mpeg4.h"

/**
 *	macros
**/

#define mmax(a, b)      ((a) > (b) ? (a) : (b))
#define mmin(a, b)      ((a) < (b) ? (a) : (b))
#define mnint(a)        ((a) < 0 ? (int)(a - 0.5) : (int)(a + 0.5))
#define sign(a)         ((a) < 0 ? -1 : 1)
#define abs(a)					((a)>0 ? (a) : -(a))
#define sign(a)					((a) < 0 ? -1 : 1)
#define mnint(a)				((a) < 0 ? (int)(a - 0.5) : (int)(a + 0.5))
#define _div_div(a, b)	(a>0) ? (a+(b>>1))/b : (a-(b>>1))/b

/**
 *	decoder struct
**/
#if 0
typedef struct 
{
	// bit input
	int infile;
	unsigned char rdbfr[2051];
	unsigned char *rdptr;
	unsigned char inbfr[16];
	int incnt;
	int bitcnt;
	int length;
	// block data
	short block[64];
} MP4_STREAM;

typedef struct _ac_dc
{
	int dc_store_lum[2*DEC_MBR+1][2*DEC_MBC+1];
	int ac_left_lum[2*DEC_MBR+1][2*DEC_MBC+1][7];
	int ac_top_lum[2*DEC_MBR+1][2*DEC_MBC+1][7];

	int dc_store_chr[2][DEC_MBR+1][DEC_MBC+1];
	int ac_left_chr[2][DEC_MBR+1][DEC_MBC+1][7];
	int ac_top_chr[2][DEC_MBR+1][DEC_MBC+1][7];

	int predict_dir;

} ac_dc;

typedef void (* pfun_convert_yuv)(unsigned char *puc_y, int stride_y,
	unsigned char *puc_u, unsigned char *puc_v, int stride_uv,
	unsigned char *bmp, int width_y, int height_y,
	unsigned int stride_out);

/***/

typedef struct _MP4_STATE_
{
	mp4_header hdr;

	int	modemap[DEC_MBR+1][DEC_MBC+2];
	int	quant_store[DEC_MBR+1][DEC_MBC+1]; // [Review]
	int	MV[2][6][DEC_MBR+1][DEC_MBC+2];

	ac_dc coeff_pred;

	short iclp_data[1024];       
	short *iclp;
	unsigned char clp_data[1024];
	unsigned char *clp;

	pfun_convert_yuv convert_yuv;
	int flag_invert;

	int	horizontal_size;
	int	vertical_size;
	int	mb_width;
	int	mb_height;
	int	juice_hor;
	int	juice_ver;
	int	coded_picture_width;
	int	coded_picture_height;
	int	chrom_width;
	int	chrom_height;
	
	int	juice_flag;
	int	post_flag;
	int pp_options;

#ifndef _DECORE
	char *infilename;
	char * outputname;
	int output_flag;
#endif
} 
MP4_STATE;
#endif 

/**
 *	globals
**/

extern unsigned char	*edged_ref[3],
						*edged_for[3],
						*frame_ref[3],
						*frame_for[3],
						*display_frame[3];

//extern MP4_STATE	 *mp4_state;
//extern MP4_TABLES	 *mp4_tables;
//extern MP4_STREAM	 *ld;


/** 
 *	prototypes of global functions
**/
void save_tables (MP4_TABLES * tables);
int blockInter(bitstream *bs, mp4_private_t *priv, int block_num, int coded);
int blockIntra(bitstream *bs, mp4_private_t *priv, int block_num, int coded);

#if 0
int decore_init (int hor_size, int ver_size, int output_format, int time_incr, DEC_BUFFERS buffers);
int decore_frame (unsigned char *stream, int length, unsigned char *bmp, unsigned int stride, int render_flag);
int decore_release ();
int decore_setoutput (int output_format);
void closedecoder ();
void initdecoder (DEC_BUFFERS buffers);

void idct (short *block);
void reconstruct (int bx, int by, int mode);
void get_mp4picture (unsigned char *bmp, unsigned int stride, int render_flag);
void PictureDisplay (unsigned char *bmp, unsigned int stride, int render_flag);
#endif

#endif
