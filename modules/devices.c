/*
 *    Hardinfo2 - System information and benchmark
 *    Copyright (C) 2003-2007 L. A. F. Pereira <l@tia.mat.br>
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

#ifndef __USE_XOPEN
#define __USE_XOPEN
#endif /* __USE_XOPEN */

#include <gtk/gtk.h>
#include <config.h>
#include <string.h>

#include <hardinfo.h>
#include <shell.h>
#include <iconcache.h>
#include <syncmanager.h>

#include <expr.h>
#include <socket.h>

#include "devices.h"
#include "dt_util.h"
#include "udisks2_util.h"
#include "storage_util.h"
#include "pci_util.h"
#include <json-glib/json-glib.h>
#include "cpu_util.h"

gchar *callback_processors();
gchar *callback_gpu();
gchar *callback_monitors();
gchar *callback_battery();
gchar *callback_pci();
gchar *callback_sensors();
gchar *callback_printers();
gchar *callback_storage();
gchar *callback_input();
gchar *callback_usb();
gchar *callback_dmi();
gchar *callback_dmi_mem();
gchar *callback_firmware();
gchar *callback_dtree();
gchar *callback_device_resources();

void scan_processors(gboolean reload);
void scan_gpu(gboolean reload);
void scan_monitors(gboolean reload);
void scan_battery(gboolean reload);
void scan_pci(gboolean reload);
void scan_sensors(gboolean reload);
void scan_printers(gboolean reload);
void scan_storage(gboolean reload);
void scan_input(gboolean reload);
void scan_usb(gboolean reload);
void scan_dmi(gboolean reload);
void scan_dmi_mem(gboolean reload);
void scan_firmware(gboolean reload);
void scan_dtree(gboolean reload);
void scan_device_resources(gboolean reload);

gboolean root_required_for_resources(void);
gboolean spd_decode_show_hinote(const char**);

gchar *hi_more_info(gchar *entry);

enum {
    ENTRY_DTREE,
    ENTRY_DMI,
    ENTRY_PROCESSOR,
    ENTRY_GPU,
    ENTRY_MONITORS,
    ENTRY_DMI_MEM,
    ENTRY_PCI,
    ENTRY_USB,
    ENTRY_FW,
    ENTRY_PRINTERS,
    ENTRY_BATTERY,
    ENTRY_SENSORS,
    ENTRY_INPUT,
    ENTRY_STORAGE,
    ENTRY_RESOURCES
};

static ModuleEntry entries[] = {
    [ENTRY_PROCESSOR] = {N_("Processor"), "processor.svg", callback_processors, scan_processors, MODULE_FLAG_NONE},
    [ENTRY_GPU] = {N_("Graphics Processors"), "gpu.svg", callback_gpu, scan_gpu, MODULE_FLAG_NONE},
    [ENTRY_MONITORS] = {N_("Monitors"), "monitor.svg", callback_monitors, scan_monitors, MODULE_FLAG_NONE},
    [ENTRY_PCI] = {N_("PCI Devices"), "pci.svg", callback_pci, scan_pci, MODULE_FLAG_NONE},
    [ENTRY_USB] = {N_("USB Devices"), "usb.svg", callback_usb, scan_usb, MODULE_FLAG_NONE},
    [ENTRY_FW] = {N_("Firmware"), "firmware.svg", callback_firmware, scan_firmware, MODULE_FLAG_NONE},
    [ENTRY_PRINTERS] = {N_("Printers"), "printer.svg", callback_printers, scan_printers, MODULE_FLAG_NONE},
    [ENTRY_BATTERY] = {N_("Battery"), "battery.svg", callback_battery, scan_battery, MODULE_FLAG_NONE},
    [ENTRY_SENSORS] = {N_("Sensors"), "therm.svg", callback_sensors, scan_sensors, MODULE_FLAG_NONE},
    [ENTRY_INPUT] = {N_("Input Devices"), "inputdevices.svg", callback_input, scan_input, MODULE_FLAG_NONE},
    [ENTRY_STORAGE] = {N_("Storage"), "hdd.svg", callback_storage, scan_storage, MODULE_FLAG_NONE},
    [ENTRY_DMI] = {N_("System DMI"), "dmi.svg", callback_dmi, scan_dmi, MODULE_FLAG_NONE},
    [ENTRY_DMI_MEM] = {N_("Memory Devices"), "memory.svg", callback_dmi_mem, scan_dmi_mem, MODULE_FLAG_NONE},
#if defined(ARCH_x86) || defined(ARCH_x86_64)
    [ENTRY_DTREE] = {N_("Device Tree"), "devicetree.svg", callback_dtree, scan_dtree, MODULE_FLAG_HIDE},
#else
    [ENTRY_DTREE] = {N_("Device Tree"), "devicetree.svg", callback_dtree, scan_dtree, MODULE_FLAG_NONE},
#endif	/* x86 or x86_64 */
    [ENTRY_RESOURCES] = {N_("Resources"), "resources.svg", callback_device_resources, scan_device_resources, MODULE_FLAG_NONE},
    { NULL }
};

