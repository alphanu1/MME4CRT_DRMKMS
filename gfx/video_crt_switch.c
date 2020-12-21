/* CRT SwitchRes Core
 * Copyright (C) 2018 Alphanu / Ben Templeman.
 *
 * RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "drivers/drm_gfx.c"

#include "../retroarch.h"
#include "video_crt_switch.h"
#include "video_display_server.h"

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif



#if defined(HAVE_VIDEOCORE)
#include "include/userland/interface/vmcs_host/vc_vchi_gencmd.h"
static void crt_rpi_switch(int width, int height, float hz, int xoffset);
#endif

struct modeset_fbuf
{
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t size;
	uint32_t handle;
	uint8_t *map;
	uint32_t fb_id;
	uint32_t pixel_format;
};

#if defined(HAVE_KMS)
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdio.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <drm_fourcc.h>

 int s_shared_fd[10] = {};
 int s_shared_count[10] = {};
 int static_id = 0;

typedef struct modeline
{
	uint64_t    pclock;
	int    hactive;
	int    hbegin;
	int    hend;
	int    htotal;
	int    vactive;
	int    vbegin;
	int    vend;
	int    vtotal;
	int    interlace;
	int    doublescan;
	int    hsync;
	int    vsync;
	//
	double vfreq;
	double hfreq;
	//
	int    width;
	int    height;
	int    refresh;
	int    refresh_label;
	//
	int    type;
	int    range;
	uint64_t platform_data;
	//
	//mode_result result;
} modeline;



int m_id = 0;

		int m_drm_fd = 0;
		drmModeCrtc *mp_crtc_desktop = NULL;
		int m_card_id = 0;
		int drm_master_hook(int fd);

		char m_device_name[32];
		unsigned int m_desktop_output = 0;
		int m_video_modes_position = 0;

		void *mp_drm_handle = NULL;
		unsigned int m_dumb_handle = 0;
		unsigned int m_framebuffer_id = 0;

//#include <libkms.h>
static void crt_kms_switch(unsigned width, unsigned height, 
   int int_hz, float hz, int center, int monitor_index, 
   int xoffset, int padjust);
const char *get_connector_name(int mode);
int drm_master_hook(int last_fd);
bool drmkms_init();
bool update_mode(modeline *mode);
bool add_mode(modeline *mode);
bool delete_mode(modeline *mode);
#endif
int fbuffer = 1;




static void switch_crt_hz(videocrt_switch_t *p_switch)
{
   float ra_core_hz = p_switch->ra_core_hz;

   /* set hz float to an int for windows switching */
   if (ra_core_hz < 100)
   {
      if (ra_core_hz < 53)
         p_switch->ra_set_core_hz = 50;
      if (ra_core_hz >= 53  &&  ra_core_hz < 57)
         p_switch->ra_set_core_hz = 55;
      if (ra_core_hz >= 57)
         p_switch->ra_set_core_hz = 60;
   }

   if (ra_core_hz > 100)
   {
      if (ra_core_hz < 106)
         p_switch->ra_set_core_hz = 120;
      if (ra_core_hz >= 106  &&  ra_core_hz < 114)
         p_switch->ra_set_core_hz = 110;
      if (ra_core_hz >= 114)
         p_switch->ra_set_core_hz = 120;
   }

   video_monitor_set_refresh_rate(p_switch->ra_set_core_hz);

   p_switch->ra_tmp_core_hz = ra_core_hz;
}

static void crt_aspect_ratio_switch(
      videocrt_switch_t *p_switch,
      unsigned width, unsigned height)
{
   /* send aspect float to video_driver */
   p_switch->fly_aspect = (float)width / height;
   video_driver_set_aspect_ratio_value((float)p_switch->fly_aspect);
}

static void switch_res_crt(
      videocrt_switch_t *p_switch,
      unsigned width, unsigned height)
{
/*   video_display_server_set_resolution(width, height,
         p_switch->ra_set_core_hz,
         p_switch->ra_core_hz,
         p_switch->center_adjust,
         p_switch->index,
         p_switch->center_adjust,
         p_switch->porch_adjust); */

#if defined(HAVE_VIDEOCORE)
   crt_rpi_switch(width, height,
         p_switch->ra_core_hz,
         p_switch->center_adjust);
   video_monitor_set_refresh_rate(p_switch->ra_core_hz);
   crt_switch_driver_reinit();
#endif

#if defined(HAVE_KMS)
    crt_kms_switch(width, height,
         p_switch->ra_set_core_hz,
         p_switch->ra_core_hz,
         p_switch->center_adjust,
         p_switch->index,
         p_switch->center_adjust,
         p_switch->porch_adjust);
#endif
   video_driver_apply_state_changes();
}

/* Create correct aspect to fit video 
 * if resolution does not exist */
static void crt_screen_setup_aspect(
      videocrt_switch_t *p_switch,
      unsigned width, unsigned height)
{
#if defined(HAVE_VIDEOCORE)
   if (height > 300)
      height = height/2;
#endif

   if (p_switch->ra_core_hz != p_switch->ra_tmp_core_hz)
      switch_crt_hz(p_switch);

   /* Get original resolution of core */
   if (height == 4)
   {
      /* Detect menu only */
      if (width < 700)
         width = 320;

      height = 240;

      crt_aspect_ratio_switch(p_switch, width, height);
   }

   if (height < 200 && height != 144)
   {
      crt_aspect_ratio_switch(p_switch, width, height);
      height = 200;
   }

   if (height > 200)
      crt_aspect_ratio_switch(p_switch, width, height);

   if (height == 144 && p_switch->ra_set_core_hz == 50)
   {
      height = 288;
      crt_aspect_ratio_switch(p_switch, width, height);
   }

   if (height > 200 && height < 224)
   {
      crt_aspect_ratio_switch(p_switch, width, height);
      height = 224;
   }

   if (height > 224 && height < 240)
   {
      crt_aspect_ratio_switch(p_switch, width, height);
      height = 240;
   }

   if (height > 240 && height < 255)
   {
      crt_aspect_ratio_switch(p_switch, width, height);
      height = 254;
   }

   if (height == 528 && p_switch->ra_set_core_hz == 60)
   {
      crt_aspect_ratio_switch(p_switch, width, height);
      height = 480;
   }

   if (height >= 240 && height < 255 && p_switch->ra_set_core_hz == 55)
   {
      crt_aspect_ratio_switch(p_switch, width, height);
      height = 254;
   }

   switch_res_crt(p_switch, width, height);
}

