/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */

# Integrated GPU bring-up on real hardware

Scope: Intel and AMD integrated display controllers (class 0x03), driver side
only. No pipe reprogramming. This document is the checklist to run when the
image is booted on a physical machine instead of QEMU.

## What the driver does

On real hardware the firmware (UEFI GOP / VBE) has already programmed the
display engine, and the boot loader hands nefuOS the framebuffer that resulted.
There is no command queue to drive, so taking the screen over means:

1. Locate the display MMIO register window through PCI BAR0.
2. Read the live pipe/plane state: source size, plane stride, plane surface
   base and the plane enable bit.
3. Use the surface base as proof. Only when it equals the framebuffer address
   from the boot info do we know the register read was valid, and only then do
   we trust the pipe geometry enough to override the boot pitch/resolution.

The pipe is never written. A wrong plane surface base on real hardware means a
black screen, and the firmware-programmed image is already correct in every
case we can verify.

## Registers used

Intel (offsets inside the GTTMMADR BAR0 window, valid Gen2..Gen12):

| Register | Offset | Meaning |
| --- | --- | --- |
| PIPE_SRC(A..D) | 0x6001C + pipe*0x1000 | (width-1) low 16 bits, (height-1) high 16 bits |
| PLANE_CTRL(A..D) | 0x70180 + pipe*0x1000 | bit31 = plane enabled |
| PLANE_STRIDE(A..D) | 0x70188 + pipe*0x1000 | byte pitch, low 16 bits |
| PLANE_SURF(A..D) | 0x7019C + pipe*0x1000 | surface base, low 12 bits are flags |

AMD (DCE/DCN, BAR0):

| Register | Offset | Meaning |
| --- | --- | --- |
| D1GRPH_CONTROL | 0x6104 | bit0 = graphics enabled |
| D1GRPH_X_END | 0x6108 | width in pixels |
| D1GRPH_Y_END | 0x610C | height in pixels |
| D1GRPH_SURF | 0x6118 | primary surface base, low word |
| D1GRPH_SURF_HI | 0x611C | primary surface base, high word |
| D1GRPH_PITCH | 0x6120 | plane pitch |

## Safety gates

These exist because a wrong guess here is a black screen or a page fault,
which is not recoverable at runtime:

- **4GB ceiling.** nefuOS identity maps only 0..4GB (entry.s). A BAR placed
  above 4GB would fault on the first MMIO read, so such a window is skipped
  and the boot framebuffer is kept.
- **Geometry sanity.** Width 320..8192, height 200..8192, stride between
  width*2 and width*8 bytes. Anything outside that range is not trusted.
- **Surface proof.** A pipe is only taken over when its surface base matches
  the boot framebuffer address. A mismatch (for example an Intel GTT offset
  instead of a physical address) falls back to the boot framebuffer.
- **No writes to the engine.** Only reads are issued against the GPU.

## Expected log on a machine that gets taken over

```
gpu: [00:02.0] Intel Intel Iris Xe Graphics (Tiger Lake) [8086:8a52] (bar0=0x...)
gpu: intel iGPU 00:02.0 bar0=0x...
gpu: intel pipe A src=1920x1080 ctrl=0x80000000 stride=7680 surf=0x...
gpu: intel pipe B src=1x1 ctrl=0x0 stride=0 surf=0x0
gpu: intel pipe C src=1x1 ctrl=0x0 stride=0 surf=0x0
gpu: intel pipe D src=1x1 ctrl=0x0 stride=0 surf=0x0
gpu: intel pipe A surface=0x..., boot LFB=0x...
gpu: intel iGPU owns the screen (1920x1080 pitch=7680)
```

`pipe B/C/D` reporting `src=1x1 ctrl=0x0` is normal: those pipes are disabled
and the source size register reads back as zero.

## Reading the outcome

- `iGPU owns the screen` -> verified takeover. The framebuffer geometry now
  comes from the hardware.
- `surface is not the boot LFB, keeping boot framebuffer` -> the register read
  disagreed with the boot info. The screen still works on the boot LFB; the
  log line is the evidence to investigate (GTT-mapped surface, or a plane that
  the firmware drives through a different path).
- `software rendering on boot LFB, verified writable` -> no hardware takeover
  (no adapter, or a BAR above 4GB), but the screen is usable for every software
  renderer.
- `no usable framebuffer - screen unavailable` -> the boot info pointed at dead
  memory. Nothing can be drawn to a physical display.
- `MMIO not usable` -> the BAR sits above 4GB. Nothing was touched.

## Software fallback (the 4GB case)

The 4GB identity map limits hardware takeover, not rendering. When no backend
takes the screen - including when an Intel/AMD MMIO BAR sits above 4GB - the
driver falls back to software rendering into the boot framebuffer, and proves
that memory is writable before advertising it:

```
gpu: software rendering on boot LFB 0x..., (1920x1080x32), verified writable
```

Everything that draws without a GPU goes through that pointer: `gfx`, LVGL, the
window manager, and the `swgl` canvases the browser composites. None of those
needs GPU MMIO, so a BAR above 4GB costs the hardware takeover and nothing
else.

The proof step matters. A boot info block can point at memory that is not
there - `-vga none` on QEMU still reports `LFB=0xfd000000` with no display
device present. Two pixels are written and read back, then restored; if they do
not come back the framebuffer is declared dead:

```
gpu: no usable framebuffer (LFB 0xfd000000 1024x768x32) - screen unavailable
```

The kernel keeps running in that case. The rendering self-tests still pass
because they work on their own off-screen buffers, but nothing reaches a
physical display, which is exactly the honest state to report.

## Known limits

- Only the primary plane is inspected; overlays/cursors are ignored.
- Intel Gen13+ (Xe) moved parts of the display register block and may read
  back as disabled; the fallback then keeps the boot framebuffer.
- Multi-GPU systems take the first Intel/AMD adapter whose surface matches.
- No page flip, no modeset, no interrupt handling yet. Presenting is a plain
  memory write plus a fence.

## Test procedure

1. Build: `powershell -ExecutionPolicy Bypass -File tools\build_iso.ps1`,
   output lands in `%TEMP%\nefu_dist\nefuOS.iso`.
2. Write the ISO to a USB stick as a raw image (same procedure as the earlier
   bare-metal runs).
3. Boot the machine, watch the first lines of the kernel log for the
   `gpu:` group.
4. If the screen is black, the firmware pipe was not matched: report the
   `gpu: intel pipe ...` lines. They carry the raw register values needed to
   extend the register set.
