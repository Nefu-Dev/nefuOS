# nefuOS: headless NVFS unit test build.
# Compiles nvfs.cpp + vfs.cpp + klib with malloc-backed stubs, runs the
# functional suite (format/mount, journal recovery, tree round-trip).
$ErrorActionPreference = "Stop"
$root = "D:\mycppos1\nefuOS"
$g = "D:\CLion\bin\mingw\bin\g++.exe"
Set-Location $root

$out = Join-Path $env:TEMP "nefu_build\nvfs_test\nvfs_test.exe"
New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null

& $g -std=c++17 -O2 -fno-exceptions -fno-rtti -fno-builtin -Wall -Wextra -Wno-sized-deallocation -I core `
  "tests\nvfs_test.cpp" "tests\nvfs_stubs.cpp" `
  "core\vfs\nvfs.cpp" "core\vfs\vfs.cpp" `
  "core\klib\memory.cpp" "core\klib\string.cpp" "core\klib\printf.cpp" `
  -o $out
if ($LASTEXITCODE -ne 0) { throw "nvfs test build failed" }

Write-Output "built: $out"
& $out
exit $LASTEXITCODE
