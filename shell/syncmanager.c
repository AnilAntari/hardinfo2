/*
 *    HardInfo2 - System Information and Benchmark
 *    Copyright (C) 2003-2009 L. A. F. Pereira <l@tia.mat.br>
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

#include "config.h"
#include "hardinfo.h"
#include "iconcache.h"
#include "syncmanager.h"

#include <libsoup/soup.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>


#ifndef SOUP_CHECK_VERSION
    #define SOUP_CHECK_VERSION(a,b,c) 0
#endif

typedef struct _SyncDialog SyncDialog;
typedef struct _SyncNetArea SyncNetArea;
typedef struct _SyncNetAction SyncNetAction;

struct _SyncNetArea {
    GtkWidget *vbox;
};

struct _SyncNetAction {
    SyncEntry *entry;
    GError *error;
};

struct _SyncDialog {
    GtkWidget *dialog;
    GtkWidget *label;

    GtkWidget *button_sync;
    GtkWidget *button_cancel;
    GtkWidget *button_close;
    GtkWidget *button_priv_policy;

    GtkWidget *scroll_box;

    SyncNetArea *sna;

    gboolean flag_cancel : 1;
};

static GSList *entries = NULL;
static SoupSession *session = NULL;
static GMainLoop *loop;
static GQuark err_quark;
static guint server_blobs_update_version = 0;
static guint our_blobs_update_version = 0;

//Note there are no personal information involved
//Very old linux systems does not work with HTTPS
//But it is the standard and should be used in new distros
#if(HARDINFO2_NOSSL)
#define API_SERVER_URI "http://api.hardinfo2.org"
#else
#define API_SERVER_URI "https://api.hardinfo2.org"
#endif

#define LABEL_SYNC_DEFAULT                                                     \
    _("<big><b>Synchronize with Central Database</b></big>\n"                  \
      "The following information may be synchronized\n"                         \
      "with the HardInfo2 central database.")
#define LABEL_SYNC_SYNCING                                                     \
    _("<big><b>Synchronizing</b></big>\n"                                      \
      "This may take some time.")

static SyncDialog *sync_dialog_new(GtkWidget *parent);
static void sync_dialog_destroy(SyncDialog *sd);
static void sync_dialog_start_sync(SyncDialog *sd);

static SyncNetArea *sync_dialog_netarea_new(void);
static void sync_dialog_netarea_destroy(SyncNetArea *sna);
static void sync_dialog_netarea_show(SyncDialog *sd);
#if 0
static void sync_dialog_netarea_hide(SyncDialog * sd);
#endif
static void
sync_dialog_netarea_start_actions(SyncDialog *sd, SyncNetAction *sna, gint n);

#define SNA_ERROR(code, message, ...)                                          \
    if (!sna->error) {                                                         \
        sna->error = g_error_new(err_quark, code, message, ##__VA_ARGS__);     \
    }

gint sync_manager_count_entries(void)
{
    return g_slist_length(entries);
}

void sync_manager_add_entry(SyncEntry *entry)
{
    //DEBUG("registering syncmanager entry ''%s''", entry->name);

    entry->selected = TRUE;
    entries = g_slist_append(entries, entry);
}

void sync_manager_clear_entries(void)
{
    //DEBUG("clearing syncmanager entries");

    g_slist_free(entries);
    entries = NULL;
}

void sync_manager_show(GtkWidget *parent)
{
    SyncDialog *sd = sync_dialog_new(parent);

    err_quark = g_quark_from_static_string("syncmanager");

    if (gtk_dialog_run(GTK_DIALOG(sd->dialog)) == GTK_RESPONSE_ACCEPT) {
        shell_view_set_enabled(FALSE);
        shell_status_set_enabled(TRUE);
        shell_set_transient_dialog(GTK_WINDOW(sd->dialog));

        sync_dialog_start_sync(sd);

        shell_set_transient_dialog(NULL);
        shell_status_set_enabled(FALSE);
        shell_view_set_enabled(TRUE);
    }

    sync_dialog_destroy(sd);
}

static gboolean _cancel_sync(GtkWidget *widget, gpointer data)
{
    SyncDialog *sd = (SyncDialog *)data;

    if (session) {
        soup_session_abort(session);
    }

    sd->flag_cancel = TRUE;
    g_main_loop_quit(loop);

    gtk_widget_set_sensitive(widget, FALSE);

    return FALSE;
}

static SyncNetAction *sync_manager_get_selected_actions(gint *n)
{
    gint i;
    GSList *entry;
    SyncNetAction *actions;

    actions = g_new0(SyncNetAction, g_slist_length(entries));

    for (entry = entries, i = 0; entry; entry = entry->next) {
        SyncEntry *e = (SyncEntry *)entry->data;

        if ((entry->next==NULL) || e->selected) {//Last is version
            SyncNetAction sna = {.entry = e};
            actions[i++] = sna;
        }
    }

    *n = i;
    return actions;
}

#if SOUP_CHECK_VERSION(3,0,0)
static GProxyResolver *sync_manager_get_proxy(void)
{
    const gchar *conf;

    if (!(conf = g_getenv("HTTP_PROXY"))) {
        if (!(conf = g_getenv("http_proxy"))) {
            return NULL;
        }
    }

    return g_simple_proxy_resolver_new(conf,NULL);
}
#else
static SoupURI *sync_manager_get_proxy(void)
{
    const gchar *conf;

    if (!(conf = g_getenv("HTTP_PROXY"))) {
        if (!(conf = g_getenv("http_proxy"))) {
            return NULL;
        }
    }

    return soup_uri_new(conf);
}

#endif

static void ensure_soup_session(void)
{
    if (!session) {
#if SOUP_CHECK_VERSION(3,0,0)
      GProxyResolver *resolver=sync_manager_get_proxy();
      session = soup_session_new_with_options("timeout", 10, "proxy-resolver", resolver, NULL);
      if(resolver) g_object_unref(resolver);
#else
#if SOUP_CHECK_VERSION(2,42,0)
        SoupURI *proxy = sync_manager_get_proxy();
        session = soup_session_new_with_options(
            SOUP_SESSION_TIMEOUT, 10, SOUP_SESSION_PROXY_URI, proxy, NULL);
#else
        SoupURI *proxy = sync_manager_get_proxy();
        session = soup_session_async_new_with_options(
            SOUP_SESSION_TIMEOUT, 10, SOUP_SESSION_PROXY_URI, proxy, NULL);
#endif
#endif
    }
}

static void sync_dialog_start_sync(SyncDialog *sd)
{
    gint nactions;
    SyncNetAction *actions;
    gchar *path;
    int fd=-1;
    gchar buf[101];

    path = g_build_filename(g_get_user_config_dir(), "hardinfo2",
                           "blobs-update-version.json", NULL);
    fd = open(path,O_RDONLY);
    if(fd<0) {
        free(path);
        path = g_build_filename(params.path_data,"blobs-update-version.json", NULL);
        fd = open(path,O_RDONLY);
    }
    if(fd>=0){
        if(read(fd,buf,100))
            sscanf(buf,"{\"update-version\":\"%u\",",&our_blobs_update_version);
        close(fd);
    }
    free(path);
    DEBUG("OUR2_BLOBS_UPDATE_VERSION=%u",our_blobs_update_version);

    ensure_soup_session();

    loop = g_main_loop_new(NULL, FALSE);

    gtk_widget_hide(sd->button_sync);
    gtk_widget_hide(sd->button_priv_policy);
    sync_dialog_netarea_show(sd);
    g_signal_connect(G_OBJECT(sd->button_cancel), "clicked",
                     (GCallback)_cancel_sync, sd);

    actions = sync_manager_get_selected_actions(&nactions);
    sync_dialog_netarea_start_actions(sd, actions, nactions);
    g_free(actions);

    if (sd->flag_cancel) {
        gtk_widget_hide(sd->button_cancel);
        gtk_widget_show(sd->button_close);

        /* wait for the user to close the dialog */
        g_main_loop_run(loop);
    }

    g_main_loop_unref(loop);

    shell_do_reload(FALSE);
}


