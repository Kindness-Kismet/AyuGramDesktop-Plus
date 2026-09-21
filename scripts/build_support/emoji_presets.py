import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from build_support.paths import ROOT, TMP_DIR

STAGE_NAME = "emoji_presets"
DIRECTORY_NAME = "emoji-presets"
MANIFEST = ROOT / "scripts/emoji_presets.json"
CACHE = TMP_DIR / "Resources" / DIRECTORY_NAME


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def prepare_presets(cache: Path = CACHE, manifest: Path = MANIFEST) -> list[Path]:
    definition = json.loads(manifest.read_text(encoding="utf-8"))
    cache.mkdir(parents=True, exist_ok=True)
    prepared = []
    sources = []
    for preset in definition["presets"]:
        target = cache / preset["file"]
        if "system_font" in preset:
            source = Path(os.environ["WINDIR"]) / "Fonts" / preset["system_font"]
            digest = file_hash(source)
            if not target.is_file() or file_hash(target) != digest:
                shutil.copy2(source, target)
            source_label = "Windows 系统字体：" + preset["system_font"]
        else:
            digest = preset["sha256"]
            if not target.is_file() or file_hash(target) != digest:
                partial = target.with_suffix(".download")
                subprocess.run(["curl", "--fail", "--location", "--retry", "2",
                                "--connect-timeout", "30", "--max-time", "300",
                                "--silent", "--show-error", "--output", str(partial),
                                preset["url"]], check=True)
                if file_hash(partial) != digest:
                    raise SystemExit(f"表情字体校验失败：{preset['name']}")
                partial.replace(target)
            source_label = preset["url"]
        prepared.append({"id": preset["id"], "name": preset["name"],
                         "file": target.name, "sha256": digest})
        sources.append(f"{preset['name']}\n{source_label}\nSHA-256: {digest}\n")
        print(f"  {preset['name']}: {target.stat().st_size:,} bytes", flush=True)
    (cache / "presets.json").write_text(
        json.dumps({"presets": prepared}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (cache / "SOURCES.txt").write_text("\n".join(sources), encoding="utf-8")
    return prepared_files(cache, manifest)


def prepared_files(cache: Path = CACHE, manifest: Path = MANIFEST) -> list[Path]:
    index = cache / "presets.json"
    if not index.is_file():
        raise SystemExit("缺少预设表情包，请先运行 python scripts/prebuild.py --stage emoji_presets")
    wanted = json.loads(manifest.read_text(encoding="utf-8"))["presets"]
    prepared = json.loads(index.read_text(encoding="utf-8"))["presets"]
    if [p["id"] for p in wanted] != [p["id"] for p in prepared]:
        raise SystemExit("预设表情包清单已变化，请重新运行预构建。")
    result = [index, cache / "SOURCES.txt"]
    for definition, preset in zip(wanted, prepared):
        path = cache / definition["file"]
        if preset["file"] != definition["file"] or preset["name"] != definition["name"]:
            raise SystemExit("预设表情包清单已变化，请重新运行预构建。")
        digest = definition.get("sha256", preset["sha256"])
        if preset["sha256"] != digest or not path.is_file() or file_hash(path) != digest:
            raise SystemExit(f"预设表情包缺失或损坏：{definition['name']}，请重新运行预构建。")
        result.append(path)
    if not result[1].is_file():
        raise SystemExit("缺少表情字体来源说明，请重新运行预构建。")
    return result
