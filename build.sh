#!/bin/sh
# Usage: ./build.sh [DEBUG_LOG value, default 1]
set -e
cd "$(dirname "$0")"
DBG=${1:-1}
CXX=x86_64-w64-mingw32-g++-posix
CC=x86_64-w64-mingw32-gcc-posix
TP=third_party
mkdir -p build
for f in buffer hook trampoline; do $CC -O2 -c $TP/minhook/src/$f.c -I$TP/minhook/include -o build/mh_$f.o; done
$CC -O2 -c $TP/minhook/src/hde/hde64.c -o build/mh_hde64.o
$CXX -std=c++20 -O2 -Wall -Wno-unused-function -DDEBUG_LOG=$DBG \
  -I$TP/OpenXR-SDK/include -I$TP -I$TP/openvr010 -I$TP/minhook/include \
  -shared -o build/re2_frame_controllers.dll src/re2_frame_controllers.cpp build/mh_*.o \
  -static -static-libgcc -static-libstdc++ -Wl,--kill-at -lversion
echo built DEBUG_LOG=$DBG