static void got_msg(const guint8 *buf,int len, gpointer user_data)
{
    SyncNetAction *sna = user_data;
    gchar *path;
    int fd,updateversion=0;
    gchar buffer[101];
    gsize datawritten;

    if (sna->entry->file_name != NULL) {
        //check for missing config dirs
        g_mkdir(g_get_user_config_dir(), 0766);
        g_mkdir(g_build_filename(g_get_user_config_dir(),"hardinfo2",NULL), 0766);
	if(strncmp(sna->entry->file_name,"blobs-update-version.json",25)==0){
	    updateversion=1;
	}
        path = g_build_filename(g_get_user_config_dir(), "hardinfo2",
                                       sna->entry->file_name, NULL);
        GFile *file = g_file_new_for_path(path);
        GFileOutputStream *output = g_file_replace(file, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION,
                                                   NULL, &sna->error);

        if(buf){
            DEBUG("got file with len: %u", (unsigned int)len);
            if(len>0){
                g_output_stream_write_all(G_OUTPUT_STREAM(output),buf,len,&datawritten,NULL,&sna->error);
            }
        }

	if(updateversion){
            fd = open(path,O_RDONLY);
            if(fd){
	        if(read(fd,buffer,100)) {
                   sscanf(buffer,"{\"update-version\":\"%u\",",&server_blobs_update_version);
		   DEBUG("SERVER_BLOBS_UPDATE_VERSION=%u",server_blobs_update_version);
	        }
                close(fd);
            }
	}

        g_free(path);
        g_object_unref(file);
    }

}


