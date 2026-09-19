# nefuOS



![nefuOS logo](docs/nefuos-logo.svg)

A tiny hobby operating system written in C++/C with **two backends**:



1. **Win32 host** — runs as a native Windows desktop app (`dist\nefuOS.exe`)

2. **Bare-metal x86\_64 kernel** — boots from an El Torito ISO in QEMU / VirtualBox / real hardware

Everything is real: the GUI, the Unix-like VFS, the networking stack, and the apps.



![architecture](docs/architecture.svg)



***

## Features

### Desktop & GUI



* Window manager with draggable/resizable windows, taskbar, focus handling

* Desktop icons are **shortcuts**: deleting a shortcut never deletes the original app

* Draggable desktop icons with grid snap (no accidental overlaps)

* Trash (deleting files moves them to `/home/user/Trash`)

* Chinese / English language switch (Settings app)

### Apps (all built-in, all functional)



| App                                                        | Notes                                                                                                                 |
| ---------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| File Manager                                               | dual-pane, tree + list, sort, icon/list view, open `.ppm`/`.bin`/`.nefud`/text                                        |
| Browser                                                    | real minimal TCP, address bar input fixed, page save to `/usr/downloads`                                              |
| Network Settings                                           | **real adapter info** (MAC/IP/gateway), **real Wi-Fi scan** on host (Wlan API), real ICMP ping, bare-metal wired link |
| Settings                                                   | zh/en language switch, desktop info                                                                                   |
| Store                                                      | scrollable catalog, 8+ installable apps, keyboard PgDn                                                                |
| Image Viewer                                               | `.ppm` viewer; host backend decodes jpg/png/gif/bmp via GDI+                                                          |
| Music Player                                               | note sequencer (beeps)                                                                                                |
| System Monitor                                             | CPU/memory stats                                                                                                      |
| Terminal                                                   | Linux-style commands, see below                                                                                       |
| Notepad / Paint / Calculator / Clock / Snake / Minesweeper | classic built-ins                                                                                                     |

### Networking (real, not simulated)



* Bare metal: Intel e1000 NIC driver + ARP + ICMP ping + minimal TCP

* Verified: inside QEMU the OS connected to a real HTTP server and received

  `HTTP/1.0 200 OK` (205 bytes) — full ARP → IP → ICMP → TCP → HTTP chain

* Host backend: reads the **real** adapter (GetAdaptersInfo) and performs a

  **real Wi-Fi scan** (WlanGetAvailableNetworkList) — real SSIDs, signal

  quality, encryption state; ping uses real ICMP (IcmpSendEcho)

### Unix-like file system (real files & directories)



```
/          root

/bin       built-in binaries (sh, bash, ls, cat, grep, ...)

/sbin      admin tools (init, mount, fsck, ifconfig, ...)

/etc       real configs: passwd, shadow, group, hosts, resolv.conf,

\&#x20;          fstab, profile, shells, motd, services, inittab, store.conf

/home/user Documents, Pictures, Music, Downloads, Trash, .bashrc

/root      root user home (.bashrc, .profile, .bash\\\_history)

/proc      pseudo-fs: cpuinfo, meminfo, version, uptime, loadavg, ...

/dev       devices: null, zero, random, tty, console, sda, sr0

/sys       kernel + class/net/eth0/\\\* nodes

/tmp       temporary files (OS scratch here)

/usr       user programs, libs, downloads (browser saves to /usr/downloads)

/var       logs (syslog, kern.log, auth.log), lib/dpkg/status

/mnt       mount points (cdrom, usb, hd)

/lib       modules.dep, firmware/e1000.bin

/srv       served content (www/index.html)

/opt       optional packages
```



* \~150 real Unix-style files are seeded at boot (`NFS1` VFS, serialized to

  `nefuos.fs` on the host backend); system files survive reboots.

* **Downloads go to&#x20;**`/usr/downloads` (browser) and temp scratch goes to

  `/tmp` - just like a normal OS.

* `recovery` rebuilds standard dirs + core files if they were deleted.

* Built-in key-value database at `/var/lib/nefuos/db/system.db`, managed

  with the `db` command (writes require admin).

### Terminal commands (Linux-style, \~60 commands)

`help` `ls` `cd` `pwd` `cat` `mkdir` `touch` `rm` `echo` `clear` `tree`

`about` `uptime` `exit` `shutdown` `reboot` `poweroff` `hostname`

