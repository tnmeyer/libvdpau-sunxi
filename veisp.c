
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "ve.h"
#include "sunxi_disp_ioctl.h"
#include <errno.h>
#include <string.h>

static int fd = -1;

void cedarv_disp_init()
{
   if(fd == -1)
      fd = open("/dev/disp", O_RDWR);
}

void cedarv_disp_close()
{
   close(fd);
   fd = -1;
}

int cedarv_disp_convertMb2Yuv420(int width, int height, CEDARV_MEMORY y, CEDARV_MEMORY uv, CEDARV_MEMORY convY, CEDARV_MEMORY convU, CEDARV_MEMORY convV)
{
   unsigned long arg[4] = {0, 0, 0, 0};
   __disp_scaler_para_t scaler_para;
   int result; 
   arg[1] = ioctl(fd, DISP_CMD_SCALER_REQUEST, (unsigned long) arg);
   if(arg[1] == (unsigned long)-1) return 0;

   memset(&scaler_para, 0, sizeof(__disp_scaler_para_t));
   scaler_para.input_fb.addr[0] = cedarv_virt2phys(y);
   scaler_para.input_fb.addr[1] = cedarv_virt2phys(uv);
   scaler_para.input_fb.size.width = width;
   scaler_para.input_fb.size.height = height;
   scaler_para.input_fb.format = DISP_FORMAT_YUV420;
   scaler_para.input_fb.seq = DISP_SEQ_UVUV;
   scaler_para.input_fb.mode = DISP_MOD_MB_UV_COMBINED;
   scaler_para.input_fb.br_swap = 0;
   scaler_para.input_fb.cs_mode = DISP_BT601;
   scaler_para.source_regn.x = 0;
   scaler_para.source_regn.y = 0;
   scaler_para.source_regn.width = width;
   scaler_para.source_regn.height = height;
   scaler_para.output_fb.addr[0] = cedarv_virt2phys(convY);
   scaler_para.output_fb.addr[1] = cedarv_virt2phys(convU);
   scaler_para.output_fb.addr[2] = cedarv_virt2phys(convV);
   scaler_para.output_fb.size.width = width;
   scaler_para.output_fb.size.height = height;
   scaler_para.output_fb.format = DISP_FORMAT_YUV420;
   scaler_para.output_fb.seq = DISP_SEQ_P3210;
   scaler_para.output_fb.mode = DISP_MOD_NON_MB_PLANAR;
   scaler_para.output_fb.br_swap = 0;
   scaler_para.output_fb.cs_mode = DISP_BT601;

   arg[2] = (unsigned long) &scaler_para;
   result = ioctl(fd, DISP_CMD_SCALER_EXECUTE, (unsigned long) arg);
   if(result < 0)
      printf("scaler execution failed=%d\n", errno);
   ioctl(fd, DISP_CMD_SCALER_RELEASE, (unsigned long) arg);
   return 1;
}

#if 0
void veisp_setPicSize(int width, int height)
{
  void *cedarv_regs = cedarv_get_regs();

  uint32_t width_mb16  = (width + 15) / 16;
  uint32_t height_mb16 = (height + 15) / 16;
  uint32_t width_mb64  = ((width + 31) & ~31) / 16;
  uint32_t height_mb64 = ((height + 31) & ~31) / 16;
  uint32_t size  = ((width_mb16 & 0x3ff) << 16) | (height_mb16 & 0x3ff);
  uint32_t stride = (width_mb64 & 0x3ff) << 16 | height_mb64;
  uint32_t scaleSize = ((width_mb16 & 0xff) << 8) | ((height_mb16 & 0xff) << 0);
  writel(size, cedarv_regs + CEDARV_ISP_PIC_SIZE);
  writel(stride, cedarv_regs + CEDARV_ISP_PIC_STRIDE);
  writel(scaleSize, cedarv_regs + CEDARV_ISP_SCALER_SIZE);
  writel(0x80, cedarv_regs + CEDARV_ISP_SCALER_OFFSET_Y);
  writel(0x80, cedarv_regs + CEDARV_ISP_SCALER_OFFSET_C);
  
  veisp_setScalerFactor();
}

void veisp_setInBufferPtr(void *lumaPtr, void* chromaPtr)
{
  void *cedarv_regs = cedarv_get_regs();
  writel(lumaPtr, cedarv_regs + CEDARV_ISP_WB_THUMB_LUMA);
  writel(chromaPtr, cedarv_regs + CEDARV_ISP_WB_THUMB_CHROMA);
}

