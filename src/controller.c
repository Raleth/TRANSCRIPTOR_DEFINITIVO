#include "controller.h"

#include "formats.h"
#include "models.h"
#include "process.h"

#include <string.h>

/* Un único lote activo. */
typedef struct {
    gboolean          running;
    char            **files;
    int               n_files;
    char             *output_dir;
    char             *model_path;
    char             *language;
    char             *format_id;
    char             *download_model_id; /* descarga en curso */
    ControllerEventCb event_cb;
    gpointer          user_data;
    GThread          *thread;
} ControllerState;

static ControllerState *state = NULL;

/* ---------------------------------------------------------------------------
 * Entrega de eventos al main loop
 * ------------------------------------------------------------------------- */

typedef struct {
    ControllerEvent   event;
    ControllerEventCb cb;
    gpointer          user_data;
    char             *file;
    char             *phase;
    char             *message;
} EventDispatch;

static void
controller_finish_locked (void);

static gboolean
dispatch_idle (gpointer user_data)
{
    EventDispatch *d = user_data;

    if (d->cb != NULL)
        d->cb (&d->event, d->user_data);

    /* la limpieza del estado se hace en el hilo del main loop */
    if (d->event.type == CONTROLLER_EVENT_FINISHED ||
        d->event.type == CONTROLLER_EVENT_DOWNLOAD_FINISHED ||
        d->event.type == CONTROLLER_EVENT_MODELS_SYNCED)
        controller_finish_locked ();

    return G_SOURCE_REMOVE;
}

static void
dispatch_destroy (gpointer user_data)
{
    EventDispatch *d = user_data;
    g_free (d->file);
    g_free (d->phase);
    g_free (d->message);
    g_free (d);
}

static void
publish_event (ControllerEventType type, int done, int total, int file_percent,
               int ok_count, const char *file, const char *phase,
               const char *message, gboolean success)
{
    EventDispatch *d = g_new0 (EventDispatch, 1);

    d->event.type         = type;
    d->event.done         = done;
    d->event.total        = total;
    d->event.file_percent = file_percent;
    d->event.ok_count     = ok_count;
    d->event.success      = success;
    d->file    = g_strdup (file);
    d->phase   = g_strdup (phase);
    d->message = g_strdup (message);
    d->event.file    = d->file;
    d->event.phase   = d->phase;
    d->event.message = d->message;
    d->cb       = state->event_cb;
    d->user_data = state->user_data;

    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, dispatch_idle, d, dispatch_destroy);
}

/* ---------------------------------------------------------------------------
 * Lote (hilo de trabajo)
 * ------------------------------------------------------------------------- */

static void
on_batch_progress (int files_done, int total_files, int file_percent,
                   const char *current_file, const char *phase, gpointer user_data)
{
    publish_event (CONTROLLER_EVENT_PROGRESS, files_done, total_files,
                   file_percent, 0, current_file, phase, NULL, FALSE);
}

static void
on_batch_report (const ProcessReport *r, gpointer user_data)
{
    publish_event (CONTROLLER_EVENT_REPORT, r->done, r->total, 0, 0,
                   r->file, NULL, r->message, r->success);
}

static gpointer
controller_worker (gpointer user_data)
{
    const FormatWriter *writer = formats_from_string (state->format_id);

    int ok = process_run_batch ((const char *const *) state->files,
                                state->n_files,
                                state->output_dir,
                                state->model_path,
                                state->language,
                                writer,
                                on_batch_progress,
                                on_batch_report,
                                state);

    /* último evento: finalización (la limpieza la hace dispatch_idle) */
    publish_event (CONTROLLER_EVENT_FINISHED, 0, state->n_files, 0, ok,
                   NULL, NULL, NULL, FALSE);
    return NULL;
}

static gpointer
controller_download_worker (gpointer user_data)
{
    const char *id = state->download_model_id;
    const KnownModel *m = models_find (id);

    publish_event (CONTROLLER_EVENT_PROGRESS, 0, 1, 0, 0,
                   (m != NULL) ? m->label : id, "descargando", NULL, FALSE);

    GError *err = NULL;
    gboolean ok = models_download (id, &err);

    publish_event (CONTROLLER_EVENT_DOWNLOAD_FINISHED, ok ? 1 : 0, 1, 0, ok ? 1 : 0,
                   id, "descarga",
                   (err != NULL) ? err->message : (ok ? "listo" : "error"),
                   ok);
    if (err != NULL)
        g_error_free (err);
    return NULL;
}