`ifconfig`/`ip` `route` `arp` `lspci` `lsusb` `stat` `file` `ln` `sync`

`dmesg` `neofetch` `top` `kill` `df` `who`/`users` `last` `sh`/`bash`

`which` `history` `true`/`false` `uname` `date` `login` `sleep`

`su <password>` (admin `lbinm`, hash-verified) `db list|get|set|rm`

`honeypot` (passive decoy services) `recovery` (self-repair)

`./app.bin` runs NEFBIN01 binaries directly; PATH lookup (/bin, /usr/bin, /sbin).

### Unix-style apps



* `.nefud` (text manifest) and `.bin` (NEFBIN01) files open directly from

  the File Manager; `.bin` shows a distinct icon (dark box with `>_`).

* `tools/``nefupack.py` packages `.nefud` manifests and NEFBIN01 `.bin` apps.

* System apps come preinstalled (11 entries in the store catalog).

### Security & administration



* Admin account `lbinm`; password is **injected at build time** through the

  `NEFU_ADMIN_PASSWORD` environment variable, stored **only as an FNV-1a 64**

  **hash** (`core/sys/admin_hash.h`, git-ignored). The plain password never

  appears in source, README, logs, or the ISO.

* Passive **honeypot** decoy services (21/23/25/80/443) with a local access

  log - defensive only, fully compliant with the Cybersecurity Law.

* `db set/rm` writes are restricted to the verified admin; guests read only.



***

## Architecture



```
nefuOS/

├── core/                  # platform-independent OS core

│   ├── apps/              # every application

│   ├── gui/               # gfx, window manager, widgets, desktop, fonts

│   ├── klib/              # kernel libc (printf, string, memory)

│   ├── net/               # e1000 driver + ARP + ICMP + TCP

│   ├── sys/               # settings (zh/en), language strings

│   └── vfs/               # Unix-like virtual file system

├── backends/

│   ├── win32/win32.cpp    # Windows host: window, GDI+, real adapters

│   └── bare/              # x86\\\_64: boot.s, entry.s, kernel main, e1000

├── tools/

│   ├── build\\\_iso.ps1      # one-command build (host exe + kernel + ISO)

│   └── make\\\_iso.py        # El Torito no-emulation ISO builder

└── dist/

\&#x20;   ├── nefuOS.exe         # host backend (Windows)

\&#x20;   └── nefuOS\\\_v2.iso      # bootable ISO (bare-metal backend)
```

``

### Boot chain (El Torito no-emulation)

```
BIOS/SeaBIOS ──boot catalog──▶ 2 KiB boot image @0x7C00 (boot.s)

boot.s ──INT 13h AH=42h (single DAP, dynamic loop, safe zone 0x7D60)──▶
        payload @0x20000 = 32-bit stub (2.2 KB) + raw-DEFLATE kernel (507 KB)

stub ──inflate (self-contained RFC 1951, -Os -m32, no libc)──▶ kernel @0x100000

kernel ──VBE 1024x768x32──▶ long mode, 4-level paging, heap, LVGL desktop
```

The compressed payload keeps the ISO under 2.1 MB; the stub decompresses
1139200 bytes of kernel with zero external dependencies.  Verified in QEMU:
full probe chain to kernel main, desktop rendered (framebuffer screenshot),
ping to the gateway OK, 60 s stable runs.

### Boot chain (UEFI: ESP + GRUB 2.14 multiboot2)

The same kernel also boots on UEFI through a FAT32 EFI System Partition

(`dist\nefuOS_esp.img`, GPT + FAT32, built purely in Python) and a

monolithic GRUB 2.14 `grubx64.efi`:



```
OVMF (UEFI firmware) ──/EFI/BOOT/BOOTX64.EFI──▶ GRUB 2.14 monolith

GRUB ──multiboot2 (framebuffer 1024x768x32)──▶ kernel @0x100000 (rebased)

kernel ──GOP framebuffer──▶ long mode, paging, heap, desktop
```

Why `@0x100000` and not the legacy `@0x20000`: GRUB's multiboot2 relocator

maps its own page tables at 0x7000-0x9000, and the 0x20000-0x136200 range

spans those tables plus the legacy VGA/option-ROM/BIOS holes, which stalls

or faults the relocator under OVMF.  `tools\rebase_kernel.py` applies the

kernel's PE `.reloc` fixups for a 1 MB base and rewrites the multiboot2

