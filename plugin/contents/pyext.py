#!/bin/python3

import asyncio
import websockets
import json
import base64
import math
import os
import platform
import shutil

from pathlib import Path
from urllib.parse import unquote

from typing import Callable,Any,Optional

# import functools;



class Main:
    def __init__(self):
        self.config_dir: Path = self.__config_dir()
        self.config_wallpaper_dir: Path = self.config_dir / 'wallpaper'

        self.config_wallpaper_dir.mkdir(parents=True, exist_ok=True)

    def __config_dir(self) -> Path:
        config_name: str = "wekde"
        xdg_config_home: Optional[str] = os.getenv("XDG_CONFIG_HOME")
        if xdg_config_home:
            return Path(xdg_config_home) / config_name
        return Path.home() / ".config" / config_name
    def __wallpaper_config_file(self, id: str) -> Path:
        return self.config_wallpaper_dir / (id + '.json')

    def read_wallpaper_config(self, id: str) -> dict:
        cfg_file: Path = self.__wallpaper_config_file(id)
        if not cfg_file.exists():
            return dict()

        with open(cfg_file, "r") as f:
            return json.load(f)

    def write_wallpaper_config(self, id: str, changed: dict) -> None:
        cfg: dict = self.read_wallpaper_config(id)
        cfg.update(changed)
        cfg_file: Path = self.__wallpaper_config_file(id)

        with open(cfg_file, "w+") as f:
            json.dump(cfg, f)

    def reset_wallpaper_config(self, id: str) -> None:
        cfg_file: Path = self.__wallpaper_config_file(id)
        cfg_file.unlink()

    def delete_wallpaper_config(self, id: str) -> None:
        cfg_file: Path = self.__wallpaper_config_file(id)
        if cfg_file.exists():
            cfg_file.unlink()

class Jsonrpc:
    def __init__(self):
        self.method_map = dict()

    def add_method(self, func: Callable) -> Callable:
        self.method_map[func.__name__] = func
        return func

    def add_class_method(self, obj: Any, func: Callable) -> None:
        def wrapper(*args):
            func(obj, *args)
        self.method_map[func.__name__] = wrapper

    def handle(self, msg) -> str:
        j: dict = {}
        error = None
        try:
            j = json.loads(msg)
        except Exception as e:
            error = repr(e)
            return json.dumps({"id": -1, "error": error})

        result = {"id": j.get("id")}
        method = j.get("method")
        if method in self.method_map:
            func = self.method_map[method]
            params = j.get("params") or []
            try:
                result["result"] = func(*params)
            except Exception as e:
                error = repr(e)
        else:
            error = "jsonrpc no such func"
        if error:
            result["error"] = error
        return json.dumps(result)

M = Main()
jrpc = Jsonrpc()


@jrpc.add_method
def version() -> str:
    return platform.python_version()


@jrpc.add_method
def readfile(path: str) -> str:
    with open(path, "rb") as f:
        data: bytes = f.read()
        return base64.b64encode(data).decode("ascii")


@jrpc.add_method
def get_dir_size(path: str, depth: int) -> int:
    glob_strs: list[str] = (
        ["**/*"]
        if depth <= 0
        else ["/".join(["*" for _ in range(i + 1)]) for i in range(depth)]
    )
    root_directory: Path = Path(path)
    return sum(
        [
            sum(f.stat().st_size for f in root_directory.glob(s) if f.is_file())
            for s in glob_strs
        ]
    )


@jrpc.add_method
def get_folder_list(path: str, _opt: dict = {}) -> Optional[dict]:
    def gen_item(f: Path) -> dict:
        stat: os.stat_result = f.stat()
        return {"name": f.name, "mtime": math.floor(stat.st_mtime)}

    opt: dict = get_folder_list.default_opt.copy()
    opt.update(_opt)
    opt_only_dir = opt["only_dir"]

    def path_filter(p: Path) -> bool:
        return p.is_dir() if opt_only_dir else True

    folder: Optional[Path] = next(
        filter(lambda p: p.is_dir(), [Path(p) for p in [path, *opt["fallbacks"]]]), None
    )
    if folder is None:
        return None
    return {
        "folder": str(folder),
        "items": [gen_item(p) for p in folder.glob("*") if path_filter(p)],
    }
get_folder_list.default_opt = {"only_dir": True, "fallbacks": []}

jrpc.add_method(M.read_wallpaper_config)
jrpc.add_method(M.write_wallpaper_config)
jrpc.add_method(M.reset_wallpaper_config)


@jrpc.add_method
def delete_wallpaper(path: str, workshopid: str = "") -> dict:
    # Safety: require the target to be an existing directory containing
    # a project.json file so arbitrary paths can't be wiped out.
    try:
        folder: Path = Path(path).resolve()
        if not folder.is_dir():
            return {"ok": False, "error": "not a directory"}
        if not (folder / "project.json").is_file():
            return {"ok": False, "error": "not a wallpaper folder"}
        shutil.rmtree(folder)
        if workshopid:
            M.delete_wallpaper_config(workshopid)
        return {"ok": True}
    except Exception as e:
        return {"ok": False, "error": repr(e)}


