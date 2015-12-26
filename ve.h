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

#ifndef __VE_H__
#define __VE_H__

#include <stdint.h>

int cedarv_open(void);
void cedarv_close(void);
int cedarv_get_version(void);
int cedarv_wait(int timeout);
void *cedarv_get(int engine, uint32_t flags);
void cedarv_put(void);
void* cedarv_get_regs();

#if USE_UMP
  #include <ump/ump.h>
  #include <ump/ump_ref_drv.h>

  typedef struct _CEDARV_MEMORY {
      ump_handle mem_id;
  }CEDARV_MEMORY;
#else
  typedef void* CEDARV_MEMORY;
  
#endif

CEDARV_MEMORY cedarv_malloc(int size);
int cedarv_isValid(CEDARV_MEMORY mem);
void cedarv_free(CEDARV_MEMORY mem);
uint32_t cedarv_virt2phys(CEDARV_MEMORY mem);
void cedarv_flush_cache(CEDARV_MEMORY mem, int len);
void cedarv_memcpy(CEDARV_MEMORY dst, size_t offset, const void * src, size_t len);
void* cedarv_getPointer(CEDARV_MEMORY mem);
size_t cedarv_getSize(CEDARV_MEMORY mem);
unsigned char cedarv_byteAccess(CEDARV_MEMORY mem, size_t offset);
void cedarv_setBufferInvalid(CEDARV_MEMORY mem);

static inline void writel(uint32_t val, void *addr)
{
	*((volatile uint32_t *)addr) = val;
}

static inline uint32_t readl(void *addr)
{
	return *((volatile uint32_t *) addr);
}

static inline void writeb(uint8_t val, void *addr)
{
   *((volatile uint8_t *)addr) = val;
}


#define CEDARV_ENGINE_MPEG			0x0
#define CEDARV_ENGINE_H264			0x1

#define CEDARV_CTRL				0x000
#define CEDARV_VERSION			0x0f0

#define CEDARV_MPEG_PIC_HDR			0x100
#define CEDARV_MPEG_VOP_HDR         0x104
#define CEDARV_MPEG_SIZE			0x108
#define CEDARV_MPEG_FRAME_SIZE		0x10c
#define CEDARV_MPEG_MBA             0x110
#define CEDARV_MPEG_CTRL			0x114
#define CEDARV_MPEG_TRIGGER			0x118
#define CEDARV_MPEG_STATUS			0x11c
#define CEDARV_MPEG_TRBTRD_FIELD	0x120
#define CEDARV_MPEG_TRBTRD_FRAME	0x124
#define CEDARV_MPEG_VLD_ADDR		0x128
#define CEDARV_MPEG_VLD_OFFSET		0x12c
#define CEDARV_MPEG_VLD_LEN			0x130
#define CEDARV_MPEG_VLD_END			0x134
#define CEDARV_MPEG_MBH_ADDR        0x138
#define CEDARV_MPEG_DCAC_ADDR       0x13c
#define CEDARV_MPEG_NCF_ADDR        0x144
#define CEDARV_MPEG_REC_LUMA		0x148
#define CEDARV_MPEG_REC_CHROMA		0x14c
#define CEDARV_MPEG_FWD_LUMA		0x150
#define CEDARV_MPEG_FWD_CHROMA		0x154
#define CEDARV_MPEG_BACK_LUMA		0x158
#define CEDARV_MPEG_BACK_CHROMA		0x15c
#define CEDARV_MPEG_SOCX            0x160
#define CEDARV_MPEG_SOCY            0x164
#define CEDARV_MPEG_SOL             0x168
#define CEDARV_MPEG_SDLX            0x16c
#define CEDARV_MPEG_SDLY            0x170
#define CEDARV_MPEG_SPRITESHIFT     0x174
#define CEDARV_MPEG_SDCX            0x178
#define CEDARV_MPEG_SDCY            0x17c
#define CEDARV_MPEG_IQ_MIN_INPUT	0x180
#define CEDARV_MPEG_QP_INPUT        0x184
#define CEDARV_MPEG_MSMPEG4_HDR     0x188
#define CEDARV_MPEG_MV5             0x1A8
#define CEDARV_MPEG_MV6             0x1AC
#define CEDARV_MPEG_JPEG_SIZE		0x1b8
#define CEDARV_MPEG_JPEG_RES_INT		0x1c0

#define CEDARV_MPEG_ERROR           0x1c4
#define CEDARV_MPEG_CTR_MB          0x1c8
#define CEDARV_MPEG_ROT_LUMA		0x1cc
#define CEDARV_MPEG_ROT_CHROMA		0x1d0
#define CEDARV_MPEG_SDROT_CTRL		0x1d4
#define CEDARV_MPEG_RAM_WRITE_PTR		0x1e0
#define CEDARV_MPEG_RAM_WRITE_DATA		0x1e4


