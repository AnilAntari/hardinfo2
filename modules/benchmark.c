/*
 *    HardInfo - Displays System Information
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

#define _GNU_SOURCE
#include <config.h>
#include <hardinfo.h>
#include <iconcache.h>
#include <shell.h>
#include <syncmanager.h>

#include <sys/resource.h>
#include <sys/time.h>

#include <signal.h>
#include <sys/types.h>

#include "appf.h"
#include "benchmark.h"
#include "cpu_util.h"

#include "benchmark/bench_results.c"

bench_value bench_results[BENCHMARK_N_ENTRIES];

static int btotaltimer, btimer;

static void do_benchmark(void (*benchmark_function)(void), int entry);
static gchar *benchmark_include_results_reverse(bench_value result,
                                                const gchar *benchmark);
static gchar *benchmark_include_results(bench_value result,
                                        const gchar *benchmark);

/* ModuleEntry entries, scan_*(), callback_*(), etc. */
#include "benchmark/benches.c"

char *bench_value_to_str(bench_value r)
{
    gboolean has_rev = (r.revision >= 0);
    gboolean has_extra = (r.extra[0] != 0);
    char *ret = g_strdup_printf("%lf; %lf; %d", r.result, r.elapsed_time, r.threads_used);
    if (has_rev || has_extra)
        ret = appf(ret, "; ", "%d", r.revision);
    if (has_extra)
        ret = appf(ret, "; ", "%s", r.extra);
    return ret;
}

bench_value bench_value_from_str(const char *str)
{
    bench_value ret = EMPTY_BENCH_VALUE;
    char rstr[32] = "", estr[32] = "", *p;
    int t, c, v;
    char extra[256];
    if (str) {
        /* try to handle floats from locales that use ',' or '.' as decimal sep
         */
        c = sscanf(
            str, "%[-+0-9.,]; %[-+0-9.,]; %d; %d; %255[^\r\n;|]",
            rstr, estr, &t, &v, extra);
        if (c >= 3) {
            if ((p = strchr(rstr, ','))) {
                *p = '.';
            }
            if ((p = strchr(estr, ','))) {
                *p = '.';
            }
            ret.result = g_ascii_strtod(rstr, NULL);
            ret.elapsed_time = g_ascii_strtod(estr, NULL);
            ret.threads_used = t;
        }
        if (c >= 4) {
            ret.revision = v;
        }
        if (c >= 5) {
            strcpy(ret.extra, extra);
        }
    }
    return ret;
}

typedef struct _ParallelBenchTask ParallelBenchTask;

struct _ParallelBenchTask {
    gint thread_number;
    guint start, end;
    gpointer data, callback;
    int *stop;
};

static gpointer benchmark_crunch_for_dispatcher(gpointer data)
{
    ParallelBenchTask *pbt = (ParallelBenchTask *)data;
    gpointer (*callback)(void *data, gint thread_number);
    gpointer return_value = g_malloc(sizeof(double));
    int count = 0;

    if ((callback = pbt->callback)) {
        while (!*pbt->stop) {
            callback(pbt->data, pbt->thread_number);
            /* don't count if didn't finish in time */
            if (!*pbt->stop)
                count++;
        }
    } else {
        DEBUG("this is thread %p; callback is NULL and it should't be!",
              g_thread_self());
    }

    g_free(pbt);

    *(double *)return_value = (double)count;
    return return_value;
}

