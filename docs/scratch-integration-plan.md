# Scratch Integration Plan for nefuOS

Status: **phase 1 shipped** (block programming playground, app `APP_SCRATCH`).
Audience: nefuOS maintainers. Language: English (project convention).

## 1. Goal

Bring a Scratch-like visual, block-based programming experience to nefuOS so
that beginners can compose small animations without typing code. The
deliverable is `core/apps/scratch.cpp` — a self-contained graphical app
(600x470 logical pixels) with a stage, a sprite, a block palette and a script
panel, all built on the kernel's own GUI primitives (no Scratch runtime, no
third-party engine).

## 2. Shipped in phase 1

### 2.1 Layout (stage coordinates)

- Window 600x470. Stage 240x200 on the left; sprite moves in 8 compass
  directions, pure integer math, direction 0 = right, increasing clockwise
  (0,45,90,...,315 degrees).
- Block palette 4x2 grid on the right; clicking a block appends it to the
  script.
- Script panel center-right: the current script with the running block
  highlighted; a `repeat` block highlights the whole repeated span while it
  executes.
- Bottom action bar: Run, Clear, Backspace (remove last block), Random
  (generates a demo script).

### 2.2 Block set (8 blocks)

| Block | Behavior |
|---|---|
| move 10 steps | advance 10 px in the current direction |
| turn right 45 | direction += 45 (mod 360) |
| turn left 45 | direction -= 45 (mod 360) |
| go to random | teleport to a random stage position |
| say Hello 2s | speech bubble for 2 seconds |
| wait 0.5s | pause 0.5 s |
| repeat next x10 | repeat the next block 10 times |
| clear speech | remove the speech bubble |

The script steps at ~110 ms per block so animations are visibly paced.

### 2.3 Integration

- `core/apps/apps.h`: `APP_SCRATCH` added after `APP_DATECALC`;
  `scratch_launch()` declared.
- `core/apps/apps.cpp`: app name `"Scratch 编程"/"Scratch"` and a launch case.
- Uses `KEY_BACKSPACE` (already defined by the input layer).

## 3. Phase 2: richer blocks (planned)

1. **Motion** — glide to x/y, point towards, change x/y by, bounce off edge.
2. **Looks** — costume/sprite flip, size change, show/hide, broadcast-free
   text display on stage.
3. **Sound** — play a short tone through the kernel beeper (must stay within
   the pure-integer rule; no audio codec).
4. **Control** — `if/then`, `forever`, nested repeat, stop all; a tiny
   expression evaluator (integer + comparison) so conditionals are real.
5. **Variables** — integer variables with set/change/read blocks; display as
   stage badges (this is where a Scratch program stops being a slideshow).
6. **Events** — green-flag start, key-pressed triggers.
7. **Save/load** — serialize a script as a small text DSL and persist it in
   NVFS (e.g. `/home/user/scratch/*.scr`), reusing the journaled file API.

### 3.1 Execution engine notes

- Keep the interpreter a simple array-of-block walker; add a program counter
  stack for `repeat`/`forever`. Each step must yield back to the GUI loop
  (no blocking loops) so the window stays responsive.
- All coordinates and angles stay integer; round motion to whole pixels to
  avoid float math on the bare-metal backend.

## 4. Acceptance

Phase 1 is accepted when the app launches from the app grid, all eight blocks
run, Random produces a working demo, and Backspace/Clear behave. Phase 2 adds
variables + conditionals + save/load; acceptance = a script can be saved,
reloaded and re-run byte-identically.
