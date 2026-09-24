# Build & run the headless mail-app logic test (tests\mail_logic_test.cpp).
# Usage:  powershell -ExecutionPolicy Bypass -File tests\build_mail_test.ps1
$ErrorActionPreference = "Stop"
$root = "D:\mycppos1\nefuOS"
$g = "D:\CLion\bin\mingw\bin\g++.exe"
Set-Location $root

$lvConfPath = (Resolve-Path "third_party\lvgl_conf\lv_conf.h").Path -replace '\\', '/'
$srcs = @(
  "tests\mail_logic_test.cpp", "tests\mail_stubs.cpp",
  "core\klib\memory.cpp", "core\klib\string.cpp", "core\klib\printf.cpp",
  "core\vfs\vfs.cpp", "core\gui\gfx.cpp"
)
& $g -std=c++17 -O2 -fno-exceptions -fno-rtti -fno-builtin -Wall -Wextra `
    -Wno-sized-deallocation -DNEFU_BARE `
    -I core -I third_party -I third_party\lvgl_conf -I third_party\lvgl_src\lvgl-9.2.0 `
    -D ("LV_CONF_PATH=" + $lvConfPath) `
    @srcs -o "$env:TEMP\mail_test.exe"
if ($LASTEXITCODE -ne 0) { throw "mail test build failed" }
Write-Output "mail test built -> $env:TEMP\mail_test.exe"
& "$env:TEMP\mail_test.exe"
if ($LASTEXITCODE -ne 0) { throw "mail test FAILED ($LASTEXITCODE failures)" }
Write-Output "mail test PASSED (58 checks)"