void veisp_setOutBufferPtr(void *lumaPtr, void* chromaPtr)
{
  void *cedarv_regs = cedarv_get_regs();
  writel(lumaPtr, cedarv_regs + CEDARV_ISP_OUTPUT_LUMA);
  writel(chromaPtr, cedarv_regs + CEDARV_ISP_OUTPUT_CHROMA);
}

void veisp_selectSubEngine()
{
  void *cedarv_regs = cedarv_get_regs();
  writel((readl(cedarv_regs + CEDARV_CTRL) & 0xfffffff0) | 0xa, cedarv_regs + CEDARV_CTRL);
}

void veisp_initCtrl(int input_fmt)
{
  void *cedarv_regs = cedarv_get_regs();
  uint32_t ctrl = 0;
  ctrl |= (input_fmt & 0x7) << 29;
  ctrl |= 0x1 << 25;
  ctrl |= 0 << 24;
  ctrl |= 1 << 19; //output enable, verified
  ctrl |= 0 << 16;
  ctrl |= 0xfff << 2;
  ctrl |= 0x1;
  writel(ctrl, cedarv_regs + CEDARV_ISP_CTRL);
}

void veisp_trigger(void)
{
  void *cedarv_regs = cedarv_get_regs();
  writel(0x1, cedarv_regs + CEDARV_ISP_TRIG);
}

void veisp_setScalerFactor()
{
  void *cedarv_regs = cedarv_get_regs();  
  uint32_t scale = (0x100 << 12) | (0x100);
  writel(scale, cedarv_regs + CEDARV_ISP_SCALER_FACTOR);
}

void ConvertToNv21Y(char* pSrc, char* pDst, int nWidth, int nHeight)
{
   int i = 0;
   int j = 0;
   int nMbWidth = 0;
   int nMbHeight = 0;
   int nLineStride=0;
   char* pSrcOffset;

   nLineStride = (nWidth + 15) &~15;
   nMbWidth = (nWidth+31)&~31;
   nMbWidth /= 32;

   nMbHeight = (nHeight+31)&~31;
   nMbHeight /= 32;
  
   int blockYMod = (nMbWidth*1024) % nWidth;
   int blockYLines = (nMbWidth*1024) / nWidth;
   int blockXMod = (8 * 1024) % nWidth;
   int blockXLines = (8 * 1024) / nWidth;
   
   for(i=0; i < nHeight; ++i)
   {
      for(j=0; j < nWidth; ++j)
      {
         //hier offset berechnung

         float x_pos = ((j % 256) / 32)*1024 + (j % 32) + j/256*blockXMod;
         float y_pos = ((i % 32) * 32) + (i/32)*blockYMod;
         float srcOffset = x_pos + y_pos;

         float x = fmod(srcOffset,nWidth);
         float y = (int)(srcOffset / nWidth) + (i/32)*blockYLines + (j/256)*blockXLines;
         pDst[i*nLineStride + j] = *(pSrc + (int)y*nWidth + (int)x);
      }
   }
   
}
void ConvertToNv21C(char* pSrc, char* pDst, int nWidth, int nHeight)
{
   int i = 0;
   int j = 0;
   int nMbWidth = 0;
   int nMbHeight = 0;
   int nLineStride=0;
   char* pSrcOffset;

   nLineStride = (nWidth*2 + 15) &~15;
   nMbWidth = (nWidth*2+31)&~31;
   nMbWidth /= 32;

   nMbHeight = (nHeight+31)&~31;
   nMbHeight /= 32;
  
   int blockYMod = (nMbWidth*1024) % nWidth;
   int blockYLines = (nMbWidth*1024) / nWidth;
   int blockXMod = (8 * 1024) % nWidth;
   int blockXLines = (8 * 1024) / nWidth;
   
   for(i=0; i < nHeight; ++i)
   {
      for(j=0; j < nWidth; j+=1)
      {
         //hier offset berechnung

         float x_pos = (((j*2) % 256) / 32)*1024 + (j*2 % 32) + (j*2/256)*blockXMod;
#if 1
         float y_pos = ((i % 32) * 32) + (i/32)*blockYMod;
#else
         float y_pos = ((i % 32) * 32) + (i/32)*nMbWidth*1024;
#endif
         float srcOffset = x_pos + y_pos;

         int xU = fmod(srcOffset, nWidth);
         int yU = (int)(srcOffset / nWidth) + (i/32)*blockYLines + (j*2/256)*blockXLines;
         int xV = fmod((srcOffset + 1), nWidth);
         int yV = (int)((srcOffset + 1) / nWidth) + (i/32)*blockYLines + (j*2/256)*blockXLines;

         pDst[i*nLineStride + j*2+1] = *(pSrc + yU*nWidth + xU);
         pDst[i*nLineStride + j*2] = *(pSrc + yV*nWidth + xV);
      }
   }
}

