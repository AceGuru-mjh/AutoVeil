#!/usr/bin/env bash
# NexusProp Editor — 本地打包脚本
# 输出 dist/nexus_nexus_prop_editor_<VERSION>.zip

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_DIR="$SCRIPT_DIR"
DIST_DIR="$SCRIPT_DIR/../dist"
VERSION="1.0.0"
MODULE_ID="nexus_prop_editor"

mkdir -p "$DIST_DIR"

OUTPUT="$DIST_DIR/nexus_${MODULE_ID}_${VERSION}.zip"

cd "$MODULE_DIR"
zip -r "$OUTPUT" \
    manifest.json \
    customize.sh \
    post-fs-data.sh \
    service.sh \
    uninstall.sh \
    verify.sh \
    README.md 2>/dev/null || true

echo "打包完成: $OUTPUT"
echo "大小: $(du -h "$OUTPUT" | cut -f1)"