bench_value benchmark_crunch_for(float seconds,
                                 gint n_threads,
                                 gpointer callback,
                                 gpointer callback_data)
{
    int cpu_procs, cpu_cores, cpu_threads, cpu_nodes;
    int thread_number, stop = 0;
    GSList *threads = NULL, *t;
    GTimer *timer = NULL;
    bench_value ret = EMPTY_BENCH_VALUE;

    timer = g_timer_new();

    cpu_procs_cores_threads_nodes(&cpu_procs, &cpu_cores, &cpu_threads, &cpu_nodes);
    if (n_threads > 0)
        ret.threads_used = n_threads;
    else if (n_threads < 0)
        ret.threads_used = cpu_cores;
    else
        ret.threads_used = cpu_threads;

    g_timer_start(timer);
    for (thread_number = 0; thread_number < ret.threads_used; thread_number++) {
        ParallelBenchTask *pbt = g_new0(ParallelBenchTask, 1);
        GThread *thread;

        DEBUG("launching thread %d", thread_number);

        pbt->thread_number = thread_number;
        pbt->data = callback_data;
        pbt->callback = callback;
        pbt->stop = &stop;

#if GLIB_CHECK_VERSION(2,32,0)
        thread = g_thread_new("dispatcher", (GThreadFunc)benchmark_crunch_for_dispatcher, pbt);
#else
        thread = g_thread_create((GThreadFunc)benchmark_crunch_for_dispatcher, pbt,TRUE,NULL);
#endif
        threads = g_slist_prepend(threads, thread);

        DEBUG("thread %d launched as context %p", thread_number, thread);
    }

    /* wait for time */
    // while ( g_timer_elapsed(timer, NULL) < seconds ) { }
    g_usleep(seconds * 1000000);

    /* signal all threads to stop */
    stop = 1;
    g_timer_stop(timer);

    ret.result = 0;
    DEBUG("waiting for all threads to finish");
    for (t = threads; t; t = t->next) {
        DEBUG("waiting for thread with context %p", t->data);
        gpointer *rv = g_thread_join((GThread *)t->data);
        ret.result += *(double *)rv;
        g_free(rv);
    }

    ret.elapsed_time = g_timer_elapsed(timer, NULL);

    g_slist_free(threads);
    g_timer_destroy(timer);

    return ret;
}

static gpointer benchmark_parallel_for_dispatcher(gpointer data)
{
    ParallelBenchTask *pbt = (ParallelBenchTask *)data;
    gpointer (*callback)(unsigned int start, unsigned int end, void *data,
                         gint thread_number);
    gpointer return_value = NULL;

    if ((callback = pbt->callback)) {
        DEBUG("this is thread %p; items %d -> %d, data %p", g_thread_self(),
              pbt->start, pbt->end, pbt->data);
        return_value =
            callback(pbt->start, pbt->end, pbt->data, pbt->thread_number);
        DEBUG("this is thread %p; return value is %p", g_thread_self(),
              return_value);
    } else {
        DEBUG("this is thread %p; callback is NULL and it should't be!",
              g_thread_self());
    }

    g_free(pbt);

    return return_value;
}

/* one call for each thread to be used */
bench_value
benchmark_parallel(gint n_threads, gpointer callback, gpointer callback_data)
{
    int cpu_procs, cpu_cores, cpu_threads, cpu_nodes;
    cpu_procs_cores_threads_nodes(&cpu_procs, &cpu_cores, &cpu_threads, &cpu_nodes);

    if (n_threads == 0)
        n_threads = cpu_threads;
    else if (n_threads == -1)
        n_threads = cpu_cores;

    return benchmark_parallel_for(n_threads, 0, n_threads, callback,
                                  callback_data);
}

/* Note:
 *    benchmark_parallel_for(): element [start] included, but [end] is excluded.
 *    callback(): expected to processes elements [start] through [end]
 * inclusive.
 */
