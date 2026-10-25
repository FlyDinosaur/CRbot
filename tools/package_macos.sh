#!/usr/bin/env bash
# 构建并打包 macOS 版
#
# 用法：
#   tools/package_macos.sh                 # 构建 Release（build-release）并打包
#   tools/package_macos.sh --skip-build    # 只打包已有产物
#   BUILD_DIR=build tools/package_macos.sh --skip-build
#
# 产物：dist/CRBot-<版本>-macos-<arch>/ 和同名 .zip
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
ARCH="$(uname -m)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-release}"
SKIP_BUILD=0

for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=1 ;;
        --build-dir=*) BUILD_DIR="${arg#*=}" ;;
        -h|--help)
            sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "不认识的参数：$arg" >&2; exit 2 ;;
    esac
done

PKG_NAME="CRBot-$VERSION-macos-$ARCH"
PKG_DIR="$ROOT/dist/$PKG_NAME"

echo "==> CRBot $VERSION (Release, $ARCH)"
echo "    build dir: $BUILD_DIR"

if [[ $SKIP_BUILD -eq 0 ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"
else
    echo "==> 跳过构建，直接打包已有产物"
fi

BIN="$BUILD_DIR/CRbot"
[[ -x "$BIN" ]] || { echo "没有找到 $BIN（先构建，或用 BUILD_DIR 指到构建目录）" >&2; exit 1; }

# ad-hoc 签名：从浏览器下载的包会被 Gatekeeper 打上隔离属性，
# 没有签名时可能直接被拒绝运行；本地 ad-hoc 签名能少踩这个坑。
if command -v codesign >/dev/null 2>&1; then
    codesign --force --sign - "$BIN" >/dev/null 2>&1 || echo "    (codesign 跳过)"
fi

# ---- 打包 ----
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/assets"
cp "$BIN" "$PKG_DIR/"

# 程序按 <exe>/assets/templates 找模板
cp -R "$ROOT/assets/templates" "$PKG_DIR/assets/"
cp "$ROOT/assets/README.md" "$PKG_DIR/assets/"
cp "$ROOT/README.md" "$ROOT/CHANGELOG.md" "$ROOT/VERSION" "$PKG_DIR/"
# GPL-3.0 要求分发二进制时随附许可证与第三方声明
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$PKG_DIR/"
mkdir -p "$PKG_DIR/docs"
cp "$ROOT/docs/圣水识别方案.md" "$PKG_DIR/docs/"

ZIP="$PKG_DIR.zip"
rm -f "$ZIP"
(cd "$ROOT/dist" && zip -qry "$(basename "$ZIP")" "$PKG_NAME")

echo ""
echo "==> 完成"
echo "    目录:   $PKG_DIR"
echo "    压缩包: $ZIP"
echo "    运行:   cd \"$PKG_DIR\" && ./CRbot"
