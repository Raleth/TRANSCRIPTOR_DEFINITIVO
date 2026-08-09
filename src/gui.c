#include "gui.h"

#include "batch.h"
#include "config.h"
#include "process.h"
#include "formats.h"
#include "transcribe.h"

#include <stdarg.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Estado de la aplicación
 * ------------------------------------------------------------------------- */

typedef struct {
    GtkWidget      *window;
    GtkWidget      *entry_input;
    GtkWidget      *entry_output;
    GtkWidget      *entry_model;
    GtkWidget      *combo_lang;
    GtkWidget      *combo_format;

    /* Preferencias (pestaña con guardado automático). */
    GtkWidget      *entry_pref_model;
    GtkWidget      *entry_pref_output;
    GtkWidget      *combo_pref_lang;
    GtkWidget      *combo_pref_format;
    guint           save_timeout_id;

    GtkWidget      *list_box;         /* GtkBox vertical con las filas        */
    GtkWidget      *list_placeholder; /* etiqueta mostrada cuando no hay nada  */
    GPtrArray      *rows;             /* GtkWidget* filas de la lista         */
    GPtrArray      *files;            /* gchar* rutas absolutas (free func)   */

    GtkWidget      *progress;
    GtkWidget      *label_status;
    GtkTextBuffer  *log_buffer;
    GtkWidget      *btn_process;

    /* Estado del procesamiento en segundo plano. */
    gboolean        processing;
    gchar         **files_snapshot;
    int             n_files_snapshot;
    gchar          *output_dir_snapshot;
    gchar          *model_snapshot;
    gchar          *language_snapshot;
    gchar          *format_id_snapshot;
} AppState;

/* ---------------------------------------------------------------------------
 * Prototipos (para no depender del orden de definición)
 * ------------------------------------------------------------------------- */

static void     log_line          (AppState *st, const char *fmt, ...);
static void     update_status     (AppState *st, const char *text);
static void     update_file_count (AppState *st);
static void     add_row_for_path  (AppState *st, const char *path);
static void     clear_rows        (AppState *st);
static void     set_files         (AppState *st, GPtrArray *files);
static GtkWidget *make_file_row   (AppState *st, const char *path);

/* ---------------------------------------------------------------------------
 * Utilidades de UI
 * ------------------------------------------------------------------------- */

static void
log_line (AppState *st, const char *fmt, ...)
{
    GDateTime *now = g_date_time_new_now_local ();
    gchar *ts = g_date_time_format (now, "%H:%M:%S");
    g_date_time_unref (now);

    va_list ap;
    va_start (ap, fmt);
    gchar *msg = g_strdup_vprintf (fmt, ap);
    va_end (ap);

    gchar *line = g_strdup_printf ("[%s] %s\n", ts, msg);
    g_free (ts);
    g_free (msg);

    GtkTextIter iter;
    gtk_text_buffer_get_end_iter (st->log_buffer, &iter);
    gtk_text_buffer_insert (st->log_buffer, &iter, line, -1);

    g_print ("%s", line);
    g_free (line);
}

static void
update_status (AppState *st, const char *text)
{
    gtk_label_set_text (GTK_LABEL (st->label_status), text);
}

static void
update_file_count (AppState *st)
{
    gchar *msg = g_strdup_printf ("%u archivo(s) en cola.", st->files->len);
    update_status (st, msg);
    g_free (msg);
}

/* Maneja el error de un diálogo de archivos; devuelve TRUE si fue cancelado. */
static gboolean
handle_dialog_error (AppState *st, GError *err, const char *action)
{
    if (err == NULL)
        return FALSE;

    gboolean cancelled =
        g_error_matches (err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) ||
        g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED);

    if (!cancelled)
        log_line (st, "Error al %s: %s", action, err->message);

    g_error_free (err);
    return cancelled;
}

/* ---------------------------------------------------------------------------
 * Lista de archivos
 * ------------------------------------------------------------------------- */

static void
on_remove_row (GtkButton *button, gpointer user_data)
{
    GtkWidget *row = GTK_WIDGET (user_data);
    AppState *st = g_object_get_data (G_OBJECT (row), "state");
    const char *path = g_object_get_data (G_OBJECT (row), "path");

    for (guint i = 0; i < st->files->len; i++) {
        const char *item = g_ptr_array_index (st->files, i);
        if (g_strcmp0 (item, path) == 0) {
            g_ptr_array_remove_index (st->files, i); /* free func libera la cadena */
            break;
        }
    }

    for (guint i = 0; i < st->rows->len; i++) {
        if (g_ptr_array_index (st->rows, i) == row) {
            g_ptr_array_remove_index (st->rows, i); /* no libera (GTK posee el widget) */
            break;
        }
    }

    gtk_box_remove (GTK_BOX (st->list_box), row);
    gtk_widget_set_visible (st->list_placeholder, st->files->len == 0);
    update_file_count (st);
}