bench_value benchmark_parallel_for(gint n_threads,
                                   guint start,
                                   guint end,
                                   gpointer callback,
                                   gpointer callback_data)
{
    int cpu_procs, cpu_cores, cpu_threads, cpu_nodes;
    guint iter_per_thread=1, iter, thread_number = 0;
    GSList *threads = NULL, *t;
    GTimer *timer;

    bench_value ret = EMPTY_BENCH_VALUE;

    timer = g_timer_new();

    cpu_procs_cores_threads_nodes(&cpu_procs, &cpu_cores, &cpu_threads, &cpu_nodes);

    if (n_threads > 0)
        ret.threads_used = n_threads;
    else if (n_threads < 0)
        ret.threads_used = cpu_cores;
    else
        ret.threads_used = cpu_threads;

    while (ret.threads_used > 0) {
        iter_per_thread = (end - start) / ret.threads_used;

        if (iter_per_thread == 0) {
            DEBUG("not enough items per thread; disabling one thread");
            ret.threads_used--;
        } else {
            break;
        }
    }

    /*DEBUG("Using %d threads across %d logical processors; processing %d elements (%d per thread)",
      ret.threads_used, cpu_threads, (end - start), iter_per_thread);*/

    g_timer_start(timer);
    for (iter = start; iter < end;) {
        ParallelBenchTask *pbt = g_new0(ParallelBenchTask, 1);
        GThread *thread;

        guint ts = iter, te = iter + iter_per_thread;
        /* add the remainder of items/iter_per_thread to the last thread */
        if (end - te < iter_per_thread)
            te = end;
        iter = te;

        DEBUG("launching thread %d", 1 + thread_number);

        pbt->thread_number = thread_number++;
        pbt->start = ts;
        pbt->end = te - 1;
        pbt->data = callback_data;
        pbt->callback = callback;

#if GLIB_CHECK_VERSION(2,32,0)
        thread = g_thread_new("dispatcher", (GThreadFunc)benchmark_parallel_for_dispatcher, pbt);
#else
        thread = g_thread_create((GThreadFunc)benchmark_parallel_for_dispatcher, pbt,TRUE,NULL);
#endif

        threads = g_slist_prepend(threads, thread);

        DEBUG("thread %d launched as context %p", thread_number, thread);
    }

    DEBUG("waiting for all threads to finish");
    for (t = threads; t; t = t->next) {
        DEBUG("waiting for thread with context %p", t->data);
        gpointer *rv = g_thread_join((GThread *)t->data);
        if (rv) {
            if (ret.result == -1.0)
                ret.result = 0;
            ret.result += *(double *)rv;
        }
        g_free(rv);
    }

    g_timer_stop(timer);
    ret.elapsed_time = g_timer_elapsed(timer, NULL);

    g_slist_free(threads);
    g_timer_destroy(timer);

    DEBUG("finishing; all threads took %f seconds to finish", ret.elapsed_time);

    return ret;
}

gchar *hi_more_info(gchar *entry)
{
    const gchar *info = moreinfo_lookup_with_prefix("BENCH", entry);
    return g_strdup(info ? info : "?");
}

gchar *hi_get_field(gchar *field)
{
    const gchar *info = moreinfo_lookup_with_prefix("BENCH", field);
    return g_strdup(info ? info : field);
}

static void br_mi_add(char **results_list, bench_result *b, gboolean select)
{
    static unsigned int ri = 0; /* to ensure key is unique */
    gchar *rkey, *lbl, *elbl, *this_marker;

    if (select) {
        this_marker = format_with_ansi_color(_("This Machine"), "0;30;43",
                                             params.fmt_opts);
    } else {
        this_marker = "";
    }

    rkey = g_strdup_printf("%s__%d", b->machine->mid, ri++);

    if(strstr(b->name,"GPU")){//GPU
        lbl = g_strdup_printf("%s%s%s%s", this_marker, select ? " " : "",
                          b->machine->gpu_name,
                          b->legacy ? problem_marker() : "");
    } else if(strstr(b->name,"Storage")){//Storage
        lbl = g_strdup_printf("%s%s%s%s", this_marker, select ? " " : "",
                          b->machine->storage,
                          b->legacy ? problem_marker() : "");
    } else {//CPU
        lbl = g_strdup_printf("%s%s%s%s", this_marker, select ? " " : "",
                          b->machine->cpu_name,
                          b->legacy ? problem_marker() : "");
    }
    elbl = key_label_escape(lbl);

    if(strstr(b->name,"GPU")){//GPU
         *results_list = h_strdup_cprintf("$@%s%s$%s=%.2f\n", *results_list,
                                     select ? "*" : "", rkey, elbl,
                                     b->bvalue.result);
    } else if(strstr(b->name,"Storage")){//Storage
        *results_list = h_strdup_cprintf("$@%s%s$%s=%.2f\n", *results_list,
                                     select ? "*" : "", rkey, elbl,
                                     b->bvalue.result);
    } else {//CPU
        *results_list = h_strdup_cprintf("$@%s%s$%s=%.2f|%s\n", *results_list,
                                     select ? "*" : "", rkey, elbl,
                                     b->bvalue.result, b->machine->cpu_config);
    }

    moreinfo_add_with_prefix("BENCH", rkey, bench_result_more_info(b));

    g_free(lbl);
    g_free(elbl);
    g_free(rkey);
    if (*this_marker)
        g_free(this_marker);
}

