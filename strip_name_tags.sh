#!/bin/sh
# Strip redundant category tags like "[ESP32]" from app display names (the name=
# field in each application.fam). The apps are now sorted into sub-folders
# (GPIO/ESP32, GPIO/NRF24, ...) by categorize_apps.sh, so the tag just repeats the
# folder -- e.g. an ESP32/ folder full of "[ESP32] ..." entries.
#
# This runs on the BUILD COPIES only (after the apps are moved into the firmware's
# applications_user/, before fbt compiles them), so the repo's app sources are left
# untouched -- handy for apps that get upstreamed.
#
# Only tags that duplicate a folder name are stripped. Hardware/chip tags such as
# [J305], [MH-Z19], [W5500] or [YRM100] are kept: they name the specific module,
# which the folder ("Sensors", "Programmers") does not.
#
# Usage: strip_name_tags.sh <apps-dir>   e.g. unleashed-firmware/applications_user
set -eu

DIR="${1:?usage: strip_name_tags.sh <apps-dir>}"

# Tags to strip. Each of these duplicates a category / sub-category folder name.
# Edit this list to add or remove tags.
TAGS="ESP32 ESP8266 NRF24 VGM GPIO"

find "$DIR" -name application.fam | while read -r fam; do
    for tag in $TAGS; do
        if grep -q "name=\"\[$tag\]" "$fam" 2>/dev/null; then
            # name="[TAG] Foo"  ->  name="Foo"   (temp file: portable across sed variants)
            sed "s/name=\"\[$tag\] */name=\"/g" "$fam" >"$fam.tmp" && mv "$fam.tmp" "$fam"
            echo "  stripped [$tag] from $fam"
        fi
    done
done

echo "Done stripping name tags in $DIR"
