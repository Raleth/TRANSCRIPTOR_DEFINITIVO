#ifndef FORMATS_H
#define FORMATS_H

#include <glib.h>

G_BEGIN_DECLS

/*
 * formats: sistema extensible de formatos de salida para las transcripciones.
 *
 * Para añadir un formato nuevo (markdown, latex, odt, html, ...):
 *   1. crea src/fmt_<nombre>.c con un FormatWriter y una función
 *      fmt_<nombre>_register();
 *   2. añade la llamada a fmt_<nombre>_register() en formats_init()
 *      (formats.c);
 *   3. añade el archivo a CMakeLists.txt.
 * El resto (configuración, CLI y desplegable de la GUI) funciona solo.
 */

/* Un segmento de la transcripción: texto + marcas de tiempo en ms. */
typedef struct {
    int64_t start_ms;
    int64_t end_ms;
    char   *text;
} TranscriptSegment;

/* Documento de transcripción completo (entrada de los escritores). */
typedef struct {
    TranscriptSegment *segments;
    int                n_segments;
    char              *source_name;  /* archivo de origen (para títulos) */
    char              *model_name;   /* modelo usado (informativo) */
} TranscriptDocument;

/*
 * Escritor de un formato. `write` recibe el documento y la ruta de destino
 * y devuelve TRUE en éxito (en error, llena `error`).
 */
typedef struct {
    const char *id;           /* "docx", "pdf", "markdown", ... */
    const char *label;        /* etiqueta legible para la GUI */
    const char *extension;    /* "docx", "pdf", "md", ... (sin punto) */
    gboolean (*write) (const TranscriptDocument *doc,
                       const char *out_path,
                       GError **error);
} FormatWriter;

/* Inicializa el registro (se llama una vez, desde main). */
void formats_init (void);

/* Registra un escritor (usado por las funciones fmt_<nombre>_register). */
void formats_register (const FormatWriter *writer);

/* Busca por id (insensible a mayúsculas). NULL si no existe. */
const FormatWriter *formats_get (const char *id);

/* Formato por defecto ("docx" si está registrado). */
const FormatWriter *formats_get_default (void);

/* Lista de todos los escritores registrados (GPtrArray de FormatWriter*). */
const GPtrArray *formats_list (void);

/* Interpreta una cadena de configuración; devuelve el default si es inválida. */
const FormatWriter *formats_from_string (const char *id);

/* Utilidad compartida por los escritores de texto plano. */
gboolean formats_write_text_file (const char *path, const char *contents, GError **error);

G_END_DECLS

#endif /* FORMATS_H */
