#ifndef TRANSCRIBE_H
#define TRANSCRIBE_H

#include <glib.h>
#include "formats.h"

G_BEGIN_DECLS

/*
 * transcribe: transcripción con whisper (port vcpkg "whisper-cpp").
 *
 * El flujo es: ffmpeg convierte a WAV PCM16 mono 16 kHz (audio.c) y aquí
 * se transcribe ese WAV con el modelo elegido por el usuario. El resultado
 * se entrega como TranscriptDocument (segmentos con texto y tiempos) y lo
 * escribe el FormatWriter elegido (formats.h).
 */

/* Sesión de whisper: carga el modelo UNA vez y se reutiliza en todo el lote. */
typedef struct WhisperSession WhisperSession;

/*
 * Progreso interno de whisper durante la transcripción de un archivo:
 * `percent` va de 0 a 100. Se llama desde el hilo que invoca
 * transcribe_session_run (no toques widgets desde aquí).
 */
typedef void (*TranscribeProgressCb) (int percent, gpointer user_data);

/*
 * Carga el modelo de whisper. Devuelve NULL y llena `error` si falla.
 */
WhisperSession *transcribe_session_open (const char *model_path, GError **error);

/*
 * Transcribe `wav_path` (WAV PCM16, 16 kHz) y escribe la transcripción en
 * `out_path` con el escritor `writer`. `source_name` se usa para el título
 * del documento. `language`: "auto", NULL o código ISO 639-1.
 * `progress_cb` (opcional) recibe el progreso 0-100 de whisper.
 * Devuelve TRUE en éxito.
 */
gboolean transcribe_session_run (WhisperSession *session,
                                 const char *wav_path,
                                 const char *language,
                                 const FormatWriter *writer,
                                 const char *source_name,
                                 const char *out_path,
                                 TranscribeProgressCb progress_cb,
                                 gpointer progress_data,
                                 GError **error);

/* Libera el modelo y la sesión. */
void transcribe_session_close (WhisperSession *session);

G_END_DECLS

#endif /* TRANSCRIBE_H */
