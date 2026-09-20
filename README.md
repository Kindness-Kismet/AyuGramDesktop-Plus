# AyuGramDesktop-Plus

A customized Telegram Desktop client, based on
[re-zero001/AyuGramDesktop](https://github.com/re-zero001/AyuGramDesktop) and
maintained as a fast-moving fork that is designed to be developed rapidly by
AI coding assistants and humans alike.

## Acknowledgements

This project would not exist without the work of its upstream authors:

- **[re-zero001/AyuGramDesktop](https://github.com/re-zero001/AyuGramDesktop)** —
  the direct upstream of this repository. Thank you for maintaining and
  sharing your fork; it is the foundation everything here builds on.
- **[AyuGram](https://github.com/AyuGram/AyuGramDesktop)** — the AyuGram line
  of forks, whose customization work this project builds on.
- **[Telegram Desktop](https://github.com/telegramdesktop/tdesktop)** — the
  official client this whole family is built on.

## Features

- **Auto CJK–Latin spacing** — spaces are inserted automatically between
  CJK text and Latin words/digits when sending, with mentions, links,
  e-mail addresses and code blocks kept intact.
- **UI customizations** — wide-message multipliers, avatar corner radius,
  switch styles and more.
- **Bundled languages** — English and Simplified Chinese ship with the
  binary.

## AI-assisted development

This repository is set up so that an AI coding assistant (or a human) can go
from "change a line" to "verified running build" in minutes, without touching
a debugger UI. The moving parts:

### 1. A single agent-facing specification

[`AGENTS.md`](AGENTS.md) (mirrored as `CLAUDE.md`) is the one document an
assistant reads first: where every kind of change belongs, naming rules, the
rpl/reactive and threading conventions, build knowledge, and a review
checklist. Keeping it accurate is part of every change.

### 2. Skills (`.claude/skills/` and `.codex/skills/`, byte-identical)

Portable instruction packages that Claude Code, Codex CLI and other agent
harnesses pick up automatically:

| Skill | Purpose |
|---|---|
| `app-debug` | Drive the running Debug build: read/write settings, send real messages, take screenshots, click widgets, inject fake sessions — the core verification loop |
| `upstream-diff` | Track official Telegram Desktop updates, generate adaptation material (diffs, conflict previews, per-file lists), record the adapted baseline |
| `version-bump` | Verify the four version locations after adapting upstream, keep `upstream.json` bookkeeping in sync |
| `commit` | Atomic, scoped commits; path-by-path staging; no stray files |
| `pull-request` | PR structure, review checklist, scope control |

### 3. A debug server inside the app

Debug builds (`_DEBUG` only) listen on `127.0.0.1:20100` and answer one-line
JSON commands over TCP — no UI scripting, no screenshot OCR guessing:

- **Lifecycle** — start, restart and stop the app from the CLI; the build
  script stops running instances automatically before linking.
- **Settings** — list keys, dump all, read and write any setting; writes go
  through the same code path as the settings UI.
- **Messaging** — list loaded chats, send real text through the official
  sending pipeline (all send-time hooks apply), open any chat, inject
  local-only messages, or create a local fake session that bypasses login.
- **Observation** — screenshot the active window, list the widget tree,
  synthesize clicks that go through the real event dispatch.
- **Diagnostics** — crash logs.

### 4. Build tooling that keeps iteration fast

- `scripts/prebuild.py` — one-time build of the third-party stack
  (static Qt, OpenSSL, FFmpeg, …), cached and only re-run when dependencies
  change.
- `scripts/build.py` — configures and builds in one step, stops running
  instances of this project first (by absolute executable path, never by
  process name), and drops versioned output under `build/`.
- Incremental builds: a single `.cpp` change links in ~2 minutes.

### 5. The loop, end to end

```bash
# edit code, then:
python scripts/build.py --dev                       # build Debug
python .claude/skills/app-debug/scripts/cli.py app.ensure   # start & wait
python .claude/skills/app-debug/scripts/cli.py settings.get autoSpaceSending
python .claude/skills/app-debug/scripts/cli.py screenshot.take
python .claude/skills/app-debug/scripts/cli.py app.stop
# commit via the commit skill
```

An assistant can run this entire loop unattended — including visual
verification via screenshots — which is how every feature in this fork was
developed and tested.

## Building from source

### Windows

**Prerequisites**

- Visual Studio 2026 (any edition, including the free Community edition)
  with the "Desktop development with C++" workload installed — this
  includes the v145 toolset (MSVC 14.51), ATL/MFC headers, and the
  bundled CMake. Older Visual Studio versions are not supported.
- Windows 10 SDK 10.0.26100.0 or later (select it in the installer if
  it is not already present).
- Python 3.10 or later ([python.org](https://www.python.org/downloads/))
  and Git ([git-scm.com](https://git-scm.com/download/win)).
- Roughly 60 GB of free disk space (dependencies take about 10 GB,
  the rest covers build intermediates and the output).

**Steps**

```bash
git clone --recursive https://github.com/Kindness-Kismet/AyuGramDesktop-Plus
cd AyuGramDesktop-Plus

# One-time: build third-party dependencies (~30 stages, cached in build/tmp)
python scripts/prebuild.py
python scripts/prebuild.py --list    # see what it does

# Build the app
python scripts/build.py              # Release
python scripts/build.py --dev        # Debug (also collects the PDB)
python scripts/build.py --jobs 16    # more parallelism, see memory notes
```

Output lands in `build/AyuGram-v<version>-win-x64-{release|dev}/`. The
first three version components follow upstream Telegram Desktop; repeated
releases on the same upstream version append a fourth revision component.

Notes:

- Dependencies only need rebuilding when submodule pointers or
  `Telegram/build/version` change — day-to-day edits just run `build.py`.
- Builds default to the official public test API credentials. Pass
  `--api-id/--api-hash` to `build.py` to use your own.
- MSVC precompiled headers take roughly 0.5 GB of committed memory per
  compiler process; with 32 GB of RAM, `--jobs 8` is the sweet spot.
- The `cmake` submodule is the official `desktop-app/cmake_helpers`;
  project-specific patches are applied at configure time by
  `scripts/build_support/cmake_patch.py`, so a "modified content" marker on
  that submodule is expected.

### Linux (Docker, Rocky Linux 8)

The reference Linux build runs in the same container as CI, so the host
distro does not matter. Dependencies are compiled inside the image and
installed into `/usr/local`.

**Prerequisites**: Docker Engine 20+ ([install guide](https://docs.docker.com/engine/install/)),
Poetry ([install guide](https://python-poetry.org/docs/#installation)),
Python 3.8+, and about 60 GB of free disk space (the dependency image
alone takes about 20 GB).

```bash
git clone --recursive https://github.com/Kindness-Kismet/AyuGramDesktop-Plus
cd AyuGramDesktop-Plus
cd Telegram/build/docker/centos_env

# Generate a Release Dockerfile (DEBUG= and LTO= disable those flags)
poetry install
DEBUG= LTO= poetry run gen_dockerfile > Dockerfile

# Build the dependency image (first run: 1–2 hours)
docker build -t ayugram:centos_env .

cd ../../../..   # repository root

# Build the app inside the container
docker run --rm \
  -v "$PWD":/usr/src/tdesktop \
  -e CONFIG=Release \
  ayugram:centos_env \
  /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
  -D CMAKE_CONFIGURATION_TYPES=Release \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
```

The binary lands in `out/Release/AyuGram` (relative to the repository
root, inside the mounted volume). Incremental rebuilds re-run the same
`docker run` command — the container compiles into the mounted `out/`
directory, so only changed objects are rebuilt.

### macOS

Dependencies are built natively (Qt 6.11.2, Release configuration) into
`Libraries/` next to the checkout.

**Prerequisites**: Xcode 14 or later from the Mac App Store,
Homebrew ([brew.sh](https://brew.sh)), Python 3 (built into recent
macOS via `xcode-select --install`), and about 40 GB of free disk space.

```bash
brew install automake libtool meson nasm ninja pkg-config
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer

git clone --recursive https://github.com/Kindness-Kismet/AyuGramDesktop-Plus
cd AyuGramDesktop-Plus

# One-time: build dependencies (omitting 'silent' shows all output)
./Telegram/build/prepare/mac.sh silent

# Build the app
cd Telegram
./configure.sh \
  -D CMAKE_CONFIGURATION_TYPES=Release \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627 \
  -D CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
cmake --build ../out --config Release --parallel
```

The app bundle lands in `out/Release/AyuGram.app`. As on Windows,
dependency rebuilds are only needed when submodule pointers or
`Telegram/build/version` change.

On Windows, `build.py --pack` additionally produces
`build/AyuGram-v<version>-win-x64.zip` containing just the executables;
pass `--clean-pack` to first remove any runtime leftovers (tdata, logs)
from the output directory. Pull requests run the `Release` workflow's unit
tests, release-note checks, and actionlint validation without starting the
Windows, Linux, or macOS builds. A push to `main` dispatches the three platform
build repositories in parallel and publishes only artifacts tied to the exact
source commit and workflow runs. Each artifact carries a provenance manifest;
the release job verifies its source run, builder run, version, file set, size,
and hash before publishing. Manual runs also perform the complete
three-platform build; runs from `main` publish, while runs from other branches
only validate the build. Provenance is bound to the complete Release run
attempt, so retry with **Re-run all jobs** or start a new Release run; rerunning
only selected failed jobs cannot reuse artifacts from an earlier attempt.
Release notes are extracted from the matching version section in
`.github/CHANGELOG.md`; older version sections stay in the repository history
but are not copied into a new GitHub Release.

## Repository layout

```
scripts/            prebuild.py + build.py and their support package
Telegram/
  SourceFiles/ayu/  all fork features (one directory per feature under features/)
  lib_ui, lib_tl,   forked submodules (Kindness-Kismet/...) — other lib_* are
  codegen           upstream desktop-app modules, treated as read-only
.github/
  upstream.json     the adaptation baseline: which upstream commits are merged
AGENTS.md           the agent-facing specification (mirror: CLAUDE.md)
.claude/, .codex/   skill packages for AI assistants (skills only)
```

## License

Like its upstreams, this project is licensed under the
[GNU GPL v3](https://www.gnu.org/licenses/gpl-3.0.html) or later, as
Telegram Desktop and AyuGram are.
