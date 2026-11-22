#!/bin/bash
# Builds default.xex in WSL: compiles what changed, strips the .deplibs sections and links.
# Run it through a path without spaces (make cannot take them), e.g. a link in your home:
#   ln -s "/mnt/c/.../this project" ~/sor1 && bash ~/sor1/port/tools/build.sh
set -e
cd "$(dirname "$0")/.."
PORT_DIR=$(pwd -L)
ROOT=$(dirname "$PORT_DIR")
LLVM=${OXDK_LLVM:-$HOME/oxdk-llvm/build}
mkdir -p build
[ -f build/.stamp ] || touch -d 2000-01-01 build/.stamp
make objs -j"$(nproc)" ROOT="$ROOT" 2>&1 | grep -E "error|warning: implicit" || true
find build/obj -name "*.o" -newer build/.stamp -exec "$LLVM/bin/llvm-objcopy" --remove-section=.deplibs {} \; 2>/dev/null || \
  find build/obj -name "*.o" -exec "$LLVM/bin/llvm-objcopy" --remove-section=.deplibs {} \;
touch build/.stamp
make xex ROOT="$ROOT" 2>&1 | grep -v "oxdklink.py\|^    -T\|^    --" > build/link_log.txt || true
grep -E "^  [A-Za-z_]+$" build/link_log.txt | sed 's/^ */unresolved: /' | sort -u
tail -2 build/link_log.txt
