# Temporary pre-build check: compile every core source with the same flags as
# build_iso.ps1 (bare) into an isolated obj dir, and report every failing file.
# Does NOT link; just surfaces compile errors before the slow full build.
$ErrorActionPreference = "Continue"
Set-Location "D:\mycppos1\nefuOS"
$g = "D:\CLion\bin\mingw\bin\g++.exe"
$conf = "D:/mycppos1/nefuOS/third_party/lvgl_conf/lv_conf.h"
$outRoot = Join-Path $env:TEMP "nefu_precheck"
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
$coreExclude = @(
  "core\apps\calculator.cpp", "core\apps\html5.cpp", "core\apps\lockscreen.cpp",
  "core\apps\recycle.cpp", "core\apps\stubs.cpp",
  "core\apps\terminal_commands.cpp", "core\apps\terminal_extra.cpp",
  "core\sys\sysapi_extended.cpp",
  "core\net\tls13_fixed.cpp",      # supersedes tls13.cpp -> duplicate symbols
  "core\stdcxx\test_stdcxx.cpp"    # test main() collides with win32.cpp main
)
$coreRoot = (Get-Location).Path
$coreSrc = @(Get-ChildItem "core" -Recurse -Filter *.cpp |
  ForEach-Object { $_.FullName.Substring($coreRoot.Length + 1) } |
  Where-Object { $coreExclude -notcontains $_ } |
  Sort-Object) + @("third_party\stb_image_wrap.cpp")
Write-Output "prechecking $($coreSrc.Count) core sources ..."
$fail = @()
foreach ($s in $coreSrc) {
  if ($s -like "*numeric.cpp") { continue }
  $name = $s -replace '[\\/]', '_' -replace '\.cpp$', '.o'
  $obj = Join-Path $outRoot $name
  $flags = @("-std=c++17", "-fno-exceptions", "-fno-rtti", "-fno-builtin",
             "-fno-stack-protector", "-mno-red-zone", "-O2",
             "-ffunction-sections", "-fdata-sections", "-fno-asynchronous-unwind-tables",
             "-Wno-sized-deallocation", "-DNEFU_BARE", "-DNEFU_LVGL_DEMO",
             "-I", "core", "-I", "third_party",
             "-I", "third_party\lvgl_conf", "-I", "third_party\lvgl_src\lvgl-9.2.0",
             "-D", "LV_CONF_PATH=$conf", "-c")
  if ($s -like "*minijs.cpp") { } # no -mgeneral-regs-only anywhere; bareFlags has none
  & $g @flags $s -o $obj 2>&1 | Out-Null
  if ($LASTEXITCODE -ne 0) {
    $fail += $s
    Write-Output "FAIL: $s"
    & $g @flags $s 2>&1 | Select-Object -First 6 | ForEach-Object { Write-Output "   $_" }
  }
}
Write-Output "precheck done: $($coreSrc.Count - $fail.Count - 1) ok, $($fail.Count) failed"
$fail | ForEach-Object { Write-Output "FAILED: $_" }
