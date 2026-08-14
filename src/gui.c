/* ============================================================================
 * gui.c — interfaz GTK 4 de Ana-Trans (ventana, pestañas, eventos del motor).
 *
 * [PROYECTO]    Ana-Trans: widgets, callbacks, guardado de preferencias y
 *               presentación de los eventos de controller.c.
 * [DEPENDENCIA] GTK 4 (widgets), GLib; el trabajo pesado lo hace el motor.
 * ========================================================================== */

#include "gui.h"

/* módulos del proyecto */
#include "batch.h"
#include "config.h"
#include "controller.h"
#include "formats.h"
#include "i18n.h"
#include "models.h"
#include "transcribe.h"

/* dependencias (GLib / libc) */
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

    /* Sección "modelo" de la pestaña Transcripción (según el modo). */
    GtkWidget      *box_model;         /* contenedor visible */
    GtkWidget      *row_model_custom;  /* vista modo personalizado */
    GtkWidget      *box_model_easy;    /* vista modo sencillo (3 radios) */
    GtkWidget      *radio_easy[3];     /* Preciso / Equilibrado / Rápido */
    GtkWidget      *label_easy_status[3]; /* subtexto + estado por modelo */

    /* Preferencias (pestaña con guardado automático). */
    GtkWidget      *entry_pref_model;
    GtkWidget      *entry_pref_output;
    GtkWidget      *combo_pref_lang;
    GtkWidget      *combo_pref_format;
    GtkWidget      *row_pref_custom;      /* fila del modelo personalizado */
    GtkWidget      *combo_pref_model_mode; /* personalizado / sencillo */
    GtkWidget      *row_pref_easy;         /* panel del modo sencillo */
    GtkWidget      *btn_pref_download;
    GtkWidget      *label_pref_easy_status[3]; /* estado por modelo */
    guint           save_timeout_id;

    /* Internacionalización (cambio de idioma en caliente, i18n.h). */
    GPtrArray      *i18n_binds;        /* I18nBind* (widgets traducibles) */
    guint           i18n_cb_id;        /* id del callback de cambio de idioma */
    GtkWidget      *combo_pref_ui_lang; /* selector de idioma de la interfaz */
    GtkWidget      *label_hint;         /* nota de guardado (se regenera al traducir) */
    GtkStringList  *model_ui_lang;      /* modelo del combo de idioma (vive en st) */
    GtkStringList  *model_mode;         /* modelo del combo de modo (vive en st) */
    GPtrArray      *ui_lang_codes;      /* códigos en el mismo orden que el modelo de idioma */

    GtkWidget      *list_box;         /* GtkBox vertical con las filas        */
    GtkWidget      *list_placeholder; /* etiqueta mostrada cuando no hay nada  */
    GPtrArray      *rows;             /* GtkWidget* filas de la lista         */
    GPtrArray      *files;            /* gchar* rutas absolutas (free func)   */

    GtkWidget      *progress;
    GtkWidget      *label_status;
    GtkTextBuffer  *log_buffer;
    GtkWidget      *btn_process;

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
static void     update_easy_model_status (AppState *st);
static void     start_missing_download   (AppState *st);
static void     on_easy_radio_toggled    (GtkCheckButton *button, gpointer user_data);
static void     apply_translations       (AppState *st);
static void     refresh_ui_language_combo (AppState *st);
static void     refresh_mode_combo       (AppState *st);
static guint    combo_string_index       (GtkStringList *list, const char *value,
                                          guint fallback);
static void     on_pref_ui_lang_changed  (GObject *gobject, GParamSpec *pspec,
                                          gpointer user_data);
static void     on_pref_mode_changed     (GObject *gobject, GParamSpec *pspec,
                                          gpointer user_data);
static guint    ui_lang_code_index      (AppState *st, const char *code,
                                          guint fallback);

/* ---------------------------------------------------------------------------
 * Internacionalización (cambio de idioma en caliente; ver i18n.h)
 * ------------------------------------------------------------------------- */

/* Alias corto para traducir las cadenas de la GUI (msgid en español). */
#define t_(msgid) i18n_t (msgid)

typedef enum {
    I18N_KIND_LABEL,        /* GtkLabel */
    I18N_KIND_BUTTON,       /* GtkButton / GtkCheckButton (radio) */
    I18N_KIND_PLACEHOLDER,  /* GtkEntry */
} I18nKind;

typedef struct {
    GtkWidget *widget;
    I18nKind   kind;
    gchar     *key;         /* msgid (texto base en español) */
} I18nBind;

static void
free_i18n_bind (gpointer p)
{
    I18nBind *b = p;
    g_free (b->key);
    g_free (b);
}

static void
apply_i18n_to_bind (I18nBind *b)
{
    const char *txt = i18n_t (b->key);

    switch (b->kind) {
        case I18N_KIND_LABEL:
            gtk_label_set_text (GTK_LABEL (b->widget), txt);
            break;
        case I18N_KIND_BUTTON:
            /* En GTK4 moderno GtkCheckButton (radios) ya no deriva de
             * GtkButton: cada tipo tiene su propio set_label. */
            if (GTK_IS_CHECK_BUTTON (b->widget))
                gtk_check_button_set_label (GTK_CHECK_BUTTON (b->widget), txt);
            else
                gtk_button_set_label (GTK_BUTTON (b->widget), txt);
            break;
        case I18N_KIND_PLACEHOLDER:
            gtk_entry_set_placeholder_text (GTK_ENTRY (b->widget), txt);
            break;
    }
}

/* Registra un widget para re-traducirlo al cambiar de idioma y le aplica el
 * texto actual. `key` es el msgid (texto base en español). */
static void
i18n_bind (AppState *st, GtkWidget *widget, I18nKind kind, const char *key)
{
    I18nBind *b = g_new (I18nBind, 1);
    b->widget = widget;
    b->kind   = kind;
    b->key    = g_strdup (key);
    g_ptr_array_add (st->i18n_binds, b);
    apply_i18n_to_bind (b);
}

