/*
 * Copyright (c) 2015 Martin Ostertag <martin.ostertag@gmx.de>
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

#include "vdpau_private.h"
#include "ve.h"
#include <vdpau/vdpau_x11.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include <EGL/eglplatform_fb.h>
#include <EGL/fbdev_window.h>  

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <assert.h>
#include "opengl_nv.h"
#include "ve.h"
#include "veisp.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/fbdev_window.h>
#include <stdlib.h>

#define USE_TILE 0

static PFNEGLCREATEIMAGEKHRPROC peglCreateImageKHR = NULL;
static PFNEGLDESTROYIMAGEKHRPROC peglDestroyImageKHR = NULL;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pglEGLImageTargetTexture2DOES = NULL;
static EGLDisplay eglDisplay;
static EGLContext eglSharedContext = EGL_NO_CONTEXT;
static EGLContext eglContext = EGL_NO_CONTEXT;
static EGLSurface    eglSurface = EGL_NO_SURFACE;

static void (*Log)(int loglevel, const char *format, ...);

void glVDPAUUnmapSurfacesNV(GLsizei numSurfaces, const vdpauSurfaceNV *surfaces);

static int TestEGLError(const char* pszLocation){

  /*
   eglGetError returns the last error that has happened using egl,
   not the status of the last called function. The user has to
   check after every single egl call or at least once every frame.
  */
   EGLint iErr = eglGetError();
   if (iErr != EGL_SUCCESS)
   {
      printf("%s failed (0x%x).\n", pszLocation, iErr);
      return 0;
   }

   return 1;
}

VdpStatus vdp_device_opengles_nv_open(EGLDisplay _eglDisplay, VdpGetProcAddress **get_proc_address)
{
  eglDisplay = _eglDisplay;
}

