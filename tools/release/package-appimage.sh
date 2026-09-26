#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/linux-appimage}"
OUTPUT="${OUTPUT:-$ROOT/dist/notepadFasaFiso-x86_64.AppImage}"
SOURCE_OUTPUT="${SOURCE_OUTPUT:-$ROOT/dist/notepadFasaFiso-appimage-corresponding-source.tar.gz}"
RUNTIME_SOURCE_OUTPUT="${RUNTIME_SOURCE_OUTPUT:-$ROOT/dist/notepadFasaFiso-appimage-runtime-source.tar.gz}"
: "${LINUXDEPLOY:?Set LINUXDEPLOY to a local linuxdeploy-x86_64.AppImage or executable}"
: "${APPIMAGETOOL:?Set APPIMAGETOOL to a local appimagetool-x86_64.AppImage or executable}"
: "${APPIMAGE_RUNTIME:?Set APPIMAGE_RUNTIME to a local x86_64 AppImage runtime file}"
for f in \
  "$LINUXDEPLOY" \
  "$APPIMAGETOOL" \
  "$APPIMAGE_RUNTIME" \
  "$BUILD_DIR/notepadFasaFiso_gui" \
  "$ROOT/LICENSE" \
  "$ROOT/PRIVACY.md" \
  "$ROOT/THIRD_PARTY_NOTICES.txt" \
  "$ROOT/tools/release/collect-appimage-licenses.sh" \
  "$ROOT/tools/release/collect-appimage-sources.sh" \
  "$ROOT/tools/release/collect-appimage-runtime-sources.sh"; do
  [[ -f "$f" ]] || { echo "Missing required file: $f" >&2; exit 2; }
done
[[ -d "$ROOT/third_party/licenses" ]] || { echo "Missing third-party license directory" >&2; exit 2; }

APPDIR="$BUILD_DIR/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps" "$(dirname "$OUTPUT")" "$(dirname "$SOURCE_OUTPUT")" "$(dirname "$RUNTIME_SOURCE_OUTPUT")"
install -m 0755 "$BUILD_DIR/notepadFasaFiso_gui" "$APPDIR/usr/bin/notepadFasaFiso"
install -m 0644 "$ROOT/assets/nff-icon-256.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/notepadFasaFiso.png"
cat > "$APPDIR/notepadFasaFiso.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=notepadFasaFiso
Exec=notepadFasaFiso %F
Icon=notepadFasaFiso
Terminal=false
Categories=Utility;TextEditor;
MimeType=text/plain;
DESKTOP
cp "$APPDIR/notepadFasaFiso.desktop" "$APPDIR/usr/share/applications/notepadFasaFiso.desktop"
cp "$ROOT/assets/nff-icon-256.png" "$APPDIR/notepadFasaFiso.png"
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/usr/bin/env bash
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/usr/bin/notepadFasaFiso" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

export APPIMAGE_EXTRACT_AND_RUN=1
"$LINUXDEPLOY" --appdir "$APPDIR" --executable "$APPDIR/usr/bin/notepadFasaFiso" --desktop-file "$APPDIR/notepadFasaFiso.desktop" --icon-file "$APPDIR/notepadFasaFiso.png"

DOCROOT="$APPDIR/usr/share/doc/notepadFasaFiso"
mkdir -p "$DOCROOT/third-party-licenses"
install -m 0644 "$ROOT/LICENSE" "$DOCROOT/LICENSE"
install -m 0644 "$ROOT/PRIVACY.md" "$DOCROOT/PRIVACY.md"
install -m 0644 "$ROOT/THIRD_PARTY_NOTICES.txt" "$DOCROOT/THIRD_PARTY_NOTICES.txt"
cp -a "$ROOT/third_party/licenses/." "$DOCROOT/third-party-licenses/"
bash "$ROOT/tools/release/collect-appimage-licenses.sh" "$APPDIR"
bash "$ROOT/tools/release/collect-appimage-sources.sh" "$APPDIR" "$SOURCE_OUTPUT"
bash "$ROOT/tools/release/collect-appimage-runtime-sources.sh" "$APPIMAGE_RUNTIME" "$RUNTIME_SOURCE_OUTPUT"

ARCH=x86_64 "$APPIMAGETOOL" --runtime-file "$APPIMAGE_RUNTIME" "$APPDIR" "$OUTPUT"
chmod +x "$OUTPUT"
sha256sum "$OUTPUT" "$SOURCE_OUTPUT" "$RUNTIME_SOURCE_OUTPUT"