static int crt_compute_dynamic_width(
      videocrt_switch_t *p_switch,
      int width)
{
   unsigned i;
   int       dynamic_width     = 0;
   unsigned       min_height   = 261;

#if defined(HAVE_VIDEOCORE)
   p_switch->p_clock           = 32000000;
#else
   p_switch->p_clock           = 21000000;
#endif

   for (i = 0; i < 10; i++)
   {
      dynamic_width = width * i;
      if ((dynamic_width * min_height * p_switch->ra_core_hz) 
            > p_switch->p_clock)
         break;
   }
   return dynamic_width;
}

void crt_switch_res_core(
      videocrt_switch_t *p_switch,
      unsigned width, unsigned height,
      float hz, unsigned crt_mode,
      int crt_switch_center_adjust,
      int crt_switch_porch_adjust,
      int monitor_index, bool dynamic)
{
   /* ra_core_hz float passed from within
    * video_driver_monitor_adjust_system_rates() */
   if (width == 4)
   {
      width                        = 320;
      height                       = 240;
   }

   p_switch->porch_adjust          = crt_switch_porch_adjust;
   p_switch->ra_core_height        = height;
   p_switch->ra_core_hz            = hz;

   if (dynamic)
      p_switch->ra_core_width      = crt_compute_dynamic_width(p_switch, width);
   else 
      p_switch->ra_core_width      = width;

   p_switch->center_adjust         = crt_switch_center_adjust;
   p_switch->index                 = monitor_index;

   if (crt_mode == 2)
   {
      if (hz > 53)
         p_switch->ra_core_hz      = hz * 2;
      if (hz <= 53)
         p_switch->ra_core_hz      = 120.0f;
   }

   /* Detect resolution change and switch */
   if (
         (p_switch->ra_tmp_height != p_switch->ra_core_height) ||
         (p_switch->ra_core_width != p_switch->ra_tmp_width) || 
         (p_switch->center_adjust != p_switch->tmp_center_adjust||
          p_switch->porch_adjust  !=  p_switch->tmp_porch_adjust )
      )
      crt_screen_setup_aspect(
            p_switch,
            p_switch->ra_core_width,
            p_switch->ra_core_height);

   p_switch->ra_tmp_height     = p_switch->ra_core_height;
   p_switch->ra_tmp_width      = p_switch->ra_core_width;
   p_switch->tmp_center_adjust = p_switch->center_adjust;
   p_switch->tmp_porch_adjust =  p_switch->porch_adjust;

   /* Check if aspect is correct, if not change */
   if (video_driver_get_aspect_ratio() != p_switch->fly_aspect)
   {
      video_driver_set_aspect_ratio_value((float)p_switch->fly_aspect);
      video_driver_apply_state_changes();
   }
}

#if defined(HAVE_VIDEOCORE)
static void crt_rpi_switch(int width, int height, float hz, int xoffset)
{
   char buffer[1024];
   VCHI_INSTANCE_T vchi_instance;
   VCHI_CONNECTION_T *vchi_connection  = NULL;
   static char output[250]             = {0};
   static char output1[250]            = {0};
   static char output2[250]            = {0};
   static char set_hdmi[250]           = {0};
   static char set_hdmi_timing[250]    = {0};
   int i                               = 0;
   int hfp                             = 0;
   int hsp                             = 0;
   int hbp                             = 0;
   int vfp                             = 0;
   int vsp                             = 0;
   int vbp                             = 0;
   int hmax                            = 0;
   int vmax                            = 0;
   int pdefault                        = 8;
   int pwidth                          = 0;
   int ip_flag                         = 0;
   float roundw                        = 0.0f;
   float roundh                        = 0.0f;
   float pixel_clock                   = 0.0f;

   /* set core refresh from hz */
   video_monitor_set_refresh_rate(hz);

   /* following code is the mode line generator */
   hsp    = (width * 0.117) - (xoffset*4);
   if (width < 700)
   {
      hfp    = (width * 0.065);
      hbp  = width * 0.35-hsp-hfp;
   }
   else
   {
      hfp  = (width * 0.033) + (width / 112);
      hbp  = (width * 0.225) + (width /58);
      xoffset = xoffset*2;
   }
   
   hmax = hbp;

   if (height < 241)
      vmax = 261;
   if (height < 241 && hz > 56 && hz < 58)
      vmax = 280;
   if (height < 241 && hz < 55)
      vmax = 313;
   if (height > 250 && height < 260 && hz > 54)
      vmax = 296;
   if (height > 250 && height < 260 && hz > 52 && hz < 54)
      vmax = 285;
   if (height > 250 && height < 260 && hz < 52)
      vmax = 313;
   if (height > 260 && height < 300)
      vmax = 318;

   if (height > 400 && hz > 56)
      vmax = 533;
   if (height > 520 && hz < 57)
      vmax = 580;

   if (height > 300 && hz < 56)
      vmax = 615;
   if (height > 500 && hz < 56)
      vmax = 624;
   if (height > 300)
      pdefault = pdefault * 2;

   vfp = (height + ((vmax - height) / 2) - pdefault) - height;

   if (height < 300)
      vsp = vfp + 3; /* needs to be 3 for progressive */
   if (height > 300)
      vsp = vfp + 6; /* needs to be 6 for interlaced */

   vsp  = 3;
   vbp  = (vmax-height)-vsp-vfp;
   hmax = width+hfp+hsp+hbp;

   if (height < 300)
   {
      pixel_clock = (hmax * vmax * hz) ;
      ip_flag     = 0;
   }

   if (height > 300)
   {
      pixel_clock = (hmax * vmax * (hz/2)) /2 ;
      ip_flag     = 1;
   }
   /* above code is the modeline generator */

   snprintf(set_hdmi_timing, sizeof(set_hdmi_timing),
         "hdmi_timings %d 1 %d %d %d %d 1 %d %d %d 0 0 0 %f %d %f 1 ",
         width, hfp, hsp, hbp, height, vfp,vsp, vbp,
         hz, ip_flag, pixel_clock);

   vcos_init();

   vchi_initialise(&vchi_instance);

   vchi_connect(NULL, 0, vchi_instance);

   vc_vchi_gencmd_init(vchi_instance, &vchi_connection, 1);

   vc_gencmd(buffer, sizeof(buffer), set_hdmi_timing);

   vc_gencmd_stop();

   vchi_disconnect(vchi_instance);

   snprintf(output1,  sizeof(output1),
         "tvservice -e \"DMT 87\" > /dev/null");
   system(output1);
   snprintf(output2,  sizeof(output1),
         "fbset -g %d %d %d %d 24 > /dev/null",
         width, height, width, height);
   system(output2);
}
#endif

