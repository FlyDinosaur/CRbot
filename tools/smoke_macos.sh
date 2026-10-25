#!/usr/bin/env bash
# macOS 冒烟测试：不接真机，用一份假 adb 跑通
#   启动 → 找 adb → 拉起 server → 设备发现/去重 → PNG 截屏解码 → 分辨率校验 → 终端面板
# 用法：tools/smoke_macos.sh [build 目录]（默认 build）
set -euo pipefail

BUILD_DIR="${1:-build}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/$BUILD_DIR"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[[ -x "$BIN/CRbot" ]] || fail "找不到 $BIN/CRbot，先 cmake --build $BUILD_DIR"
[[ -x "$BIN/ui_probe" ]] || fail "找不到 $BIN/ui_probe"

# 1. 造一张 720x1280 的假截图（纯 Python 标准库，不依赖 PIL）
python3 - "$TMP/screen720.png" <<'PY'
import struct, sys, zlib
w, h = 720, 1280
rows = bytearray()
for y in range(h):
    rows.append(0)  # filter: none
    for x in range(w):
        v = 40 if (300 <= x < 420 and 500 <= y < 700) else 20
        rows += bytes((v, v, v, 255))
chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(bytes(rows), 6))
png += chunk(b'IEND', b'')
open(sys.argv[1], 'wb').write(png)
PY

# 2. 假 adb：模仿 CRBot 用到的命令
cat > "$TMP/fake_adb" <<'ADB'
#!/bin/bash
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
MARK="__CRBOT_DONE_7f3a__"
args=("$@")
idx=0
if [[ ${#args[@]} -ge 2 && "${args[0]}" == "-s" ]]; then idx=2; fi
cmd="${args[$idx]:-}"

respond() {
    case "$*" in
        *"getprop ro.product.model"*) echo "Fake Phone 720" ;;
        *"getprop ro.serialno"*) echo "FAKE123" ;;
        *"settings get secure android_id"*) echo "fakeandroidid" ;;
        *"pm list packages"*) echo "package:com.tencent.tmgp.supercell.clashroyale" ;;
        *"pidof"*) echo "4242" ;;
        *) : ;;
    esac
}

case "$cmd" in
    start-server) exit 0 ;;
    devices) printf 'List of devices attached\nfake-1\tdevice\n\n' ;;
    connect)
        target="${args[$((idx + 1))]:-}"
        if [[ "$target" == "127.0.0.1:5555" ]]; then echo "connected to $target"
        else echo "failed to connect to '$target': Connection refused"; fi
        ;;
    exec-out)
        if [[ "${args[$((idx + 1))]:-}" == "screencap" ]]; then
            if [[ "${args[$((idx + 2))]:-}" == "-p" ]]; then cat "$HERE/screen720.png"
            else python3 -c 'import sys,struct; sys.stdout.buffer.write(struct.pack("<III",720,1280,1)+b"\0"*(720*1280*4))'
            fi
        fi
        ;;
    shell)
        if [[ $((idx + 1)) -ge ${#args[@]} ]]; then
            while IFS= read -r line; do respond "$line"; echo "$MARK"; done
        else
            respond "${args[*]:$((idx + 1))}"
        fi
        ;;
esac
exit 0
ADB
chmod +x "$TMP/fake_adb"

# 3. 跑主程序，2 秒后把终端面板的一帧导出成纯文本
CRBOT_ADB="$TMP/fake_adb" CRBOT_UI_DUMP="$TMP/ui.txt" \
    "$BIN/CRbot" </dev/null >/dev/null 2>&1 || fail "CRbot 退出码非 0"

grep -q "fake-1" "$TMP/ui.txt" || fail "面板里没有设备行"
grep -q "720x1280" "$TMP/ui.txt" || fail "截图解码 / 分辨率校验没通过"
grep -q "s=Stop" "$TMP/ui.txt" || fail "面板底部按键提示缺失"
grep -q "\[DEVICE\] fake-1" "$TMP/ui.txt" || fail "日志没有进面板"

# 4. 离线模板匹配（PNG 解码 + NCC）
"$BIN/ui_probe" "$ROOT/assets/screens/result2.png" "$ROOT/assets/templates/btn_ok.png" \
    | grep -q "found=1 score=1.0000" || fail "ui_probe 没在自身上找到模板"

# 5. 离线圣水识别能读图不崩
"$BIN/elixir_probe" "$ROOT/assets/screens" >/dev/null 2>&1 || fail "elixir_probe 失败"

echo "PASS: macOS 冒烟测试全部通过"
