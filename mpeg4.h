#ifndef _MPEG4_H_
#define _MPEG4_H_

#include "vdpau_private.h"

struct mp4_private_t;

#include "mp4_vld.h"

#define mmin(a, b)      ((a) < (b) ? (a) : (b))
#define mmax(a, b)      ((a) > (b) ? (a) : (b))

extern uint64_t get_time();

//vol_sprite_usage / sprite_enable
#define NULL_SPRITE     0
#define STATIC_SPRITE   1
#define GMC_SPRITE      2

// shapes
#define RECT_SHAPE       0
#define BIN_SHAPE        1
#define BIN_ONLY_SHAPE   2
#define GRAY_SHAPE       3

/* this is necessary for the max resolution (juice resolution) */
#define DEC_MBC         45
#define DEC_MBR         36

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
    int         mb_width;
    int         mb_height;
    int         mb_x;
    int         mb_y;
    int         mb_xpos;
    int         mb_ypos;
    int         mb_num;
    int 	curr_mb_num;
} video_packet_header_t;

typedef struct
{
    int vop_coding_type;
    int last_coding_type;
    int intra_dc_vlc_thr;
    int vop_quant;
    int quantizer;
    int fcode_forward;
    int fcode_backward;

    //macroblock
    int not_coded;
    int mcbpc;
    int derived_mb_type;
    int cbpc;
    int ac_pred_flag;
    int cbpy;
    int dquant;
    int cbp;

    int slice_height;
    int no_rounding;
    int flipflop_rounding;
    int use_skip_mb_code;
    int rl_table_index;
    int rl_chroma_table_index;
    int dc_table_index;
    int mv_table_index;
    int per_mb_rl_table;
    int per_mb_rl_bit;
    int msmpeg4_version;
    int bit_rate;
    int inter_intra_pred;
    uint16_t sprite_traj[4][2];      ///< sprite trajectory points
    int sprite_offset[2][2];         ///< sprite offset[isChroma][isMVY]
    int sprite_delta[2][2];          ///< sprite_delta [isY][isMVY]
    int sprite_shift[2];             ///< sprite shift [isChroma]
    int sprite_ref[4][2];
    int real_sprite_warping_points;
    int sprite_offset_impr[2][2];
    int sprite_delta_impr[2][2];
    int sprite_shift_impr[2];
    int virtual_ref[2][2];
    int virtual_ref2[2][2];
    int socx;
    int socy;
    int mv5_upper;
    int mv5_lower;
    int mv6_upper;
    int mv6_lower;
    int mask2;

    int vop_reduced_resolution;
    int vop_width;
    int vop_height;
    int vop_horizontal_mc_spatial_ref;
    int vop_vertical_mc_spatial_ref;
    int background_composition;
    int change_conv_ratio_disable;
    int vop_constant_alpha;
    int vop_constant_alpha_value;
    int top_field_first;
    int alternate_vertical_scan_flag;
    int num_gop_mbas;
} vop_header_t;

typedef struct
{
    void                        *mbh_buffer;
    void                        *dcac_buffer;
    void                        *ncf_buffer;
/*    vol_header_t                vol_hdr; */
    video_packet_header_t       pkt_hdr;
    VdpDecoderMpeg4VolHeader    mpeg4VolHdr;
    int                         mpeg4VolHdrSet;
    vop_header_t                vop_header;
    int                         MV[2][6][DEC_MBR+1][DEC_MBC+2];
    MP4_TABLES                  tables;
    int                         dc_scaler;
} mp4_private_t;

#define VOP_I	0
#define VOP_P	1
#define VOP_B	2
#define VOP_S	3

typedef struct {
  int val, len;
} VLCtabMb;

#define NOT_CODED -1
#define INTER			0
#define INTER_Q	  1
#define INTER4V		2
#define INTRA			3
#define INTRA_Q	 	4
#define STUFFING	7

#endif
