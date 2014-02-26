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
/// mp4_block.c //

#include <math.h>
#include <stdlib.h>
#include <assert.h>

#include "mp4_vars.h"

#include "bitstream.h"
//#include "clearblock.h"
//#include "mp4_iquant.h"
//#include "mp4_predict.h"
#include "mp4_vld.h"
//#include "debug.h"
//#include "mp4_block.h"
#include "mpeg4.h"
/**
 *
**/

static int getDCsizeLum(bitstream *bs);
static int getDCsizeChr(bitstream *bs);
static int getDCdiff(bitstream *bs, int);
static void setDCscaler(mp4_private_t *priv, int block_num);
static int getACdir();

event_t vld_event(bitstream *bs, mp4_private_t *priv, int intraFlag);
event_t vld_intra_dct(bitstream *bs, mp4_private_t *priv);
event_t vld_inter_dct(bitstream *bs, mp4_private_t *priv);

/***/

// Purpose: texture decoding of block_num
int block(bitstream *bs, mp4_private_t *priv, int block_num, int coded)
{
	int i;
	int dct_dc_size, dct_dc_diff;
    vop_header_t *h = &priv->vop_header;
    VdpDecoderMpeg4VolHeader *vol = &priv->mpeg4VolHdr;
    
	int intraFlag = ((h->derived_mb_type == INTRA) || 
		(h->derived_mb_type == INTRA_Q)) ? 1 : 0;
	event_t event;

	//clearblock(ld->block); // clearblock

	if (intraFlag)
	{
		setDCscaler(priv, block_num); // calculate DC scaler

		if (block_num < 4) {
			dct_dc_size = getDCsizeLum(bs);
			if (dct_dc_size != 0) 
				dct_dc_diff = getDCdiff(bs, dct_dc_size);
			else 
				dct_dc_diff = 0;
			if (dct_dc_size > 8)
				get_bits(bs, 1); // marker bit
		}
		else {
			dct_dc_size = getDCsizeChr(bs);
			if (dct_dc_size != 0)
				dct_dc_diff = getDCdiff(bs, dct_dc_size);
			else 
				dct_dc_diff = 0;
			if (dct_dc_size > 8)
				get_bits(bs, 1); // marker bit
		}

		//ld->block[0] = (short) dct_dc_diff;
//		_Print("DC diff: %d\n", dct_dc_diff);
	}
	if (intraFlag)
	{
		// dc reconstruction, prediction direction
		//dc_recon(block_num, &ld->block[0]);
	}

	if (coded) 
	{
#if 0    
		unsigned int * zigzag; // zigzag scan dir

		if ((intraFlag) && (h->ac_pred_flag == 1)) {

			if (priv->coeff_pred.predict_dir == TOP)
				zigzag = priv->tables.alternate_horizontal_scan;
			else
				zigzag = priv->tables.alternate_vertical_scan;
		}
		else {
			zigzag = priv->tables.zig_zag_scan;
		}
#endif

		i = intraFlag ? 1 : 0;
		do // event vld
		{
			event = vld_event(bs, priv, intraFlag);
/***
			if (event.run == -1)
			{
				printf("Error: invalid vld code\n");
				exit(201);
			}
***/			
			i+= event.run;
			//ld->block[zigzag[i]] = (short) event.level;

//			_Print("Vld Event: Run Level Last %d %d %d\n", event.run, event.level, event.last);

			i++;
		} while (! event.last);
	}

	if (intraFlag)
	{
		// ac reconstruction
		// ac_rescaling(...)
		//ac_recon(block_num, &ld->block[0]);
	}

#ifdef _DEBUG_B_ACDC
	if (intraFlag)
	{
		int i;
		_Print("After AcDcRecon:\n");
		_Print("   x ");
		for (i = 1; i < 64; i++) {
			if ((i != 0) && ((i % 8) == 0))
				_Print("\n");
			_Print("%4d ", ld->block[i]);
		}
		_Print("\n");
	}
#endif // _DEBUG_ACDC

	if (vol->quant_type == 0)
	{
		// inverse quantization
		//iquant(ld->block, intraFlag);
	}
	else 
	{
		printf("Error: MPEG-2 inverse quantization NOT implemented\n");
		exit(110);
	}

#ifdef _DEBUG_B_QUANT
	{
		int i;
		_Print("After IQuant:\n");
		_Print("   x ");
		for (i = 1; i < 64; i++) {
			if ((i != 0) && ((i % 8) == 0))
				_Print("\n");
			_Print("%4d ", ld->block[i]);
		}
		_Print("\n");
	}
#endif // _DEBUG_B_QUANT

	// inverse dct
	//idct(ld->block);

	return 1;
}

