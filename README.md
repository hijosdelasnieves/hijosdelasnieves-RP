# hijosdelasnieves-RP — SkyMP fork (Hijos de las Nieves)

> **This is the source-code mirror of the SkyrimPlatform build used by the Hijos de las Nieves private server.**
> It is published here to comply with the GPLv3 / AGPLv3 licenses of the upstream project.

## What this repo contains

This is a fork of [skyrim-multiplayer/skymp](https://github.com/skyrim-multiplayer/skymp).

The only modification made relative to upstream is in `skyrim-platform/src/tilted/ui/MyChromiumApp.cpp`:
a set of CEF command-line flags that allow `getUserMedia()` (microphone access) to work inside the
Chromium overlay without prompting the user, enabling the in-game voice chat (LiveKit integration).

The branch `hdn/build-skyrim-platform` contains this change along with the GitHub Actions workflow
used to build and publish `SkyrimPlatform.dll` as a GitHub Release.

## What this repo does NOT contain

The gameplay logic, quest scripts, economy systems, world configuration, and all other content
specific to the Hijos de las Nieves server are maintained in separate private repositories and
are not part of this codebase.

## License compliance

| Subproject | License | Why published here |
|---|---|---|
| `skyrim-platform` | GPLv3 | We distribute a compiled `SkyrimPlatform.dll` — source must be available |
| `skymp5-server` | AGPLv3 | Network service — source must be available to users |
| `skymp5-client` | GPLv3 | Distributed to players |
| `skymp5-scripts` | GPLv3 | Distributed to players |
| `skymp5-front` | GPLv3 | Distributed to players |
| `skymp5-functions-lib` | MIT | Included for completeness |

See [TERMS.md](TERMS.md) for the upstream project terms and [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES) for third-party licenses.

---

# SkyMP (upstream)

[![Discord Chat](https://img.shields.io/discord/699653182946803722?label=Discord&logo=Discord)](https://discord.gg/k39uQ9Yudt) 
[![PR's Welcome](https://img.shields.io/badge/PRs%20-welcome-brightgreen.svg)](CONTRIBUTING.md)
[![Players](https://skymp-badges.vercel.app/badges/players_online.svg)](https://discord.gg/k39uQ9Yudt) 
[![Servers](https://skymp-badges.vercel.app/badges/servers_online.svg)](https://discord.gg/k39uQ9Yudt)

SkyMP is an open-source multiplayer mod for Skyrim ⚡

SkyMP is built on top of the [SkyrimPlatform](docs/docs_skyrim_platform.md) - a tool to create Skyrim mods with TypeScript and Chromium. 🚀

This repo hosts all sources to ease local setup and contributing. See [CONTRIBUTING](CONTRIBUTING.md) for build instructions.

### Terms of Use

See [TERMS.md](TERMS.md). TL;DR disclose the source code of your forks.

Third-party code licenses can be found in [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES).

### Development with GitHub Codespaces

[![Create Codespace](https://img.shields.io/badge/Codespace-Launch-blue?logo=github)](https://github.com/codespaces/new?repo=skyrim-multiplayer/skymp&ref=main)