/* Traduce las fases internas del motor a texto visible. */
static const char *
phase_label (const char *phase)
{
    if (g_strcmp0 (phase, "modelo") == 0)
        return t_ ("Cargando modelo");
    if (g_strcmp0 (phase, "convirtiendo") == 0)
        return t_ ("Convirtiendo");
    if (g_strcmp0 (phase, "transcribiendo") == 0)
        return t_ ("Transcribiendo");
    if (g_strcmp0 (phase, "listo") == 0)
        return t_ ("Listo");
    return (phase != NULL) ? phase : "";
}

/* Nombre legible del idioma en su propio idioma (msgid en el catálogo). */
static const char *
ui_lang_name (const char *code)
{
    if (g_strcmp0 (code, "sistema") == 0)
        return t_ ("Sistema (idioma del sistema)");
    if (g_strcmp0 (code, "es") == 0)
        return t_ ("Español");
    if (g_strcmp0 (code, "en") == 0)
        return t_ ("English");
    return code; /* idioma no catalogado: se muestra su código */
}

/* Índice de un código dentro de st->ui_lang_codes (fallback si no está). */
static guint
ui_lang_code_index (AppState *st, const char *code, guint fallback)
{
    if (st->ui_lang_codes != NULL && code != NULL) {
        for (guint i = 0; i < st->ui_lang_codes->len; i++) {
            if (g_strcmp0 (g_ptr_array_index (st->ui_lang_codes, i), code) == 0)
                return i;
        }
    }
    return fallback;
}

/* Crea el desplegable de idioma de la interfaz. El modelo es un GtkStringList
 * de NOMBRES visibles ya traducidos; los códigos reales viven en paralelo en
 * st->ui_lang_codes (mismo orden). El modelo vive en st->model_ui_lang. */
static GtkWidget *
make_ui_lang_dropdown (AppState *st)
{
    GtkStringList *list = gtk_string_list_new (NULL);
    st->ui_lang_codes = g_ptr_array_new_with_free_func (g_free);

    gtk_string_list_append (list, ui_lang_name ("sistema"));
    g_ptr_array_add (st->ui_lang_codes, g_strdup ("sistema"));

    GPtrArray *langs = i18n_languages ();
    for (guint i = 0; i < langs->len; i++) {
        const char *code = g_ptr_array_index (langs, i);
        gtk_string_list_append (list, ui_lang_name (code));
        g_ptr_array_add (st->ui_lang_codes, g_strdup (code));
    }
    i18n_free_languages (langs);

    GtkWidget *dd = gtk_drop_down_new (G_LIST_MODEL (list), NULL);
    st->model_ui_lang = list;   /* el modelo queda en st hasta que se sustituya */
    g_object_ref (list);        /* ref propia de st (el dropdown tiene la suya) */
    return dd;
}

/* Refresca el combo de idioma: el modelo (códigos) es estable y vive en st;
 * set_model(NULL)+set_model() fuerza el re-bind de la factory, que pinta los
 * nombres de idioma traducidos al idioma activo. */
static void
refresh_ui_language_combo (AppState *st)
{
    if (st->combo_pref_ui_lang == NULL || st->model_ui_lang == NULL
        || st->ui_lang_codes == NULL)
        return;

    const char *cur = i18n_get_language ();

    /* Reconstruir el modelo con los nombres ya traducidos (los códigos no
     * cambian: viven en st->ui_lang_codes, mismo orden). */
    GtkStringList *list = gtk_string_list_new (NULL);
    for (guint i = 0; i < st->ui_lang_codes->len; i++)
        gtk_string_list_append (list,
                                ui_lang_name (g_ptr_array_index (st->ui_lang_codes, i)));

    /* Bloquear los callbacks: set_model()/set_selected() emiten
     * notify::selected y reentrarían en on_pref_ui_lang_changed(). */
    g_signal_handlers_block_by_func (st->combo_pref_ui_lang,
                                     G_CALLBACK (on_pref_ui_lang_changed), st);

    gtk_drop_down_set_model (GTK_DROP_DOWN (st->combo_pref_ui_lang), NULL);
    gtk_drop_down_set_model (GTK_DROP_DOWN (st->combo_pref_ui_lang),
                             G_LIST_MODEL (list));
    gtk_drop_down_set_selected (GTK_DROP_DOWN (st->combo_pref_ui_lang),
                                ui_lang_code_index (st, cur, 0));

    g_signal_handlers_unblock_by_func (st->combo_pref_ui_lang,
                                       G_CALLBACK (on_pref_ui_lang_changed), st);

    /* El dropdown ya soltó su referencia al modelo viejo con set_model(NULL):
     * liberar la de st y quedarse con el nuevo (con ref propia de st). */
    g_clear_object (&st->model_ui_lang);
    st->model_ui_lang = list;
    g_object_ref (list);
}

/* Refresca el combo de modo con las etiquetas traducidas (mismo índice). */
static void
refresh_mode_combo (AppState *st)
{
    if (st->combo_pref_model_mode == NULL)
        return;

    guint idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (st->combo_pref_model_mode));
    GtkStringList *modes =
        gtk_string_list_new ((const char *[]) { t_ ("Modelo personalizado"),
                                                t_ ("Modo sencillo"), NULL });

    /* Bloquear on_pref_mode_changed durante la reconstrucción (evita reentrada). */
    g_signal_handlers_block_by_func (st->combo_pref_model_mode,
                                     G_CALLBACK (on_pref_mode_changed), st);

    gtk_drop_down_set_model (GTK_DROP_DOWN (st->combo_pref_model_mode), NULL);
    gtk_drop_down_set_model (GTK_DROP_DOWN (st->combo_pref_model_mode),
                             G_LIST_MODEL (modes));
    gtk_drop_down_set_selected (GTK_DROP_DOWN (st->combo_pref_model_mode), idx);

    g_signal_handlers_unblock_by_func (st->combo_pref_model_mode,
                                       G_CALLBACK (on_pref_mode_changed), st);

    /* El dropdown ya soltó su referencia al modelo viejo con set_model(NULL):
     * liberar la de st y quedarse con el nuevo (con ref propia de st). */
    g_clear_object (&st->model_mode);
    st->model_mode = modes;
    g_object_ref (modes);
}

