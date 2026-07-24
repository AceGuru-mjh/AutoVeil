#!/usr/bin/env bash
# 打包 NexusProp Editor 模块 ZIP
set -euo pipefail

MODULE_ID="nexus_prop_editor"
VERSION="1.0.0"
OUT_DIR="dist"
OUT="$OUT_DIR/nexus_${MODULE_ID}_${VERSION}.zip"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE_DIR="$SCRIPT_DIR"

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

TMP="$(mktemp -d)"
trap "rm -rf $TMP" EXIT

cp -r "$MODULE_DIR"/* "$TMP/" 2>/dev/null || true
rm -rf "$TMP/dist" "$TMP/build.sh"

if [ ! -f "$TMP/manifest.json" ]; then
    echo "! manifest.json missing" >&2
    exit 1
fi

chmod +x "$TMP"/*.sh 2>/dev/null || true

( cd "$TMP" && zip -r9 "$OLDPWD/$OUT" . -x "*.DS_Store" "*/.git*" )

echo "Built: $OUT"
unzip -l "$OUT"
