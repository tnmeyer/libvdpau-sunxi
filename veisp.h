#ifndef _VEISP_H_
#define _VEISP_H_

#include "ve.h"

#if 0
void veisp_setPicSize(int width, int height);
void veisp_setInBufferPtr(void *lumaPtr, void* chromaPtr);
void veisp_setOutBufferPtr(void *lumaPtr, void* chromaPtr);
void veisp_selectSubEngine();
void veisp_initCtrl(int input_fmt);
void veisp_trigger(void);
void veisp_setScalerFactor();
void ConvertMb32420ToNv21C(char* pSrc,char* pDst,int nPicWidth, int nPicHeight);
void ConvertMb32420ToNv21Y(char* pSrc,char* pDst,int nWidth, int nHeight);
void ConvertMb32420ToYv12C(char* pSrc,char* pDst,int nPicWidth, int nPicHeight);
void ConvertMb32420ToYv12Y(char* pSrc,char* pDst,int nWidth, int nHeight);
#endif

void cedarv_disp_init();
void cedarv_disp_close();
int cedarv_disp_convertMb2Yuv420(int width, int height, CEDARV_MEMORY y, CEDARV_MEMORY uv, 
                                 CEDARV_MEMORY convY, CEDARV_MEMORY convU, CEDARV_MEMORY convV);

#endif
