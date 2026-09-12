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

## Building from source (Windows)

**Prerequisites**

- Visual Studio 2026 with the v145 toolset (MSVC 14.51)
- Windows 10 SDK (10.0.26100.0 or later)
- Python 3.10+, Git

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
version number follows upstream Telegram Desktop.

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
