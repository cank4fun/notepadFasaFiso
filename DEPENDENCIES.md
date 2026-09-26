# Dependency policy

Runtime architecture stays intentionally small: native C++23 + wxWidgets/Scintilla + operating-system APIs.

## Windows release dependency input

The release-only vcpkg manifest requests `wxwidgets` with default features disabled (`wxwidgets[core]` equivalent) and uses `x64-windows-static`. The builtin registry is pinned to vcpkg release `2026.07.29` commit:

```text
9e593bb18ea69cc5095e012465dcd675a822ed0d
```

The Windows release currently pins wxWidgets 3.3.1#1. Static linkage may bring transitive image/text codec libraries into the executable. `THIRD_PARTY_NOTICES.txt` indexes the release dependency set and the pinned license/notice texts are retained under `third_party/licenses/`; the Windows portable packaging step copies those notices alongside the executable.

Do not commit a `vcpkg_installed/` tree. Rebuild dependencies from the pinned manifest/registry when producing a release. A dependency or baseline change requires a fresh license review before publishing.

## Linux

Linux uses the distro wxGTK/GTK development stack at build time. AppImage packaging bundles the appropriate runtime closure with linuxdeploy rather than trying to statically link glibc/GTK.

Because that closure depends on the build host, `tools/release/collect-appimage-licenses.sh` inventories the finished AppDir, maps bundled shared libraries to their installed Debian packages, records exact binary and source package versions, copies each package's Debian copyright file, and copies `/usr/share/common-licenses`. `tools/release/collect-appimage-sources.sh` then downloads the exact recorded Debian source package artifacts and creates `dist/notepadFasaFiso-appimage-corresponding-source.tar.gz`.

The AppImage type2 runtime is separately pinned by binary hash rather than downloaded implicitly:

```text
runtime-x86_64 SHA-256: 1cc49bcf1e2ccd593c379adb17c9f85a36d619088296504de95b1d06215aebbf
AppImage/type2-runtime: 75849dce7cc37e4319b633df1f116ca895c71a12
libfuse source: 3.15.0, SHA-256 70589cfd5e1cff7ccd6ac91c86c01be340b227285c5e200baa284e401eea2ca0
squashfuse source: 0.5.2, SHA-256 db0238c5981dabbd80ee09ae15387f390091668ca060a7bc38047912491443d3
```

`tools/release/collect-appimage-runtime-sources.sh` verifies that runtime hash and creates `dist/notepadFasaFiso-appimage-runtime-source.tar.gz` containing the pinned type2-runtime tree and the exact libfuse/squashfuse source inputs used by its upstream build. The runtime's attribution notices are retained under `third_party/licenses/appimage-runtime/`.

The AppImage, Debian corresponding-source archive, and AppImage-runtime source archive form one Linux release set and must be published together. The verifier rejects missing or inconsistent source artifacts.

Use an older supported Debian baseline for release packaging, keep Debian source repositories enabled on the release host, and do not publish an AppImage if any bundled library cannot be mapped to an installed package with an available copyright notice or exact source package.

## Tooling policy

Release scripts run locally and do not depend on CI-specific tooling. AppImage tooling is supplied as explicit local inputs; hidden AppImage-runtime downloads are intentionally disabled by requiring `APPIMAGE_RUNTIME`. Legal source collection intentionally uses the configured Debian source repositories and pinned upstream source locations so corresponding source can be shipped with the Linux binary release.
