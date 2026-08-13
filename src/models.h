#ifndef MODELS_H
#define MODELS_H

/* dependencias (GLib) */
#include <glib.h>

G_BEGIN_DECLS

/*
 * models: descarga y localización de modelos de whisper en modo sencillo.
 *
 * Los modelos se guardan en g_get_user_data_dir()/anatrans/models
 * (~/.local/share/anatrans/models en Linux), fuera del alcance del usuario
 * para que no se borren accidentalmente. Se descargan con el script
 * scripts/download-ggml-model.sh incluido en el proyecto.
 */

/* Modelo conocido por el modo sencillo. */
typedef struct {
    const char *id;       /* "large-v3-turbo", "medium", "small" */
    const char *filename; /* archivo esperado (ggml-<id>.bin) */
    const char *label;    /* etiqueta legible (Preciso / Equilibrado / Rápido) */
    const char *desc;     /* subtexto que describe el modelo */
} KnownModel;

/* Lista de modelos del modo sencillo (terminada en un elemento con id NULL). */
const KnownModel *models_known (void);

/* Busca un modelo por id (insensible). NULL si no existe. */
const KnownModel *models_find (const char *id);

/* Carpeta donde viven los modelos (la crea si hace falta). */
const char *models_get_dir (void);

/* Ruta esperada del archivo del modelo (nueva cadena). */
gchar *models_path (const char *id);

/* TRUE si el modelo ya está descargado. */
gboolean models_is_downloaded (const char *id);

/* Primer modelo no descargado (NULL si están todos). */
const KnownModel *models_first_missing (void);

/*
 * Descarga el modelo en la carpeta de modelos ejecutando el script
 * download-ggml-model.sh. Devuelve TRUE en éxito.
 */
gboolean models_download (const char *id, GError **error);

G_END_DECLS

#endif /* MODELS_H */
