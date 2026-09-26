#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE="${1:-$ROOT/dist/notepadFasaFiso-x86_64.AppImage}"
SOURCE_BUNDLE="${2:-$ROOT/dist/notepadFasaFiso-appimage-corresponding-source.tar.gz}"
RUNTIME_SOURCE_BUNDLE="${3:-$ROOT/dist/notepadFasaFiso-appimage-runtime-source.tar.gz}"
SMOKE_SECONDS="${NFF_APPIMAGE_SMOKE_SECONDS:-10}"
RUNTIME_SHA256="1cc49bcf1e2ccd593c379adb17c9f85a36d619088296504de95b1d06215aebbf"
RUNTIME_COMMIT="75849dce7cc37e4319b633df1f116ca895c71a12"

[[ -f "$IMAGE" ]] || { echo "AppImage not found: $IMAGE" >&2; exit 2; }
[[ -x "$IMAGE" ]] || { echo "AppImage is not executable: $IMAGE" >&2; exit 2; }
[[ -f "$SOURCE_BUNDLE" ]] || { echo "AppImage corresponding-source.tar.gz not found: $SOURCE_BUNDLE" >&2; exit 2; }
[[ -f "$RUNTIME_SOURCE_BUNDLE" ]] || { echo "AppImage runtime-source.tar.gz not found: $RUNTIME_SOURCE_BUNDLE" >&2; exit 2; }
command -v file >/dev/null || { echo "Missing required tool: file" >&2; exit 2; }
command -v sha256sum >/dev/null || { echo "Missing required tool: sha256sum" >&2; exit 2; }
command -v ldd >/dev/null || { echo "Missing required tool: ldd" >&2; exit 2; }
command -v timeout >/dev/null || { echo "Missing required tool: timeout" >&2; exit 2; }
command -v xvfb-run >/dev/null || { echo "Missing required tool: xvfb-run" >&2; exit 2; }
command -v tar >/dev/null || { echo "Missing required tool: tar" >&2; exit 2; }

case "$(file -b "$IMAGE")" in
  *ELF*64-bit*x86-64*|*ELF*64-bit*x86_64*) ;;
  *) echo "Unexpected AppImage executable format: $(file -b "$IMAGE")" >&2; exit 3 ;;
esac

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cp "$IMAGE" "$work/notepadFasaFiso-x86_64.AppImage"
chmod +x "$work/notepadFasaFiso-x86_64.AppImage"
(
  cd "$work"
  ./notepadFasaFiso-x86_64.AppImage --appimage-extract >/dev/null
)
inner="$work/squashfs-root/usr/bin/notepadFasaFiso"
[[ -x "$inner" ]] || { echo "Packaged executable missing from AppImage" >&2; exit 4; }

DOCROOT="$work/squashfs-root/usr/share/doc/notepadFasaFiso"
[[ -f "$DOCROOT/LICENSE" ]] || { echo "Project LICENSE missing from AppImage" >&2; exit 4; }
[[ -f "$DOCROOT/PRIVACY.md" ]] || { echo "PRIVACY.md missing from AppImage" >&2; exit 4; }
[[ -f "$DOCROOT/THIRD_PARTY_NOTICES.txt" ]] || { echo "THIRD_PARTY_NOTICES.txt missing from AppImage" >&2; exit 4; }
[[ -s "$DOCROOT/BUNDLED_DEBIAN_PACKAGES.txt" ]] || { echo "Bundled Debian package inventory missing from AppImage" >&2; exit 4; }
[[ -s "$DOCROOT/BUNDLED_DEBIAN_SOURCES.txt" ]] || { echo "Bundled Debian source inventory missing from AppImage" >&2; exit 4; }
[[ -f "$DOCROOT/third-party-licenses/pcre2.txt" ]] || { echo "Pinned third-party license set missing from AppImage" >&2; exit 4; }
[[ -f "$DOCROOT/third-party-licenses/appimage-runtime/LICENSE.txt" ]] || { echo "AppImage runtime license missing from AppImage" >&2; exit 4; }
[[ -n "$(find "$DOCROOT/debian-package-copyrights" -type f -print -quit 2>/dev/null)" ]] || { echo "Debian package copyright notices missing from AppImage" >&2; exit 4; }
[[ -f "$DOCROOT/common-licenses/LGPL-2.1" ]] || { echo "LGPL-2.1 license text missing from AppImage" >&2; exit 4; }

