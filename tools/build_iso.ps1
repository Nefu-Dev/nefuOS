# nefuOS ： exe + + + ISO
$ErrorActionPreference = "Stop"
$root = "D:\mycppos1\nefuOS"
$g = "D:\CLion\bin\mingw\bin\g++.exe"
$as = "D:\CLion\bin\mingw\bin\as.exe"
$ld = "D:\CLion\bin\mingw\bin\ld.exe"
$objcopy = "D:\CLion\bin\mingw\bin\objcopy.exe"
$xorriso = "C:\msys64\usr\bin\xorriso.exe"
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
  "core\sys\settings.cpp"
)

# 1) （win32）
& $g -std=c++17 -O2 -fno-exceptions -fno-rtti -fno-builtin -Wall -Wextra -Wno-sized-deallocation -I core -o dist\nefuOS.exe ($coreSrc + @("backends\win32\win32.cpp")) -lgdi32 -luser32 -lgdiplus -lole32 -lws2_32 -liphlpapi -lwlanapi -lwininet -lwinmm -lwinmm
if ($LASTEXITCODE -ne 0) { throw "host build failed" }
Write-Output "host exe OK: $((Get-Item dist\nefuOS.exe).Length) bytes"

# 2)
$bareFlags = @(
  "-std=c++17", "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-builtin",
  "-fno-stack-protector", "-mno-red-zone", "-mgeneral-regs-only", "-O2", "-Wall", "-Wextra",
  "-Wno-sized-deallocation", "-I", "core", "-c"
)
$objs = @()
$bareOut = "build\bare"
New-Item -ItemType Directory -Force -Path $bareOut | Out-Null
foreach ($s in $coreSrc) {
  $name = $s -replace '[\\/]', '_' -replace '\.cpp$', '.o'
  $obj = "$bareOut\$name"
  & $g @bareFlags $s -o $obj
  if ($LASTEXITCODE -ne 0) { throw "bare compile failed: $s" }
  $objs += $obj
}
& $g @bareFlags "backends\bare\bare.cpp" -o "$bareOut\bare_bare.o"
if ($LASTEXITCODE -ne 0) { throw "bare.cpp failed" }
$objs += "$bareOut\bare_bare.o"

# 3) ：boot sector + kernel entry
& $as "backends\bare\boot.s" -o "$bareOut\boot.o"
if ($LASTEXITCODE -ne 0) { throw "boot.s failed" }
& $as "backends\bare\entry.s" -o "$bareOut\entry.o"
if ($LASTEXITCODE -ne 0) { throw "entry.s failed" }

# ：0x20000 kernel_start
$objs = @("$bareOut\entry.o") + $objs

# 4) link（PE ）->
& $ld -mi386pep --image-base 0x20000 -T "backends\bare\linker.ld" -o "$bareOut\kernel.exe" -Map "$bareOut\kernel.map" $objs
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
        foreach ($s in $secs) {
            # .bss has no raw data (zeroed by entry.s at runtime); .reloc is
            # not needed for a flat kernel image. Skip both so kernel.bin
            # stays compact (no 42KB zero padding).
            if ($s.Name -eq ".reloc") { continue }
            if ($s.RawSize -eq 0) { continue }
            $end = $s.RVA + $s.RawSize
            if ($end -gt $maxEnd) { $maxEnd = $end }
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
$kSize = (Get-Item "$bareOut\kernel.bin").Length
Write-Output "kernel.bin OK: $kSize bytes"
if ($kSize -gt 720000) { throw "kernel too large for floppy image" }

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
# cdap4 count @0x1A2: remaining kernel bytes beyond 0x50000, in 2048B CD
# sectors. Chunks 1-3 are fixed 32 sectors (LBA24..119 -> 0x20000..0x50000);
# chunk 4 (LBA120, seg 0x5000) is capped at 32 (int13 EDD 64KB limit).
$kTotal = [Math]::Ceiling($kSize / 2048)
$cdap4Count = $kTotal - 96
if ($cdap4Count -lt 0) { $cdap4Count = 0 }
if ($cdap4Count -gt 32) { $cdap4Count = 32 }
$fs.Position = 0x1A2
$fs.WriteByte([byte]($cdap4Count -band 0xFF))
$fs.WriteByte([byte](($cdap4Count -shr 8) -band 0xFF))
$fs.Close()
Write-Output "boot.bin patched: $kSectors kernel sectors (kernel_count@0x58), cdap4=$cdap4Count"

# 6) （1.44MB，2880 sectors）
$floppy = "$bareOut\floppy.img"
$fs = [IO.File]::Create($floppy)
$fs.SetLength(1474560)
$fs.Close()
$fs = [IO.File]::OpenWrite($floppy)
$boot = [IO.File]::ReadAllBytes("$bareOut\boot.bin")
$fs.Write($boot, 0, 512)
$kernel = [IO.File]::ReadAllBytes("$bareOut\kernel.bin")
$fs.Position = 512
$fs.Write($kernel, 0, $kernel.Length)
$fs.Close()
Write-Output "floppy.img OK"

# 7) ISO（El Torito no-emulation：SeaBIOS read-only boot.s， boot.s int13 0x42 ）
$python = "C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe"
if (-not (Test-Path $python)) { $python = "python" }
& $python "tools\make_iso.py" "$bareOut\boot.bin" "$bareOut\kernel.bin" "dist\nefuOS_v2.iso"
if ($LASTEXITCODE -ne 0) { throw "make_iso failed" }
Write-Output "ISO OK: $((Get-Item dist\nefuOS_v2.iso).Length) bytes"
Write-Output "BUILD DONE"