#if defined(HAVE_KMS)


static void crt_kms_switch(unsigned width, unsigned height, 
   int int_hz, float hz, int center, int monitor_index, 
   int xoffset, int padjust)
{
  // int i                               = 0;
   int hfp                             = 0;
   int hsp                             = 0;
   int hbp                             = 0;
   int vfp                             = 0;
   int vsp                             = 0;
   int vbp                             = 0;
   int hmax                            = 0;
   int vmax                            = 0;
   int pdefault                        = 8;
   int pwidth                          = 0;
   int ip_flag                         = 0;
   float roundw                        = 0.0f;
   float roundh                        = 0.0f;
   float pixel_clock                   = 0.0f;

   video_monitor_set_refresh_rate(hz);

   drmkms_init();

   /* following code is the mode line generator */
   hsp    = (width * 0.117) - (xoffset*4);
   if (width < 700)
   {
      hfp    = (width * 0.065);
      hbp  = width * 0.35-hsp-hfp;
   }
   else
   {
      hfp  = (width * 0.033) + (width / 112);
      hbp  = (width * 0.225) + (width /58);
      xoffset = xoffset*2;
   }
   
   hmax = hbp;

   if (height < 241)
      vmax = 261;
   if (height < 241 && hz > 56 && hz < 58)
      vmax = 280;
   if (height < 241 && hz < 55)
      vmax = 313;
   if (height > 250 && height < 260 && hz > 54)
      vmax = 296;
   if (height > 250 && height < 260 && hz > 52 && hz < 54)
      vmax = 285;
   if (height > 250 && height < 260 && hz < 52)
      vmax = 313;
   if (height > 260 && height < 300)
      vmax = 318;

   if (height > 400 && hz > 56)
      vmax = 533;
   if (height > 520 && hz < 57)
      vmax = 580;

   if (height > 300 && hz < 56)
      vmax = 615;
   if (height > 500 && hz < 56)
      vmax = 624;
   if (height > 300)
      pdefault = pdefault * 2;

   vfp = (height + ((vmax - height) / 2) - pdefault) - height;

   if (height < 300)
      vsp = vfp + 3; /* needs to be 3 for progressive */
   if (height > 300)
      vsp = vfp + 6; /* needs to be 6 for interlaced */

   vsp  = 3;
   vbp  = (vmax-height)-vsp-vfp;
   hmax = width+hfp+hsp+hbp;

   if (height < 300)
   {
      pixel_clock = (hmax * vmax * hz) ;
      ip_flag     = 0;
   }

   if (height > 300)
   {
      pixel_clock = (hmax * vmax * (hz/2)) /2 ;
      ip_flag     = 1;
   }

//    int fd = 5;

//  struct modeset_buf fbuf;
//  fbuf.width = width;
//  fbuf.height = height;


//  int ret;
//    drmModeConnector *connector;
//    uint i;

//    drm.fd = open("/dev/dri/card0", O_RDWR);
//    ret = drmSetClientCap(drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
//    ret = drmSetClientCap(drm.fd, DRM_CLIENT_CAP_ATOMIC, 1);
// drm.resources = drmModeGetResources(drm.fd);
// connector = drmModeGetConnector(drm.fd, drm.resources->connectors[0]);
// drm.encoder = drmModeGetEncoder(drm.fd, drm.resources->encoders[0]);
// drmModeFreeEncoder(drm.encoder);

// drmModeFreeConnector(connector);

// ret = modeset_create_dumbfb(drm.fd, &fbuf, 4, DRM_FORMAT_XRGB8888);

// drm.crtc_id = drm.encoder->crtc_id;
// drm.connector_id = connector->connector_id;
// drm.orig_crtc = drmModeGetCrtc(drm.fd, drm.encoder->crtc_id);
// drm.current_mode = &(drm.orig_crtc->mode);

// struct _drmModeModeInfo *NewDrmMode = &(drm.orig_crtc->mode);

// NewDrmMode->clock = pixel_clock / 1000;
// NewDrmMode->hdisplay = width;
// NewDrmMode->vdisplay = height;
// NewDrmMode->hsync_start = hbp;
// NewDrmMode->hsync_end = hsp;
// NewDrmMode->htotal = hfp;
// NewDrmMode->vsync_start = vbp;
// NewDrmMode->vsync_end = vsp;
// NewDrmMode->vtotal = vfp;
// NewDrmMode->hskew = 0;
// NewDrmMode->vscan = 0;

// drmModeSetCrtc(drm.fd, drm.crtc_id, fbuf.fb_id, 0, 0,
//             &drm.connector_id, 1, NewDrmMode);

// ret = modeset_create_dumbfb(drm.fd, &buf, 4, DRM_FORMAT_XRGB8888);

struct modeline dmode;
                        // Create specific mode name
                        //snprintf(dmode.name, 32, "SR-%d_%dx%d@%.02f%s", m_id, width, height, hz);
                        //dmode.       = pixel_clock  / 1000;
                        dmode.hactive    = width;
                        dmode.hbegin = hfp;
                        dmode.hend   = hsp;
                        dmode.htotal      = hbp;
                        dmode.vactive    = height;
                        dmode.vbegin = vfp;
                        dmode.vend   = vsp;
                        dmode.vtotal      = vbp;
                        //dmode.flags       = 10;

                       // dmode.hskew       = 0;
                       // dmode.vscan       = 0;

                        dmode.vfreq    = hz;	// Used only for human readable output

                        dmode.type        = DRM_MODE_TYPE_USERDEF;	//DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

                        drmModeModeInfo *modeline = &dmode;
   update_mode(modeline);

}


