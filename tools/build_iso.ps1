# nefuOS ： exe + + + ISO（-GrubEsp 时额外生成 ESP+GRUB 磁盘镜像）
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

$coreSrc = @(
  "core\nefuos.cpp", "core\klib\memory.cpp", "core\klib\string.cpp", "core\klib\printf.cpp",
  "core\vfs\vfs.cpp", "core\gui\gfx.cpp", "core\gui\wm.cpp", "core\gui\widgets.cpp", "core\gui\desktop.cpp",
  "core\apps\apps.cpp", "core\apps\terminal.cpp", "core\apps\filemgr.cpp", "core\apps\calc.cpp",
  "core\apps\textview.cpp", "core\apps\sysinfo.cpp", "core\apps\settings.cpp", "core\apps\store.cpp",
  "core\apps\snake.cpp", "core\apps\paint.cpp", "core\apps\clock.cpp", "core\apps\notepad.cpp",
  "core\apps\minesweep.cpp", "core\apps\imageviewer.cpp", "core\apps\music.cpp", "core\apps\monitor.cpp",
  "core\apps\browser.cpp", "core\apps\netcfg.cpp",
  "core\apps\nefvm.cpp", "core\apps\nefud.cpp", "core\apps\jpeg.cpp", "core\net\net.cpp",
  "core\gui\ttfont.cpp", "core\apps\fontview.cpp", "core\apps\lvgl_demo.cpp",
  "core\gui\lvgl_win.cpp",
  "core\apps\lvgl_desktop.cpp",
  "core\sys\settings.cpp", "core\sys\sha256.cpp",
  "core\apps\wiki.cpp",
  "core\sys\power.cpp",
  "core\apps\bios.cpp",
  "core\audio.cpp",
  "third_party\stb_image_wrap.cpp"
)

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

# 1) （win32）
$bareOut = Join-Path $env:TEMP "nefu_build\bare"
$distOut = Join-Path $env:TEMP "nefu_dist"
New-Item -ItemType Directory -Force -Path $bareOut | Out-Null
New-Item -ItemType Directory -Force -Path $distOut | Out-Null
$hostExe = Join-Path $distOut "nefuOS.exe"
# LVGL is pure C: compile with gcc first, then link via g++ with the core
$lvglHostObjs = @()
foreach ($lv in $lvglSrc) {
  $lname = ($lv -replace '.*\\src\\', 'lvgl_h_') -replace '[\\/]', '_' -replace '\.c$', '.o'
  $lobj = "$bareOut\$lname"
  if (-not (Test-Path $lobj)) {
    & $gcc @lvglCFlags $lv -o $lobj
    if ($LASTEXITCODE -ne 0) { throw "lvgl host compile failed: $lv" }
  }
  $lvglHostObjs += $lobj
}
$linkErr = Join-Path $env:TEMP "nefu_link.log"
& $g -std=c++17 -O2 -fno-exceptions -fno-rtti -fno-builtin -Wall -Wextra -Wno-sized-deallocation -I core -I third_party @lvglInclude -o $hostExe ($coreSrc + @("backends\win32\win32.cpp")) $lvglHostObjs -lgdi32 -luser32 -lgdiplus -lole32 -lws2_32 -liphlpapi -lwlanapi -lwininet -lwinmm -lwinmm 2> $linkErr
if ($LASTEXITCODE -ne 0) { throw "host build failed" }
Write-Output "host exe OK: $((Get-Item $hostExe).Length) bytes"