tar -tzf "$SOURCE_BUNDLE" > "$work/source-bundle.list"
grep -Fxq 'README.txt' "$work/source-bundle.list" || { echo "Corresponding-source bundle README missing" >&2; exit 4; }
grep -Fxq 'BUNDLED_DEBIAN_SOURCES.txt' "$work/source-bundle.list" || { echo "Corresponding-source inventory missing" >&2; exit 4; }
grep -Eq '^source-packages/.+\.dsc$' "$work/source-bundle.list" || { echo "Corresponding-source bundle contains no Debian .dsc files" >&2; exit 4; }
tar -xOf "$SOURCE_BUNDLE" BUNDLED_DEBIAN_SOURCES.txt > "$work/source-bundle.inventory"
cmp -s "$DOCROOT/BUNDLED_DEBIAN_SOURCES.txt" "$work/source-bundle.inventory" || { echo "AppImage and corresponding-source inventories do not match" >&2; exit 4; }

runtime_source_dir="$work/runtime-source"
mkdir -p "$runtime_source_dir"
tar -xzf "$RUNTIME_SOURCE_BUNDLE" -C "$runtime_source_dir"
[[ -f "$runtime_source_dir/README.txt" ]] || { echo "AppImage runtime source README missing" >&2; exit 4; }
[[ -f "$runtime_source_dir/SHA256SUMS.txt" ]] || { echo "AppImage runtime source checksum manifest missing" >&2; exit 4; }
[[ -f "$runtime_source_dir/sources/type2-runtime-$RUNTIME_COMMIT.tar.gz" ]] || { echo "AppImage type2 runtime source archive missing" >&2; exit 4; }
[[ -f "$runtime_source_dir/sources/fuse-3.15.0.tar.xz" ]] || { echo "AppImage libfuse source archive missing" >&2; exit 4; }
[[ -f "$runtime_source_dir/sources/squashfuse-0.5.2.tar.gz" ]] || { echo "AppImage squashfuse source archive missing" >&2; exit 4; }
grep -Fq "$RUNTIME_SHA256" "$runtime_source_dir/README.txt" || { echo "AppImage runtime source bundle does not identify the pinned runtime SHA-256" >&2; exit 4; }
grep -Fq "$RUNTIME_COMMIT" "$runtime_source_dir/README.txt" || { echo "AppImage runtime source bundle does not identify the pinned runtime commit" >&2; exit 4; }
(
  cd "$runtime_source_dir/sources"
  sha256sum -c ../SHA256SUMS.txt >/dev/null
)

tar -tzf "$runtime_source_dir/sources/type2-runtime-$RUNTIME_COMMIT.tar.gz" > "$work/runtime-source.list"
grep -Fqx "type2-runtime-$RUNTIME_COMMIT/patches/libfuse/mount.c.diff" "$work/runtime-source.list" || {
  echo "AppImage runtime source archive is missing its libfuse patch" >&2
  exit 4
}

if ldd "$inner" | grep -F 'not found'; then
  echo "AppImage executable has unresolved runtime dependencies" >&2
  exit 5
fi

set +e
xvfb-run -a env APPIMAGE_EXTRACT_AND_RUN=1 timeout --signal=TERM "${SMOKE_SECONDS}s" "$work/notepadFasaFiso-x86_64.AppImage" >/dev/null 2>"$work/smoke.err"
status=$?
set -e
if [[ $status -ne 124 && $status -ne 143 ]]; then
  cat "$work/smoke.err" >&2
  echo "AppImage relocation smoke exited unexpectedly with status $status" >&2
  exit 6
fi

sha256sum "$IMAGE" "$SOURCE_BUNDLE" "$RUNTIME_SOURCE_BUNDLE"
echo "AppImage dependency, legal-notice, corresponding-source, runtime-source, and relocation smoke verified."
