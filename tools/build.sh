#!/usr/bin/env bash
# Build the development tools (cb-tool, cb-eval, cb-match-eval) next to this script.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
DRV=../libfprint-tod-canvasbio
FLAGS="-O2 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter"
FP="$(pkg-config --cflags --libs libfprint-2-tod-1 libfprint-2 glib-2.0 gio-2.0 gusb)"
if pkg-config --exists opencv4; then
  CV="$(pkg-config --cflags --libs opencv4)"
else
  CV="-I/usr/include/opencv4 -lopencv_calib3d -lopencv_features2d -lopencv_flann -lopencv_imgproc -lopencv_core"
fi
g++ -O2 -std=c++17 -c -o cb-match.o $DRV/cb-match.cpp $(pkg-config --cflags glib-2.0) ${CV%% -l*}
gcc $FLAGS -o cb-tool cb-tool.c cb-match.o $FP $CV -lstdc++ -lm
gcc $FLAGS -o cb-eval cb-eval.c $FP -lm
gcc $FLAGS -o cb-match-eval cb-match-eval.c cb-match.o $(pkg-config --cflags --libs glib-2.0) $CV -lstdc++ -lm
echo "built: cb-tool cb-eval cb-match-eval"