#define CEDARV_H264_FRAME_SIZE		0x200
#define CEDARV_H264_PIC_HDR			0x204
#define CEDARV_H264_SLICE_HDR		0x208
#define CEDARV_H264_SLICE_HDR2		0x20c
#define CEDARV_H264_PRED_WEIGHT		0x210
#define CEDARV_H264_QP_PARAM		0x21c
#define CEDARV_H264_CTRL			0x220
#define CEDARV_H264_TRIGGER			0x224
#define CEDARV_H264_STATUS			0x228
#define CEDARV_H264_CUR_MB_NUM		0x22c
#define CEDARV_H264_VLD_ADDR		0x230
#define CEDARV_H264_VLD_OFFSET		0x234
#define CEDARV_H264_VLD_LEN			0x238
#define CEDARV_H264_VLD_END			0x23c
#define CEDARV_H264_SDROT_CTRL		0x240
#define CEDARV_H264_OUTPUT_FRAME_IDX	0x24c
#define CEDARV_H264_EXTRA_BUFFER1		0x250
#define CEDARV_H264_EXTRA_BUFFER2		0x254
#define CEDARV_H264_BASIC_BITS		0x2dc
#define CEDARV_H264_RAM_WRITE_PTR		0x2e0
#define CEDARV_H264_RAM_WRITE_DATA		0x2e4

#define CEDARV_SRAM_H264_PRED_WEIGHT_TABLE	0x000
#define CEDARV_SRAM_H264_FRAMEBUFFER_LIST	0x400
#define CEDARV_SRAM_H264_REF_LIST0		0x640
#define CEDARV_SRAM_H264_REF_LIST1		0x664
#define CEDARV_SRAM_H264_SCALING_LISTS	0x800

#define CEDARV_ISP_PIC_SIZE 		0x0a00 	//ISP source picture size in macroblocks (16x16)
#define CEDARV_ISP_PIC_STRIDE 		0x0a04 	//ISP source picture stride
#define CEDARV_ISP_CTRL 			0x0a08 	//ISP IRQ Control
#define CEDARV_ISP_TRIG 			0x0a0c 	//ISP Trigger
#define CEDARV_ISP_SCALER_SIZE 		0x0a2c 	//ISP scaler frame size/16
#define CEDARV_ISP_SCALER_OFFSET_Y 		0x0a30 	//ISP scaler picture offset for luma
#define CEDARV_ISP_SCALER_OFFSET_C 		0x0a34 	//ISP scaler picture offset for chroma
#define CEDARV_ISP_SCALER_FACTOR 		0x0a38 	//ISP scaler picture scale factor
//#define CEDARV_ISP_BUF??? 	0x0a44 	4B 	ISP PHY Buffer offset
//#define CEDARV_ISP_BUF??? 	0x0a48 	4B 	ISP PHY Buffer offset
//#define CEDARV_ISP_BUF??? 	0x0a4C 	4B 	ISP PHY Buffer offset
//#define CEDARV_ISP_?? 	0x0a74 	4B 	ISP ??
#define CEDARV_ISP_OUTPUT_LUMA 		0x0a70 	//ISP Output LUMA Address
#define CEDARV_ISP_OUTPUT_CHROMA 		0x0a74 	//ISP Output CHROMA Address
#define CEDARV_ISP_WB_THUMB_LUMA 		0x0a78 	//ISP THUMB WriteBack PHY LUMA Address
#define CEDARV_ISP_WB_THUMB_CHROMA 		0x0a7c 	//ISP THUMB WriteBack PHY CHROMA Adress
#define CEDARV_ISP_SRAM_INDEX 		0x0ae0 	//ISP VE SRAM Index
#define CEDARV_ISP_SRAM_DATA 		0x0ae4 	//ISP VE SRAM Data



#define CEDARV_MPEG_TRIG_FORMAT                     24
#define CEDARV_MPEG_TRIG_FORMAT_SIZE                0x7
#define CEDARV_MPEG_TRIG_FORMAT_RESERVED            0x0

#define CEDARV_MPEG_TRIG_COLOR_FORMAT               27
#define CEDARV_MPEG_TRIG_COLOR_FORMAT_SIZE          0x7
#define CEDARV_MPEG_TRIG_COLOR_FORMAT_YUV_4_2_0     0x0
#define CEDARV_MPEG_TRIG_COLOR_FORMAT_YUV_4_1_1     0x1
#define CEDARV_MPEG_TRIG_COLOR_FORMAT_YUV_4_2_2_HOR 0x2
#define CEDARV_MPEG_TRIG_COLOR_FORMAT_YUV_4_4_4     0x3
#define CEDARV_MPEG_TRIG_COLOR_FORMAT_YUV_4_2_2_VER 0x4

#define CEDARV_MPEG_TRIG_ERROR_DISABLE_BIT  31
#define CEDARV_MPEG_TRIG_ERROR_DISABLE_SIZE  0x1

#define CEDARV_MPEG_TRIG_ERROR_DISABLE(er_dis)      ((err_dis & CEDARV_MPEG_TRIG_ERROR_DISABLE_SIZE) << CEDARV_MPEG_TRIG_ERROR_DISABLE_BIT)

#endif
