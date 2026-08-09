#include "formats.h"

/* Escritor de texto plano: un párrafo por segmento. */

static gboolean
txt_write (const TranscriptDocument *doc, const char *out_path, GError **error)
{
    GString *out = g_string_new (NULL);

    for (int i = 0; i < doc->n_segments; i++) {
        g_string_append (out, doc->segments[i].text);
        g_string_append_c (out, '\n');
    }

    gboolean ok = formats_write_text_file (out_path, out->str, error);
    g_string_free (out, TRUE);
    return ok;
}

static const FormatWriter txt_writer = {
    .id        = "txt",
    .label     = "Texto plano (.txt)",
    .extension = "txt",
    .write     = txt_write,
};

void
fmt_txt_register (void)
{
    formats_register (&txt_writer);
}