gint bench_result_sort(gconstpointer a, gconstpointer b)
{
    bench_result *A = (bench_result *)a, *B = (bench_result *)b;
    if (A->bvalue.result < B->bvalue.result)
        return -1;
    if (A->bvalue.result > B->bvalue.result)
        return 1;
    return 0;
}

struct append_machine_result_json_data {
    GSList **result_list;
    const gchar *benchmark_name;
};

static void append_machine_result_json(JsonArray *array,
                                       guint index,
                                       JsonNode *element_node,
                                       gpointer user_data)
{
    struct append_machine_result_json_data *data = user_data;
    bench_result *result;

    result = bench_result_benchmarkjson(data->benchmark_name, element_node);
    *data->result_list = g_slist_append(*data->result_list, result);
}

static GSList *benchmark_include_results_json(const gchar *path,
                                              bench_value r,
                                              const gchar *benchmark)
{
    JsonParser *parser;
    JsonNode *root;
    GSList *result_list = NULL;
    GError *error=NULL;

    DEBUG("Loading benchmark results from JSON file %s", path);

    parser = json_parser_new();
    json_parser_load_from_file(parser, path, &error);
    if(error){
        DEBUG ("Unable to parse JSON %s %s", path, error->message);
        g_error_free(error);
        g_object_unref(parser);
        return result_list;
    }

    root = json_parser_get_root(parser);
    if (!root || (json_node_get_node_type(root) != JSON_NODE_OBJECT))  goto out;

    JsonObject *results = json_node_get_object(root);
    if ( results && json_object_has_member(results,benchmark) ) {
      JsonArray *machines = json_object_get_array_member(results, benchmark);

        if (machines) {
            struct append_machine_result_json_data data = {
                .result_list = &result_list,
                .benchmark_name = benchmark,
            };
            json_array_foreach_element(machines, append_machine_result_json,
                                       &data);
        }
    }

out:
    g_object_unref(parser);

    return result_list;
}

static gchar *find_benchmark_conf(void)
{
    const gchar *config_dir = g_get_user_config_dir();
    gchar *path;

    path = g_build_filename(config_dir, "hardinfo2", "benchmark.json", NULL);
    if (g_file_test(path, G_FILE_TEST_EXISTS))
        return path;
    g_free(path);

    path = g_build_filename(params.path_data, "benchmark.json", NULL);
    if (g_file_test(path, G_FILE_TEST_EXISTS))
        return path;
    g_free(path);

    return NULL;
}

struct bench_window {
    int min, max;
};

static struct bench_window get_bench_window(GSList *result_list,
                                            const bench_result *this_machine)
{
    struct bench_window window = {};
    int size = params.max_bench_results;
    int len = g_slist_length(result_list);

    if (size == 0)
        size = 1;
    else if (size < 0)
        size = len;

    int loc = g_slist_index(result_list, this_machine); /* -1 if not found */
    if (loc >= 0) {
        window.min = loc - size / 2;
        window.max = window.min + size;
        if (window.min < 0) {
            window.min = 0;
            window.max = MIN(size, len);
        } else if (window.max > len) {
            window.max = len;
            window.min = MAX(len - size, 0);
        }
    } else {
        window.min = 0;
        if(params.max_bench_results==0) window.max = 0; else window.max=size;
    }

    //DEBUG("...len: %d, loc: %d, win_size: %d, win: [%d..%d]\n", len, loc, size, window.min, window.max - 1);

    return window;
}

static gboolean is_in_bench_window(const struct bench_window *window, int i)
{
    return i >= window->min && i < window->max;
}