#if SOUP_CHECK_VERSION(2,42,0)
static void got_response(GObject *source, GAsyncResult *res, gpointer user_data)
#else
static void got_response(SoupSession *source, SoupMessage *res, gpointer user_data)
#endif
{
    SyncNetAction *sna = user_data;
#if SOUP_CHECK_VERSION(2,42,0)
    GInputStream *is;
#endif
    gchar *path;
    int fd,updateversion=0;
    gchar buffer[101];
#if SOUP_CHECK_VERSION(2,42,0)
#else
    const guint8 *buf=NULL;
    gsize len,datawritten;
    SoupBuffer *soupmsg=NULL;
#endif

#if SOUP_CHECK_VERSION(2,42,0)
    is = soup_session_send_finish(session, res, &sna->error);
    if (is == NULL)
        goto out;
    if (sna->error != NULL)
        goto out;
#endif

    if (sna->entry->file_name != NULL) {
        //check for missing config dirs
        g_mkdir(g_get_user_config_dir(), 0766);
        g_mkdir(g_build_filename(g_get_user_config_dir(),"hardinfo2",NULL), 0766);
	if(strncmp(sna->entry->file_name,"blobs-update-version.json",25)==0){
	    updateversion=1;
	}
        path = g_build_filename(g_get_user_config_dir(), "hardinfo2",
                                       sna->entry->file_name, NULL);
        GFile *file = g_file_new_for_path(path);
        GFileOutputStream *output = g_file_replace(file, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION,
                                                   NULL, &sna->error);

        if (output != NULL) {
#if SOUP_CHECK_VERSION(2,42,0)
            g_output_stream_splice(G_OUTPUT_STREAM(output), is,
                                   G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET, NULL,
                                   &sna->error);
#else
            soupmsg=soup_message_body_flatten(res->response_body);
            if(soupmsg){
                soup_buffer_get_data(soupmsg,&buf,&len);
                DEBUG("got file with len: %u", (unsigned int)len);
                if(len>0){
                    g_output_stream_write_all(G_OUTPUT_STREAM(output),buf,len,&datawritten,NULL,&sna->error);
                    soup_buffer_free(soupmsg);
	        }
	    }
#endif
        }

	if(updateversion){
            fd = open(path,O_RDONLY);
            if(fd){
	        if(read(fd,buffer,100))
                    sscanf(buffer,"{\"update-version\":\"%u\",",&server_blobs_update_version);
		DEBUG("SERVER_BLOBS_UPDATE_VERSION=%u",server_blobs_update_version);
                close(fd);
            }
	}

        g_free(path);
        g_object_unref(file);
    }

out:
    g_main_loop_quit(loop);
#if SOUP_CHECK_VERSION(2,42,0)
    g_object_unref(is);
#endif
}

