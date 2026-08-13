#ifndef PROCESS_H
#define PROCESS_H

/* dependencias (GLib) */
#include <glib.h>

/* módulos del proyecto */
#include "formats.h"

G_BEGIN_DECLS

/*
 * process: procesamiento por lote de los archivos en cola.
 *
 * Por cada archivo:
 *   1) ffmpeg lo convierte a WAV PCM16 mono 16 kHz (audio.c);
 *   2) whisper transcribe el WAV y guarda la transcripción en
 *      <output_dir>/<nombre>_transcripcion.<ext> usando el FormatWriter
 *      indicado.
 */

/* Reporte de un archivo del lote (válido solo durante la llamada al callback). */
typedef struct {
    int      done;     /* índice 1-based dentro del lote */
    int      total;    /* tamaño del lote */
    char    *file;     /* ruta del archivo procesado */
    gboolean success;  /* TRUE si se transcribió correctamente */
    char    *message;  /* detalle: error, o ruta del archivo generado */
} ProcessReport;

/*
 * Callback de progreso del lote (se llama desde el hilo que procesa).
 *   files_done   -> archivos ya completados
 *   total_files  -> tamaño del lote
 *   file_percent -> 0-100 dentro del archivo actual
 *   current_file -> ruta del archivo actual (puede ser NULL)
 *   phase        -> "modelo", "convirtiendo", "transcribiendo" o "listo"
 */
typedef void (*ProcessProgressCb) (int files_done, int total_files, int file_percent,
                                   const char *current_file, const char *phase,
                                   gpointer user_data);

/*
 * Ejecuta el lote. `model_path` es la ruta al modelo .bin de whisper
 * (se carga una sola vez); `language` es "auto"/NULL o código ISO 639-1;
 * `writer` es el formato de salida elegido.
 * `progress` (opcional) recibe el avance; `report` se llama por cada archivo.
 * Devuelve el número de archivos procesados correctamente.
 */
int process_run_batch (const char *const *files, int n_files,
                       const char *output_dir,
                       const char *model_path,
                       const char *language,
                       const FormatWriter *writer,
                       ProcessProgressCb progress,
                       void (*report) (const ProcessReport *report,
                                       gpointer user_data),
                       gpointer user_data);

G_END_DECLS

#endif /* PROCESS_H */
