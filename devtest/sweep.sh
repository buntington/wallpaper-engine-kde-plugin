#!/bin/bash
# Usage: [BUILDS="sys ours"] [VISIBLE=1] devtest/sweep.sh <out dir> <scene.json>...
# Renders every scene with the installed plugin ("sys") and the staged build ("ours")
# and writes screenshots, logs and results.tsv to <out dir>. Windows open in a
# headless KWin so nothing shows on screen; VISIBLE=1 opens them on the desktop.
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

run() { # <command...>, headless unless VISIBLE is set
    if [ -n "$VISIBLE" ]; then
        "$@"
    else
        WAYLAND_DISPLAY= kwin_wayland --virtual --no-lockscreen --socket wek-sweep \
            --width 1280 --height 800 --exit-with-session \
            "$(printf '%q ' env QT_QPA_PLATFORM=wayland WAYLAND_DISPLAY=wek-sweep "$@")"
    fi
}

for scene in "$@"; do
    name=$(basename "$(dirname "$scene")")
    for build in ${BUILDS:-sys ours}; do
        png="$out/$name-$build.png"
        log="$out/$name-$build.log"
        rm -f "$png"
        if [ "$build" = ours ]; then
            "$root/devtest/make-data.sh" "$scene" > /dev/null 2>&1
            run timeout 40 env QML_IMPORT_PATH="$root/stage/usr/lib/qt6/qml" QT_FORCE_STDERR_LOGGING=1 \
                qml6 "$root/devtest/text.qml" -- "$png" "$scene" "$assets" > "$log" 2>&1
        else
            run timeout 40 env QT_FORCE_STDERR_LOGGING=1 \
                qml6 "$root/devtest/scene.qml" -- "$png" "$scene" "$assets" > "$log" 2>&1
        fi
        code=$?
        errors=$(grep -E '^ERROR|TypeError|ReferenceError' "$log" | grep -v genExTexture | sort -u | wc -l)
        printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$build" "$code" "$(stats "$png" | tr ' ' '\t')" "$errors" >> "$results"
    done
done