static GtkWidget *
make_file_row (AppState *st, const char *path)
{
    gchar *name = g_path_get_basename (path);

    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top (row, 2);
    gtk_widget_set_margin_bottom (row, 2);

    GtkWidget *label = gtk_label_new (name);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
    gtk_widget_set_hexpand (label, TRUE);
    gtk_widget_set_tooltip_text (label, path);

    GtkWidget *btn = gtk_button_new_from_icon_name ("window-close-symbolic");
    gtk_widget_set_tooltip_text (btn, "Quitar de la lista");

    gtk_box_append (GTK_BOX (row), label);
    gtk_box_append (GTK_BOX (row), btn);

    g_object_set_data_full (G_OBJECT (row), "path", g_strdup (path), g_free);
    g_object_set_data (G_OBJECT (row), "state", st);

    g_signal_connect (btn, "clicked", G_CALLBACK (on_remove_row), row);

    g_free (name);
    return row;
}

static void
add_row_for_path (AppState *st, const char *path)
{
    GtkWidget *row = make_file_row (st, path);
    gtk_box_append (GTK_BOX (st->list_box), row);
    g_ptr_array_add (st->rows, row);
    gtk_widget_set_visible (st->list_placeholder, FALSE);
}

static void
clear_rows (AppState *st)
{
    for (guint i = 0; i < st->rows->len; i++) {
        GtkWidget *row = g_ptr_array_index (st->rows, i);
        gtk_box_remove (GTK_BOX (st->list_box), row);
    }
    g_ptr_array_set_size (st->rows, 0);
    gtk_widget_set_visible (st->list_placeholder, TRUE);
}

/* Toma posesión de `files` (GPtrArray con free func). */
static void
set_files (AppState *st, GPtrArray *files)
{
    if (st->files != NULL)
        g_ptr_array_free (st->files, TRUE);
    st->files = files;

    clear_rows (st);
    for (guint i = 0; i < st->files->len; i++)
        add_row_for_path (st, g_ptr_array_index (st->files, i));

    update_file_count (st);
}

/* ---------------------------------------------------------------------------
 * Diálogos de archivos (GtkFileDialog, async)
 * ------------------------------------------------------------------------- */

static void
on_input_folder_finish (GObject *source, GAsyncResult *res, gpointer user_data)
{
    AppState *st = user_data;
    GError *err = NULL;
    GFile *folder = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source), res, &err);

    if (handle_dialog_error (st, err, "seleccionar carpeta"))
        return;

    gchar *path = g_file_get_path (folder);
    g_object_unref (folder);

    gtk_editable_set_text (GTK_EDITABLE (st->entry_input), path);

    /* carpeta de salida por defecto: <carpeta>/transcripciones */
    gchar *out = g_build_filename (path, config_get_output_dir (), NULL);
    gtk_editable_set_text (GTK_EDITABLE (st->entry_output), out);
    g_free (out);

    GError *scan_err = NULL;
    GPtrArray *files = batch_scan_folder (path, &scan_err);
    if (scan_err != NULL) {
        log_line (st, "Error al escanear %s: %s", path, scan_err->message);
        g_error_free (scan_err);
        g_free (path);
        return;
    }

    set_files (st, files);
    log_line (st, "Carpeta de entrada: %s (%u archivo(s) de audio/video).",
              path, files->len);
    g_free (path);
}

static void
on_pick_input_folder (GtkButton *button, gpointer user_data)
{
    AppState *st = user_data;
    GtkFileDialog *dlg = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dlg, "Seleccionar carpeta de entrada");
    gtk_file_dialog_select_folder (dlg, GTK_WINDOW (st->window), NULL,
                                   on_input_folder_finish, st);
}

static void
on_input_files_finish (GObject *source, GAsyncResult *res, gpointer user_data)
{
    AppState *st = user_data;
    GError *err = NULL;
    GListModel *model = gtk_file_dialog_open_multiple_finish (GTK_FILE_DIALOG (source), res, &err);

    if (handle_dialog_error (st, err, "agregar archivos"))
        return;

    guint n = g_list_model_get_n_items (model);
    GPtrArray *paths = g_ptr_array_new_with_free_func (g_free);

    for (guint i = 0; i < n; i++) {
        GFile *file = g_list_model_get_item (model, i);
        gchar *p = g_file_get_path (file);
        g_object_unref (file);
        if (p != NULL)
            g_ptr_array_add (paths, p);
    }
    g_object_unref (model);

    GPtrArray *media = batch_filter_file_list ((char **) paths->pdata, (int) paths->len);
    g_ptr_array_free (paths, TRUE);

    for (guint i = 0; i < media->len; i++) {
        const char *p = g_ptr_array_index (media, i);
        gboolean duplicate = FALSE;
        for (guint j = 0; j < st->files->len; j++) {
            if (g_strcmp0 (g_ptr_array_index (st->files, j), p) == 0) {
                duplicate = TRUE;
                break;
            }
        }
        if (!duplicate) {
            g_ptr_array_add (st->files, g_strdup (p));
            add_row_for_path (st, p);
        }
    }

    batch_free_file_list (media);
    log_line (st, "Archivos agregados a la cola.");
    update_file_count (st);
}