void glVDPAUInitNV(const void *vdpDevice, const void *getProcAddress, EGLContext shared_context,
                   void (*_Log)(int loglevel, const char *format, ...))
{
   eglSharedContext = shared_context;
   Log = _Log;

   peglCreateImageKHR =
    (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
 
  if(peglCreateImageKHR == NULL){
    printf("eglCreateImageKHR not found!\n");
  }

  peglDestroyImageKHR =
    (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
 
  if(peglDestroyImageKHR == NULL){
    printf("eglCreateImageKHR not found!\n");
  }

  pglEGLImageTargetTexture2DOES =
    (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

  if(pglEGLImageTargetTexture2DOES == NULL){
    printf("glEGLImageTargetTexture2DOES not found!\n");
  }
  if(shared_context != EGL_NO_CONTEXT)
  {
     EGLConfig     eglConfig  = 0;
     
     eglBindAPI(EGL_OPENGL_ES_API);
     EGLint pi32ConfigAttribs[7];
     pi32ConfigAttribs[0] = EGL_SURFACE_TYPE;
     pi32ConfigAttribs[1] = EGL_WINDOW_BIT | EGL_PBUFFER_BIT;
     pi32ConfigAttribs[2] = EGL_RENDERABLE_TYPE;
     pi32ConfigAttribs[3] = EGL_OPENGL_ES2_BIT;
     pi32ConfigAttribs[4] = EGL_NONE;
     int iConfigs;
     if (!eglChooseConfig(eglDisplay, pi32ConfigAttribs, &eglConfig, 1, &iConfigs) || (iConfigs != 1))
     {
        printf("Error: eglChooseConfig() failed.\n");
        exit(1);
     }

     eglSurface = eglCreatePbufferSurface(eglDisplay, eglConfig, NULL);
     if (!TestEGLError("eglCreateContext"))
     {
        exit(1);
     }
     EGLint ai32ContextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
     eglContext = eglCreateContext(eglDisplay, eglConfig, eglSharedContext, ai32ContextAttribs);
     if (!TestEGLError("eglCreateContext"))
     {
        exit(1);
     }
     eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
     if (!TestEGLError("eglMakeCurrent"))
     {
        exit(1);
     }
  }
  cedarv_disp_init();
}

void glVDPAUFiniNV(void)
{
   eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, eglContext);
   if(eglSurface != EGL_NO_SURFACE)
   {
      eglDestroySurface(eglDisplay, eglSurface);
      eglSurface = EGL_NO_SURFACE;
   }
   if(eglContext != EGL_NO_CONTEXT)
   {
      eglDestroyContext(eglDisplay, eglContext);
      eglContext = EGL_NO_CONTEXT;
   }
   cedarv_disp_close();
}

vdpauSurfaceNV glVDPAURegisterVideoSurfaceNV (const void *vdpSurface, uint32_t target, 
					    GLsizei numTextureNames, const uint *textureNames)
{
   vdpauSurfaceNV surfaceNV;

   assert(target == GL_TEXTURE_2D);
   
   video_surface_ctx_t *vs = (video_surface_ctx_t *)handle_get((uint32_t)vdpSurface);
   assert(vs);
   
   assert(vs->chroma_type == VDP_CHROMA_TYPE_420);

   assert(numTextureNames <= MAX_NUM_TEXTURES);
   
   surface_nv_ctx_t *nv = handle_create(sizeof(*nv), &surfaceNV, htype_nvidia_vdpau);
   assert(nv);

   assert(vs->vdpNvState == VdpauNVState_Unregistered);

   vs->vdpNvState = VdpauNVState_Registered;
   
   nv->surface 		= (uint32_t)vdpSurface;
   nv->vdpNvState 	= VdpauNVState_Registered;
   nv->target		= target;
   nv->numTextureNames 	= numTextureNames;
   memset(nv->textureNames, 0, sizeof(nv->textureNames));
   memcpy(nv->textureNames, textureNames, sizeof(uint) * numTextureNames);
   
   nv->convY = cedarv_malloc(vs->plane_size);
   nv->convU = cedarv_malloc(vs->plane_size/4);
   nv->convV = cedarv_malloc(vs->plane_size/4);
   nv->conv_width 	= (vs->width + 15) & ~15;
   nv->conv_height	= (vs->height + 15) & ~15;

   if (! cedarv_isValid(nv->convY) || ! cedarv_isValid(nv->convU) || ! cedarv_isValid(nv->convV))
   {
      handle_release(nv->surface);
      handle_destroy(surfaceNV);
      return 0;
   }

   //handle_release(vdpSurface);
 
   return surfaceNV;
}

vdpauSurfaceNV glVDPAURegisterOutputSurfaceNV (const void *vdpSurface, uint32_t target,
					     GLsizei numTextureNmes, const uint *textureNames)
{
}

int glVDPAUIsSurfaceNV (vdpauSurfaceNV surface)
{
}

void glVDPAUUnregisterSurfaceNV (vdpauSurfaceNV surface)
{
   surface_nv_ctx_t *nv  = handle_get(surface);
   assert(nv);
   
   video_surface_ctx_t *vs = handle_get(nv->surface);
   assert(vs);
   if(vs->vdpNvState == VdpauNVState_Mapped)
   {
      vdpauSurfaceNV surf[] = {surface};
      glVDPAUUnmapSurfacesNV(1, surf);
   }

   vs->vdpNvState = VdpauNVState_Unregistered;
   if(nv->surface)
   {
      handle_release(nv->surface);
      handle_destroy(nv->surface);
      nv->surface = 0;
   }
   if( cedarv_isValid(nv->convY) )
      cedarv_free(nv->convY);
   if (cedarv_isValid(nv->convU) )
      cedarv_free(nv->convU);
   if (cedarv_isValid(nv->convV) )
      cedarv_free(nv->convV);

   cedarv_setBufferInvalid(nv->convY);
   cedarv_setBufferInvalid(nv->convU);
   cedarv_setBufferInvalid(nv->convV);

   handle_release(surface);
   handle_destroy(surface); 
}

void glVDPAUGetSurfaceivNV(vdpauSurfaceNV surface, uint32_t pname, GLsizei bufSize,
			 GLsizei *length, int *values)
{
}

void glVDPAUSurfaceAccessNV(vdpauSurfaceNV surface, uint32_t access)
{
}
enum col_plane
{
   y_plane,
   u_plane,
   v_plane,
   uv_plane
};

static void createTexture2D(fbdev_pixmap *pm, surface_nv_ctx_t *nv, video_surface_ctx_t *vs, enum col_plane cp)
{
   GLenum format = GL_LUMINANCE;
   int buf_size = 8;
   int lum_size = 8;
   int alpha_size = 0;
   CEDARV_MEMORY mem;
   int width = 0;
   int height = 0;


   switch(cp)
   {
      case(y_plane):
#if USE_TILE
         mem = vs->dataY;
         width = vs->width;
         height = vs->height;
#else
         mem = nv->convY;
         width = nv->conv_width;
         height = nv->conv_height;
#endif
         break;
      case(u_plane):
#if USE_TILE
         mem = vs->dataU;
         width = (vs->width + 1)/2;
         height = (vs->height + 1)/2;
#else
         mem = nv->convU;
         width = (nv->conv_width + 1) / 2;
         height = (nv->conv_height+1) / 2;
#endif
         break;
      case(v_plane):
#if USE_TILE
         mem = vs->dataV;
         width = (vs->width + 1) / 2;
         height = (vs->height + 1) / 2;
#else
         mem = nv->convV;
         width = (nv->conv_width + 1) / 2;
         height = (nv->conv_height + 1) / 2;
#endif
         break;
      case(uv_plane):
         buf_size = 16;
         lum_size = 8;
         alpha_size = 8;
         format = GL_LUMINANCE_ALPHA;
#if USE_TILE
         mem = vs->dataU;
         width = (vs->width + 1) / 2;
         height = vs->height;
#else
         mem = nv->convU;
#endif
   }

   pm->bytes_per_pixel 	= buf_size / 8;
   pm->buffer_size 	= buf_size;
   pm->red_size 	= 0;
   pm->green_size 	= 0;
   pm->blue_size 	= 0;
   pm->alpha_size 	= alpha_size;
   pm->luminance_size 	= lum_size;
   pm->flags 		= FBDEV_PIXMAP_SUPPORTS_UMP;
   pm->format 		= 0;
   pm->width 		= width;
   pm->height 		= height;
   ump_reference_add(mem.mem_id);
   pm->data 		= (short unsigned int*)mem.mem_id;
   //cedarv_flush_cache(mem, cedarv_getSize(mem));
   
   glTexImage2D(GL_TEXTURE_2D, 0, format, pm->width, 
                 pm->height, 0, format, GL_UNSIGNED_BYTE, NULL);
   TestEGLError("createTexture2D");
}
void glVDPAUMapSurfacesNV(GLsizei numSurfaces, const vdpauSurfaceNV *surfaces)
{
  int i, j;
  glEnable(GL_TEXTURE_2D);

  for(j = 0; j < numSurfaces; j++)
  {
    surface_nv_ctx_t *nv = handle_get(surfaces[j]);
    assert(nv);
    const EGLint renderImageAttrs[] = {
      EGL_IMAGE_PRESERVED_KHR, EGL_FALSE, 
      EGL_NONE
    };

    video_surface_ctx_t *vs = handle_get(nv->surface);
    assert(vs);

    //Log(0, "glVDPAUMapSurfacesNV: starting MB2Yuv planar convert");
    cedarv_disp_convertMb2Yuv420(nv->conv_width, nv->conv_height,
                            vs->dataY, vs->dataU, nv->convY, nv->convU, nv->convV);
    //Log(0, "glVDPAUMapSurfacesNV: finished MB2Yuv planar convert");

    for(i = 0; (nv->vdpNvState == VdpauNVState_Registered) && (i < nv->numTextureNames); i++)
    {

      glActiveTexture(GL_TEXTURE0 + nv->textureNames[i]);
      EGLint iErr = eglGetError();
      assert (iErr == EGL_SUCCESS);
      
      glBindTexture(GL_TEXTURE_2D, nv->textureNames[i]);
      iErr = eglGetError();
      assert (iErr == EGL_SUCCESS);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      iErr = eglGetError();
      assert (iErr == EGL_SUCCESS);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      if (i == 0 || i == 1 )
      {
        createTexture2D(&nv->cMemPixmap[i], nv, vs, y_plane);
      }
      else if(i == 2 || i == 3) 
      {
        if(nv->numTextureNames == 6)
           createTexture2D(&nv->cMemPixmap[i], nv, vs, u_plane);
        else
           createTexture2D(&nv->cMemPixmap[i], nv, vs, uv_plane);
      }
      else
      {
           createTexture2D(&nv->cMemPixmap[i], nv, vs, v_plane);
      }
      vs->vdpNvState = VdpauNVState_Mapped;
      //create the chrominance egl image
      nv->eglImage[i] = peglCreateImageKHR(eglDisplay,
			  EGL_NO_CONTEXT,  
			  EGL_NATIVE_PIXMAP_KHR,
			  &nv->cMemPixmap[i],
			  renderImageAttrs);
      iErr = eglGetError();
      assert (iErr == EGL_SUCCESS);
      pglEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)nv->eglImage[i]);
      iErr = glGetError();
      assert (iErr == GL_NO_ERROR);
    }
    handle_release(nv->surface);
    nv->vdpNvState = VdpauNVState_Mapped;
    handle_release(surfaces[j]);
  }
}

void glVDPAUUnmapSurfacesNV(GLsizei numSurfaces, const vdpauSurfaceNV *surfaces)
{
  int i,j;
  
  for(j = 0; j < numSurfaces; j++)
  {
    surface_nv_ctx_t *nv  = handle_get(surfaces[j]);
    assert(nv);
    
    for(i = 0; (nv->vdpNvState == VdpauNVState_Mapped) && (i < nv->numTextureNames); i++)
    {
      video_surface_ctx_t *vs = handle_get(nv->surface);
      assert(vs);
      vs->vdpNvState = VdpauNVState_Registered;
      glActiveTexture(GL_TEXTURE0 + nv->textureNames[i]);
      glBindTexture(GL_TEXTURE_2D, 0);
      if(nv->eglImage[i])
      {
	peglDestroyImageKHR(eglDisplay, nv->eglImage[i]);
	nv->eglImage[i] = 0;
      }
      ump_reference_release(nv->cMemPixmap[i].data);
      //cedarv_setBufferInvalid((CEDARV_MEMORY)nv->cMemPixmap[i].data);
      handle_release(nv->surface);
    }
    nv->vdpNvState = VdpauNVState_Registered;
    handle_release(surfaces[j]);
  }
}

