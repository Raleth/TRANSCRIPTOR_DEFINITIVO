#include "transcribe.h"

#include <whisper.h>

#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct WhisperSession {
    struct whisper_context *ctx;
};

/* Paquete para reenviar el progreso de whisper al callback del llamador. */
typedef struct {
    TranscribeProgressCb cb;
    gpointer             user_data;
} ProgressPack;

static void
whisper_progress_forward (struct whisper_context *ctx,
                          struct whisper_state  *state,
                          int                    progress,
                          void                  *user_data)
{
    ProgressPack *pack = user_data;
    if (pack->cb != NULL)
        pack->cb (progress, pack->user_data);
}

/* ---------------------------------------------------------------------------
 * Lector de WAV (PCM)
 * ------------------------------------------------------------------------- */

static guint32
read_le32 (const guint8 *p)
{
    return (guint32) p[0] | ((guint32) p[1] << 8) |
           ((guint32) p[2] << 16) | ((guint32) p[3] << 24);
}

static guint16
read_le16 (const guint8 *p)
{
    return (guint16) p[0] | ((guint16) p[1] << 8);
}

/*
 * Lee un WAV PCM16 y devuelve muestras float mono (promedia canales),
 * normalizadas a [-1, 1] como espera whisper. El pipeline de ffmpeg
 * garantiza PCM16 mono 16 kHz.
 */
static gboolean
read_wav_mono (const char *path, float **samples_out, int *n_out, GError **error)
{
    FILE *f = fopen (path, "rb");
    if (f == NULL) {
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                     "No se pudo abrir el WAV: %s", path);
        return FALSE;
    }

    guint8 hdr[12];
    if (fread (hdr, 1, 12, f) != 12 || memcmp (hdr, "RIFF", 4) != 0 ||
        memcmp (hdr + 8, "WAVE", 4) != 0) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                     "No es un WAV RIFF válido: %s", path);
        fclose (f);
        return FALSE;
    }

    guint16 audio_format = 0, num_channels = 0, bits = 0;
    guint32 sample_rate = 0;
    gboolean got_fmt = FALSE;
    gboolean found_data = FALSE;
    float *samples = NULL;
    int n_frames = 0;

    guint8 chunk[8];
    while (fread (chunk, 1, 8, f) == 8) {
        guint32 size = read_le32 (chunk + 4);

        if (memcmp (chunk, "fmt ", 4) == 0) {
            guint8 fmt[16];
            if (fread (fmt, 1, 16, f) != 16)
                break;
            audio_format = read_le16 (fmt + 0);
            num_channels = read_le16 (fmt + 2);
            sample_rate  = read_le32 (fmt + 4);
            bits         = read_le16 (fmt + 14);
            got_fmt = TRUE;
            if (size > 16)
                fseek (f, size - 16, SEEK_CUR);
        }
        else if (memcmp (chunk, "data", 4) == 0) {
            if (!got_fmt || audio_format != 1 || bits != 16 ||
                num_channels == 0 || sample_rate != 16000) {
                g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                             "El WAV debe ser PCM 16 bits a 16 kHz (mono): %s", path);
                fclose (f);
                return FALSE;
            }

            int bytes_per_sample = bits / 8;
            n_frames = (int) (size / (num_channels * bytes_per_sample));
            if (n_frames <= 0) {
                g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                             "WAV sin datos de audio: %s", path);
                fclose (f);
                return FALSE;
            }

            samples = g_new (float, (gsize) n_frames);
            for (int i = 0; i < n_frames; i++) {
                long acc = 0;
                for (int ch = 0; ch < num_channels; ch++) {
                    guint8 b[2];
                    if (fread (b, 1, 2, f) != 2) {
                        g_free (samples);
                        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                     "WAV truncado: %s", path);
                        fclose (f);
                        return FALSE;
                    }
                    acc += (short) read_le16 (b);
                }
                samples[i] = (float) acc / (float) num_channels / 32768.0f;
            }

            found_data = TRUE;
            break;
        }
        else {
            if (fseek (f, size + (size & 1), SEEK_CUR) != 0)
                break;
        }
    }

    fclose (f);

    if (!found_data) {
        g_free (samples);
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                     "No se encontró la sección 'data' en el WAV: %s", path);
        return FALSE;
    }

    *samples_out = samples;
    *n_out = n_frames;
    return TRUE;
}