static void
on_pick_input_files (GtkButton *button, gpointer user_data)
{
    AppState *st = user_data;
    GtkFileDialog *dlg = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dlg, "Agregar archivos de audio/video");
    gtk_file_dialog_open_multiple (dlg, GTK_WINDOW (st->window), NULL,
                                   on_input_files_finish, st);
}

static void
on_output_folder_finish (GObject *source, GAsyncResult *res, gpointer user_data)
{
    AppState *st = user_data;
    GError *err = NULL;
    GFile *folder = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source), res, &err);

    if (handle_dialog_error (st, err, "seleccionar carpeta de salida"))
        return;

    gchar *path = g_file_get_path (folder);
    g_object_unref (folder);
    gtk_editable_set_text (GTK_EDITABLE (st->entry_output), path);
    g_free (path);
}

static void
on_pick_output_folder (GtkButton *button, gpointer user_data)
{
    AppState *st = user_data;
    GtkFileDialog *dlg = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dlg, "Seleccionar carpeta de salida");
    gtk_file_dialog_select_folder (dlg, GTK_WINDOW (st->window), NULL,
                                   on_output_folder_finish, st);
}

static void
on_model_file_finish (GObject *source, GAsyncResult *res, gpointer user_data)
{
    AppState *st = user_data;
    GError *err = NULL;
    GFile *file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), res, &err);

    if (handle_dialog_error (st, err, "seleccionar modelo"))
        return;

    gchar *path = g_file_get_path (file);
    g_object_unref (file);
    gtk_editable_set_text (GTK_EDITABLE (st->entry_model), path);
    g_free (path);
}

static void
on_pick_model_file (GtkButton *button, gpointer user_data)
{
    AppState *st = user_data;
    GtkFileDialog *dlg = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dlg, "Seleccionar modelo de whisper (.bin)");
    gtk_file_dialog_open (dlg, GTK_WINDOW (st->window), NULL,
                          on_model_file_finish, st);
}

/* ---------------------------------------------------------------------------
 * Procesamiento en segundo plano (hilo de trabajo)
 * ------------------------------------------------------------------------- */

typedef struct {
    AppState *st;
    gboolean  final;
    int       done;
    int       total;
    int       ok_count;
    char     *file;
    gboolean  success;
    char     *message;
} UiUpdate;

/* Actualización de la barra de progreso (se crea en el hilo de trabajo). */
typedef struct {
    AppState *st;
    int       files_done;    /* archivos ya completados */
    int       total_files;   /* total del lote */
    int       file_percent;  /* 0-100 dentro del archivo actual */
    char     *file;          /* archivo actual (puede ser NULL) */
    char     *phase;         /* "modelo", "convirtiendo", "transcribiendo", "listo" */
} ProgressUpdate;

static gboolean
on_progress_idle (gpointer user_data)
{
    ProgressUpdate *p = user_data;
    AppState *st = p->st;

    /* whisper puede reportar valores fuera de [0,100] en los bordes. */
    int file_pct = CLAMP (p->file_percent, 0, 100);
    double fraction = (p->total_files > 0)
                          ? ((double) p->files_done + (double) file_pct / 100.0)
                                / (double) p->total_files
                          : 0.0;

    gchar *text;
    if (g_strcmp0 (p->phase, "modelo") == 0) {
        text = g_strdup ("Cargando modelo…");
    } else {
        int pct = (int) (fraction * 100.0 + 0.5);
        gchar *fname = (p->file != NULL) ? g_path_get_basename (p->file) : NULL;
        text = g_strdup_printf ("%d/%d · %d%% — %s (%s)",
                                p->files_done + 1, p->total_files, pct,
                                (fname != NULL) ? fname : "?",
                                (p->phase != NULL) ? p->phase : "");
        g_free (fname);
    }

    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st->progress), fraction);
    gtk_progress_bar_set_text (GTK_PROGRESS_BAR (st->progress), text);
    update_status (st, text);
    g_free (text);
    return G_SOURCE_REMOVE;
}

static void
on_progress_idle_destroy (gpointer user_data)
{
    ProgressUpdate *p = user_data;
    g_free (p->file);
    g_free (p->phase);
    g_free (p);
}

static void
on_process_progress (int files_done, int total_files, int file_percent,
                     const char *current_file, const char *phase, gpointer user_data)
{
    AppState *st = user_data;

    ProgressUpdate *p = g_new0 (ProgressUpdate, 1);
    p->st           = st;
    p->files_done   = files_done;
    p->total_files  = total_files;
    p->file_percent = file_percent;
    p->file         = g_strdup (current_file);
    p->phase        = g_strdup (phase);

    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, on_progress_idle, p, on_progress_idle_destroy);
}