static gboolean send_request_for_net_action(SyncNetAction *sna)
{
    gchar *uri;
    SoupMessage *msg;
    const guint8 *buf=NULL;
    gsize len;
    gchar *contents=NULL;
    gsize size;
#if !SOUP_CHECK_VERSION(3, 0, 0)
    SoupBuffer *soupmsg=NULL;
#endif

    if(!sna->entry->optional || (our_blobs_update_version<server_blobs_update_version)){
        if(strncmp(sna->entry->file_name,"blobs-update-version.json",25)==0){
          uri = g_strdup_printf("%s/%s?ver=%s&blobver=%d&rel=%d", API_SERVER_URI, sna->entry->file_name,VERSION,our_blobs_update_version,RELEASE);
	} else if(strncmp(sna->entry->file_name,"benchmark.json",14)==0){
	    if (sna->entry->generate_contents_for_upload == NULL) {//GET/Fetch
	        gchar *cpuname=module_call_method("devices::getProcessorName");
		gchar *machinetype=module_call_method("computer::getMachineTypeEnglish");
	        if(params.bench_user_note){
		  uri = g_strdup_printf("%s/%s?ver=%s&L=%d&rel=%d&MT=%s&CPU=%s&BUN=%s", API_SERVER_URI,
				        sna->entry->file_name, VERSION,
		                        params.max_bench_results,RELEASE,
					machinetype,
					cpuname,
					params.bench_user_note);
		} else {
		  uri = g_strdup_printf("%s/%s?ver=%s&L=%d&rel=%d&&MT=%s&CPU=%s", API_SERVER_URI,
					sna->entry->file_name, VERSION,
		                        params.max_bench_results, RELEASE,
					machinetype,
					cpuname);
		}
		g_free(cpuname);
		g_free(machinetype);
	    } else {//POST/Send
	      uri = g_strdup_printf("%s/%s?ver=%s&rel=%d", API_SERVER_URI,
				    sna->entry->file_name, VERSION, RELEASE);
	    }
	} else {
            uri = g_strdup_printf("%s/%s", API_SERVER_URI, sna->entry->file_name);
	}
    if (sna->entry->generate_contents_for_upload == NULL) {
        msg = soup_message_new("GET", uri);
    } else {
        contents = sna->entry->generate_contents_for_upload(&size);

        msg = soup_message_new("POST", uri);

#if SOUP_CHECK_VERSION(3, 0, 0)
        soup_message_set_request_body_from_bytes(msg, "application/octet-stream", g_bytes_new_static(contents,size));
#else
        soup_message_set_request(msg, "application/octet-stream",
                                 SOUP_MEMORY_TAKE, contents, size);
#endif
    }
    if(params.gui_running){
#if SOUP_CHECK_VERSION(3, 0, 0)
      soup_session_send_async(session, msg, G_PRIORITY_DEFAULT, NULL, got_response, sna);
#else
#if SOUP_CHECK_VERSION(2,42,0)
        soup_session_send_async(session, msg, NULL, got_response, sna);
#else
        soup_session_queue_message(session, msg, got_response, sna);
#endif
#endif

    } else {//Blocking/Sync sending when no gui

#if SOUP_CHECK_VERSION(3, 0, 0)
    buf=g_bytes_unref_to_data(soup_session_send_and_read(session, msg, NULL, NULL), &len);
    got_msg(buf,len,sna);
#else
        soup_session_send_message(session, msg);
        soupmsg=soup_message_body_flatten(msg->response_body);
        if(soupmsg){
            soup_buffer_get_data(soupmsg,&buf,&len);
            got_msg(buf,len,sna);
	}
#endif
    }
    if(params.gui_running)
        g_main_loop_run(loop);

    g_object_unref(msg);
    g_free(uri);

    if (sna->error != NULL) {
        DEBUG("Error while sending request: %s", sna->error->message);
        g_error_free(sna->error);
        sna->error = NULL;
        return FALSE;
    }
    }
    return TRUE;
}