static gpointer
controller_download_missing_worker (gpointer user_data)
{
    const KnownModel *km = models_known ();
    int total = 0;
    for (int i = 0; km[i].id != NULL; i++)
        total++;

    int done = 0, ok_count = 0;

    for (int i = 0; km[i].id != NULL; i++) {
        if (models_is_downloaded (km[i].id)) {
            done++;
            ok_count++;
            continue;
        }

        publish_event (CONTROLLER_EVENT_PROGRESS, done, total, 0, 0,
                       km[i].label, "descargando", NULL, FALSE);

        GError *err = NULL;
        gboolean ok = models_download (km[i].id, &err);
        if (ok)
            ok_count++;
        gchar *msg = (err != NULL) ? g_strdup (err->message)
                                   : g_strdup (ok ? "listo" : "error");
        if (err != NULL)
            g_error_free (err);

        /* avance por modelo; no se emite DOWNLOAD_FINISHED aquí porque
         * dispararía la limpieza del estado en mitad del bucle. */
        publish_event (CONTROLLER_EVENT_PROGRESS, done + 1, total, 100, 0,
                       km[i].label, "descargado", msg, ok);
        g_free (msg);
        done++;
    }

    /* último evento del hilo: fin de la auto-descarga completa. */
    publish_event (CONTROLLER_EVENT_DOWNLOAD_FINISHED, ok_count, total, 0, ok_count,
                   NULL, "descarga", (ok_count == total) ? "listo" : "parcial",
                   ok_count == total);
    publish_event (CONTROLLER_EVENT_MODELS_SYNCED, done, total, 0, ok_count,
                   NULL, NULL, NULL, ok_count == total);
    return NULL;
}

/* ---------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

void
controller_init (void)
{
    if (state == NULL)
        state = g_new0 (ControllerState, 1);
}

static void
controller_finish_locked (void)
{
    if (state == NULL)
        return;

    if (state->thread != NULL) {
        g_thread_join (state->thread); /* el worker ya terminó */
        state->thread = NULL;
    }

    for (int i = 0; i < state->n_files; i++)
        g_free (state->files[i]);
    g_free (state->files);
    g_free (state->output_dir);
    g_free (state->model_path);
    g_free (state->language);
    g_free (state->format_id);
    g_clear_pointer (&state->download_model_id, g_free);
    state->files      = NULL;
    state->output_dir = NULL;
    state->model_path = NULL;
    state->language   = NULL;
    state->format_id  = NULL;
    state->n_files    = 0;
    state->running    = FALSE;
    state->event_cb   = NULL;
    state->user_data  = NULL;
}

gboolean
controller_start (const ControllerJob *job, ControllerEventCb event_cb,
                  gpointer user_data)
{
    controller_init ();

    if (job == NULL || job->files == NULL || job->n_files <= 0 ||
        job->output_dir == NULL || job->model_path == NULL)
        return FALSE;
    if (state->running)
        return FALSE;

    g_clear_pointer (&state->download_model_id, g_free);

    state->files = g_new0 (gchar *, (gsize) job->n_files);
    for (int i = 0; i < job->n_files; i++)
        state->files[i] = g_strdup (job->files[i]);
    state->n_files     = job->n_files;
    state->output_dir  = g_strdup (job->output_dir);
    state->model_path  = g_strdup (job->model_path);
    state->language    = g_strdup ((job->language != NULL) ? job->language : "auto");
    state->format_id   = g_strdup ((job->format_id != NULL) ? job->format_id : "");
    state->event_cb    = event_cb;
    state->user_data   = user_data;
    state->running     = TRUE;
    state->thread      = NULL;

    state->thread = g_thread_new ("controller", controller_worker, NULL);
    return TRUE;
}

gboolean
controller_download_model (const char *model_id, ControllerEventCb event_cb,
                          gpointer user_data)
{
    controller_init ();
    if (model_id == NULL || models_find (model_id) == NULL)
        return FALSE;
    if (state->running)
        return FALSE;

    state->download_model_id = g_strdup (model_id);
    state->event_cb    = event_cb;
    state->user_data   = user_data;
    state->running     = TRUE;
    state->thread      = NULL;

    state->thread = g_thread_new ("controller-dl", controller_download_worker, NULL);
    return TRUE;
}

gboolean
controller_download_missing_models (ControllerEventCb event_cb, gpointer user_data)
{
    controller_init ();
    if (state->running)
        return FALSE;
    if (models_first_missing () == NULL)
        return FALSE; /* no hay nada que descargar */

    g_clear_pointer (&state->download_model_id, g_free);
    state->event_cb    = event_cb;
    state->user_data   = user_data;
    state->running     = TRUE;
    state->thread      = NULL;

    state->thread = g_thread_new ("controller-dl3", controller_download_missing_worker, NULL);
    return TRUE;
}

gboolean
controller_is_running (void)
{
    if (state == NULL)
        return FALSE;
    return state->running;
}

void
controller_shutdown (void)
{
    if (state == NULL)
        return;

    if (state->running) {
        /* esperar a que termine el lote y liberar */
        if (state->thread != NULL)
            g_thread_join (state->thread);
        state->running = FALSE;
    }
    controller_finish_locked ();
    g_clear_pointer (&state, g_free);
}
