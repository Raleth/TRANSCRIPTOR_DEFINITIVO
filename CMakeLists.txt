cmake_minimum_required(VERSION 3.25)

project(transcriptor
    VERSION 0.1.0
    DESCRIPTION "Ana-Trans: transcriptor de audio/video con GTK 4, whisper.cpp y llama.cpp"
    LANGUAGES C CXX
)

# ---------------------------------------------------------------------------
# Estándares de lenguaje
# ---------------------------------------------------------------------------
# El proyecto utiliza C17 para los fuentes C y C++17 para los fuentes C++.
# Estos valores llegan a compile_commands.json, de modo que clangd recibe
# exactamente el mismo estándar que el compilador real.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# vcpkg
# ---------------------------------------------------------------------------
# El toolchain de vcpkg (definido en CMakePresets.json) define
# VCPKG_INSTALLED_DIR y VCPKG_TARGET_TRIPLET antes de llegar a este archivo.
#
# vcpkg NO configura PKG_CONFIG_PATH automáticamente para los consumidores
# (solo lo hace internamente al compilar ports), por lo que se indica aquí
# para que GTK se resuelva desde vcpkg y no desde un GTK del sistema.
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_VCPKG_PKG_CONFIG_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig")
    if(WIN32)
        set(_PKG_CONFIG_SEP ";")
    else()
        set(_PKG_CONFIG_SEP ":")
    endif()
    set(ENV{PKG_CONFIG_PATH} "${_VCPKG_PKG_CONFIG_DIR}${_PKG_CONFIG_SEP}$ENV{PKG_CONFIG_PATH}")
endif()

# ---------------------------------------------------------------------------
# Dependencias
# ---------------------------------------------------------------------------
# GTK 4: el port de vcpkg no exporta un config de CMake, así que se detecta
# vía pkg-config. IMPORTED_TARGET expone el target PkgConfig::GTK4 con todos
# los -I/-D necesarios (incluye gtk-4.0, glib, pango, cairo, etc.).
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

# whisper.cpp (port vcpkg "whisper-cpp"): exporta el target IMPORTED
# "whisper", que propaga sus include dirs y enlaza ggml::ggml y ggml::ggml-base.
find_package(whisper CONFIG REQUIRED)

# llama.cpp (port vcpkg "llama-cpp"): exporta el target IMPORTED "llama",
# que propaga sus include dirs y enlaza ggml::ggml y ggml::ggml-base.
find_package(llama CONFIG REQUIRED)

# libzip: generación de documentos DOCX.
find_package(libzip CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Target principal
# ---------------------------------------------------------------------------
add_executable(transcriptor
    src/main.c
    src/gui.c
    src/batch.c
    src/config.c
    src/audio.c
    src/process.c
    src/transcribe.c
    src/formats.c
    src/fmt_txt.c
    src/fmt_markdown.c
    src/fmt_latex.c
    src/fmt_docx.c
    src/fmt_pdf.c
)

target_include_directories(transcriptor PRIVATE src)

# Ruta del binario ffmpeg compilado por vcpkg, para localizarlo en runtime
# (ver config_get_ffmpeg_bin en src/config.c).
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    target_compile_definitions(transcriptor PRIVATE
        VCPKG_FFMPEG_TOOLS_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/ffmpeg")
endif()

# El estándar queda asociado al target para que compile_commands.json
# refleje exactamente lo que se compila.
target_compile_features(transcriptor PRIVATE c_std_17)

target_link_libraries(transcriptor
    PRIVATE
        PkgConfig::GTK4   # GTK 4
        whisper           # whisper.cpp
        llama             # llama.cpp
        libzip::zip       # DOCX
)