void ConvertMb32420ToNv21Y(char* pSrc,char* pDst,int nWidth, int nHeight)
{
	int nMbWidth = 0;
	int nMbHeight = 0;
	int i = 0;
	int j = 0;
	int m = 0;
	int k = 0;
    int nLineStride=0;
    int lineNum = 0;
    int offset = 0;
    char* ptr = NULL;
    char *dstAsm = NULL;
    char *srcAsm = NULL;
    char bufferU[32];
    int nWidthMatchFlag = 0;
    int nCopyMbWidth = 0;

    nLineStride = (nWidth + 15) &~15;
    nMbWidth = (nWidth+31)&~31;
    nMbWidth /= 32;

    nMbHeight = (nHeight+31)&~31;
    nMbHeight /= 32;
    ptr = pSrc;

    nWidthMatchFlag = 0;
	nCopyMbWidth = nMbWidth-1;

    if(nMbWidth*32 == nLineStride)
    {
    	nWidthMatchFlag = 1;
    	nCopyMbWidth = nMbWidth;

    }
    for(i=0; i<nMbHeight; i++)
    {
    	for(j=0; j<nCopyMbWidth; j++)
    	{
    		for(m=0; m<32; m++)
    		{
    			if((i*32 + m) >= nHeight)
    		  	{
    				ptr += 32;
    		    	continue;
    		  	}
    			srcAsm = ptr;
    			lineNum = i*32 + m;           //line num
    			offset =  lineNum*nLineStride + j*32;
    			dstAsm = pDst+ offset;

    			 asm volatile (
    					        "vld1.8         {d0 - d3}, [%[srcAsm]]              \n\t"
    					        "vst1.8         {d0 - d3}, [%[dstAsm]]              \n\t"
    					       	: [dstAsm] "+r" (dstAsm), [srcAsm] "+r" (srcAsm)
    					       	:  //[srcY] "r" (srcY)
    					       	: "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31");
    			ptr += 32;
    		}
    	}

    	if(nWidthMatchFlag == 1)
    	{
    		continue;
    	}
    	for(m=0; m<32; m++)
    	{
    		if((i*32 + m) >= nHeight)
    		{
    			ptr += 32;
    	    	continue;
    	   	}
    		dstAsm = bufferU;
    		srcAsm = ptr;
    	 	lineNum = i*32 + m;           //line num
    		offset =  lineNum*nLineStride + j*32;

    	   	 asm volatile (
    	    	      "vld1.8         {d0 - d3}, [%[srcAsm]]              \n\t"
    	              "vst1.8         {d0 - d3}, [%[dstAsm]]              \n\t"
    	         	    : [dstAsm] "+r" (dstAsm), [srcAsm] "+r" (srcAsm)
    	    	     	:  //[srcY] "r" (srcY)
    	    	    	: "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31");
    	   	ptr += 32;
    	   	for(k=0; k<32; k++)
    	   	{
    	   		if((j*32+ k) >= nLineStride)
    	   	   	{
    	   			break;
    	   	  	}
    	   	 	pDst[offset+k] = bufferU[k];
    	   	}
    	}
    }
}