static GSList *processors = NULL;
gchar *printer_list = NULL;
gchar *printer_icons = NULL;
gchar *pci_list = NULL;
gchar *input_list = NULL;
gboolean storage_no_nvme = FALSE;
gchar *storage_list = NULL;
gchar *battery_list = NULL;
gchar *powerstate=NULL;
gchar *gpuname=NULL;

/* in dmi_memory.c */
gchar *memory_devices_get_info();
gchar *memory_devices_get_system_memory_types_str();
gchar *memory_devices_get_system_memory_str();
gboolean memory_devices_hinote(const char **msg);
gchar *memory_devices_info = NULL;
gchar *memory_devices_desc = NULL;

/* in firmware.c */
gchar *firmware_get_info();
gboolean firmware_hinote(const char **msg);
gchar *firmware_info = NULL;

/* in monitors.c */
gchar *monitors_get_info();
gboolean monitors_hinote(const char **msg);
gchar *monitors_info = NULL;

#include <vendor.h>

extern gchar *gpu_summary;
const gchar *get_gpu_summary() {
    if (gpu_summary == NULL)
        scan_gpu(FALSE);
    return g_strdup(gpu_summary);
}

static gint proc_cmp_model_name(Processor *a, Processor *b) {
    return g_strcmp0(a->model_name, b->model_name);
}

static gint proc_cmp_max_freq(Processor *a, Processor *b) {
    if (a->cpu_mhz == b->cpu_mhz)
        return 0;
    if (a->cpu_mhz > b->cpu_mhz)
        return -1;
    return 1;
}

gchar *processor_describe_default(GSList * processors)
{
    int packs, cores, threads, nodes;
    const gchar  *packs_fmt, *cores_fmt, *threads_fmt, *nodes_fmt;
    gchar *ret, *full_fmt;

    cpu_procs_cores_threads_nodes(&packs, &cores, &threads, &nodes);

    if (cores > 0) {
        packs_fmt = ngettext("%d physical processor", "%d physical processors", packs);
        cores_fmt = ngettext("%d core", "%d cores", cores);
        threads_fmt = ngettext("%d thread", "%d threads", threads);
        if (nodes > 1) {
            nodes_fmt = ngettext("%d NUMA node", "%d NUMA nodes", nodes);
            full_fmt = g_strdup_printf("%s; %s across %s; %s", packs_fmt, cores_fmt, nodes_fmt, threads_fmt);
            ret = g_strdup_printf(full_fmt, packs, cores, nodes, threads);
        } else {
	    full_fmt = g_strdup_printf("%s; %s; %s", packs_fmt, cores_fmt, threads_fmt);
            ret = g_strdup_printf(full_fmt, packs, cores, threads);
        }
        g_free(full_fmt);
        return ret;
    } else { //fallback to old method
        return processor_describe_by_counting_names(processors);
    }
}

gchar *processor_name_default(GSList * processors)
{
    gchar *ret = g_strdup("");
    GSList *tmp, *l;
    Processor *p;
    gchar *cur_str = NULL;
    gint cur_count = 0;

    tmp = g_slist_copy(processors);
    tmp = g_slist_sort(tmp, (GCompareFunc)proc_cmp_model_name);

    for (l = tmp; l; l = l->next) {
        p = (Processor*)l->data;
        if (cur_str == NULL) {
            cur_str = p->model_name;
            cur_count = 1;
        } else {
            if(g_strcmp0(cur_str, p->model_name)) {
                ret = h_strdup_cprintf("%s%s", ret, strlen(ret) ? "; " : "", cur_str);
                cur_str = p->model_name;
                cur_count = 1;
            } else {
                cur_count++;
            }
        }
    }
    ret = h_strdup_cprintf("%s%s", ret, strlen(ret) ? "; " : "", cur_str);
    g_slist_free(tmp);
    return ret;
}

/* TODO: prefix counts are threads when they should be cores. */
gchar *processor_describe_by_counting_names(GSList * processors)
{
    gchar *ret = g_strdup("");
    GSList *tmp, *l;
    Processor *p;
    gchar *cur_str = NULL;
    gint cur_count = 0;

    tmp = g_slist_copy(processors);
    tmp = g_slist_sort(tmp, (GCompareFunc)proc_cmp_model_name);

    for (l = tmp; l; l = l->next) {
        p = (Processor*)l->data;
        if (cur_str == NULL) {
            cur_str = p->model_name;
            cur_count = 1;
        } else {
            if(g_strcmp0(cur_str, p->model_name)) {
                ret = h_strdup_cprintf("%s%dx %s", ret, strlen(ret) ? " + " : "", cur_count, cur_str);
                cur_str = p->model_name;
                cur_count = 1;
            } else {
                cur_count++;
            }
        }
    }
    ret = h_strdup_cprintf("%s%dx %s", ret, strlen(ret) ? " + " : "", cur_count, cur_str);
    g_slist_free(tmp);
    return ret;
}

