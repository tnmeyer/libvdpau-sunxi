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

extern uint64_t get_time();

//vol_sprite_usage / sprite_enable
#define STATIC_SPRITE 1
#define GMC_SPRITE 2

// shapes
#define RECT_SHAPE       0
#define BIN_SHAPE        1
#define BIN_ONLY_SHAPE   2
#define GRAY_SHAPE       3

enum AVPictureType {
    AV_PICTURE_TYPE_I = 1, ///< Intra
    AV_PICTURE_TYPE_P,     ///< Predicted
    AV_PICTURE_TYPE_B,     ///< Bi-dir predicted
    AV_PICTURE_TYPE_S,     ///< S(GMC)-VOP MPEG4
    AV_PICTURE_TYPE_SI,    ///< Switching Intra
    AV_PICTURE_TYPE_SP,    ///< Switching Predicted
    AV_PICTURE_TYPE_BI,    ///< BI type
};

typedef struct
{
	const uint8_t *data;
	unsigned int length;
	unsigned int bitpos;
} bitstream;


typedef struct
{
    uint32_t    vo_type;
    uint32_t    width;
    uint32_t    height;
    uint32_t    mb_num;
    int         aspect_ratio_info;
    int         low_delay;
    int         picture_number;
    int         shape;
    int         time_base_den;
    int         time_base_num;
    int         time_increment_bits;
    int         vol_sprite_usage;
    int         num_sprite_warping_points;
    int         sprite_warping_accuracy;
    int         sprite_brightness_change;
    int         quant_precision;
    int         mpeg_quant;
    int         quarter_sample;
    int         cplx_estimation_trash_i;
    int         cplx_estimation_trash_p;
    int         cplx_estimation_trash_b;
    int         resync_marker;
    int         data_partitioning;
    int         rvlc;
    int         new_pred;
    int         scalability;
    int         enhancement_type;
    int         vol_control_parameters;
} vol_header_t;

typedef struct {
    int         mb_x;
    int         mb_y;
} video_packet_header_t;

typedef struct
{
	void            *mbh_buffer;
	void            *dcac_buffer;
	void            *ncf_buffer;
    vol_header_t    vol_hdr;
    video_packet_header_t pkt_hdr;
} mp4_private_t;

#define VOP_I	0
#define VOP_P	1
#define VOP_B	2
#define VOP_S	3

typedef struct
{
	int vop_coding_type;
	int intra_dc_vlc_thr;
	int vop_quant;
    int fcode_forward;
    int fcode_backward;
} vop_header;

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

static uint32_t show_bits(bitstream *bs, int n)
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

static uint32_t get_bits(bitstream *bs, int n)
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

