#ifndef I18N_H
#define I18N_H

#include <glib.h>

G_BEGIN_DECLS

/*
 * i18n: internacionalización de la interfaz con gettext y cambio en caliente.
 *
 * [PROYECTO]    Ana-Trans: catálogos po/, cambio de idioma sin reiniciar y
 *               callback de re-render para la GUI.
 * [DEPENDENCIA] gettext (libintl, librería del sistema), GLib.
 *
 * Convención: la GUI marca cada texto con i18n_t() (o el alias t()); los
 * msgid son las cadenas en español (idioma base). Los catálogos se compilan
 * de po/*.po a locale/<lang>/LC_MESSAGES/transcriptor.mo (ver
 * scripts/build-mo.sh) y se cargan desde I18N_LOCALEDIR.
 */

/* Inicializa gettext y aplica el idioma pedido. Idempotente. */
void i18n_init (void);

/* Traduce msgid al idioma activo (gettext). Nunca devuelve NULL. */
const char *i18n_t (const char *msgid);

/* TRUE si existe catálogo para el idioma ("sistema" siempre es válido). */
gboolean i18n_has_language (const char *lang);

/*
 * Cambia el idioma en caliente: "sistema" (locale del SO) o un código
 * ISO 639 como "es"/"en". Dispara los callbacks registrados para que la
 * GUI re-pinte los textos. Devuelve FALSE si no hay catálogo.
 */
gboolean i18n_set_language (const char *lang);

/* Idioma activo ("sistema" o un código ISO 639). */
const char *i18n_get_language (void);

/* TRUE si el idioma activo se escribe de derecha a izquierda (RTL). */
gboolean i18n_is_rtl (void);

/*
 * Idiomas disponibles: códigos de los catálogos instalados (sin "sistema").
 * Devuelve un GPtrArray de gchar* (ordenado); liberar con i18n_free_languages.
 */
GPtrArray *i18n_languages (void);
void i18n_free_languages (GPtrArray *list);

/* Callbacks de cambio de idioma (para re-render de la GUI). */
typedef void (*I18nChangedCb) (gpointer user_data);
guint i18n_add_changed_cb (I18nChangedCb cb, gpointer user_data);
void i18n_remove_changed_cb (guint id);

G_END_DECLS

#endif /* I18N_H */
