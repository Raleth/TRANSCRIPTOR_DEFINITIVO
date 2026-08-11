#include "models.h"

#include <glib/gstdio.h>
#include <string.h>

/* Los 3 modelos del modo sencillo. */
static const KnownModel k_models[] = {
    { "large-v3-turbo", "ggml-large-v3-turbo.bin", "Preciso",
      "Máxima fidelidad del habla; más lento y ocupa ~1.6 GB." },
    { "medium",         "ggml-medium.bin",         "Equilibrado",
      "Buen balance entre calidad y velocidad; ~1.5 GB." },
    { "small",          "ggml-small.bin",          "Rápido",
      "El más veloz y ligero; algo menos preciso; ~0.5 GB." },
    { NULL, NULL, NULL, NULL },
};

static gchar *g_models_dir = NULL;

const KnownModel *
models_known (void)
{
    return k_models;
}

const KnownModel *
models_find (const char *id)
{
    if (id == NULL)
        return NULL;
    for (const KnownModel *m = k_models; m->id != NULL; m++) {
        if (g_ascii_strcasecmp (m->id, id) == 0)
            return m;
    }
    return NULL;
}

const char *
models_get_dir (void)
{
    if (g_models_dir == NULL) {
        g_models_dir = g_build_filename (g_get_user_data_dir (),
                                         "anatrans", "models", NULL);
        g_mkdir_with_parents (g_models_dir, 0755);
    }
    return g_models_dir;
}

gchar *
models_path (const char *id)
{
    const KnownModel *m = models_find (id);
    if (m == NULL)
        return NULL;
    return g_build_filename (models_get_dir (), m->filename, NULL);
}

gboolean
models_is_downloaded (const char *id)
{
    gchar *path = models_path (id);
    if (path == NULL)
        return FALSE;
    gboolean ok = g_file_test (path, G_FILE_TEST_IS_REGULAR);
    g_free (path);
    return ok;
}

const KnownModel *
models_first_missing (void)
{
    for (const KnownModel *m = k_models; m->id != NULL; m++) {
        if (!models_is_downloaded (m->id))
            return m;
    }
    return NULL;
}

gboolean
models_download (const char *id, GError **error)
{
    const KnownModel *m = models_find (id);
    if (m == NULL) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                     "Modelo desconocido: %s", id);
        return FALSE;
    }

    if (models_is_downloaded (id))
        return TRUE; /* ya está descargado */

    /* ubicación del script (definida por CMake en MODELS_SCRIPT_DIR) */
#ifdef MODELS_SCRIPT_DIR
    const char *script_dir = MODELS_SCRIPT_DIR;
#else
    const char *script_dir = "scripts"; /* último recurso: CWD */
#endif
    gchar *script = g_build_filename (script_dir, "download-ggml-model.sh", NULL);

    if (!g_file_test (script, G_FILE_TEST_IS_REGULAR)) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "No se encontró el script de descarga: %s", script);
        g_free (script);
        return FALSE;
    }

    /* sh <script> <modelo> <carpeta_models> */
    const char *argv[] = {
        "sh", script, m->id, models_get_dir (), NULL,
    };

    gchar *stderr_buf = NULL;
    gint exit_status = 0;

    gboolean ok = g_spawn_sync (NULL,
                                (char **) argv,
                                NULL,
                                G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
                                NULL, NULL,
                                NULL, &stderr_buf,
                                &exit_status,
                                error);

    if (!ok) {
        if (error != NULL && *error == NULL)
            g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                         "No se pudo ejecutar el script de descarga (¿falta 'sh'?).");
        g_free (stderr_buf);
        g_free (script);
        return FALSE;
    }

    if (exit_status != 0) {
        gchar *detail = (stderr_buf != NULL) ? g_strstrip (stderr_buf) : NULL;
        g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                     "La descarga del modelo %s falló%s%s",
                     m->id,
                     (detail != NULL && *detail != 0) ? ":\n" : "",
                     (detail != NULL && *detail != 0) ? detail : "");
        g_free (detail);
        g_free (stderr_buf);
        g_free (script);
        return FALSE;
    }

    g_free (stderr_buf);
    g_free (script);

    if (!models_is_downloaded (id)) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "El script terminó pero no se generó el archivo del modelo.");
        return FALSE;
    }

    return TRUE;
}
