#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APPDIR="${1:?Usage: collect-appimage-sources.sh <AppDir> [output.tar.gz]}"
OUTPUT="${2:-$ROOT/dist/notepadFasaFiso-appimage-corresponding-source.tar.gz}"
DOCROOT="$APPDIR/usr/share/doc/notepadFasaFiso"
INVENTORY="$DOCROOT/BUNDLED_DEBIAN_SOURCES.txt"

[[ -d "$APPDIR" ]] || { echo "AppDir not found: $APPDIR" >&2; exit 2; }
[[ -s "$INVENTORY" ]] || { echo "Bundled Debian source inventory not found: $INVENTORY" >&2; exit 2; }
command -v apt-get >/dev/null || { echo "Missing required tool: apt-get" >&2; exit 2; }
command -v tar >/dev/null || { echo "Missing required tool: tar" >&2; exit 2; }
command -v sha256sum >/dev/null || { echo "Missing required tool: sha256sum" >&2; exit 2; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
source_dir="$work/source-packages"
mkdir -p "$source_dir" "$(dirname "$OUTPUT")"
cp "$INVENTORY" "$work/BUNDLED_DEBIAN_SOURCES.txt"

cat > "$work/README.txt" <<'README'
notepadFasaFiso Linux AppImage corresponding source bundle
==========================================================

This archive accompanies the official AppImage binary distribution. It contains
the Debian source-package files for the Debian-built shared libraries bundled
inside that AppImage. The exact source package names and versions are recorded
in BUNDLED_DEBIAN_SOURCES.txt.

The files in source-packages/ are the source artifacts retrieved by `apt-get
source --download-only` from the configured Debian source repositories on the
release build host. Their own copyright and license terms apply.

The notepadFasaFiso application source is distributed separately in the
canonical project source release under the project's MIT License.
README

mapfile -t source_rows < <(tail -n +3 "$INVENTORY" | sed '/^[[:space:]]*$/d')
if [[ ${#source_rows[@]} -eq 0 ]]; then
    echo "No corresponding Debian source packages were recorded" >&2
    exit 3
fi

for row in "${source_rows[@]}"; do
    IFS=$'\t' read -r source_package source_version extra <<< "$row"
    if [[ -z "$source_package" || -z "$source_version" || -n "${extra:-}" ]]; then
        echo "Malformed Debian source inventory row: $row" >&2
        exit 4
    fi

    before="$(find "$source_dir" -maxdepth 1 -type f -name '*.dsc' | wc -l)"
    (
        cd "$source_dir"
        apt-get source --download-only "$source_package=$source_version"
    )
    after="$(find "$source_dir" -maxdepth 1 -type f -name '*.dsc' | wc -l)"
    if [[ "$after" -le "$before" ]]; then
        echo "No Debian .dsc source descriptor downloaded for $source_package=$source_version" >&2
        exit 5
    fi
done

[[ -n "$(find "$source_dir" -maxdepth 1 -type f -name '*.dsc' -print -quit)" ]] || {
    echo "Corresponding source bundle contains no Debian source descriptors" >&2
    exit 6
}

rm -f "$OUTPUT"
tar -czf "$OUTPUT" -C "$work" README.txt BUNDLED_DEBIAN_SOURCES.txt source-packages
sha256sum "$OUTPUT"
echo "AppImage corresponding source bundle created: $OUTPUT"
