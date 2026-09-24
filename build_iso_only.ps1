# nefuOS bare-metal ISO only build (skip host exe)
$ErrorActionPreference = "Stop"
$root = "D:\mycppos1\nefuOS"
$g = "D:\CLion\bin\mingw\bin\g++.exe"
$as = "D:\CLion\bin\mingw\bin\as.exe"
$ld = "D:\CLion\bin\mingw\bin\ld.exe"
$objcopy = "D:\CLion\bin\mingw\bin\objcopy.exe"
Set-Location $root

$python3 = "C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe"
if (-not (Test-Path $python3)) { $python3 = "python" }
$adminPw = [Environment]::GetEnvironmentVariable("NEFU_ADMIN_PASSWORD")
& $python3 "tools\admin_hash.py" $adminPw | Out-Null
if ($LASTEXITCODE -ne 0) { throw "admin hash gen failed" }

$coreSrc = @(
  "core\nefuos.cpp", "core\klib\memory.cpp", "core\klib\string.cpp", "core\klib\printf.cpp",
  "core\vfs\vfs.cpp", "core\vfs\nvfs.cpp", "core\gui\gfx.cpp", "core\gui\wm.cpp", "core\gui\widgets.cpp", "core\gui\desktop.cpp",
  "core\apps\apps.cpp", "core\apps\terminal.cpp", "core\apps\filemgr.cpp", "core\apps\calc.cpp",
  "core\apps\textview.cpp", "core\apps\sysinfo.cpp", "core\apps\settings.cpp", "core\apps\store.cpp",
  "core\apps\snake.cpp", "core\apps\paint.cpp", "core\apps\clock.cpp", "core\apps\notepad.cpp",
  "core\apps\minesweep.cpp", "core\apps\imageviewer.cpp", "core\apps\music.cpp", "core\apps\videoplayer.cpp", "core\apps\monitor.cpp",
  "core\apps\browser.cpp", "core\apps\netcfg.cpp",
  "core\apps\nefvm.cpp", "core\apps\nefud.cpp", "core\apps\jpeg.cpp", "core\net\net.cpp",
  "core\gui\ttfont.cpp", "core\apps\fontview.cpp", "core\apps\lvgl_demo.cpp",
  "core\gui\lvgl_win.cpp",
  "core\apps\lvgl_desktop.cpp",
  "core\sys\settings.cpp", "core\sys\sha256.cpp", "core\sys\power.cpp",
  "core\apps\wiki.cpp",
  "core\audio.cpp",
  "third_party\stb_image_wrap.cpp"
)

$gcc = "D:\CLion\bin\mingw\bin\gcc.exe"
$lvglRoot = "third_party\lvgl_src\lvgl-9.2.0"
$lvglSrc = Get-ChildItem (Join-Path $lvglRoot "src") -Recurse -Filter *.c | ForEach-Object { $_.FullName }
$lvConfPath = (Resolve-Path "third_party\lvgl_conf\lv_conf.h").Path -replace '\\', '/'
$lvglInclude = @("-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
                 "-D", ("LV_CONF_PATH=" + $lvConfPath))

$bareOut = Join-Path $env:TEMP "nefu_build\bare"
$distOut = Join-Path $env:TEMP "nefu_dist"
New-Item -ItemType Directory -Force -Path $bareOut | Out-Null
New-Item -ItemType Directory -Force -Path $distOut | Out-Null

# 2) bare kernel compile
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
& $g @bareFlags "backends\bare\softfloat.cpp" -o "$bareOut\bare_softfloat.o"
if ($LASTEXITCODE -ne 0) { throw "softfloat.cpp failed" }
$objs += "$bareOut\bare_softfloat.o"

# 3) boot sector + kernel entry + multi-boot picker
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

$objs = @("$bareOut\entry.o") + $objs

# 4) link
& $ld -mi386pep --image-base 0x20000 --gc-sections -T "backends\bare\linker.ld" -o "$bareOut\kernel.exe" -Map "$bareOut\kernel.map" $objs
if ($LASTEXITCODE -ne 0) { throw "link failed" }

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
            $vs = $br.ReadUInt32()
            $va = $br.ReadUInt32()
            $rawSize = $br.ReadUInt32()
            $rawPtr = $br.ReadUInt32()
            $rva = $va
            $secs += [pscustomobject]@{ Name = $name; RVA = $rva; RawSize = $rawSize; RawPtr = $rawPtr; VSize = $vs }
        }
        $maxEnd = 0
        $bssRva = 0
        $bssVSize = 0
        foreach ($s in $secs) {
            if ($s.Name -eq ".bss") { $bssRva = $s.RVA; $bssVSize = $s.VSize }
            if ($s.Name -eq ".reloc") { continue }
            if ($s.RawSize -eq 0) { continue }
            $end = $s.RVA + $s.RawSize
            if ($end -gt $maxEnd) { $maxEnd = $end }
        }
        if ($bssVSize -gt 0 -and $bssRva -gt 0) {
            Set-Variable -Name kernelBssStart -Value ($bssRva + 0x20000) -Scope Script
            Set-Variable -Name kernelBssSize -Value $bssVSize -Scope Script
        } elseif ($bssRva -gt 0) {
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
$isoKernelMax = 1024 * 2048
if ($kSize -gt $isoKernelMax) { throw "kernel too large for ISO (max $isoKernelMax bytes, got $kSize)" }

# 5) boot sector
& $objcopy -O binary -j .text "$bareOut\boot.o" "$bareOut\boot.bin"
if ($LASTEXITCODE -ne 0) { throw "boot objcopy failed" }
$bootLen = (Get-Item "$bareOut\boot.bin").Length
if ($bootLen -ne 512) { throw "boot.bin size $bootLen != 512" }
$kSectors = [Math]::Ceiling($kSize / 512)
$fs = [IO.File]::OpenWrite("$bareOut\boot.bin")
$fs.Position = 0x58
$fs.WriteByte([byte]($kSectors -band 0xFF))
$fs.WriteByte([byte](($kSectors -shr 8) -band 0xFF))
$fs.Position = 0x1C0
$fs.Write([BitConverter]::GetBytes([uint32]$kernelBssStart), 0, 4)
$fs.Write([BitConverter]::GetBytes([uint32]$kernelBssSize), 0, 4)
$fs.Close()
Write-Output "boot.bin patched: $kSectors kernel sectors"

# 6) ISO (El Torito no-emulation)
$isoOut = Join-Path $distOut "nefuOS.iso"
& $python3 "tools\make_iso.py" "$bareOut\boot.bin" "$bareOut\menu.bin" "$bareOut\kernel.bin" $isoOut
if ($LASTEXITCODE -ne 0) { throw "make_iso failed" }
Write-Output "ISO OK: $((Get-Item $isoOut).Length) bytes"

# copy to dist folder
$distIso = "D:\mycppos1\nefuOS\dist\nefuOS.iso"
Copy-Item $isoOut $distIso -Force
Write-Output "ISO copied to dist: $((Get-Item $distIso).Length) bytes"
Write-Output "BUILD DONE"
