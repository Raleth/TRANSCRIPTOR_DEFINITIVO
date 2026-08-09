#include "process.h"

#include "audio.h"
#include "transcribe.h"

#include <glib/gstdio.h>
#include <string.h>

static gchar *
build_transcript_path (const char *output_dir, const char *input, const char *ext)
{
    gchar *base = g_path_get_basename (input);

    char *dot = strrchr (base, '.');
    if (dot != NULL)
        *dot = '\0';

    gchar *name = g_strdup_printf ("%s_transcripcion.%s", base, ext);
    gchar *out  = g_build_filename (output_dir, name, NULL);

    g_free (name);
    g_free (base);
    return out;
}

/* Puente entre el progreso de whisper y el callback del lote. */
typedef struct {
    ProcessProgressCb cb;
    gpointer          user_data;
    int               files_done;
    int               total_files;
    const char       *file;
} WhisperProgress;

static void
on_whisper_progress (int percent, gpointer user_data)
{
    WhisperProgress *w = user_data;
    if (w->cb != NULL)
        w->cb (w->files_done, w->total_files, percent, w->file,
               "transcribiendo", w->user_data);
}

int
process_run_batch (const char *const *files, int n_files,
                   const char *output_dir,
                   const char *model_path,
                   const char *language,
                   const FormatWriter *writer,
                   ProcessProgressCb progress,
                   void (*report) (const ProcessReport *report,
                                   gpointer user_data),
                   gpointer user_data)
{
    g_return_val_if_fail (files != NULL, 0);
    g_return_val_if_fail (output_dir != NULL && *output_dir != '\0', 0);
    g_return_val_if_fail (writer != NULL, 0);

    if (progress != NULL)
        progress (0, n_files, 0, (n_files > 0) ? files[0] : NULL, "modelo", user_data);

    /* Cargar el modelo de whisper una sola vez para todo el lote. */
    GError *err = NULL;
    WhisperSession *session = transcribe_session_open (model_path, &err);

    if (session == NULL) {
        if (report != NULL && n_files > 0) {
            ProcessReport rep = {
                .done    = 1,
                .total   = n_files,
                .file    = (char *) files[0],
                .success = FALSE,
                .message = g_strdup ((err != NULL) ? err->message
                                                   : "no se pudo cargar el modelo"),
            };
            report (&rep, user_data);
            g_free (rep.message);
        }
        if (err != NULL)
            g_error_free (err);
        return 0;
    }
    if (err != NULL)
        g_error_free (err);

    int ok_count = 0;

    for (int i = 0; i < n_files; i++) {
        const char *input = files[i];
        if (input == NULL || *input == '\0')
            continue;

        ProcessReport rep = {
            .done    = i + 1,
            .total   = n_files,
            .file    = (char *) input,
            .success = FALSE,
            .message = NULL,
        };

        if (progress != NULL)
            progress (i, n_files, 0, input, "convirtiendo", user_data);

        /* 1) ffmpeg -> WAV PCM16 mono 16 kHz */
        gchar *wav = audio_build_wav_path (output_dir, input);
        GError *cerr = NULL;
        if (!audio_convert_to_wav (input, wav, &cerr)) {
            rep.message = g_strdup ((cerr != NULL) ? cerr->message
                                                   : "error al convertir a WAV");
            if (cerr != NULL)
                g_error_free (cerr);
            if (report != NULL)
                report (&rep, user_data);
            if (progress != NULL)
                progress (i + 1, n_files, 100, input, "listo", user_data);
            g_free (rep.message);
            g_free (wav);
            continue;
        }

        /* 2) whisper -> transcripción en el formato elegido */
        gchar *out_path = build_transcript_path (output_dir, input, writer->extension);
        gchar *source_name = g_path_get_basename (input);

        WhisperProgress wp = { progress, user_data, i, n_files, input };
        GError *terr = NULL;
        if (transcribe_session_run (session, wav, language, writer,
                                    source_name, out_path,
                                    (progress != NULL) ? on_whisper_progress : NULL, &wp,
                                    &terr)) {
            rep.success = TRUE;
            rep.message = g_strdup_printf ("transcrito: %s", out_path);
            ok_count++;
        } else {
            rep.message = g_strdup ((terr != NULL) ? terr->message
                                                   : "error al transcribir");
            if (terr != NULL)
                g_error_free (terr);
        }

        if (report != NULL)
            report (&rep, user_data);
        if (progress != NULL)
            progress (i + 1, n_files, 100, input, "listo", user_data);

        g_free (rep.message);
        g_free (out_path);
        g_free (source_name);
        g_free (wav);
    }

    transcribe_session_close (session);
    return ok_count;
}
