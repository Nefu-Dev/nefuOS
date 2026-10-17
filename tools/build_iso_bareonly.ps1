# nefuOS �?exe + + + ISO�?GrubEsp 閺冨爼顤傛径鏍晸閹?ESP+GRUB 绾句胶娲忛梹婊冨剼閿?
param(
    [switch]$GrubEsp
)
$ErrorActionPreference = "Continue"
$root = "D:\mycppos1\nefuOS"
$g = "D:\CLion\bin\mingw\bin\g++.exe"
$as = "D:\CLion\bin\mingw\bin\as.exe"
$ld = "D:\CLion\bin\mingw\bin\ld.exe"
$objcopy = "D:\CLion\bin\mingw\bin\objcopy.exe"
Set-Location $root

# 0) Admin hash injection (FNV-1a 64, hex). The plain password is read from
#    the NEFU_ADMIN_PASSWORD env var, hashed, and ONLY the hash is embedded in
#    the build. The password never appears in source, ISO, or logs.
#    If the env var is unset, a random placeholder hash is used (login denied).
$python3 = "C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe"
if (-not (Test-Path $python3)) { $python3 = "python" }
$adminPw = [Environment]::GetEnvironmentVariable("NEFU_ADMIN_PASSWORD")
& $python3 "tools\admin_hash.py" $adminPw | Out-Null
if ($LASTEXITCODE -ne 0) { throw "admin hash gen failed" }

# The old hand-kept source array was dropped: it had drifted ~230 files behind
# the tree (whole module directories: raytrace, deeplearn, compiler, minilang,
# netproto, ...), which broke the link with undefined *_launch() references.
# core/**/*.cpp is enumerated from disk below so it cannot go stale again; only
# these retired files are left out (nothing references them and neither build
# list ever had them).
$coreExclude = @(
  "core\apps\calculator.cpp", "core\apps\html5.cpp", "core\apps\lockscreen.cpp",
  "core\apps\recycle.cpp", "core\apps\stubs.cpp",
  "core\apps\terminal_commands.cpp", "core\apps\terminal_extra.cpp",
  "core\sys\sysapi_extended.cpp"
)
$coreRoot = (Get-Location).Path
$coreSrc = @(Get-ChildItem "core" -Recurse -Filter *.cpp |
  ForEach-Object { $_.FullName.Substring($coreRoot.Length + 1) } |
  Where-Object { $coreExclude -notcontains $_ } |
  Sort-Object) + @("third_party\stb_image_wrap.cpp")
Write-Output "core sources: $($coreSrc.Count)"

# LVGL 9.2.0 sources (third_party/lvgl_src/lvgl-9.2.0/src/*.c)
$gcc = "D:\CLion\bin\mingw\bin\gcc.exe"
$lvglRoot = "third_party\lvgl_src\lvgl-9.2.0"
$lvglSrc = Get-ChildItem (Join-Path $lvglRoot "src") -Recurse -Filter *.c | ForEach-Object { $_.FullName }
$lvConfPath = (Resolve-Path "third_party\lvgl_conf\lv_conf.h").Path -replace '\\', '/'
$lvglInclude = @("-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
                 "-D", ("LV_CONF_PATH=" + $lvConfPath))
$lvglCFlags = @("-std=gnu11", "-O2", "-Wall", "-I", "third_party",
                "-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
                "-D", ('LV_CONF_PATH=' + $lvConfPath), "-c")

