#!/usr/bin/env bash
# 构建并打包 macOS 版
#
# 用法：
#   tools/package_macos.sh                 # 构建 Release（build-release）并打包
#   tools/package_macos.sh --skip-build    # 只打包已有产物
#   BUILD_DIR=build tools/package_macos.sh --skip-build
#
#   --channel=stable                   # 正式版本不显示 Build 号
# 产物：dist/CRBot-<版本>(<Build>)-macos-<arch>/ 和同名 .zip
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCH="$(uname -m)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-release}"
SKIP_BUILD=0
BUILD_NUMBER=AUTO
CHANNEL=prerelease

for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=1 ;;
        --build-dir=*) BUILD_DIR="${arg#*=}" ;;
        --build-number=*) BUILD_NUMBER="${arg#*=}" ;;
        --channel=*) CHANNEL="${arg#*=}" ;;
        -h|--help)
            sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "不认识的参数：$arg" >&2; exit 2 ;;
    esac
done

echo "==> CRBot (Release, $ARCH), build dir: $BUILD_DIR"

if [[ $SKIP_BUILD -eq 0 ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
        -DCRBOT_BUILD_NUMBER="$BUILD_NUMBER" -DCRBOT_RELEASE_CHANNEL="$CHANNEL"
    cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"
else
    echo "==> 跳过构建，直接打包已有产物"
fi

# Name packages from the metadata of the actual build, including --skip-build.
METADATA="$BUILD_DIR/build-info.json"
[[ -f "$METADATA" ]] || { echo "没有构建元数据 $METADATA，请先重新构建" >&2; exit 1; }
VERSION="$(plutil -extract version raw -o - "$METADATA")"
BUILD_NUMBER="$(plutil -extract build_number raw -o - "$METADATA")"
CHANNEL="$(plutil -extract channel raw -o - "$METADATA")"
DISPLAY_VERSION="$(plutil -extract display_version raw -o - "$METADATA")"
[[ "$VERSION" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ && "$BUILD_NUMBER" =~ ^[1-9][0-9]*$ ]] || { echo "非法版本元数据" >&2; exit 1; }
case "$CHANNEL" in
    prerelease) EXPECTED_VERSION="$VERSION($BUILD_NUMBER)" ;;
    stable) EXPECTED_VERSION="$VERSION" ;;
    *) echo "非法发布通道：$CHANNEL" >&2; exit 1 ;;
esac
[[ "$DISPLAY_VERSION" == "$EXPECTED_VERSION" ]] || { echo "构建版本不一致" >&2; exit 1; }
PKG_NAME="CRBot-$DISPLAY_VERSION-macos-$ARCH"
PKG_DIR="$ROOT/dist/$PKG_NAME"

BIN="$BUILD_DIR/CRbot"
APP="$BUILD_DIR/CRBot.app"
[[ -x "$BIN" ]] || { echo "没有找到 $BIN（先构建，或用 BUILD_DIR 指到构建目录）" >&2; exit 1; }
[[ -x "$APP/Contents/MacOS/CRBot" ]] || { echo "没有找到 $APP（构建时须开启 CRBOT_BUILD_MACOS_GUI）" >&2; exit 1; }

# ad-hoc 签名：从浏览器下载的包会被 Gatekeeper 打上隔离属性，
# 没有签名时可能直接被拒绝运行；本地 ad-hoc 签名能少踩这个坑。
if command -v codesign >/dev/null 2>&1; then
    codesign --force --sign - "$BIN"
    codesign --force --sign - "$APP"
    codesign --verify --strict "$APP"
fi

# ---- 打包 ----
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/assets"
cp "$BIN" "$PKG_DIR/"
cp -R "$APP" "$PKG_DIR/"

# 程序按 <exe>/assets/templates 找模板
cp -R "$ROOT/assets/templates" "$PKG_DIR/assets/"
cp "$ROOT/assets/README.md" "$PKG_DIR/assets/"
cp "$ROOT/README.md" "$ROOT/CHANGELOG.md" "$METADATA" "$PKG_DIR/"
printf '%s\n' "$VERSION" > "$PKG_DIR/VERSION"
# GPL-3.0 要求分发二进制时随附许可证与第三方声明
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$PKG_DIR/"
mkdir -p "$PKG_DIR/docs"
cp "$ROOT/docs/圣水识别方案.md" "$PKG_DIR/docs/"

ZIP="$PKG_DIR.zip"
rm -f "$ZIP"
(cd "$ROOT/dist" && zip -qry "$(basename "$ZIP")" "$PKG_NAME")
(cd "$ROOT/dist" && shasum -a 256 "$(basename "$ZIP")" > "$(basename "$ZIP").sha256")

echo ""
echo "==> 完成"
echo "    目录:   $PKG_DIR"
echo "    压缩包: $ZIP"
echo "    GUI:    open \"$PKG_DIR/CRBot.app\""
echo "    终端版: cd \"$PKG_DIR\" && ./CRbot"