//currently only 64bit x86 has ld caps
#ifdef ARCH_x86
#define PTR_BITS ((unsigned int)sizeof(void*) * 8)
#else
#define PTR_BITS 32
#endif
gchar *ldlinux_hwcaps() {
    gboolean spawned;
    gchar *cmd_line,*out=NULL,*err=NULL,*supported=g_strdup("");

    if(PTR_BITS==64){//64bit
        cmd_line=g_strdup("sh -c 'LC_ALL=C /usr/lib64/ld-linux-x86-64.so.2 --help'");
	spawned = g_spawn_command_line_sync(cmd_line, &out, &err, NULL, NULL);
	g_free(cmd_line);
	if (!spawned || strlen(out)<100) {
	   if(out) {g_free(out);out=NULL;}
	   if(err) {g_free(err);err=NULL;}
	   cmd_line=g_strdup("sh -c 'LC_ALL=C /lib64/ld-linux-x86-64.so.2 --help'");
	   spawned = g_spawn_command_line_sync(cmd_line, &out, &err, NULL, NULL);
	   g_free(cmd_line);
	}
	if (spawned && strlen(out)>=100) {
	    if(strstr(out,"x86-64-v1 (sup")) supported=g_strconcat(supported," x86-64-V1 ",NULL);
	    if(strstr(out,"x86-64-v2 (sup")) supported=g_strconcat(supported," x86-64-V2 ",NULL);
	    if(strstr(out,"x86-64-v3 (sup")) supported=g_strconcat(supported," x86-64-V3 ",NULL);
	    if(strstr(out,"x86-64-v4 (sup")) supported=g_strconcat(supported," x86-64-V4 ",NULL);
	    if(strstr(out,"x86-64-v5 (sup")) supported=g_strconcat(supported," x86-64-V5 ",NULL);//future
	    if(strlen(supported)<1) supported=g_strconcat(supported," x86-64-V1 ",NULL);
	} else {
	    supported=g_strconcat(supported," x86-64-V1 ",NULL);
	}
    } else {//32bit and others
        cmd_line=g_strdup("sh -c 'LC_ALL=C uname -m'");
	spawned = g_spawn_command_line_sync(cmd_line, &out, &err, NULL, NULL);
	g_free(cmd_line);
	if (spawned && strlen(out)>=1) {
	    supported=g_strconcat(supported, " ",out," ", NULL);
	}else{
	    supported=g_strconcat(supported, " ",HARDINFO2_ARCH," ", NULL);
	}
    }
    if(out) g_free(out);
    if(err) g_free(err);

    if(strlen(supported)<1) {
        g_free(supported);
        supported=g_strdup("(None)");
    }

    return supported;
}

gchar *ldlinux_hwcaps_info() {
    gchar *supported=ldlinux_hwcaps();

    gchar *ret = g_strdup_printf("[%s]\n"
			  "HWCAPS=  %s\n",
			  _("Distro and CPU Supported Profiles"),
			  supported
			  );
    g_free(supported);

    return ret;
}


gchar *get_processor_name(void)
{
    scan_processors(FALSE);
    return processor_name(processors);
}

gchar *get_processor_desc(void)
{
    scan_processors(FALSE);
    return processor_describe(processors);
}

gchar *get_processor_name_and_desc(void)
{
    scan_processors(FALSE);
    gchar* name = processor_name(processors);
    gchar* desc = processor_describe(processors);
    gchar* nd = g_strdup_printf("%s\n%s", name, desc);
    g_free(name);
    g_free(desc);
    return nd;
}

gchar *get_storage_devices_simple(void)
{
    scan_storage(FALSE);

    struct Info *info = info_unflatten(storage_list);
    if (!info) {
        return g_strdup("");
    }

    guint i, fi;
    struct InfoGroup *group;
    struct InfoField *field;
    gchar *storage_devs = NULL, *tmp;

    GRegex *regex;
    regex = g_regex_new ("<.*?>", 0, 0, NULL);
    for (i = 0; i < info->groups->len; i++) {
        group = &g_array_index(info->groups, struct InfoGroup, info->groups->len - 1);
        if (!group)
            continue;

        info_group_strip_extra(group);
        for (fi = 0; fi < group->fields->len; fi++) {
            field = &g_array_index(group->fields, struct InfoField, fi);
            if (!field->value)
                continue;

            tmp = g_regex_replace(regex, field->value, -1, 0, "", 0, NULL); // remove html tags
	    tmp=strreplace(tmp,"  "," ");
            storage_devs = h_strdup_cprintf("%s\n", storage_devs, g_strstrip(tmp));
            g_free(tmp);
        }
    }
    g_regex_unref(regex);
    g_free(info);

    return storage_devs;
}

