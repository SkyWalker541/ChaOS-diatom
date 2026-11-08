#!/bin/sh
# Checks the CPU port of PT_SkyWalker541 against the shader itself.
#
#   sh sweep.sh gl     real GLSL (Mesa, software) vs the float reference
#   sh sweep.sh fast   float reference vs the fast renderer that ships
#
# Every case must match within one colour level (0-255).
#
# "gl" makes two test-only edits to the GLSL (mkref.py builds them), neither
# changing what it computes:
#   - the grain hash returns 0.5 in both versions: a hash amplifies the last
#     bit of its input, so its speckle differs between any two GPUs;
#   - the glare's light point is worked out in the fragment stage with the
#     vertex stage's exact code, because Mesa's software driver feeds wrong
#     values to vertex-only uniforms in this shader. The vertex code itself is
#     checked on its own by mkref.py.
#
# Drop shadows at half-pixel distances are reported but not failed in "gl":
# there the shadow's source sits exactly on a pixel border, and which side
# wins is down to the last bit of the coordinate on any GPU.
set -e
cd "$(dirname "$0")"
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1
mode=${1:-gl}
fail=0
TIE=0

run() {   # frame scale frameno label params...
	f=$1; s=$2; n=$3; label=$4; shift 4
	if [ "$mode" = gl ]; then
		./glref fraglight.glsl "$f.ppm" "$s" "$n" a.ppm "$@"
		./ptref_flat "$f.ppm" "$s" "$n" b.ppm "$@"
	else
		./ptref "$f.ppm" "$s" "$n" a.ppm "$@"
		${PTC:-./ptc} "$f.ppm" "$s" "$n" b.ppm "$@"
	fi
	printf '%-34s %-4s ' "$label" "$f"
	if [ $TIE = 1 ] && [ "$mode" = gl ]; then
		python3 compare.py a.ppm b.ppm; echo "    (half-pixel tie: reported, not failed)"
	elif [ "$mode" = gl ] && case "$s" in *x*) true ;; *) false ;; esac; then
		# Any size: a screen pixel's centre can fall exactly on the line
		# between two game pixels (426.5 * 160 / 853 is 80), and which one
		# the GPU's nearest takes there is its rounding. Those lines only.
		r=$(python3 compare.py a.ppm b.ppm); echo "$r"
		echo "$r" | sed -n 's/.*within 1 level *\([0-9.]*\)%.*/\1/p' |
			awk '{ exit !($1 >= 99.85) }' || fail=1
	else
		python3 compare.py a.ppm b.ppm 1 || fail=1
	fi
}

