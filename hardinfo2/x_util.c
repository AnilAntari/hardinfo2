/*
 *    HardInfo - Displays System Information
 *    Copyright (C) 2003-2017 L. A. F. Pereira <l@tia.mat.br>
 *    This file
 *    Copyright (C) 2018 Burt P. <pburt0@gmail.com>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, version 2 or later.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "hardinfo.h"
#include "x_util.h"
#include <X11/Xlib.h>

/* wayland stuff lives here for now */

wl_info *get_walyand_info() {
    wl_info *s = malloc(sizeof(wl_info));
    memset(s, 0, sizeof(wl_info));
    s->xdg_session_type = g_strdup( getenv("XDG_SESSION_TYPE") );
    if (s->xdg_session_type == NULL)
        s->xdg_session_type = strdup("x11");
    s->display_name = g_strdup( getenv("WAYLAND_DISPLAY") );
    return s;
}

void wl_free(wl_info *s) {
    if (s) {
        free(s->xdg_session_type);
        free(s->display_name);
        free(s);
    }
}

/* get x information from xrandr, xdpyinfo, glxinfo and vulkaninfo*/

static char *simple_line_value(char *line, const char *prefix) {
    if (g_str_has_prefix(g_strstrip(line), prefix)) {
        line += strlen(prefix) + 1;
        return g_strstrip(line);
    } else
        return NULL;
}

gboolean fill_vk_info(vk_info *vk) {
    gboolean spawned;
    gchar *out, *err, *p, *l, *next_nl;
    int gpu=0,old=0,found=0;
    gchar *vk_cmd = g_strdup("vulkaninfo --summary");

#define VK_MATCH_LINE(prefix_str, struct_member) \
    if (l = simple_line_value(p, prefix_str)) { vk->struct_member = g_strdup(strreplace(g_strdup(l),"= ","")); goto vk_next_line; }

  while(old<=1){
    spawned = hardinfo_spawn_command_line_sync(vk_cmd, &out, &err, NULL, NULL);
    g_free(vk_cmd);
    if (spawned) {
        p = out;
        while(next_nl = strchr(p, '\n')) {
            strend(p, '\n');
            g_strstrip(p);
            VK_MATCH_LINE("Vulkan Instance Version", vk_instVer);
            if(strstr(p,"GPU")==p) {found=1;sscanf(p,"GPU%d:",&gpu);}
	    if((gpu>=0) && (gpu<VK_MAX_GPU)){
                VK_MATCH_LINE("apiVersion", vk_apiVer[gpu]);
                VK_MATCH_LINE("driverVersion", vk_drvVer[gpu]);
                VK_MATCH_LINE("vendorID", vk_vendorId[gpu]);
                VK_MATCH_LINE("deviceType", vk_devType[gpu]);
                VK_MATCH_LINE("deviceName", vk_devName[gpu]);
                VK_MATCH_LINE("driverName", vk_drvName[gpu]);
                VK_MATCH_LINE("driverInfo", vk_drvInfo[gpu]);
                VK_MATCH_LINE("conformanceVersion", vk_conformVer[gpu]);
	    }

            vk_next_line:
                p = next_nl + 1;
        }
        g_free(out);
        g_free(err);
	int i=0; do {if(vk->vk_vendorId[i]){
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x10000","Khronos");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x10001","Viv");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x10002","VSI");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x10003","Kazan");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x10004","CodePlay");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x10005","Mesa");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x10006","POCL");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x10007","MobileEye");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x0","Unknown");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x1002","AMD");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x106b","Apple");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x13b5","ARM");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x14e4","BroadCom");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x1ae0","Google");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x8086","Intel");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x10de","NVidia");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x1010","PowerVR");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x5143","Qualcom");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x144d","Samsung");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x15ad","VMWare");
	    vk->vk_vendorId[i]=strreplace(vk->vk_vendorId[i],"0x9999","Vivante");
	  }i++;} while(i<VK_MAX_GPU);
        if(found) return TRUE;
    }
    //old vulkaninfo does not have summary
    //new full info is too hard to parse -> 2 steps
    vk_cmd = g_strdup("vulkaninfo");
    old++;
  }
  return FALSE;
}

vk_info *vk_create() {
    vk_info *s = malloc(sizeof(vk_info));
    if(!s) return NULL;
    memset(s, 0, sizeof(vk_info));
    return s;
}
void vk_free(vk_info *s) {
    if (s) {
        free(s->vk_instVer);
	//
	int i=0;do{
            free(s->vk_apiVer[i]);
            free(s->vk_drvVer[i]);
            free(s->vk_vendorId[i]);
            free(s->vk_devType[i]);
            free(s->vk_devName[i]);
            free(s->vk_drvName[i]);
            free(s->vk_drvInfo[i]);
            free(s->vk_conformVer[i]);
	    i++;
	}while(i<VK_MAX_GPU);
        free(s);
    }
}


