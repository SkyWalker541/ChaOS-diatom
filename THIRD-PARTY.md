# Third-party notices

Diatom's own code is under the Mozilla Public License 2.0: its own files stay
open, and the license reaches no further than them, so Diatom escapes GPL
inheritance either way, which is a founding goal of the project.

## `src/libretro.h`

The libretro API header, vendored so the build is self-contained.

- **Copyright (C) 2010-2024 The RetroArch team**
- **MIT-style permissive license**, and the header states that its license
  statement *"only applies to this libretro API header (libretro.h)"*.

That scoping is deliberate on libretro's part and is what makes the whole
arrangement work: any-license frontends may host any-license cores, because the
interface between them carries no copyleft.

Full text is at the top of the file.

## `vendor/rcheevos/`

RetroAchievements' implementation of their achievement condition language,
vendored so the build stays self-contained.

- **Copyright (c) 2018 RetroAchievements.org**
- **MIT licensed**, full text in `vendor/rcheevos/LICENSE`.
- From https://github.com/RetroAchievements/rcheevos at `f1417fb` (2026-08-12);
  `vendor/rcheevos/README.md` says what was taken and what was left out.

MIT is the whole reason this is possible rather than a problem: it fits beside
diatom's own license, so the project's founding goal - escaping GPL inheritance - is
untouched. The networking halves (`rc_client`, `rapi`, `rurl`) are deliberately
not vendored; `vendor/rcheevos/README.md` lists every omission and why.

## Cores

**Diatom ships no cores and has no core list.** It loads whatever shared library
it is handed at runtime. Cores keep their own licenses - commonly GPL - and are
the responsibility of whoever distributes them.

`test/stubcore.c` is diatom's own code: a libretro core that is not an emulator,
so the frontend can be exercised with no third-party binary present.

## Libraries it links

None are carried in this repository or shipped with diatom. It links against
zlib (zipped games), SDL2 (the window, input and sound where SDL drives them)
and, on each device, the system's ALSA and graphics libraries (OpenGL ES and
EGL), all provided by the system it runs on: on the GKD Pixel 2 and the TrimUI
Brick that is plastron, whose package list on a card is
`Licenses/plastron-packages.md`.

## `shaders/pt/` (added in the ChaOS fork)

**PT SkyWalker541**, the pixel transparency shader for Game Boy, Game Boy
Color and Game Boy Advance, ported from GLSL to C to run on the CPU.

- **Copyright (c) 2026 SkyWalker541**, **MIT**; full text in
  `shaders/pt/LICENSE`. From https://github.com/SkyWalker541/PT-SkyWalker541
  (v2.1.0); the original GLSL is kept beside the port.
- The backing grain's hash is **"Hash without Sine" by David Hoskins**
  (https://www.shadertoy.com/view/4djSRW), **MIT**, Copyright (c) 2014
  David Hoskins. Its notice:

  > Permission is hereby granted, free of charge, to any person obtaining a
  > copy of this software and associated documentation files (the
  > "Software"), to deal in the Software without restriction, including
  > without limitation the rights to use, copy, modify, merge, publish,
  > distribute, sublicense, and/or sell copies of the Software, and to permit
  > persons to whom the Software is furnished to do so, subject to the
  > following conditions:
  >
  > The above copyright notice and this permission notice shall be included
  > in all copies or substantial portions of the Software.
  >
  > THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
  > OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  > MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
  > NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
  > DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
  > OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
  > USE OR OTHER DEALINGS IN THE SOFTWARE.

- LCD Dot mode is inspired by **Themaister's dot shader** (public domain); no
  code is taken from it.

MIT sits beside diatom's MPL-2.0 as rcheevos does: the files keep their own
licence.

## `shaders/crt/` (added in the ChaOS fork)

**crt-lottes-fast**, for the NES, SNES, Genesis, Master System and PC Engine,
ported from GLSL to C to run on the CPU.

- **[CRTS] "PUBLIC DOMAIN CRT-STYLED SCALAR" by Timothy Lottes**
  (https://www.shadertoy.com/view/MtSfRK), adapted for RetroArch by hunterk,
  as libretro's glsl-shaders carries it in `crt/shaders/crt-lottes-fast.glsl`
  (https://github.com/libretro/glsl-shaders, at `1aaabbe`).
- **The Unlicense** ("aka PUBLIC DOMAIN"), whose full text is in the
  original's header; the original is kept beside the port as
  `shaders/crt/crt-lottes-fast.glsl`.

## `shaders/lcd/` (added in the ChaOS fork)

**lcd3x**, for the Game Gear and the Neo Geo Pocket and Pocket Color, ported
from GLSL to C to run on the CPU.

- **By Gigaherz**, **public domain** (its header: "License: Public domain"),
  as libretro's glsl-shaders carries it in `handheld/shaders/lcd3x.glsl` (at
  `1aaabbe`); the original is kept beside the port as `shaders/lcd/lcd3x.glsl`.

Public-domain code sits beside diatom's MPL-2.0 with nothing owed; the
notices are kept so where the code came from stays plain.
