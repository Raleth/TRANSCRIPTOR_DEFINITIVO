#ifndef CONFIG_H
#define CONFIG_H

/* dependencias (GLib) */
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
 *   TRANSCRIPT_FORMAT  -> id del formato
 *
 * Modo de modelo (preferencia):
 *   model-mode  = "custom" (el usuario elige su .bin) | "easy" (auto-descarga)
 *   easy-model  = "large-v3-turbo" | "medium" | "small"
 */

/* Carga la configuración guardada (idempotente; no falla si no existe). */
void config_init (void);

/* Ruta del archivo de configuración del usuario. */
const char *config_get_config_file (void);

/* Nombre de la carpeta de salida por defecto. */
const char *config_default_output_dir (void);

/* Carpeta de salida: env > config > "transcripciones". */
const char *config_get_output_dir     (void);

/*
 * Modelo de whisper en uso: env > (según model-mode) ruta del modo sencillo
 * o el modelo personalizado de config > "".
 */
const char *config_get_whisper_model  (void);

/* Ejecutable de ffmpeg: $FFMPEG_BIN, o el de vcpkg, o "ffmpeg". */
const char *config_get_ffmpeg_bin     (void);

/* Idioma de transcripción: env > config > "auto". */
const char *config_get_transcript_lang (void);

/* Formato de transcripción: env > config > "docx". */
const char *config_get_transcript_format (void);

/* Modo de modelo: "custom" o "easy" (config > "custom"). */
const char *config_get_model_mode (void);
void config_set_model_mode (const char *value);

/* Modelo del modo sencillo (config > "large-v3-turbo"). */
const char *config_get_easy_model (void);
void config_set_easy_model (const char *value);

/* Setters de las preferencias existentes. */
void config_set_whisper_model      (const char *value);
void config_set_output_dir         (const char *value);
void config_set_transcript_lang    (const char *value);
void config_set_transcript_format  (const char *value);

/* Escribe el archivo de configuración. Devuelve TRUE en éxito. */
gboolean config_save (void);

G_END_DECLS

#endif /* CONFIG_H */