static gchar *benchmark_include_results_internal(bench_value this_machine_value,
                                                 const gchar *benchmark,
                                                 ShellOrderType order_type)
{
    bench_result *this_machine;
    GSList *result_list=NULL, *li;
    gchar *results = g_strdup("");
    gchar *output;
    gchar *path;
    gint i;

    path = find_benchmark_conf();
    if (path) {
        result_list = benchmark_include_results_json(
            path, this_machine_value, benchmark);
    }

    /* this result */
    if (this_machine_value.result > 0.0) {
        this_machine = bench_result_this_machine(benchmark, this_machine_value);
        result_list = g_slist_prepend(result_list, this_machine);
    } else {
        this_machine = NULL;
    }

    /* sort */
    result_list = g_slist_sort(result_list, bench_result_sort);
    if (order_type == SHELL_ORDER_DESCENDING)
        result_list = g_slist_reverse(result_list);

    /* prepare for shell */
    moreinfo_del_with_prefix("BENCH");

    const struct bench_window window = get_bench_window(result_list, this_machine);

    for (i = 0, li = result_list; li; li = g_slist_next(li), i++) {
        bench_result *br = li->data;
        if (is_in_bench_window(&window, i)) br_mi_add(&results, br, br == this_machine);
        bench_result_free(br); /* no longer needed */
    }
    g_slist_free(result_list);
    if(strstr(benchmark,"GPU")){//GPU
        output = g_strdup_printf("[$ShellParam$]\n"
                             "Zebra=1\n"
                             "OrderType=%d\n"
                             "ViewType=4\n"
                             "ColumnTitle$Progress=%s\n"  /* Results */
                             "ColumnTitle$TextValue=%s\n" /* GPU */
                             "ShowColumnHeaders=true\n"
                             "[%s]\n%s",
                             order_type, _("Results"),
                             _("GPU"), benchmark, results);
    }else if(strstr(benchmark,"Storage")){//Storage
        output = g_strdup_printf("[$ShellParam$]\n"
                             "Zebra=1\n"
                             "OrderType=%d\n"
                             "ViewType=4\n"
                             "ColumnTitle$Progress=%s\n"  /* Results */
                             "ColumnTitle$TextValue=%s\n" /* GPU */
                             "ShowColumnHeaders=true\n"
                             "[%s]\n%s",
                             order_type, _("Results"),
                             _("Storage"), benchmark, results);
    } else {//CPU
        output = g_strdup_printf("[$ShellParam$]\n"
                             "Zebra=1\n"
                             "OrderType=%d\n"
                             "ViewType=4\n"
                             "ColumnTitle$Extra1=%s\n"    /* CPU Clock */
                             "ColumnTitle$Progress=%s\n"  /* Results */
                             "ColumnTitle$TextValue=%s\n" /* CPU */
                             "ShowColumnHeaders=true\n"
                             "[%s]\n%s",
                             order_type, _("CPU Config"), _("Results"),
                             _("CPU"), benchmark, results);
    }
    g_free(path);
    g_free(results);

    return output;
}

static gchar *benchmark_include_results_reverse(bench_value result,
                                                const gchar *benchmark)
{
    return benchmark_include_results_internal(result, benchmark,
                                              SHELL_ORDER_DESCENDING);
}

static gchar *benchmark_include_results(bench_value result,
                                        const gchar *benchmark)
{
    return benchmark_include_results_internal(result, benchmark,
                                              SHELL_ORDER_ASCENDING);
}

typedef struct _BenchmarkDialog BenchmarkDialog;
struct _BenchmarkDialog {
    GtkWidget *dialog;
    bench_value r;
};

static gboolean
do_benchmark_handler(GIOChannel *source, GIOCondition condition, gpointer data)
{
    BenchmarkDialog *bench_dialog = (BenchmarkDialog *)data;
    GIOStatus status;
    gchar *result = NULL;
    bench_value r = EMPTY_BENCH_VALUE;

    status = g_io_channel_read_line(source, &result, NULL, NULL, NULL);
    if (status != G_IO_STATUS_NORMAL) {
        DEBUG("error while reading benchmark result");
        r.result = -1.0f;
        if(bench_dialog) bench_dialog->r = r;
	gtk_dialog_response(GTK_DIALOG(bench_dialog->dialog),100);
        return FALSE;
    }

    if(result) r = bench_value_from_str(result);
    if(result && bench_dialog) bench_dialog->r = r;

    g_free(result);
    gtk_dialog_response(GTK_DIALOG(bench_dialog->dialog),GTK_RESPONSE_NONE);
    return FALSE;
}