void ConvertMb32420ToNv21C(char* pSrc,char* pDst,int nPicWidth, int nPicHeight)
{
	int nMbWidth = 0;
	int nMbHeight = 0;
	int i = 0;
	int j = 0;
	int m = 0;
	int k = 0;
    int nLineStride=0;
    int lineNum = 0;
    int offset = 0;
    char* ptr = NULL;
    char *dst0Asm = NULL;
    char *dst1Asm = NULL;
    char *srcAsm = NULL;
    char bufferV[16], bufferU[16];
    int nWidth = 0;
    int nHeight = 0;

    nWidth = (nPicWidth+1)/2;
    nHeight = (nPicHeight+1)/2;

    nLineStride = (nWidth*2 + 15) &~15;
    nMbWidth = (nWidth*2+31)&~31;
    nMbWidth /= 32;

    nMbHeight = (nHeight+31)&~31;
    nMbHeight /= 32;


    ptr = pSrc;

    for(i=0; i<nMbHeight; i++)
    {
    	for(j=0; j<nMbWidth; j++)
    	{
    		for(m=0; m<32; m++)
    		{
    			if((i*32 + m) >= nHeight)
    			{
    				ptr += 32;
    				continue;
        		}

    			dst0Asm = bufferU;
    			dst1Asm = bufferV;
    			srcAsm = ptr;
    			lineNum = i*32 + m;           //line num
    			offset =  lineNum*nLineStride + j*32;

    			asm volatile(
    					"vld2.8         {d0-d3}, [%[srcAsm]]              \n\t"
    			    	"vst1.8         {d0,d1}, [%[dst0Asm]]              \n\t"
    			    	"vst1.8         {d2,d3}, [%[dst1Asm]]              \n\t"
    			    	: [dst0Asm] "+r" (dst0Asm), [dst1Asm] "+r" (dst1Asm), [srcAsm] "+r" (srcAsm)
    			        :  //[srcY] "r" (srcY)
    			        : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
    			     );
    			ptr += 32;


    			for(k=0; k<16; k++)
    			{
    				if((j*32+ 2*k) >= nLineStride)
    				{
    					break;
    				}
    				pDst[offset+2*k]   = bufferV[k];
    			   	pDst[offset+2*k+1] = bufferU[k];
    			}
    		}
    	}
    }
}

void ConvertMb32420ToYv12C(char* pSrc,char* pDstU, char*pDstV,int nPicWidth, int nPicHeight)
{
	int nMbWidth = 0;
	int nMbHeight = 0;
	int i = 0;
	int j = 0;
	int m = 0;
	int k = 0;
    int nLineStride=0;
    int lineNum = 0;
    int offset = 0;
    char* ptr = NULL;
    char *dst0Asm = NULL;
    char *dst1Asm = NULL;
    char *srcAsm = NULL;
    int nWidth = 0;
    int nHeight = 0;
    char bufferV[16], bufferU[16];
    int nWidthMatchFlag = 0;
    int nCopyMbWidth = 0;

    nWidth = (nPicWidth+1)/2;
    nHeight = (nPicHeight+1)/2;

    //nLineStride = ((nPicWidth+ 15) &~15)/2;
    nLineStride = (nWidth+7)&~7;

    nMbWidth = (nWidth*2+31)&~31;
    nMbWidth /= 32;

    nMbHeight = (nHeight+31)&~31;
    nMbHeight /= 32;


    ptr = pSrc;

    nWidthMatchFlag = 0;
	nCopyMbWidth = nMbWidth-1;

    if(nMbWidth*16 == nLineStride)
    {
    	nWidthMatchFlag = 1;
    	nCopyMbWidth = nMbWidth;
    }

    for(i=0; i<nMbHeight; i++)
    {
    	for(j=0; j<nCopyMbWidth; j++)
    	{
    		for(m=0; m<32; m++)
    		{
    			if((i*32 + m) >= nHeight)
    			{
    				ptr += 32;
    				continue;
        		}

    			srcAsm = ptr;
    			lineNum = i*32 + m;           //line num
    			offset =  lineNum*nLineStride + j*16;
    			dst0Asm = pDstU+offset;
    		    dst1Asm = pDstV+offset;
    			asm volatile(
    					"vld2.8         {d0-d3}, [%[srcAsm]]              \n\t"
    			    	"vst1.8         {d0,d1}, [%[dst0Asm]]              \n\t"
    			    	"vst1.8         {d2,d3}, [%[dst1Asm]]              \n\t"
    			    	: [dst0Asm] "+r" (dst0Asm), [dst1Asm] "+r" (dst1Asm), [srcAsm] "+r" (srcAsm)
    			        :  //[srcY] "r" (srcY)
    			        : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
    			     );
    			ptr += 32;
    		}
    	}

    	if(nWidthMatchFlag == 1)
    	{
    		continue;
    	}
    	for(m=0; m<32; m++)
    	{
    		if((i*32 + m) >= nHeight)
    		{
    			ptr += 32;
    			continue;
    		}


    	   	srcAsm = ptr;
    	   	lineNum = i*32 + m;           //line num
    		offset =  lineNum*nLineStride + j*16;
    	   	dst0Asm = bufferU;
        	dst1Asm = bufferV;
    	   	asm volatile(
    	   			"vld2.8         {d0-d3}, [%[srcAsm]]              \n\t"
    	    		"vst1.8         {d0,d1}, [%[dst0Asm]]              \n\t"
    	    	 	"vst1.8         {d2,d3}, [%[dst1Asm]]              \n\t"
    	    	   	: [dst0Asm] "+r" (dst0Asm), [dst1Asm] "+r" (dst1Asm), [srcAsm] "+r" (srcAsm)
    	            :  //[srcY] "r" (srcY)
    	            : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
    	         );
    	   	ptr += 32;

    	   	for(k=0; k<16; k++)
    	   	{
    	   		if((j*16+ k) >= nLineStride)
    	   		{
    	   			break;
    	   		}
    	   		pDstV[offset+k] = bufferV[k];
    	   		pDstU[offset+k] = bufferU[k];
    	   	}
    	}
    }
}