const char *get_connector_name(int mode)
{
	switch (mode)
	{
		case DRM_MODE_CONNECTOR_Unknown:
			return "Unknown";
		case DRM_MODE_CONNECTOR_VGA:
			return "VGA-";
		case DRM_MODE_CONNECTOR_DVII:
			return "DVI-I-";
		case DRM_MODE_CONNECTOR_DVID:
			return "DVI-D-";
		case DRM_MODE_CONNECTOR_DVIA:
			return "DVI-A-";
		case DRM_MODE_CONNECTOR_Composite:
			return "Composite-";
		case DRM_MODE_CONNECTOR_SVIDEO:
			return "SVIDEO-";
		case DRM_MODE_CONNECTOR_LVDS:
			return "LVDS-";
		case DRM_MODE_CONNECTOR_Component:
			return "Component-";
		case DRM_MODE_CONNECTOR_9PinDIN:
			return "9PinDIN-";
		case DRM_MODE_CONNECTOR_DisplayPort:
			return "DisplayPort-";
		case DRM_MODE_CONNECTOR_HDMIA:
			return "HDMI-A-";
		case DRM_MODE_CONNECTOR_HDMIB:
			return "HDMI-B-";
		case DRM_MODE_CONNECTOR_TV:
			return "TV-";
		case DRM_MODE_CONNECTOR_eDP:
			return "eDP-";
		case DRM_MODE_CONNECTOR_VIRTUAL:
			return "VIRTUAL-";
		case DRM_MODE_CONNECTOR_DSI:
			return "DSI-";
		case DRM_MODE_CONNECTOR_DPI:
			return "DPI-";
		default:
			return "not_defined-";
	}
}



void drmkms_timing(char *device_name)
{
	//m_vs = *vs;
	m_id = ++static_id;

	//log_verbose("DRM/KMS: <%d> (drmkms_timing) creation (%s)\n", m_id, device_name);
	// Copy screen device name and limit size
	if ((strlen(device_name) + 1) > 32)
	{
		strncpy(m_device_name, device_name, 31);
		//log_error("DRM/KMS: <%d> (drmkms_timing) [ERROR] the devine name is too long it has been trucated to %s\n", m_id, m_device_name);
	}
	else
		strcpy(m_device_name, device_name);
}

