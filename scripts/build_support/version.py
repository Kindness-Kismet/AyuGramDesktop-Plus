import re
import sys
from dataclasses import dataclass
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from build_support.paths import ROOT, VERSION_FILE

_PATTERN = re.compile(r"^\s*(\d+)\.(\d+)(\.(\d+)(\.(\d+|beta))?)?\s*$")

_CORE_VERSION = ROOT / "Telegram" / "SourceFiles" / "core" / "version.h"
_TELEGRAM_RC = ROOT / "Telegram" / "Resources" / "winrc" / "Telegram.rc"
_UPDATER_RC = ROOT / "Telegram" / "Resources" / "winrc" / "Updater.rc"
_APPX_MANIFEST = ROOT / "Telegram" / "Resources" / "uwp" / "AppX" / "AppxManifest.xml"
_CHANGELOG = ROOT / "changelog.txt"


@dataclass(frozen=True)
class Version:
    original: str
    major: int
    minor: int
    patch: int
    alpha: int
    beta: bool

    @property
    def full(self) -> int:
        return self.major * 1000000 + self.minor * 1000 + self.patch

    @property
    def full_alpha(self) -> int:
        return self.full * 1000 + self.alpha if self.alpha else 0

    @property
    def text(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"

    @property
    def text_small(self) -> str:
        return self.text if self.patch else f"{self.major}.{self.minor}"

    @property
    def channel(self) -> str:
        if self.beta:
            return "beta"
        return "closed alpha" if self.alpha else "stable"


def parse_version(text: str) -> Version:
    match = _PATTERN.match(text)
    if not match:
        raise SystemExit(f"Bad version '{text}'. Expected major.minor[.patch[.alpha|beta]].")

    parts = [match.group(1), match.group(2), match.group(4) or "0"]
    suffix = match.group(6) or ""
    beta = suffix == "beta"
    alpha = "0" if beta or not suffix else suffix

    for part in parts + [alpha]:
        # 每段限制在 0..999，否则合成的整数版本会串位
        if str(int(part) % 1000) != part:
            raise SystemExit(f"Bad version part: {part}")

    return Version(
        original=text.strip(),
        major=int(parts[0]),
        minor=int(parts[1]),
        patch=int(parts[2]),
        alpha=int(alpha),
        beta=beta,
    )


def read_current_version() -> str:
    if not VERSION_FILE.is_file():
        raise SystemExit(f"{VERSION_FILE} not found.")
    for line in VERSION_FILE.read_text(encoding="utf-8").splitlines():
        match = re.match(r"\s*AppVersionOriginal\s+(\S+)", line)
        if match:
            return match.group(1)
    raise SystemExit(f"AppVersionOriginal not found in {VERSION_FILE}.")


def apply_version(version: Version, check_changelog: bool = True) -> list[str]:
    if check_changelog:
        _check_changelog(version)

    comma = ",".join(str(p) for p in (version.major, version.minor, version.patch, version.alpha))
    dot = ".".join(str(p) for p in (version.major, version.minor, version.patch, version.alpha))
    rc_rules = [
        (r"(FILEVERSION\s+)\d+,\d+,\d+,\d+", r"\g<1>" + comma),
        (r"(PRODUCTVERSION\s+)\d+,\d+,\d+,\d+", r"\g<1>" + comma),
        (r'("FileVersion",\s+)"\d+\.\d+\.\d+\.\d+"', r"\g<1>" + f'"{dot}"'),
        (r'("ProductVersion",\s+)"\d+\.\d+\.\d+\.\d+"', r"\g<1>" + f'"{dot}"'),
    ]

    touched: list[str] = []
    touched += _replace(VERSION_FILE, [
        (r"(AppVersion\s+)\d+", r"\g<1>" + str(version.full)),
        (r"(AppVersionStrMajor\s+)\d[\d\.]*", r"\g<1>" + f"{version.major}.{version.minor}"),
        (r"(AppVersionStrSmall\s+)\d[\d\.]*", r"\g<1>" + version.text_small),
        (r"(AppVersionStr\s+)\d[\d\.]*", r"\g<1>" + version.text),
        (r"(BetaChannel\s+)\d", r"\g<1>" + ("1" if version.beta else "0")),
        (r"(AlphaVersion\s+)\d+", r"\g<1>" + str(version.full_alpha)),
        (r"(AppVersionOriginal\s+)\d[\d\.beta]*", r"\g<1>" + version.original),
    ])
    touched += _replace(_CORE_VERSION, [
        (r"(TDESKTOP_REQUESTED_ALPHA_VERSION\s+)\(\d+ULL\)", r"\g<1>" + f"({version.full_alpha}ULL)"),
        (r"(AppVersion\s+=\s+)\d+", r"\g<1>" + str(version.full)),
        (r"(AppVersionStr\s+=\s+)[^;]+", r"\g<1>" + f'"{version.text_small}"'),
        (r"(AppBetaVersion\s+=\s+)[a-z]+", r"\g<1>" + ("true" if version.beta else "false")),
    ])
    touched += _replace(_TELEGRAM_RC, rc_rules)
    touched += _replace(_UPDATER_RC, rc_rules)
    touched += _replace(_APPX_MANIFEST, [(r'( Version=)"\d+\.\d+\.\d+\.\d+"', r"\g<1>" + f'"{dot}"')])
    return touched


def _check_changelog(version: Version) -> None:
    if not _CHANGELOG.is_file():
        raise SystemExit(f"{_CHANGELOG} not found.")
    prefixes = (f"{version.text} ", f"{version.text_small} ")
    count = sum(1 for line in _CHANGELOG.read_text(encoding="utf-8").splitlines() if line.startswith(prefixes))
    if count == 0:
        raise SystemExit(f"Changelog entry for {version.text} not found.")
    if count > 1:
        raise SystemExit(f"Found {count} changelog entries for {version.text}, expected one.")


def _replace(path: Path, rules: list[tuple[str, str]]) -> list[str]:
    if not path.is_file():
        raise SystemExit(f"{path} not found.")

    original = path.read_text(encoding="utf-8")
    content = original
    for pattern, replacement in rules:
        if not re.search(pattern, content):
            raise SystemExit(f'Could not find "{pattern}" in {path}.')
        content = re.sub(pattern, replacement, content)

    if content == original:
        return []
    path.write_text(content, encoding="utf-8")
    return [str(path.relative_to(ROOT))]


def main(argv: list[str] | None = None) -> None:
    import argparse

    from build_support.console import header, print_summary
    from build_support.help import MultilineHelpFormatter

    parser = argparse.ArgumentParser(
        prog="python -m build_support.version",
        description="Show or set the AyuGram version across build files",
        formatter_class=MultilineHelpFormatter,
    )
    parser.add_argument(
        "version",
        nargs="?",
        metavar="VERSION",
        help="New version as major.minor[.patch[.alpha|beta]]; omit to show the current one",
    )
    parser.add_argument(
        "--skip-changelog",
        action="store_true",
        help="Do not require a matching changelog.txt entry",
    )
    args = parser.parse_args(argv)

    if not args.version:
        current = parse_version(read_current_version())
        print(header("Current version"))
        print(f"  Version        {current.original}")
        print(f"  Channel        {current.channel}")
        print(f"  Numeric        {current.full}")
        return

    version = parse_version(args.version)
    print(header(f"Setting version {version.text} {version.channel}"))
    touched = apply_version(version, check_changelog=not args.skip_changelog)
    print_summary(*(f"patched {path}" for path in touched) if touched else ["already up to date"])


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    main()
