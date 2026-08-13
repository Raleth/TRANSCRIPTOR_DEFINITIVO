/* ============================================================================
 * fmt_latex.c — escritor de formato LaTeX.
 *
 * [PROYECTO]    Ana-Trans: documento article con un párrafo por segmento.
 * [DEPENDENCIA] GLib (funciones de cadena; formats.h trae glib.h).
 * ========================================================================== */

#include "formats.h"

static gchar *
latex_escape (const char *text)
{
    GString *s = g_string_new (NULL);
    for (const unsigned char *p = (const unsigned char *) text; *p; p++) {
        switch (*p) {
            case '\\': g_string_append (s, "\\textbackslash{}"); break;
            case '&':  g_string_append (s, "\\&"); break;
            case '%':  g_string_append (s, "\\%"); break;
            case '$':  g_string_append (s, "\\$"); break;
            case '#':  g_string_append (s, "\\#"); break;
            case '_':  g_string_append (s, "\\_"); break;
            case '{':  g_string_append (s, "\\{"); break;
            case '}':  g_string_append (s, "\\}"); break;
            case '~':  g_string_append (s, "\\textasciitilde{}"); break;
            case '^':  g_string_append (s, "\\textasciicircum{}"); break;
            default:   g_string_append_c (s, (char) *p); break;
        }
    }
    return g_string_free (s, FALSE);
}

static gboolean
tex_write (const TranscriptDocument *doc, const char *out_path, GError **error)
{
    GString *out = g_string_new (NULL);

    g_string_append (out,
        "\\documentclass[11pt]{article}\n"
        "\\usepackage[utf8]{inputenc}\n"
        "\\usepackage[T1]{fontenc}\n"
        "\\begin{document}\n\n");

    if (doc->source_name != NULL && *doc->source_name != '\0') {
        gchar *esc = latex_escape (doc->source_name);
        g_string_append_printf (out, "\\section*{Transcripción: %s}\n\n", esc);
        g_free (esc);
    }

    for (int i = 0; i < doc->n_segments; i++) {
        gchar *esc = latex_escape (doc->segments[i].text);
        g_string_append (out, esc);
        g_string_append (out, "\n\n");
        g_free (esc);
    }

    g_string_append (out, "\\end{document}\n");

    gboolean ok = formats_write_text_file (out_path, out->str, error);
    g_string_free (out, TRUE);
    return ok;
}

static const FormatWriter tex_writer = {
    .id        = "latex",
    .label     = "LaTeX (.tex)",
    .extension = "tex",
    .write     = tex_write,
};

void
fmt_latex_register (void)
{
    formats_register (&tex_writer);
}
