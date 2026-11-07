#!/bin/sh
# crt-lottes-fast and lcd3x (ChaOS): the CPU ports (othc) against the GLSL
# drawn by Mesa (glref), every pixel within one level.
#
# The GLSL is libretro glsl-shaders' (GLSL=path to that checkout), drawn at
# highp: its mediump sizes, kept at half precision by Mesa, slide the
# scanlines down the screen (shaders/crt/crts.c says more).
set -e
cd "$(dirname "$0")"
G=${GLSL:-../../../libretro/glsl-shaders}
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
hp() { sed 's/precision mediump float/precision highp float/; s/#define COMPAT_PRECISION mediump/#define COMPAT_PRECISION highp/' "$1" > "$2"; }
hp "$G/crt/shaders/crt-lottes-fast.glsl" "$T/crt.glsl"
hp "$G/handheld/shaders/lcd3x.glsl" "$T/lcd.glsl"
fail=0
run() {
	k=$1 f=$2 s=$3; shift 3
	./glref "$T/$k.glsl" "$f.ppm" "$s" 0 "$T/r.ppm" "$@" > /dev/null 2>&1
	./othc "$k" "$f.ppm" "$s" "$T/c.ppm" "$@"
	r=$(python3 compare.py "$T/r.ppm" "$T/c.ppm")
	echo "$k $f x$s $*: $r"
	echo "$r" | grep -q "within 1 level 100.00%" || fail=1
}
for f in gb gbc gba; do
	run lcd $f 3
	run lcd $f 5 brighten_scanlines=4 brighten_lcd=1
	run crt $f 3
	run crt $f 4 CURVATURE=0
	run crt $f 3 MASK=3 CURVATURE=0.1 TRINITRON_CURVE=1
	run crt $f 4 MASK=2 SCAN_BLUR=4 CORNER=0
	run crt $f 3 MASK=0 SCANLINE_THINNESS=1 CRT_GAMMA=2.2
	run crt $f 3 MASK=3 CURVATURE=0 MASK_INTENSITY=1
done
[ $fail = 0 ] && echo "ok: crt-lottes-fast and lcd3x match the GLSL" || { echo "FAIL"; exit 1; }
