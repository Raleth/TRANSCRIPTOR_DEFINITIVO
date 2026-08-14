# Ana-Trans — Transcriptor de audio/video

Transcribe audio y video a texto usando **GTK 4**, **whisper.cpp** y **ffmpeg**.

> ⚠️ **Proyecto en desarrollo / incompleto.** Verificado en **Linux**. **Windows aún no está probado** — falta revisar sus funcionalidades.

## Características

- GUI GTK4: elige archivos o una carpeta completa, modelo, idioma y formato de salida.
- CLI con procesamiento por lotes (carpeta o lista de archivos de audio/video).
- Formatos de salida: `txt`, `markdown`, `latex`, `docx`, `pdf`.
- **Modo sencillo** (por defecto): 3 modelos de Whisper, descargados automáticamente en el primer uso y reutilizados después.
- **Modo personalizado**: usa cualquier modelo `.bin` de whisper.cpp.
- **Interfaz multilingüe** (es/en): cambia el idioma de la interfaz en caliente, sin reiniciar.

## Requisitos

- CMake ≥ 3.25, Ninja y un compilador C17/C++17.
- **vcpkg** clonado en `./vcpkg` (GTK 4, ffmpeg, whisper.cpp, llama.cpp y libzip se compilan desde ahí).
- Linux: GCC/Clang + `pkg-config`.
- Windows: Visual Studio Build Tools (MSVC) — *sin probar*.

## Compilación

### Linux (verificado)

```sh
git clone https://github.com/microsoft/vcpkg.git vcpkg
cmake --preset linux
cmake --build --preset linux
./build/linux/transcriptor
```

> La primera ejecución de `cmake` compila todas las dependencias (GTK4, ffmpeg, whisper.cpp, llama.cpp, libzip) desde vcpkg — puede tardar bastante.

### Windows (sin verificar)

```bat
cmake --preset windows
cmake --build --preset windows
build\windows\transcriptor.exe
```

> ⚠️ **Windows no está probado a fondo.** La auto-descarga de modelos, las rutas de datos y la GUI están pendientes de revisar. Espera fallos.

## Uso

| Comando | Acción |
|---|---|
| `./transcriptor` | Abre la GUI GTK |
| `./transcriptor /ruta/carpeta` | Transcribe todos los audio/video de la carpeta |
| `./transcriptor a.mp4 b.mkv` | Transcribe los archivos indicados |

Variables de entorno:

| Variable | Descripción | Por defecto |
|---|---|---|
| `TRANSCRIPT_LANG` | Idioma (`auto`, `es`, `en`, …) | `auto` |
| `TRANSCRIPT_FORMAT` | Formato (`txt`, `markdown`, `latex`, `docx`, `pdf`) | `docx` |
| `OUTPUT_DIR` | Carpeta de salida | `transcripciones/` |
| `WHISPER_MODEL` | Ruta a un modelo `.bin` personalizado (modo personalizado) | — |
| `FFMPEG_BIN` | Ejecutable de ffmpeg | compilado por vcpkg / `PATH` |

## Modelos

En modo sencillo los modelos se descargan bajo demanda en `~/.local/share/anatrans/models` (Linux) o `%LOCALAPPDATA%\anatrans\models` (Windows):

| Opción GUI | Modelo | Tamaño | Ideal para |
|---|---|---|---|
| Preciso | `large-v3-turbo` | ~1,6 GB | Máxima fidelidad del habla, más lento |
| Equilibrado | `medium` | ~1,5 GB | Equilibrio entre calidad y velocidad |
| Rápido | `small` | ~0,5 GB | El más veloz y ligero, algo menos preciso |

Descarga manual (cualquier modelo de whisper.cpp):

```sh
./scripts/download-ggml-model.sh small
```

## Estructura del proyecto

```
CMakeLists.txt         Sistema de build (presets CMake: linux, windows)
scripts/               Script de descarga de modelos
src/                   Fuentes: GUI, CLI, pipeline y formatos de salida
```

## Estado y limitaciones

- Proyecto en desarrollo / **incompleto**.
- **Linux es la plataforma de referencia**; las funcionalidades en Windows están **pendientes de revisión**.
- El GTK compilado por vcpkg solo soporta el backend X11, por lo que se fuerza `GDK_BACKEND=x11` al iniciar.

## Licencia

Aún no definida.

## Documentación

- [Arquitectura](docs/ARQUITECTURA.md)
- [Guía de desarrollo](docs/GUIA_DE_DESARROLLO.md)
- [Dependencias: código propio vs terceros](docs/DEPENDENCIAS.md)

---

[English](README.md) · **Español**
