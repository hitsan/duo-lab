#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

if [ -z "${TOOLCHAIN_PREFIX:-}" ]; then
    echo "error: run 'source /PATH/TO/duo-examples/envsetup.sh' first (https://github.com/milkv-duo/duo-examples)" >&2
    exit 1
fi

make -C linux sobel-host
make -C linux sobel
