# Release procedure

## Windows portable

1. Build `windows-portable` with MSVC x64 and the pinned vcpkg manifest.
2. Run `tools/release/package-portable-windows.ps1`.
3. The verifier audits the PE import table with `dumpbin /DEPENDENTS` and rejects non-system runtime DLLs.
4. The distribution directory contains `notepadFasaFiso.exe`, the project `LICENSE`, `PRIVACY.md`, `THIRD_PARTY_NOTICES.txt`, and `third_party/licenses/`.
5. Smoke-test the EXE after copying the complete portable distribution directory to an otherwise empty location.
6. Rebuild the portable target from a clean `build/windows-portable` directory under the same pinned toolchain and verify that the EXE SHA-256 is identical. The portable linker uses `/Brepro`; a hash mismatch is a release failure.

No installer, administrator elevation, wx DLLs, Visual C++ Redistributable installation, or adjacent branding PNG is expected. The executable remains a single portable binary; the accompanying text files are distribution notices, not runtime dependencies.

## Linux AppImage

Build the `linux-appimage` preset on an older supported Linux baseline rather than the newest distro available. The release host must have Debian source repositories enabled because legal packaging downloads the exact corresponding source packages for bundled Debian shared libraries. Then provide local AppImage tools explicitly:

```bash
export LINUXDEPLOY=/path/to/linuxdeploy-x86_64.AppImage
export APPIMAGETOOL=/path/to/appimagetool-x86_64.AppImage
export APPIMAGE_RUNTIME=/path/to/runtime-x86_64
./tools/release/package-appimage.sh
```

The AppImage runtime is supplied locally so appimagetool cannot introduce an implicit runtime download. The release tooling accepts the pinned x86_64 runtime SHA-256 recorded in `DEPENDENCIES.md`; a different runtime is a release failure requiring a fresh provenance/license review.

After linuxdeploy builds the AppDir, packaging records the exact bundled Debian shared-library package set, copies the corresponding Debian copyright files plus `/usr/share/common-licenses` into the AppImage, records the source-package names and versions, embeds the project `LICENSE`, `PRIVACY.md`, `THIRD_PARTY_NOTICES.txt`, and pinned third-party notices, and uses the configured Debian source repositories to create `dist/notepadFasaFiso-appimage-corresponding-source.tar.gz`.

Packaging also creates `dist/notepadFasaFiso-appimage-runtime-source.tar.gz`. That archive is tied to the pinned runtime hash and contains the exact AppImage/type2-runtime source commit plus the libfuse and squashfuse source inputs used by the runtime's upstream static build. Packaging fails if the runtime hash differs, if a bundled shared library cannot be mapped to an installed Debian package, if its copyright notice is unavailable, or if its exact Debian source package cannot be downloaded.

Publish these three Linux artifacts together:

```text
notepadFasaFiso-x86_64.AppImage
notepadFasaFiso-appimage-corresponding-source.tar.gz
notepadFasaFiso-appimage-runtime-source.tar.gz
```

Verify the complete set with:

```bash
./tools/release/verify-appimage.sh
```

The verifier extracts the relocated AppImage, checks the embedded legal/privacy notices, proves that the Debian source archive inventory matches the AppImage inventory, verifies the runtime-source archive and its checksums/provenance, checks the packaged executable with `ldd`, then performs an Xvfb launch smoke test.

## Canonical source tree

On Windows, generate the canonical tree into a separate destination outside the working repository:

```powershell
.\tools\release\make-release-tree.ps1 -Destination C:\path\to\nff-release-tree
```

The generator is allowlist-based. It includes the project `LICENSE`, `THIRD_PARTY_NOTICES.txt`, `PRIVACY.md`, third-party license inputs, and legal source-collection tooling, and does not copy build output, `.git`, `.vs`, local artifacts, patch files, Wine/toolchain dumps, or caches. Run `verify-release-tree.ps1` after any maintenance change.

## Legal/compliance gate

Before publishing a binary distribution:

- ship the project `LICENSE`, `PRIVACY.md`, and `THIRD_PARTY_NOTICES.txt` with the Windows portable distribution;
- retain the complete Windows dependency notice set used by the pinned vcpkg closure;
- ensure the AppImage contains `LICENSE`, `PRIVACY.md`, `THIRD_PARTY_NOTICES.txt`, AppImage-runtime notices, exact Debian binary/source inventories, Debian package copyright files, and Debian common license texts;
- publish `notepadFasaFiso-appimage-corresponding-source.tar.gz` beside the AppImage so corresponding source for bundled Debian libraries remains available with the binary release;
- publish `notepadFasaFiso-appimage-runtime-source.tar.gz` beside the AppImage so the pinned type2 runtime and its statically linked libfuse source/relinking materials remain available with the binary release;
- do not replace full third-party license texts with pointer-only placeholders where the applicable license requires notice reproduction;
- retain `PRIVACY.md` as the description of the official release's data-handling behavior;
- if the dependency set, AppImage runtime, or Linux build baseline changes, rerun the legal inventory and review newly introduced licenses before publishing.

## Final gates

Before publishing: Debug/Release tests, strict compiler pass, sanitizers, Windows import/relocation smoke, Linux AppImage relocation smoke, canonical-tree fresh build/tests, security audit, legal/compliance gate, project `LICENSE` plus third-party notices, corresponding source for bundled copyleft libraries/runtime, and artifact SHA-256 recording.
