# PT SkyWalker541 - CPU version

The pixel transparency shader by SkyWalker541, ported from
`PT_SkyWalker541.glsl` v2.1.0 to C so it runs on devices that draw frames with
the CPU, like the TrimUI Brick. Same 31 parameters, same names, same defaults,
same picture, at whole-number scale factors. MIT License, see `LICENSE`;
what it builds on is credited in `../../THIRD-PARTY.md`.

## Files

| File | What it is |
|------|------------|
| `pt_shader.h` | The API: parameters, rendering a frame, rendering in bands across threads |
| `pt_shader.c` | The renderer that ships |
| `pt_math.h` | The shader's arithmetic, shared with the test reference |
| `PT_SkyWalker541.glsl` | The original shader, kept as the source of truth |

## Building

Build `pt_shader.c` with:

```
-O3 -fno-trapping-math -fno-math-errno -ffp-contract=off
```

- `-O3` vectorizes the per-pixel loops (NEON on the Brick).
- `-fno-trapping-math` and `-fno-math-errno` let the loops with floating point
  (LCD Dot, glare) vectorize too. Nothing here uses floating-point exceptions
  or `errno`.
- `-ffp-contract=off` keeps the backing grain identical on every machine: the
  grain is a hash, and fusing a multiply and an add changes its last bit and
  so the whole speckle.

## Using it

One thread:

```c
pt_ctx *c = pt_create();
pt_params p;
pt_params_default(&p);
p.v[PT_PIXEL_EFFECT] = 2;              /* LCD Dot */
pt_set_params(c, &p);
pt_render(c, frame, 160, 144, 160, 5, screen, 1024, &fmt, frame_count);
```

Several threads: `pt_begin_frame` once, then `pt_render_band` from each
thread with its own `pt_band` and rows. Any split gives exactly the same
picture.

## How it is checked

`../../test/pt/` holds the tests (`make check` there):

- `glref` renders with the real GLSL file on Mesa's software GPU.
- `ptref` is a literal, pixel-by-pixel C version of the shader's `main()`.
- `ptc` is the fast renderer.
- `sweep.sh gl` checks `ptref` against the GLSL; `sweep.sh fast` checks `ptc`
  against `ptref`. 110 cases each, covering every parameter, three test
  frames and scales 1 to 6. Every case must match within one colour level.

Both sweeps pass on x86 and on an ARM (Cortex-A53) build run under emulation.

Two notes from the GLSL comparison:

- The grain is a hash, so its speckle differs between any two GPUs. The GLSL
  comparison holds the grain flat in both versions; everything else is exact.
- At a drop-shadow distance ending in .5, the shadow's source pixel sits
  exactly on a pixel border, and which neighbour casts it comes down to the
  last bit of a coordinate. It differs between GPUs too, so those two cases
  are reported but not failed.

## Speed

Measured on a desktop CPU (AVX2), 800x720 output: about 1.6 ms per frame at
default settings, 2.6 ms with LCD Dot, 3.2 ms with a drifting glare light.

On the Brick (Cortex-A53, 1.8 GHz) it is not yet measured. The inner loop at
default settings is about 21 instructions per pixel, which suggests roughly
10 ms per frame on one core, or 3.5 to 5 ms split across two or three of the
cores the emulator isn't using.