bool drmkms_init()
{
	//log_verbose("DRM/KMS: <%d> (init) loading DRM/KMS library\n", m_id);
	mp_drm_handle = dlopen("libdrm.so", RTLD_NOW);
/*	if (mp_drm_handle)
	{
		drmGetVersion = (drmGetVersion)dlsym(mp_drm_handle, "drmGetVersion");
		if (drmGetVersion == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmGetVersion", "DRM_LIBRARY");
			return false;
		}

		drmFreeVersion = (drmFreeVersion) dlsym(mp_drm_handle, "drmFreeVersion");
		if (drmFreeVersion == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmFreeVersion", "DRM_LIBRARY");
			return false;
		}

		drmModeGetResources =  (drmModeGetResources)dlsym(mp_drm_handle, "drmModeGetResources");
		if (drmModeGetResources == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeGetResources", "DRM_LIBRARY");
			return false;
		}

		drmModeGetConnector =  (drmModeGetConnector)dlsym(mp_drm_handle, "drmModeGetConnector");
		if (drmModeGetConnector == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeGetConnector", "DRM_LIBRARY");
			return false;
		}

		drmModeFreeConnector = (drmModeFreeConnector)dlsym(mp_drm_handle, "drmModeFreeConnector");
		if (drmModeFreeConnector == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeFreeConnector", "DRM_LIBRARY");
			return false;
		}

		drmModeFreeResources = (drmModeFreeConnector)dlsym(mp_drm_handle, "drmModeFreeResources");
		if (drmModeFreeResources == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeFreeResources", "DRM_LIBRARY");
			return false;
		}

		drmModeGetEncoder = (drmModeGetEncoder) dlsym(mp_drm_handle, "drmModeGetEncoder");
		if (drmModeGetEncoder == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeGetEncoder", "DRM_LIBRARY");
			return false;
		}

		drmModeFreeEncoder = (drmModeFreeEncoder) dlsym(mp_drm_handle, "drmModeFreeEncoder");
		if (drmModeFreeEncoder == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeFreeEncoder", "DRM_LIBRARY");
			return false;
		}

		drmModeGetCrtc = (drmModeGetCrtc) dlsym(mp_drm_handle, "drmModeGetCrtc");
		if (drmModeGetCrtc == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeGetCrtc", "DRM_LIBRARY");
			return false;
		}

		drmModeSetCrtc = (drmModeSetCrtc) dlsym(mp_drm_handle, "drmModeSetCrtc");
		if (drmModeSetCrtc == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeSetCrtc", "DRM_LIBRARY");
			return false;
		}

		drmModeFreeCrtc = (drmModeFreeCrtc) dlsym(mp_drm_handle, "drmModeFreeCrtc");
		if (drmModeFreeCrtc == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeFreeCrtc", "DRM_LIBRARY");
			return false;
		}

		drmModeAttachMode = (drmModeAttachMode) dlsym(mp_drm_handle, "drmModeAttachMode");
		if (drmModeAttachMode == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeAttachMode", "DRM_LIBRARY");
			return false;
		}

		drmModeAddFB = (drmModeAddFB) dlsym(mp_drm_handle, "drmModeAddFB");
		if (drmModeAddFB == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeAddFB", "DRM_LIBRARY");
			return false;
		}

		drmModeRmFB = (drmModeRmFB) dlsym(mp_drm_handle, "drmModeRmFB");
		if (drmModeRmFB == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeRmFB", "DRM_LIBRARY");
			return false;
		}

		drmModeGetFB = (drmModeGetFB) dlsym(mp_drm_handle, "drmModeGetFB");
		if (drmModeGetFB == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeGetFB", "DRM_LIBRARY");
			return false;
		}

		drmModeFreeFB = (drmModeFreeFB) dlsym(mp_drm_handle, "drmModeFreeFB");
		if (drmModeFreeFB == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeFreeFB", "DRM_LIBRARY");
			return false;
		}

		drmPrimeHandleToFD = (__typeof__(drmPrimeHandleToFD)) dlsym(mp_drm_handle, "drmPrimeHandleToFD");
		if (drmPrimeHandleToFD == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmPrimeHandleToFD", "DRM_LIBRARY");
			return false;
		}

		drmModeGetPlaneResources = (drmModeGetPlaneResources) dlsym(mp_drm_handle, "drmModeGetPlaneResources");
		if (drmModeGetPlaneResources == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeGetPlaneResources", "DRM_LIBRARY");
			return false;
		}

		drmModeFreePlaneResources = (drmModeFreePlaneResources) dlsym(mp_drm_handle, "drmModeFreePlaneResources");
		if (drmModeFreePlaneResources == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmModeFreePlaneResources", "DRM_LIBRARY");
			return false;
		}

		drmIoctl = (drmIoctl) dlsym(mp_drm_handle, "drmIoctl");
		if (drmIoctl == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmIoctl", "DRM_LIBRARY");
			return false;
		}

		drmGetCap = (drmGetCap) dlsym(mp_drm_handle, "drmGetCap");
		if (drmGetCap == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmGetCap", "DRM_LIBRARY");
			return false;
		}

		drmIsMaster = (drmIsMaster) dlsym(mp_drm_handle, "drmIsMaster");
		if (drmIsMaster == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmIsMaster", "DRM_LIBRARY");
			return false;
		}

		drmSetMaster = (drmSetMaster) dlsym(mp_drm_handle, "drmSetMaster");
		if (drmSetMaster == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmSetMaster", "DRM_LIBRARY");
			return false;
		}

		drmDropMaster = (drmDropMaster) dlsym(mp_drm_handle, "drmDropMaster");
		if (drmDropMaster == NULL)
		{
			//log_error("DRM/KMS: <%d> (init) [ERROR] missing func %s in %s", m_id, "drmDropMaster", "DRM_LIBRARY");
			return false;
		}
	}
	else
	{
		//log_error("DRM/KMS: <%d> (init) [ERROR] missing %s library\n", m_id, "DRM/KMS_LIBRARY");
		return false;
	}
*/
	int screen_pos = -1;

	// Handle the screen name, "auto", "screen[0-9]" and device name
	if (strlen(m_device_name) == 7 && !strncmp(m_device_name, "screen", 6) && m_device_name[6] >= '0' && m_device_name[6] <= '9')
		screen_pos = m_device_name[6] - '0';
	else if (strlen(m_device_name) == 1 && m_device_name[0] >= '0' && m_device_name[0] <= '9')
		screen_pos = m_device_name[0] - '0';

	char drm_name[15] = "/dev/dri/card_";
	drmModeRes *p_res;
	drmModeConnector *p_connector;

	int output_position = 0;
	for (int num = 0; !m_desktop_output && num < 10; num++)
	{
		drm_name[13] = '0' + num;
		m_drm_fd = open(drm_name, O_RDWR | O_CLOEXEC);

		if (m_drm_fd > 0)
		{
			drmVersion *version = drmGetVersion(m_drm_fd);
			//log_verbose("DRM/KMS: <%d> (init) version %d.%d.%d type %s\n", m_id, version->version_major, version->version_minor, version->version_patchlevel, version->name);
			drmFreeVersion(version);

			uint64_t check_dumb = 0;
			if (drmGetCap(m_drm_fd, DRM_CAP_DUMB_BUFFER, &check_dumb) < 0);
				//log_error("DRM/KMS: <%d> (init) [ERROR] ioctl DRM_CAP_DUMB_BUFFER\n", m_id);

			//if (!check_dumb)
				//log_error("DRM/KMS: <%d> (init) [ERROR] dumb buffer not supported\n", m_id);

			p_res = drmModeGetResources(m_drm_fd);

			for (int i = 0; i < p_res->count_connectors; i++)
			{
				p_connector = drmModeGetConnector(m_drm_fd, p_res->connectors[i]);
				if (p_connector)
				{
					char connector_name[32];
					snprintf(connector_name, 32, "%s%d", get_connector_name(p_connector->connector_type), p_connector->connector_type_id);
					//log_verbose("DRM/KMS: <%d> (init) card %d connector %d id %d name %s status %d - modes %d\n", m_id, num, i, p_connector->connector_id, connector_name, p_connector->connection, p_connector->count_modes);
					// detect desktop connector
					if (!m_desktop_output && p_connector->connection == DRM_MODE_CONNECTED)
					{
						if (!strcmp(m_device_name, "auto") || !strcmp(m_device_name, connector_name) || output_position == screen_pos)
						{
							m_desktop_output = p_connector->connector_id;
							m_card_id = num;
							//log_verbose("DRM/KMS: <%d> (init) card %d connector %d id %d name %s selected as primary output\n", m_id, num, i, m_desktop_output, connector_name);

							drmModeEncoder *p_encoder = drmModeGetEncoder(m_drm_fd, p_connector->encoder_id);

							if (p_encoder)
							{
								for (int e = 0; e < p_res->count_crtcs; e++)
								{
									mp_crtc_desktop = drmModeGetCrtc(m_drm_fd, p_res->crtcs[e]);

									if (mp_crtc_desktop->crtc_id == p_encoder->crtc_id)
									{
										//log_verbose("DRM/KMS: <%d> (init) desktop mode name %s crtc %d fb %d valid %d\n", m_id, mp_crtc_desktop->mode.name, mp_crtc_desktop->crtc_id, mp_crtc_desktop->buffer_id, mp_crtc_desktop->mode_valid);
										break;
									}
									drmModeFreeCrtc(mp_crtc_desktop);
								}
							}
							if (mp_crtc_desktop)
								drmModeFreeEncoder(p_encoder); //log_error("DRM/KMS: <%d> (init) [ERROR] no crtc found\n", m_id);
							
						}
						output_position++;
					}
					drmModeFreeConnector(p_connector);
				}
				//else
					//log_error("DRM/KMS: <%d> (init) [ERROR] card %d connector %d - %d\n", m_id, num, i, p_res->connectors[i]);
			}
			drmModeFreeResources(p_res);
			if (!m_desktop_output)
				close(m_drm_fd);
			else
			{
				if (drmIsMaster(m_drm_fd))
				{
					s_shared_fd[m_card_id] = m_drm_fd;
					s_shared_count[m_card_id] = 1;
					drmDropMaster(m_drm_fd);
				}
				else
				{
					if (s_shared_count[m_card_id] > 0)
					{
						close(m_drm_fd);
						m_drm_fd = s_shared_fd[m_card_id];
						s_shared_count[m_card_id]++;
					}
					else if (m_id == 1)
					{
						//log_verbose("DRM/KMS: <%d> (init) looking for the DRM master\n", m_id);
						int fd = drm_master_hook(m_drm_fd);
						if (fd)
						{
							close(m_drm_fd);
							m_drm_fd = fd;
							s_shared_fd[m_card_id] = m_drm_fd;
							// start at 2 to disable closing the fd
							s_shared_count[m_card_id] = 2;
						}
					}
				}

			}
		}

	}

	// Handle no screen detected case
	if (!m_desktop_output)
	{
		//log_error("DRM/KMS: <%d> (init) [ERROR] no screen detected\n", m_id);
		return false;
	}
	else
	{
	}

	return true;
}

