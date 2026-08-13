#ifndef AUDIO_H
#define AUDIO_H

/* dependencias (GLib) */
#include <glib.h>

G_BEGIN_DECLS

/*
 * audio: conversión de audio/video a WAV PCM16 mono 16 kHz, el formato
 * que espera whisper. Se usa el binario ffmpeg (config_get_ffmpeg_bin,
 * que resuelve el binario compilado por vcpkg o $FFMPEG_BIN).
 *
 * Equivalente a:
 *   ffmpeg -y -loglevel error -i <entrada> -ar 16000 -ac 1 -c:a pcm_s16le <salida>
 */

/*
 * Convierte `input` a WAV PCM16 mono 16 kHz en `output`.
 * Sobrescribe el destino si existe y crea las carpetas necesarias.
 * Devuelve TRUE en éxito; en error llena `error` con el detalle de ffmpeg.
 */
gboolean audio_convert_to_wav (const char *input, const char *output, GError **error);

/*
 * Construye la ruta del WAV de salida dentro de `output_dir`:
 *   <output_dir>/<nombre_sin_ext>_audio.wav
 */
gchar *audio_build_wav_path (const char *output_dir, const char *input);

G_END_DECLS

#endif /* AUDIO_H */
