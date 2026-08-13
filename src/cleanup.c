/* ============================================================================
 * cleanup.c — carpeta temporal para WAV intermedios y limpieza ante señales.
 *
 * [PROYECTO]    Ana-Trans: gestión del dir temporal y barrido de huérfanos.
 * [DEPENDENCIA] GLib (rutas, g_dir_make_tmp); señales POSIX / libc.
 * ========================================================================== */

#include "cleanup.h"

/* dependencias (GLib y libc del sistema) */
#include <glib/gstdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <process.h>
#define unlink _unlink
#define rmdir _rmdir
#else
#include <unistd.h>
#endif

#define TEMP_PREFIX     "anatrans-XXXXXX"
#define PATH_BUF_LEN    4096
#define STALE_AGE_SECS  3600  /* dirs temporales de >1 h se consideran huérfanos */

static gchar *g_wav_dir = NULL;         /* dir temporal actual */
static gchar *g_current_wav = NULL;     /* WAV en curso (contexto normal) */
static char g_signal_wav[PATH_BUF_LEN]; /* copia para el handler de señal */
static char g_signal_dir[PATH_BUF_LEN];
static volatile sig_atomic_t g_in_signal = 0;
static gboolean g_handlers_set = FALSE;

/* ---------------------------------------------------------------------------
 * Utilidades
 * ------------------------------------------------------------------------- */

static void
remove_dir_recursive (const char *dir)
{
    GDir *d = g_dir_open (dir, 0, NULL);
    if (d == NULL)
        return;

    const char *name;
    while ((name = g_dir_read_name (d)) != NULL) {
        gchar *path = g_build_filename (dir, name, NULL);
        if (g_file_test (path, G_FILE_TEST_IS_DIR))
            remove_dir_recursive (path);
        else
            g_remove (path);
        g_free (path);
    }
    g_dir_close (d);
    g_rmdir (dir);
}

static void
set_signal_handler (int sig, void (*handler) (int))
{
    struct sigaction sa;
    memset (&sa, 0, sizeof (sa));
    sa.sa_handler = handler;
    sigemptyset (&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction (sig, &sa, NULL);
}

/*
 * Barre g_get_tmp_dir() y borra los dirs anatrans-* huérfanos (>1 h)
 * dejados por ejecuciones anteriores interrumpidas (crash, SIGKILL).
 */
static void
sweep_stale_dirs (void)
{
    const char *tmp = g_get_tmp_dir ();
    GDir *d = g_dir_open (tmp, 0, NULL);
    if (d == NULL)
        return;

    const char *name;
    while ((name = g_dir_read_name (d)) != NULL) {
        if (!g_str_has_prefix (name, "anatrans-"))
            continue;

        gchar *path = g_build_filename (tmp, name, NULL);
        if (g_wav_dir != NULL && g_strcmp0 (path, g_wav_dir) == 0) {
            g_free (path);
            continue;
        }

        GStatBuf st;
        if (g_stat (path, &st) == 0) {
            time_t now = time (NULL);
            if (now > st.st_mtime && (now - st.st_mtime) > STALE_AGE_SECS)
                remove_dir_recursive (path);
        }
        g_free (path);
    }
    g_dir_close (d);
}

/* ---------------------------------------------------------------------------
 * Manejo de señales (solo operaciones async-signal-safe: unlink/rmdir/_exit)
 * ------------------------------------------------------------------------- */

static void
signal_cleanup_handler (int signum)
{
    if (g_in_signal)
        _exit (128 + signum);
    g_in_signal = 1;

    if (g_signal_wav[0] != '\0')
        unlink (g_signal_wav);
    if (g_signal_dir[0] != '\0')
        rmdir (g_signal_dir);

    _exit (128 + signum);
}

static void
atexit_cleanup (void)
{
    cleanup_finish ();
}

/* ---------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

gboolean
cleanup_begin (void)
{
    if (g_wav_dir != NULL)
        return TRUE; /* ya creado */

    GError *err = NULL;
    gchar *dir = g_dir_make_tmp (TEMP_PREFIX, &err);
    if (dir == NULL) {
        g_warning ("No se pudo crear la carpeta temporal: %s",
                   (err != NULL) ? err->message : "?");
        if (err != NULL)
            g_error_free (err);
        return FALSE;
    }
    g_wav_dir = dir;

    /* dejar el path del dir listo para el handler de señal desde ya
     * (la carga del modelo ocurre antes del primer archivo). */
    memset (g_signal_dir, 0, sizeof (g_signal_dir));
    g_strlcpy (g_signal_dir, g_wav_dir, sizeof (g_signal_dir));

    if (!g_handlers_set) {
        atexit (atexit_cleanup);
        set_signal_handler (SIGINT, signal_cleanup_handler);
        set_signal_handler (SIGTERM, signal_cleanup_handler);
#ifndef _WIN32
        set_signal_handler (SIGHUP, signal_cleanup_handler);
#endif
        g_handlers_set = TRUE;
    }

    /* limpiar restos de ejecuciones anteriores interrumpidas */
    sweep_stale_dirs ();

    return TRUE;
}

const char *
cleanup_get_wav_dir (void)
{
    return g_wav_dir;
}

void
cleanup_set_current_wav (const char *path)
{
    g_free (g_current_wav);
    g_current_wav = g_strdup ((path != NULL) ? path : "");

    /* copias para el handler de señal */
    memset (g_signal_wav, 0, sizeof (g_signal_wav));
    if (g_current_wav != NULL && *g_current_wav != '\0')
        g_strlcpy (g_signal_wav, g_current_wav, sizeof (g_signal_wav));

    memset (g_signal_dir, 0, sizeof (g_signal_dir));
    if (g_wav_dir != NULL)
        g_strlcpy (g_signal_dir, g_wav_dir, sizeof (g_signal_dir));
}

void
cleanup_clear_current_wav (void)
{
    cleanup_set_current_wav (NULL);
}

void
cleanup_finish (void)
{
    if (g_wav_dir == NULL)
        return;

    if (g_current_wav != NULL && *g_current_wav != '\0')
        g_remove (g_current_wav);

    remove_dir_recursive (g_wav_dir);

    g_clear_pointer (&g_current_wav, g_free);
    g_clear_pointer (&g_wav_dir, g_free);
    memset (g_signal_wav, 0, sizeof (g_signal_wav));
    memset (g_signal_dir, 0, sizeof (g_signal_dir));

    if (g_handlers_set) {
        set_signal_handler (SIGINT, SIG_DFL);
        set_signal_handler (SIGTERM, SIG_DFL);
#ifndef _WIN32
        set_signal_handler (SIGHUP, SIG_DFL);
#endif
        g_handlers_set = FALSE;
    }
}