static void
sync_dialog_netarea_start_actions(SyncDialog *sd, SyncNetAction sna[], gint n)
{
    gint i;
    GtkWidget **labels;
    GtkWidget **status_labels;
    const gchar *done_str = "\342\234\223";
    const gchar *error_str = "\342\234\227";
    const gchar *curr_str = "\342\226\266";
    const gchar *empty_str = "\302\240\302\240";

    if(params.bench_user_note && !strstr(params.bench_user_note,"Group-MachineName-ServerReq")) {
        GKeyFile *key_file = g_key_file_new();
	g_mkdir(g_get_user_config_dir(),0755);
	g_mkdir(g_build_filename(g_get_user_config_dir(), "hardinfo2", NULL),0755);
	gchar *conf_path = g_build_filename(g_get_user_config_dir(), "hardinfo2", "settings.ini", NULL);
	g_key_file_load_from_file( key_file, conf_path,
				   G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
	if(params.bench_user_note) {
	    g_key_file_set_string(key_file, "Sync", "UserNote", params.bench_user_note);
	} else {
	    g_key_file_set_string(key_file, "Sync", "UserNote", "");
	}
#if GLIB_CHECK_VERSION(2,40,0)
	g_key_file_save_to_file(key_file, conf_path, NULL);
#else
	g2_key_file_save_to_file(key_file, conf_path, NULL);
#endif
	g_free(conf_path);
	g_key_file_free(key_file);
    }

    labels = g_new0(GtkWidget *, n);
    status_labels = g_new0(GtkWidget *, n);

    for (i = n-1; i >0; i--) {
        GtkWidget *hbox;

        hbox = gtk_hbox_new(FALSE, 5);

        labels[i] = gtk_label_new(_(sna[i].entry->name));
        status_labels[i] = gtk_label_new(empty_str);

        gtk_label_set_use_markup(GTK_LABEL(labels[i]), TRUE);
        gtk_label_set_use_markup(GTK_LABEL(status_labels[i]), TRUE);

        gtk_misc_set_alignment(GTK_MISC(labels[i]), 0.0, 0.5);
        gtk_misc_set_alignment(GTK_MISC(status_labels[i]), 1.0, 0.5);

        gtk_box_pack_start(GTK_BOX(hbox), status_labels[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), labels[i], TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(sd->sna->vbox), hbox, FALSE, FALSE, 3);

        gtk_widget_show_all(hbox);
    }

    int ii=5;while(ii-- && gtk_events_pending() && !gtk_main_iteration_do(FALSE)) {;}

    for (i = n-1; i >0; i--) {
        gchar *markup;

        if (sd->flag_cancel) {
            markup = g_strdup_printf("<s>%s</s> <i>%s</i>",
                                     _(sna[i].entry->name),
                                     _("(canceled)"));
            gtk_label_set_markup(GTK_LABEL(labels[i]), markup);
            g_free(markup);

            gtk_label_set_markup(GTK_LABEL(status_labels[i]), error_str);
            break;
        }

        markup = g_strdup_printf("<b>%s</b>", _(sna[i].entry->name));
        gtk_label_set_markup(GTK_LABEL(labels[i]), markup);
        g_free(markup);

        gtk_label_set_markup(GTK_LABEL(status_labels[i]), curr_str);

        if (sna[i].entry && !send_request_for_net_action(&sna[i])) {
            markup = g_strdup_printf("<b><s>%s</s></b> <i>%s</i>",
                                     _(sna[i].entry->name), _("(failed)"));
            gtk_label_set_markup(GTK_LABEL(labels[i]), markup);
            g_free(markup);

            sd->flag_cancel = TRUE;

            gtk_label_set_markup(GTK_LABEL(status_labels[i]), error_str);
            if (sna[i].error) {
                g_error_free(sna[i].error);
            }
            break;
        }

        gtk_label_set_markup(GTK_LABEL(status_labels[i]), done_str);
        gtk_label_set_markup(GTK_LABEL(labels[i]), _(sna[i].entry->name));
    }

    g_free(labels);
    g_free(status_labels);
}

static SyncNetArea *sync_dialog_netarea_new(void)
{
    SyncNetArea *sna = g_new0(SyncNetArea, 1);

    sna->vbox = gtk_vbox_new(FALSE, 0);

    gtk_container_set_border_width(GTK_CONTAINER(sna->vbox), 10);

    gtk_widget_show_all(sna->vbox);
    gtk_widget_hide(sna->vbox);

    return sna;
}

static void sync_dialog_netarea_destroy(SyncNetArea *sna)
{
    g_return_if_fail(sna != NULL);

    g_free(sna);
}

static void sync_dialog_netarea_show(SyncDialog *sd)
{
    g_return_if_fail(sd && sd->sna);

    gtk_widget_hide(GTK_WIDGET(sd->scroll_box));
    gtk_widget_show(GTK_WIDGET(sd->sna->vbox));

    gtk_label_set_markup(GTK_LABEL(sd->label), LABEL_SYNC_SYNCING);
    //gtk_window_set_default_size(GTK_WINDOW(sd->dialog), 0, 0);
    //gtk_window_reshow_with_initial_size(GTK_WINDOW(sd->dialog));
}

/*static void sync_dialog_netarea_hide(SyncDialog *sd)
{
    g_return_if_fail(sd && sd->sna);

    gtk_widget_show(GTK_WIDGET(sd->scroll_box));
    gtk_widget_hide(GTK_WIDGET(sd->sna->vbox));

    gtk_label_set_markup(GTK_LABEL(sd->label), LABEL_SYNC_DEFAULT);
    gtk_window_reshow_with_initial_size(GTK_WINDOW(sd->dialog));
}*/

static void populate_store(GtkListStore *store)
{
    GSList *entry;
    SyncEntry *e;

    gtk_list_store_clear(store);

    for (entry = entries; entry; entry = entry->next) {
        GtkTreeIter iter;

        e = (SyncEntry *)entry->data;

        e->selected = TRUE;

        gtk_list_store_prepend(store, &iter);
        gtk_list_store_set(store, &iter, 0, TRUE, 1, _(e->name), 2, e, -1);
    }
}

static void sel_toggle(GtkCellRendererToggle *cellrenderertoggle,
                       gchar *path_str,
                       GtkTreeModel *model)
{
    GtkTreeIter iter;
    GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
    SyncEntry *se;
    gboolean active;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, 0, &active, 2, &se, -1);

    if(strncmp(se->name,N_("Send benchmark results"),10)==0) //only allow to disable sending benchmark results
      se->selected = !active;

    gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, se->selected, -1);
    gtk_tree_path_free(path);
}

