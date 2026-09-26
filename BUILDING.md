# Building notepadFasaFiso

## Common requirements

- CMake 3.24+
- Ninja
- C++23 compiler

## Core development builds

```text
cmake --preset dev
cmake --build --preset dev
ctest --preset dev

cmake --preset release
cmake --build --preset release
ctest --preset release
```

These presets do not require wxWidgets because the desktop GUI is optional.

## Windows GUI development

Existing `gui-dev` / `gui-release` behavior is preserved. Point CMake at the wxWidgets installation/vcpkg tree you already use, then configure the relevant preset.

## Windows portable release

Use MSVC x64, set `VCPKG_ROOT` to a vcpkg checkout containing the pinned registry history, then:

```powershell
cmake --preset windows-portable
cmake --build --preset windows-portable
.\tools\release\package-portable-windows.ps1
```

The preset uses `x64-windows-static`, static MSVC runtime (`/MT`), and the release-only manifest under `packaging/vcpkg/`. Normal development presets do not opt into that manifest.

## Linux GUI/AppImage build

On Debian 12/13, install the native build prerequisites:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config libwxgtk3.2-dev libgtk-3-dev patchelf file xvfb xauth
```

Then configure and build:

```bash
cmake --preset linux-appimage
cmake --build --preset linux-appimage
```

AppImage packaging requires local copies of linuxdeploy, appimagetool, and an x86_64 AppImage runtime; see `RELEASING.md`.