# 1) 閿涘澋in32�?
$bareOut = Join-Path $env:TEMP "nefu_build\bare"
$distOut = "D:\mycppos1\nefuOS\dist"
New-Item -ItemType Directory -Force -Path $bareOut | Out-Null
New-Item -ItemType Directory -Force -Path $distOut | Out-Null
$hostExe = Join-Path $distOut "nefuOS.exe"
# ============================================================
# HOST (win32) build SKIPPED - bare-only verification build
# Generated from build_iso.ps1; do not edit by hand.
# ============================================================
# 2)
$bareFlags = @(
  "-std=c++17", "-fno-exceptions", "-fno-rtti", "-fno-builtin",
  "-fno-stack-protector", "-mno-red-zone", "-O2", "-Wall", "-Wextra",
  "-ffunction-sections", "-fdata-sections", "-fno-asynchronous-unwind-tables",
  "-Wno-sized-deallocation", "-DNEFU_BARE", "-DNEFU_LVGL_DEMO", "-I", "core", "-I", "third_party",
  "-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
  "-D", ('LV_CONF_PATH=' + $lvConfPath), "-c"
)
# NOTE: no -ffreestanding here.  GCC 15's libstdc++ refuses <string>/<vector>/
# <map> in freestanding mode (requires_hosted.h #error), and kernel apps use
# them.  bare_libc.cpp therefore also provides operator new/delete and the
# std::__throw_* abort stubs so STL containers link against the bare heap.
# The whole kernel is compiled WITHOUT -mgeneral-regs-only (entry.s
# initializes the x87/SSE state at boot and the IRQ handlers save/restore the
# FPU state), so double math emits real x87/SSE code instead of a pile of
# undefined __adddf3/__muldf3 soft-float libgcc helpers, and every TU shares
# one calling convention for float/double arguments.
$bareFpuFlags = @()
foreach ($f in $bareFlags) { if ($f -ne "-mgeneral-regs-only") { $bareFpuFlags += $f } }
# bare.cpp hosts the IDT/ISRs; GCC's interrupt attribute REQUIRES the
# -mgeneral-regs-only model (it errors on SSE in an ISR), so the platform
# file alone keeps the integer ABI.  Its public platform_* API is entirely
# integer/pointer, which is ABI-identical across both models, and the ISR
# bodies are integer-only, so the kernel stays coherent.
$bareIntFlags = @()
foreach ($f in $bareFlags) { $bareIntFlags += $f }
$bareIntFlags += "-mgeneral-regs-only"
$objs = @()
foreach ($s in $coreSrc) {
  # numeric.cpp is double-based and the bare kernel has no soft-double:
  # it is compiled for the host build only, never into the kernel.
  if ($s -like "*numeric.cpp") { continue }
  $name = $s -replace '[\\/]', '_' -replace '\.cpp$', '.o'
  $obj = "$bareOut\$name"
  $cflags = $bareFlags
  if ($s -like "*minijs.cpp") { $cflags = $bareFpuFlags }
  & $g @cflags $s -o $obj
  if ($LASTEXITCODE -ne 0) { throw "bare compile failed: $s" }
  $objs += $obj
}
# LVGL 9.2.0 bare objects (pure C; LV_USE_FLOAT=0 so the public API is
# integer/struct only -- same FPU calling convention as the core)
$lvglBareFlags = @("-std=gnu11", "-ffreestanding", "-fno-stack-protector", "-mno-red-zone",
                   "-O2", "-Wall", "-ffunction-sections", "-fdata-sections",
                   "-fno-asynchronous-unwind-tables",
                   "-I", "third_party",
                   "-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
                   "-D", ('LV_CONF_PATH=' + $lvConfPath), "-c")
