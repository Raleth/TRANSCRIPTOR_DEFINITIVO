#include "audio.h"

#include "config.h"

#include <glib/gstdio.h>
#include <string.h>

gboolean
audio_convert_to_wav (const char *input, const char *output, GError **error)
{
    g_return_val_if_fail (input != NULL && *input != '\0', FALSE);
    g_return_val_if_fail (output != NULL && *output != '\0', FALSE);

    /* Asegurar que el directorio de salida existe. */
    gchar *out_dir = g_path_get_dirname (output);
    if (g_mkdir_with_parents (out_dir, 0755) != 0) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "No se pudo crear la carpeta de salida: %s", out_dir);
        g_free (out_dir);
        return FALSE;
    }
    g_free (out_dir);

    const char *ffmpeg = config_get_ffmpeg_bin ();

    /* ffmpeg -y -loglevel error -i <input> -ar 16000 -ac 1 -c:a pcm_s16le <output> */
    const char *argv[] = {
        ffmpeg,
        "-y",
        "-loglevel", "error",
        "-i", input,
        "-ar", "16000",
        "-ac", "1",
        "-c:a", "pcm_s16le",
        output,
        NULL,
    };

    gchar *stdout_buf = NULL;
    gchar *stderr_buf = NULL;
    gint exit_status = 0;

    gboolean ok = g_spawn_sync (NULL,
                                (char **) argv,
                                NULL, /* env: hereda */
                                G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
                                NULL, NULL,
                                NULL, &stderr_buf, /* stdout: se descarta */
                                &exit_status,
                                error);

    if (!ok) {
        /* Error al lanzar ffmpeg (p. ej. binario no instalado). */
        if (error != NULL && *error == NULL)
            g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                         "No se pudo ejecutar ffmpeg ('%s'). Instálalo o define $FFMPEG_BIN.",
                         ffmpeg);
        g_free (stdout_buf);
        g_free (stderr_buf);
        return FALSE;
    }

    if (exit_status != 0) {
        gchar *detail = (stderr_buf != NULL) ? g_strstrip (stderr_buf) : NULL;
        g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                     "ffmpeg falló al convertir '%s'%s%s",
                     input,
                     (detail != NULL && *detail != '\0') ? ":\n" : "",
                     (detail != NULL && *detail != '\0') ? detail : "");
        /* g_strstrip modifica stderr_buf in-place y devuelve el mismo
         * puntero: se libera una sola vez. */
        g_free (stdout_buf);
        g_free (stderr_buf);
        return FALSE;
    }

    /* Comprobar que el WAV realmente se generó. */
    if (!g_file_test (output, G_FILE_TEST_IS_REGULAR)) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "ffmpeg terminó pero no se generó el archivo: %s", output);
        g_free (stdout_buf);
        g_free (stderr_buf);
        return FALSE;
    }

    g_free (stdout_buf);
    g_free (stderr_buf);
    return TRUE;
}

gchar *
audio_build_wav_path (const char *output_dir, const char *input)
{
    gchar *base = g_path_get_basename (input);

    char *dot = strrchr (base, '.');
    if (dot != NULL)
        *dot = '\0';

    gchar *name = g_strdup_printf ("%s_audio.wav", base);
    gchar *out  = g_build_filename (output_dir, name, NULL);

    g_free (name);
    g_free (base);
    return out;
}