gchar *get_storage_home_models(void)
{
    scan_storage(FALSE);

    if (!storage_list) return g_strdup("");

    gchar *p,*np,*tmp,*p2;
    GRegex *regex;
    gchar *homepath=NULL,*out=NULL,*err=NULL;
    gboolean spawned;
    const char cmd_line[] = "sh -c 'cd ~;df --output=source . |tail -1'";
    const char cmd_line1disk[] = "sh -c 'lsblk -l |grep disk|grep -v zram'";
    char cmd_lineblk[100];

    //lookup home disk by df - only works on newer machines
    spawned = g_spawn_command_line_sync(cmd_line, &out, &err, NULL, NULL);
    if(spawned && out){
        if(strstr(out,"/dev/") && !strstr(out,"mapper") && !strstr(out,"/dev/root") ) homepath=strdup(out+5);
	if(strstr(out,"mapper")) {
	    p=strstr(out,"\n");
	    *p=0;
	    sprintf(cmd_lineblk,"sh -c 'lsblk -l -s %s|tail -1'",out);
	    g_free(out);
	    g_free(err);
            spawned = g_spawn_command_line_sync(cmd_lineblk, &out, &err, NULL, NULL);
	    if(spawned && out){
	        p=strstr(out," ")+1;//note: field 4 is size
	        *p=0;
	        homepath=g_strdup(out);
	    }
	}
    }
    g_free(out);
    g_free(err);

    if(!homepath) {  //simple systems - only 1 disk
        spawned = g_spawn_command_line_sync(cmd_line1disk, &out, &err, NULL, NULL);
        if(spawned && out){
	    if(strstr(out,"disk") && (strstr(out,"\n")==(out+strlen(out)-1)) ) {
	        p=strstr(out," ")+1;//note: field 4 is size
		*p=0;
                homepath=strdup(out);
	    }
        }
        g_free(out);
        g_free(err);
    }
    if(!homepath) return g_strdup("NoHomePath");
    homepath[strlen(homepath)-1]=0;
    while(homepath[strlen(homepath)-1]>='0' && homepath[strlen(homepath)-1]<='9') homepath[strlen(homepath)-1]=0;
    if( !strstr(homepath,"sdp") && !strstr(homepath,"vdp") && (homepath[strlen(homepath)-1]=='p') ) homepath[strlen(homepath)-1]=0;
    //printf("Homepath=%s (%u)\n",homepath,(unsigned int)strlen(homepath));

    regex = g_regex_new ("<.*?>", 0, 0, NULL);
    p2=p=g_strdup(storage_list);
    while ( (np=strstr(p,"\n")) ){
      *np=0;
      //printf("name=%s\n",p);
      if(strstr(p,homepath)) {
	  tmp = g_regex_replace(regex, strstr(p,"=")+1, -1, 0, "", 0, NULL); // remove html tags
	  tmp = g_strstrip(strreplace(tmp,"  "," "));
	  tmp = g_strstrip(strreplace(tmp,"| ","|"));
	  p=strstr(tmp,"|");
	  *p=0;
	  p++;
          g_regex_unref(regex);
          g_free(homepath);
	  p=g_strdup(p);
	  g_free(p2);
          //printf("Homepathmodel=%s\n",g_strdup_printf("%s (%s)",p,tmp));
	  //return g_strdup_printf("%s (%s)",p,tmp);
          //printf("Homepathmodel=%s\n",p);
	  return p;
      }
      p=np+1;
    }

    g_regex_unref(regex);
    g_free(homepath);
    g_free(p2);
    return g_strdup("HomePathNotFound");
}