int drm_master_hook(int last_fd)
{
	for (int fd = 4; fd < last_fd; fd++)
	{
		struct stat st;
		if (!fstat(fd, &st))
		{
			// in case of multiple video cards, it wouldd be better to compare dri number
			if (S_ISCHR(st.st_mode))
			{
				if (drmIsMaster(fd))
				{
					drmVersion *version_hook = drmGetVersion(m_drm_fd);
					//log_verbose("DRM/KMS: <%d> (init) DRM hook created version %d.%d.%d type %s\n", m_id, version_hook->version_major, version_hook->version_minor, version_hook->version_patchlevel, version_hook->name);
					drmFreeVersion(version_hook);
					return fd;
				}
			}
		}
	}
	return 0;
}

bool update_mode(modeline *mode)
{
	if (!mode)
		return false;

	if (!m_desktop_output)
	{
		//log_error("DRM/KMS: <%d> (update_mode) [ERROR] no screen detected\n", m_id);
		return false;
	}

	if (!delete_mode(mode))
	{
		//log_error("DRM/KMS: <%d> (update_mode) [ERROR] delete operation not successful", m_id);
		return false;
	}

	if (!add_mode(mode))
	{
		//log_error("DRM/KMS: <%d> (update_mode) [ERROR] add operation not successful", m_id);
		return false;
	}

	return true;
}

