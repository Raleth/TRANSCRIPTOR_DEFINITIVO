# Dependencias del proyecto

Este documento explica qué librerías, herramientas y servicios externos usa
**Ana-Trans**, para qué sirve cada uno y dónde aparece en el código.

Regla general en el código: lo marcado **`[PROYECTO]`** es lógica escrita por
nosotros; lo marcado **`[DEPENDENCIA]`** es código o binario de terceros.

## Tabla rápida

| Dependencia | ¿Qué es? | ¿Para qué se usa aquí? | Archivos | Fuente |
|---|---|---|---|---|
| **GLib** | Biblioteca base de C de GNOME: tipos, `GError`, contenedores, main loop, procesos (`g_spawn`), rutas | Prácticamente todo el proyecto | Todos los `src/*` | vcpkg (viene con GTK) |
| **GTK 4** | Toolkit de interfaz gráfica | Ventanas, widgets, diálogos de la GUI | `gui.c`, `gui.h`, `main.c` | vcpkg (port `gtk`) |
| **whisper.cpp** | Implementación en C/C++ del modelo Whisper (OpenAI) | Transcripción de audio a texto | `transcribe.c`, `transcribe.h` | vcpkg (port `whisper-cpp`) |
| **llama.cpp** | Infraestructura de modelos LLM en C/C++ | **No se usa en el código fuente** | — | vcpkg (port `llama-cpp`) |
| **libzip** | Biblioteca para crear/leer archivos ZIP | Generar documentos DOCX | `fmt_docx.c` | vcpkg (port `libzip`) |
| **cairo + Pango** | Dibujo 2D y renderizado de texto UTF-8 | Generar PDF | `fmt_pdf.c` | llegan con GTK |
| **ffmpeg** | Suite de conversión de audio/video (binario) | Convertir la entrada a WAV PCM16 mono 16 kHz | `audio.c` | vcpkg (features `ffmpeg`/`ffprobe`) o del sistema |
| **vcpkg** | Gestor de dependencias C/C++ de Microsoft | Compila e instala las librerías del proyecto | `vcpkg.json`, `CMakeLists.txt`, `CMakePresets.json` | clon externo en `./vcpkg` |
| **sh + curl/wget** | Shell y descargadores de red | Ejecutar `scripts/download-ggml-model.sh` | `models.c` | sistema operativo |
| **Hugging Face** | Servidor web que aloja los modelos | Servir los modelos Whisper en formato ggml | `scripts/download-ggml-model.sh` | servicio externo |

## Detalle por dependencia

### GLib — [DEPENDENCIA]
- **Qué usamos**: `GError` (errores), `GPtrArray`/`GString` (datos), `GKeyFile`
  (config.ini), `g_spawn_sync` (procesos), `g_idle_add` (hilos → main loop),
  `GDir` (carpetas), `g_path_*`, `g_str_*`, `g_ascii_*`, funciones de
  memoria (`g_new`, `g_free`, `g_clear_pointer`), `g_get_user_data_dir()`.
- **Dónde**: todos los archivos; los `.h` incluyen `<glib.h>`.

### GTK 4 — [DEPENDENCIA]
- **Qué usamos**: `GtkApplication`, `GtkWindow`, entradas, botones, combos,
  radios, cajas, `GtkTextBuffer`, diálogos de archivos, `GtkDropDown`.
- **Dónde**: `gui.c`/`gui.h` y el arranque en `main.c`.

### whisper.cpp — [DEPENDENCIA]
- **Qué usamos**: `whisper.h` (`struct whisper_context`, `whisper_full_params`,
  `whisper_full`, `whisper_full_n_segments`, `whisper_full_get_segment_*`).
- **Dónde**: `transcribe.c`/`transcribe.h`.
- **Nota**: el port de vcpkg es `whisper-cpp`; expone el target CMake `whisper`.

### llama.cpp — [DEPENDENCIA DECLARADA PERO SIN USO]
- Aparece en `vcpkg.json` y en `CMakeLists.txt` (`find_package(llama)` y
  `target_link_libraries(... llama)`), pero **ningún fuente la utiliza**:
  la transcripción la hace whisper.cpp. Es candidata a eliminarse para
  simplificar la compilación.

### libzip — [DEPENDENCIA]
- **Qué usamos**: `zip_open`, `zip_source_buffer`, `zip_file_add`, `zip_close`
  para empaquetar el DOCX (es un ZIP con XML dentro).
- **Dónde**: `fmt_docx.c` (`#include <zip.h>`).

### cairo / Pango — [DEPENDENCIA]
- **Qué usamos**: `cairo_pdf_surface_create`, `pango_cairo_create_layout` para
  dibujar el PDF.
- **Dónde**: `fmt_pdf.c`. No se declaran aparte en `vcpkg.json`: llegan como
  parte de GTK.

### ffmpeg — [DEPENDENCIA]
- Se ejecuta como **proceso externo** (no se enlaza): `audio.c` lanza
  `ffmpeg -y -loglevel error -i <entrada> -ar 16000 -ac 1 -c:a pcm_s16le <wav>`.
- La ruta del binario la resuelve `config_get_ffmpeg_bin()` (`config.c`):
  `$FFMPEG_BIN` → binario compilado por vcpkg → `PATH`.

### vcpkg — [HERRAMIENTA DE BUILD]
- `vcpkg.json` declara las dependencias; `CMakePresets.json` apunta al
  toolchain; las librerías se instalan en `vcpkg_installed/` (ignorado por git).
- `./vcpkg` no se sube al repositorio: quien clone debe clonar vcpkg aparte.

### Script de descarga de modelos — [PROYECTO, con partes de terceros]
- `scripts/download-ggml-model.sh` está basado en el script oficial del
  proyecto whisper.cpp (ggerganov). Descarga los `.bin` desde Hugging Face
  (`huggingface.co/ggerganov/whisper.cpp`). Requiere `sh` y `curl`/`wget`.

## Qué es de Ana-Trans ([PROYECTO])

Todo lo que no esté en la tabla anterior: la GUI, el motor asíncrono
(`controller`), el pipeline (`process`), la configuración (`config`), el
escaneo de archivos (`batch`), la gestión de modelos (`models`), la limpieza
de temporales (`cleanup`), el sistema de formatos (`formats` + `fmt_*`) y el
arranque (`main`).

## Archivo → tipo

| Archivo | Tipo | Dependencias directas |
|---|---|---|
| `src/main.c` | PROPIO | GTK, GLib |
| `src/gui.c` | PROPIO | GTK4, GLib, módulos propios |
| `src/controller.c` | PROPIO | GLib, módulos propios |
| `src/process.c` | PROPIO | GLib, módulos propios |
| `src/transcribe.c` | PROPIO | **whisper.cpp**, GLib |
| `src/audio.c` | PROPIO | **ffmpeg** (binario), GLib |
| `src/batch.c` | PROPIO | GLib |
| `src/config.c` | PROPIO | GLib |
| `src/models.c` | PROPIO | GLib, script sh |
| `src/cleanup.c` | PROPIO | GLib, señales POSIX |
| `src/formats.c` | PROPIO | GLib |
| `src/fmt_txt.c`, `src/fmt_markdown.c`, `src/fmt_latex.c` | PROPIO | GLib |
| `src/fmt_docx.c` | PROPIO | **libzip**, GLib |
| `src/fmt_pdf.c` | PROPIO | **cairo/Pango**, GLib |
| `scripts/download-ggml-model.sh` | PROPIO (base de terceros) | sh, curl/wget |
