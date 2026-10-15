#!/usr/bin/env bash
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
TESTS="$REPO/misc/hdn-tests"
SOURCE="$REPO/skyrim-platform/src/platform_se/skyrim_platform"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
c++ -std=c++20 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -fno-omit-frame-pointer -I "$SOURCE" "$TESTS/remote-face-tint-queue.cpp" -o "$OUT/queue"
"$OUT/queue"
# GCC warns for ABI offset assertions on polymorphic engine classes. The
# assertions remain active, as they are in the real Windows build.
c++ -std=c++20 -Wall -Wextra -Werror -Wno-invalid-offsetof -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$TESTS/test-renderer-stubs" -I "$SOURCE" \
  -include "$TESTS/test-renderer-stubs/MockCommonLib.h" \
  "$SOURCE/RemoteFaceTintRenderer.cpp" "$TESTS/test-remote-face-renderer.cpp" -o "$OUT/render"
"$OUT/render"
