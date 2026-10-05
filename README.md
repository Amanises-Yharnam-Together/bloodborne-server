# bloodborne-server

Seamless co-op for Bloodborne (CUSA03173, patch 01.09) on shadPS4, modeled on Elden Ring Seamless Co-op.

Built on two upstream forks:

| Component | Upstream | Role |
|---|---|---|
| Emulator | [Wozzardman/shadp2p](https://github.com/Wozzardman/shadp2p) | shadPS4 with Bloodborne P2P and the native game hooks (`src/core/bloodborne_re.cpp`) |
| Server | [Wozzardman/shadnet-p2p](https://github.com/Wozzardman/shadnet-p2p) | accounts, matchmaking, signaling, WebAPI and the Bloodborne summon broker |

The plan and milestones are in [docs/SEAMLESS_COOP_PLAN.md](docs/SEAMLESS_COOP_PLAN.md).

## Dev setup (Windows)

Prerequisites: VS 2022 Build Tools (C++ workload, Windows SDK), LLVM (clang-cl), CMake, Ninja, jj,
Qt 6.8.3 `msvc2022_64` in `C:\Qt` (via `aqt`), and Python 3.12 with `clang-format==19.1.7` and `reuse`.

Checkouts live under `BB_ROOT` (default `C:\bb`):

```powershell
jj git clone --colocate https://github.com/Wozzardman/shadp2p.git C:\bb\shadp2p
jj git clone --colocate https://github.com/Wozzardman/shadnet-p2p.git C:\bb\shadnet-p2p
git -C C:\bb\shadp2p submodule update --init --recursive
git -C C:\bb\shadnet-p2p submodule update --init --recursive
```

| Script | What it does |
|---|---|
| `tools/env.ps1` | dot-source it to enter an MSVC x64 dev shell with LLVM and Qt on PATH |
| `tools/build-emu.ps1` | configures and builds shadp2p (`RelWithDebInfo`, tests on) |
| `tools/ci-check.ps1 -Repo <name> [-All]` | runs the upstream CI checks locally: clang-format 19, trailing whitespace, REUSE, build, ctest |

## Workflow

- Version control is jj.
- Red-green: write the failing test, see it fail, then make it pass.
- Run `tools/ci-check.ps1` and get a green result before pushing.