static gboolean
on_process_idle (gpointer user_data)
{
    UiUpdate *u = user_data;
    AppState *st = u->st;

    if (u->final) {
        gchar *msg = g_strdup_printf ("Proceso terminado: %d de %d archivo(s) convertidos.",
                                      u->ok_count, u->total);
        log_line (st, "%s", msg);
        update_status (st, msg);
        g_free (msg);

        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st->progress), 1.0);
        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (st->progress), "Completado");
        gtk_widget_set_sensitive (st->btn_process, TRUE);
        st->processing = FALSE;

        for (int i = 0; i < st->n_files_snapshot; i++)
            g_free (st->files_snapshot[i]);
        g_free (st->files_snapshot);
        st->files_snapshot = NULL;
        st->n_files_snapshot = 0;
        g_free (st->output_dir_snapshot);
        st->output_dir_snapshot = NULL;
        g_free (st->model_snapshot);
        st->model_snapshot = NULL;
        g_free (st->language_snapshot);
        st->language_snapshot = NULL;
        g_free (st->format_id_snapshot);
        st->format_id_snapshot = NULL;

        return G_SOURCE_REMOVE;
    }

    gchar *msg = g_strdup_printf ("[%d/%d] %s", u->done, u->total, u->file);
    log_line (st, "%s -> %s", msg, u->success ? "OK (WAV)" : "ERROR");
    if (!u->success && u->message != NULL && *u->message != '\0')
        log_line (st, "    %s", u->message);
    update_status (st, msg);
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st->progress),
                                   (double) u->done / (double) u->total);
    g_free (msg);
    return G_SOURCE_REMOVE;
}

static void
on_process_idle_destroy (gpointer user_data)
{
    UiUpdate *u = user_data;
    g_free (u->file);
    g_free (u->message);
    g_free (u);
}

static void
on_process_report (const ProcessReport *r, gpointer user_data)
{
    AppState *st = user_data;

    UiUpdate *u = g_new0 (UiUpdate, 1);
    u->st      = st;
    u->done    = r->done;
    u->total   = r->total;
    u->success = r->success;
    u->file    = g_strdup (r->file);
    u->message = g_strdup (r->message);

    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, on_process_idle, u, on_process_idle_destroy);
}

static gpointer
process_worker (gpointer user_data)
{
    AppState *st = user_data;

    const FormatWriter *writer = formats_from_string (st->format_id_snapshot);
    int ok = process_run_batch ((const char *const *) st->files_snapshot,
                                st->n_files_snapshot,
                                st->output_dir_snapshot,
                                st->model_snapshot,
                                st->language_snapshot,
                                writer,
                                on_process_progress,
                                on_process_report, st);

    UiUpdate *u = g_new0 (UiUpdate, 1);
    u->st       = st;
    u->final    = TRUE;
    u->ok_count = ok;
    u->total    = st->n_files_snapshot;
    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, on_process_idle, u, on_process_idle_destroy);

    return NULL;
}

static gboolean
on_close_request (GtkWidget *widget, gpointer user_data)
{
    AppState *st = user_data;

    if (st->processing) {
        update_status (st, "Procesamiento en curso; espera a que termine.");
        return TRUE; /* veta el cierre mientras se procesa */
    }
    return FALSE;
}

/* ---------------------------------------------------------------------------
 * Acciones
 * ------------------------------------------------------------------------- */

static void
on_process (GtkButton *button, gpointer user_data)
{
    AppState *st = user_data;

    if (st->processing) {
        update_status (st, "Ya hay un procesamiento en curso.");
        return;
    }

    if (st->files->len == 0) {
        update_status (st, "No hay archivos en la cola. Selecciona una carpeta o agrega archivos.");
        return;
    }

    const char *out_dir = gtk_editable_get_text (GTK_EDITABLE (st->entry_output));
    if (out_dir == NULL || *out_dir == '\0') {
        update_status (st, "Indica una carpeta de salida primero.");
        return;
    }

    const char *model = gtk_editable_get_text (GTK_EDITABLE (st->entry_model));
    if (model == NULL || *model == '\0') {
        update_status (st, "Selecciona el modelo de whisper (.bin) o define WHISPER_MODEL.");
        return;
    }

    /* Snapshot para el hilo de trabajo (el hilo no toca widgets). */
    st->n_files_snapshot = (int) st->files->len;
    st->files_snapshot = g_new0 (gchar *, (guint) st->n_files_snapshot);
    for (int i = 0; i < st->n_files_snapshot; i++)
        st->files_snapshot[i] = g_strdup (g_ptr_array_index (st->files, (guint) i));
    st->output_dir_snapshot = g_strdup (out_dir);
    st->model_snapshot = g_strdup (model);

    const char *lang = gtk_string_list_get_string (
        GTK_STRING_LIST (gtk_drop_down_get_model (GTK_DROP_DOWN (st->combo_lang))),
        gtk_drop_down_get_selected (GTK_DROP_DOWN (st->combo_lang)));
    const char *fmt = gtk_string_list_get_string (
        GTK_STRING_LIST (gtk_drop_down_get_model (GTK_DROP_DOWN (st->combo_format))),
        gtk_drop_down_get_selected (GTK_DROP_DOWN (st->combo_format)));

    st->language_snapshot = g_strdup (lang);
    st->format_id_snapshot = g_strdup (fmt);

    st->processing = TRUE;
    gtk_widget_set_sensitive (st->btn_process, FALSE);
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st->progress), 0.0);

    log_line (st, "--- Procesamiento (ffmpeg -> whisper) ---");
    log_line (st, "Carpeta de salida: %s", out_dir);
    log_line (st, "Modelo whisper    : %s", model);
    log_line (st, "Idioma / Formato  : %s / %s", lang, fmt);
    log_line (st, "Archivos en cola  : %d", st->n_files_snapshot);

    update_status (st, "Procesando…");
    g_thread_new ("process", process_worker, st);
}



