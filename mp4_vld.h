/**************************************************************************
 *                                                                        *
 * This code has been developed by John Funnell. This software is an      *
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
 * John Funnell
 * Andrea Graziani
 *
 * DivX Advanced Research Center <darc@projectmayo.com>
*
**/
// mp4_vld.h //

#ifndef _MP4_VLD_H_
#define _MP4_VLD_H_

#define ESCAPE 7167

#include "bitstream.h"

/*** *** ***/
struct mp4_private_t;

typedef struct {
	int val, len;
} tab_type;

/***/

typedef struct {
	int last;
	int run;
	int level;
} event_t;

/*** *** ***/


/***/

/***/

extern tab_type tableB16_1[];
extern tab_type tableB16_2[];
extern tab_type tableB16_3[];

extern tab_type tableB17_1[];
extern tab_type tableB17_2[];
extern tab_type tableB17_3[];

/***/

typedef struct _MP4_TABLES_
{
	unsigned int zig_zag_scan[64];
	unsigned int alternate_vertical_scan[64];
	unsigned int alternate_horizontal_scan[64];
	unsigned int intra_quant_matrix[64];
	unsigned int nonintra_quant_matrix[64];

	unsigned int msk[33];

	int roundtab[16];
	int saiAcLeftIndex[8];
	int DQtab[4];

	tab_type MCBPCtabIntra[32];
	tab_type MCBPCtabInter[256];
	tab_type CBPYtab[48];

	tab_type MVtab0[14];
	tab_type MVtab1[96];
	tab_type MVtab2[124];

	tab_type tableB16_1[112];
	tab_type tableB16_2[96];
	tab_type tableB16_3[120];
	tab_type tableB17_1[112];
	tab_type tableB17_2[96];
	tab_type tableB17_3[120];
} MP4_TABLES;


int vldTableB19(int last, int run);
int vldTableB20(int last, int run);
int vldTableB21(int last, int level);
int vldTableB22(int last, int level);

/***/

/***/
#endif // _MP4_VLD_H_