/***/

int blockIntra(bitstream *bs, mp4_private_t *priv, int block_num, int coded)
{
	int i;
	int dct_dc_size, dct_dc_diff;
	event_t event;
    vop_header_t *h = &priv->vop_header;

	//clearblock(ld->block); // clearblock

	// dc coeff
	setDCscaler(priv, block_num); // calculate DC scaler

	if (block_num < 4) {
		dct_dc_size = getDCsizeLum(bs);
		if (dct_dc_size != 0) 
			dct_dc_diff = getDCdiff(bs, dct_dc_size);
		else 
			dct_dc_diff = 0;
		if (dct_dc_size > 8)
			get_bits(bs, 1); // marker bit
	}
	else {
		dct_dc_size = getDCsizeChr(bs);
		if (dct_dc_size != 0)
			dct_dc_diff = getDCdiff(bs, dct_dc_size);
		else 
			dct_dc_diff = 0;
		if (dct_dc_size > 8)
			get_bits(bs, 1); // marker bit
	}

	//ld->block[0] = (short) dct_dc_diff;

	// dc reconstruction, prediction direction
	//dc_recon(block_num, &ld->block[0]);

	if (coded) 
	{
#if 0        
		unsigned int * zigzag; // zigzag scan dir

		if (h->ac_pred_flag == 1) {

			if (priv->coeff_pred.predict_dir == TOP)
				zigzag = priv->tables.alternate_horizontal_scan;
			else
				zigzag = priv->tables.alternate_vertical_scan;
		}
		else {
			zigzag = priv->tables.zig_zag_scan;
		}
#endif

		i = 1;
		do // event vld
		{
			event = vld_intra_dct(bs, priv);
/***
			if (event.run == -1)
			{
				printf("Error: invalid vld code\n");
				exit(201);
			}
***/			
			i+= event.run;
			//ld->block[zigzag[i]] = (short) event.level;

//			_Print("Vld Event: Run Level Last %d %d %d\n", event.run, event.level, event.last);

			i++;
		} while (! event.last);
	}

#if 0
	// ac reconstruction
	h->intrablock_rescaled = ac_rescaling(block_num, &ld->block[0]);
	if (! h->intrablock_rescaled)
	{
		ac_recon(block_num, &ld->block[0]);
	}
	ac_store(block_num, &ld->block[0]);

	if (h->quant_type == 0)
	{
		iquant(ld->block, 1);
	}
	else 
	{
		iquant_typefirst(ld->block);
	}

	// inverse dct
	idct(ld->block);
#endif

	return 1;
}

/***/

int blockInter(bitstream *bs, mp4_private_t *priv, int block_num, int coded)
{
	event_t event;
	unsigned int * zigzag = priv->tables.zig_zag_scan; // zigzag scan dir
	int i;
    vop_header_t *h = &priv->vop_header;
    VdpDecoderMpeg4VolHeader *vol = &priv->mpeg4VolHdr;

	//clearblock(ld->block); // clearblock

	// inverse quant type
	if (vol->quant_type == 0) 
	{
		int q_scale = h->vop_quant;
		int q_2scale = q_scale << 1;
		int q_add = (q_scale & 1) ? q_scale : (q_scale - 1);
			
		i = 0;
		do // event vld
		{
			event = vld_inter_dct(bs, priv);

			/***
			if (event.run == -1)
			{
			printf("Error: invalid vld code\n");
			exit(201);
			}
			***/			
			i+= event.run;
#if 0
			if (event.level > 0) {
				ld->block[zigzag[i]] = (q_2scale * event.level) + q_add;
			}
			else {
				ld->block[zigzag[i]] = (q_2scale * event.level) - q_add;
			}
#endif			
			// _Print("Vld Event: Run Level Last %d %d %d\n", event.run, event.level, event.last);
			
			i++;
		} while (! event.last);
	}
	else 
	{
		int k, m = 0;
		i = 0;

		// event vld
		do 
		{
			event = vld_inter_dct(bs, priv);

			i+= event.run;
	
			k = (event.level > 0) ? 1 : -1;

			//assert(ld->block[zigzag[i]] < 2047);
			//assert(ld->block[zigzag[i]] > -2048);

#if 0            
			ld->block[zigzag[i]] = ((2 * event.level + k) * mp4_state->hdr.quantizer * 
				mp4_tables->nonintra_quant_matrix[zigzag[i]]) >> 4;

			assert(ld->block[zigzag[i]] < 2047);
			assert(ld->block[zigzag[i]] > -2048);

			m ^= ld->block[zigzag[i]];
#endif			
			// _Print("Vld Event: Run Level Last %d %d %d\n", event.run, event.level, event.last);
			
			i++;
		} while (! event.last);

		//if (!(m%2)) ld->block[63] ^= 1;
	}

	// inverse dct
	//idct(ld->block);
		
	return 1;
}

