# Quick re-check of the previously failing files (compile only).
$ErrorActionPreference = "Continue"
Set-Location "D:\mycppos1\nefuOS"
$g = "D:\CLion\bin\mingw\bin\g++.exe"
$conf = "D:/mycppos1/nefuOS/third_party/lvgl_conf/lv_conf.h"
$outRoot = Join-Path $env:TEMP "nefu_recheck"
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
$files = @(
  "core\apps\deb_selftest.cpp",
  "core\apps\ncode.cpp",
  "core\apps\npkg.cpp",
  "core\apps\update.cpp",
  "core\audio\audio_engine.cpp",
  "core\drivers\audio_driver.cpp"
)
foreach ($s in $files) {
  $name = $s -replace '[\\/]', '_' -replace '\.cpp$', '.o'
  $obj = Join-Path $outRoot $name
  $flags = @("-std=c++17", "-fno-exceptions", "-fno-rtti", "-fno-builtin",
             "-fno-stack-protector", "-mno-red-zone", "-O2",
             "-ffunction-sections", "-fdata-sections", "-fno-asynchronous-unwind-tables",
             "-Wno-sized-deallocation", "-DNEFU_BARE", "-DNEFU_LVGL_DEMO",
             "-I", "core", "-I", "third_party",
             "-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
             "-D", "LV_CONF_PATH=$conf", "-c")
  & $g @flags $s -o $obj 2>&1 | Out-Null
  if ($LASTEXITCODE -eq 0) {
    Write-Output "PASS: $s"
  } else {
    Write-Output "FAIL: $s"
    & $g @flags $s 2>&1 | Select-Object -First 8 | ForEach-Object { Write-Output "   $_" }
  }
}
