#!/bin/bash
# Usage: devtest/sweep.sh <out dir> <scene.json>...
# Renders every scene with the installed plugin ("sys") and the staged build ("ours")
# in a standalone window, and writes screenshots, logs and results.tsv to <out dir>.
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1
shift
mkdir -p "$out"
assets="$HOME/.local/share/Steam/steamapps/common/wallpaper_engine/assets"
results="$out/results.tsv"
printf 'name\tbuild\texit\tframe\tbrightness\terrors\n' > "$results"

stats() { # <png> -> "frame brightness"
    python3 -I -c '
import sys
try:
    from PIL import Image, ImageStat
    im = Image.open(sys.argv[1]).convert("L")
    print("yes", round(ImageStat.Stat(im).mean[0], 1))
except Exception:
    print("no", "-")' "$1"
}

for scene in "$@"; do
    name=$(basename "$(dirname "$scene")")
    for build in sys ours; do
        png="$out/$name-$build.png"
        log="$out/$name-$build.log"
        rm -f "$png"
        if [ "$build" = ours ]; then
            "$root/devtest/make-data.sh" "$scene" > /dev/null 2>&1
            QML_IMPORT_PATH="$root/stage/usr/lib/qt6/qml" QT_FORCE_STDERR_LOGGING=1 \
                timeout 40 qml6 "$root/devtest/text.qml" -- "$png" "$scene" "$assets" > "$log" 2>&1
        else
            QT_FORCE_STDERR_LOGGING=1 \
                timeout 40 qml6 "$root/devtest/scene.qml" -- "$png" "$scene" "$assets" > "$log" 2>&1
        fi
        code=$?
        errors=$(grep -E '^ERROR|TypeError|ReferenceError' "$log" | grep -v genExTexture | sort -u | wc -l)
        printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$build" "$code" "$(stats "$png" | tr ' ' '\t')" "$errors" >> "$results"
    done
done
