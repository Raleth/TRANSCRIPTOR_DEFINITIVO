#!/bin/sh
# check-i18n.sh — verifica los catálogos de traducción:
#   1. msgfmt -c valida cada .po (sintaxis y formato).
#   2. Todos los .po tienen las mismas claves que es.po (la base).
#   3. Los placeholders (%s, %d, %u, %%) del msgstr coinciden con el msgid.
# Uso: scripts/check-i18n.sh   (salida 0 = todo correcto)

set -e

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
po_dir="$script_dir/../po"
base="es.po"
errores=0

if ! command -v msgfmt >/dev/null 2>&1; then
    echo "check-i18n: msgfmt no encontrado; no se puede validar."
    exit 2
fi

# 1) Sintaxis de cada .po
for po in "$po_dir"/*.po; do
    [ -f "$po" ] || continue
    if ! msgfmt -c -o /dev/null "$po" 2>/dev/null; then
        echo "ERROR: sintaxis inválida en $(basename "$po")"
        errores=$((errores + 1))
    fi
done

# 2) y 3) Comparación de claves y placeholders
extraer_msgids() {
    awk '
        /^msgid "/ {
            line=$0
            sub(/^msgid "/, "", line)
            sub(/"$/, "", line)
            print line
        }
    ' "$1"
}

placeholders() {
    printf '%s' "$1" | grep -o '%[sd u%]' | sort | tr '\n' ' '
}

base_keys="$(mktemp)"
extraer_msgids "$po_dir/$base" | sort > "$base_keys"

for po in "$po_dir"/*.po; do
    [ -f "$po" ] || continue
    lang="$(basename "$po" .po)"
    [ "$lang" = es ] && continue

    this_keys="$(mktemp)"
    extraer_msgids "$po" | sort > "$this_keys"

    faltan="$(comm -23 "$base_keys" "$this_keys")"
    sobran="$(comm -13 "$base_keys" "$this_keys")"
    if [ -n "$faltan" ]; then
        echo "ERROR ($lang): faltan $([ "$(printf '%s\n' "$faltan" | wc -l)" -gt 1 ] && echo claves || echo clave):"
        printf '%s\n' "$faltan" | sed 's/^/    - /'
        errores=$((errores + 1))
    fi
    if [ -n "$sobran" ]; then
        echo "AVISO ($lang): claves extra (no están en es.po):"
        printf '%s\n' "$sobran" | sed 's/^/    - /'
    fi

    # 3) placeholders por entrada
    awk '
        /^msgid "/ { m=$0; sub(/^msgid "/, "", m); sub(/"$/, "", m); in_msgid=1; s=""; next }
        /^msgstr "/ { in_msgid=0; s=$0; sub(/^msgstr "/, "", s); sub(/"$/, "", s); print m "\t" s }
    ' "$po" > "$this_keys.tab"

    while IFS="$(printf '\t')" read -r mid mstr; do
        ph_mid="$(placeholders "$mid")"
        ph_mst="$(placeholders "$mstr")"
        if [ "$ph_mid" != "$ph_mst" ]; then
            echo "ERROR ($lang): placeholders distintos en: $mid"
            echo "    msgid : $ph_mid"
            echo "    msgstr: $ph_mst"
            errores=$((errores + 1))
        fi
    done < "$this_keys.tab"

    rm -f "$this_keys" "$this_keys.tab"
done

rm -f "$base_keys"

if [ "$errores" -eq 0 ]; then
    echo "check-i18n: OK (todos los catálogos válidos, completos y coherentes)."
else
    echo "check-i18n: $errores error(es)."
    exit 1
fi
