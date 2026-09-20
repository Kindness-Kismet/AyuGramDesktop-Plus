import glob
import hashlib
import os
import re
from pathlib import Path

from build_support.dependency_env import environment_keys
from build_support.paths import LIBRARIES_ARCH_DIR, THIRD_PARTY_DIR
from build_support.recipe import Stage

KEYS_DIR_NAME = "cache_keys"

_LOCATIONS = {"Libraries": LIBRARIES_ARCH_DIR, "ThirdParty": THIRD_PARTY_DIR}


def stage_directory(stage: Stage) -> Path:
    try:
        return _LOCATIONS[stage.location]
    except KeyError:
        raise SystemExit(f"Unknown location: {stage.location}")


def key_path(stage: Stage) -> Path:
    return stage_directory(stage) / KEYS_DIR_NAME / stage.name


def compute_cache_key(stage: Stage) -> str:
    libraries_key, third_party_key = environment_keys()
    env_key = third_party_key if stage.location == "ThirdParty" else libraries_key

    objects = [env_key, stage.location, stage.name, stage.version, stage.commands]
    for pattern in stage.dependencies:
        matches = glob.glob(str(LIBRARIES_ARCH_DIR / pattern))
        if not matches:
            matches = glob.glob(str(THIRD_PARTY_DIR / pattern))
        if not matches:
            raise SystemExit(f"Nothing found: {pattern}")
        items = [pattern]
        for path in matches:
            items.append(_file_hash(Path(path)))
        objects.append(":".join(items))

    return hashlib.sha1(";".join(objects).encode("utf-8")).hexdigest()


def check_cache_key(stage: Stage, key: str) -> str:
    """返回 Good / Stale / NotFound。"""
    directory = stage_directory(stage)
    if not (directory / stage.name).exists():
        return "NotFound"

    path = key_path(stage)
    if not path.exists():
        return "Stale"
    if path.read_text(encoding="utf-8") != key:
        return "Stale"
    if stage.name.startswith("qt_6."):
        # Qt 6 的安装包引用独立对象文件，缓存键一致也不能复用被裁剪的安装目录。
        prefix = directory / ("Qt-" + stage.name.removeprefix("qt_"))
        objects = set()
        for target in (prefix / "lib/cmake").glob("**/*Targets-*.cmake"):
            objects.update(re.findall(
                r'\$\{_IMPORT_PREFIX\}/([^";]*objects-[^";]+\.obj)',
                target.read_text(encoding="utf-8"),
            ))
        if not objects or any(not (prefix / obj).is_file() for obj in objects):
            return "Stale"
    return "Good"


def clear_cache_key(stage: Stage) -> None:
    path = key_path(stage)
    if path.exists():
        path.unlink()


def write_cache_key(stage: Stage, key: str) -> None:
    path = key_path(stage)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(key, encoding="utf-8")


def ensure_key_directories() -> None:
    for directory in _LOCATIONS.values():
        (directory / KEYS_DIR_NAME).mkdir(parents=True, exist_ok=True)


def _file_hash(path: Path) -> str:
    if not path.exists():
        raise SystemExit(f"Not found: {path}")
    sha1 = hashlib.sha1()
    with path.open("rb") as handle:
        while True:
            data = handle.read(256 * 1024)
            if not data:
                break
            sha1.update(data)
    return sha1.hexdigest()