/* Re-aplica todos los textos visibles tras un cambio de idioma (en caliente). */
static void
apply_translations (AppState *st)
{
    if (st->i18n_binds == NULL)
        return;

    for (guint i = 0; i < st->i18n_binds->len; i++)
        apply_i18n_to_bind (g_ptr_array_index (st->i18n_binds, i));

    refresh_ui_language_combo (st);
    refresh_mode_combo (st);

    /* La nota de guardado contiene una ruta: se regenera completa. */
    if (st->label_hint != NULL) {
        gchar *hint = g_strdup_printf ("%s\n%s",
                                       t_ ("Los cambios se guardan automáticamente en:"),
                                       config_get_config_file ());
        gtk_label_set_text (GTK_LABEL (st->label_hint), hint);
        g_free (hint);
    }

    update_easy_model_status (st);

    /* Dirección del texto según el idioma (RTL: árabe, hebreo, ...). */
    gtk_widget_set_default_direction (i18n_is_rtl ()
                                          ? GTK_TEXT_DIR_RTL
                                          : GTK_TEXT_DIR_LTR);

    /* Estado inactivo: refrescar el texto visible. */
    if (!controller_is_running ()) {
        if (st->files != NULL && st->files->len > 0)
            update_file_count (st);
        else
            update_status (st, t_ ("Listo."));
    }
}

/* Id del idle que re-aplica las traducciones (0 si no hay ninguno pendiente).
 * Se usa para no acumular varios idles si el idioma cambia deprisa. */
static guint g_i18n_apply_idle = 0;

static gboolean
apply_translations_idle (gpointer user_data)
{
    g_i18n_apply_idle = 0;
    AppState *st = user_data;
    apply_translations (st);
    return G_SOURCE_REMOVE;
}

static void
on_i18n_changed (gpointer user_data)
{
    /* Diferir la reconstrucción de widgets al siguiente ciclo del main loop:
     * nunca ejecutar apply_translations() dentro de la emisión de una señal
     * GTK (p. ej. notify::selected del combo de idioma), porque reconstruir
     * el modelo del dropdown ahí corrompe su estado interno (segfault). */
    if (g_i18n_apply_idle == 0)
        g_i18n_apply_idle = g_idle_add (apply_translations_idle, user_data);
}

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
    gchar *msg = g_strdup_printf (t_ ("%u archivo(s) en cola."), st->files->len);
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
    gtk_file_dialog_set_title (dlg, t_ ("Seleccionar carpeta de entrada"));
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
    gtk_file_dialog_set_title (dlg, t_ ("Agregar archivos de audio/video"));
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
    gtk_file_dialog_set_title (dlg, t_ ("Seleccionar carpeta de salida"));
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
    gtk_file_dialog_set_title (dlg, t_ ("Seleccionar modelo de whisper (.bin)"));
    gtk_file_dialog_open (dlg, GTK_WINDOW (st->window), NULL,
                          on_model_file_finish, st);
}

/* ---------------------------------------------------------------------------
 * Eventos del motor (controller.h): la GUI solo pinta lo que recibe.
 * ------------------------------------------------------------------------- */

