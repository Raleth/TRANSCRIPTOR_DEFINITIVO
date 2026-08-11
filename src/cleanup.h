#ifndef CLEANUP_H
#define CLEANUP_H

#include <glib.h>

G_BEGIN_DECLS

/*
 * cleanup: carpeta temporal para los WAV intermedios de ffmpeg y limpieza
 * automática al terminar el lote o al cerrar el programa de forma abrupta
 * (SIGINT/SIGTERM/SIGHUP y atexit).
 */

/*
 * Crea el dir temporal único (en g_get_tmp_dir()) y registra atexit +
 * manejadores de señal. Idempotente. Devuelve FALSE si no se pudo crear
 * (el llamador puede usar la carpeta de salida como respaldo).
 */
gboolean cleanup_begin (void);

/* Dir temporal para los WAV (NULL si no se creó). */
const char *cleanup_get_wav_dir (void);

/* Rastrea el WAV en curso (para borrarlo ante una señal). */
void cleanup_set_current_wav (const char *path);
void cleanup_clear_current_wav (void);

/*
 * Borra el WAV actual y el dir temporal (recursivo), y restaura los
 * manejadores. Idempotente; seguro llamarlo varias veces.
 */
void cleanup_finish (void);

G_END_DECLS

#endif /* CLEANUP_H */
