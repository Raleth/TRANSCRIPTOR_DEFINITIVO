#include "batch.h"

#include <glib/gstdio.h>
#include <string.h>

/* Extensiones de audio/video reconocidas (en minúsculas). */
static const char *const k_media_extensions[] = {
    ".mp3", ".wav", ".m4a", ".mp4", ".mkv", ".mov",
    ".flac", ".ogg", ".opus", ".webm", ".aac", ".avi", ".wmv",
};

static const int k_media_extensions_len = G_N_ELEMENTS (k_media_extensions);

gboolean
batch_is_media_path (const char *path)
{
    if (path == NULL || *path == '\0')
        return FALSE;

    const char *dot = strrchr (path, '.');
    if (dot == NULL || dot == path)
        return FALSE;

    gchar *ext = g_ascii_strdown (dot, -1);
    gboolean found = FALSE;
    for (int i = 0; i < k_media_extensions_len && !found; i++) {
        if (g_strcmp0 (ext, k_media_extensions[i]) == 0)
            found = TRUE;
    }
    g_free (ext);
    return found;
}

static gboolean
is_regular_file (const char *path)
{
    return g_file_test (path, G_FILE_TEST_IS_REGULAR);
}

/* ¿La lista contiene ya esta ruta? (comparación por contenido). */
static gboolean
list_contains (GPtrArray *list, const char *path)
{
    for (guint i = 0; i < list->len; i++) {
        const char *item = g_ptr_array_index (list, i);
        if (g_strcmp0 (item, path) == 0)
            return TRUE;
    }
    return FALSE;
}

static gint
compare_paths (gconstpointer a, gconstpointer b)
{
    return g_strcmp0 (*(const char *const *) a,
                      *(const char *const *) b);
}

GPtrArray *
batch_scan_folder (const char *folder, GError **error)
{
    g_return_val_if_fail (folder != NULL && *folder != '\0', NULL);

    if (!g_file_test (folder, G_FILE_TEST_IS_DIR)) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "No es una carpeta: %s", folder);
        return NULL;
    }

    GDir *dir = g_dir_open (folder, 0, error);
    if (dir == NULL)
        return NULL;

    GPtrArray *files = g_ptr_array_new_with_free_func (g_free);
    const char *name;

    while ((name = g_dir_read_name (dir)) != NULL) {
        /* ignorar archivos ocultos */
        if (name[0] == '.')
            continue;

        gchar *path = g_build_filename (folder, name, NULL);
        if (is_regular_file (path) && batch_is_media_path (path)) {
            if (!list_contains (files, path))
                g_ptr_array_add (files, path);
            else
                g_free (path);
        } else {
            g_free (path);
        }
    }
    g_dir_close (dir);

    g_ptr_array_sort (files, compare_paths);
    return files;
}

GPtrArray *
batch_filter_file_list (char **paths, int n_paths)
{
    g_return_val_if_fail (paths != NULL, NULL);

    GPtrArray *files = g_ptr_array_new_with_free_func (g_free);

    for (int i = 0; i < n_paths; i++) {
        const char *path = paths[i];
        if (path == NULL || *path == '\0')
            continue;
        if (!is_regular_file (path) || !batch_is_media_path (path))
            continue;
        if (!list_contains (files, path))
            g_ptr_array_add (files, g_strdup (path));
    }

    g_ptr_array_sort (files, compare_paths);
    return files;
}

void
batch_free_file_list (GPtrArray *list)
{
    if (list != NULL)
        g_ptr_array_free (list, TRUE);
}
