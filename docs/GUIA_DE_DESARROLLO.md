# Guía de desarrollo

Objetivo de este documento: que una persona sin contexto pueda **compilar,
entender y modificar** Ana-Trans con seguridad.

## Compilar (Linux)

```sh
# 1) vcpkg (una sola vez; no se sube al repositorio)
git clone https://github.com/microsoft/vcpkg.git vcpkg

# 2) configurar y compilar (la 1ª vez tarda: compila GTK, ffmpeg, whisper...)
cmake --preset linux
cmake --build --preset linux

# 3) ejecutar
./build/linux/transcriptor                          # GUI
./build/linux/transcriptor /ruta/a/carpeta          # CLI: lote
```

Más detalles (requisitos, Windows) en el `README.md`.

## Estructura de `src/`

| Archivo | Qué contiene |
|---|---|
| `main.c` | Arranque: decide CLI o GUI |
| `gui.c` | Ventana GTK4: pestaña Transcripción y Preferencias |
| `batch.c` | Escaneo/filtrado de archivos de audio/video |
| `config.c` | Preferencias (env > config.ini > default) |
| `audio.c` | Conversión a WAV con ffmpeg |
| `transcribe.c` | Transcripción con whisper.cpp |
| `process.c` | El lote: convierte + transcribe + escribe |
| `controller.c` | Motor asíncrono (hilo) que llama a process |
| `formats.c` | Registro de formatos de salida |
| `fmt_txt.c`, `fmt_markdown.c`, `fmt_latex.c`, `fmt_docx.c`, `fmt_pdf.c` | Un formato cada uno |
| `models.c` | Modelos del modo sencillo (catálogo + descarga) |
| `cleanup.c` | WAV temporales y limpieza ante señales |

Cada módulo tiene su `.h` (interfaz pública documentada) y su `.c`
(implementación). **Todas las funciones internas son `static`**: solo se
expone en el `.h` lo que otros módulos deben usar.

## Convenciones del código

- **Lenguaje**: C17 (los archivos `.c`), C++17 (solo para consumir librerías).
  El estándar lo fija CMake (`.c_std_17`).
- **Librerías base**: GLib para memoria/tipos/errores; GTK 4 solo en la GUI.
- **Comentarios en español**, con el marcado:
  - `[PROYECTO]` = lógica propia de Ana-Trans.
  - `[DEPENDENCIA]` = librería o herramienta de terceros.
  - Los includes se agrupan: primero `/* módulos del proyecto */`, luego
    `/* dependencias (sistema / vcpkg) */`.
- **Errores**: siempre `GError**` en las funciones que pueden fallar; nunca
  códigos de retorno crípticos.
- **Memoria**: funciones `g_*` de GLib (`g_new`, `g_free`, `g_clear_pointer`);
  el `*.h` documenta quién libera cada estructura.
- **Texto de usuario**: en español, directamente (no hay sistema de
  traducciones todavía).
- **Estilo**: 4 espacios, llaves de función en línea propia, `static` para lo
  interno, prototipos estáticos al inicio del `.c` cuando hacen falta.

## Añadir un formato de salida nuevo

1. Crea `src/fmt_<x>.c` con una variable `FormatWriter` (id, label,
   extension, función `write`) y la función `fmt_<x>_register()`.
2. Declara y llama `fmt_<x>_register()` dentro de `formats_init()`
   (`src/formats.c`).
3. Añade el archivo a `add_executable` en `CMakeLists.txt`.
4. El resto (desplegable de la GUI, CLI, configuración) funciona solo.

Modelo a imitar: `src/fmt_markdown.c` (el más corto).

## Añadir un idioma a la interfaz

La interfaz se traduce con **gettext** (módulo `src/i18n.c`); el español es el
idioma base (los `msgid` son las cadenas en español) y el cambio de idioma es
**en caliente** (sin reiniciar). Para añadir un idioma:

1. Crea `po/<codigo>.po` (copia de `po/en.po` como plantilla) y traduce los
   `msgstr`. También añade su nombre al `ui_lang_name()` de `gui.c` para que
   se muestre en su propio idioma en el selector.
2. Recompila: `cmake --build build/linux` (o `scripts/build-mo.sh`) genera
   `locale/<codigo>/LC_MESSAGES/transcriptor.mo`.
3. Verifica: `scripts/check-i18n.sh` (completitud y placeholders) y
   `ctest --test-dir build/linux` (anchos y cambio en caliente).

Reglas al añadir/mover textos de la GUI:

- Envuélvelos en `t_("...")` para mensajes dinámicos (estado, progreso) o
  regístralos con `i18n_bind(widget, tipo, "texto base")` para los textos
  estáticos (títulos, botones, placeholders, pestañas).
- Los `msgid` no deben variar entre versiones (el catálogo los referencia);
  cambia el texto en `es.po` y en el código a la vez.
- Los textos con `%s`/`%d` deben mantener los mismos placeholders en todas
  las traducciones (lo comprueba `check-i18n.sh`).

## Añadir un modelo al modo sencillo

1. Añade una entrada a `k_models` en `src/models.c` con `id`, `filename`
   (`ggml-<id>.bin`), `label` y `desc`.
2. Comprueba que `scripts/download-ggml-model.sh` acepta ese `id` (la lista
   está en la variable `models` del script). Si no, añádelo allí.

## Modificar la GUI

- Todo el estado vive en `AppState` (`gui.c`, struct al inicio).
- `build_ui()` crea los widgets; los callbacks se llaman `on_*`.
- Para una preferencia nueva:
  1. Añade getter/setter en `config.c`/`config.h`.
  2. Añade el widget en la pestaña Preferencias de `build_ui()`.
  3. Conecta el callback que llama al setter y programa el guardado con
     `schedule_config_save()` (debounce de 400 ms).
- La barra de progreso y el log se alimentan únicamente desde
  `on_controller_event()`: no escribas en widgets desde otro sitio.

## Depurar

- La **CLI** es la vía más rápida (sin ventanas):
  `./build/linux/transcriptor /tmp/prueba.mp3`.
- `g_print`/`g_printerr` salen por consola; en la GUI el log también se imprime.
- Para problemas de transcripción prueba con el modelo `small` (rápido) y
  `TRANSCRIPT_FORMAT=txt`.
- `gdb ./build/linux/transcriptor` y `valgrind` funcionan con normalidad.

## Qué NO tocar sin entenderlo antes

- Los **handlers de señal** de `cleanup.c`: solo pueden usar operaciones
  async-signal-safe (`unlink`, `rmdir`, `_exit`). Añadir `g_*` ahí es un bug.
- El **bucle de eventos** de `controller.c`: la GUI recibe eventos en su
  hilo; modificar la entrega rompe toda la interfaz.
- La línea `g_setenv ("GDK_BACKEND", "x11", TRUE)` de `main.c`: quitarla hace
  que la GUI no abra en sesiones Wayland con el GTK de vcpkg.

## Windows (estado pendiente)

Existe el preset `windows` (`cmake --preset windows`), pero **no está
verificado**. Faltan por revisar, como mínimo: el script de descarga de
modelos (es un `.sh`), las rutas de datos (`g_get_user_data_dir()`), el
backend de GTK y la compilación de `cleanup.c` (`#ifdef _WIN32`).