static void close_clicked(void) { g_main_loop_quit(loop); }


void insert_text_event(GtkEditable *editable, const gchar *text, gint length, gint *position, gpointer data) {
    int i,c=0;
    guint u;
    gchar *usernote=g_strdup(g_strconcat(gtk_entry_get_text(data),text,NULL));

    //first cannot be dash
    //if((*position==0) && (text[0]=='-')) {g_signal_stop_emission_by_name(G_OBJECT(editable), "insert-text");g_free(usernote);return;}
    //only two dash
    for (u=0; u < strlen(usernote); u++) if(usernote[u]=='-') c++;
    if(c>2) {g_signal_stop_emission_by_name(G_OBJECT(editable), "insert-text");g_free(usernote);return;}
    //check digit+alpha+dash
    for (i = 0; i < length; i++) {
	if (!isalpha(text[i]) && !isdigit(text[i]) && (text[i]!='-')) {
            g_signal_stop_emission_by_name(G_OBJECT(editable), "insert-text");
	    g_free(usernote);
	    return;
        }
    }
}

void changed_event(GtkEditable *editable, gpointer data) {
    gchar *usernote=g_strdup(gtk_entry_get_text(data));
    //printf("Usernote=%s\n",usernote);
    if(params.bench_user_note) g_free(params.bench_user_note);
    params.bench_user_note=usernote;
}


static SyncDialog *sync_dialog_new(GtkWidget *parent)
{
    SyncDialog *sd;
    GtkWidget *dialog;
    GtkWidget *dialog1_vbox;
    GtkWidget *scrolledwindow2;
    GtkWidget *treeview2;
    GtkWidget *dialog1_action_area;
    GtkWidget *button8;
    GtkWidget *button7;
    GtkWidget *button6;
    GtkWidget *priv_policy_btn,*link1;
    GtkWidget *label,*label2,*label3;
    GtkWidget *hbox,*hbox2,*hbox3;
    GtkWidget *usernote;

    GtkTreeViewColumn *column;
    GtkTreeModel *model;
    GtkListStore *store;
    GtkCellRenderer *cr_text, *cr_toggle;

    sd = g_new0(SyncDialog, 1);
    sd->sna = sync_dialog_netarea_new();

    dialog = gtk_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
    gtk_window_set_title(GTK_WINDOW(dialog), _("Synchronize"));
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_icon(GTK_WINDOW(dialog), icon_cache_get_pixbuf("sync.svg"));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560*params.scale, 500*params.scale);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);

    gtk_container_set_border_width(GTK_CONTAINER(dialog), 5);

#if GTK_CHECK_VERSION(2, 14, 0)
    dialog1_vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
#else
    dialog1_vbox = GTK_DIALOG(dialog)->vbox;
#endif
    gtk_box_set_spacing(GTK_BOX(dialog1_vbox), 5);
    gtk_container_set_border_width(GTK_CONTAINER(dialog1_vbox), 4);
    gtk_widget_show(dialog1_vbox);
#if GTK_CHECK_VERSION(3, 0, 0)
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
#else
    hbox = gtk_hbox_new(FALSE, 5);
#endif
    gtk_box_pack_start(GTK_BOX(dialog1_vbox), hbox, FALSE, FALSE, 0);

    label = gtk_label_new(LABEL_SYNC_DEFAULT);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
#if GTK_CHECK_VERSION(3, 0, 0)
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
#else
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
#endif

    gtk_box_pack_start(GTK_BOX(hbox), icon_cache_get_image_at_size("sync.svg", 64, 64), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);
    gtk_widget_show_all(hbox);

    //UserNote
