/* ============================================================================
 * i18n.c — internacionalización con gettext y cambio de idioma en caliente.
 *
 * [PROYECTO]    Ana-Trans: idioma activo, idiomas disponibles, callbacks de
 *               re-render y detección RTL.
 * [DEPENDENCIA] gettext (libintl), GLib (rutas, g_setenv, GDir).
 * ========================================================================== */

#include "i18n.h"

/* dependencias (libintl / libc, GLib) */
#include <libintl.h>
#include <locale.h>
#include <string.h>

#include <glib.h>

#ifndef I18N_LOCALEDIR
#define I18N_LOCALEDIR "locale"
#endif

#define I18N_PACKAGE "transcriptor"

static gboolean g_init = FALSE;
static gchar *g_lang = NULL; /* "sistema" o código activo */

typedef struct {
    I18nChangedCb cb;
    gpointer      user_data;
    guint         id;
} I18nCbEntry;

static GPtrArray *g_cbs = NULL;
static guint g_next_cb_id = 1;

/* Idiomas que se escriben de derecha a izquierda (ISO 639-1). */
static const char *const k_rtl_codes[] = {
    "ar", "fa", "he", "ur", "ku", "ps", "dv", "ug",
};

static gint
compare_str (gconstpointer a, gconstpointer b)
{
    return g_strcmp0 (*(const char *const *) a, *(const char *const *) b);
}

static gboolean
lang_is_rtl_code (const char *code)
{
    if (code == NULL)
        return FALSE;
    for (guint i = 0; i < G_N_ELEMENTS (k_rtl_codes); i++) {
        if (g_ascii_strcasecmp (code, k_rtl_codes[i]) == 0)
            return TRUE;
    }
    return FALSE;
}

static gchar *
catalog_path (const char *lang)
{
    return g_build_filename (I18N_LOCALEDIR, lang, "LC_MESSAGES",
                             I18N_PACKAGE ".mo", NULL);
}

static gboolean
has_catalog (const char *lang)
{
    if (lang == NULL || *lang == '\0')
        return FALSE;
    gchar *path = catalog_path (lang);
    gboolean ok = g_file_test (path, G_FILE_TEST_IS_REGULAR);
    g_free (path);
    return ok;
}

void
i18n_init (void)
{
    if (g_init)
        return;
    g_init = TRUE;

    setlocale (LC_ALL, "");
    bindtextdomain (I18N_PACKAGE, I18N_LOCALEDIR);
    bind_textdomain_codeset (I18N_PACKAGE, "UTF-8");
    textdomain (I18N_PACKAGE);

    /* Idioma pedido: por env (override) o por defecto "sistema". La GUI
     * completa la preferencia guardada en config. */
    const char *env = g_getenv ("TRANSCRIPT_UI_LANG");
    i18n_set_language ((env != NULL && *env != '\0') ? env : "sistema");
}

const char *
i18n_t (const char *msgid)
{
    return (msgid != NULL) ? gettext (msgid) : "";
}

gboolean
i18n_has_language (const char *lang)
{
    if (lang == NULL)
        return FALSE;
    if (g_strcmp0 (lang, "sistema") == 0)
        return TRUE;
    return has_catalog (lang);
}

gboolean
i18n_set_language (const char *lang)
{
    i18n_init ();

    const char *wanted = (lang != NULL && *lang != '\0') ? lang : "sistema";

    if (g_strcmp0 (wanted, i18n_get_language ()) == 0)
        return TRUE; /* ya está activo (idempotente) */

    if (g_strcmp0 (wanted, "sistema") == 0) {
        g_unsetenv ("LANGUAGE");
        setlocale (LC_ALL, "");
        g_free (g_lang);
        g_lang = g_strdup ("sistema");
    } else {
        if (!has_catalog (wanted))
            return FALSE;
        g_setenv ("LANGUAGE", wanted, TRUE);
        setlocale (LC_ALL, wanted);
        g_free (g_lang);
        g_lang = g_strdup (wanted);
    }

    /* textdomain() de nuevo fuerza a libintl a recargar el catálogo con el
     * nuevo idioma (sin esto, glibc mantiene en caché el idioma anterior). */
    textdomain (I18N_PACKAGE);

    /* Notificar a la GUI para que re-pinte los textos. */
    if (g_cbs != NULL) {
        for (guint i = 0; i < g_cbs->len; i++) {
            const I18nCbEntry *e = g_ptr_array_index (g_cbs, i);
            if (e->cb != NULL)
                e->cb (e->user_data);
        }
    }
    return TRUE;
}

const char *
i18n_get_language (void)
{
    return (g_lang != NULL) ? g_lang : "sistema";
}

gboolean
i18n_is_rtl (void)
{
    const char *code = i18n_get_language ();

    if (g_strcmp0 (code, "sistema") == 0) {
        /* En modo sistema el idioma lo decide el locale del SO. */
        const char *const *names = g_get_language_names ();
        if (names != NULL && names[0] != NULL)
            code = names[0];
    }
    return lang_is_rtl_code (code);
}

GPtrArray *
i18n_languages (void)
{
    GPtrArray *out = g_ptr_array_new_with_free_func (g_free);

    GDir *d = g_dir_open (I18N_LOCALEDIR, 0, NULL);
    if (d == NULL)
        return out;

    const char *name;
    while ((name = g_dir_read_name (d)) != NULL) {
        /* Solo códigos simples (es, en, ...); el resto se ignora. */
        if (name[0] == '.' || strchr (name, '_') != NULL)
            continue;
        if (!has_catalog (name))
            continue;
        g_ptr_array_add (out, g_strdup (name));
    }
    g_dir_close (d);

    g_ptr_array_sort (out, compare_str);
    return out;
}

void
i18n_free_languages (GPtrArray *list)
{
    if (list != NULL)
        g_ptr_array_free (list, TRUE);
}

guint
i18n_add_changed_cb (I18nChangedCb cb, gpointer user_data)
{
    i18n_init ();

    if (g_cbs == NULL)
        g_cbs = g_ptr_array_new_with_free_func (g_free);

    I18nCbEntry *e = g_new (I18nCbEntry, 1);
    e->cb        = cb;
    e->user_data = user_data;
    e->id        = g_next_cb_id++;
    g_ptr_array_add (g_cbs, e);
    return e->id;
}

void
i18n_remove_changed_cb (guint id)
{
    if (g_cbs == NULL)
        return;

    for (guint i = 0; i < g_cbs->len; i++) {
        I18nCbEntry *e = g_ptr_array_index (g_cbs, i);
        if (e->id == id) {
            g_ptr_array_remove_index (g_cbs, i);
            return;
        }
    }
}
