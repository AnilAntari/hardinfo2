/*
 *    HardInfo - Displays System Information
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

#include <config.h>
#include <gtk/gtk.h>
#include <stock.h>
#include <iconcache.h>

static struct {
    gchar *filename;
    gchar *stock_id;
} stock_icons[] = {
    { "refresh.svg", HI_STOCK_REFRESH},
    { "report.svg", HI_STOCK_REPORT},
    { "sync.svg", HI_STOCK_SYNC},
    { "updates.svg", HI_STOCK_UPDATES},
};

static GtkIconFactory *icon_factory;

void stock_icon_register(gchar * filename, gchar * stock_id)
{
    GtkIconSet *icon_set;
    GtkIconSource *icon_source;

    icon_set = gtk_icon_set_new();
    icon_source = gtk_icon_source_new();

    gtk_icon_source_set_pixbuf(icon_source,
			       icon_cache_get_pixbuf(filename));
    gtk_icon_set_add_source(icon_set, icon_source);
    gtk_icon_source_free(icon_source);

    gtk_icon_factory_add(icon_factory, stock_id, icon_set);

    gtk_icon_set_unref(icon_set);
}

void stock_icons_init(void)
{
    guint i;
    guint n_stock_icons = G_N_ELEMENTS(stock_icons);

    DEBUG("initializing stock icons");

    icon_factory = gtk_icon_factory_new();

    for (i = 0; i < n_stock_icons; i++) {
	stock_icon_register(stock_icons[i].filename,
			    stock_icons[i].stock_id);
    }

    gtk_icon_factory_add_default(icon_factory);

    g_object_unref(icon_factory);
}