bool add_mode(modeline *mode)
{
	if (!mode)
		return false;

	// Handle no screen detected case
	if (!m_desktop_output)
	{
		//log_error("DRM/KMS: <%d> (add_mode) [ERROR] no screen detected\n", m_id);
		return false;
	}

	if (!mp_crtc_desktop)
	{
		//log_error("DRM/KMS: <%d> (add_mode) [ERROR] no desktop crtc\n", m_id);
		return false;
	}

	if (!mode)
		return false;

	return true;
}
/*
bool set_timing(modeline *mode)
{
	if (!mode)
		return false;

	// Handle no screen detected case
	if (!m_desktop_output)
	{
		//log_error("DRM/KMS: <%d> (set_timing) [ERROR] no screen detected\n", m_id);
		return false;
	}

	drmSetMaster(m_drm_fd);

	// Setup the DRM mode structure
	drmModeModeInfo dmode = {};

	// Create specific mode name
	snprintf(dmode.name, 32, "SR-%d_%dx%d@%.02f%s", m_id, mode->hactive, mode->vactive, mode->vfreq, mode->interlace ? "i" : "");
	dmode.clock       = mode->pclock / 1000;
	dmode.hdisplay    = mode->hactive;
	dmode.hsync_start = mode->hbegin;
	dmode.hsync_end   = mode->hend;
	dmode.htotal      = mode->htotal;
	dmode.vdisplay    = mode->vactive;
	dmode.vsync_start = mode->vbegin;
	dmode.vsync_end   = mode->vend;
	dmode.vtotal      = mode->vtotal;
	dmode.flags       = (mode->interlace ? DRM_MODE_FLAG_INTERLACE : 0) | (mode->doublescan ? DRM_MODE_FLAG_DBLSCAN : 0) | (mode->hsync ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC) | (mode->vsync ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC);

	dmode.hskew       = 0;
	dmode.vscan       = 0;

	dmode.vrefresh    = mode->refresh;	// Used only for human readable output

	dmode.type        = DRM_MODE_TYPE_USERDEF;	//DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	mode->type |= CUSTOM_VIDEO_TIMING_DRMKMS;

	if (mode->platform_data == 4815162342)
	{
		//log_verbose("DRM/KMS: <%d> (set_timing) <debug> restore desktop mode\n", m_id);
		drmModeSetCrtc(m_drm_fd, mp_crtc_desktop->crtc_id, mp_crtc_desktop->buffer_id, mp_crtc_desktop->x, mp_crtc_desktop->y, &m_desktop_output, 1, &mp_crtc_desktop->mode);
		if (m_dumb_handle)
		{
			int ret = ioctl(m_drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &m_dumb_handle);
			if (!ret)
				//log_verbose("DRM/KMS: <%d> (add_mode) [ERROR] ioctl DRM_IOCTL_MODE_DESTROY_DUMB %d\n", m_id, ret);
			   m_dumb_handle = 0;
		}
		if (m_framebuffer_id && m_framebuffer_id != mp_crtc_desktop->buffer_id)
		{
			if (!drmModeRmFB(m_drm_fd, m_framebuffer_id))
				//log_verbose("DRM/KMS: <%d> (add_mode) [ERROR] remove frame buffer\n", m_id);
			    m_framebuffer_id = 0;
		}
	}
	else
	{
		unsigned int old_dumb_handle = m_dumb_handle;

		drmModeFB *pframebuffer = drmModeGetFB(m_drm_fd, mp_crtc_desktop->buffer_id);
		//log_verbose("DRM/KMS: <%d> (add_mode) <debug> existing frame buffer id %d size %dx%d bpp %d\n", m_id, mp_crtc_desktop->buffer_id, pframebuffer->width, pframebuffer->height, pframebuffer->bpp);
		//drmModePlaneRes *pplanes = drmModeGetPlaneResources(m_drm_fd);
		//log_verbose("DRM/KMS: <%d> (add_mode) <debug> total planes %d\n", m_id, pplanes->count_planes);
		//drmModeFreePlaneResources(pplanes);

		unsigned int framebuffer_id = mp_crtc_desktop->buffer_id;

		//if (pframebuffer->width < dmode.hdisplay || pframebuffer->height < dmode.vdisplay)
		if (1)
		{
			//log_verbose("DRM/KMS: <%d> (add_mode) <debug> creating new frame buffer with size %dx%d\n", m_id, dmode.hdisplay, dmode.vdisplay);

			// create a new dumb fb (not driver specefic)
			drm_mode_create_dumb create_dumb = {};
			create_dumb.width = dmode.hdisplay;
			create_dumb.height = dmode.vdisplay;
			create_dumb.bpp = pframebuffer->bpp;

			int ret = ioctl(m_drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);
			//if (ret)
				//log_verbose("DRM/KMS: <%d> (add_mode) [ERROR] ioctl DRM_IOCTL_MODE_CREATE_DUMB %d\n", m_id, ret);

			if (drmModeAddFB(m_drm_fd, dmode.hdisplay, dmode.vdisplay, pframebuffer->depth, pframebuffer->bpp, create_dumb.pitch, create_dumb.handle, &framebuffer_id))
				//log_error("DRM/KMS: <%d> (add_mode) [ERROR] cannot add frame buffer\n", m_id);
			else
				m_dumb_handle = create_dumb.handle;

			drm_mode_map_dumb map_dumb = {};
			map_dumb.handle = create_dumb.handle;

			ret = drmIoctl(m_drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb);
			if (ret)
				log_verbose("DRM/KMS: <%d> (add_mode) [ERROR] ioctl DRM_IOCTL_MODE_MAP_DUMB %d\n", m_id, ret);

			void *map = mmap(0, create_dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, m_drm_fd, map_dumb.offset);
			if (map != MAP_FAILED)
			{
				// clear the frame buffer
				memset(map, 0, create_dumb.size);
			}
			
		}
	

		drmModeFreeFB(pframebuffer);

		pframebuffer = drmModeGetFB(m_drm_fd, framebuffer_id);
		//log_verbose("DRM/KMS: <%d> (add_mode) <debug> frame buffer id %d size %dx%d bpp %d\n", m_id, framebuffer_id, pframebuffer->width, pframebuffer->height, pframebuffer->bpp);
		drmModeFreeFB(pframebuffer);

		// set the mode on the crtc
		if (drmModeSetCrtc(m_drm_fd, mp_crtc_desktop->crtc_id, framebuffer_id, 0, 0, &m_desktop_output, 1, &dmode))
			//log_error("DRM/KMS: <%d> (add_mode) [ERROR] cannot attach the mode to the crtc %d frame buffer %d\n", m_id, mp_crtc_desktop->crtc_id, framebuffer_id);
		else
		{
			if (old_dumb_handle)
			{
				//log_verbose("DRM/KMS: <%d> (add_mode) <debug> remove old dumb %d\n", m_id, old_dumb_handle);
				int ret = ioctl(m_drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &old_dumb_handle);
				if (ret)
					log_verbose("DRM/KMS: <%d> (add_mode) [ERROR] ioctl DRM_IOCTL_MODE_DESTROY_DUMB %d\n", m_id, ret);
				old_dumb_handle = 0;
			}
			if (m_framebuffer_id && framebuffer_id != mp_crtc_desktop->buffer_id)
			{
				//log_verbose("DRM/KMS: <%d> (add_mode) <debug> remove old frame buffer %d\n", m_id, m_framebuffer_id);
				drmModeRmFB(m_drm_fd, m_framebuffer_id);
				m_framebuffer_id = 0;
			}
			m_framebuffer_id = framebuffer_id;
		}
	}
	drmDropMaster(m_drm_fd);

	return true;
}
*/

