/* ============================================================================
 * tests/i18n_layout.c — verificación de que las traducciones no rompan la
 * interfaz: mide con Pango el ancho real de cada texto por idioma.
 *
 * [PROYECTO]    Ana-Trans: test de integración de i18n.
 * [DEPENDENCIA] GLib, Pango (vienen con GTK), i18n.c del proyecto.
 *
 * Para cada idioma del catálogo, activa el idioma y mide el ancho en píxeles
 * del msgstr comparado con el msgid (español, base). Un texto mucho más
 * ancho que el original (o más de 520 px) puede desbordar el layout: se
 * reporta como aviso o error. No necesita abrir una ventana (sin display).
 * ========================================================================== */

#include "i18n.h"

#include <pango/pangocairo.h>

#include <stdio.h>
#include <string.h>

#ifndef PO_DIR
#define PO_DIR "po"
#endif

static PangoContext *g_pango_ctx = NULL;

static void
measure_init (void)
{
    /* Sin display: el font map cairo por defecto basta para medir texto. */
    PangoFontMap *fm = pango_cairo_font_map_get_default ();
    g_pango_ctx = pango_font_map_create_context (fm);
}

static int
text_width_px (const char *text)
{
    PangoLayout *lay = pango_layout_new (g_pango_ctx);
    PangoFontDescription *fd = pango_font_description_from_string ("Sans 10");
    pango_layout_set_font_description (lay, fd);
    pango_font_description_free (fd);
    pango_layout_set_text (lay, text, -1);

    int w = 0, h = 0;
    pango_layout_get_pixel_size (lay, &w, &h);
    g_object_unref (lay);
    return w;
}

/* Extrae los msgid de un .po (líneas `msgid "..."`). */
static GPtrArray *
read_msgids (const char *po_path)
{
    GPtrArray *out = g_ptr_array_new_with_free_func (g_free);

    gchar *contents = NULL;
    if (!g_file_get_contents (po_path, &contents, NULL, NULL))
        return out;

    gchar **lines = g_strsplit (contents, "\n", -1);
    g_free (contents);

    for (guint i = 0; lines[i] != NULL; i++) {
        const char *p = strstr (lines[i], "msgid \"");
        if (p == NULL)
            continue;
        p += 7; /* len("msgid \"") */
        const char *end = strchr (p, '"');
        if (end == NULL || end == p) /* header (msgid "") */
            continue;
        g_ptr_array_add (out, g_strndup (p, (gsize) (end - p)));
    }
    g_strfreev (lines);
    return out;
}

static gboolean
list_has (GPtrArray *list, const char *s)
{
    for (guint i = 0; i < list->len; i++) {
        if (g_strcmp0 (g_ptr_array_index (list, i), s) == 0)
            return TRUE;
    }
    return FALSE;
}

int
main (void)
{
    i18n_init ();
    measure_init ();

    gchar *es_po = g_build_filename (PO_DIR, "es.po", NULL);
    GPtrArray *msgids = read_msgids (es_po);
    g_free (es_po);

    if (msgids->len == 0) {
        g_printerr ("i18n_layout: no se pudieron leer msgid de %s\n", PO_DIR "/es.po");
        return 2;
    }

    /* Idiomas a probar: es (base) + los instalados en el catálogo. */
    GPtrArray *langs = g_ptr_array_new_with_free_func (g_free);
    g_ptr_array_add (langs, g_strdup ("es"));
    GPtrArray *available = i18n_languages ();
    for (guint i = 0; i < available->len; i++) {
        const char *l = g_ptr_array_index (available, i);
        if (!list_has (langs, l))
            g_ptr_array_add (langs, g_strdup (l));
    }
    i18n_free_languages (available);

    int errors = 0, warnings = 0;

    for (guint li = 0; li < langs->len; li++) {
        const char *lang = g_ptr_array_index (langs, li);
        if (!i18n_set_language (lang)) {
            g_print ("ERROR(%s): no se pudo activar el catálogo\n", lang);
            errors++;
            continue;
        }

        for (guint mi = 0; mi < msgids->len; mi++) {
            const char *mid = g_ptr_array_index (msgids, mi);
            const char *tr  = i18n_t (mid);

            if (tr == NULL || *tr == '\0') {
                g_print ("ERROR(%s): traducción vacía para: %s\n", lang, mid);
                errors++;
                continue;
            }
            if (g_strcmp0 (lang, "es") != 0 && g_strcmp0 (tr, mid) == 0) {
                g_print ("AVISO(%s): sin traducir (msgid == msgstr): %s\n", lang, mid);
                warnings++;
            }

            int w_base = text_width_px (mid);
            int w_tr   = text_width_px (tr);

            if (w_tr > 520 || (w_tr > 350 && w_tr > w_base * 2)) {
                g_print ("ERROR(%s): texto muy ancho %dpx (base %dpx): %s\n",
                         lang, w_tr, w_base, tr);
                errors++;
            } else if (w_tr > 350 && w_tr > w_base * 16 / 10) {
                g_print ("AVISO(%s): texto ancho %dpx (base %dpx): %s\n",
                         lang, w_tr, w_base, tr);
                warnings++;
            }
        }
    }

    g_print ("i18n_layout: %u msgid probados en %u idioma(s) | avisos: %d | errores: %d\n",
             msgids->len, langs->len, warnings, errors);

    /* --- Verificación del cambio de idioma EN CALIENTE (i18n_set_language) ---
     * El mismo proceso debe poder alternar es -> en -> es y que gettext
     * devuelva la traducción correspondiente en cada momento. */
    if (i18n_set_language ("es") && g_strcmp0 (i18n_t ("Procesar"), "Procesar") != 0) {
        g_print ("ERROR: 'Procesar' no es 'Procesar' en español\n");
        errors++;
    }
    if (!i18n_set_language ("en") || g_strcmp0 (i18n_t ("Procesar"), "Process") != 0) {
        g_print ("ERROR: 'Procesar' no se traduce a 'Process' en inglés\n");
        errors++;
    }
    if (!i18n_set_language ("es") || g_strcmp0 (i18n_t ("Procesar"), "Procesar") != 0) {
        g_print ("ERROR: vuelta a español fallida (catálogo en caché)\n");
        errors++;
    }
    if (i18n_set_language ("xx-idioma-inexistente")) {
        g_print ("ERROR: se aceptó un idioma sin catálogo\n");
        errors++;
    }

    g_ptr_array_free (langs, TRUE);
    g_ptr_array_free (msgids, TRUE);
    return (errors > 0) ? 1 : 0;
}