gboolean fill_glx_info(glx_info *glx) {
    gboolean spawned;
    gchar *out, *err, *p, *l, *next_nl;
    gchar *glx_cmd = g_strdup("glxinfo");

#define GLX_MATCH_LINE(prefix_str, struct_member) \
    if (l = simple_line_value(p, prefix_str)) { glx->struct_member = g_strdup(l); goto glx_next_line; }

    spawned = hardinfo_spawn_command_line_sync(glx_cmd, &out, &err, NULL, NULL);
    g_free(glx_cmd);
    if (spawned) {
        p = out;
        while(next_nl = strchr(p, '\n')) {
            strend(p, '\n');
            g_strstrip(p);
            GLX_MATCH_LINE("GLX version", glx_version);
            GLX_MATCH_LINE("OpenGL vendor string", ogl_vendor);
            GLX_MATCH_LINE("OpenGL renderer string", ogl_renderer);
            GLX_MATCH_LINE("OpenGL core profile version string", ogl_core_version);
            GLX_MATCH_LINE("OpenGL core profile shading language version string", ogl_core_sl_version);
            GLX_MATCH_LINE("OpenGL version string", ogl_version);
            GLX_MATCH_LINE("OpenGL shading language version string", ogl_sl_version);
            GLX_MATCH_LINE("OpenGL ES profile version string", ogles_version);
            GLX_MATCH_LINE("OpenGL ES profile shading language version string", ogles_sl_version);

            if (l = simple_line_value(p, "direct rendering")) {
                if (strstr(p, "Yes"))
                    glx->direct_rendering = 1;
                goto glx_next_line;
            }

            glx_next_line:
                p = next_nl + 1;
        }
        g_free(out);
        g_free(err);
        return TRUE;
    }
    return FALSE;
}

glx_info *glx_create() {
    glx_info *s = malloc(sizeof(glx_info));
    if(!s) return NULL;
    memset(s, 0, sizeof(glx_info));
    return s;
}

void glx_free(glx_info *s) {
    if (s) {
        free(s->glx_version);
        free(s->ogl_vendor);
        free(s->ogl_renderer);
        free(s->ogl_core_version);
        free(s->ogl_core_sl_version);
        free(s->ogl_version);
        free(s->ogl_sl_version);
        free(s->ogles_version);
        free(s->ogles_sl_version);
        free(s);
    }
}

gboolean fill_xinfo(xinfo *xi) {
    gboolean spawned;
    gchar *out, *err, *p, *l, *next_nl;
    gchar *xi_cmd = g_strdup("xdpyinfo");

#define XI_MATCH_LINE(prefix_str, struct_member) \
    if (l = simple_line_value(p, prefix_str)) { xi->struct_member = g_strdup(l); goto xi_next_line; }

    spawned = hardinfo_spawn_command_line_sync(xi_cmd, &out, &err, NULL, NULL);
    g_free(xi_cmd);
    if (spawned) {
        p = out;
        while(next_nl = strchr(p, '\n')) {
            strend(p, '\n');
            g_strstrip(p);
            XI_MATCH_LINE("name of display", display_name);
            XI_MATCH_LINE("vendor string", vendor);
            XI_MATCH_LINE("vendor release number", release_number);
            if(!xi->version) XI_MATCH_LINE("XFree86 version", version);
            if(!xi->version) XI_MATCH_LINE("X.Org version", version);

            xi_next_line:
                p = next_nl + 1;
        }
        g_free(out);
        g_free(err);
        return TRUE;
    }
    return FALSE;
}