gchar *get_storage_devices_models(void)
{
    scan_storage(FALSE);

    struct Info *info = info_unflatten(storage_list);
    if (!info) {
      return g_strdup("");
    }

    guint i, fi;
    struct InfoGroup *group;
    struct InfoField *field;
    gchar *storage_devs = NULL, *tmp;//,*t,*s;
    GList *hdlist=NULL;
    GRegex *regex;
    regex = g_regex_new ("<.*?>", 0, 0, NULL);
    for (i = 0; i < info->groups->len; i++) {
        group = &g_array_index(info->groups, struct InfoGroup, info->groups->len - 1);
        if (!group)
            continue;

        info_group_strip_extra(group);
        for (fi = 0; fi < group->fields->len; fi++) {
            field = &g_array_index(group->fields, struct InfoField, fi);
            if (!field->value)
                continue;

            tmp = g_regex_replace(regex, field->value, -1, 0, "", 0, NULL); // remove html tags
	    tmp=g_strstrip(strreplace(tmp,"  "," "));

	    if(!g_list_find_custom(hdlist, tmp, (GCompareFunc)g_strcmp0) && !strstr(tmp,"CDROM") && !strstr(tmp,"DVD") && !strstr(tmp," CD")) {
                storage_devs = h_strdup_cprintf("%s,", storage_devs, tmp);
	    }
	    hdlist=g_list_append(hdlist, tmp);
        }
    }
    g_regex_unref(regex);
    g_free(info);
    g_list_free_full(hdlist,g_free);
    if(storage_devs) storage_devs[strlen(storage_devs)-1]=0;

    return storage_devs;
}

gchar *get_storage_devices(void)
{
    scan_storage(FALSE);
    return g_strdup(storage_list);
}

gchar *get_printers(void)
{
    scan_printers(FALSE);
    return g_strdup(printer_list);
}

gchar *get_input_devices(void)
{
    scan_input(FALSE);
    return g_strdup(input_list);
}

gchar *get_processor_count(void)
{
    scan_processors(FALSE);
    return g_strdup_printf("%d", g_slist_length(processors));
}

gchar *get_power_state(void)
{
    scan_battery(FALSE);
    if(!powerstate) return g_strdup("AC");
    return g_strdup(powerstate);
}

gchar *get_gpuname(void)
{
    scan_gpu(FALSE);
    if(!gpuname) return g_strdup("Error");
    if(strlen(gpuname)>4 && gpuname[3]=='=') {
      gchar *t=strreplace(g_strdup(gpuname+4),"\n","");
      return t;
    }
    return g_strdup(gpuname);
}

gchar *get_mem_desc(void)
{
    scan_dmi_mem(FALSE);
    return g_strdup(memory_devices_desc);
}



/* TODO: maybe move into processor.c along with processor_name() etc.
 * Could mention the big.LITTLE cluster arangement for ARM that kind of thing.
 * TODO: prefix counts are threads when they should be cores. */
gchar *processor_frequency_desc(GSList * processors)
{
    gchar *ret = g_strdup("");
    GSList *tmp, *l;
    Processor *p;
    float cur_val = -1;
    gint cur_count = 0;

    tmp = g_slist_copy(processors);
    tmp = g_slist_sort(tmp, (GCompareFunc)proc_cmp_max_freq);

    for (l = tmp; l; l = l->next) {
        p = (Processor*)l->data;
        if (cur_val == -1) {
            cur_val = p->cpu_mhz;
            cur_count = 1;
        } else {
            if(cur_val != p->cpu_mhz) {
                ret = h_strdup_cprintf("%s%dx %.2f %s", ret, strlen(ret) ? " + " : "", cur_count, cur_val, _("MHz") );
                cur_val = p->cpu_mhz;
                cur_count = 1;
            } else {
                cur_count++;
            }
        }
    }
    ret = h_strdup_cprintf("%s%dx %.2f %s", ret, strlen(ret) ? " + " : "", cur_count, cur_val, _("MHz"));
    g_slist_free(tmp);
    return ret;
}

gchar *get_processor_frequency_desc(void)
{
    scan_processors(FALSE);
    return processor_frequency_desc(processors);
}

gchar *get_processor_max_frequency(void)
{
    GSList *l;
    Processor *p;
    float max_freq = 0;

    scan_processors(FALSE);

    for (l = processors; l; l = l->next) {
        p = (Processor*)l->data;
        if (p->cpu_mhz > max_freq)
            max_freq = p->cpu_mhz;
    }

    if (max_freq == 0.0f) {
        return g_strdup(N_("Unknown"));
    } else {
        return g_strdup_printf("%.2f %s", max_freq, _("MHz") );
    }
}