/***/

static int getDCsizeLum(bitstream *bs)
{
	int code;

	// [Ag][note] bad code

	if (show_bits(bs, 11) == 1) {
		flush_bits(bs, 11);
		return 12;
	}
    if (show_bits(bs, 10) == 1) {
        flush_bits(bs, 10);
        return 11;
    }
    if (show_bits(bs, 9) == 1) {
        flush_bits(bs, 9);
        return 10;
	}
	if (show_bits(bs, 8) == 1) {
		flush_bits(bs, 8);
		return 9;
	}
	if (show_bits(bs, 7) == 1) {
		flush_bits(bs, 7);
		return 8;
	}
	if (show_bits(bs, 6) == 1) {
		flush_bits(bs, 6);
		return 7;
	}  
	if (show_bits(bs, 5) == 1) {
		flush_bits(bs, 5);
		return 6;
	}
	if (show_bits(bs, 4) == 1) {
		flush_bits(bs, 4);
		return 5;
	}

	code = show_bits(bs, 3);

	if (code == 1) {
		flush_bits(bs, 3);
		return 4;
	} else if (code == 2) {
		flush_bits(bs, 3);
		return 3;
	} else if (code == 3) {
		flush_bits(bs, 3);
		return 0;
	}

  code = show_bits(bs, 2);

  if (code == 2) {
		flush_bits(bs, 2);
		return 2;
	} else if (code == 3) {
		flush_bits(bs, 2);
		return 1;
	}     

	return 0;
}

static int getDCsizeChr(bitstream *bs)
{
	// [Ag][note] bad code

	if (show_bits(bs, 12) == 1) {
		flush_bits(bs, 12);
		return 12;
	}
	if (show_bits(bs, 11) == 1) {
		flush_bits(bs, 11);
		return 11;
	}
	if (show_bits(bs, 10) == 1) {
		flush_bits(bs, 10);
		return 10;
	}
	if (show_bits(bs, 9) == 1) {
		flush_bits(bs, 9);
		return 9;
	}
	if (show_bits(bs, 8) == 1) {
		flush_bits(bs, 8);
		return 8;
	}
	if (show_bits(bs, 7) == 1) {
		flush_bits(bs, 7);
		return 7;
	}
	if (show_bits(bs, 6) == 1) {
		flush_bits(bs, 6);
		return 6;
	}
	if (show_bits(bs, 5) == 1) {
		flush_bits(bs, 5);
		return 5;
	}
	if (show_bits(bs, 4) == 1) {
		flush_bits(bs, 4);
		return 4;
	} 
	if (show_bits(bs, 3) == 1) {
		flush_bits(bs, 3);
		return 3;
	} 

	return (3 - get_bits(bs, 2));
}

/***/

static int getDCdiff(bitstream *bs, int dct_dc_size)
{
	int code = get_bits(bs, dct_dc_size);
	int msb = code >> (dct_dc_size - 1);

	if (msb == 0) {
		return (-1 * (code^((int) pow(2.0,(double) dct_dc_size) - 1)));
	}
  else { 
		return code;
	}
}

/***/

static void setDCscaler(mp4_private_t *priv, int block_num) 
{
    vop_header_t *h = &priv->vop_header;
	int type = (block_num < 4) ? 0 : 1;
	int quant = h->vop_quant;

	if (type == 0) {
		if (quant > 0 && quant < 5) 
			priv->dc_scaler = 8;
		else if (quant > 4 && quant < 9) 
			priv->dc_scaler = (2 * quant);
		else if (quant > 8 && quant < 25) 
			priv->dc_scaler = (quant + 8);
		else 
			priv->dc_scaler = (2 * quant - 16);
	}
  else {
		if (quant > 0 && quant < 5) 
			priv->dc_scaler = 8;
		else if (quant > 4 && quant < 25) 
			priv->dc_scaler = ((quant + 13) / 2);
		else 
			priv->dc_scaler = (quant - 6);
	}
}

/***/

