#include "formats.h"

/* Escritor Markdown: título + un párrafo por segmento. */

static gboolean
md_write (const TranscriptDocument *doc, const char *out_path, GError **error)
{
    GString *out = g_string_new (NULL);

    g_string_append (out, "# Transcripción");
    if (doc->source_name != NULL && *doc->source_name != '\0')
        g_string_append_printf (out, ": %s", doc->source_name);
    g_string_append (out, "\n\n");

    for (int i = 0; i < doc->n_segments; i++) {
        g_string_append (out, doc->segments[i].text);
        g_string_append (out, "\n\n");
    }

    gboolean ok = formats_write_text_file (out_path, out->str, error);
    g_string_free (out, TRUE);
    return ok;
}

static const FormatWriter md_writer = {
    .id        = "markdown",
    .label     = "Markdown (.md)",
    .extension = "md",
    .write     = md_write,
};

void
fmt_markdown_register (void)
{
    formats_register (&md_writer);
}