#if GTK_CHECK_VERSION(3, 0, 0)
    hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
#else
    hbox2 = gtk_hbox_new(FALSE, 5);
#endif
    gtk_box_pack_start(GTK_BOX(dialog1_vbox), hbox2, FALSE, FALSE, 0);

    usernote = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(usernote),50);
    g_signal_connect (usernote, "insert-text", G_CALLBACK(insert_text_event), (gpointer) usernote);
    g_signal_connect (usernote, "changed", G_CALLBACK(changed_event), (gpointer) usernote);
    if(!params.bench_user_note){//Fetch UserNote from Settings if not specified
        GKeyFile *key_file = g_key_file_new();
	gchar *conf_path = g_build_filename(g_get_user_config_dir(), "hardinfo2", "settings.ini", NULL);
	g_key_file_load_from_file(key_file, conf_path,
				G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
	params.bench_user_note = g_key_file_get_string(key_file, "Sync", "UserNote", NULL);
        g_free(conf_path);
	g_key_file_free(key_file);
    }

    if(params.bench_user_note) gtk_entry_set_text (GTK_ENTRY(usernote), params.bench_user_note);
    else gtk_entry_set_text (GTK_ENTRY(usernote), "Group-MachineName-ServerReq");

    label2 = gtk_label_new(_("User Note "));
    gtk_label_set_line_wrap(GTK_LABEL(label2), FALSE);
    gtk_label_set_use_markup(GTK_LABEL(label2), TRUE);

    gtk_box_pack_start(GTK_BOX(hbox2), label2, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox2), usernote, TRUE, TRUE, 0);
    gtk_widget_show_all(hbox2);

    //user note help
#if GTK_CHECK_VERSION(3, 0, 0)
    hbox3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
#else
    hbox3 = gtk_hbox_new(FALSE, 5);
#endif
    gtk_box_pack_start(GTK_BOX(dialog1_vbox), hbox3, FALSE, FALSE, 0);

    label3 = gtk_label_new(_("Leave it default/blank or see "));
    gtk_label_set_line_wrap(GTK_LABEL(label3), FALSE);
    gtk_label_set_use_markup(GTK_LABEL(label3), TRUE);
    link1 = gtk_link_button_new_with_label("https://hardinfo2.org/userguide#usernote", _("User Guide"));
    gtk_box_pack_start(GTK_BOX(hbox3), label3, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox3), link1, FALSE, TRUE, 0);
    gtk_widget_show_all(hbox3);

    gtk_box_pack_start(GTK_BOX(dialog1_vbox), sd->sna->vbox, TRUE, TRUE, 0);

    scrolledwindow2 = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_show(scrolledwindow2);
    gtk_box_pack_start(GTK_BOX(dialog1_vbox), scrolledwindow2, TRUE, TRUE, 0);
    gtk_widget_set_size_request(scrolledwindow2, -1, 200);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow2),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolledwindow2),
                                        GTK_SHADOW_IN);

    store =
        gtk_list_store_new(3, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_POINTER);
    model = GTK_TREE_MODEL(store);

    treeview2 = gtk_tree_view_new_with_model(model);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview2), FALSE);
    gtk_widget_show(treeview2);
    gtk_container_add(GTK_CONTAINER(scrolledwindow2), treeview2);

    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview2), column);

    cr_toggle = gtk_cell_renderer_toggle_new();
    gtk_tree_view_column_pack_start(column, cr_toggle, FALSE);
    g_signal_connect(cr_toggle, "toggled", G_CALLBACK(sel_toggle), model);
    gtk_tree_view_column_add_attribute(column, cr_toggle, "active", 0);

    cr_text = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, cr_text, TRUE);
    gtk_tree_view_column_add_attribute(column, cr_text, "markup", 1);

    populate_store(store);

    priv_policy_btn = gtk_link_button_new_with_label(
            "https://github.com/hardinfo2/hardinfo2?tab=readme-ov-file#privacy-policy",
            _("Privacy Policy"));
    gtk_widget_show(priv_policy_btn);
    gtk_box_pack_start(GTK_BOX(dialog1_vbox), priv_policy_btn, FALSE, FALSE, 0);

#if GTK_CHECK_VERSION(2, 14, 0)
    dialog1_action_area = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