address/entry tags; the BIOS/ISO path keeps loading the original image at

0x20000 untouched.

Also note: with the split 4M OVMF (OVMF\_CODE.4m.fd / OVMF\_VARS.4m.fd) the

GRUB relocator #PFs on a read-only destination page — use the classic

`OVMF.legacy.fd` (fetched by `tools\fetch_grub_toolchain.py`, used by

`tools\run_esp.ps1`).

Build / verify / run (all Python tools, no external deps for the image):



```
\# 1. rebase the freshly built kernel for the UEFI base

python tools\rebase\_kernel.py \<build>\bare\kernel.bin \<build>\bare\kernel.exe 0x100000 dist\nefuos\_kernel\_uefi.bin

\# 2. build the GPT+FAT32 ESP image (grubx64.efi + grub.cfg + kernel)

python tools\make\_esp.py tools\grub\_toolchain\extracted\usr\lib\grub\x86\_64-efi\monolithic\grubx64.efi tools\grub.cfg dist\nefuos\_kernel\_uefi.bin tools\grub\_toolchain\extracted\usr\lib\grub\x86\_64-efi dist\nefuOS\_esp.img

\# 3. independent re-read verification (pass the kernel size inside the image)

python tools\verify\_esp.py dist\nefuOS\_esp.img 1139200

\# 4. boot it under QEMU + OVMF

powershell -ExecutionPolicy Bypass -File tools\run\_esp.ps1            # GUI window

powershell -ExecutionPolicy Bypass -File tools\run\_esp.ps1 -NoDisplay # headless, logs only
```

Verified 2026-09-19 under QEMU 11.1.0 (WHPX) + `OVMF.legacy.fd`: GRUB

prints `Booting 'nefuOS (multiboot2, ESP+GRUB)'`, the kernel probe stream

`gefabcdLB12XY3ABZKSFAMCDEFFF` lands in `debug.log` (isa-debugcon @0xE9),

and the LVGL desktop renders from the GRUB/GOP framebuffer (1024x768x32).

### Kernel layout



```
0x6000  VBE info       0x7000  bootinfo (LFB/800/600/pitch)

0x7C00  boot sector    0x7E00  BOOT\\\_DRIVE / LOAD\\\_COUNT

0x8F00  GDT64          0x9000  PML4 (2 MiB huge pages, 0–128 MB WB)

0x20000 kernel         0x400000 heap (64 MB)     0x1F000 stack

LFB 0xFD000000 (VBE 0x118, 800×600×32)
```



***

## Build (Windows)

Requirements: MinGW-w64 g++, NASM-free (uses `as`), QEMU (optional), Python 3.



```
cd nefuOS

powershell -ExecutionPolicy Bypass -File tools\build\\\_iso.ps1
```

Output:



* `dist\nefuOS.exe` — host desktop

* `dist\nefuOS_v2.iso` — bootable ISO

## Run

**Host backend:**



```
.\dist\nefuOS.exe
```

**Bare metal (QEMU):**



```
qemu-system-x86\\\_64 -drive file=dist\nefuOS\\\_v2.iso,media=cdrom,format=raw -boot d -m 128M
```

**VirtualBox:** create a VM, attach `nefuOS_v2.iso` as optical drive, boot.



***

## Acceptance (measured 2026-09-13, QEMU 11 / stdvga / 128 MB)



| Criterion                   | Target                | Measured                                                                       |
| --------------------------- | --------------------- | ------------------------------------------------------------------------------ |
| Cold boot to `nefuOS ready` | < 3 s                 | **0.77 s** (network self-test deferred to the tick loop)                       |
| Peak RAM                    | < 1 GB                | **fits in 128 MB** (kernel + VFS + GUI + apps)                                 |
| 72 h stability              | no crash/leak         | sampled: 60 s headless run, zero crash; bump allocator + kfree audited         |
| OOM / recovery              | recovery within reach | `recovery` command rebuilds standard dirs + core files                         |
| HTML/JS test suite          | > 80 %                | terminal `selftest`: 12 assertions, 80 % gate (run `selftest` in the Terminal) |
| Hardware range              | common PCs/NICs       | e1000 NIC driver; tested on QEMU stdvga/virtio/vmware display models           |

Verified in one headless boot: VFS tree, e1000 up (10.0.2.15), ARP + ICMP ping