static void
on_controller_event (const ControllerEvent *ev, gpointer user_data)
{
    AppState *st = user_data;

    switch (ev->type) {
        case CONTROLLER_EVENT_PROGRESS: {
            if (g_strcmp0 (ev->phase, "descargando") == 0) {
                gchar *txt = g_strdup_printf (t_ ("Descargando modelo %s (%d/%d)…"),
                                              (ev->file != NULL) ? ev->file : "?",
                                              ev->done + 1, ev->total);
                update_status (st, txt);
                gtk_progress_bar_set_text (GTK_PROGRESS_BAR (st->progress), txt);
                gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st->progress),
                                               (ev->total > 0)
                                                   ? (double) ev->done / ev->total
                                                   : 0.0);
                g_free (txt);
                break;
            }
            if (g_strcmp0 (ev->phase, "descargado") == 0) {
                gchar *txt = g_strdup_printf (t_ ("Modelo %s listo (%d/%d)."),
                                              (ev->file != NULL) ? ev->file : "?",
                                              ev->done, ev->total);
                update_status (st, txt);
                gtk_progress_bar_set_text (GTK_PROGRESS_BAR (st->progress), txt);
                gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st->progress),
                                               (ev->total > 0)
                                                   ? (double) ev->done / ev->total
                                                   : 1.0);
                g_free (txt);
                break;
            }
            int file_pct = CLAMP (ev->file_percent, 0, 100);
            double fraction = (ev->total > 0)
                                  ? ((double) ev->done + (double) file_pct / 100.0)
                                        / (double) ev->total
                                  : 0.0;

            gchar *text;
            if (g_strcmp0 (ev->phase, "modelo") == 0) {
                text = g_strdup (t_ ("Cargando modelo…"));
            } else {
                int pct = (int) (fraction * 100.0 + 0.5);
                gchar *fname = (ev->file != NULL) ? g_path_get_basename (ev->file) : NULL;
                text = g_strdup_printf (t_ ("%d/%d · %d%% — %s (%s)"),
                                        ev->done + 1, ev->total, pct,
                                        (fname != NULL) ? fname : "?",
                                        phase_label (ev->phase));
                g_free (fname);
            }

            gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st->progress), fraction);
            gtk_progress_bar_set_text (GTK_PROGRESS_BAR (st->progress), text);
            update_status (st, text);
            g_free (text);
            break;
        }

        case CONTROLLER_EVENT_REPORT: {
            gchar *msg = g_strdup_printf ("[%d/%d] %s", ev->done, ev->total, ev->file);
            log_line (st, "%s -> %s", msg, ev->success ? "OK" : "ERROR");
            if (!ev->success && ev->message != NULL && ev->message[0] != 0)
                log_line (st, "    %s", ev->message);
            g_free (msg);
            break;
        }

        case CONTROLLER_EVENT_FINISHED: {
            gchar *msg = g_strdup_printf (t_ ("Proceso terminado: %d de %d archivo(s) convertidos."),
                                          ev->ok_count, ev->total);
            log_line (st, "%s", msg);
            update_status (st, msg);
            g_free (msg);
            gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st->progress), 1.0);
            gtk_progress_bar_set_text (GTK_PROGRESS_BAR (st->progress), t_ ("Completado"));
            gtk_widget_set_sensitive (st->btn_process, TRUE);
            break;
        }

        case CONTROLLER_EVENT_DOWNLOAD_FINISHED: {
            if (ev->ok_count > 0) {
                if (ev->file != NULL) {
                    log_line (st, "Modelo %s descargado correctamente.", ev->file);
                    update_status (st, t_ ("Modelo descargado."));
                } else {
                    gchar *msg = g_strdup_printf (t_ ("Descarga de modelos terminada: %d de %d correctos."),
                                                  ev->ok_count, ev->total);
                    log_line (st, "%s", msg);
                    update_status (st, msg);
                    g_free (msg);
                }
                gtk_progress_bar_set_text (GTK_PROGRESS_BAR (st->progress), t_ ("Modelo descargado"));
            } else {
                if (ev->file != NULL)
                    log_line (st, "Error al descargar el modelo %s: %s",
                              ev->file, (ev->message != NULL) ? ev->message : "?");
                else
                    log_line (st, "Error al descargar los modelos: %s",
                              (ev->message != NULL) ? ev->message : "?");
                update_status (st, t_ ("Error al descargar el modelo."));
                gtk_progress_bar_set_text (GTK_PROGRESS_BAR (st->progress), t_ ("Descarga fallida"));
            }
            gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st->progress), 1.0);
            update_easy_model_status (st);
            break;
        }

        case CONTROLLER_EVENT_MODELS_SYNCED: {
            update_easy_model_status (st);
            if (ev->ok_count == ev->total) {
                log_line (st, "Modelos del modo sencillo listos.");
                update_status (st, t_ ("Modelos del modo sencillo listos."));
                gtk_progress_bar_set_text (GTK_PROGRESS_BAR (st->progress), t_ ("Modelos listos"));
            } else {
                gchar *msg = g_strdup_printf (t_ ("La descarga terminó con errores (%d de %d modelos)."),
                                              ev->ok_count, ev->total);
                log_line (st, "%s", msg);
                update_status (st, msg);
                gtk_progress_bar_set_text (GTK_PROGRESS_BAR (st->progress), t_ ("Descarga con errores"));
                g_free (msg);
            }
            break;
        }
    }
}

static gboolean
on_close_request (GtkWidget *widget, gpointer user_data)
{
    AppState *st = user_data;

    if (controller_is_running ()) {
        update_status (st, t_ ("Procesamiento en curso; espera a que termine."));
        return TRUE; /* veta el cierre mientras el motor trabaja */
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

    if (controller_is_running ()) {
        update_status (st, t_ ("Ya hay un procesamiento en curso."));
        return;
    }

    if (st->files->len == 0) {
        update_status (st, t_ ("No hay archivos en la cola. Selecciona una carpeta o agrega archivos."));
        return;
    }

    const char *out_dir = gtk_editable_get_text (GTK_EDITABLE (st->entry_output));
    if (out_dir == NULL || out_dir[0] == 0) {
        update_status (st, t_ ("Indica una carpeta de salida primero."));
        return;
    }

    /* Modelo según el modo activo. */
    gchar *model = NULL;
    gboolean easy_mode = g_ascii_strcasecmp (config_get_model_mode (), "easy") == 0;

    if (easy_mode) {
        const char *id = config_get_easy_model ();
        const KnownModel *m = models_find (id);
        if (m == NULL || !models_is_downloaded (id)) {
            gchar *txt = g_strdup_printf (t_ ("El modelo '%s' aún no está descargado (modo sencillo)."),
                                          (m != NULL) ? m->label : id);
            update_status (st, txt);
            g_free (txt);
            return;
        }
        model = models_path (id);
    } else {
        const char *text = gtk_editable_get_text (GTK_EDITABLE (st->entry_model));
        if (text == NULL || text[0] == 0) {
            update_status (st, t_ ("Selecciona el modelo de whisper (.bin) o define WHISPER_MODEL."));
            return;
        }
        model = g_strdup (text);
    }

    const char *lang = gtk_string_list_get_string (
        GTK_STRING_LIST (gtk_drop_down_get_model (GTK_DROP_DOWN (st->combo_lang))),
        gtk_drop_down_get_selected (GTK_DROP_DOWN (st->combo_lang)));
    const char *fmt = gtk_string_list_get_string (
        GTK_STRING_LIST (gtk_drop_down_get_model (GTK_DROP_DOWN (st->combo_format))),
        gtk_drop_down_get_selected (GTK_DROP_DOWN (st->combo_format)));

    /* Entregar el trabajo al motor: la GUI solo inicia y espera eventos. */
    ControllerJob job = {
        .files      = (char **) st->files->pdata,
        .n_files    = (int) st->files->len,
        .output_dir = (char *) out_dir,
        .model_path = (char *) model,
        .language   = (char *) lang,
        .format_id  = (char *) fmt,
    };

    gtk_widget_set_sensitive (st->btn_process, FALSE);
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st->progress), 0.0);
    gtk_progress_bar_set_text (GTK_PROGRESS_BAR (st->progress), "");

    log_line (st, "--- Procesamiento (ffmpeg -> whisper) ---");
    log_line (st, "Carpeta de salida: %s", out_dir);
    log_line (st, "Modelo whisper    : %s", model);
    log_line (st, "Idioma / Formato  : %s / %s", lang, fmt);
    log_line (st, "Archivos en cola  : %d", job.n_files);

    update_status (st, t_ ("Procesando…"));

    if (!controller_start (&job, on_controller_event, st)) {
        update_status (st, t_ ("No se pudo iniciar el procesamiento."));
        gtk_widget_set_sensitive (st->btn_process, TRUE);
    }
    g_free (model);
}




