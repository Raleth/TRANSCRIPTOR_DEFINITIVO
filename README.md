# Ana-Trans — Audio/Video Transcriber

Transcribe audio and video to text using **GTK 4**, **whisper.cpp** and **ffmpeg**.

> ⚠️ **Work in progress / incomplete.** Verified on **Linux**. **Windows is not tested yet** — its functionality still needs to be reviewed.

## Features

- GTK4 GUI: pick files or a whole folder, choose the model, language and output format.
- CLI with batch processing (a folder or any list of media files).
- Output formats: `txt`, `markdown`, `latex`, `docx`, `pdf`.
- **Easy mode** (default): 3 Whisper models, auto-downloaded on first run and reused afterwards.
- **Custom mode**: use any whisper.cpp `.bin` model.
- **Multilingual UI** (es/en): switch the interface language at runtime, without restarting.

## Requirements

- CMake ≥ 3.25, Ninja and a C17/C++17 compiler.
- **vcpkg** cloned into `./vcpkg` (GTK 4, ffmpeg, whisper.cpp, llama.cpp and libzip are built from it).
- Linux: GCC/Clang + `pkg-config`.
- Windows: Visual Studio Build Tools (MSVC) — *untested*.

## Build

### Linux (verified)

```sh
git clone https://github.com/microsoft/vcpkg.git vcpkg
cmake --preset linux
cmake --build --preset linux
./build/linux/transcriptor
```

> The first `cmake` run compiles all dependencies (GTK4, ffmpeg, whisper.cpp, llama.cpp, libzip) from vcpkg — it can take a long time.

### Windows (not verified)

```bat
cmake --preset windows
cmake --build --preset windows
build\windows\transcriptor.exe
```

> ⚠️ **Windows is not fully tested.** Model auto-download, data paths and the GUI still need to be reviewed. Expect issues.

## Usage

| Command | Action |
|---|---|
| `./transcriptor` | Opens the GTK GUI |
| `./transcriptor /path/to/folder` | Transcribes every audio/video file in the folder |
| `./transcriptor a.mp4 b.mkv` | Transcribes the given files |

Environment variables:

| Variable | Description | Default |
|---|---|---|
| `TRANSCRIPT_LANG` | Language (`auto`, `es`, `en`, …) | `auto` |
| `TRANSCRIPT_FORMAT` | Output format (`txt`, `markdown`, `latex`, `docx`, `pdf`) | `docx` |
| `OUTPUT_DIR` | Output folder | `transcripciones/` |
| `WHISPER_MODEL` | Path to a custom `.bin` model (custom mode) | — |
| `FFMPEG_BIN` | ffmpeg executable | vcpkg build / `PATH` |

## Models

In easy mode, models are downloaded on demand to `~/.local/share/anatrans/models` (Linux) or `%LOCALAPPDATA%\anatrans\models` (Windows):

| GUI option | Model | Size | Best for |
|---|---|---|---|
| Preciso | `large-v3-turbo` | ~1.6 GB | Maximum speech fidelity, slower |
| Equilibrado | `medium` | ~1.5 GB | Balance between quality and speed |
| Rápido | `small` | ~0.5 GB | Fastest, lightweight, slightly less accurate |

Manual download (any whisper.cpp model):

```sh
./scripts/download-ggml-model.sh small
```

## Project layout

```
CMakeLists.txt         Build system (CMake presets: linux, windows)
scripts/               Model download helper script
src/                   Sources: GUI, CLI, pipeline and output formats
```

## Status & limitations

- Work in progress / **incomplete**.
- **Linux is the reference platform**; Windows functionality is **pending review**.
- GTK built by vcpkg only supports the X11 backend, so `GDK_BACKEND=x11` is forced at startup.

## License

Not defined yet.

## Documentation

- [Architecture](docs/ARQUITECTURA.md)
- [Development guide](docs/GUIA_DE_DESARROLLO.md)
- [Dependencies: own code vs third-party](docs/DEPENDENCIAS.md)

---

**English** · [Español](README.es.md)
