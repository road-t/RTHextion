#!/usr/bin/env bash
# Build RTHextion and package it into a self-contained .dmg
# Usage: ./package_macos.sh [qt_bin_dir]
# Example: ./package_macos.sh /opt/homebrew/opt/qt@5/bin
#          ./package_macos.sh ~/Qt/6.8.3/macos/bin

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── Find Qt ────────────────────────────────────────────────────────
if [[ -n "$1" ]]; then
    QT_BIN="$1"
elif [[ -x /opt/homebrew/opt/qt/bin/qmake ]]; then
    QT_BIN=/opt/homebrew/opt/qt/bin          # Homebrew Qt 6
elif [[ -x /opt/homebrew/opt/qt@5/bin/qmake ]]; then
    QT_BIN=/opt/homebrew/opt/qt@5/bin        # Homebrew Qt 5
else
    echo "ERROR: Qt not found. Pass the Qt bin directory as argument."
    echo "  Example: $0 ~/Qt/6.8.3/macos/bin"
    exit 1
fi

echo "Using Qt from: $QT_BIN"
"$QT_BIN/qmake" --version

# ── Build ───────────────────────────────────────────────────────────
BUILD_DIR="$SCRIPT_DIR/build_package"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

"$QT_BIN/qmake" ../src/rthextion.pro CONFIG+=release
make -j"$(sysctl -n hw.logicalcpu)"

APP="$BUILD_DIR/rthextion.app"
echo "Built: $APP"

# ── Deploy Qt libraries ─────────────────────────────────────────────
"$QT_BIN/macdeployqt" "$APP" -dmg

DMG="$BUILD_DIR/rthextion.dmg"
FINAL="$SCRIPT_DIR/RTHextion-macOS-$(uname -m).dmg"
mv "$DMG" "$FINAL"

echo ""
echo "✓ Done! Output: $FINAL"