gchar *get_motherboard(void)
{
    gchar *board_vendor;

#if defined(ARCH_x86) || defined(ARCH_x86_64)
    gchar *board_name, *board_version;
    gchar *board_part = NULL, *product_part = NULL;
    const gchar *tmp;
    gchar *product_name, *product_vendor, *product_version;
    int b = 0, p = 0;
    gchar *ret;
    scan_dmi(FALSE);

    board_name = dmi_get_str("baseboard-product-name");
    board_version = dmi_get_str("baseboard-version");
    board_vendor = dmi_get_str("baseboard-manufacturer");
    if (board_vendor) {
        /* attempt to shorten */
        tmp = vendor_get_shortest_name(board_vendor);
        if (tmp && tmp != board_vendor) {
            g_free(board_vendor);
            board_vendor = g_strdup(tmp);
        }
    }

    product_name = dmi_get_str("system-product-name");
    product_version = dmi_get_str("system-version");
    product_vendor = dmi_get_str("system-manufacturer");
    if (product_vendor) {
        /* attempt to shorten */
        tmp = vendor_get_shortest_name(product_vendor);
        if (tmp && tmp != product_vendor) {
            g_free(product_vendor);
            product_vendor = g_strdup(tmp);
        }
    }

    if (board_vendor && product_vendor &&
        strcmp(board_vendor, product_vendor) == 0) {
            /* ignore duplicate vendor */
            g_free(product_vendor);
            product_vendor = NULL;
    }

    if (board_name && product_name &&
        strcmp(board_name, product_name) == 0) {
            /* ignore duplicate name */
            g_free(product_name);
            product_name = NULL;
    }

    if (board_version && product_version &&
        strcmp(board_version, product_version) == 0) {
            /* ignore duplicate version */
            g_free(product_version);
            product_version = NULL;
    }

    if (board_name) b += 1;
    if (board_vendor) b += 2;
    if (board_version) b += 4;

    switch(b) {
        case 1: /* only name */
            board_part = g_strdup(board_name);
            break;
        case 2: /* only vendor */
            board_part = g_strdup(board_vendor);
            break;
        case 3: /* only name and vendor */
            board_part = g_strdup_printf("%s %s", board_vendor, board_name);
            break;
        case 4: /* only version? Seems unlikely */
            board_part = g_strdup(board_version);
            break;
        case 5: /* only name and version? */
            board_part = g_strdup_printf("%s %s", board_name, board_version);
            break;
        case 6: /* only vendor and version (like lpereira's Thinkpad) */
            board_part = g_strdup_printf("%s %s", board_vendor, board_version);
            break;
        case 7: /* all */
            board_part = g_strdup_printf("%s %s %s", board_vendor, board_name, board_version);
            break;
    }

    if (product_name) p += 1;
    if (product_vendor) p += 2;
    if (product_version) p += 4;

    switch(p) {
        case 1: /* only name */
            product_part = g_strdup(product_name);
            break;
        case 2: /* only vendor */
            product_part = g_strdup(product_vendor);
            break;
        case 3: /* only name and vendor */
            product_part = g_strdup_printf("%s %s", product_vendor, product_name);
            break;
        case 4: /* only version? Seems unlikely */
            product_part = g_strdup(product_version);
            break;
        case 5: /* only name and version? */
            product_part = g_strdup_printf("%s %s", product_name, product_version);
            break;
        case 6: /* only vendor and version? */
            product_part = g_strdup_printf("%s %s", product_vendor, product_version);
            break;
        case 7: /* all */
            product_part = g_strdup_printf("%s %s %s", product_vendor, product_name, product_version);
            break;
    }

    if (board_part && product_part) {
        ret = g_strdup_printf("%s (%s)", board_part, product_part);
    } else if (board_part)
        ret = g_strdup(board_part);
    else if (product_part)
        ret = g_strdup(product_part);
    else {
        if(strstr(module_call_method("computer::getOSKernel"),"WSL2")){
	    ret = g_strdup(_("WSL2"));
        } else {
            ret = g_strdup(_("(Unknown)"));
	}
    }
    g_free(board_part);
    g_free(product_part);

    g_free(board_name);
    g_free(board_vendor);
    g_free(board_version);
    g_free(product_name);
    g_free(product_vendor);
    g_free(product_version);

    return ret;
#endif

    /* use device tree "model" */
    board_vendor = dtr_get_string("/model", 0);
    if (board_vendor != NULL)
        return board_vendor;

    return g_strdup(_("Unknown"));
}

const ShellModuleMethod *hi_exported_methods(void)
{
    static const ShellModuleMethod m[] = {
        {"getProcessorCount", get_processor_count},
        {"getProcessorName", get_processor_name},
        {"getProcessorDesc", get_processor_desc},
        {"getProcessorNameAndDesc", get_processor_name_and_desc},
        {"getProcessorFrequency", get_processor_max_frequency},
        {"getProcessorFrequencyDesc", get_processor_frequency_desc},
        {"getProcessorHwCaps", ldlinux_hwcaps},
        {"getStorageDevices", get_storage_devices},
        {"getStorageDevicesSimple", get_storage_devices_simple},
        {"getStorageDevicesModels", get_storage_devices_models},
        {"getStorageHomeModels", get_storage_home_models},
        {"getPrinters", get_printers},
        {"getInputDevices", get_input_devices},
        {"getMotherboard", get_motherboard},
        {"getGPUList", get_gpu_summary},
        {"getPowerState", get_power_state},
        {"getGPUname", get_gpuname},
	{"getMemDesc", get_mem_desc},
        {NULL},
    };

    return m;
}

