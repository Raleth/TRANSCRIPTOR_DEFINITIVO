#ifndef CONTROLLER_H
#define CONTROLLER_H

/* dependencias (GLib) */
#include <glib.h>

G_BEGIN_DECLS

/*
 * controller: motor asíncrono del lote de transcripción.
 *
 * Separa el trabajo pesado (ffmpeg, whisper, escritura y borrado de
 * archivos) de la interfaz: la GUI construye un ControllerJob, lo entrega
 * con controller_start() y recibe eventos (progreso, reporte por archivo,
 * finalización) en el hilo del main loop, únicamente para pintarlos.
 * Este módulo NO depende de GTK.
 */

/* Trabajo a procesar (las cadenas se copian internamente). */
typedef struct {
    char **files;      /* rutas de los archivos a procesar */
    int    n_files;
    char  *output_dir; /* carpeta de salida */
    char  *model_path; /* modelo de whisper */
    char  *language;   /* "auto", "es", "en", ... */
    char  *format_id;  /* id de formato (formats.h) */
} ControllerJob;

typedef enum {
    CONTROLLER_EVENT_PROGRESS,  /* avance dentro del lote */
    CONTROLLER_EVENT_REPORT,    /* resultado de un archivo */
    CONTROLLER_EVENT_FINISHED,           /* el lote de transcripción terminó */
    CONTROLLER_EVENT_DOWNLOAD_FINISHED,  /* la descarga de un modelo terminó */
    CONTROLLER_EVENT_MODELS_SYNCED,      /* terminó la auto-descarga del modo sencillo */
} ControllerEventType;

/*
 * Evento entregado en el hilo del main loop. Los punteros a cadena son
 * válidos solo durante la llamada al callback.
 */
typedef struct {
    ControllerEventType type;
    int         done;          /* PROGRESS/REPORT: índice 1-based */
    int         total;         /* tamaño del lote */
    int         file_percent;  /* PROGRESS: 0-100 del archivo actual */
    int         ok_count;      /* FINISHED */
    const char *file;          /* PROGRESS/REPORT: archivo actual */
    const char *phase;         /* PROGRESS: modelo/convirtiendo/transcribiendo/listo */
    const char *message;       /* REPORT: detalle o error */
    gboolean    success;       /* REPORT */
} ControllerEvent;

typedef void (*ControllerEventCb) (const ControllerEvent *event, gpointer user_data);

/* Inicializa el controlador (idempotente). */
void controller_init (void);

/*
 * Lanza el lote en segundo plano (un solo lote a la vez). Los eventos se
 * entregan en el hilo del main loop vía g_idle_add. Devuelve FALSE si ya
 * hay un lote en curso o el trabajo no es válido.
 */
gboolean controller_start (const ControllerJob *job,
                           ControllerEventCb event_cb,
                           gpointer user_data);

/*
 * Lanza la descarga de un modelo del modo sencillo en segundo plano.
 * Los eventos (PROGRESS y DOWNLOAD_FINISHED) llegan por la misma vía.
 */
gboolean controller_download_model (const char *model_id,
                                    ControllerEventCb event_cb,
                                    gpointer user_data);

/*
 * Lanza en segundo plano la descarga automática de los modelos del modo
 * sencillo que falten (en secuencia). Emite PROGRESS por modelo y, al
 * terminar todo, DOWNLOAD_FINISHED y MODELS_SYNCED. Devuelve FALSE si ya
 * hay una tarea en curso o no falta ningún modelo.
 */
gboolean controller_download_missing_models (ControllerEventCb event_cb,
                                             gpointer user_data);

/* TRUE si hay un lote en curso. */
gboolean controller_is_running (void);

/* Espera a que el lote termine y libera recursos (para la salida). */
void controller_shutdown (void);

G_END_DECLS

#endif /* CONTROLLER_H */
