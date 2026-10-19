#!/usr/bin/env bash
#
# 头文件 -> /usr/include/coroutine/
# 动态库 -> /usr/lib/coroutine/
#
# 用法: ./install.sh          (不是 root 会自动加 sudo)
# 卸载: sudo rm -rf /usr/include/coroutine /usr/lib/coroutine
#
set -euo pipefail

SRC=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
INC_DIR=/usr/include/coroutine
LIB_DIR=/usr/lib/

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi

# 没编过就先编一次
if [ ! -f "$SRC/lib/libcoroutine.so" ]; then
    echo "==> 先编译"
    cmake -S "$SRC" -B "$SRC/build"
    cmake --build "$SRC/build" -j"$(nproc)"
fi

# 清掉之前误装到 /usr/include 和 /usr/lib 的平铺副本:
# 只删和本仓库内容完全一致的文件, 不会动别人的同名头文件
for f in "$SRC"/src/*/*.h; do
    dst="/usr/include/$(basename "$f")"
    if [ -f "$dst" ] && cmp -s "$f" "$dst"; then
        $SUDO rm -f "$dst"
        echo "==> 移除旧的平铺头文件 $dst"
    fi
done
if [ -f /usr/lib/libcoroutine.so ] && cmp -s "$SRC/lib/libcoroutine.so" /usr/lib/libcoroutine.so; then
    $SUDO rm -f /usr/lib/libcoroutine.so
    echo "==> 移除旧的平铺动态库 /usr/lib/libcoroutine.so"
fi

$SUDO mkdir -p "$INC_DIR" "$LIB_DIR"
$SUDO cp -f "$SRC"/src/*/*.h "$INC_DIR"/
$SUDO cp -f "$SRC"/lib/libcoroutine.so "$LIB_DIR"/

echo "==> 头文件 -> $INC_DIR"
ls -1 "$INC_DIR"
echo "==> 动态库 -> $LIB_DIR/libcoroutine.so"

cat <<EOF

用法:

  #include <coroutine/iomanager.h>
  #include <coroutine/hook.h>

  g++ app.cpp -L$LIB_DIR -lcoroutine -Wl,-rpath,$LIB_DIR -o app
EOF
