#include "formats.h"

#include <zip.h>

#include <glib/gstdio.h>
#include <string.h>

/*
 * Escritor DOCX (Office Open XML) usando libzip.
 * Genera el paquete ZIP mínimo: [Content_Types].xml, _rels/.rels y
 * word/document.xml con un párrafo por segmento.
 */

static gchar *
xml_escape (const char *text)
{
    GString *s = g_string_new (NULL);
    for (const unsigned char *p = (const unsigned char *) text; *p; p++) {
        switch (*p) {
            case '&':  g_string_append (s, "&amp;");  break;
            case '<':  g_string_append (s, "&lt;");   break;
            case '>':  g_string_append (s, "&gt;");   break;
            case '\"': g_string_append (s, "&quot;"); break;
            case '\'': g_string_append (s, "&apos;"); break;
            default:   g_string_append_c (s, (char) *p); break;
        }
    }
    return g_string_free (s, FALSE);
}

static gboolean
docx_write (const TranscriptDocument *doc, const char *out_path, GError **error)
{
    /* word/document.xml */
    GString *docxml = g_string_new (
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
        "  <w:body>\n");

    for (int i = 0; i < doc->n_segments; i++) {
        gchar *esc = xml_escape (doc->segments[i].text);
        g_string_append_printf (docxml,
            "    <w:p><w:r><w:t xml:space=\"preserve\">%s</w:t></w:r></w:p>\n", esc);
        g_free (esc);
    }

    g_string_append (docxml, "  </w:body>\n</w:document>\n");

    GString *content_types = g_string_new (
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        "  <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        "  <Default Extension=\"xml\" ContentType=\"application/xml\"/>\n"
        "  <Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>\n"
        "</Types>\n");

    GString *rels = g_string_new (
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "  <Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>\n"
        "</Relationships>\n");

    int zerr = 0;
    zip_t *z = zip_open (out_path, ZIP_CREATE | ZIP_TRUNCATE, &zerr);
    if (z == NULL) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "No se pudo crear el DOCX %s (error de zip %d)", out_path, zerr);
        g_string_free (docxml, TRUE);
        g_string_free (content_types, TRUE);
        g_string_free (rels, TRUE);
        return FALSE;
    }

    struct { const char *name; GString *data; } entries[] = {
        { "[Content_Types].xml", content_types },
        { "_rels/.rels",         rels },
        { "word/document.xml",   docxml },
    };

    gboolean ok = TRUE;
    for (guint i = 0; i < G_N_ELEMENTS (entries) && ok; i++) {
        zip_source_t *src = zip_source_buffer (z, entries[i].data->str,
                                               (zip_uint64_t) entries[i].data->len, 0);
        if (src == NULL ||
            zip_file_add (z, entries[i].name, src, ZIP_FL_OVERWRITE) < 0) {
            if (src != NULL)
                zip_source_free (src);
            g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                         "No se pudo añadir '%s' al DOCX: %s",
                         entries[i].name, zip_strerror (z));
            ok = FALSE;
        }
    }

    if (ok) {
        gchar *close_err = g_strdup (zip_strerror (z));
        if (zip_close (z) < 0) {
            g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                         "No se pudo cerrar el DOCX %s: %s", out_path, close_err);
            ok = FALSE;
        }
        g_free (close_err);
    } else {
        zip_discard (z);
    }

    g_string_free (docxml, TRUE);
    g_string_free (content_types, TRUE);
    g_string_free (rels, TRUE);
    return ok;
}

static const FormatWriter docx_writer = {
    .id        = "docx",
    .label     = "Word (.docx)",
    .extension = "docx",
    .write     = docx_write,
};

void
fmt_docx_register (void)
{
    formats_register (&docx_writer);
}