foreach ($lv in $lvglSrc) {
  $lname = ($lv -replace '.*\\src\\', 'lvgl_') -replace '[\\/]', '_' -replace '\.c$', '.o'
  $lobj = "$bareOut\$lname"
  if (-not (Test-Path $lobj)) {
    & $gcc @lvglBareFlags $lv -o $lobj
    if ($LASTEXITCODE -ne 0) { throw "lvgl compile failed: $lv" }
  }
  $objs += $lobj
}
& $g @bareIntFlags "backends\bare\bare.cpp" -o "$bareOut\bare_bare.o"
if ($LASTEXITCODE -ne 0) { throw "bare.cpp failed" }
$objs += "$bareOut\bare_bare.o"
# minimal soft-float (IEEE-754 single precision) for the bare kernel; integer
# ABI keeps it linkable by any leftover soft-float references
& $g @bareIntFlags "backends\bare\softfloat.cpp" -o "$bareOut\bare_softfloat.o"
if ($LASTEXITCODE -ne 0) { throw "softfloat.cpp failed" }
$objs += "$bareOut\bare_softfloat.o"
# bare libc for QuickJS: malloc family, printf family, full libm (x87/SSE),
# time functions.  Must NOT be built with -mgeneral-regs-only.
& $g @bareFpuFlags "backends\bare\bare_libc.cpp" -o "$bareOut\bare_barelibc.o"
if ($LASTEXITCODE -ne 0) { throw "bare_libc.cpp failed" }
$objs += "$bareOut\bare_barelibc.o"
# QuickJS (bare kernel): same sources as the host engine, but single-threaded
# (CONFIG_DISABLE_ATOMICS kills the pthread path), assert-free (NDEBUG; there
# is no __assert_fail in a freestanding kernel) and linked against bare_libc.
$qjBareFlags = @("-std=gnu11", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
                 "-fno-asynchronous-unwind-tables", "-Os", "-w", "-D_GNU_SOURCE",
                 "-DCONFIG_DISABLE_ATOMICS", "-DNDEBUG",
                 "-I", "third_party\quickjs", "-I", "third_party", "-c")
foreach ($q in $qjSrc) {
  $qobj = "$bareOut\bare_quickjs_" + [IO.Path]::GetFileNameWithoutExtension($q) + ".o"
  if (-not (Test-Path $qobj)) {
    & $gcc @qjBareFlags "third_party\quickjs\$q" -o $qobj
    if ($LASTEXITCODE -ne 0) { throw "quickjs bare compile failed: $q" }
  }
  $objs += $qobj
}
$webpBareSrc = @(
  "third_party\libwebp\src\dec\alpha_dec.c", "third_party\libwebp\src\dec\buffer_dec.c",
  "third_party\libwebp\src\dec\frame_dec.c", "third_party\libwebp\src\dec\idec_dec.c",
  "third_party\libwebp\src\dec\io_dec.c", "third_party\libwebp\src\dec\quant_dec.c",
  "third_party\libwebp\src\dec\tree_dec.c", "third_party\libwebp\src\dec\vp8_dec.c",
  "third_party\libwebp\src\dec\vp8l_dec.c", "third_party\libwebp\src\dec\webp_dec.c",
  "third_party\libwebp\src\dsp\alpha_processing.c", "third_party\libwebp\src\dsp\cpu.c",
  "third_party\libwebp\src\dsp\dec.c", "third_party\libwebp\src\dsp\dec_clip_tables.c",
  "third_party\libwebp\src\dsp\filters.c", "third_party\libwebp\src\dsp\lossless.c",
  "third_party\libwebp\src\dsp\rescaler.c", "third_party\libwebp\src\dsp\upsampling.c",
  "third_party\libwebp\src\dsp\yuv.c",
  "third_party\libwebp\src\dsp\alpha_processing_sse2.c", "third_party\libwebp\src\dsp\dec_sse2.c",
  "third_party\libwebp\src\dsp\filters_sse2.c", "third_party\libwebp\src\dsp\lossless_sse2.c",
  "third_party\libwebp\src\dsp\rescaler_sse2.c", "third_party\libwebp\src\dsp\upsampling_sse2.c",
  "third_party\libwebp\src\dsp\yuv_sse2.c",
  "third_party\libwebp\src\utils\bit_reader_utils.c", "third_party\libwebp\src\utils\color_cache_utils.c",
  "third_party\libwebp\src\utils\filters_utils.c", "third_party\libwebp\src\utils\huffman_utils.c",
  "third_party\libwebp\src\utils\palette.c", "third_party\libwebp\src\utils\quant_levels_dec_utils.c",
  "third_party\libwebp\src\utils\random_utils.c", "third_party\libwebp\src\utils\rescaler_utils.c",
  "third_party\libwebp\src\utils\thread_utils.c", "third_party\libwebp\src\utils\utils.c"
)
$webpBareFlags = @("-std=gnu11", "-ffreestanding", "-fno-stack-protector", "-mno-red-zone",
                   "-O2", "-Wall",
                   "-ffunction-sections", "-fdata-sections", "-fno-asynchronous-unwind-tables",
                   "-I", "third_party\libwebp", "-I", "third_party",
                   "-include", "third_party\webp_impl.h",
                   "-DWEBP_MALLOC=nefu_kalloc_x", "-DWEBP_CALLOC=nefu_kcalloc_x",
                   "-DWEBP_FREE=nefu_kfree_x", "-DWEBP_REALLOC=nefu_krealloc_x")
