# nefuOS 构建脚本：宿主 exe + 裸机内核 + 软盘镜像 + ISO
$ErrorActionPreference = "Stop"
$root = "D:\mycppos1\nefuOS"
$g = "D:\CLion\bin\mingw\bin\g++.exe"
$as = "D:\CLion\bin\mingw\bin\as.exe"
$ld = "D:\CLion\bin\mingw\bin\ld.exe"
$objcopy = "D:\CLion\bin\mingw\bin\objcopy.exe"
$xorriso = "C:\msys64\usr\bin\xorriso.exe"
Set-Location $root

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

# 1) 宿主版（win32）
& $g -std=c++17 -O2 -fno-exceptions -fno-rtti -fno-builtin -Wall -Wextra -Wno-sized-deallocation -I core -o dist\nefuOS.exe ($coreSrc + @("backends\win32\win32.cpp")) -lgdi32 -luser32 -lgdiplus -lole32 -lws2_32 -liphlpapi -lwlanapi
if ($LASTEXITCODE -ne 0) { throw "host build failed" }
Write-Output "host exe OK: $((Get-Item dist\nefuOS.exe).Length) bytes"

# 2) 裸机内核对象
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

# 3) 汇编：引导扇区 + 内核入口
& $as "backends\bare\boot.s" -o "$bareOut\boot.o"
if ($LASTEXITCODE -ne 0) { throw "boot.s failed" }
& $as "backends\bare\entry.s" -o "$bareOut\entry.o"
if ($LASTEXITCODE -ne 0) { throw "entry.s failed" }

# 入口必须链接在首位：0x20000 处必须是 kernel_start
$objs = @("$bareOut\entry.o") + $objs

# 4) 链接（PE 中间体）-> 手动重排为平面二进制
& $ld -mi386pep --image-base 0x20000 -T "backends\bare\linker.ld" -o "$bareOut\kernel.exe" -Map "$bareOut\kernel.map" $objs
if ($LASTEXITCODE -ne 0) { throw "link failed" }
# mingw ld 的 PE 输出有布局怪癖：首个 section(.text) 的 VirtualAddress = 绝对VMA - image_base(=0)，
# 其余 section 的 VirtualAddress = 绝对VMA。若直接用 objcopy -O binary，后段会按绝对VMA 排布，
# 加载到 0x20000 后 .rdata/.data/.bss 整体错位 0x20000（字符串/全局数据读到空洞，桌面空白）。
# 修复：解析 PE section 表，把每个 section 按 RVA(绝对VMA-0x20000) 重排输出，bss 区自动为 0。
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
            # PE section VirtualAddress 是相对 image-base(0x20000) 的 RVA：
            # 加载到 0x20000 后，文件偏移 = RVA（.text VA=0 -> 0x20000 处）。
            # 注意：不要对 >= 0x20000 的 VA 再减 0x20000（会误伤 .pdata/.data 等
            # 排在 0x40000 以上的段，覆盖 .text 入口）。
            $rva = $va
            $secs += [pscustomobject]@{ Name = $name; RVA = $rva; RawSize = $rawSize; RawPtr = $rawPtr; VSize = $vs }
        }
        $maxEnd = 0
        foreach ($s in $secs) {
            $end = $s.RVA + $s.RawSize
            if ($end -gt $maxEnd) { $maxEnd = $end }
            $bssEnd = $s.RVA + $s.VSize
            if ($bssEnd -gt $maxEnd) { $maxEnd = $bssEnd }
        }
        $out = New-Object byte[] $maxEnd
        foreach ($s in $secs) {
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

# 5) 引导扇区 -> 回填内核扇区数
& $objcopy -O binary -j .text "$bareOut\boot.o" "$bareOut\boot.bin"
if ($LASTEXITCODE -ne 0) { throw "boot objcopy failed" }
$bootLen = (Get-Item "$bareOut\boot.bin").Length
if ($bootLen -ne 512) { throw "boot.bin size $bootLen != 512" }
$kSectors = [Math]::Ceiling($kSize / 512)
$fs = [IO.File]::OpenWrite("$bareOut\boot.bin")
# 槽位 0x58（0x7C58，安全区）：读取用
$fs.Position = 0x58
$fs.Position = 0x58
$fs.WriteByte([byte]($kSectors -band 0xFF))
$fs.WriteByte([byte](($kSectors -shr 8) -band 0xFF))
# 注意：不再回填 0xFC/0x194 —— 0xFC 落在 floppy CHS 的 jb 指令上会破坏引导，
# DAP count 由 boot.s 运行时动态计算（kernel_count@0x7C58 -> 2048B 扇区数）。
# cdap4 count @0x172: remaining kernel bytes beyond 0x30000, in 2048B CD sectors.
# Reading past the ISO end (LBA150+) makes ATAPI silently fail and leaves
# .data (at 0x50020+) zeroed -> kalloc from address 0 -> page-table corruption.
$kTail = [Math]::Max(0, $kSize - 196608)
$cdap4Count = [Math]::Max(1, [Math]::Ceiling($kTail / 2048))
$fs.Position = 0x1A2
$fs.WriteByte([byte]($cdap4Count -band 0xFF))
$fs.WriteByte([byte](($cdap4Count -shr 8) -band 0xFF))
$fs.Close()
Write-Output "boot.bin patched: $kSectors kernel sectors (kernel_count@0x58), cdap4=$cdap4Count"

# 6) 软盘镜像（1.44MB，2880 扇区）
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

# 7) ISO（El Torito no-emulation：SeaBIOS 只读 boot.s，内核由 boot.s 经 int13 0x42 直读）
$python = "C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe"
if (-not (Test-Path $python)) { $python = "python" }
& $python "tools\make_iso.py" "$bareOut\boot.bin" "$bareOut\kernel.bin" "dist\nefuOS_v2.iso"
if ($LASTEXITCODE -ne 0) { throw "make_iso failed" }
Write-Output "ISO OK: $((Get-Item dist\nefuOS_v2.iso).Length) bytes"
Write-Output "BUILD DONE"
