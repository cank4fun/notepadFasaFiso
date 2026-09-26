#!/usr/bin/env bash
set -euo pipefail

APPDIR="${1:?Usage: collect-appimage-licenses.sh <AppDir>}"
[[ -d "$APPDIR" ]] || { echo "AppDir not found: $APPDIR" >&2; exit 2; }
command -v dpkg-query >/dev/null || { echo "Missing required tool: dpkg-query" >&2; exit 2; }
command -v find >/dev/null || { echo "Missing required tool: find" >&2; exit 2; }
[[ -d /usr/share/common-licenses ]] || { echo "Missing Debian common license directory" >&2; exit 2; }

DOCROOT="$APPDIR/usr/share/doc/notepadFasaFiso"
COPYRIGHT_DIR="$DOCROOT/debian-package-copyrights"
COMMON_DIR="$DOCROOT/common-licenses"
INVENTORY="$DOCROOT/BUNDLED_DEBIAN_PACKAGES.txt"
SOURCE_INVENTORY="$DOCROOT/BUNDLED_DEBIAN_SOURCES.txt"

rm -rf "$COPYRIGHT_DIR" "$COMMON_DIR"
mkdir -p "$COPYRIGHT_DIR" "$COMMON_DIR"
cp -a /usr/share/common-licenses/. "$COMMON_DIR/"

declare -A packages=()
declare -A sources=()
declare -a unresolved=()

mapfile -d '' bundled_libraries < <(find "$APPDIR" -type f \( -name '*.so' -o -name '*.so.*' \) -print0 | sort -z)
if [[ ${#bundled_libraries[@]} -eq 0 ]]; then
    echo "No bundled shared libraries found in AppDir: $APPDIR" >&2
    exit 3
fi

for bundled in "${bundled_libraries[@]}"; do
    base="$(basename "$bundled")"
    owner_line="$(dpkg-query -S "*/$base" 2>/dev/null | head -n 1 || true)"
    package="${owner_line%%:*}"
    if [[ -z "$package" || "$package" == "$owner_line" ]]; then
        unresolved+=("$bundled")
        continue
    fi
    packages["$package"]=1
done

if [[ ${#unresolved[@]} -ne 0 ]]; then
    echo "Could not map bundled shared libraries to Debian packages:" >&2
    printf '  %s\n' "${unresolved[@]}" >&2
    exit 4
fi

{
    echo "notepadFasaFiso Linux AppImage bundled Debian packages"
    echo "Generated from the packaged shared-library closure."
    echo
    printf '%-40s %s\n' "Package" "Version"
    printf '%-40s %s\n' "-------" "-------"
} > "$INVENTORY"

mapfile -t sorted_packages < <(printf '%s\n' "${!packages[@]}" | LC_ALL=C sort)
for package in "${sorted_packages[@]}"; do
    version="$(dpkg-query -W -f='${Version}' "$package" 2>/dev/null || true)"
    source_package="$(dpkg-query -W -f='${source:Package}' "$package" 2>/dev/null || true)"
    source_version="$(dpkg-query -W -f='${source:Version}' "$package" 2>/dev/null || true)"
    [[ -n "$version" ]] || { echo "Could not determine Debian package version: $package" >&2; exit 5; }
    [[ -n "$source_package" ]] || source_package="$package"
    [[ -n "$source_version" ]] || source_version="$version"

    copyright="/usr/share/doc/$package/copyright"
    if [[ ! -f "$copyright" ]]; then
        copyright="/usr/share/doc/${package%%:*}/copyright"
    fi
    [[ -f "$copyright" ]] || { echo "Missing Debian copyright file for bundled package: $package" >&2; exit 6; }

    cp -L "$copyright" "$COPYRIGHT_DIR/${package//:/_}.copyright"
    printf '%-40s %s\n' "$package" "$version" >> "$INVENTORY"
    source_key="${source_package}"$'\t'"${source_version}"
    sources["$source_key"]=1
done

{
    echo "notepadFasaFiso Linux AppImage corresponding Debian source packages"
    echo "SourcePackage<TAB>SourceVersion"
    printf '%s\n' "${!sources[@]}" | LC_ALL=C sort
} > "$SOURCE_INVENTORY"

[[ -s "$INVENTORY" ]] || { echo "Bundled package inventory was not created" >&2; exit 7; }
[[ -s "$SOURCE_INVENTORY" ]] || { echo "Bundled source inventory was not created" >&2; exit 7; }
[[ -n "$(find "$COPYRIGHT_DIR" -type f -print -quit)" ]] || { echo "No Debian package copyright files were collected" >&2; exit 7; }

echo "AppImage legal inventory collected for ${#sorted_packages[@]} Debian packages."