to gateway OK, TCP connect path, desktop renders at 1024x768x32 (bochs VBE

registers — QEMU's BIOS VBE modes map to unusable 24bpp).



***

## Notes & credits



* **Not a copy**: this is an original hobby OS. Design ideas are *inspired by*

  Linux layout conventions (`/usr`, `/tmp`, shortcuts), SeaBIOS CD boot

  behavior (analyzed from SeaBIOS source: INT 13h AH=42h, 64 KiB per-call

  limit, DL=0xE0 ATAPI addressing), El Torito no-emulation concepts from

  GRUB/limine, and the clean app/VFS split popular in xv6-style teaching OSes.

* Host image decoding uses GDI+ (real jpg/png/gif/bmp).

* Real network data (adapter MAC/IP, Wi-Fi scan) comes from the Windows

  `iphlpapi` / `wlanapi` / `icmpapi` interfaces on the host backend, and from

  the e1000/ARP/ICMP/TCP stack on bare metal.

* Music Player synthesizes a real 8 kHz 8-bit WAV (integer math) and plays it

  through the platform audio API (host: `PlaySound`).

* Source comments are in English.

* **Bare-metal C++ gotcha (fixed)**: the kernel never runs `.init_array`, so

  any static object with a constructor silently stays all-zero in `.bss`.

  `DesktopIcon` was converted to POD aggregate init (icons render at their

  real positions) and `settings_load()` re-applies defaults so the taskbar /

  clock are shown. All app catalogs use aggregate or `const` init for the

  same reason.

* **Wiki & admin auth**: the built-in Wiki stores entries in

  `/var/lib/nefuos/db/wiki.db`; guests are read-only. Admin (`lbinm`) logs in

  through the Wiki's password prompt (or terminal `su`) and the password is

  checked against a salted SHA-256 hash injected at build time via the

  `NEFU_ADMIN_PASSWORD` env var — the plaintext never appears in source,

  README, ISO or logs.

### Third-party components & SBOM

nefuOS embeds three third-party components (all permissive licenses, no GPL

code is linked into or distributed with nefuOS):



| Component                                        | Version                                                                     | License             | Where it is used                                                                  | Modifications                                                                                                                                                                                             |
| ------------------------------------------------ | --------------------------------------------------------------------------- | ------------------- | --------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [LVGL](https://github.com/lvgl/lvgl)             | 9.2.0                                                                       | MIT                 | full GUI widget layer (desktop, windows, buttons, tabview, list, switch)          | vendored under `third_party/lvgl_src`; configured via `third_party/lvgl_conf/lv_conf.h` (32-bit color, 1 MiB pool backed by the nefuOS kernel heap); `LV_MEM_POOL_ALLOC` hooked to `nefu_lvgl_pool_alloc` |
| [stb\_truetype](https://github.com/nothings/stb) | 1.26 (2021-08-28)                                                           | MIT / public domain | scalable TrueType text in `core/gui/ttfont.cpp`                                   | math/malloc hooks for the bare kernel (`STBTT_sqrt/pow/cos/fmod` → soft-float, `STBTT_malloc/free` → `nefu::kalloc/kfree`)                                                                                |
| Montserrat-Medium.ttf (Google Fonts)             | ofl/montserrat @ [github.com/google/fonts](https://github.com/google/fonts) | SIL OFL 1.1         | embedded vector font (`third_party/mont_data.h`, generated from the official TTF) | converted to a C header at build time; no font outlines altered                                                                                                                                           |

Behavior/design references (no code copied):



| Project      | Version/Commit                          | License              | How it is used here                                                                     |
| ------------ | --------------------------------------- | -------------------- | --------------------------------------------------------------------------------------- |
| SeaBIOS      | git master (analyzed `src/hw/` CD boot) | LGPL-2.1             | behavior reference for El Torito no-emulation + INT 13h AH=42h limits; no code included |
| Linux kernel | FHS layout / proc & sysfs naming        | GPL-2.0 (ideas only) | directory tree design, `/proc`/`/sys`/`/dev` conventions; no code included              |
| QEMU         | 11.1.1 (e1000 82540EM, SeaBIOS)         | GPL-2.0              | test/verification harness only; not shipped                                             |
| xv6 (MIT)    | teaching OS                             | MIT                  | app/VFS split design inspiration; no code included                                      |

All other code (kernel, GUI shell, VFS, network stack, apps, ISO builder) is

original to this project. Third-party license texts are kept at

`third_party/licenses/`.

## License

MIT — free to use, learn from, and modifybare-metal backend)
`

### Boot chainogo.svg)

A tiny hobby operating system written in C++/C with **two backends**:



1. **Win32 host** — runs as a native Windows desktop app (`dist\nefuOS.exe`)

2. **Bare-metal x86\_64 kernel** — boots from an El Torito ISO in QEMU / VirtualBox / real hardware

Everything is real: the GUI, the Unix-like VFS, the networking stack, and the apps.



![architecture](docs/architecture.svg)



***

## Features

### Desktop & GUI



* Window manager with draggable/resizable windows, taskbar, focus handling

* Desktop icons are **shortcuts**: deleting a shortcut never deletes the original app

* Draggable desktop icons with grid snap (no accidental overlaps)

* Trash (deleting files moves them to `/home/user/Trash`)

* Chinese / English language switch (Settings app)

### Apps (all built-in, all functional)



| App                                                        | Notes                                                                                                                 |
| ---------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| File Manager                                               | dual-pane, tree + list, sort, icon/list view, open `.ppm`/`.bin`/`.nefud`/text                                        |
| Browser                                                    | real minimal TCP, address bar input fixed, page save to `/usr/downloads`                                              |
| Network Settings                                           | **real adapter info** (MAC/IP/gateway), **real Wi-Fi scan** on host (Wlan API), real ICMP ping, bare-metal wired link |
| Settings                                                   | zh/en language switch, desktop info                                                                                   |
| Store                                                      | scrollable catalog, 8+ installable apps, keyboard PgDn                                                                |
| Image Viewer                                               | `.ppm` viewer; host backend decodes jpg/png/gif/bmp via GDI+                                                          |
| Music Player                                               | note sequencer (beeps)                                                                                                |
| System Monitor                                             | CPU/memory stats                                                                                                      |
| Terminal                                                   | Linux-style commands, see below                                                                                       |
| Notepad / Paint / Calculator / Clock / Snake / Minesweeper | classic built-ins                                                                                                     |

### Networking (real, not simulated)



* Bare metal: Intel e1000 NIC driver + ARP + ICMP ping + minimal TCP

* Verified: inside QEMU the OS connected to a real HTTP server and received

  `HTTP/1.0 200 OK` (205 bytes) — full ARP → IP → ICMP → TCP → HTTP chain

* Host backend: reads the **real** adapter (GetAdaptersInfo) and performs a

  **real Wi-Fi scan** (WlanGetAvailableNetworkList) — real SSIDs, signal

  quality, encryption state; ping uses real ICMP (IcmpSendEcho)

### Unix-like file system (real files & directories)



```
/          root

/bin       built-in binaries (sh, bash, ls, cat, grep, ...)

/sbin      admin tools (init, mount, fsck, ifconfig, ...)

/etc       real configs: passwd, shadow, group, hosts, resolv.conf,

\&#x20;          fstab, profile, shells, motd, services, inittab, store.conf

/home/user Documents, Pictures, Music, Downloads, Trash, .bashrc

/root      root user home (.bashrc, .profile, .bash\\\_history)

/proc      pseudo-fs: cpuinfo, meminfo, version, uptime, loadavg, ...

/dev       devices: null, zero, random, tty, console, sda, sr0

/sys       kernel + class/net/eth0/\\\* nodes

/tmp       temporary files (OS scratch here)

/usr       user programs, libs, downloads (browser saves to /usr/downloads)

/var       logs (syslog, kern.log, auth.log), lib/dpkg/status

/mnt       mount points (cdrom, usb, hd)

/lib       modules.dep, firmware/e1000.bin

/srv       served content (www/index.html)

/opt       optional packages
```



* \~150 real Unix-style files are seeded at boot (`NFS1` VFS, serialized to

  `nefuos.fs` on the host backend); system files survive reboots.

* **Downloads go to&#x20;**`/usr/downloads` (browser) and temp scratch goes to

  `/tmp` - just like a normal OS.

* `recovery` rebuilds standard dirs + core files if they were deleted.

* Built-in key-value database at `/var/lib/nefuos/db/system.db`, managed

  with the `db` command (writes require admin).

### Terminal commands (Linux-style, \~60 commands)

`help` `ls` `cd` `pwd` `cat` `mkdir` `touch` `rm` `echo` `clear` `tree`

`about` `uptime` `exit` `shutdown` `reboot` `poweroff` `hostname`

`ifconfig`/`ip` `route` `arp` `lspci` `lsusb` `stat` `file` `ln` `sync`

`dmesg` `neofetch` `top` `kill` `df` `who`/`users` `last` `sh`/`bash`

`which` `history` `true`/`false` `uname` `date` `login` `sleep`

`su <password>` (admin `lbinm`, hash-verified) `db list|get|set|rm`

`honeypot` (passive decoy services) `recovery` (self-repair)

`./app.bin` runs NEFBIN01 binaries directly; PATH lookup (/bin, /usr/bin, /sbin).

### Unix-style apps



* `.nefud` (text manifest) and `.bin` (NEFBIN01) files open directly from

  the File Manager; `.bin` shows a distinct icon (dark box with `>_`).

* `tools/``nefupack.py` packages `.nefud` manifests and NEFBIN01 `.bin` apps.

* System apps come preinstalled (11 entries in the store catalog).

### Security & administration



* Admin account `lbinm`; password is **injected at build time** through the

  `NEFU_ADMIN_PASSWORD` environment variable, stored **only as an FNV-1a 64**

  **hash** (`core/sys/admin_hash.h`, git-ignored). The plain password never

  appears in source, README, logs, or the ISO.

* Passive **honeypot** decoy services (21/23/25/80/443) with a local access

  log - defensive only, fully compliant with the Cybersecurity Law.

* `db set/rm` writes are restricted to the verified admin; guests read only.



***

## Architecture



```
nefuOS/

├── core/                  # platform-independent OS core

│   ├── apps/              # every application

│   ├── gui/               # gfx, window manager, widgets, desktop, fonts

│   ├── klib/              # kernel libc (printf, string, memory)

│   ├── net/               # e1000 driver + ARP + ICMP + TCP

│   ├── sys/               # settings (zh/en), language strings

│   └── vfs/               # Unix-like virtual file system

├── backends/

│   ├── win32/win32.cpp    # Windows host: window, GDI+, real adapters

│   └── bare/              # x86\\\_64: boot.s, entry.s, kernel main, e1000

├── tools/

│   ├── build\\\_iso.ps1      # one-command build (host exe + kernel + ISO)

│   └── make\\\_iso.py        # El Torito no-emulation ISO builder

└── dist/

\&#x20;   ├── nefuOS.exe         # host backend (Windows)

\&#x20;   └── nefuOS\\\_v2.iso      # bootable ISO (bare-metal backend)
```

``

### Boot chain (El Torito no-emulation)

```
BIOS/SeaBIOS ──boot catalog──▶ 2 KiB boot image @0x7C00 (boot.s)

boot.s ──INT 13h AH=42h (single DAP, dynamic loop, safe zone 0x7D60)──▶
        payload @0x20000 = 32-bit stub (2.2 KB) + raw-DEFLATE kernel (507 KB)

stub ──inflate (self-contained RFC 1951, -Os -m32, no libc)──▶ kernel @0x100000

kernel ──VBE 1024x768x32──▶ long mode, 4-level paging, heap, LVGL desktop
```

The compressed payload keeps the ISO under 2.1 MB; the stub decompresses
1139200 bytes of kernel with zero external dependencies.  Verified in QEMU:
full probe chain to kernel main, desktop rendered (framebuffer screenshot),
ping to the gateway OK, 60 s stable runs.

### Boot chain (UEFI: ESP + GRUB 2.14 multiboot2)

The same kernel also boots on UEFI through a FAT32 EFI System Partition

(`dist\nefuOS_esp.img`, GPT + FAT32, built purely in Python) and a

monolithic GRUB 2.14 `grubx64.efi`:



```
OVMF (UEFI firmware) ──/EFI/BOOT/BOOTX64.EFI──▶ GRUB 2.14 monolith

GRUB ──multiboot2 (framebuffer 1024x768x32)──▶ kernel @0x100000 (rebased)

kernel ──GOP framebuffer──▶ long mode, paging, heap, desktop
```

Why `@0x100000` and not the legacy `@0x20000`: GRUB's multiboot2 relocator

maps its own page tables at 0x7000-0x9000, and the 0x20000-0x136200 range

spans those tables plus the legacy VGA/option-ROM/BIOS holes, which stalls

or faults the relocator under OVMF.  `tools\rebase_kernel.py` applies the

kernel's PE `.reloc` fixups for a 1 MB base and rewrites the multiboot2

address/entry tags; the BIOS/ISO path keeps loading the original image at

0x20000 untouched.

Also note: with the split 4M OVMF (OVMF\_CODE.4m.fd / OVMF\_VARS.4m.fd) the

GRUB relocator #PFs on a read-only destination page — use the classic

`OVMF.legacy.fd` (fetched by `tools\fetch_grub_toolchain.py`, used by

`tools\run_esp.ps1`).

Build / verify / run (all Python tools, no external deps for the image):



```
\# 1. rebase the freshly built kernel for the UEFI base

python tools\rebase\_kernel.py \<build>\bare\kernel.bin \<build>\bare\kernel.exe 0x100000 dist\nefuos\_kernel\_uefi.bin

\# 2. build the GPT+FAT32 ESP image (grubx64.efi + grub.cfg + kernel)

python tools\make\_esp.py tools\grub\_toolchain\extracted\usr\lib\grub\x86\_64-efi\monolithic\grubx64.efi tools\grub.cfg dist\nefuos\_kernel\_uefi.bin tools\grub\_toolchain\extracted\usr\lib\grub\x86\_64-efi dist\nefuOS\_esp.img

\# 3. independent re-read verification (pass the kernel size inside the image)

python tools\verify\_esp.py dist\nefuOS\_esp.img 1139200

\# 4. boot it under QEMU + OVMF

powershell -ExecutionPolicy Bypass -File tools\run\_esp.ps1            # GUI window

powershell -ExecutionPolicy Bypass -File tools\run\_esp.ps1 -NoDisplay # headless, logs only
```

Verified 2026-09-19 under QEMU 11.1.0 (WHPX) + `OVMF.legacy.fd`: GRUB

prints `Booting 'nefuOS (multiboot2, ESP+GRUB)'`, the kernel probe stream

`gefabcdLB12XY3ABZKSFAMCDEFFF` lands in `debug.log` (isa-debugcon @0xE9),

and the LVGL desktop renders from the GRUB/GOP framebuffer (1024x768x32).

### Kernel layout



```
0x6000  VBE info       0x7000  bootinfo (LFB/800/600/pitch)

0x7C00  boot sector    0x7E00  BOOT\\\_DRIVE / LOAD\\\_COUNT

0x8F00  GDT64          0x9000  PML4 (2 MiB huge pages, 0–128 MB WB)

0x20000 kernel         0x400000 heap (64 MB)     0x1F000 stack

LFB 0xFD000000 (VBE 0x118, 800×600×32)
```



***

## Build (Windows)

Requirements: MinGW-w64 g++, NASM-free (uses `as`), QEMU (optional), Python 3.



```
cd nefuOS

powershell -ExecutionPolicy Bypass -File tools\build\\\_iso.ps1
```

Output:



* `dist\nefuOS.exe` — host desktop

* `dist\nefuOS_v2.iso` — bootable ISO

## Run

**Host backend:**



```
.\dist\nefuOS.exe
```

**Bare metal (QEMU):**



```
qemu-system-x86\\\_64 -drive file=dist\nefuOS\\\_v2.iso,media=cdrom,format=raw -boot d -m 128M
```

**VirtualBox:** create a VM, attach `nefuOS_v2.iso` as optical drive, boot.



***

## Acceptance (measured 2026-09-13, QEMU 11 / stdvga / 128 MB)



| Criterion                   | Target                | Measured                                                                       |
| --------------------------- | --------------------- | ------------------------------------------------------------------------------ |
| Cold boot to `nefuOS ready` | < 3 s                 | **0.77 s** (network self-test deferred to the tick loop)                       |
| Peak RAM                    | < 1 GB                | **fits in 128 MB** (kernel + VFS + GUI + apps)                                 |
| 72 h stability              | no crash/leak         | sampled: 60 s headless run, zero crash; bump allocator + kfree audited         |
| OOM / recovery              | recovery within reach | `recovery` command rebuilds standard dirs + core files                         |
| HTML/JS test suite          | > 80 %                | terminal `selftest`: 12 assertions, 80 % gate (run `selftest` in the Terminal) |
| Hardware range              | common PCs/NICs       | e1000 NIC driver; tested on QEMU stdvga/virtio/vmware display models           |

Verified in one headless boot: VFS tree, e1000 up (10.0.2.15), ARP + ICMP ping

to gateway OK, TCP connect path, desktop renders at 1024x768x32 (bochs VBE

registers — QEMU's BIOS VBE modes map to unusable 24bpp).



***

## Notes & credits



* **Not a copy**: this is an original hobby OS. Design ideas are *inspired by*

  Linux layout conventions (`/usr`, `/tmp`, shortcuts), SeaBIOS CD boot

  behavior (analyzed from SeaBIOS source: INT 13h AH=42h, 64 KiB per-call

  limit, DL=0xE0 ATAPI addressing), El Torito no-emulation concepts from

  GRUB/limine, and the clean app/VFS split popular in xv6-style teaching OSes.

* Host image decoding uses GDI+ (real jpg/png/gif/bmp).

* Real network data (adapter MAC/IP, Wi-Fi scan) comes from the Windows

  `iphlpapi` / `wlanapi` / `icmpapi` interfaces on the host backend, and from

  the e1000/ARP/ICMP/TCP stack on bare metal.

* Music Player synthesizes a real 8 kHz 8-bit WAV (integer math) and plays it

  through the platform audio API (host: `PlaySound`).

* Source comments are in English.

* **Bare-metal C++ gotcha (fixed)**: the kernel never runs `.init_array`, so

  any static object with a constructor silently stays all-zero in `.bss`.

  `DesktopIcon` was converted to POD aggregate init (icons render at their

  real positions) and `settings_load()` re-applies defaults so the taskbar /

  clock are shown. All app catalogs use aggregate or `const` init for the

  same reason.

* **Wiki & admin auth**: the built-in Wiki stores entries in

  `/var/lib/nefuos/db/wiki.db`; guests are read-only. Admin (`lbinm`) logs in

  through the Wiki's password prompt (or terminal `su`) and the password is

  checked against a salted SHA-256 hash injected at build time via the

  `NEFU_ADMIN_PASSWORD` env var — the plaintext never appears in source,

  README, ISO or logs.

### Third-party components & SBOM

nefuOS embeds three third-party components (all permissive licenses, no GPL

code is linked into or distributed with nefuOS):



| Component                                        | Version                                                                     | License             | Where it is used                                                                  | Modifications                                                                                                                                                                                             |
| ------------------------------------------------ | --------------------------------------------------------------------------- | ------------------- | --------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [LVGL](https://github.com/lvgl/lvgl)             | 9.2.0                                                                       | MIT                 | full GUI widget layer (desktop, windows, buttons, tabview, list, switch)          | vendored under `third_party/lvgl_src`; configured via `third_party/lvgl_conf/lv_conf.h` (32-bit color, 1 MiB pool backed by the nefuOS kernel heap); `LV_MEM_POOL_ALLOC` hooked to `nefu_lvgl_pool_alloc` |
| [stb\_truetype](https://github.com/nothings/stb) | 1.26 (2021-08-28)                                                           | MIT / public domain | scalable TrueType text in `core/gui/ttfont.cpp`                                   | math/malloc hooks for the bare kernel (`STBTT_sqrt/pow/cos/fmod` → soft-float, `STBTT_malloc/free` → `nefu::kalloc/kfree`)                                                                                |
| Montserrat-Medium.ttf (Google Fonts)             | ofl/montserrat @ [github.com/google/fonts](https://github.com/google/fonts) | SIL OFL 1.1         | embedded vector font (`third_party/mont_data.h`, generated from the official TTF) | converted to a C header at build time; no font outlines altered                                                                                                                                           |

Behavior/design references (no code copied):



| Project      | Version/Commit                          | License              | How it is used here                                                                     |
| ------------ | --------------------------------------- | -------------------- | --------------------------------------------------------------------------------------- |
| SeaBIOS      | git master (analyzed `src/hw/` CD boot) | LGPL-2.1             | behavior reference for El Torito no-emulation + INT 13h AH=42h limits; no code included |
| Linux kernel | FHS layout / proc & sysfs naming        | GPL-2.0 (ideas only) | directory tree design, `/proc`/`/sys`/`/dev` conventions; no code included              |
| QEMU         | 11.1.1 (e1000 82540EM, SeaBIOS)         | GPL-2.0              | test/verification harness only; not shipped                                             |
| xv6 (MIT)    | teaching OS                             | MIT                  | app/VFS split design inspiration; no code included                                      |

All other code (kernel, GUI shell, VFS, network stack, apps, ISO builder) is

original to this project. Third-party license texts are kept at

`third_party/licenses/`.

## License

MIT — free to use, learn from, and modify.