static gboolean benchmark_update(gpointer user_data){
  if(btotaltimer) shell_status_set_percentage(100*(btotaltimer-btimer)/btotaltimer);
  if(btimer) btimer--;
  return TRUE;
}

static void do_benchmark(void (*benchmark_function)(void), int entry)
{
    int old_priority = 0;

    if (params.skip_benchmarks)
        return;

    if (params.gui_running && !params.run_benchmark) {
        gchar *argv[] = {params.argv0, "-b",entries[entry].name,"-n",params.darkmode?"1":"0",NULL};
        GPid bench_pid;
        gint bench_stdout;
        GtkWidget *bench_dialog = NULL;
        GtkWidget *bench_image;
        BenchmarkDialog *benchmark_dialog = NULL;
        GSpawnFlags spawn_flags = G_SPAWN_STDERR_TO_DEV_NULL;
	gchar *bench_status;
        GtkWidget *content_area, *box, *label;
        bench_value r = EMPTY_BENCH_VALUE;
        GIOChannel *channel=NULL;
        guint watch_id;
	gchar *title;
        gboolean done=FALSE;
	guint btimer_id=0;

        bench_results[entry] = r;

	bench_status = g_strdup_printf(_("Benchmarking: <b>%s</b>."), _(entries[entry].name));
	btotaltimer=entries_btimer[entry];
	btimer=entries_btimer[entry];
	benchmark_update(NULL);
        shell_status_update(bench_status);
	g_free(bench_status);

	title=g_strdup_printf(_("Benchmarking: %s"),_(entries[entry].name));
	bench_dialog = gtk_dialog_new_with_buttons (title,
                                      GTK_WINDOW(shell_get_main_shell()->transient_dialog),
                                      GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
				      _("Stop"), GTK_RESPONSE_ACCEPT,
                                      NULL);
	g_free(title);

	content_area = gtk_dialog_get_content_area (GTK_DIALOG(bench_dialog));

        bench_image = icon_cache_get_image_at_size(entries[entry].icon, 64, 64);

#if GTK_CHECK_VERSION(3,0,0)
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
#else
	box = gtk_hbox_new(FALSE, 1);
#endif
	label = gtk_label_new (_("Please do not move your mouse\nor press any keys."));

#if GTK_CHECK_VERSION(3,0,0)
	gtk_widget_set_halign (bench_image, GTK_ALIGN_START);
#else
        gtk_misc_set_alignment(GTK_MISC(bench_image), 0.0, 0.0);
#endif

	gtk_box_pack_start (GTK_BOX(box), bench_image, TRUE, TRUE, 10);
	gtk_box_pack_start (GTK_BOX(box), label, TRUE, TRUE, 10);
	gtk_container_add (GTK_CONTAINER(content_area), box);

	gtk_window_set_deletable(GTK_WINDOW(bench_dialog), FALSE);
	gtk_widget_show_all (bench_dialog);

        benchmark_dialog = g_new0(BenchmarkDialog, 1);
        benchmark_dialog->dialog = bench_dialog;
        benchmark_dialog->r = r;

        if (!g_path_is_absolute(params.argv0)) {
            spawn_flags |= G_SPAWN_SEARCH_PATH;
        }

        if (g_spawn_async_with_pipes(NULL, argv, NULL, spawn_flags, NULL, NULL,
                                     &bench_pid, NULL, &bench_stdout, NULL,
                                     NULL)) {
	    btimer_id=g_timeout_add(1000,benchmark_update,NULL);

            channel = g_io_channel_unix_new(bench_stdout);
            watch_id = g_io_add_watch(channel, G_IO_IN, do_benchmark_handler, benchmark_dialog);

            switch (gtk_dialog_run(GTK_DIALOG(benchmark_dialog->dialog))) {
            case GTK_RESPONSE_NONE:
                if(benchmark_dialog) bench_results[entry] = benchmark_dialog->r;
		done=TRUE;
                break;
	    case GTK_RESPONSE_ACCEPT:
		break;
	    }

            if(!done) if(watch_id) g_source_remove(watch_id);
            if(!done) kill(bench_pid, SIGINT);
	    if(!done) params.aborting_benchmarks=1;

            g_io_channel_unref(channel);
            if(benchmark_dialog && benchmark_dialog->dialog) gtk_widget_destroy(benchmark_dialog->dialog);
            g_free(benchmark_dialog);
	    g_source_remove(btimer_id);

            return;
        }
        if(benchmark_dialog && benchmark_dialog->dialog) gtk_widget_destroy(benchmark_dialog->dialog);
        g_free(benchmark_dialog);
	if(btimer_id) g_source_remove(btimer_id);
        return;
    }

    setpriority(PRIO_PROCESS, 0, -20);
    benchmark_function();
    setpriority(PRIO_PROCESS, 0, old_priority);
}

