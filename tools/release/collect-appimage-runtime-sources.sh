#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUNTIME="${1:?Usage: collect-appimage-runtime-sources.sh <runtime-x86_64> [output.tar.gz]}"
OUTPUT="${2:-$ROOT/dist/notepadFasaFiso-appimage-runtime-source.tar.gz}"

RUNTIME_SHA256="1cc49bcf1e2ccd593c379adb17c9f85a36d619088296504de95b1d06215aebbf"
RUNTIME_COMMIT="75849dce7cc37e4319b633df1f116ca895c71a12"
LIBFUSE_VERSION="3.15.0"
LIBFUSE_SHA256="70589cfd5e1cff7ccd6ac91c86c01be340b227285c5e200baa284e401eea2ca0"
SQUASHFUSE_VERSION="0.5.2"
SQUASHFUSE_SHA256="db0238c5981dabbd80ee09ae15387f390091668ca060a7bc38047912491443d3"

[[ -f "$RUNTIME" ]] || { echo "AppImage runtime not found: $RUNTIME" >&2; exit 2; }
command -v git >/dev/null || { echo "Missing required tool: git" >&2; exit 2; }
command -v curl >/dev/null || { echo "Missing required tool: curl" >&2; exit 2; }
command -v sha256sum >/dev/null || { echo "Missing required tool: sha256sum" >&2; exit 2; }
command -v tar >/dev/null || { echo "Missing required tool: tar" >&2; exit 2; }

actual_runtime_sha="$(sha256sum "$RUNTIME" | awk '{print $1}')"
if [[ "$actual_runtime_sha" != "$RUNTIME_SHA256" ]]; then
    echo "Unexpected AppImage runtime SHA-256" >&2
    echo "Expected: $RUNTIME_SHA256" >&2
    echo "Actual:   $actual_runtime_sha" >&2
    exit 3
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$(dirname "$OUTPUT")" "$work/sources"

runtime_repo="$work/type2-runtime"
git init -q "$runtime_repo"
git -C "$runtime_repo" remote add origin https://github.com/AppImage/type2-runtime.git
git -C "$runtime_repo" fetch -q --depth 1 origin "$RUNTIME_COMMIT"
git -C "$runtime_repo" checkout -q --detach FETCH_HEAD
actual_commit="$(git -C "$runtime_repo" rev-parse HEAD)"
if [[ "$actual_commit" != "$RUNTIME_COMMIT" ]]; then
    echo "Unexpected AppImage runtime source commit: $actual_commit" >&2
    exit 4
fi

git -C "$runtime_repo" archive \
    --format=tar.gz \
    --prefix="type2-runtime-$RUNTIME_COMMIT/" \
    -o "$work/sources/type2-runtime-$RUNTIME_COMMIT.tar.gz" \
    HEAD

runtime_archive_list="$work/type2-runtime.list"
tar -tzf "$work/sources/type2-runtime-$RUNTIME_COMMIT.tar.gz" > "$runtime_archive_list"
grep -Fqx "type2-runtime-$RUNTIME_COMMIT/patches/libfuse/mount.c.diff" "$runtime_archive_list" || {
    echo "AppImage runtime source archive is missing the libfuse patch" >&2
    exit 5
}

curl -fL \
    "https://github.com/libfuse/libfuse/releases/download/fuse-$LIBFUSE_VERSION/fuse-$LIBFUSE_VERSION.tar.xz" \
    -o "$work/sources/fuse-$LIBFUSE_VERSION.tar.xz"
echo "$LIBFUSE_SHA256  $work/sources/fuse-$LIBFUSE_VERSION.tar.xz" | sha256sum -c - >/dev/null

curl -fL \
    "https://github.com/vasi/squashfuse/archive/$SQUASHFUSE_VERSION.tar.gz" \
    -o "$work/sources/squashfuse-$SQUASHFUSE_VERSION.tar.gz"
echo "$SQUASHFUSE_SHA256  $work/sources/squashfuse-$SQUASHFUSE_VERSION.tar.gz" | sha256sum -c - >/dev/null

cat > "$work/README.txt" <<README
notepadFasaFiso AppImage runtime corresponding source
=====================================================

The official Linux AppImage is prefixed with the AppImage type2 runtime whose
exact x86_64 binary SHA-256 is:

  $RUNTIME_SHA256

That binary is the AppImage/type2-runtime continuous release built from commit:

  $RUNTIME_COMMIT

The runtime is statically linked. Its upstream build at this commit uses
libfuse $LIBFUSE_VERSION and squashfuse $SQUASHFUSE_VERSION from the exact
source archives included here, together with the patch and build scripts in
the type2-runtime source archive. The runtime also links permissively licensed
musl, zstd, zlib and mimalloc components supplied by its Alpine build image.
Their attribution/license material is shipped inside the notepadFasaFiso
AppImage under usr/share/doc/notepadFasaFiso/third-party-licenses/.

This bundle is provided alongside the AppImage so the source and relinking
materials for the statically linked LGPL libfuse portion remain available with
the distributed runtime.
README

(
    cd "$work/sources"
    sha256sum * > "$work/SHA256SUMS.txt"
)

rm -f "$OUTPUT"
tar -czf "$OUTPUT" -C "$work" README.txt SHA256SUMS.txt sources
sha256sum "$OUTPUT"
echo "AppImage runtime source bundle created: $OUTPUT"