gchar *hi_more_info(gchar * entry)
{
    gchar *info = moreinfo_lookup_with_prefix("DEV", entry);

    if (info)
	return g_strdup(info);

    return g_strdup("?");
}

gchar *hi_get_field(gchar * field)
{
    gchar *info = moreinfo_lookup_with_prefix("DEV", field);
    if (info)
        return g_strdup(info);

    return g_strdup(field);
}

void scan_dmi(gboolean reload)
{
    SCAN_START();
    __scan_dmi();
    SCAN_END();
}

void scan_dmi_mem(gboolean reload)
{
    SCAN_START();
    if (memory_devices_info) g_free(memory_devices_info);
    memory_devices_info = memory_devices_get_info();
    //
    if (memory_devices_desc) g_free(memory_devices_desc);
    gchar *st=memory_devices_get_system_memory_str();
    if(st) {
        memory_devices_desc = g_strdup_printf("%s %s",st,memory_devices_get_system_memory_types_str());
	g_free(st);
    } else {
        memory_devices_desc = NULL;
    }
    SCAN_END();
}

void scan_monitors(gboolean reload)
{
    SCAN_START();
    if (monitors_info)
        g_free(monitors_info);
    monitors_info = monitors_get_info();
    SCAN_END();
}

void scan_firmware(gboolean reload)
{
    SCAN_START();
    if (firmware_info)
        g_free(firmware_info);
    firmware_info = firmware_get_info();
    SCAN_END();
}

void scan_dtree(gboolean reload)
{
    SCAN_START();
    __scan_dtree();
    SCAN_END();
}

void scan_processors(gboolean reload)
{
    SCAN_START();
    if (!processors)
	processors = processor_scan();
    SCAN_END();
}

void scan_battery(gboolean reload)
{
    SCAN_START();
    scan_battery_do();
    SCAN_END();
}

void scan_gpu(gboolean reload)
{
    SCAN_START();
    scan_gpu_do();
    SCAN_END();
}

void scan_pci(gboolean reload)
{
    SCAN_START();
    scan_pci_do();
    SCAN_END();
}

void scan_sensors(gboolean reload)
{
    SCAN_START();
    scan_sensors_do();
    SCAN_END();
}

void scan_printers(gboolean reload)
{
    SCAN_START();
    scan_printers_do();
    SCAN_END();
}

void scan_storage(gboolean reload)
{
    SCAN_START();
    g_free(storage_list);
    storage_list = g_strdup("");
    storage_no_nvme = FALSE;
    if (!__scan_udisks2_devices()) {
        storage_no_nvme = TRUE;
        __scan_ide_devices();
        __scan_scsi_devices();
    }
    SCAN_END();
}

void scan_input(gboolean reload)
{
    SCAN_START();
    __scan_input_devices();
    SCAN_END();
}

void scan_usb(gboolean reload)
{
    SCAN_START();
    __scan_usb();
    SCAN_END();
}

gchar *callback_processors()
{
    return processor_get_info(processors);
}

gchar *callback_dmi()
{
    return g_strdup_printf("%s"
                           "[$ShellParam$]\n"
                           "ViewType=5\n",
                           dmi_info);
}

gchar *callback_dmi_mem()
{
    return g_strdup(memory_devices_info);
}

gchar *callback_monitors()
{
    return g_strdup(monitors_info);
}

gchar *callback_firmware()
{
    return g_strdup(firmware_info);
}

gchar *callback_dtree()
{
    return g_strdup_printf("%s"
        "[$ShellParam$]\n"
        "ViewType=1\n", dtree_info);
}

gchar *callback_battery()
{
    return g_strdup_printf("%s\n"
			   "[$ShellParam$]\n"
			   "ViewType=5\n"
			   "ReloadInterval=4000\n", battery_list);
}

gchar *callback_pci()
{
    return g_strdup(pci_list);
}

gchar *callback_gpu()
{
    return g_strdup(gpu_list);
}

gchar *callback_sensors()
{
    return g_strdup_printf("%s\n"
                           "[$ShellParam$]\n"
                           "ViewType=2\n"
                           "LoadGraphSuffix=\n"
                           "ColumnTitle$TextValue=%s\n"
                           "ColumnTitle$Value=%s\n"
                           "ColumnTitle$Extra1=%s\n"
                           "ShowColumnHeaders=true\n"
                           "RescanInterval=5000\n"
                           "%s\n"
                           "%s",
                           sensors,
                           _("Sensor"), _("Value"),
                                SENSORS_GROUP_BY_TYPE ? _("Driver"): _("Type"),
                           lginterval,
                           sensor_icons);
}

gchar *callback_printers()
{
    return g_strdup_printf("%s\n"
                           "[$ShellParam$]\n"
                           "ViewType=1\n"
			   "ReloadInterval=5000\n"
			   "%s", printer_list, printer_icons);
}

