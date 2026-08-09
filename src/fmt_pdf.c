#include "formats.h"

#include <cairo.h>
#include <cairo-pdf.h>
#include <pango/pangocairo.h>

/*
 * Escritor PDF usando cairo (surface PDF) + Pango (texto UTF-8, saltos
 * de línea y ajuste de palabra). Ambos ya llegan vía GTK, sin dependencias
 * nuevas. Página A4 vertical con título y un párrafo por segmento.
 */

#define PAGE_W 595.0   /* A4 portrait en puntos */
#define PAGE_H 842.0
#define MARGIN 56.0

static void
render_paragraph (cairo_t *cr, const char *text, const char *font, double *y)
{
    PangoLayout *layout = pango_cairo_create_layout (cr);

    PangoFontDescription *fd = pango_font_description_from_string (font);
    pango_layout_set_font_description (layout, fd);
    pango_font_description_free (fd);

    pango_layout_set_width (layout, pango_units_from_double (PAGE_W - 2 * MARGIN));
    pango_layout_set_wrap (layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text (layout, text, -1);

    PangoRectangle ink, logical;
    pango_layout_get_extents (layout, &ink, &logical);
    double line_h = pango_units_to_double (logical.height);

    if (*y + line_h > PAGE_H - MARGIN) {
        cairo_show_page (cr);
        *y = MARGIN;
    }

    cairo_move_to (cr, MARGIN, *y);
    pango_cairo_update_layout (cr, layout);
    pango_cairo_show_layout (cr, layout);
    *y += line_h + 5.0;

    g_object_unref (layout);
}

static gboolean
pdf_write (const TranscriptDocument *doc, const char *out_path, GError **error)
{
    cairo_surface_t *surface = cairo_pdf_surface_create (out_path, PAGE_W, PAGE_H);
    cairo_status_t status = cairo_surface_status (surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "No se pudo crear el PDF %s: %s", out_path,
                     cairo_status_to_string (status));
        cairo_surface_destroy (surface);
        return FALSE;
    }

    cairo_t *cr = cairo_create (surface);
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);

    double y = MARGIN;

    if (doc->source_name != NULL && *doc->source_name != '\0') {
        gchar *title = g_strdup_printf ("Transcripción: %s", doc->source_name);
        render_paragraph (cr, title, "Sans Bold 14", &y);
        g_free (title);
        y += 6.0;
    }

    for (int i = 0; i < doc->n_segments; i++) {
        const char *text = doc->segments[i].text;
        if (text == NULL || *text == '\0')
            continue;
        render_paragraph (cr, text, "Sans 10", &y);
    }

    cairo_show_page (cr);
    cairo_destroy (cr);
    cairo_surface_destroy (surface);
    return TRUE;
}

static const FormatWriter pdf_writer = {
    .id        = "pdf",
    .label     = "PDF (.pdf)",
    .extension = "pdf",
    .write     = pdf_write,
};

void
fmt_pdf_register (void)
{
    formats_register (&pdf_writer);
}
