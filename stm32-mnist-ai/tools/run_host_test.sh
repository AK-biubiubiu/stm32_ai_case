#!/bin/sh
# Host-side verification of the on-device pipeline (no ARM toolchain needed).
set -e
cd "$(dirname "$0")/.."

OUT=build_host
mkdir -p "$OUT"

gcc -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter \
    -IApp -IModel \
    App/ai_mlp.c App/ai_preprocess.c tools/host_test.c \
    -lm -o "$OUT/host_test"

./"$OUT/host_test" tools/data
