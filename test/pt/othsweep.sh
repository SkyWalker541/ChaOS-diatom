#!/bin/sh
# crt-lottes-fast, lcd3x and res-independent-scanlines (ChaOS): the CPU ports (othc) against the GLSL
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
# glref draws the picture's top row at the bottom of its framebuffer (it reads
# back without a flip), so gl_FragCoord.y counts from the picture's top there;
# on a screen it counts from the bottom, which is what the scanlines' CPU port
# follows. The one place the scanlines read it is turned round to match.
hp "$G/scanlines/shaders/res-independent-scanlines.glsl" "$T/scan0.glsl"
sed 's/(gl_FragCoord.y \* OutSize.w)/((OutputSize.y - gl_FragCoord.y) * OutSize.w)/' "$T/scan0.glsl" > "$T/scan.glsl"
grep -q 'OutputSize.y - gl_FragCoord.y' "$T/scan.glsl"
fail=0
run() {
	k=$1 f=$2 s=$3; shift 3
	./glref "$T/$k.glsl" "$f.ppm" "$s" 0 "$T/r.ppm" "$@" > /dev/null 2>&1
	./othc "$k" "$f.ppm" "$s" "$T/c.ppm" "$@"
	r=$(python3 compare.py "$T/r.ppm" "$T/c.ppm")
	echo "$k $f x$s $*: $r"
	echo "$r" | grep -q "within 1 level 100.00%" || ok_ties "$r" || fail=1
}
# At a size that is no whole-number scale, a screen pixel's centre can fall
# exactly on the line between two game pixels (426.5 * 160 / 853 is 80), and
# which of the two "nearest" picks there is the GPU's rounding: the CPU takes
# the right-hand one. Those columns and rows only, a tenth of a percent at most.
ok_ties() {
	case "$s" in *x*) ;; *) return 1 ;; esac
	echo "$1" | sed -n 's/.*within 1 level *\([0-9.]*\)%.*/\1/p' |
		awk '{ exit !($1 >= 99.85) }'
}
for f in gb gbc gba; do
	run scan $f 3
	run scan $f 4 autoscale=1
	run scan $f 3 autoscale=1 amp=1 lines_black=0.3
	run scan $f 5 autoscale=1 phase=1.2 lines_white=1.5 amp=0.8
	run scan $f 2 imageSize=160
	run lcd $f 3
	run lcd $f 5 brighten_scanlines=4 brighten_lcd=1
	run crt $f 3
	run crt $f 4 CURVATURE=0
	run crt $f 3 MASK=3 CURVATURE=0.1 TRINITRON_CURVE=1
	run crt $f 4 MASK=2 SCAN_BLUR=4 CORNER=0
	run crt $f 3 MASK=0 SCANLINE_THINNESS=1 CRT_GAMMA=2.2
	run crt $f 3 MASK=3 CURVATURE=0 MASK_INTENSITY=1
done
# Any size (ChaOS's Aspect and Stretch): the Brick's whole screen, and sizes
# no whole-number scale gives
for f in gb gbc gba; do
	for sz in 1024x768 853x768 1024x683 1001x701; do
		run scan $f $sz autoscale=1
		run lcd $f $sz
		run crt $f $sz CURVATURE=0
		run crt $f $sz
	done
done
[ $fail = 0 ] && echo "ok: crt-lottes-fast, lcd3x and the scanlines match the GLSL" || { echo "FAIL"; exit 1; }