/* ---------------------------------------------------------------------------
 * Sesión de whisper
 * ------------------------------------------------------------------------- */

WhisperSession *
transcribe_session_open (const char *model_path, GError **error)
{
    if (model_path == NULL || *model_path == '\0') {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                     "No se indicó el modelo de whisper (WHISPER_MODEL o selector de modelo).");
        return NULL;
    }

    struct whisper_context_params cparams = whisper_context_default_params ();
    struct whisper_context *ctx =
        whisper_init_from_file_with_params (model_path, cparams);

    if (ctx == NULL) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "No se pudo cargar el modelo de whisper: %s", model_path);
        return NULL;
    }

    WhisperSession *s = g_new (WhisperSession, 1);
    s->ctx = ctx;
    return s;
}

gboolean
transcribe_session_run (WhisperSession *session,
                        const char *wav_path,
                        const char *language,
                        const FormatWriter *writer,
                        const char *source_name,
                        const char *out_path,
                        TranscribeProgressCb progress_cb,
                        gpointer progress_data,
                        GError **error)
{
    g_return_val_if_fail (session != NULL, FALSE);
    g_return_val_if_fail (wav_path != NULL, FALSE);
    g_return_val_if_fail (writer != NULL, FALSE);
    g_return_val_if_fail (out_path != NULL, FALSE);

    float *samples = NULL;
    int n_samples = 0;
    if (!read_wav_mono (wav_path, &samples, &n_samples, error))
        return FALSE;

    struct whisper_full_params params = whisper_full_default_params (WHISPER_SAMPLING_GREEDY);
    params.n_threads = MIN (g_get_num_processors (), 8);
    params.language = (language != NULL && *language != '\0' &&
                       g_ascii_strcasecmp (language, "auto") != 0) ? language : "auto";  /* "auto" -> detecta Y transcribe */
    params.detect_language = FALSE;  /* no fijar a TRUE: whisper_full solo detectaría el idioma y devolvería sin transcribir */
    params.no_context    = TRUE;  /* cada archivo del lote es independiente */
    params.n_max_text_ctx = 0;    /* -mc 0: sin contexto del texto previo (evita bucles de repetición en audios largos) */
    params.no_timestamps = TRUE;  /* los tiempos se toman de los segmentos */
    params.print_progress   = FALSE;
    params.print_realtime   = FALSE;
    params.print_timestamps = FALSE;
    params.print_special    = FALSE;

    ProgressPack pack = { progress_cb, progress_data };
    params.progress_callback          = whisper_progress_forward;
    params.progress_callback_user_data = &pack;

    int ret = whisper_full (session->ctx, params, samples, n_samples);
    g_free (samples);

    if (ret != 0) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "whisper falló al transcribir %s (código %d)", wav_path, ret);
        return FALSE;
    }

    /* Construir el documento con los segmentos. */
    int n_segments = whisper_full_n_segments (session->ctx);

    TranscriptDocument doc = {
        .segments    = g_new0 (TranscriptSegment, (gsize) MAX (n_segments, 0)),
        .n_segments  = n_segments,
        .source_name = g_strdup ((source_name != NULL) ? source_name : ""),
        .model_name  = NULL,
    };

    for (int i = 0; i < n_segments; i++) {
        const char *text = whisper_full_get_segment_text (session->ctx, i);
        doc.segments[i].text = g_strdup ((text != NULL) ? text : "");
        doc.segments[i].start_ms = (int64_t) whisper_full_get_segment_t0 (session->ctx, i);
        doc.segments[i].end_ms   = (int64_t) whisper_full_get_segment_t1 (session->ctx, i);
    }

    gboolean ok = writer->write (&doc, out_path, error);

    for (int i = 0; i < n_segments; i++)
        g_free (doc.segments[i].text);
    g_free (doc.segments);
    g_free (doc.source_name);

    return ok;
}

void
transcribe_session_close (WhisperSession *session)
{
    if (session == NULL)
        return;
    whisper_free (session->ctx);
    g_free (session);
}
