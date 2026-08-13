/* ============================================================================
 * formats.c — registro de formatos de salida y utilidades compartidas.
 *
 * [PROYECTO]    Ana-Trans: lista de escritores registrados (fmt_*.c).
 * [DEPENDENCIA] GLib (GPtrArray, GError).
 * ========================================================================== */

#include "formats.h"

/* dependencias (GLib y libc) */
#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Funciones de registro de cada formato (definidas en cada fmt_*.c). */
void fmt_txt_register      (void);
void fmt_markdown_register (void);
void fmt_latex_register    (void);
void fmt_docx_register     (void);
void fmt_pdf_register      (void);

static GPtrArray *writers;  /* elementos: FormatWriter* */

static void
ensure_writers (void)
{
    if (writers == NULL)
        writers = g_ptr_array_new ();
}

void
formats_init (void)
{
    static gboolean done = FALSE;
    if (done)
        return;
    done = TRUE;

    /* Para añadir un formato: crea fmt_<x>.c, añade aquí su registro
     * y agrégalo a CMakeLists.txt. */
    fmt_txt_register ();
    fmt_markdown_register ();
    fmt_latex_register ();
    fmt_docx_register ();
    fmt_pdf_register ();
}

void
formats_register (const FormatWriter *writer)
{
    if (writer == NULL || writer->id == NULL || writer->write == NULL)
        return;
    ensure_writers ();
    g_ptr_array_add (writers, (gpointer) writer);
}

const FormatWriter *
formats_get (const char *id)
{
    formats_init ();
    if (id == NULL)
        return NULL;
    ensure_writers ();
    for (guint i = 0; i < writers->len; i++) {
        const FormatWriter *w = g_ptr_array_index (writers, i);
        if (g_ascii_strcasecmp (w->id, id) == 0)
            return w;
    }
    return NULL;
}

const FormatWriter *
formats_get_default (void)
{
    const FormatWriter *d = formats_get ("docx");
    if (d != NULL)
        return d;
    ensure_writers ();
    if (writers->len > 0)
        return g_ptr_array_index (writers, 0);
    return NULL;
}

const GPtrArray *
formats_list (void)
{
    formats_init ();
    ensure_writers ();
    return writers;
}

const FormatWriter *
formats_from_string (const char *id)
{
    const FormatWriter *w = formats_get (id);
    return (w != NULL) ? w : formats_get_default ();
}

/* Utilidad compartida por los escritores de texto plano. */
gboolean
formats_write_text_file (const char *path, const char *contents, GError **error)
{
    FILE *f = fopen (path, "w");
    if (f == NULL) {
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                     "No se pudo escribir %s", path);
        return FALSE;
    }
    if (contents != NULL)
        fputs (contents, f);
    fclose (f);
    return TRUE;
}