class ScenePkg:
    """Reader for the PKGV archive that holds a scene's files."""

    def __init__(self, path: Path):
        self.data: bytes = path.read_bytes()
        self.entries: dict = {}
        pos = 0

        def read_str() -> str:
            nonlocal pos
            n = int.from_bytes(self.data[pos:pos + 4], "little")
            pos += 4
            s = self.data[pos:pos + n].decode("utf-8", errors="replace")
            pos += n
            return s

        read_str()  # version
        count = int.from_bytes(self.data[pos:pos + 4], "little")
        pos += 4
        table = []
        for _ in range(count):
            name = read_str()
            off = int.from_bytes(self.data[pos:pos + 4], "little")
            size = int.from_bytes(self.data[pos + 4:pos + 8], "little")
            pos += 8
            table.append((name, off, size))
        for name, off, size in table:
            self.entries[name] = (pos + off, size)

    def read(self, name: str) -> Optional[bytes]:
        if name not in self.entries:
            return None
        off, size = self.entries[name]
        return self.data[off:off + size]


def _scene_vec(value: Any, default: list) -> list:
    if isinstance(value, str):
        try:
            nums = [float(x) for x in value.split()]
            return nums + default[len(nums):]
        except ValueError:
            return default
    if isinstance(value, (int, float)):
        return [float(value)] * len(default)
    return default


def _resolve_user(value: Any, user_props: dict) -> Any:
    """Resolve a {"user": ..., "value": ...} binding against the user properties."""
    if not isinstance(value, dict):
        return value
    if "user" in value:
        user = value["user"]
        default = value.get("value")
        if isinstance(user, dict):
            name = user.get("name")
            if name not in user_props:
                return default
            if "condition" in user:
                return str(user_props[name]) == str(user["condition"])
            return user_props[name]
        return user_props.get(user, default)
    return value


@jrpc.add_method
def scene_text(source: str, assets: str) -> dict:
    """Collect a scene's text objects, their scripts and the user property values."""
    source = unquote(source.removeprefix("file://"))
    assets = unquote(assets.removeprefix("file://"))
    folder: Path = Path(source).parent
    pkg: Optional[ScenePkg] = ScenePkg(folder / "scene.pkg") if (folder / "scene.pkg").is_file() else None

    def read_file(name: str) -> Optional[bytes]:
        if (folder / name).is_file():
            return (folder / name).read_bytes()
        if pkg is not None:
            data = pkg.read(name)
            if data is not None:
                return data
        if assets and (Path(assets) / name).is_file():
            return (Path(assets) / name).read_bytes()
        return None

    scene_name: str = Path(source).name if Path(source).suffix == ".json" else "scene.json"
    scene_data = read_file(scene_name)
    if scene_data is None:
        return {"ortho": [1920, 1080], "texts": []}
    scene: dict = json.loads(scene_data)

    user_props: dict = {}
    try:
        project: dict = json.loads((folder / "project.json").read_text())
        for name, prop in (project.get("general", {}).get("properties") or {}).items():
            if isinstance(prop, dict) and "value" in prop:
                user_props[name] = prop["value"]
    except (OSError, ValueError):
        pass
    user_props.update(M.read_wallpaper_config(folder.name).get("user_properties", {}))

    ortho: dict = scene.get("general", {}).get("orthogonalprojection") or {}
    objects: dict = {o.get("id"): o for o in scene.get("objects", []) if isinstance(o, dict)}

    visible_cache: dict = {}

    def visible(obj_id: Any, depth: int = 0) -> bool:
        """Whether an object and all of its parents are visible."""
        if obj_id in visible_cache:
            return visible_cache[obj_id]
        obj = objects.get(obj_id)
        if obj is None or depth > 64:
            return True
        shown = _resolve_user(obj.get("visible", True), user_props)
        shown = shown.get("value", True) if isinstance(shown, dict) else bool(shown)
        if obj.get("parent") in objects:
            shown = shown and visible(obj.get("parent"), depth + 1)
        visible_cache[obj_id] = shown
        return shown

    def scripted(value: Any) -> dict:
        """Split a property into its static value and optional script."""
        value = _resolve_user(value, user_props)
        if isinstance(value, dict) and "script" in value:
            return {
                "value": _resolve_user(value.get("value"), user_props),
                "script": value["script"],
                "scriptproperties": {
                    k: _resolve_user(v, user_props)
                    for k, v in (value.get("scriptproperties") or {}).items()
                },
            }
        if isinstance(value, dict):
            value = value.get("value")
        return {"value": value}

    # The renderer draws text objects; this is what their scripts need.
    texts: list = []
    for obj in scene.get("objects", []):
        if not isinstance(obj, dict) or "text" not in obj:
            continue
        text = scripted(obj.get("text"))
        if not isinstance(text.get("value"), str):
            text["value"] = "" if text.get("value") is None else str(text["value"])
        texts.append({
            "id": obj.get("id"),
            "name": obj.get("name", ""),
            "visible": visible(obj.get("id")),
            "text": text,
        })

    return {
        "ortho": [float(ortho.get("width", 1920)), float(ortho.get("height", 1080))],
        "userProperties": user_props,
        "texts": texts,
    }


async def connect(uri):
    async with websockets.connect(uri) as websocket:
        while True:
            recv: str = jrpc.handle(await websocket.recv())
            await websocket.send(recv)

if __name__ == "__main__":
    import argparse

    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="qml localfile helper"
    )
    parser.add_argument("url", metavar="URL", type=str, help="a websocket url")
    args: dict = vars(parser.parse_args())

    if hasattr(asyncio, "run"):
        asyncio.run(connect(args["url"]))
    else:
        asyncio.get_event_loop().run_until_complete(connect(args["url"]))
