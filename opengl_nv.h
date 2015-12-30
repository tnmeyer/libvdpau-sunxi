#ifndef _OPENGL_NV_H_
#define _OPENGL_NV_H_

#include "vdpau_private.h"
#include <EGL/eglplatform_fb.h>
#include <EGL/fbdev_window.h>  

#include <EGL/egl.h>
#include <EGL/eglext.h>

typedef uint32_t vdpauSurfaceNV;

#define MAX_NUM_TEXTURES	6
typedef struct surface_nv_ctx_struct
{
  VdpVideoSurface 	surface;
  enum VdpauNVState	vdpNvState;
  uint32_t		target;
  GLsizei		numTextureNames;
  uint			textureNames[MAX_NUM_TEXTURES];
  EGLImageKHR		eglImage[MAX_NUM_TEXTURES];
  struct fbdev_pixmap 	cMemPixmap[MAX_NUM_TEXTURES];
  CEDARV_MEMORY         convY;
  CEDARV_MEMORY         convU;
  CEDARV_MEMORY         convV;
  uint32_t              conv_width;
  uint32_t              conv_height;

} surface_nv_ctx_t;

#endif
