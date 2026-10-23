#!/bin/sh
# Usage: devtest/make-data.sh <scene.json> [user_properties JSON]
# Writes devtest/scene-data.js with what pyext.scene_text() returns for the scene.
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root/plugin/contents" && python3 -c "
import sys, types, json; sys.modules['websockets'] = types.ModuleType('websockets')
import pyext
pyext.M.read_wallpaper_config = lambda wid: {'user_properties': json.loads(sys.argv[3])}
r = pyext.scene_text(sys.argv[1], sys.argv[2])
open(sys.argv[4], 'w').write('.pragma library\nvar data = ' + json.dumps(r) + ';')
" "$1" "$HOME/.local/share/Steam/steamapps/common/wallpaper_engine/assets" "${2:-{\}}" "$root/devtest/scene-data.js"