static int bytealign(bitstream *bs)
{
    bs->bitpos = ((bs->bitpos+7) >> 3) << 3;
    if(bs->bitpos > bs->length * 8)
    {
        bs->bitpos = bs->length * 8;
        return 1;
    }
    return 0;
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

static int decode_vop_header(bitstream *bs, VdpPictureInfoMPEG4Part2 const *info, vop_header *h)
{
    int dummy;
    
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

	// assume default size of 5 bits
	h->vop_quant = get_bits(bs, 5);

	// vop_fcode_forward
	if (h->vop_coding_type != VOP_I)
		h->fcode_forward=get_bits(bs, 3);

	// vop_fcode_backward
	if (h->vop_coding_type == VOP_B)
		h->fcode_backward=get_bits(bs, 3);


	return 1;
}
#if 1
static int decode_packet_header(bitstream *gb, VdpPictureInfoMPEG4Part2 const *info, video_packet_header_t *h, vol_header_t *vol)
{
    int mb_width = (vol->width+15)/16;
    int mb_height = (vol->height+15)/16;
    int mb_num_calc = mb_width * mb_height;
    
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

    if (vol->shape != RECT_SHAPE) {
        header_extension = get_bits(gb,1);
        // FIXME more stuff here
    }

    mb_num = get_bits(gb, mb_num_bits);
    if (mb_num >= mb_num_calc) {
        printf("illegal mb_num in video packet (%d %d) \n", mb_num, mb_num_calc);
        return -1;
    }
    if (vol->vo_type == AV_PICTURE_TYPE_B) {
        int mb_x = 0, mb_y = 0;

#if 0
        while (s->next_picture.mbskip_table[s->mb_index2xy[mb_num]]) {
            if (!mb_x)
                ff_thread_await_progress(&s->next_picture_ptr->tf, mb_y++, 0);
            mb_num++;
            if (++mb_x == mb_width)
                mb_x = 0;
        }
        if (mb_num >= mb_num_calc)
            return -1;  // slice contains just skipped MBs (already decoded)
#endif

    }

    h->mb_x = mb_num % mb_width;
    h->mb_y = mb_num / mb_width;

    if (vol->shape != BIN_ONLY_SHAPE) {
        int qscale = get_bits(gb, vol->quant_precision);
#if 0
        if (qscale)
            vol->chroma_qscale = vol->qscale = qscale;
#endif
    }

    if (vol->shape == RECT_SHAPE)
        header_extension = get_bits(gb,1);

    if (header_extension) {
        int time_incr = 0;

        while (get_bits(gb,1) != 0)
            time_incr++;

        get_bits(gb, 1);
        get_bits(gb, vol->time_increment_bits);      /* time_increment */
        get_bits(gb, 1);

        get_bits(gb, 2); /* vop coding type */
        // FIXME not rect stuff here

        if (vol->shape != BIN_ONLY_SHAPE) {
            get_bits(gb, 3); /* intra dc vlc threshold */
            // FIXME don't just ignore everything

            if (vol->vo_type == AV_PICTURE_TYPE_S &&
                vol->vol_sprite_usage == GMC_SPRITE) {
#if 0
                if (mpeg4_decode_sprite_trajectory(ctx, &s->gb) < 0)
                    return AVERROR_INVALIDDATA;
#endif
                printf("untested\n");
            }

            // FIXME reduced res stuff here

            if (vol->vo_type != AV_PICTURE_TYPE_I) {
                int f_code = get_bits(gb, 3);       /* fcode_for */
                if (f_code == 0)
                    printf("Error, video packet header damaged (f_code=0)\n");
            }
            if (vol->vo_type == AV_PICTURE_TYPE_B) {
                int b_code = get_bits(gb, 3);
                if (b_code == 0)
                    printf("Error, video packet header damaged (b_code=0)\n");
            }
        }
    }
}
#endif

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

static unsigned long num_pics=0;
static unsigned long num_longs=0;
int mpeg4_decode(decoder_ctx_t *decoder, VdpPictureInfoMPEG4Part2 const *_info, const int len, video_surface_ctx_t *output)
{
	VdpPictureInfoMPEG4Part2 const *info = (VdpPictureInfoMPEG4Part2 const *)_info;
	mp4_private_t *decoder_p = (mp4_private_t *)decoder->private;

    uint32_t    startcode;
    int        more_mbs = 1;
    uint32_t   mp4mbaAddr_reg = 0;
    
#if 0    
	if (!info->resync_marker_disable)
	{
		VDPAU_DBG("We can't decode VOPs with resync markers yet! Sorry");
		return VDP_STATUS_ERROR;
	}
#endif 

	int i;
	void *ve_regs = ve_get_regs();
	bitstream bs = { .data = decoder->data, .length = len, .bitpos = 0 };
    
	while (find_startcode(&bs))
	{
        startcode = get_bits(&bs, 8);
        if( startcode >= 0x20 && startcode <= 0x2f)
        {
            decode_vol_header(&bs, &decoder_p->vol_hdr);
            continue;
        }
		else if ( startcode != 0xb6)
			continue;

		vop_header hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.fcode_forward = 1;
        hdr.fcode_backward = 1;
		if (!decode_vop_header(&bs, info, &hdr))
			continue;

            // activate MPEG engine
            writel((readl(ve_regs + VE_CTRL) & ~0xf) | 0x0, ve_regs + VE_CTRL);
            int iq_input = hdr.vop_quant;
            
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
            if (hdr.vop_coding_type == VOP_B)
            {
                writel((info->trb[0] << 16) | (info->trd[0] << 0), ve_regs + VE_MPEG_TRBTRD_FRAME);
                // unverified:
                writel((info->trb[1] << 16) | (info->trd[1] << 0), ve_regs + VE_MPEG_TRBTRD_FIELD);
            }
            // set size
            uint16_t width = (decoder_p->vol_hdr.width + 15) / 16;
            uint16_t height = (decoder_p->vol_hdr.height + 15) / 16;
            uint16_t width2 = width;
#if 1
            if((width2 & 0x1) == 0x0)
                width2 += 1;
#endif
            writel((width2 <<16) | (width << 8) | height, ve_regs + VE_MPEG_SIZE);
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
            ve_control |= (hdr.vop_coding_type == VOP_P ? 0x1 : 0x0) << 12;
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

            while(more_mbs == 1) {

                uint32_t vop_hdr = 0;
                vop_hdr |= (info->interlaced & 0x1) << 30;
                vop_hdr |= (hdr.vop_coding_type == VOP_B ? 0x1 : 0x0) << 28;
                vop_hdr |= (info->quant_type & 0x1) << 24;
                vop_hdr |= (info->quarter_sample & 0x1) << 23;
                vop_hdr |= (info->resync_marker_disable & 0x1) << 22; //error_res_disable
                vop_hdr |= (hdr.vop_coding_type & 0x3) << 18;
                vop_hdr |= (info->rounding_control &0x1) << 17;
                vop_hdr |= (hdr.intra_dc_vlc_thr & 0x7) << 8;
                vop_hdr |= (info->top_field_first& 0x1) << 7;
                vop_hdr	|= (info->alternate_vertical_scan_flag & 0x1) << 6;
                vop_hdr	|= (hdr.vop_coding_type != VOP_I ? info->vop_fcode_forward & 0x7 : 0) << 3;
                vop_hdr	|= (hdr.vop_coding_type == VOP_B ? info->vop_fcode_backward & 0x7 : 0) << 0;
                writel(vop_hdr, ve_regs + VE_MPEG_VOP_HDR);

                writel(iq_input, ve_regs + VE_MPEG_QP_INPUT);
            
                writel(readl(ve_regs + VE_MPEG_MBA)&0xff, ve_regs + VE_MPEG_MBA);
                
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
                        
                        if(decoder_p->vol_hdr.shape == BIN_SHAPE)
                           marker_length=17;
                        else {
                            if(hdr.vop_coding_type == VOP_I)
                                marker_length = 17;
                            else if(hdr.vop_coding_type == VOP_B) {
                                int fcode = ((hdr.fcode_forward) > (hdr.fcode_backward) ?  
                                    (hdr.fcode_forward) : (hdr.fcode_backward));
                                marker_length=15+fcode < 17 ? 15+fcode : 17;
                                //add +1 for 1 bit
                                marker_length +=1;
                            }
                            else if(hdr.vop_coding_type == VOP_P) {
                                marker_length = 15+hdr.fcode_forward + 1;
                            }
                        }
                        if(find_resynccode(&bs, marker_length)) {
                            //get_bits(&bs, marker_length);
                            decode_packet_header(&bs, 
                                                 info,
                                                 &decoder_p->pkt_hdr, 
                                                 &decoder_p->vol_hdr);
                            more_mbs=1;
                        }
                    }
                }
                int veCurMBA = readl(ve_regs + VE_MPEG_MBA);
                writel(readl(ve_regs + VE_MPEG_CTRL) | 0x7C, ve_regs + VE_MPEG_CTRL);            
            }
            // stop MPEG engine
            writel((readl(ve_regs + VE_CTRL) & ~0xf) | 0x7, ve_regs + VE_CTRL);
    	}
	return VDP_STATUS_OK;
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