static void
on_clear (GtkButton *button, gpointer user_data)
{
    AppState *st = user_data;

    g_ptr_array_set_size (st->files, 0);
    clear_rows (st);
    log_line (st, "Lista vaciada.");
    update_status (st, "Lista vaciada.");
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st->progress), 0.0);
}

/* ---------------------------------------------------------------------------
 * Preferencias (pestaña con guardado automático)
 * ------------------------------------------------------------------------- */

/* Índice de `value` dentro de un GtkStringList (insensible); fallback si no está. */
static guint
combo_string_index (GtkStringList *list, const char *value, guint fallback)
{
    if (value != NULL) {
        guint n = g_list_model_get_n_items (G_LIST_MODEL (list));
        for (guint i = 0; i < n; i++) {
            const char *s = gtk_string_list_get_string (list, i);
            if (g_ascii_strcasecmp (s, value) == 0)
                return i;
        }
    }
    return fallback;
}

static gboolean
save_config_cb (gpointer user_data)
{
    AppState *st = user_data;
    st->save_timeout_id = 0;
    config_save ();
    return G_SOURCE_REMOVE;
}

static void
schedule_config_save (AppState *st)
{
    if (st->save_timeout_id != 0)
        g_source_remove (st->save_timeout_id);
    st->save_timeout_id = g_timeout_add (400, save_config_cb, st);
}

static void
on_pref_model_changed (GtkEditable *editable, gpointer user_data)
{
    AppState *st = user_data;
    const char *v = gtk_editable_get_text (editable);
    config_set_whisper_model (v);
    gtk_editable_set_text (GTK_EDITABLE (st->entry_model), v);
    schedule_config_save (st);
}

static void
on_pref_output_changed (GtkEditable *editable, gpointer user_data)
{
    AppState *st = user_data;
    const char *v = gtk_editable_get_text (editable);
    config_set_output_dir (v);
    gtk_editable_set_text (GTK_EDITABLE (st->entry_output), v);
    schedule_config_save (st);
}

static void
on_pref_lang_changed (GObject *gobject, GParamSpec *pspec, gpointer user_data)
{
    AppState *st = user_data;
    GtkDropDown *dropdown = GTK_DROP_DOWN (gobject);
    const char *v = gtk_string_list_get_string (
        GTK_STRING_LIST (gtk_drop_down_get_model (dropdown)),
        gtk_drop_down_get_selected (dropdown));

    config_set_transcript_lang (v);
    gtk_drop_down_set_selected (
        GTK_DROP_DOWN (st->combo_lang),
        combo_string_index (GTK_STRING_LIST (gtk_drop_down_get_model (GTK_DROP_DOWN (st->combo_lang))),
                            v, 0));
    schedule_config_save (st);
}

static void
on_pref_format_changed (GObject *gobject, GParamSpec *pspec, gpointer user_data)
{
    AppState *st = user_data;
    GtkDropDown *dropdown = GTK_DROP_DOWN (gobject);
    const char *v = gtk_string_list_get_string (
        GTK_STRING_LIST (gtk_drop_down_get_model (dropdown)),
        gtk_drop_down_get_selected (dropdown));

    config_set_transcript_format (v);
    gtk_drop_down_set_selected (
        GTK_DROP_DOWN (st->combo_format),
        combo_string_index (GTK_STRING_LIST (gtk_drop_down_get_model (GTK_DROP_DOWN (st->combo_format))),
                            v, 0));
    schedule_config_save (st);
}

static void
on_pref_model_finish (GObject *source, GAsyncResult *res, gpointer user_data)
{
    AppState *st = user_data;
    GError *err = NULL;
    GFile *file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), res, &err);
    if (handle_dialog_error (st, err, "seleccionar modelo"))
        return;
    gchar *path = g_file_get_path (file);
    g_object_unref (file);
    gtk_editable_set_text (GTK_EDITABLE (st->entry_pref_model), path);
    g_free (path);
}

static void
on_pick_pref_model (GtkButton *button, gpointer user_data)
{
    AppState *st = user_data;
    GtkFileDialog *dlg = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dlg, "Seleccionar modelo de whisper (.bin)");
    gtk_file_dialog_open (dlg, GTK_WINDOW (st->window), NULL,
                          on_pref_model_finish, st);
}

static void
on_pref_output_finish (GObject *source, GAsyncResult *res, gpointer user_data)
{
    AppState *st = user_data;
    GError *err = NULL;
    GFile *folder = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source), res, &err);
    if (handle_dialog_error (st, err, "seleccionar carpeta de salida"))
        return;
    gchar *path = g_file_get_path (folder);
    g_object_unref (folder);
    gtk_editable_set_text (GTK_EDITABLE (st->entry_pref_output), path);
    g_free (path);
}

