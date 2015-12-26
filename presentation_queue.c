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

#include "vdpau_private.h"
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include "sunxi_disp_ioctl.h"
#include "ve.h"
#include <errno.h>

uint64_t get_time(void)
{
	struct timespec tp;

	if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1)
		return 0;

	return (uint64_t)tp.tv_sec * 1000000000ULL + (uint64_t)tp.tv_nsec;
}

VdpStatus vdp_presentation_queue_target_create_x11(VdpDevice device, Drawable drawable, VdpPresentationQueueTarget *target)
{
    uint32_t tmp[4];
    if (!target /* || !drawable */)
        return VDP_STATUS_INVALID_POINTER;

    device_ctx_t *dev = handle_get(device);
    if (!dev)
        return VDP_STATUS_INVALID_HANDLE;

    queue_target_ctx_t *qt = handle_create(sizeof(*qt), target, htype_presentation_target);
    if (!qt)
    {
        handle_release(device);
        return VDP_STATUS_RESOURCES;
    }

    qt->drawable = drawable;
    qt->fd = open("/dev/disp", O_RDWR);
    if (qt->fd == -1)
    {
        handle_release(device);
        handle_destroy(*target);
        return VDP_STATUS_ERROR;
    }

    dev->fb_fd = open("/dev/fb0", O_RDWR);
    if (dev->fb_fd == -1)
    {
        close(qt->fd);
        handle_release(device);
        handle_destroy(*target);
        return VDP_STATUS_ERROR;
    }

    int ver = SUNXI_DISP_VERSION;
    if (ioctl(qt->fd, DISP_CMD_VERSION, &ver) < 0)
    {
        close(qt->fd);
        close(dev->fb_fd);
        handle_release(device);
        handle_destroy(*target);
        return VDP_STATUS_ERROR;
    }

    if (ioctl(dev->fb_fd, FBIOGET_LAYER_HDL_0, &dev->fb_layer_id))
    {
        close(qt->fd);
        close(dev->fb_fd);
        handle_release(device);
        handle_destroy(*target);
        return VDP_STATUS_ERROR;
    }
    uint32_t args[4]; 
    int i;
    for (i = 0x65; i <= 0x67; i++)
    {
    //release possibly lost allocated layers
       args[0] = dev->fb_id;
       args[1] = i;
       args[2] = 0;
       args[3] = 0;
       ioctl(qt->fd, DISP_CMD_LAYER_RELEASE, &args[0]);
    }

    args[1] = DISP_LAYER_WORK_MODE_SCALER;
    qt->layer = ioctl(qt->fd, DISP_CMD_LAYER_REQUEST, args);
    if (qt->layer == 0)
    {
            close(qt->fd);
            close(dev->fb_fd);
            handle_release(device);
            handle_destroy(*target);
            return VDP_STATUS_RESOURCES;
    }

    //XSetWindowBackground(dev->display, drawable, 0x000102);

    __disp_colorkey_t ck;
#if 1
    ck.ck_max.red = ck.ck_min.red = 0x0;
    ck.ck_max.green = ck.ck_min.green = 0x1;
    ck.ck_max.blue = ck.ck_min.blue = 0x2;
    ck.ck_max.alpha = ck.ck_min.alpha = 0xff;
#endif
    ck.red_match_rule = 2;
    ck.green_match_rule = 2;
    ck.blue_match_rule = 2;

    args[0] = dev->fb_id;
    args[1] = (unsigned long)(&ck);
    ioctl(qt->fd, DISP_CMD_SET_COLORKEY, args);

    tmp[0] = dev->fb_id;
    int ret;
    ret = ioctl(qt->fd, DISP_CMD_SCN_GET_WIDTH, tmp);
    qt->screen_width = ret;

    ret = ioctl(qt->fd, DISP_CMD_SCN_GET_HEIGHT, tmp);
    qt->screen_height = ret;

#if 1
    __disp_layer_info_t layer_info;
    tmp[0] = dev->fb_id;
    tmp[1] = dev->fb_layer_id;
    tmp[2] = (unsigned long) (&layer_info);
    tmp[3] = 0;
    if (ioctl(qt->fd, DISP_CMD_LAYER_GET_PARA, tmp) < 0)
    {
            printf("layer get para failed\n");
    }
    layer_info.alpha_en = 1;
    layer_info.alpha_val = 255;

    if (ioctl(qt->fd, DISP_CMD_LAYER_SET_PARA, tmp) < 0)
    {
            printf("layer get para failed\n");
    }
#endif

#if 0
    /* Enable color key for the overlay layer */
    tmp[0] = dev->fb_id;
    tmp[1] = qt->layer;
    if (ioctl(qt->fd, DISP_CMD_LAYER_CK_ON, &tmp) < 0)
    {
            printf("layer ck on failed\n");
    }
#endif
    args[0] = dev->fb_id;
    args[1] = (unsigned long)(&ck);
    args[2] = 0;
    args[3] = 0;
    ioctl(qt->fd, DISP_CMD_SET_BKCOLOR, args);
    
    tmp[0] = dev->fb_id;
    tmp[1] = qt->layer;
    if (ioctl(qt->fd, DISP_CMD_LAYER_TOP, &tmp) < 0)
    {
        printf("layer bottom 2 failed\n");
    }
#if 0
    // but should be 1 when layering is fixed again.
    /* Set the overlay layer below the screen layer */
    tmp[0] = dev->fb_id;
    tmp[1] = dev->fb_layer_id;
    if (ioctl(qt->fd, DISP_CMD_LAYER_TOP, &tmp) < 0)
    {
        printf("layer bottom 1 failed\n");
    }
#endif

#if 1
    /* Disable color key and enable global alpha for the screen layer */
    tmp[0] = dev->fb_id;
    tmp[1] = dev->fb_layer_id;
    if (ioctl(qt->fd, DISP_CMD_LAYER_CK_OFF, &tmp) < 0)
    {
            printf("layer ck off failed\n");
    }
    tmp[0] = dev->fb_id;
    tmp[1] = dev->fb_layer_id;
    tmp[2] = 0xFF;
    if (ioctl(qt->fd,DISP_CMD_LAYER_SET_ALPHA_VALUE,(void*)tmp) < 0)
    {
            printf("set alpha value failed\n");
    }

    tmp[0] = dev->fb_id;
    tmp[1] = dev->fb_layer_id;
    if (ioctl(qt->fd, DISP_CMD_LAYER_ALPHA_ON, &tmp) < 0)
    {
            printf("alpha on failed\n");
    }
#if 0
    error = ioctl(q->target->fd_disp, DISP_CMD_LAYER_BOTTOM, args);
    if(error < 0)
    {
            printf("layer top failed\n");
    }

    if (ioctl(qt->fd, DISP_CMD_VIDEO_START, args) < 0)
    {
            printf("video start failed\n");
    }
#endif
#endif
    printf("vdpau presentation target queue=%d created\n", *target);

    handle_release(device);
    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_target_destroy(VdpPresentationQueueTarget presentation_queue_target)
{
	queue_target_ctx_t *qt = handle_get(presentation_queue_target);
	if (!qt)
		return VDP_STATUS_INVALID_HANDLE;

	uint32_t args[4] = { 0, qt->layer, 0, 0 };
	ioctl(qt->fd, DISP_CMD_LAYER_CLOSE, args);
	ioctl(qt->fd, DISP_CMD_LAYER_RELEASE, args);

	close(qt->fd);

        handle_release(presentation_queue_target);
	handle_destroy(presentation_queue_target);

        printf("vdpau presentation target queue=%d destroyed\n", presentation_queue_target);
        return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_create(VdpDevice device, VdpPresentationQueueTarget presentation_queue_target, VdpPresentationQueue *presentation_queue)
{
	if (!presentation_queue)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	queue_target_ctx_t *qt = handle_get(presentation_queue_target);
	if (!qt)
        {
                handle_release(device);
		return VDP_STATUS_INVALID_HANDLE;
        }

	queue_ctx_t *q = handle_create(sizeof(*q), presentation_queue, htype_presentation);
	if (!q)
        {
                handle_release(device);
                handle_release(presentation_queue_target);
		return VDP_STATUS_RESOURCES;
        }

	//keep refcnt from handle_get increased, decrease when destroying the presentation queue
        q->target = qt;
	q->device = dev;
        q->target_hdl = presentation_queue_target;
        q->device_hdl = device;
        
        printf("vdpau presentation queue=%d created\n", *presentation_queue);

        return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_destroy(VdpPresentationQueue presentation_queue)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

        handle_release(q->target_hdl);
        handle_release(q->device_hdl);
        handle_release(presentation_queue);
	handle_destroy(presentation_queue);

        printf("vdpau presentation queue=%d destroyed\n", presentation_queue);
        return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_set_background_color(VdpPresentationQueue presentation_queue, VdpColor *const background_color)
{
	if (!background_color)
		return VDP_STATUS_INVALID_POINTER;

	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	q->background.red = background_color->red;
	q->background.green = background_color->green;
	q->background.blue = background_color->blue;
	q->background.alpha = background_color->alpha;

	handle_release(presentation_queue);
        return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_get_background_color(VdpPresentationQueue presentation_queue, VdpColor *const background_color)
{
	if (!background_color)
		return VDP_STATUS_INVALID_POINTER;

	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	background_color->red = q->background.red;
	background_color->green = q->background.green;
	background_color->blue = q->background.blue;
	background_color->alpha = q->background.alpha;

        handle_release(presentation_queue);
	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_get_time(VdpPresentationQueue presentation_queue, VdpTime *current_time)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	*current_time = get_time();
        handle_release(presentation_queue);
	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_display(VdpPresentationQueue presentation_queue, VdpOutputSurface surface, uint32_t clip_width, uint32_t clip_height, VdpTime earliest_presentation_time)
{
        int error;

	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	output_surface_ctx_t *os = handle_get(surface);
	if (!os)
        {
                handle_release(presentation_queue);
		return VDP_STATUS_INVALID_HANDLE;
        }

	if (!(os->vs))
	{
		printf("trying to display empty surface\n");
		VDPAU_DBG("trying to display empty surface");
                handle_release(presentation_queue);
                handle_release(surface);
		return VDP_STATUS_OK;
	}

	if (earliest_presentation_time != 0)
		VDPAU_DBG_ONCE("Presentation time not supported");

	//printf("%s: p_q=%d,o_s=%d\n", __FUNCTION__, presentation_queue, surface);

	Window c;
	int x=0,y=0;
	//XTranslateCoordinates(q->device->display, q->target->drawable, RootWindow(q->device->display, q->device->screen), 0, 0, &x, &y, &c);
	//XClearWindow(q->device->display, q->target->drawable);

	__disp_layer_info_t layer_info;
	memset(&layer_info, 0, sizeof(layer_info));
	layer_info.pipe = 1;
#if 1
        layer_info.alpha_en = 1;
        layer_info.alpha_val = 0xff;
#endif
	layer_info.mode = DISP_LAYER_WORK_MODE_SCALER;
	layer_info.fb.format = DISP_FORMAT_YUV420;
	layer_info.fb.seq = DISP_SEQ_UVUV;
	switch (os->vs->source_format) {
	case VDP_YCBCR_FORMAT_YUYV:
		layer_info.fb.mode = DISP_MOD_INTERLEAVED;
		layer_info.fb.format = DISP_FORMAT_YUV422;
		layer_info.fb.seq = DISP_SEQ_YUYV;
		break;
	case VDP_YCBCR_FORMAT_UYVY:
		layer_info.fb.mode = DISP_MOD_INTERLEAVED;
		layer_info.fb.format = DISP_FORMAT_YUV422;
		layer_info.fb.seq = DISP_SEQ_UYVY;
		break;
	case VDP_YCBCR_FORMAT_NV12:
		layer_info.fb.mode = DISP_MOD_NON_MB_UV_COMBINED;
		break;
	case VDP_YCBCR_FORMAT_YV12:
		layer_info.fb.mode = DISP_MOD_NON_MB_PLANAR;
		break;
	default:
	case INTERNAL_YCBCR_FORMAT:
		layer_info.fb.mode = DISP_MOD_MB_UV_COMBINED;
		break;
	}
	
	layer_info.fb.br_swap = 0;
	//recalc data to cpu kernel addresses (+ 0x40000000)
	layer_info.fb.addr[0] = cedarv_virt2phys(os->vs->dataY) + 0x40000000;
	layer_info.fb.addr[1] = cedarv_virt2phys(os->vs->dataU)/* + os->vs->plane_size*/ + 0x40000000;
	if( cedarv_isValid(os->vs->dataV))
	  layer_info.fb.addr[2] = cedarv_virt2phys(os->vs->dataV)/* + os->vs->plane_size + os->vs->plane_size / 4*/ + 0x40000000;

	layer_info.fb.cs_mode = DISP_BT709;
	layer_info.fb.size.width = os->vs->width; //q->target->screen_width;
	layer_info.fb.size.height = os->vs->width; //q->target->screen_height;
#if 0
       layer_info.src_win.x = 0;
       layer_info.src_win.y = 0;
       layer_info.src_win.width = os->vs->width;
       layer_info.src_win.height = os->vs->height;
       layer_info.scn_win.x = 0; //x + os->video_x;
       layer_info.scn_win.y = 0; //y + os->video_y;
#endif
	layer_info.src_win.x = os->video_src_rect.x0;
	layer_info.src_win.y = os->video_src_rect.y0;
	layer_info.src_win.width = os->video_src_rect.x1 - os->video_src_rect.x0;
	layer_info.src_win.height = os->video_src_rect.y1 - os->video_src_rect.y0;
	layer_info.scn_win.x = x + os->video_dst_rect.x0;
	layer_info.scn_win.y = y + os->video_dst_rect.y0;
	layer_info.scn_win.width = os->video_dst_rect.x1 - os->video_dst_rect.x0;
	layer_info.scn_win.height = os->video_dst_rect.y1 - os->video_dst_rect.y0;
	layer_info.ck_enable = 1;

	if (layer_info.scn_win.y < 0)
	{
		int cutoff = -(layer_info.scn_win.y);
		layer_info.src_win.y += cutoff;
		layer_info.src_win.height -= cutoff;
		layer_info.scn_win.y = 0;
		layer_info.scn_win.height -= cutoff;
	}


	uint32_t args[4] = { 0, q->target->layer, (unsigned long)(&layer_info), 0 };
	error = ioctl(q->target->fd, DISP_CMD_LAYER_SET_PARA, args);
	if(error < 0)
	{
		printf("set para failed\n");
	}

#if 1
	error = ioctl(q->target->fd, DISP_CMD_LAYER_OPEN, args);
	if(error < 0)
	{
		printf("layer open failed, fd=%d, errno=%d\n", q->target->fd, errno);
	}
	// Note: might be more reliable (but slower and problematic when there
	// are driver issues and the GET functions return wrong values) to query the
	// old values instead of relying on our internal csc_change.
	// Since the driver calculates a matrix out of these values after each
	// set doing this unconditionally is costly.
#endif
	if (os->csc_change) {
		ioctl(q->target->fd, DISP_CMD_LAYER_ENHANCE_OFF, args);
		args[2] = 0xff * os->brightness + 0x20;
		ioctl(q->target->fd, DISP_CMD_LAYER_SET_BRIGHT, args);
		args[2] = 0x20 * os->contrast;
		ioctl(q->target->fd, DISP_CMD_LAYER_SET_CONTRAST, args);
		args[2] = 0x20 * os->saturation;
		ioctl(q->target->fd, DISP_CMD_LAYER_SET_SATURATION, args);
		// hue scale is randomly chosen, no idea how it maps exactly
		args[2] = (32 / 3.14) * os->hue + 0x20;
		ioctl(q->target->fd, DISP_CMD_LAYER_SET_HUE, args);
		ioctl(q->target->fd, DISP_CMD_LAYER_ENHANCE_ON, args);
		os->csc_change = 0;
	}

        handle_release(presentation_queue);
        handle_release(surface);
	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_block_until_surface_idle(VdpPresentationQueue presentation_queue, VdpOutputSurface surface, VdpTime *first_presentation_time)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	output_surface_ctx_t *out = handle_get(surface);
	if (!out)
        {
                handle_release(presentation_queue);
		return VDP_STATUS_INVALID_HANDLE;
        }

	*first_presentation_time = get_time();

        handle_release(presentation_queue);
        handle_release(surface);
	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_query_surface_status(VdpPresentationQueue presentation_queue, VdpOutputSurface surface, VdpPresentationQueueStatus *status, VdpTime *first_presentation_time)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	output_surface_ctx_t *out = handle_get(surface);
	if (!out)
        {
                handle_release(presentation_queue);
		return VDP_STATUS_INVALID_HANDLE;
        }

	*status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;
	*first_presentation_time = get_time();

        handle_release(presentation_queue);
        handle_release(surface);
	return VDP_STATUS_OK;
}
