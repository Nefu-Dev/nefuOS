# nefuOS

![nefuOS logo](docs/nefuos-logo.svg)

A tiny hobby operating system written in C++/C with **two backends**:

1. **Win32 host** — runs as a native Windows desktop app (`dist\nefuOS.exe`)
2. **Bare-metal x86_64 kernel** — boots from an El Torito ISO in QEMU / VirtualBox / real hardware

Everything is real: the GUI, the Unix-like VFS, the networking stack, and the apps.

![architecture](docs/architecture.svg)

---

## Features

### Desktop & GUI
- Window manager with draggable/resizable windows, taskbar, focus handling
- Desktop icons are **shortcuts**: deleting a shortcut never deletes the original app
- Draggable desktop icons with grid snap (no accidental overlaps)
- Trash (deleting files moves them to `/home/user/Trash`)
- Chinese / English language switch (Settings app)

### Apps (all built-in, all functional)
| App | Notes |
|---|---|
| File Manager | dual-pane, tree + list, sort, icon/list view, open `.ppm`/`.bin`/`.nefud`/text |
| Browser | real minimal TCP, address bar input fixed, page save to `/usr/downloads` |
| Network Settings | **real adapter info** (MAC/IP/gateway), **real Wi-Fi scan** on host (Wlan API), real ICMP ping, bare-metal wired link |
| Settings | zh/en language switch, desktop info |
| Store | scrollable catalog, 8+ installable apps, keyboard PgDn |
| Image Viewer | `.ppm` viewer; host backend decodes jpg/png/gif/bmp via GDI+ |
| Music Player | note sequencer (beeps) |
| System Monitor | CPU/memory stats |
| Terminal | Linux-style commands, see below |
| Notepad / Paint / Calculator / Clock / Snake / Minesweeper | classic built-ins |

### Networking (real, not simulated)
- Bare metal: Intel e1000 NIC driver + ARP + ICMP ping + minimal TCP
- Verified: inside QEMU the OS connected to a real HTTP server and received
  `HTTP/1.0 200 OK` (205 bytes) — full ARP → IP → ICMP → TCP → HTTP chain
- Host backend: reads the **real** adapter (GetAdaptersInfo) and performs a
  **real Wi-Fi scan** (WlanGetAvailableNetworkList) — real SSIDs, signal
  quality, encryption state; ping uses real ICMP (IcmpSendEcho)

### Unix-like file system (real files & directories)
```
/          root
/bin       built-in binaries (sh, bash, ls, cat, grep, ...)
/sbin      admin tools (init, mount, fsck, ifconfig, ...)
/etc       real configs: passwd, shadow, group, hosts, resolv.conf,
           fstab, profile, shells, motd, services, inittab, store.conf
/home/user Documents, Pictures, Music, Downloads, Trash, .bashrc
/root      root user home (.bashrc, .profile, .bash_history)
/proc      pseudo-fs: cpuinfo, meminfo, version, uptime, loadavg, ...
/dev       devices: null, zero, random, tty, console, sda, sr0
/sys       kernel + class/net/eth0/* nodes
/tmp       temporary files (OS scratch here)
/usr       user programs, libs, downloads (browser saves to /usr/downloads)
/var       logs (syslog, kern.log, auth.log), lib/dpkg/status
/mnt       mount points (cdrom, usb, hd)
/lib       modules.dep, firmware/e1000.bin
/srv       served content (www/index.html)
/opt       optional packages
```
- ~150 real Unix-style files are seeded at boot (`NFS1` VFS, serialized to
  `nefuos.fs` on the host backend); system files survive reboots.
- **Downloads go to `/usr/downloads`** (browser) and temp scratch goes to
  `/tmp` - just like a normal OS.
- `recovery` rebuilds standard dirs + core files if they were deleted.
- Built-in key-value database at `/var/lib/nefuos/db/system.db`, managed
  with the `db` command (writes require admin).

### Terminal commands (Linux-style, ~60 commands)
`help` `ls` `cd` `pwd` `cat` `mkdir` `touch` `rm` `echo` `clear` `tree`
`about` `uptime` `exit` `shutdown` `reboot` `poweroff` `hostname`
`ifconfig`/`ip` `route` `arp` `lspci` `lsusb` `stat` `file` `ln` `sync`
`dmesg` `neofetch` `top` `kill` `df` `who`/`users` `last` `sh`/`bash`
`which` `history` `true`/`false` `uname` `date` `login` `sleep`
`su <password>` (admin `lbinm`, hash-verified) `db list|get|set|rm`
`honeypot` (passive decoy services) `recovery` (self-repair)
`./app.bin` runs NEFBIN01 binaries directly; PATH lookup (/bin, /usr/bin, /sbin).

### Unix-style apps
- `.nefud` (text manifest) and `.bin` (NEFBIN01) files open directly from
  the File Manager; `.bin` shows a distinct icon (dark box with `>_`).