static void
on_pick_pref_output (GtkButton *button, gpointer user_data)
{
    AppState *st = user_data;
    GtkFileDialog *dlg = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dlg, "Seleccionar carpeta de salida");
    gtk_file_dialog_select_folder (dlg, GTK_WINDOW (st->window), NULL,
                                   on_pref_output_finish, st);
}

static void
build_preferences_tab (AppState *st, GtkNotebook *notebook)
{
    GtkWidget *pref = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top (pref, 12);
    gtk_widget_set_margin_bottom (pref, 12);
    gtk_widget_set_margin_start (pref, 12);
    gtk_widget_set_margin_end (pref, 12);

    /* Modelo de whisper */
    GtkWidget *row_model = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    st->entry_pref_model = gtk_entry_new ();
    gtk_widget_set_hexpand (st->entry_pref_model, TRUE);
    gtk_entry_set_placeholder_text (GTK_ENTRY (st->entry_pref_model),
                                    "Ruta del modelo .bin preferido");
    gtk_editable_set_text (GTK_EDITABLE (st->entry_pref_model),
                           config_get_whisper_model ());
    GtkWidget *btn_model = gtk_button_new_with_label ("…");
    g_signal_connect (btn_model, "clicked", G_CALLBACK (on_pick_pref_model), st);
    g_signal_connect (st->entry_pref_model, "changed",
                      G_CALLBACK (on_pref_model_changed), st);
    gtk_box_append (GTK_BOX (row_model), gtk_label_new ("Modelo de whisper:"));
    gtk_box_append (GTK_BOX (row_model), st->entry_pref_model);
    gtk_box_append (GTK_BOX (row_model), btn_model);
    gtk_box_append (GTK_BOX (pref), row_model);

    /* Carpeta de salida */
    GtkWidget *row_out = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    st->entry_pref_output = gtk_entry_new ();
    gtk_widget_set_hexpand (st->entry_pref_output, TRUE);
    gtk_entry_set_placeholder_text (GTK_ENTRY (st->entry_pref_output),
                                    "Carpeta donde guardar las transcripciones");
    gtk_editable_set_text (GTK_EDITABLE (st->entry_pref_output),
                           config_get_output_dir ());
    GtkWidget *btn_out = gtk_button_new_with_label ("…");
    g_signal_connect (btn_out, "clicked", G_CALLBACK (on_pick_pref_output), st);
    g_signal_connect (st->entry_pref_output, "changed",
                      G_CALLBACK (on_pref_output_changed), st);
    gtk_box_append (GTK_BOX (row_out), gtk_label_new ("Carpeta de salida:"));
    gtk_box_append (GTK_BOX (row_out), st->entry_pref_output);
    gtk_box_append (GTK_BOX (row_out), btn_out);
    gtk_box_append (GTK_BOX (pref), row_out);

    /* Idioma por defecto */
    GtkWidget *row_lang = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkStringList *langs =
        gtk_string_list_new ((const char *[]) { "auto", "es", "en", NULL });
    st->combo_pref_lang = gtk_drop_down_new (G_LIST_MODEL (langs), NULL);
    gtk_drop_down_set_selected (GTK_DROP_DOWN (st->combo_pref_lang),
                                combo_string_index (langs, config_get_transcript_lang (), 0));
    g_signal_connect (st->combo_pref_lang, "notify::selected",
                      G_CALLBACK (on_pref_lang_changed), st);
    gtk_box_append (GTK_BOX (row_lang), gtk_label_new ("Idioma por defecto:"));
    gtk_box_append (GTK_BOX (row_lang), st->combo_pref_lang);
    gtk_box_append (GTK_BOX (pref), row_lang);

    /* Formato por defecto */
    GtkWidget *row_fmt = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkStringList *fmts = gtk_string_list_new (NULL);
    const GPtrArray *fmt_list = formats_list ();
    const FormatWriter *target = formats_from_string (config_get_transcript_format ());
    guint fmt_idx = 0;
    for (guint i = 0; i < fmt_list->len; i++) {
        const FormatWriter *w = g_ptr_array_index (fmt_list, i);
        gtk_string_list_append (fmts, w->id);
        if (w == target)
            fmt_idx = i;
    }
    st->combo_pref_format = gtk_drop_down_new (G_LIST_MODEL (fmts), NULL);
    gtk_drop_down_set_selected (GTK_DROP_DOWN (st->combo_pref_format), fmt_idx);
    g_signal_connect (st->combo_pref_format, "notify::selected",
                      G_CALLBACK (on_pref_format_changed), st);
    gtk_box_append (GTK_BOX (row_fmt), gtk_label_new ("Formato por defecto:"));
    gtk_box_append (GTK_BOX (row_fmt), st->combo_pref_format);
    gtk_box_append (GTK_BOX (pref), row_fmt);

    /* Ruta del archivo y nota */
    gchar *hint = g_strdup_printf ("Los cambios se guardan automáticamente en:\n%s",
                                   config_get_config_file ());
    GtkWidget *label_hint = gtk_label_new (hint);
    gtk_label_set_xalign (GTK_LABEL (label_hint), 0.0f);
    gtk_label_set_wrap (GTK_LABEL (label_hint), TRUE);
    gtk_widget_set_margin_top (label_hint, 16);
    gtk_box_append (GTK_BOX (pref), label_hint);
    g_free (hint);

    gtk_notebook_append_page (notebook, pref, gtk_label_new ("Preferencias"));
}

