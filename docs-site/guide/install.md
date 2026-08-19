# Install souxmar

::: warning No binary distribution yet
`v0.9.0` is souxmar's first tagged release and it ships **source only**.
There are no signed installers, no `.dmg`, no `.zip`, no `.tar.gz`, and
**`pysouxmar` is not on PyPI** — `pip install pysouxmar` does not work.
Build from source; it is the only path that works today.
:::

## Build from source

Prerequisites: CMake ≥ 3.25, Ninja, a C++20 compiler (GCC 13 / Clang 17 /
AppleClang / MSVC 19.36+), and [vcpkg](https://github.com/microsoft/vcpkg)
cloned with `VCPKG_ROOT` exported.

On macOS, vcpkg builds `libsodium` through autotools, so you also need
`brew install autoconf autoconf-archive automake libtool`. Without them the
first `cmake --preset dev` fails inside the vcpkg port build rather than in
souxmar's own configure, which makes the cause easy to misread.

```sh
git clone https://github.com/celikgo/souxmar.git
cd souxmar
export VCPKG_ROOT="$HOME/vcpkg"

cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

The CLI lands at `build/dev/src/cli/souxmar` and the example plugins at
`build/dev/examples/plugins`. Run your first pipeline:

```sh
cd examples/stl-cube
../../build/dev/src/cli/souxmar run pipeline.yaml \
  --plugin-path ../../build/dev/examples/plugins
```

The first `cmake --preset` builds vcpkg dependencies from source — roughly
five minutes for the default feature set, longer with the heavy adapters
(OpenCASCADE, Gmsh, DOLFINx, OpenFOAM) enabled.

### Python bindings

Also source-only, via the `dev-python` preset:

```sh
cmake --preset dev-python
cmake --build --preset dev-python
```

## What is planned, and not yet real

The release pipeline in `.github/workflows/release.yml` is written to produce
signed artefacts for all three platforms — Apple notarisation, EV
Authenticode, and a detached GPG signature — and it degrades to a loud
"unsigned" warning when the credentials are absent. **Those credentials do
not exist yet**, so no signed artefact has ever been published. The trust
chain is described in
[SECURITY.md](https://github.com/celikgo/souxmar/blob/master/SECURITY.md);
treat it as design, not as something you can verify against a download today.

## System requirements

| Resource              | Minimum             | Recommended          |
| --------------------- | ------------------- | -------------------- |
| OS — macOS            | 13 (Ventura)        | 14 (Sonoma)+         |
| OS — Windows          | 10 22H2 / Server 2022 | 11                 |
| OS — Linux            | Ubuntu 22.04 / Fedora 39 | Ubuntu 24.04    |
| CPU                   | x86_64 / aarch64    | 4+ cores             |
| RAM                   | 8 GB                | 16 GB+ for ≥ 1M-cell meshes |
| Disk                  | 1 GB free           | 10 GB free for caches + sample projects |
| GPU                   | (CLI / Python: none) | WebGL2-capable for desktop viewport |

The desktop app's viewport benefits from a GPU but degrades
gracefully to software rendering. The mesh + solve pipeline is
CPU-bound; GPU acceleration is a Sprint 18+ exploration.

## Verifying a download

::: warning Not yet applicable
No signed artefact has been published. The commands below are what the
verification steps *will* be once release signing is live — they are kept
here so the trust chain is reviewable, not because there is anything to run
them against today.
:::

Every release artefact is intended to be signed. Before running, verify:

::: code-group

```sh [macOS]
# Apple Gatekeeper does this automatically on first launch.
# To verify manually:
codesign -dv --verbose=4 /Applications/souxmar.app
```

```sh [Linux]
# Detached GPG signature lives alongside the .tar.gz:
gpg --verify souxmar-0.9.0-linux-x86_64.tar.gz.asc \
            souxmar-0.9.0-linux-x86_64.tar.gz
```

```powershell [Windows]
# EV Authenticode — Windows verifies automatically.
# To inspect manually:
Get-AuthenticodeSignature .\souxmar.exe
```

:::

## Plugin search path

souxmar looks for plugins under:

| OS     | Default path                                            |
| ------ | ------------------------------------------------------- |
| macOS  | `~/Library/Application Support/souxmar/plugins`         |
| Linux  | `$XDG_DATA_HOME/souxmar/plugins` (or `~/.local/share/souxmar/plugins`) |
| Windows | `%APPDATA%\souxmar\plugins`                            |

Drop a plugin directory there; the next `souxmar plugin list` /
desktop-app restart sees it. See [the plugins
guide](https://github.com/celikgo/souxmar/blob/master/docs/PLUGIN_SDK.md) for authoring.

## Building from source

If you'd rather build:

```sh
git clone https://github.com/celikgo/souxmar.git
cd souxmar

# vcpkg-managed deps
git clone --depth 1 https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg

cmake --preset dev
cmake --build --preset dev

# The CLI is now at build/dev/src/cli/souxmar
build/dev/src/cli/souxmar version
```

See `CONTRIBUTING.md` in the repo for the full toolchain matrix.

## Uninstall

::: code-group

```sh [macOS]
# Just delete the app + the data directory:
rm -rf /Applications/souxmar.app
rm -rf ~/Library/Application\ Support/souxmar
rm -rf ~/Library/Caches/souxmar
```

```sh [Linux]
# Per the package manager:
sudo apt remove souxmar          # .deb
sudo dnf remove souxmar          # .rpm
# Plus the data + cache directories:
rm -rf ~/.local/share/souxmar
rm -rf ~/.cache/souxmar
```

```powershell [Windows]
# Settings → Apps → souxmar → Uninstall, then:
Remove-Item -Recurse "$env:APPDATA\souxmar"
Remove-Item -Recurse "$env:LOCALAPPDATA\souxmar"
```

:::

The auto-updater's per-user state file
(`update-state.toml`) lives under those data directories; uninstall
removes it cleanly.
