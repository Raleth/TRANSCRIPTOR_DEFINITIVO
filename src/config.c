/* ============================================================================
 * config.c — preferencias del usuario (env > config.ini > valor por defecto).
 *
 * [PROYECTO]    Ana-Trans: resolución de valores y guardado de config.ini.
 * [DEPENDENCIA] GLib (GKeyFile, rutas, g_getenv).
 * ========================================================================== */

#include "config.h"

/* módulos del proyecto */
#include "models.h"

/* dependencias (GLib) */
#include <glib/gstdio.h>

#define CONFIG_GROUP      "preferences"
#define CONFIG_FILE_NAME  "config.ini"

/* Valores cacheados del archivo de configuración. */
static gchar *cfg_whisper_model  = NULL;
static gchar *cfg_output_dir     = NULL;
static gchar *cfg_transcript_lang = NULL;
static gchar *cfg_transcript_format = NULL;
static gchar *cfg_model_mode = NULL;
static gchar *cfg_easy_model = NULL;
static gchar *cfg_ui_language = NULL;

static GKeyFile *keyfile = NULL;
static gchar *config_file = NULL;
static gboolean loaded = FALSE;

static void
config_ensure (void)
{
    if (loaded)
        return;
    loaded = TRUE;

    config_file = g_build_filename (g_get_user_config_dir (), "anatrans",
                                    CONFIG_FILE_NAME, NULL);

    keyfile = g_key_file_new ();
    GError *err = NULL;
    if (!g_key_file_load_from_file (keyfile, config_file, G_KEY_FILE_NONE, &err)) {
        if (err != NULL) {
            if (!g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
                g_warning ("No se pudo leer la configuración %s: %s",
                           config_file, err->message);
            g_error_free (err);
        }
    }

    /* cachear valores presentes */
    cfg_whisper_model   = g_key_file_get_string (keyfile, CONFIG_GROUP, "whisper-model", NULL);
    cfg_output_dir      = g_key_file_get_string (keyfile, CONFIG_GROUP, "output-dir", NULL);
    cfg_transcript_lang = g_key_file_get_string (keyfile, CONFIG_GROUP, "transcript-lang", NULL);
    cfg_transcript_format = g_key_file_get_string (keyfile, CONFIG_GROUP, "transcript-format", NULL);
    cfg_model_mode  = g_key_file_get_string (keyfile, CONFIG_GROUP, "model-mode", NULL);
    cfg_easy_model  = g_key_file_get_string (keyfile, CONFIG_GROUP, "easy-model", NULL);
    cfg_ui_language = g_key_file_get_string (keyfile, CONFIG_GROUP, "ui-language", NULL);
}

void
config_init (void)
{
    config_ensure ();
}

const char *
config_get_config_file (void)
{
    config_ensure ();
    return config_file;
}

const char *
config_default_output_dir (void)
{
    return "transcripciones";
}

const char *
config_get_output_dir (void)
{
    const char *env = g_getenv ("OUTPUT_DIR");
    if (env != NULL && *env != '\0')
        return env;
    config_ensure ();
    return (cfg_output_dir != NULL && *cfg_output_dir != '\0')
               ? cfg_output_dir : config_default_output_dir ();
}

const char *
config_get_whisper_model (void)
{
    const char *env = g_getenv ("WHISPER_MODEL");
    if (env != NULL && *env != '\0')
        return env;
    config_ensure ();

    /* modo sencillo: la ruta del modelo auto-descargado. Se usa
     * config_get_model_mode() para respetar el valor por defecto (easy). */
    if (g_ascii_strcasecmp (config_get_model_mode (), "easy") == 0) {
        const char *easy = config_get_easy_model ();
        static gchar *easy_path = NULL;
        g_free (easy_path);
        easy_path = models_path (easy);
        return (easy_path != NULL) ? easy_path : "";
    }

    return (cfg_whisper_model != NULL) ? cfg_whisper_model : "";
}

const char *
config_get_ffmpeg_bin (void)
{
    /* 1) override explícito del usuario. */
    const char *value = g_getenv ("FFMPEG_BIN");
    if (value != NULL && *value != '\0')
        return value;

    /* 2) binario compilado por vcpkg (ruta definida por CMake en
     *    VCPKG_FFMPEG_TOOLS_DIR, ver CMakeLists.txt). */
#ifdef VCPKG_FFMPEG_TOOLS_DIR
    static gchar *vcpkg_bin = NULL;
    if (vcpkg_bin == NULL) {
        gchar *candidate = g_build_filename (VCPKG_FFMPEG_TOOLS_DIR, "ffmpeg", NULL);
        if (g_file_test (candidate, G_FILE_TEST_IS_EXECUTABLE))
            vcpkg_bin = candidate;
        else
            g_free (candidate);
    }
    if (vcpkg_bin != NULL)
        return vcpkg_bin;
#endif

    /* 3) último recurso: binario en el PATH del sistema. */
    return "ffmpeg";
}

const char *
config_get_transcript_lang (void)
{
    const char *env = g_getenv ("TRANSCRIPT_LANG");
    if (env != NULL && *env != '\0')
        return env;
    config_ensure ();
    return (cfg_transcript_lang != NULL && *cfg_transcript_lang != '\0')
               ? cfg_transcript_lang : "auto";
}

const char *
config_get_transcript_format (void)
{
    const char *env = g_getenv ("TRANSCRIPT_FORMAT");
    if (env != NULL && *env != '\0')
        return env;
    config_ensure ();
    return (cfg_transcript_format != NULL && *cfg_transcript_format != '\0')
               ? cfg_transcript_format : "docx";
}

/* --- setters --- */

void
config_set_whisper_model (const char *value)
{
    config_ensure ();
    g_free (cfg_whisper_model);
    cfg_whisper_model = g_strdup (value != NULL ? value : "");
    g_key_file_set_string (keyfile, CONFIG_GROUP, "whisper-model", cfg_whisper_model);
}

void
config_set_output_dir (const char *value)
{
    config_ensure ();
    g_free (cfg_output_dir);
    cfg_output_dir = g_strdup (value != NULL ? value : "");
    g_key_file_set_string (keyfile, CONFIG_GROUP, "output-dir", cfg_output_dir);
}

void
config_set_transcript_lang (const char *value)
{
    config_ensure ();
    g_free (cfg_transcript_lang);
    cfg_transcript_lang = g_strdup (value != NULL ? value : "");
    g_key_file_set_string (keyfile, CONFIG_GROUP, "transcript-lang", cfg_transcript_lang);
}

void
config_set_transcript_format (const char *value)
{
    config_ensure ();
    g_free (cfg_transcript_format);
    cfg_transcript_format = g_strdup (value != NULL ? value : "");
    g_key_file_set_string (keyfile, CONFIG_GROUP, "transcript-format", cfg_transcript_format);
}

const char *
config_get_model_mode (void)
{
    config_ensure ();
    return (cfg_model_mode != NULL && *cfg_model_mode != '\0') ? cfg_model_mode : "easy";
}

void
config_set_model_mode (const char *value)
{
    config_ensure ();
    g_free (cfg_model_mode);
    cfg_model_mode = g_strdup ((value != NULL && *value != '\0') ? value : "easy");
    g_key_file_set_string (keyfile, CONFIG_GROUP, "model-mode", cfg_model_mode);
}

const char *
config_get_easy_model (void)
{
    config_ensure ();
    return (cfg_easy_model != NULL && *cfg_easy_model != '\0')
               ? cfg_easy_model : "large-v3-turbo";
}

void
config_set_easy_model (const char *value)
{
    config_ensure ();
    g_free (cfg_easy_model);
    cfg_easy_model = g_strdup ((value != NULL && *value != '\0')
                                   ? value : "large-v3-turbo");
    g_key_file_set_string (keyfile, CONFIG_GROUP, "easy-model", cfg_easy_model);
}

/* --- guardado --- */

const char *
config_get_ui_language (void)
{
    config_ensure ();
    return (cfg_ui_language != NULL && *cfg_ui_language != '\0')
               ? cfg_ui_language : "sistema";
}

void
config_set_ui_language (const char *value)
{
    config_ensure ();
    g_free (cfg_ui_language);
    cfg_ui_language = g_strdup ((value != NULL && *value != '\0') ? value : "sistema");
    g_key_file_set_string (keyfile, CONFIG_GROUP, "ui-language", cfg_ui_language);
}

gboolean
config_save (void)
{
    config_ensure ();

    gchar *dir = g_path_get_dirname (config_file);
    gboolean ok = TRUE;

    if (g_mkdir_with_parents (dir, 0755) != 0) {
        g_warning ("No se pudo crear la carpeta de configuración %s", dir);
        ok = FALSE;
    } else {
        gchar *data = g_key_file_to_data (keyfile, NULL, NULL);
        GError *err = NULL;
        if (!g_file_set_contents (config_file, data, -1, &err)) {
            g_warning ("No se pudo guardar la configuración %s: %s",
                       config_file, (err != NULL) ? err->message : "?");
            if (err != NULL)
                g_error_free (err);
            ok = FALSE;
        }
        g_free (data);
    }

    g_free (dir);
    return ok;
}