- `tools/nefupack.py` packages `.nefud` manifests and NEFBIN01 `.bin` apps.
- System apps come preinstalled (11 entries in the store catalog).

### Security & administration
- Admin account `lbinm`; password is **injected at build time** through the
  `NEFU_ADMIN_PASSWORD` environment variable, stored **only as an FNV-1a 64
  hash** (`core/sys/admin_hash.h`, git-ignored). The plain password never
  appears in source, README, logs, or the ISO.
- Passive **honeypot** decoy services (21/23/25/80/443) with a local access
  log - defensive only, fully compliant with the Cybersecurity Law.
- `db set/rm` writes are restricted to the verified admin; guests read only.


---

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
│   └── bare/              # x86_64: boot.s, entry.s, kernel main, e1000
├── tools/
│   ├── build_iso.ps1      # one-command build (host exe + kernel + ISO)
│   └── make_iso.py        # El Torito no-emulation ISO builder
└── dist/
    ├── nefuOS.exe         # host backend (Windows)
    └── nefuOS_v2.iso      # bootable ISO (bare-metal backend)
```

### Boot chain (El Torito no-emulation)
```
BIOS/SeaBIOS ──boot catalog──▶ 2 KiB boot image @0x7C00 (boot.s)
boot.s ──INT 13h AH=42h static DAPs──▶ kernel @0x20000 (4 chunks, ≤32 sectors each)
kernel ──VBE 0x118 800x600x32──▶ long mode, paging, heap, desktop
```

### Kernel layout
```
0x6000  VBE info       0x7000  bootinfo (LFB/800/600/pitch)
0x7C00  boot sector    0x7E00  BOOT_DRIVE / LOAD_COUNT
0x8F00  GDT64          0x9000  PML4 (2 MiB huge pages, 0–128 MB WB)
0x20000 kernel         0x400000 heap (64 MB)     0x1F000 stack
LFB 0xFD000000 (VBE 0x118, 800×600×32)
```

---

## Build (Windows)

Requirements: MinGW-w64 g++, NASM-free (uses `as`), QEMU (optional), Python 3.

```powershell
cd nefuOS
powershell -ExecutionPolicy Bypass -File tools\build_iso.ps1
```

Output:
- `dist\nefuOS.exe` — host desktop
- `dist\nefuOS_v2.iso` — bootable ISO

## Run

**Host backend:**
```powershell
.\dist\nefuOS.exe
```

**Bare metal (QEMU):**
```powershell
qemu-system-x86_64 -drive file=dist\nefuOS_v2.iso,media=cdrom,format=raw -boot d -m 128M
```

**VirtualBox:** create a VM, attach `nefuOS_v2.iso` as optical drive, boot.

---

## Notes & credits

- **Not a copy**: this is an original hobby OS. Design ideas are *inspired by*
  Linux layout conventions (`/usr`, `/tmp`, shortcuts), SeaBIOS CD boot
  behavior (analyzed from SeaBIOS source: INT 13h AH=42h, 64 KiB per-call
  limit, DL=0xE0 ATAPI addressing), El Torito no-emulation concepts from
  GRUB/limine, and the clean app/VFS split popular in xv6-style teaching OSes.
- Host image decoding uses GDI+ (real jpg/png/gif/bmp).
- Real network data (adapter MAC/IP, Wi-Fi scan) comes from the Windows
  `iphlpapi` / `wlanapi` / `icmpapi` interfaces on the host backend, and from
  the e1000/ARP/ICMP/TCP stack on bare metal.
- Music Player synthesizes a real 8 kHz 8-bit WAV (integer math) and plays it
  through the platform audio API (host: `PlaySound`).
- Source comments are in English.

### Third-party references & SBOM

This project ships **no third-party binary code** - the whole kernel and all
apps are self-written. The following projects were studied or referenced for
behavior/design only (no code copied):

| Project | Version/Commit | License | How it is used here |
|---|---|---|---|
| SeaBIOS | git master (analyzed `src/hw/` CD boot) | LGPL-2.1 | behavior reference for El Torito no-emulation + INT 13h AH=42h limits; no code included |
| Linux kernel | FHS layout / proc & sysfs naming | GPL-2.0 (ideas only) | directory tree design, `/proc`/`/sys`/`/dev` conventions; no code included |
| QEMU | 11.1.1 (e1000 82540EM, SeaBIOS) | GPL-2.0 | test/verification harness only; not shipped |
| stb (nothings) | single-file image headers | MIT | image-decode *concept* reference; host uses GDI+, bare metal has its own PPM/JPEG reader |
| xv6 (MIT) | teaching OS | MIT | app/VFS split design inspiration; no code included |

No GPL-licensed code is linked into, or distributed with, nefuOS.

## License

MIT — free to use, learn from, and modify.