static void
on_clear (GtkButton *button, gpointer user_data)
{
    AppState *st = user_data;

    g_ptr_array_set_size (st->files, 0);
    clear_rows (st);
    log_line (st, "Lista vaciada.");
    update_status (st, t_ ("Lista vaciada."));
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
on_pref_ui_lang_changed (GObject *gobject, GParamSpec *pspec, gpointer user_data)
{
    AppState *st = user_data;

    /* El combo muestra nombres; el código real se lee del array paralelo. */
    guint idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (gobject));
    const char *code = (st->ui_lang_codes != NULL && idx < st->ui_lang_codes->len)
                           ? g_ptr_array_index (st->ui_lang_codes, idx)
                           : NULL;

    if (g_strcmp0 (code, i18n_get_language ()) == 0)
        return; /* sin cambio (evita bucles al refrescar el combo) */

    /* Cambio en caliente: i18n_set_language() dispara apply_translations()
     * de forma diferida (g_idle_add), fuera de esta señal de GTK. */
    if (i18n_set_language (code)) {
        config_set_ui_language (code);
        schedule_config_save (st);
    } else {
        update_status (st, t_ ("No se pudo cambiar el idioma (catálogo no encontrado)."));
    }
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
    gtk_file_dialog_set_title (dlg, t_ ("Seleccionar modelo de whisper (.bin)"));
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
    gtk_file_dialog_set_title (dlg, t_ ("Seleccionar carpeta de salida"));
    gtk_file_dialog_select_folder (dlg, GTK_WINDOW (st->window), NULL,
                                   on_pref_output_finish, st);
}

static void
refresh_model_entry (AppState *st)
{
    gtk_editable_set_text (GTK_EDITABLE (st->entry_model),
                           config_get_whisper_model ());
}

static void
update_easy_model_status (AppState *st)
{
    const KnownModel *km = models_known ();

    for (int i = 0; i < 3 && km[i].id != NULL; i++) {
        gboolean ok = models_is_downloaded (km[i].id);

        /* Subtexto de la pestaña Transcripción: descripción + estado. */
        if (st->label_easy_status[i] != NULL) {
            gchar *txt = g_strdup_printf ("%s — %s",
                                          (km[i].desc != NULL) ? t_ (km[i].desc) : "",
                                          ok ? t_ ("Descargado") : t_ ("Pendiente de descarga"));
            gtk_label_set_text (GTK_LABEL (st->label_easy_status[i]), txt);
            g_free (txt);
        }

        /* Estado en Preferencias. */
        if (st->label_pref_easy_status[i] != NULL)
            gtk_label_set_text (GTK_LABEL (st->label_pref_easy_status[i]),
                                ok ? t_ ("Descargado ✓") : t_ ("Pendiente de descarga"));
    }

    gtk_widget_set_sensitive (st->btn_pref_download,
                              models_first_missing () != NULL
                                  && !controller_is_running ());
}

/* Modo de modelo: ids internos "custom"/"easy" con etiquetas en español. */
static const char *k_mode_ids[]    = { "custom", "easy" };
static const char *k_mode_labels[] = { "Modelo personalizado", "Modo sencillo" };

static int
mode_index (const char *mode)
{
    if (mode != NULL && g_ascii_strcasecmp (mode, "custom") == 0)
        return 0;
    return 1; /* modo sencillo (por defecto) */
}

static const char *
mode_from_index (int idx)
{
    return (idx <= 0) ? k_mode_ids[0] : k_mode_ids[1];
}

static void
apply_model_mode_visibility (AppState *st)
{
    gboolean easy = g_ascii_strcasecmp (config_get_model_mode (), "easy") == 0;
    gtk_widget_set_visible (st->row_pref_custom, !easy);
    gtk_widget_set_visible (st->row_pref_easy, easy);

    /* Pestaña Transcripción: una vista u otra según el modo. */
    gtk_widget_set_visible (st->row_model_custom, !easy);
    gtk_widget_set_visible (st->box_model_easy, easy);
}

static void
on_pref_mode_changed (GObject *gobject, GParamSpec *pspec, gpointer user_data)
{
    AppState *st = user_data;
    int idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (st->combo_pref_model_mode));
    const char *v = mode_from_index (idx);

    config_set_model_mode (v);
    apply_model_mode_visibility (st);
    refresh_model_entry (st);
    if (g_ascii_strcasecmp (v, "easy") == 0) {
        update_easy_model_status (st);
        start_missing_download (st); /* auto-descarga de los que falten */
    }
    schedule_config_save (st);
}

static void
on_easy_radio_toggled (GtkCheckButton *button, gpointer user_data)
{
    AppState *st = user_data;
    if (!gtk_check_button_get_active (button))
        return;

    const KnownModel *km = models_known ();
    for (int i = 0; i < 3 && km[i].id != NULL; i++) {
        if (st->radio_easy[i] == (GtkWidget *) button) {
            config_set_easy_model (km[i].id);
            schedule_config_save (st);
            return;
        }
    }
}