for f in gbc gba gb; do
	s=5; [ "$f" = gba ] && s=4
	run $f $s 0 "defaults"
	run $f $s 0 "white mode, gap backing"  PT_TRANSPARENT_PIXELS=0
	run $f $s 0 "bright mode"              PT_TRANSPARENT_PIXELS=1 PT_TRANSPARENCY_AMOUNT=0.4
	run $f $s 0 "lcd dot, soft"            PT_PIXEL_EFFECT=2
	run $f $s 0 "lcd dot, hard, black gap" PT_PIXEL_EFFECT=2 PT_DOT_EDGE_SOFTNESS=0 PT_GAP_COLOR=1 PT_DOT_SIZE=0.6
	run $f $s 0 "lcd dot, white mode"      PT_PIXEL_EFFECT=2 PT_TRANSPARENT_PIXELS=0 PT_MIN_BRIGHTNESS_FOR_DOTS=0
	run $f $s 0 "lcd dot, white gap"       PT_PIXEL_EFFECT=2 PT_GAP_COLOR=2 PT_DOT_GAP_OPACITY=1
	run $f $s 0 "grid wide, white gap"     PT_GRID_LINE_WIDTH=0.5 PT_GRID_LINE_OPACITY=1 PT_GAP_COLOR=2
	run $f $s 0 "grid, black gap, white mode" PT_GAP_COLOR=1 PT_TRANSPARENT_PIXELS=0
	run $f $s 0 "grid width 0"             PT_GRID_LINE_WIDTH=0
	run $f $s 0 "effect off"               PT_PIXEL_EFFECT=0
	run $f $s 0 "effect off, white mode"   PT_PIXEL_EFFECT=0 PT_TRANSPARENT_PIXELS=0
	run $f $s 0 "backing grey"             PT_BACKING_COLOR=2 PT_BACKING_COLOR_STRENGTH=1
	run $f $s 0 "backing white"            PT_BACKING_COLOR=3
	run $f $s 0 "backing off"              PT_BACKING_COLOR=0
	run $f $s 0 "backing strength 0"       PT_BACKING_COLOR_STRENGTH=0
	run $f $s 0 "darken 60"                PT_DARKEN_PICTURE=60
	run $f $s 0 "darken 100"               PT_DARKEN_PICTURE=100
	run $f $s 0 "manual threshold 0.5"     PT_GAME_SYSTEM=0 PT_MANUAL_WHITE_DETECTION_LEVEL=0.5
	run $f $s 0 "system GB"                PT_GAME_SYSTEM=1
	run $f $s 0 "system GBA SP"            PT_GAME_SYSTEM=3
	run $f $s 0 "system GBA orig"          PT_GAME_SYSTEM=4
	TIE=1
	run $f $s 0 "shadow 3.5 up-left"       PT_SHADOW_DISTANCE=3.5 PT_SHADOW_DIRECTION=3 PT_DROP_SHADOW_OPACITY=1
	run $f $s 0 "shadow 0.5 up-right"      PT_SHADOW_DISTANCE=0.5 PT_SHADOW_DIRECTION=2
	TIE=0
	run $f $s 0 "shadow -2 down-left"      PT_SHADOW_DISTANCE=-2 PT_SHADOW_DIRECTION=1
	run $f $s 0 "shadow 10"                PT_SHADOW_DISTANCE=10 PT_DROP_SHADOW_OPACITY=0.8
	run $f $s 0 "shadow off"               PT_DROP_SHADOW_OPACITY=0
	run $f $s 0 "bezel 1.0"                PT_BEZEL_EDGE_SHADOW=1
	run $f $s 0 "bezel off"                PT_BEZEL_EDGE_SHADOW=0
	run $f $s 0 "glare, still light"       PT_GLARE_AND_SHEEN=1 PT_LIGHT_DRIFT_SPEED=0
	run $f $s 777 "glare, drifting, frame 777" PT_GLARE_AND_SHEEN=1
	run $f $s 5000 "glare+ripple, frame 5000" PT_GLARE_AND_SHEEN=1 PT_SHEEN_RIPPLE=0.8 PT_SHEEN_RIPPLE_SIZE=0.3 PT_SHEEN_HUE_TURN=0.4
	run $f $s 0 "glare strong, gap backing" PT_GLARE_AND_SHEEN=1 PT_GLARE_STRENGTH=1 PT_GLARE_SPREAD=0.05 PT_SHEEN_STRENGTH=0.5 PT_LIGHT_HOME_X=-0.2 PT_LIGHT_HOME_Y=1.3
	run $f $s 0 "glare only, no sheen"     PT_GLARE_AND_SHEEN=1 PT_SHEEN_STRENGTH=0 PT_GLARE_STRENGTH=1
	run $f $s 0 "everything at once"       PT_TRANSPARENT_PIXELS=0 PT_PIXEL_EFFECT=2 PT_GLARE_AND_SHEEN=1 PT_DARKEN_PICTURE=30 PT_BACKING_COLOR=2 PT_SHADOW_DISTANCE=2 PT_BEZEL_EDGE_SHADOW=0.8
done
run gbc 3 0 "scale 3"
run gbc 4 0 "scale 4, lcd dot"         PT_PIXEL_EFFECT=2
run gba 2 0 "scale 2"
run gb  6 0 "scale 6, grid"
run gbc 1 0 "scale 1"

# Any size (ChaOS's Aspect and Stretch). The Pixel Effect is off away from a
# whole-number scale (pt_shader.h), so these cases have it off.
for sz in 1024x768 853x768 1024x683 1001x701; do
	run gb  $sz 0 "$sz, effect off"         PT_PIXEL_EFFECT=0
	run gbc $sz 0 "$sz, white mode"         PT_PIXEL_EFFECT=0 PT_TRANSPARENT_PIXELS=0
	run gba $sz 777 "$sz, glare, shadow, bezel" PT_PIXEL_EFFECT=0 PT_GLARE_AND_SHEEN=1 PT_SHADOW_DISTANCE=2 PT_BEZEL_EDGE_SHADOW=0.8
done

[ $fail = 0 ] && echo "ALL CASES WITHIN 1 LEVEL" || { echo "SOME CASES DIFFER"; exit 1; }
