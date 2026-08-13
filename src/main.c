/* módulos del proyecto */
#include "batch.h"
#include "config.h"
#include "controller.h"
#include "gui.h"
#include "process.h"
#include "formats.h"
#include "transcribe.h"

/* dependencias (GLib / GTK) */
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <stdio.h>

/*
 * main.c — punto de entrada de Ana-Trans.
 *
 *   ./transcriptor                      -> abre la GUI GTK
 *   ./transcriptor /ruta/carpeta        -> convierte y transcribe los audio/video
 *   ./transcriptor a.mp4 b.mkv          -> idem con archivos sueltos
 *
 * Opciones (variables de entorno):
 *   WHISPER_MODEL       -> ruta al modelo .bin de whisper (modo personalizado)
 *   OUTPUT_DIR          -> carpeta de salida
 *   TRANSCRIPT_LANG     -> "auto", "es", "en", ... (por defecto "auto")
 *   TRANSCRIPT_FORMAT   -> "txt", "markdown", "latex", "docx", "pdf"
 *                          (por defecto "docx")
 *
 * [PROYECTO]    arranque y despacho CLI/GUI.
 * [DEPENDENCIA] GTK 4 (g_application_run), GLib.
 */

static void
cli_report (const ProcessReport *r, gpointer user_data)
{
    g_print ("  [%d/%d] %s -> %s\n", r->done, r->total, r->file,
             r->success ? "OK" : "ERROR");
    if (!r->success && r->message != NULL)
        g_print ("           %s\n", r->message);
}

static int
cli_process (const char *const *paths, int n_paths)
{
    GPtrArray *all = g_ptr_array_new_with_free_func (g_free);

    for (int i = 0; i < n_paths; i++) {
        const char *p = paths[i];

        if (g_file_test (p, G_FILE_TEST_IS_DIR)) {
            GError *err = NULL;
            GPtrArray *files = batch_scan_folder (p, &err);
            if (err != NULL) {
                g_printerr ("Error al escanear %s: %s\n", p, err->message);
                g_error_free (err);
                continue;
            }
            for (guint j = 0; j < files->len; j++)
                g_ptr_array_add (all, g_ptr_array_steal_index (files, j));
            g_ptr_array_free (files, TRUE);
        }
        else if (batch_is_media_path (p)) {
            g_ptr_array_add (all, g_strdup (p));
        }
        else {
            g_printerr ("No es carpeta ni archivo de audio/video: %s\n", p);
        }
    }

    if (all->len == 0) {
        g_ptr_array_free (all, TRUE);
        g_printerr ("No hay archivos de audio/video para procesar.\n");
        return 1;
    }

    /* Carpeta de salida: $OUTPUT_DIR, o <carpeta>/transcripciones, o "transcripciones". */
    gchar *out;
    const char *out_env = g_getenv ("OUTPUT_DIR");
    if (out_env != NULL && *out_env != '\0') {
        out = g_strdup (out_env);
    } else if (n_paths == 1 && g_file_test (paths[0], G_FILE_TEST_IS_DIR)) {
        out = g_build_filename (paths[0], config_get_output_dir (), NULL);
    } else {
        out = g_strdup (config_get_output_dir ());
    }

    /* Configuración de la transcripción. */
    const char *model = config_get_whisper_model ();
    if (model == NULL || *model == '\0') {
        g_printerr ("Falta el modelo de whisper. Define WHISPER_MODEL con la ruta al .bin.\n");
        g_free (out);
        batch_free_file_list (all);
        return 1;
    }

    const char *lang   = config_get_transcript_lang ();
    const char *fmt    = config_get_transcript_format ();
    const FormatWriter *writer = formats_from_string (fmt);

    g_print ("Procesando %u archivo(s) (ffmpeg -> whisper):\n", all->len);
    g_print ("  Modelo : %s\n", model);
    g_print ("  Idioma : %s\n", lang);
    g_print ("  Formato: %s\n", fmt);
    g_print ("  Salida : %s\n", out);

    guint total = all->len;
    int ok = process_run_batch ((const char *const *) all->pdata,
                                (int) total, out, model, lang, writer,
                                NULL, /* sin barra de progreso en CLI */
                                cli_report, NULL);

    g_print ("Resumen: %d de %u procesados correctamente.\n", ok, total);

    g_free (out);
    batch_free_file_list (all);
    return (ok == (int) total) ? 0 : 1;
}

int
main (int argc, char **argv)
{
    /*
     * El GTK instalado por vcpkg se compila solo con backend X11
     * (gtk4.pc: targets=x11; sin símbolos wayland). Las sesiones Wayland
     * exportan GDK_BACKEND=wayland y, al estar forzado a ese backend
     * inexistente, GDK no cae a X11 y falla con "Failed to open display".
     * Forzamos X11 para que la app use siempre el backend disponible.
     *
     * Nota: si algún día se reconstruye GTK con la feature "wayland",
     * esta línea debería eliminarse o hacerse condicional.
     */
    g_setenv ("GDK_BACKEND", "x11", TRUE);

    formats_init (); /* registro de formatos de salida */
    config_init ();  /* preferencias del usuario */

    /* Con argumentos de ruta: modo CLI headless (sin GUI). */
    if (argc > 1)
        return cli_process ((const char *const *) (argv + 1), argc - 1);

    /* Sin argumentos: GUI GTK. */
    controller_init ();
    GtkApplication *app =
        gtk_application_new ("org.anatrans.transcriptor", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect (app, "activate", G_CALLBACK (gui_activate), NULL);

    int status = g_application_run (G_APPLICATION (app), argc, argv);
    controller_shutdown ();
    g_object_unref (app);
    return status;
}
