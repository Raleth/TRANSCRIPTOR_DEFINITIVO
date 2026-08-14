#!/bin/sh
# Compila los catálogos de traducción: po/*.po -> locale/<lang>/LC_MESSAGES/transcriptor.mo
# Si msgfmt no está disponible, termina sin error (la app usa los msgid en español).
set -e

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
po_dir="$script_dir/../po"
locale_dir="$script_dir/../locale"

if ! command -v msgfmt >/dev/null 2>&1; then
    echo "build-mo: msgfmt no encontrado; sin catálogos compilados (idioma base: español)."
    exit 0
fi

mkdir -p "$locale_dir"
n=0
for po in "$po_dir"/*.po; do
    [ -f "$po" ] || continue
    lang="$(basename "$po" .po)"
    dest="$locale_dir/$lang/LC_MESSAGES"
    mkdir -p "$dest"
    if msgfmt -o "$dest/transcriptor.mo" "$po"; then
        n=$((n + 1))
    else
        echo "build-mo: error compilando $po"
        exit 1
    fi
done
echo "build-mo: $n catálogo(s) compilados en $locale_dir"