static void
start_missing_download (AppState *st)
{
    if (controller_is_running ()) {
        update_status (st, t_ ("Ya hay una tarea en curso; espera a que termine."));
        return;
    }
    if (models_first_missing () == NULL) {
        update_easy_model_status (st);
        return; /* no hay nada que descargar */
    }

    gtk_widget_set_sensitive (st->btn_pref_download, FALSE);
    update_status (st, t_ ("Descargando modelos del modo sencillo…"));

    if (!controller_download_missing_models (on_controller_event, st)) {
        update_status (st, t_ ("No se pudo iniciar la descarga."));
        update_easy_model_status (st);
    }
}

static void
on_pref_download_clicked (GtkButton *button, gpointer user_data)
{
    AppState *st = user_data;
    start_missing_download (st);
}

static void
build_preferences_tab (AppState *st, GtkNotebook *notebook)
{
    GtkWidget *pref = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top (pref, 12);
    gtk_widget_set_margin_bottom (pref, 12);
    gtk_widget_set_margin_start (pref, 12);
    gtk_widget_set_margin_end (pref, 12);

    /* Modo de modelo */
    GtkWidget *row_mode = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkStringList *modes =
        gtk_string_list_new ((const char *[]) { t_ (k_mode_labels[0]),
                                                t_ (k_mode_labels[1]), NULL });
    st->combo_pref_model_mode = gtk_drop_down_new (G_LIST_MODEL (modes), NULL);
    st->model_mode = modes; /* el modelo vive en st hasta que se sustituya */
    g_object_ref (modes);   /* ref propia de st (el dropdown tiene la suya) */
    gtk_drop_down_set_selected (GTK_DROP_DOWN (st->combo_pref_model_mode),
                                mode_index (config_get_model_mode ()));
    g_signal_connect (st->combo_pref_model_mode, "notify::selected",
                      G_CALLBACK (on_pref_mode_changed), st);
    GtkWidget *label_mode = gtk_label_new ("Modo de modelo:");
    i18n_bind (st, label_mode, I18N_KIND_LABEL, "Modo de modelo:");
    gtk_box_append (GTK_BOX (row_mode), label_mode);
    gtk_box_append (GTK_BOX (row_mode), st->combo_pref_model_mode);
    gtk_box_append (GTK_BOX (pref), row_mode);

    /* Modelo de whisper (personalizado) */
    GtkWidget *row_model = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    st->row_pref_custom = row_model;
    st->entry_pref_model = gtk_entry_new ();
    gtk_widget_set_hexpand (st->entry_pref_model, TRUE);
    i18n_bind (st, st->entry_pref_model, I18N_KIND_PLACEHOLDER,
               "Ruta del modelo .bin preferido");
    gtk_editable_set_text (GTK_EDITABLE (st->entry_pref_model),
                           config_get_whisper_model ());
    GtkWidget *btn_model = gtk_button_new_with_label ("…");
    g_signal_connect (btn_model, "clicked", G_CALLBACK (on_pick_pref_model), st);
    g_signal_connect (st->entry_pref_model, "changed",
                      G_CALLBACK (on_pref_model_changed), st);
    GtkWidget *label_custom = gtk_label_new ("Modelo personalizado:");
    i18n_bind (st, label_custom, I18N_KIND_LABEL, "Modelo personalizado:");
    gtk_box_append (GTK_BOX (row_model), label_custom);
    gtk_box_append (GTK_BOX (row_model), st->entry_pref_model);
    gtk_box_append (GTK_BOX (row_model), btn_model);
    gtk_box_append (GTK_BOX (pref), row_model);

    /* Modo sencillo (modelos auto-descargables) */
    GtkWidget *row_easy = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    st->row_pref_easy = row_easy;
    GtkWidget *row_easy_inner = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *label_easy_models = gtk_label_new ("Modelos del modo sencillo:");
    i18n_bind (st, label_easy_models, I18N_KIND_LABEL, "Modelos del modo sencillo:");
    gtk_box_append (GTK_BOX (row_easy_inner), label_easy_models);
    st->btn_pref_download = gtk_button_new_with_label ("Descargar modelos");
    i18n_bind (st, st->btn_pref_download, I18N_KIND_BUTTON, "Descargar modelos");
    g_signal_connect (st->btn_pref_download, "clicked",
                      G_CALLBACK (on_pref_download_clicked), st);
    gtk_box_append (GTK_BOX (row_easy_inner), st->btn_pref_download);
    gtk_box_append (GTK_BOX (row_easy), row_easy_inner);

    const KnownModel *km = models_known ();
    for (int i = 0; i < 3 && km[i].id != NULL; i++) {
        GtkWidget *r = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_append (GTK_BOX (r), gtk_label_new (km[i].label));
        st->label_pref_easy_status[i] = gtk_label_new ("");
        gtk_widget_set_hexpand (st->label_pref_easy_status[i], TRUE);
        gtk_label_set_xalign (GTK_LABEL (st->label_pref_easy_status[i]), 0.0f);
        gtk_box_append (GTK_BOX (r), st->label_pref_easy_status[i]);
        gtk_box_append (GTK_BOX (row_easy), r);
    }
    gtk_box_append (GTK_BOX (pref), row_easy);
    update_easy_model_status (st);
    apply_model_mode_visibility (st);

    /* Carpeta de salida */
    GtkWidget *row_out = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    st->entry_pref_output = gtk_entry_new ();
    gtk_widget_set_hexpand (st->entry_pref_output, TRUE);
    i18n_bind (st, st->entry_pref_output, I18N_KIND_PLACEHOLDER,
               "Carpeta donde guardar las transcripciones");
    gtk_editable_set_text (GTK_EDITABLE (st->entry_pref_output),
                           config_get_output_dir ());
    GtkWidget *btn_out = gtk_button_new_with_label ("…");
    g_signal_connect (btn_out, "clicked", G_CALLBACK (on_pick_pref_output), st);
    g_signal_connect (st->entry_pref_output, "changed",
                      G_CALLBACK (on_pref_output_changed), st);
    GtkWidget *label_out = gtk_label_new ("Carpeta de salida:");
    i18n_bind (st, label_out, I18N_KIND_LABEL, "Carpeta de salida:");
    gtk_box_append (GTK_BOX (row_out), label_out);
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
    GtkWidget *label_lang_pref = gtk_label_new ("Idioma por defecto:");
    i18n_bind (st, label_lang_pref, I18N_KIND_LABEL, "Idioma por defecto:");
    gtk_box_append (GTK_BOX (row_lang), label_lang_pref);
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
    GtkWidget *label_fmt_pref = gtk_label_new ("Formato por defecto:");
    i18n_bind (st, label_fmt_pref, I18N_KIND_LABEL, "Formato por defecto:");
    gtk_box_append (GTK_BOX (row_fmt), label_fmt_pref);
    gtk_box_append (GTK_BOX (row_fmt), st->combo_pref_format);
    gtk_box_append (GTK_BOX (pref), row_fmt);

    /* Idioma de la interfaz (cambio en caliente) */
    GtkWidget *row_ui_lang = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    st->combo_pref_ui_lang = make_ui_lang_dropdown (st);
    gtk_drop_down_set_selected (
        GTK_DROP_DOWN (st->combo_pref_ui_lang),
        ui_lang_code_index (st, config_get_ui_language (), 0));
    g_signal_connect (st->combo_pref_ui_lang, "notify::selected",
                      G_CALLBACK (on_pref_ui_lang_changed), st);
    GtkWidget *label_ui_lang = gtk_label_new ("Idioma de la interfaz:");
    i18n_bind (st, label_ui_lang, I18N_KIND_LABEL, "Idioma de la interfaz:");
    gtk_box_append (GTK_BOX (row_ui_lang), label_ui_lang);
    gtk_box_append (GTK_BOX (row_ui_lang), st->combo_pref_ui_lang);
    gtk_box_append (GTK_BOX (pref), row_ui_lang);

    /* Ruta del archivo y nota (el texto se regenera en apply_translations). */
    st->label_hint = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (st->label_hint), 0.0f);
    gtk_label_set_wrap (GTK_LABEL (st->label_hint), TRUE);
    gtk_widget_set_margin_top (st->label_hint, 16);
    gtk_box_append (GTK_BOX (pref), st->label_hint);

    GtkWidget *tab_pref = gtk_label_new ("Preferencias");
    i18n_bind (st, tab_pref, I18N_KIND_LABEL, "Preferencias");
    gtk_notebook_append_page (notebook, pref, tab_pref);
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
    GtkWidget *tab_tr = gtk_label_new ("Transcripción");
    i18n_bind (st, tab_tr, I18N_KIND_LABEL, "Transcripción");
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), root, tab_tr);

    /* ---- Entrada ------------------------------------------------------- */
    GtkWidget *row_in = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

    st->entry_input = gtk_entry_new ();
    gtk_editable_set_editable (GTK_EDITABLE (st->entry_input), FALSE);
    gtk_widget_set_hexpand (st->entry_input, TRUE);
    i18n_bind (st, st->entry_input, I18N_KIND_PLACEHOLDER,
               "Carpeta o archivos a procesar");

    GtkWidget *btn_folder = gtk_button_new_with_label ("Carpeta…");
    GtkWidget *btn_files  = gtk_button_new_with_label ("Archivos…");
    i18n_bind (st, btn_folder, I18N_KIND_BUTTON, "Carpeta…");
    i18n_bind (st, btn_files,  I18N_KIND_BUTTON, "Archivos…");
    g_signal_connect (btn_folder, "clicked", G_CALLBACK (on_pick_input_folder), st);
    g_signal_connect (btn_files,  "clicked", G_CALLBACK (on_pick_input_files),  st);

    GtkWidget *label_in = gtk_label_new ("Entrada:");
    i18n_bind (st, label_in, I18N_KIND_LABEL, "Entrada:");
    gtk_box_append (GTK_BOX (row_in), label_in);
    gtk_box_append (GTK_BOX (row_in), st->entry_input);
    gtk_box_append (GTK_BOX (row_in), btn_folder);
    gtk_box_append (GTK_BOX (row_in), btn_files);
    gtk_box_append (GTK_BOX (root), row_in);

    /* ---- Lista de archivos --------------------------------------------- */
    st->list_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    st->rows     = g_ptr_array_new ();

    st->list_placeholder =
        gtk_label_new ("Sin archivos. Selecciona una carpeta o agrega archivos.");
    i18n_bind (st, st->list_placeholder, I18N_KIND_LABEL,
               "Sin archivos. Selecciona una carpeta o agrega archivos.");
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

    GtkWidget *label_out_tr = gtk_label_new ("Salida:");
    i18n_bind (st, label_out_tr, I18N_KIND_LABEL, "Salida:");
    gtk_box_append (GTK_BOX (row_out), label_out_tr);
    gtk_box_append (GTK_BOX (row_out), st->entry_output);
    gtk_box_append (GTK_BOX (row_out), btn_out);
    gtk_box_append (GTK_BOX (root), row_out);

    /* ---- Modelo (según el modo: sencillo o personalizado) ---------------- */
    st->box_model = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);

    /* Vista modo personalizado: ruta + selector de archivo. */
    GtkWidget *row_model = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    st->row_model_custom = row_model;
    st->entry_model = gtk_entry_new ();
    gtk_widget_set_hexpand (st->entry_model, TRUE);
    gtk_editable_set_text (GTK_EDITABLE (st->entry_model), config_get_whisper_model ());
    i18n_bind (st, st->entry_model, I18N_KIND_PLACEHOLDER,
               "Selecciona el modelo .bin (o define WHISPER_MODEL)");

    GtkWidget *btn_model = gtk_button_new_with_label ("…");
    g_signal_connect (btn_model, "clicked", G_CALLBACK (on_pick_model_file), st);

    GtkWidget *label_model = gtk_label_new ("Modelo whisper:");
    i18n_bind (st, label_model, I18N_KIND_LABEL, "Modelo whisper:");
    gtk_box_append (GTK_BOX (row_model), label_model);
    gtk_box_append (GTK_BOX (row_model), st->entry_model);
    gtk_box_append (GTK_BOX (row_model), btn_model);
    gtk_box_append (GTK_BOX (st->box_model), row_model);

    /* Vista modo sencillo: 3 radios (Preciso / Equilibrado / Rápido). */
    st->box_model_easy = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *easy_title = gtk_label_new ("Modelo (modo sencillo):");
    i18n_bind (st, easy_title, I18N_KIND_LABEL, "Modelo (modo sencillo):");
    gtk_label_set_xalign (GTK_LABEL (easy_title), 0.0f);
    gtk_box_append (GTK_BOX (st->box_model_easy), easy_title);

    const KnownModel *km = models_known ();
    GtkWidget *group = NULL;
    for (int i = 0; i < 3 && km[i].id != NULL; i++) {
        GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

        GtkWidget *radio = gtk_check_button_new_with_label (t_ (km[i].label));
        if (i == 0)
            group = radio;
        else
            gtk_check_button_set_group (GTK_CHECK_BUTTON (radio),
                                        GTK_CHECK_BUTTON (group));
        st->radio_easy[i] = radio;
        i18n_bind (st, radio, I18N_KIND_BUTTON, km[i].label);
        if (g_ascii_strcasecmp (km[i].id, config_get_easy_model ()) == 0)
            gtk_check_button_set_active (GTK_CHECK_BUTTON (radio), TRUE);
        g_signal_connect (radio, "toggled", G_CALLBACK (on_easy_radio_toggled), st);

        st->label_easy_status[i] = gtk_label_new ("");
        gtk_widget_set_hexpand (st->label_easy_status[i], TRUE);
        gtk_label_set_xalign (GTK_LABEL (st->label_easy_status[i]), 0.0f);
        gtk_label_set_wrap (GTK_LABEL (st->label_easy_status[i]), TRUE);

        gtk_box_append (GTK_BOX (row), radio);
        gtk_box_append (GTK_BOX (row), st->label_easy_status[i]);
        gtk_box_append (GTK_BOX (st->box_model_easy), row);
    }
    gtk_box_append (GTK_BOX (st->box_model), st->box_model_easy);
    gtk_box_append (GTK_BOX (root), st->box_model);

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

    GtkWidget *label_lang_tr = gtk_label_new ("Idioma:");
    GtkWidget *label_fmt_tr  = gtk_label_new ("Formato:");
    i18n_bind (st, label_lang_tr, I18N_KIND_LABEL, "Idioma:");
    i18n_bind (st, label_fmt_tr,  I18N_KIND_LABEL, "Formato:");
    gtk_box_append (GTK_BOX (row_lang), label_lang_tr);
    gtk_box_append (GTK_BOX (row_lang), st->combo_lang);
    gtk_box_append (GTK_BOX (row_lang), label_fmt_tr);
    gtk_box_append (GTK_BOX (row_lang), st->combo_format);
    gtk_box_append (GTK_BOX (root), row_lang);

    /* ---- Progreso y estado ---------------------------------------------- */
    st->progress = gtk_progress_bar_new ();
    gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (st->progress), TRUE);
    gtk_box_append (GTK_BOX (root), st->progress);

    st->label_status = gtk_label_new ("Listo.");
    i18n_bind (st, st->label_status, I18N_KIND_LABEL, "Listo.");
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
    i18n_bind (st, btn_clear,   I18N_KIND_BUTTON, "Limpiar lista");
    i18n_bind (st, btn_process, I18N_KIND_BUTTON, "Procesar");
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

    if (st->i18n_cb_id != 0)
        i18n_remove_changed_cb (st->i18n_cb_id);
    if (g_i18n_apply_idle != 0) {
        g_source_remove (g_i18n_apply_idle);
        g_i18n_apply_idle = 0;
    }
    if (st->i18n_binds != NULL)
        g_ptr_array_free (st->i18n_binds, TRUE);
    if (st->ui_lang_codes != NULL)
        g_ptr_array_free (st->ui_lang_codes, TRUE);
    g_clear_object (&st->model_ui_lang);
    g_clear_object (&st->model_mode);

    if (st->files != NULL)
        g_ptr_array_free (st->files, TRUE);
    if (st->rows != NULL)
        g_ptr_array_free (st->rows, TRUE);
    g_free (st);
}

