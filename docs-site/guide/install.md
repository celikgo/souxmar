# Install souxmar

souxmar ships as signed installers for macOS, Windows, and Linux,
plus a Python library on PyPI and a source tarball.

## Quick path

| Platform              | Recommended                                      |
| --------------------- | ------------------------------------------------ |
| macOS (arm64 / x86_64) | Download the `.dmg` from the [latest release](https://github.com/souxmar/souxmar/releases/latest) |
| Windows (x86_64)      | Download the `.zip` from the [latest release](https://github.com/souxmar/souxmar/releases/latest); EV-signed |
| Linux (x86_64)        | Download the `.tar.gz` from the [latest release](https://github.com/souxmar/souxmar/releases/latest); GPG-detached signature alongside |
| Python (any OS)       | `pip install pysouxmar` (Sprint 4+ landed; current version matches the release) |

All installers are signed; see the [updates guide](/guide/updates)
for the trust chain.

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

## Verifying the download

Every release artefact is signed. Before running, verify:

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
guide](/plugins/first-plugin) for authoring.

## Building from source

If you'd rather build:

```sh
git clone https://github.com/souxmar/souxmar.git
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
