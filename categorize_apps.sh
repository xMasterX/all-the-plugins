#!/bin/sh
# Reorganise the built .fap files into sub-folders inside their category folders,
# so the Flipper app browser shows tidy sub-categories (e.g. GPIO/ESP32, Games/Puzzle).
#
# This is purely a post-build reshuffle for CI. It moves <cat>/<appid>.fap into
# <cat>/<sub>/. App sources and the repo's app folders are NOT touched.
#
# The grouping itself is DATA, not code: it lives in the categories/ folder next to
# this script, as one file per sub-category:
#
#     categories/<Category>/<Subcategory>.txt   e.g. categories/GPIO/ESP32.txt
#
# Each file lists one appid (the .fap filename) per line; blank lines and lines
# starting with '#' are ignored. To move an app, edit the relevant .txt file. To add
# a whole sub-category, drop in a new .txt file. No need to touch this script.
#
# Files that aren't present in the artifacts tree are skipped, so it is safe to run
# against both the base and the extra artifact trees (each app is only in one).
#
# Usage: categorize_apps.sh <artifacts-root>
#   <artifacts-root> is the directory that directly contains the category folders
#   (GPIO/, Games/, NFC/, ...).
set -eu

ROOT="${1:?usage: categorize_apps.sh <artifacts-root>}"
CATS_DIR="$(dirname "$0")/categories"

if [ ! -d "$CATS_DIR" ]; then
    echo "categorize_apps.sh: no categories/ folder at $CATS_DIR, nothing to do" >&2
    exit 0
fi

echo "Categorising apps in $ROOT (rules from $CATS_DIR)"

# For each categories/<Category>/<Sub>.txt, move every listed appid into <Category>/<Sub>/
for catdir in "$CATS_DIR"/*/; do
    [ -d "$catdir" ] || continue
    category=$(basename "$catdir")
    for subfile in "$catdir"*.txt; do
        [ -f "$subfile" ] || continue
        sub=$(basename "$subfile" .txt)
        # Read appids, ignoring comments (# ...) and surrounding whitespace.
        while IFS= read -r line || [ -n "$line" ]; do
            appid=$(printf '%s' "$line" | sed 's/#.*//' | tr -d '[:space:]')
            [ -z "$appid" ] && continue
            src="$ROOT/$category/$appid.fap"
            if [ -f "$src" ]; then
                mkdir -p "$ROOT/$category/$sub"
                mv "$src" "$ROOT/$category/$sub/$appid.fap"
                echo "  $category/$appid.fap -> $category/$sub/"
            fi
        done <"$subfile"
    done
done

echo "Done categorising $ROOT"