gboolean fill_xrr_info(xrr_info *xrr) {
    gboolean spawned;
    gchar *out, *err, *p, *next_nl;
    gchar *xrr_cmd = g_strdup("xrandr --prop");
    int ec;

    x_screen ts;
    x_output to;
    char output_id[64];
    char status[128];
    char alist[512];

    memset(&ts, 0, sizeof(x_screen));
    memset(&to, 0, sizeof(x_output));
    memset(output_id, 0, sizeof(output_id));
    memset(status, 0, sizeof(status));
    memset(alist, 0, sizeof(alist));

    spawned = hardinfo_spawn_command_line_sync(xrr_cmd,
            &out, &err, NULL, NULL);
    g_free(xrr_cmd);
    if (spawned) {
        p = out;
        while(next_nl = strchr(p, '\n')) {
            strend(p, '\n');
            g_strstrip(p);

            ec = sscanf(p, "Screen %d: minimum %d x %d, current %d x %d, maximum %d x %d",
                &ts.number,
                &ts.min_px_width, &ts.min_px_height,
                &ts.px_width, &ts.px_height,
                &ts.max_px_width, &ts.max_px_height);
            if (ec == 7) {
                /* new screen */
                xrr->screen_count++;
                if (xrr->screens == NULL)
                    xrr->screens = malloc(xrr->screen_count * sizeof(x_screen));
                else
                    xrr->screens = realloc(xrr->screens, xrr->screen_count * sizeof(x_screen));
                memcpy(&xrr->screens[xrr->screen_count-1], &ts, sizeof(x_screen));
                memset(&ts, 0, sizeof(x_screen)); /* clear the temp */
                goto xrr_next_line;
            }

            /* looking for:
             * <output_id> (connected|disconnected|unknown connection) (primary|?) <%dx%d+%d+%d> (attribute_list) mm x mm
             */
            ec = sscanf(p, "%63s %127[^(](%511[^)]", output_id, status, alist);
            if (ec == 3) {
                int is_output = 0, found_rect = 0, n = 0;
                gchar **ot = g_strsplit(status, " ", 0);

                /* find rect */
                while (ot[n] != NULL) {
                    ec = sscanf(ot[n], "%dx%d+%d+%d", &to.px_width, &to.px_height, &to.px_offset_x, &to.px_offset_y);
                    if (ec == 4) {
                        found_rect = 1;
                        break;
                    }
                    n++;
                }
                if (found_rect)
                    to.screen = xrr->screen_count - 1;
                else
                    to.screen = -1;

                if (strcmp("disconnected", ot[0]) == 0) {
                    is_output = 1;
                    to.connected = 0;
                } else
                if (strcmp("unknown", ot[0]) == 0 && strcmp("connection", ot[1]) == 0) {
                    is_output = 1;
                    to.connected = -1;
                } else
                if (strcmp("connected", ot[0]) == 0) {
                    is_output = 1;
                    to.connected = 1;
                }
                g_strfreev(ot);
                if (is_output) {
		  strncpy(to.name, output_id, 64);
                    xrr->output_count++;
                    if (xrr->outputs == NULL)
                        xrr->outputs = malloc(xrr->output_count * sizeof(x_output));
                    else
                        xrr->outputs = realloc(xrr->outputs, xrr->output_count * sizeof(x_output));
                    memcpy(&xrr->outputs[xrr->output_count-1], &to, sizeof(x_output));
                    memset(&to, 0, sizeof(x_output)); /* clear the temp */
                    goto xrr_next_line;
                }
            }

            xrr_next_line:
                p = next_nl + 1;
        }
        g_free(out);
        g_free(err);
        return TRUE;
    }
    return FALSE;
}

gboolean fill_basic_xlib(xinfo *xi) {
    Display *display;
    int s, w, h, rn;

    display = XOpenDisplay(NULL);
    if (display) {
        if (!xi->display_name)
	    xi->display_name = g_strdup(XDisplayName(NULL));

        if (!xi->vendor)
	    xi->vendor = g_strdup(XServerVendor(display));

        if (!xi->release_number) {
            rn = XVendorRelease(display);
            xi->release_number = g_strdup_printf("%d", rn);
        }

        if (!xi->xrr || !xi->xrr->screen_count) {
            s = DefaultScreen(display);
            w = XDisplayWidth(display, s);
            h = XDisplayHeight(display, s);

            /* create at least one screen */
            x_screen ts;
            memset(&ts, 0, sizeof(x_screen));
            ts.number = s;
            ts.px_width = w;
            ts.px_height = h;

	    if(!xi->xrr) {
	      xi->xrr=malloc(sizeof(xrr_info));
	      if(xi->xrr) memset(xi->xrr,0,sizeof(xrr_info));
	    }
            if(xi->xrr) {
                xi->xrr->screen_count++;
                if (xi->xrr->screens == NULL)
                   xi->xrr->screens = malloc(xi->xrr->screen_count * sizeof(x_screen));
                else
                   xi->xrr->screens = realloc(xi->xrr->screens, xi->xrr->screen_count * sizeof(x_screen));
                memcpy(&xi->xrr->screens[xi->xrr->screen_count-1], &ts, sizeof(x_screen));
	    }
        }
	XCloseDisplay(display);
        return TRUE;
    }
    return FALSE;
}

xrr_info *xrr_create() {
    xrr_info *xrr = malloc(sizeof(xrr_info));
    if(!xrr) return NULL;
    memset(xrr, 0, sizeof(xrr_info));
    return xrr;
}

void xrr_free(xrr_info *xrr) {
    if (xrr) {
        free(xrr->screens);
        free(xrr->outputs);
        free(xrr->providers);
        free(xrr->monitors);
        free(xrr);
    }
}

xinfo *xinfo_get_info() {
    int fail=0;
    xinfo *xi = malloc(sizeof(xinfo));
    if(!xi) return NULL;
    memset(xi, 0, sizeof(xinfo));

    xi->glx = glx_create();
    if(!xi->glx) {free(xi);return NULL;}

    xi->xrr = xrr_create();
    if(!xi->xrr) {free(xi->glx);free(xi);return NULL;}

    xi->vk = vk_create();
    if(!xi->vk) {free(xi->glx);free(xi->xrr);free(xi);return NULL;}

    if(!fill_xinfo(xi)) fail++;
    if(fill_xrr_info(xi->xrr)) fail++;

    /* as fallback, try xlib directly */
    if ( fail && !fill_basic_xlib(xi) ) xi->nox = 1;

    fill_glx_info(xi->glx);
    fill_vk_info(xi->vk);

    return xi;
}

void xinfo_free(xinfo *xi) {
    if (xi) {
        free(xi->display_name);
        free(xi->vendor);
        free(xi->version);
        free(xi->release_number);
        xrr_free(xi->xrr);
        glx_free(xi->glx);
	vk_free(xi->vk);
        free(xi);
    }
}