# 2)
$bareFlags = @(
  "-std=c++17", "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-builtin",
  "-fno-stack-protector", "-mno-red-zone", "-mgeneral-regs-only", "-O2", "-Wall", "-Wextra",
  "-ffunction-sections", "-fdata-sections",
  "-Wno-sized-deallocation", "-DNEFU_BARE", "-I", "core", "-I", "third_party",
  "-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
  "-D", ('LV_CONF_PATH=' + $lvConfPath), "-c"
)
$objs = @()
foreach ($s in $coreSrc) {
  $name = $s -replace '[\\/]', '_' -replace '\.cpp$', '.o'
  $obj = "$bareOut\$name"
  & $g @bareFlags $s -o $obj
  if ($LASTEXITCODE -ne 0) { throw "bare compile failed: $s" }
  $objs += $obj
}
# LVGL 9.2.0 bare objects (pure C, compiled with gcc -mgeneral-regs-only so
# any float math resolves to the kernel soft-float library)
$lvglBareFlags = @("-std=gnu11", "-ffreestanding", "-fno-stack-protector", "-mno-red-zone",
                   "-mgeneral-regs-only", "-O2", "-Wall", "-ffunction-sections", "-fdata-sections",
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
& $g @bareFlags "backends\bare\bare.cpp" -o "$bareOut\bare_bare.o"
if ($LASTEXITCODE -ne 0) { throw "bare.cpp failed" }
$objs += "$bareOut\bare_bare.o"
# minimal soft-float (IEEE-754 single precision) for the bare kernel
& $g @bareFlags "backends\bare\softfloat.cpp" -o "$bareOut\bare_softfloat.o"
if ($LASTEXITCODE -ne 0) { throw "softfloat.cpp failed" }
$objs += "$bareOut\bare_softfloat.o"

# 3) ：boot sector + kernel entry
& $as "backends\bare\boot.s" -o "$bareOut\boot.o"
if ($LASTEXITCODE -ne 0) { throw "boot.s failed" }
& $as "backends\bare\entry.s" -o "$bareOut\entry.o"
if ($LASTEXITCODE -ne 0) { throw "entry.s failed" }

# ：0x20000 kernel_start
$objs = @("$bareOut\entry.o") + $objs

# 4) link（PE ）->
& $ld -mi386pep --image-base 0x20000 --gc-sections -T "backends\bare\linker.ld" -o "$bareOut\kernel.exe" -Map "$bareOut\kernel.map" $objs
if ($LASTEXITCODE -ne 0) { throw "link failed" }
# mingw ld of PE ： section(.text) of VirtualAddress = absoluteVMA - image_base(=0)，
# rest section of VirtualAddress = absoluteVMA。 objcopy -O binary，VMA ，
# loaded to 0x20000 after .rdata/.data/.bss 0x20000（string/，）。
# ：parse PE section ， section by RVA(absoluteVMA-0x20000) ，bss 0。
function Rebin-Kernel {
    param([string]$InExe, [string]$OutBin)
    $fs = [IO.File]::OpenRead($InExe)
    $br = New-Object IO.BinaryReader($fs)
    try {
        $fs.Seek(0x3C, 0) | Out-Null
        $peOff = $br.ReadInt32()
        $fs.Seek($peOff + 6, 0) | Out-Null
        $numSec = $br.ReadUInt16()
        $fs.Seek($peOff + 20, 0) | Out-Null
        $optSize = $br.ReadUInt16()
        $secTab = $peOff + 24 + $optSize
        $secs = @()
        for ($i = 0; $i -lt $numSec; $i++) {
            $fs.Seek($secTab + $i * 40, 0) | Out-Null
            $nameBytes = $br.ReadBytes(8)
            $name = ([Text.Encoding]::ASCII.GetString($nameBytes)).Trim([char]0)
            $vs = $br.ReadUInt32()      # VirtualSize
            $va = $br.ReadUInt32()      # VirtualAddress
            $rawSize = $br.ReadUInt32() # SizeOfRawData
            $rawPtr = $br.ReadUInt32()  # PointerToRawData
            # PE section VirtualAddress image-base(0x20000) of RVA：
            # loaded to 0x20000 after， = RVA（.text VA=0 -> 0x20000 ）。
            # note： >= 0x20000 of VA 0x20000（ .pdata/.data
            # 0x40000 ， .text entry）。
            $rva = $va
            $secs += [pscustomobject]@{ Name = $name; RVA = $rva; RawSize = $rawSize; RawPtr = $rawPtr; VSize = $vs }
        }
        $maxEnd = 0
        $bssRva = 0
        $bssVSize = 0
        foreach ($s in $secs) {
            if ($s.Name -eq ".bss") {
                $bssRva = $s.RVA
                $bssVSize = $s.VSize
            }
            # .bss has no raw data (zeroed by entry.s at runtime); .reloc is
            # not needed for a flat kernel image. Skip both so kernel.bin
            # stays compact (no 42KB zero padding).
            if ($s.Name -eq ".reloc") { continue }
            if ($s.RawSize -eq 0) { continue }
            $end = $s.RVA + $s.RawSize
            if ($end -gt $maxEnd) { $maxEnd = $end }
        }
        if ($bssVSize -gt 0 -and $bssRva -gt 0) {
            # bss runtime address = load base (0x20000) + RVA. Store it with
            # the size so entry.s can zero-fill the correct region.
            Set-Variable -Name kernelBssStart -Value ($bssRva + 0x20000) -Scope Script
            Set-Variable -Name kernelBssSize -Value $bssVSize -Scope Script
        } elseif ($bssRva -gt 0) {
            # mingw ld may leave VirtualSize=0 for .bss; derive the size from
            # the next section's VirtualAddress instead.
            $next = 0x7FFFFFFF
            foreach ($s2 in $secs) {
                if ($s2.RVA -gt $bssRva -and $s2.RVA -lt $next) { $next = $s2.RVA }
            }
            if ($next -eq 0x7FFFFFFF) { $next = $maxEnd }
            Set-Variable -Name kernelBssStart -Value ($bssRva + 0x20000) -Scope Script
            Set-Variable -Name kernelBssSize -Value ($next - $bssRva) -Scope Script
        }
        $out = New-Object byte[] $maxEnd
        foreach ($s in $secs) {
            if ($s.Name -eq ".reloc") { continue }
            if ($s.RawSize -eq 0 -or $s.RawPtr -eq 0) { continue }
            $fs.Seek($s.RawPtr, 0) | Out-Null
            $data = $br.ReadBytes($s.RawSize)
            [Array]::Copy($data, 0, $out, $s.RVA, $s.RawSize)
        }
        [IO.File]::WriteAllBytes($OutBin, $out)
    } finally {
        $fs.Close()
    }
}
Rebin-Kernel "$bareOut\kernel.exe" "$bareOut\kernel.bin"
if (-not $kernelBssStart) { $kernelBssStart = 0x93020 }
if (-not $kernelBssSize) { $kernelBssSize = 0 }
$kSize = (Get-Item "$bareOut\kernel.bin").Length
Write-Output "kernel.bin OK: $kSize bytes"
$isoKernelMax = 1024 * 2048   # ISO layout: kernel occupies up to 1024 x 2048B blocks at LBA24
if ($kSize -gt $isoKernelMax) { throw "kernel too large for ISO (max $isoKernelMax bytes, got $kSize)" }

# 4b) patch the multiboot2 header (entry.s) so GRUB/ESP boot works:
#     Address-tag load_end_addr/bss_end_addr + in-kernel kernel_boot_params
#     (bss_start/bss_size) read by entry.s on both boot paths.
& $python3 "tools\patch_mb2.py" "$bareOut\kernel.bin" $kernelBssStart $kernelBssSize
if ($LASTEXITCODE -ne 0) { throw "patch_mb2 failed" }

# 5) boot sector ->
& $objcopy -O binary -j .text "$bareOut\boot.o" "$bareOut\boot.bin"
if ($LASTEXITCODE -ne 0) { throw "boot objcopy failed" }
$bootLen = (Get-Item "$bareOut\boot.bin").Length
if ($bootLen -ne 512) { throw "boot.bin size $bootLen != 512" }
$kSectors = [Math]::Ceiling($kSize / 512)
$fs = [IO.File]::OpenWrite("$bareOut\boot.bin")
# 0x58（0x7C58，）：
$fs.Position = 0x58
$fs.WriteByte([byte]($kSectors -band 0xFF))
$fs.WriteByte([byte](($kSectors -shr 8) -band 0xFF))
# CD load is fully dynamic in boot.s now (single DAP at 0x7D60 rewritten
# per call); no static chunk patch needed here.
$fs.Close()
Write-Output "boot.bin patched: $kSectors kernel sectors (kernel_count@0x58)"

# 6) ISO（El Torito no-emulation：boot.bin 直接放在 ISO LBA23，kernel.bin 放 LBA24，
#    boot.s 通过 int13 0x42 从 CD 直接读取——不经过 floppy.img 中间层，裸机可直接启动）
$python = "C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe"
if (-not (Test-Path $python)) { $python = "python" }
$isoOut = Join-Path $distOut "nefuOS.iso"
& $python "tools\make_iso.py" "$bareOut\boot.bin" "$bareOut\kernel.bin" $isoOut
if ($LASTEXITCODE -ne 0) { throw "make_iso failed" }
Write-Output "ISO OK: $((Get-Item $isoOut).Length) bytes"

# 7) ESP + GRUB (UEFI) 磁盘镜像（可选，-GrubEsp）：
#    GPT 分区表 + FAT32 ESP，含 BOOTX64.EFI（GRUB）、grub.cfg、全部 GRUB 模块和
#    带 multiboot2 头的 kernel.bin。GRUB 通过 multiboot2 协议加载内核，内核自行
#    解析 framebuffer 标签并建立分页/长模式（见 backends/bare/entry.s）。
if ($GrubEsp) {
    $grubRoot = Join-Path $root "tools\grub_toolchain"
    $grubCore = Join-Path $grubRoot "extracted\usr\lib\grub\x86_64-efi\monolithic\grubx64.efi"
    $grubMods = Join-Path $grubRoot "extracted\usr\lib\grub\x86_64-efi"
    $grubCfg = Join-Path $root "tools\grub.cfg"
    if (-not (Test-Path $grubCore)) {
        Write-Output "GRUB toolchain missing — fetching (tools\fetch_grub_toolchain.py) ..."
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