/* ---------------------------------------------------------------------------
 * Construcción de la ventana
 * ------------------------------------------------------------------------- */

static void
build_ui (AppState *st, GtkApplication *app)
{
    st->window = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (st->window), "Ana-Trans");
    gtk_window_set_default_size (GTK_WINDOW (st->window), 760, 580);

    GtkWidget *notebook = gtk_notebook_new ();
    gtk_window_set_child (GTK_WINDOW (st->window), notebook);

    /* Pestaña "Transcripción" (contenido actual). */
    GtkWidget *root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top (root, 12);
    gtk_widget_set_margin_bottom (root, 12);
    gtk_widget_set_margin_start (root, 12);
    gtk_widget_set_margin_end (root, 12);
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), root,
                              gtk_label_new ("Transcripción"));

    /* ---- Entrada ------------------------------------------------------- */
    GtkWidget *row_in = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

    st->entry_input = gtk_entry_new ();
    gtk_editable_set_editable (GTK_EDITABLE (st->entry_input), FALSE);
    gtk_widget_set_hexpand (st->entry_input, TRUE);
    gtk_entry_set_placeholder_text (GTK_ENTRY (st->entry_input),
                                    "Carpeta o archivos a procesar");

    GtkWidget *btn_folder = gtk_button_new_with_label ("Carpeta…");
    GtkWidget *btn_files  = gtk_button_new_with_label ("Archivos…");
    g_signal_connect (btn_folder, "clicked", G_CALLBACK (on_pick_input_folder), st);
    g_signal_connect (btn_files,  "clicked", G_CALLBACK (on_pick_input_files),  st);

    gtk_box_append (GTK_BOX (row_in), gtk_label_new ("Entrada:"));
    gtk_box_append (GTK_BOX (row_in), st->entry_input);
    gtk_box_append (GTK_BOX (row_in), btn_folder);
    gtk_box_append (GTK_BOX (row_in), btn_files);
    gtk_box_append (GTK_BOX (root), row_in);

    /* ---- Lista de archivos --------------------------------------------- */
    st->list_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    st->rows     = g_ptr_array_new ();

    st->list_placeholder = gtk_label_new ("Sin archivos. Selecciona una carpeta o agrega archivos.");
    gtk_label_set_xalign (GTK_LABEL (st->list_placeholder), 0.0f);
    gtk_widget_set_margin_top (st->list_placeholder, 8);
    gtk_box_append (GTK_BOX (st->list_box), st->list_placeholder);

    GtkWidget *scroll = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_has_frame (GTK_SCROLLED_WINDOW (scroll), TRUE);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand (scroll, TRUE);
    gtk_widget_set_vexpand (scroll, TRUE);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), st->list_box);
    gtk_box_append (GTK_BOX (root), scroll);

    /* ---- Opciones ------------------------------------------------------- */
    GtkWidget *row_out = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    st->entry_output = gtk_entry_new ();
    gtk_widget_set_hexpand (st->entry_output, TRUE);
    gtk_editable_set_text (GTK_EDITABLE (st->entry_output), config_get_output_dir ());

    GtkWidget *btn_out = gtk_button_new_with_label ("…");
    g_signal_connect (btn_out, "clicked", G_CALLBACK (on_pick_output_folder), st);

    gtk_box_append (GTK_BOX (row_out), gtk_label_new ("Salida:"));
    gtk_box_append (GTK_BOX (row_out), st->entry_output);
    gtk_box_append (GTK_BOX (row_out), btn_out);
    gtk_box_append (GTK_BOX (root), row_out);

    GtkWidget *row_model = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    st->entry_model = gtk_entry_new ();
    gtk_widget_set_hexpand (st->entry_model, TRUE);
    gtk_editable_set_text (GTK_EDITABLE (st->entry_model), config_get_whisper_model ());
    gtk_entry_set_placeholder_text (GTK_ENTRY (st->entry_model),
                                    "Selecciona el modelo .bin (o define WHISPER_MODEL)");

    GtkWidget *btn_model = gtk_button_new_with_label ("…");
    g_signal_connect (btn_model, "clicked", G_CALLBACK (on_pick_model_file), st);

    gtk_box_append (GTK_BOX (row_model), gtk_label_new ("Modelo whisper:"));
    gtk_box_append (GTK_BOX (row_model), st->entry_model);
    gtk_box_append (GTK_BOX (row_model), btn_model);
    gtk_box_append (GTK_BOX (root), row_model);

    GtkWidget *row_lang = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkStringList *langs =
        gtk_string_list_new ((const char *[]) { "auto", "es", "en", NULL });
    st->combo_lang = gtk_drop_down_new (G_LIST_MODEL (langs), NULL);
    gtk_drop_down_set_selected (GTK_DROP_DOWN (st->combo_lang),
                                combo_string_index (langs, config_get_transcript_lang (), 0));

    /* Desplegable de formato: se llena desde el registro de formats.h,
     * así los formatos nuevos aparecen solos. Se selecciona el preferido. */
    GtkStringList *formats = gtk_string_list_new (NULL);
    const GPtrArray *fmt_list = formats_list ();
    guint default_idx = 0;
    const FormatWriter *target = formats_from_string (config_get_transcript_format ());
    for (guint i = 0; i < fmt_list->len; i++) {
        const FormatWriter *w = g_ptr_array_index (fmt_list, i);
        gtk_string_list_append (formats, w->id);
        if (w == target)
            default_idx = i;
    }
    st->combo_format = gtk_drop_down_new (G_LIST_MODEL (formats), NULL);
    gtk_drop_down_set_selected (GTK_DROP_DOWN (st->combo_format), default_idx);

    gtk_box_append (GTK_BOX (row_lang), gtk_label_new ("Idioma:"));
    gtk_box_append (GTK_BOX (row_lang), st->combo_lang);
    gtk_box_append (GTK_BOX (row_lang), gtk_label_new ("Formato:"));
    gtk_box_append (GTK_BOX (row_lang), st->combo_format);
    gtk_box_append (GTK_BOX (root), row_lang);

    /* ---- Progreso y estado ---------------------------------------------- */
    st->progress = gtk_progress_bar_new ();
    gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (st->progress), TRUE);
    gtk_box_append (GTK_BOX (root), st->progress);

    st->label_status = gtk_label_new ("Listo.");
    gtk_label_set_xalign (GTK_LABEL (st->label_status), 0.0f);
    gtk_widget_set_margin_top (st->label_status, 4);
    gtk_box_append (GTK_BOX (root), st->label_status);

    /* ---- Log ------------------------------------------------------------- */
    GtkWidget *log_scroll = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_has_frame (GTK_SCROLLED_WINDOW (log_scroll), TRUE);
    gtk_widget_set_vexpand (log_scroll, TRUE);
    gtk_widget_set_size_request (log_scroll, -1, 150);

    GtkWidget *textview = gtk_text_view_new ();
    gtk_text_view_set_editable (GTK_TEXT_VIEW (textview), FALSE);
    gtk_text_view_set_monospace (GTK_TEXT_VIEW (textview), TRUE);
    st->log_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (log_scroll), textview);
    gtk_box_append (GTK_BOX (root), log_scroll);

    /* ---- Botones ---------------------------------------------------------- */
    GtkWidget *row_buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *btn_clear   = gtk_button_new_with_label ("Limpiar lista");
    GtkWidget *btn_process = gtk_button_new_with_label ("Procesar");
    st->btn_process = btn_process;
    gtk_widget_add_css_class (btn_process, "suggested-action");

    g_signal_connect (btn_clear,   "clicked", G_CALLBACK (on_clear),   st);
    g_signal_connect (btn_process, "clicked", G_CALLBACK (on_process), st);

    gtk_widget_set_hexpand (btn_clear, TRUE);
    gtk_box_append (GTK_BOX (row_buttons), btn_clear);
    gtk_box_append (GTK_BOX (row_buttons), btn_process);
    gtk_box_append (GTK_BOX (root), row_buttons);

    /* Pestaña "Preferencias". */
    build_preferences_tab (st, GTK_NOTEBOOK (notebook));

    log_line (st, "Ana-Trans iniciado (infraestructura: carpeta + lote).");
}