void ConvertMb32422ToYv12C(char* pSrc,char* pDstU, char*pDstV,int nPicWidth, int nPicHeight)
{
	int nMbWidth = 0;
	int nMbHeight = 0;
	int i = 0;
	int j = 0;
	int m = 0;
	int k = 0;
    int nLineStride=0;
    int lineNum = 0;
    int offset = 0;
    char* ptr = NULL;
    char *dst0Asm = NULL;
    char *dst1Asm = NULL;
    char *srcAsm = NULL;

    int nWidth = 0;
    int nHeight = 0;
    char bufferV[16], bufferU[16];
    int nWidthMatchFlag = 0;
    int nCopyMbWidth = 0;

    nWidth = (nPicWidth+1)/2;
    nHeight = (nPicHeight+1)/2;

    nLineStride = (nWidth+7)&~7;

    nMbWidth = (nWidth*2+31)&~31;
    nMbWidth /= 32;

    nMbHeight = (nHeight*2+31)&~31;
    nMbHeight /= 32;

    ptr = pSrc;

    nWidthMatchFlag = 0;
	nCopyMbWidth = nMbWidth-1;

    if(nMbWidth*16 == nLineStride)
    {
    	nWidthMatchFlag = 1;
    	nCopyMbWidth = nMbWidth;
    }

    for(i=0; i<nMbHeight; i++)
    {
    	for(j=0; j<nCopyMbWidth; j++)
    	{
    		for(m=0; m<16; m++)
    		{
    			if((i*16 + m) >= nHeight)
    			{
    				ptr += 64;
    				continue;
        		}

    			srcAsm = ptr;
    			lineNum = i*16 + m;           //line num
    			offset =  lineNum*nLineStride + j*16;
    			dst0Asm = pDstU+offset;
    			dst1Asm = pDstV+offset;


    			asm volatile(
    					"vld2.8         {d0-d3}, [%[srcAsm]]              \n\t"
    			    	"vst1.8         {d0,d1}, [%[dst0Asm]]              \n\t"
    			    	"vst1.8         {d2,d3}, [%[dst1Asm]]              \n\t"
    			    	: [dst0Asm] "+r" (dst0Asm), [dst1Asm] "+r" (dst1Asm), [srcAsm] "+r" (srcAsm)
    			        :  //[srcY] "r" (srcY)
    			        : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
    			     );
    			ptr += 64;
    		}
    	}

    	if(nWidthMatchFlag==1)
    	{
    		continue;
    	}
    	for(m=0; m<16; m++)
    	{
    		if((i*16 + m) >= nHeight)
    	    {
    			ptr += 64;
    			continue;
    	    }

    		dst0Asm = bufferU;
    	    dst1Asm = bufferV;
    	   	srcAsm = ptr;
    	   	lineNum = i*16 + m;           //line num
    		offset =  lineNum*nLineStride + j*16;

    	   	asm volatile(
    	    			"vld2.8         {d0-d3}, [%[srcAsm]]              \n\t"
    	    		 	"vst1.8         {d0,d1}, [%[dst0Asm]]              \n\t"
    	    	    	"vst1.8         {d2,d3}, [%[dst1Asm]]              \n\t"
    	    	    	: [dst0Asm] "+r" (dst0Asm), [dst1Asm] "+r" (dst1Asm), [srcAsm] "+r" (srcAsm)
    	    	        :  //[srcY] "r" (srcY)
    	    	        : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
    	   		     );
    	   	ptr += 64;

    	   	for(k=0; k<16; k++)
    	    {
    	   		if((j*16+ k) >= nLineStride)
    	   		{
    	   			break;
    	   		}
    	    	pDstV[offset+k] = bufferV[k];
    	   	   	pDstU[offset+k] = bufferU[k];
    		}
    	}
    }
}

#endif