#else
    dialog1_action_area = GTK_DIALOG(dialog)->action_area;
#endif
    gtk_widget_show(dialog1_action_area);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(dialog1_action_area),
                              GTK_BUTTONBOX_END);

    button8 = gtk_button_new_with_mnemonic(_("_Cancel"));
    gtk_widget_show(button8);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button8,
                                 GTK_RESPONSE_CANCEL);
#if GTK_CHECK_VERSION(2, 18, 0)
    gtk_widget_set_can_default(button8, TRUE);
#else
    GTK_WIDGET_SET_FLAGS(button8, GTK_CAN_DEFAULT);
#endif
    button7 = gtk_button_new_with_mnemonic(_("_Synchronize"));
    gtk_widget_show(button7);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button7,
                                 GTK_RESPONSE_ACCEPT);
#if GTK_CHECK_VERSION(2, 18, 0)
    gtk_widget_set_can_default(button7, TRUE);
#else
    GTK_WIDGET_SET_FLAGS(button7, GTK_CAN_DEFAULT);
#endif
    button6 = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
    g_signal_connect(G_OBJECT(button6), "clicked", (GCallback)close_clicked,
                     NULL);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button6,
                                 GTK_RESPONSE_ACCEPT);
#if GTK_CHECK_VERSION(2, 18, 0)
    gtk_widget_set_can_default(button6, TRUE);
#else
    GTK_WIDGET_SET_FLAGS(button6, GTK_CAN_DEFAULT);
#endif

    sd->dialog = dialog;
    sd->button_sync = button7;
    sd->button_cancel = button8;
    sd->button_close = button6;
    sd->button_priv_policy = priv_policy_btn;
    sd->scroll_box = scrolledwindow2;
    sd->label = label;

    return sd;
}

static void sync_dialog_destroy(SyncDialog *sd)
{
    gtk_widget_destroy(sd->dialog);
    sync_dialog_netarea_destroy(sd->sna);
    g_free(sd);
}

static gboolean sync_one(gpointer data)
{
    SyncNetAction *sna = data;
    if (sna->entry->generate_contents_for_upload){
        if(params.gui_running){
           goto out;
        }
    }
    DEBUG("Syncronizing: %s", sna->entry->name);

    if(params.gui_running){
        gchar *msg = g_strdup_printf(_("Synchronizing: %s"), _(sna->entry->name));
        shell_status_update(msg);
        shell_status_pulse();
        g_free(msg);
    }
    send_request_for_net_action(sna);

out:
    g_main_loop_unref(loop);
    return FALSE;
}

void sync_manager_update_on_startup(int send_benchmark)//0:normal only get, 1:send benchmark
{
    GSList *entry;
    gchar *path;
    int fd=-1;
    gchar buf[101];
    path = g_build_filename(g_get_user_config_dir(), "hardinfo2",
                           "blobs-update-version.json", NULL);
    fd = open(path,O_RDONLY);
    if(fd<0) {
        free(path);
        path = g_build_filename(params.path_data,"blobs-update-version.json", NULL);
        fd = open(path,O_RDONLY);
    }
    if(fd>=0){
        if(read(fd,buf,100))
	    sscanf(buf,"{\"update-version\":\"%u\",",&our_blobs_update_version);
        close(fd);
    }
    free(path);
    DEBUG("OUR1_BLOBS_UPDATE_VERSION=%u",our_blobs_update_version);

    ensure_soup_session();

    loop = g_main_loop_new(NULL, FALSE);

    if(!params.gui_running) entries=g_slist_reverse(entries);//wrong direction for sync sending
    for (entry = entries; entry; entry = entry->next) {
        SyncNetAction *action = g_new0(SyncNetAction, 1);

        action->entry = entry->data;
	if(params.gui_running){
            loop = g_main_loop_ref(loop);
            g_idle_add(sync_one, action);
	} else {
	  //if send benchmark - only send benchmark
	  //if not send benchmark - sync everything else
	    if( ((strncmp(action->entry->file_name,"benchmark.json",14)==0) &&
		 (action->entry->generate_contents_for_upload) && send_benchmark) ||
		((!(strncmp(action->entry->file_name,"benchmark.json",14)==0) ||
		 (!action->entry->generate_contents_for_upload)) && (!send_benchmark)) ){
            loop = g_main_loop_ref(loop);
	    sync_one(action);
	    }
	}
    }
    if(!params.gui_running) entries=g_slist_reverse(entries);//wrong direction for sync sending
}