gchar *hi_module_get_name(void) { return _("Benchmarks"); }

guchar hi_module_get_weight(void) { return 240; }

ModuleEntry *hi_module_get_entries(void) { return entries; }

const ModuleAbout *hi_module_get_about(void)
{
    static const ModuleAbout ma = {
        .author = "L. A. F. Pereira",
        .description = N_("Perform tasks and compare with other systems"),
        .version = VERSION,
        .license = "GNU GPL version 2 or later.",
    };

    return &ma;
}

static gchar *get_benchmark_results(gsize *len)
{
    void (*scan_callback)(gboolean);
    JsonBuilder *builder;
    JsonGenerator *generator;
    bench_machine *this_machine;
    gchar *out;
    guint i;

    for (i = 0; i < G_N_ELEMENTS(entries); i++) {
        if (!entries[i].name || !entries[i].scan_callback)
            continue;
        if (entries[i].flags & MODULE_FLAG_HIDE)
            continue;

        scan_callback = entries[i].scan_callback;
        if (scan_callback)
            scan_callback(bench_results[i].result < 0.0);
    }

    this_machine = bench_machine_this();
    builder = json_builder_new();
    json_builder_begin_object(builder);
    for (i = 0; i < G_N_ELEMENTS(entries); i++) {
        if (!entries[i].name || entries[i].flags & MODULE_FLAG_HIDE)
            continue;
        if (bench_results[i].result < 0.0) {
            /* Benchmark failed? */
            continue;
        }

        json_builder_set_member_name(builder, entries_english_name[i]);

        json_builder_begin_object(builder);

#define ADD_JSON_VALUE(type, name, value)                                      \
    do {                                                                       \
        json_builder_set_member_name(builder, (name));                         \
        json_builder_add_##type##_value(builder, (value));                     \
    } while (0)

        ADD_JSON_VALUE(string, "Board", this_machine->board);
        ADD_JSON_VALUE(int, "MemoryInKiB", this_machine->memory_kiB);
        ADD_JSON_VALUE(string, "CpuName", this_machine->cpu_name);
        ADD_JSON_VALUE(string, "CpuDesc", this_machine->cpu_desc);
        ADD_JSON_VALUE(string, "CpuConfig", this_machine->cpu_config);
        ADD_JSON_VALUE(string, "CpuConfig", this_machine->cpu_config);
        ADD_JSON_VALUE(string, "OpenGlRenderer", this_machine->ogl_renderer);
        ADD_JSON_VALUE(string, "GpuDesc", this_machine->gpu_desc);
        ADD_JSON_VALUE(int, "NumCpus", this_machine->processors);
        ADD_JSON_VALUE(int, "NumCores", this_machine->cores);
        ADD_JSON_VALUE(int, "NumNodes", this_machine->nodes);
        ADD_JSON_VALUE(int, "NumThreads", this_machine->threads);
        ADD_JSON_VALUE(string, "MachineId", this_machine->mid);
        ADD_JSON_VALUE(int, "PointerBits", this_machine->ptr_bits);
        ADD_JSON_VALUE(string, "HwCAPS", this_machine->cpu_hwcaps);
        ADD_JSON_VALUE(boolean, "DataFromSuperUser", this_machine->is_su_data);
        ADD_JSON_VALUE(int, "PhysicalMemoryInMiB",
                       this_machine->memory_phys_MiB);
        ADD_JSON_VALUE(string, "MemoryTypes", this_machine->ram_types);
        ADD_JSON_VALUE(int, "MachineDataVersion",
                       this_machine->machine_data_version);
        ADD_JSON_VALUE(string, "MachineType", this_machine->machine_type);
        ADD_JSON_VALUE(string, "LinuxKernel", this_machine->linux_kernel);
        ADD_JSON_VALUE(string, "LinuxOS", this_machine->linux_os);
        ADD_JSON_VALUE(boolean, "Legacy", FALSE);
        ADD_JSON_VALUE(string, "ExtraInfo", bench_results[i].extra);
	if(params.bench_user_note){
            ADD_JSON_VALUE(string, "UserNote", params.bench_user_note);
	}else{
            ADD_JSON_VALUE(string, "UserNote", "");
	}
        ADD_JSON_VALUE(double, "BenchmarkResult", bench_results[i].result);
        ADD_JSON_VALUE(double, "ElapsedTime", bench_results[i].elapsed_time);
        ADD_JSON_VALUE(int, "UsedThreads", bench_results[i].threads_used);
        ADD_JSON_VALUE(int, "BenchmarkVersion", bench_results[i].revision);
        ADD_JSON_VALUE(string, "PowerState", this_machine->power_state);
        ADD_JSON_VALUE(string, "GPU", this_machine->gpu_name);
        ADD_JSON_VALUE(string, "Storage", this_machine->storage);
        ADD_JSON_VALUE(string, "VulkanDriver", this_machine->vulkanDriver);
        ADD_JSON_VALUE(string, "VulkanDevice", this_machine->vulkanDevice);
        ADD_JSON_VALUE(string, "VulkanVersions", this_machine->vulkanVersions);

#undef ADD_JSON_VALUE

        json_builder_end_object(builder);
    }
    json_builder_end_object(builder);

    generator = json_generator_new();
    json_generator_set_root(generator, json_builder_get_root(builder));
    json_generator_set_pretty(generator, TRUE);

    out = json_generator_to_data(generator, len);

    g_object_unref(generator);
    g_object_unref(builder);
    bench_machine_free(this_machine);

    return out;
}