void
gui_activate (GtkApplication *app)
{
    AppState *st = g_new0 (AppState, 1);
    st->files = g_ptr_array_new_with_free_func (g_free);
    st->rows  = g_ptr_array_new ();
    st->i18n_binds = g_ptr_array_new_with_free_func (free_i18n_bind);

    /* Internacionalización: idioma guardado (o env TRANSCRIPT_UI_LANG). */
    i18n_init ();
    if (g_getenv ("TRANSCRIPT_UI_LANG") == NULL)
        i18n_set_language (config_get_ui_language ());
    st->i18n_cb_id = i18n_add_changed_cb (on_i18n_changed, st);

    build_ui (st, app);

    g_signal_connect (st->window, "destroy", G_CALLBACK (on_window_destroy), st);
    g_signal_connect (st->window, "close-request", G_CALLBACK (on_close_request), st);

    /* Arranque: en modo sencillo, descargar automáticamente los modelos que
     * falten (solo la primera vez; después quedan cacheados). */
    if (g_ascii_strcasecmp (config_get_model_mode (), "easy") == 0) {
        if (models_first_missing () != NULL) {
            log_line (st, "Faltan modelos del modo sencillo; iniciando descarga automática…");
            start_missing_download (st);
        } else {
            update_easy_model_status (st);
        }
    }

    /* Textos y dirección iniciales (también rellena la nota de guardado). */
    apply_translations (st);

    gtk_window_present (GTK_WINDOW (st->window));
}