foreach ($w in $webpBareSrc) {
  $wobj = "$bareOut\" + ($w -replace '.*\\', '' -replace '\.c$', '.o')
  if (-not (Test-Path $wobj)) {
    & $gcc @webpBareFlags -c $w -o $wobj
    if ($LASTEXITCODE -ne 0) { throw "webp bare compile failed: $w" }
  }
  $objs += $wobj
}
$webpImplBare = "$bareOut\webp_impl.o"
if (-not (Test-Path $webpImplBare)) {
  & $gcc @webpBareFlags -c "third_party\webp_impl.c" -o $webpImplBare
  if ($LASTEXITCODE -ne 0) { throw "webp impl bare compile failed" }
}
$objs += $webpImplBare
# nanosvg (SVG rasterizer) is used unconditionally by stb_image_wrap.cpp
# (nsvg_decode_mem), but was only ever compiled for the HOST build -- the bare
# link failed with undefined nsvg* references.  Compile it for the kernel too,
# with the FPU calling convention (nsvgParse takes a float dpi argument), and
# let it use the bare malloc/free + float wrappers from bare_libc.
$nanosvgBare = "$bareOut\nanosvg_impl.o"
if (-not (Test-Path $nanosvgBare)) {
  & $gcc @("-std=gnu11", "-fno-stack-protector", "-mno-red-zone", "-O2",
           "-ffunction-sections", "-fdata-sections", "-fno-asynchronous-unwind-tables",
           "-I", "third_party", "-c") `
        "third_party\nanosvg_impl.c" -o $nanosvgBare
  if ($LASTEXITCODE -ne 0) { throw "nanosvg bare compile failed" }
}
$objs += $nanosvgBare

# 3) 閿涙瓬oot sector + kernel entry + multi-boot picker
& $as "backends\bare\boot.s" -o "$bareOut\boot.o"
if ($LASTEXITCODE -ne 0) { throw "boot.s failed" }
& $as "backends\bare\entry.s" -o "$bareOut\entry.o"
if ($LASTEXITCODE -ne 0) { throw "entry.s failed" }
& $as "backends\bare\menu.s" -o "$bareOut\menu.o"
if ($LASTEXITCODE -ne 0) { throw "menu.s failed" }
& $objcopy -O binary -j .text "$bareOut\menu.o" "$bareOut\menu.bin"
if ($LASTEXITCODE -ne 0) { throw "menu objcopy failed" }
$menuLen = (Get-Item "$bareOut\menu.bin").Length
if ($menuLen -gt 4096) { throw "menu.bin too large: $menuLen bytes (max 4096)" }
Write-Output "menu.bin OK: $menuLen bytes"

# �?x20000 kernel_start
$objs = @("$bareOut\entry.o") + $objs

# 4) link閿涘湧E �?>
# Same 32KB command-line cap as the host link: pass the object list via a
# response file so ld actually runs.
$bareLinkRsp = Join-Path $env:TEMP "nefu_bare_link.rsp"
$bareLinkArgs = @("-mi386pep", "--image-base", "0x100000", "--gc-sections",
                  "-T", "backends\bare\linker.ld", "-o", "$bareOut\kernel.exe",
                  "-Map", "$bareOut\kernel.map") + $objs
$bareLinkArgs | ForEach-Object { $_ -replace '\\', '/' } |
    ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } } |
    Set-Content -Path $bareLinkRsp -Encoding ASCII
& $ld "@$bareLinkRsp"
if ($LASTEXITCODE -ne 0) { throw "link failed" }
# Rebin-Kernel -> tools\rebin_kernel.py (deterministic; parses the PE section
# table, assembles the flat image by RVA, skips .reloc / zero-raw sections).
# It SELF-VERIFIES that the multiboot2 header is at flat offset 8, so a stale
# or misassembled kernel fails loudly here instead of silently dying later in
# patch_mb2.  Prints "REBIN_OK <maxEnd> <bssStart> <bssSize>" for parsing.
$rebinOut = & $python3 "tools\rebin_kernel.py" "$bareOut\kernel.exe" "$bareOut\kernel.bin"
if ($LASTEXITCODE -ne 0) { throw "rebin_kernel failed" }
$kernelBssStart = 0
$kernelBssSize = 0
$rebinText = ($rebinOut -join "`n")
if ($rebinText -match "REBIN_OK\s+\d+\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)") {
    $kernelBssStart = [Convert]::ToUInt32($matches[1], 16)
    $kernelBssSize = [Convert]::ToUInt32($matches[2], 16)
} else {
    throw "rebin_kernel output unparsable: $rebinText"
}
# This PE layout has no .bss section (all static state is in .data), so the
# bss start/size are 0 and entry.s zero-fills nothing -- correct, keep as-is.
Write-Output "rebin OK: bss_start=0x$('{0:X}' -f $kernelBssStart) bss_size=0x$('{0:X}' -f $kernelBssSize)"
if (-not $kernelBssStart) { $kernelBssStart = 0 }  # no .bss in this PE layout
if (-not $kernelBssSize) { $kernelBssSize = 0 }
$kSize = (Get-Item "$bareOut\kernel.bin").Length
Write-Output "kernel.bin OK: $kSize bytes"
$isoKernelMax = 4194304 * 2048   # ISO layout: kernel occupies up to 1024 x 2048B blocks at LBA24
Write-Output "isoKernelMax=$isoKernelMax kSize=$kSize (make_iso cap = 4194304)"
if ($kSize -gt $isoKernelMax) { throw "kernel too large for ISO (max $isoKernelMax bytes, got $kSize)" }
# memory map check: the flat kernel loads at 0x100000 and must stay BELOW the
# stub staging base 0x620000 -- otherwise the stub's inflate destination
# (0x100000..0x100000+kSize) overlaps the staging/source region and corrupts
# the compressed stream (observed as inflate error -24).  Fixed in stub.s /
# entry.s / bare.cpp.  Fail loudly instead of producing a broken image.
$kEnd = 0x100000 + $kSize
if ($kEnd -ge 0x620000) {
    throw "kernel image ends at 0x$('{0:X}' -f $kEnd) >= 0x620000 (staging base): raise STAGE_BASE/stacks/ARENA_BASE"
}
Write-Output "memmap OK: kernel ends at 0x$('{0:X}' -f $kEnd) (< 0x620000 staging, < 0x900000 stacks, < 0x920000 arena)"

# 4b) patch the multiboot2 header (entry.s) so GRUB/ESP boot works:
#     Address-tag load_end_addr/bss_end_addr + in-kernel kernel_boot_params
#     (bss_start/bss_size) read by entry.s on both boot paths.
& $python3 "tools\patch_mb2.py" "$bareOut\kernel.bin" $kernelBssStart $kernelBssSize
if ($LASTEXITCODE -ne 0) { throw "patch_mb2 failed" }

# 4c) compressed payload: stub + gzip(kernel.bin).  The multi-MB kernel cannot
#     be loaded at 0x20000 directly (VGA/ROM hole 0xA0000-0xBFFFF + 16-bit
#     segment wrap + real-mode 1.06MB cap), so boot.s loads a ~507KB payload
#     (stub + raw-DEFLATE kernel) to 0x20000; the stub inflates the kernel to
#     0x100000.  kernel.bin itself stays uncompressed for the GRUB/ESP path.
$stubAs = "D:\CLion\bin\mingw\bin\as.exe"
$stubLd = "D:\CLion\bin\mingw\bin\ld.exe"
$stubGcc = "D:\CLion\bin\mingw\bin\gcc.exe"
& $stubAs --32 "backends\bare\stub.s" -o "$bareOut\stub.o"
if ($LASTEXITCODE -ne 0) { throw "stub.s failed" }
& $g -m32 -ffreestanding -fno-builtin -Os -c "backends\bare\inflate.cpp" -o "$bareOut\inflate32.o"
if ($LASTEXITCODE -ne 0) { throw "inflate.c (m32) failed" }
& $stubLd -mi386pe --image-base 0 -T "backends\bare\stub.ld" "$bareOut\stub.o" "$bareOut\inflate32.o" -o "$bareOut\stub.exe"
if ($LASTEXITCODE -ne 0) { throw "stub link failed" }
& $objcopy -O binary -j .text "$bareOut\stub.exe" "$bareOut\stub.bin"
if ($LASTEXITCODE -ne 0) { throw "stub objcopy failed" }
$stubLen = (Get-Item "$bareOut\stub.bin").Length
if ($stubLen -gt 0x2000) { throw "stub too large: $stubLen bytes" }
Write-Output "stub.bin OK: $stubLen bytes"

# compress kernel.bin -> kernel.gz (raw DEFLATE wbits=-15, zlib level 9)
& $python3 -c "import zlib; d=open(r'$bareOut\kernel.bin','rb').read(); c=zlib.compressobj(9,zlib.DEFLATED,-15); gz=c.compress(d)+c.flush(); open(r'$bareOut\kernel.gz','wb').write(gz); print('kernel.gz OK:',len(d),'->',len(gz))"
if ($LASTEXITCODE -ne 0) { throw "kernel.gz compress failed" }
$gzLen = (Get-Item "$bareOut\kernel.gz").Length
$gzCap = 0x200000   # decompression capacity (>= kernel.bin size)
if ($kSize -gt $gzCap) { $gzCap = $kSize }
# patch stub slots: +0x80 src_off = stub length, +0x84 src_len = gz size,
#                    +0x88 dst_cap
$sf = [IO.File]::OpenWrite("$bareOut\stub.bin")
$sf.Position = 0x80
$sb = [byte[]]::new(12)
[BitConverter]::GetBytes([uint32]$stubLen).CopyTo($sb, 0)
[BitConverter]::GetBytes([uint32]$gzLen).CopyTo($sb, 4)
[BitConverter]::GetBytes([uint32]$gzCap).CopyTo($sb, 8)
$sf.Write($sb, 0, 12)
$sf.Close()
# payload.bin = stub.bin + kernel.gz
$payloadBytes = [IO.File]::ReadAllBytes("$bareOut\stub.bin") + [IO.File]::ReadAllBytes("$bareOut\kernel.gz")
[IO.File]::WriteAllBytes("$bareOut\payload.bin", $payloadBytes)
$payloadLen = $payloadBytes.Length
Write-Output "payload.bin OK: $payloadLen bytes (stub $stubLen + gz $gzLen)"

# 5) boot sector ->
& $objcopy -O binary -j .text "$bareOut\boot.o" "$bareOut\boot.bin"
if ($LASTEXITCODE -ne 0) { throw "boot objcopy failed" }
$bootLen = (Get-Item "$bareOut\boot.bin").Length
if ($bootLen -ne 512) { throw "boot.bin size $bootLen != 512" }
$payloadSize = (Get-Item "$bareOut\payload.bin").Length
$kSectors = [Math]::Ceiling($payloadSize / 512)
$fs = [IO.File]::OpenWrite("$bareOut\boot.bin")
$fs.Position = 0x58   # kernel_count lives at file offset 0x58 (runtime 0x7C58)
$fs.WriteByte([byte]($kSectors -band 0xFF))
$fs.WriteByte([byte](($kSectors -shr 8) -band 0xFF))
# CD load is fully dynamic in boot.s now (single DAP at 0x7D60 rewritten
# per call); no static chunk patch needed here.
$fs.Close()
Write-Output "boot.bin patched: $kSectors kernel sectors (kernel_count@0x58)"

# 6) ISO閿涘湕l Torito no-emulation閿涙瓬oot.bin 閻╁瓨甯撮弨鎯ф躬 ISO LBA23閿涘ernel.bin 閺€?LBA24�?#    boot.s 闁俺绻?int13 0x42 �?CD 閻╁瓨甯寸拠璇插絿閳ユ柡鈧柧绗夌紒蹇氱�?floppy.img 娑擃參妫跨仦鍌︾礉鐟佸憡婧€閸欘垳娲块幒銉ユ儙閸旑煉�?
$python = "C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe"
if (-not (Test-Path $python)) { $python = "python" }
$isoOut = Join-Path $distOut "nefuOS.iso"
& $python "tools\make_iso.py" "$bareOut\boot.bin" "$bareOut\menu.bin" "$bareOut\payload.bin" $isoOut
if ($LASTEXITCODE -ne 0) { throw "make_iso failed" }
Write-Output "ISO OK: $((Get-Item $isoOut).Length) bytes"

# 7) ESP + GRUB (UEFI) 绾句胶娲忛梹婊冨剼閿涘牆褰查柅澶涚礉-GrubEsp閿涘�?
#    GPT 閸掑棗灏悰?+ FAT32 ESP閿涘苯鎯?BOOTX64.EFI閿涘湙RUB閿涘鈧宫rub.cfg閵嗕礁鍙忛柈?GRUB 濡€虫健閸?#    �?multiboot2 婢跺娈?kernel.bin閵嗕敬RUB 闁俺绻?multiboot2 閸楀繗顔呴崝鐘烘祰閸愬懏鐗抽敍灞藉敶閺嶆瓕鍤滅悰?#    鐟欙絾鐎?framebuffer 閺嶅洨顒烽獮璺虹紦缁斿鍨庢�?闂€鎸幠佸蹇ョ礄鐟?backends/bare/entry.s閿涘鈧?
if ($GrubEsp) {
    $grubRoot = Join-Path $root "tools\grub_toolchain"
    $grubCore = Join-Path $grubRoot "extracted\usr\lib\grub\x86_64-efi\monolithic\grubx64.efi"
    $grubMods = Join-Path $grubRoot "extracted\usr\lib\grub\x86_64-efi"
    $grubCfg = Join-Path $root "tools\grub.cfg"
    if (-not (Test-Path $grubCore)) {
        Write-Output "GRUB toolchain missing �?fetching (tools\fetch_grub_toolchain.py) ..."
        & $python "tools\fetch_grub_toolchain.py"
        if ($LASTEXITCODE -ne 0) { throw "fetch_grub_toolchain failed" }
    }
    if (-not (Test-Path $grubCore)) { throw "grubx64.efi still missing after fetch" }
    $espOut = Join-Path $distOut "nefuOS_esp.img"
    & $python "tools\make_esp.py" $grubCore $grubCfg "$bareOut\kernel.bin" $grubMods $espOut
    if ($LASTEXITCODE -ne 0) { throw "make_esp failed" }
    New-Item -ItemType Directory -Force -Path "dist" | Out-Null
    Copy-Item $espOut "dist\nefuOS_esp.img" -Force
    Write-Output "ESP+GRUB image OK: dist\nefuOS_esp.img ($((Get-Item 'dist\nefuOS_esp.img').Length) bytes)"
}

Write-Output "BUILD DONE"