static gchar *run_benchmark(gchar *name)
{
    int i;
    for (i = 0; entries[i].name; i++) {
        if (g_str_equal(entries[i].name, name)) {
            void (*scan_callback)(gboolean rescan);

            if ((scan_callback = entries[i].scan_callback)) {
                scan_callback(FALSE);

#define CHK_RESULT_FORMAT(F)                                                   \
    (params.result_format && strcmp(params.result_format, F) == 0)

                if (params.run_benchmark) {
                    if (CHK_RESULT_FORMAT("shell")) {
                        bench_result *b =
                            bench_result_this_machine(name, bench_results[i]);
                        char *temp = bench_result_more_info_complete(b);
                        bench_result_free(b);
                        return temp;
                    }
                    /* defaults to "short" which is below */
                }

                return bench_value_to_str(bench_results[i]);
            }
        }
    }
    return NULL;
}

const ShellModuleMethod *hi_exported_methods(void)
{
    static const ShellModuleMethod m[] = {
        {"runBenchmark", run_benchmark},
        {NULL},
    };

    return m;
}

void hi_module_init(void)
{
    static SyncEntry se[] = {
        {
            .name = N_("Send benchmark results"),
            .file_name = "benchmark.json",
            .generate_contents_for_upload = get_benchmark_results,
	    .optional = FALSE,
        },
        {
            .name = N_("Receive benchmark results"),
            .file_name = "benchmark.json",
	    .optional = FALSE,
        },
    };

    sync_manager_add_entry(&se[1]);
    sync_manager_add_entry(&se[0]);

    guint i;
    for (i = 0; i < G_N_ELEMENTS(entries) - 1 /* account for NULL */; i++)
        bench_results[i] = (bench_value)EMPTY_BENCH_VALUE;
}

gchar **hi_module_get_dependencies(void)
{
    static gchar *deps[] = {"devices.so", NULL};

    return deps;
}
