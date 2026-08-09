#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>

G_BEGIN_DECLS

/*
 * config: preferencias del usuario y opciones por defecto.
 *
 * Fuentes de valor (en orden de prioridad):
 *   1) variable de entorno (override explícito);
 *   2) archivo de configuración del usuario (preferencias guardadas,
 *      $XDG_CONFIG_HOME/anatrans/config.ini);
 *   3) valor por defecto.
 *
 * Variables de entorno:
 *   WHISPER_MODEL      -> modelo .bin de whisper
 *   OUTPUT_DIR         -> carpeta de salida
 *   FFMPEG_BIN         -> ejecutable de ffmpeg
 *   TRANSCRIPT_LANG    -> idioma ("auto", "es", "en", ...)
 *   TRANSCRIPT_FORMAT  -> id del formato ("docx", "pdf", "markdown", ...)
 */

/* Carga la configuración guardada (idempotente; no falla si no existe). */
void config_init (void);

/* Ruta del archivo de configuración del usuario. */
const char *config_get_config_file (void);

/* Nombre de la carpeta de salida por defecto. */
const char *config_default_output_dir (void);

/* Carpeta de salida: env > config > "transcripciones". */
const char *config_get_output_dir     (void);

/* Modelo de whisper: env > config > "". */
const char *config_get_whisper_model  (void);

/* Ejecutable de ffmpeg: $FFMPEG_BIN, o el de vcpkg, o "ffmpeg". */
const char *config_get_ffmpeg_bin     (void);

/* Idioma de transcripción: env > config > "auto". */
const char *config_get_transcript_lang (void);

/* Formato de transcripción: env > config > "docx". */
const char *config_get_transcript_format (void);

/* Setters: actualizan la preferencia en memoria (el guardado a disco lo
 * orquesta la GUI con config_save, normalmente con un pequeño debounce). */
void config_set_whisper_model      (const char *value);
void config_set_output_dir         (const char *value);
void config_set_transcript_lang    (const char *value);
void config_set_transcript_format  (const char *value);

/* Escribe el archivo de configuración. Devuelve TRUE en éxito. */
gboolean config_save (void);

G_END_DECLS

#endif /* CONFIG_H */