/* ---------------------------------------------------------------------------
 * Ciclo de vida
 * ------------------------------------------------------------------------- */

static void
on_window_destroy (GtkWidget *widget, gpointer user_data)
{
    AppState *st = user_data;

    if (st->save_timeout_id != 0) {
        g_source_remove (st->save_timeout_id);
        config_save ();  /* asegurar el último cambio pendiente */
    }

    if (st->files != NULL)
        g_ptr_array_free (st->files, TRUE);
    if (st->rows != NULL)
        g_ptr_array_free (st->rows, TRUE);
    if (st->files_snapshot != NULL) {
        for (int i = 0; i < st->n_files_snapshot; i++)
            g_free (st->files_snapshot[i]);
        g_free (st->files_snapshot);
    }
    g_free (st->output_dir_snapshot);
    g_free (st->model_snapshot);
    g_free (st->language_snapshot);
    g_free (st->format_id_snapshot);
    g_free (st);
}

void
gui_activate (GtkApplication *app)
{
    AppState *st = g_new0 (AppState, 1);
    st->files = g_ptr_array_new_with_free_func (g_free);
    st->rows  = g_ptr_array_new ();

    build_ui (st, app);

    g_signal_connect (st->window, "destroy", G_CALLBACK (on_window_destroy), st);
    g_signal_connect (st->window, "close-request", G_CALLBACK (on_close_request), st);

    gtk_window_present (GTK_WINDOW (st->window));
}