gchar *callback_storage()
{
    return g_strdup_printf("%s\n"
        "[$ShellParam$]\n"
        "ReloadInterval=5000\n"
        "ColumnTitle$TextValue=%s\n"
        "ColumnTitle$Value=%s\n"
        "ColumnTitle$Extra1=%s\n"
        "ShowColumnHeaders=true\n"
        "ViewType=1\n%s", storage_list, _("Device"), _("Size"), _("Model"), storage_icons);
}

gchar *callback_input()
{
    return g_strdup_printf("[Input Devices]\n"
                           "%s"
                           "[$ShellParam$]\n"
                           "ViewType=1\n"
                           "ColumnTitle$TextValue=%s\n"
                           "ColumnTitle$Value=%s\n"
                           "ColumnTitle$Extra1=%s\n"
                           "ShowColumnHeaders=true\n"
                           "ReloadInterval=5000\n%s",
                           input_list, _("Device"), _("Vendor"), _("Type"),
                           input_icons);
}

gchar *callback_usb()
{
    return g_strdup_printf("%s"
               "[$ShellParam$]\n"
               "ViewType=1\n"
               "ReloadInterval=5000\n%s", usb_list, usb_icons);

}

ModuleEntry *hi_module_get_entries(void)
{
    return entries;
}

gchar *hi_module_get_name(void)
{
    return _("Devices");
}

guchar hi_module_get_weight(void)
{
    return 85;
}

void hi_module_init(void)
{
    static SyncEntry entries[] = {
        {
            .name = N_("Update PCI ID listing"),
            .file_name = "pci.ids",
	    .optional = TRUE,
        },
        {
            .name = N_("Update USB ID listing"),
            .file_name = "usb.ids",
	    .optional = TRUE,
        },
        {
            .name = N_("Update EDID vendor codes"),
            .file_name = "edid.ids",
	    .optional = TRUE,
        },
        {
            .name = N_("Update IEEE OUI vendor codes"),
            .file_name = "ieee_oui.ids",
	    .optional = TRUE,
        },
        {
            .name = N_("Update SD card manufacturer information"),
            .file_name = "sdcard.ids",
	    .optional = TRUE,
        },
#ifdef ARCH_x86
#if JSON_CHECK_VERSION(0,20,0)
        {
            .name = N_("Update CPU flags database"),
            .file_name = "cpuflags.json",
	    .optional = TRUE,
        },
#endif
#endif
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS(entries); i++)
        sync_manager_add_entry(&entries[i]);

    init_cups();
    sensor_init();
    udisks2_init();

#ifdef ARCH_x86
    void cpuflags_x86_init(void);
    cpuflags_x86_init();
#endif
}

void hi_module_deinit(void)
{
    moreinfo_del_with_prefix("DEV");
    sensor_shutdown();
    storage_shutdown();
    udisks2_shutdown();
    if(cups) g_module_close(cups);
}

const ModuleAbout *hi_module_get_about(void)
{
    static const ModuleAbout ma = {
        .author = "L. A. F. Pereira",
        .description = N_("Gathers information about hardware devices"),
        .version = VERSION,
        .license = "GNU GPL version 2 or later.",
    };

    return &ma;
}

gchar **hi_module_get_dependencies(void)
{
    static gchar *deps[] = { "computer.so", NULL };

    return deps;
}

const gchar *hi_note_func(gint entry)
{
    if (entry == ENTRY_PCI
        || entry == ENTRY_GPU) {
            const gchar *ids = find_pci_ids_file();
            if (!ids) {
                return g_strdup(_("A copy of <i><b>pci.ids</b></i> is not available on the system."));
            }
            if (ids && strstr(ids, ".min")) {
                return g_strdup(_("A full <i><b>pci.ids</b></i> is not available on the system."));
            }
    }
    if (entry == ENTRY_RESOURCES) {
        if (root_required_for_resources()) {
            return g_strdup(_("Ensure hardinfo2 service is enabled+started: sudo systemctl enable hardinfo2 --now (SystemD distro)\nAdd yourself to hardinfo2 group: sudo usermod -a -G hardinfo2 YOUR_LOGIN\nAnd Logout/Reboot for groups to be updated..."));
        }
    }
    else if (entry == ENTRY_STORAGE){
        if (storage_no_nvme) {
            return g_strdup(
                _("Any NVMe storage devices present are not listed.\n"
                  "<b><i>udisks2</i></b> is required for NVMe devices."));
        }
    }
    else if (entry == ENTRY_DMI_MEM){
        const char *msg;
        if (memory_devices_hinote(&msg)) {
            return msg;
        }
    }
    else if (entry == ENTRY_FW) {
        const char *msg;
        if (firmware_hinote(&msg)) {
            return msg;
        }
    }
    return NULL;
}