bool delete_mode(modeline *mode)
{
	if (!mode)
		return false;

	// Handle no screen detected case
	if (!m_desktop_output)
	{
		//log_error("DRM/KMS: <%d> (delete_mode) [ERROR] no screen detected\n", m_id);
		return false;
	}

	return true;
}
*/
bool get_timing(modeline *mode)
{
	// Handle no screen detected case
	if (!m_desktop_output)
	{
		//log_error("DRM/KMS: <%d> (get_timing) [ERROR] no screen detected\n", m_id);
		return false;
	}

	// INFO: not used vrefresh, hskew, vscan
	drmModeRes *p_res = drmModeGetResources(m_drm_fd);

	for (int i = 0; i < p_res->count_connectors; i++)
	{
		drmModeConnector *p_connector = drmModeGetConnector(m_drm_fd, p_res->connectors[i]);

		// Cycle through the modelines and report them back to the display manager
		if (p_connector && m_desktop_output == p_connector->connector_id)
		{
			if (m_video_modes_position < p_connector->count_modes)
			{
				drmModeModeInfo *pdmode = &p_connector->modes[m_video_modes_position++];

				// Use mode position as index
				mode->platform_data = m_video_modes_position;

				mode->pclock        = pdmode->clock * 1000;
				mode->hactive       = pdmode->hdisplay;
				mode->hbegin        = pdmode->hsync_start;
				mode->hend          = pdmode->hsync_end;
				mode->htotal        = pdmode->htotal;
				mode->vactive       = pdmode->vdisplay;
				mode->vbegin        = pdmode->vsync_start;
				mode->vend          = pdmode->vsync_end;
				mode->vtotal        = pdmode->vtotal;
				mode->interlace     = (pdmode->flags & DRM_MODE_FLAG_INTERLACE) ? 1 : 0;
				mode->doublescan    = (pdmode->flags & DRM_MODE_FLAG_DBLSCAN) ? 1 : 0;
				mode->hsync         = (pdmode->flags & DRM_MODE_FLAG_PHSYNC) ? 1 : 0;
				mode->vsync         = (pdmode->flags & DRM_MODE_FLAG_PVSYNC) ? 1 : 0;

				mode->hfreq         = mode->pclock / mode->htotal;
				mode->vfreq         = mode->hfreq / mode->vtotal * (mode->interlace ? 2 : 1);
				mode->refresh       = mode->vfreq;

				mode->width         = pdmode->hdisplay;
				mode->height        = pdmode->vdisplay;

				// Add the rotation flag from the plane (DRM_MODE_ROTATE_xxx)
				// TODO: mode->type |= MODE_ROTATED;

				mode->type |= CUSTOM_VIDEO_TIMING_DRMKMS;

				if (strncmp(pdmode->name, "SR-", 3) == 0)
					//log_verbose("DRM/KMS: <%d> (get_timing) [WARNING] modeline %s detected\n", m_id, pdmode->name);
				else if (!strcmp(pdmode->name, mp_crtc_desktop->mode.name) && pdmode->clock == mp_crtc_desktop->mode.clock && pdmode->vrefresh == mp_crtc_desktop->mode.vrefresh)
				{
					// Add the desktop flag to desktop modeline
					//log_verbose("DRM/KMS: <%d> (get_timing) desktop mode name %s refresh %d found\n", m_id, mp_crtc_desktop->mode.name, mp_crtc_desktop->mode.vrefresh);
					mode->type |= MODE_DESKTOP;
					mode->platform_data = 4815162342;
				}
			}
			else
			{
				// Inititalise the position for the modeline list
				m_video_modes_position = 0;
			}
		}
		drmModeFreeConnector(p_connector);
	}
	drmModeFreeResources(p_res);

	return true;
}
*/


#endif
