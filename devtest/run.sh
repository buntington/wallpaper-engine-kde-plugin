#!/bin/sh
# Usage: devtest/run.sh <out.png> <scene.json> [qml file]
# Runs the staged plugin build in its own window (never inside plasmashell).
root=$(cd "$(dirname "$0")/.." && pwd)
QML_IMPORT_PATH="$root/stage/usr/lib/qt6/qml" QT_FORCE_STDERR_LOGGING=1 \
  timeout 30 qml6 "${3:-$root/devtest/scene.qml}" -- "$1" "$2" \
  "$HOME/.local/share/Steam/steamapps/common/wallpaper_engine/assets"
