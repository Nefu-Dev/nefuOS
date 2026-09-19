# nefuOS: host-only (win32) build for quick verification.
# Outputs to $env:TEMP\nefu_fix_test\nefuOS.exe (does NOT touch dist\ or nefu_dist).
$ErrorActionPreference = "Stop"
$root = "D:\mycppos1\nefuOS"
$g = "D:\CLion\bin\mingw\bin\g++.exe"
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
  "core\gui\ttfont.cpp", "core\apps\fontview.cpp", "core\apps\lvgl_demo.cpp",
  "core\gui\lvgl_win.cpp",
  "core\apps\lvgl_desktop.cpp",
  "core\sys\settings.cpp", "core\sys\sha256.cpp",
  "core\apps\wiki.cpp",
  "core\audio.cpp",
  "third_party\stb_image_wrap.cpp"
)
$lvConfPath = (Resolve-Path "third_party\lvgl_conf\lv_conf.h").Path -replace '\\', '/'
$lvglInclude = @("-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
                 "-D", ("LV_CONF_PATH=" + $lvConfPath))
$bareOut = Join-Path $env:TEMP "nefu_build\bare"
$distOut = Join-Path $env:TEMP "nefu_fix_test"
New-Item -ItemType Directory -Force -Path $distOut | Out-Null
$hostExe = Join-Path $distOut "nefuOS.exe"
$lvglHostObjs = Get-ChildItem "$bareOut\lvgl_h_*.o" -File | ForEach-Object { $_.FullName }
if ($lvglHostObjs.Count -lt 100) { throw "lvgl host objects missing ($($lvglHostObjs.Count))" }
& $g -std=c++17 -O2 -fno-exceptions -fno-rtti -fno-builtin -Wall -Wextra -Wno-sized-deallocation -I core -I third_party @lvglInclude -o $hostExe ($coreSrc + @("backends\win32\win32.cpp")) $lvglHostObjs -lgdi32 -luser32 -lgdiplus -lole32 -lws2_32 -liphlpapi -lwlanapi -lwininet -lwinmm
if ($LASTEXITCODE -ne 0) { throw "host build failed" }
Write-Output "host exe OK: $((Get-Item $hostExe).Length) bytes -> $hostExe"
