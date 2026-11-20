#Requires -Version 5.1
<#
    cc_check.ps1 - compile one translation unit with the host flags, standalone.

    The incremental build has one failure mode worth guarding against: if a unit
    fails to compile, its old object can still be linked and the resulting exe
    silently lacks the new code (observed with browser.cpp this session). So a
    source edit is confirmed here first, exit code and first errors printed.
#>
param(
    [string]$Src = 'core\apps\browser.cpp',
    [string]$Obj = 'build\obj\host_core\core_apps_browser.o'
)
$root = 'D:\mycppos1\nefuOS'
Set-Location -LiteralPath $root
$gxx = 'D:\CLion\bin\mingw\bin\g++.exe'
$conf = (Resolve-Path -LiteralPath "$root\third_party\lvgl_conf\lv_conf.h").Path -replace '\\', '/'
$err = Join-Path $root 'verify_out\cc_check.err'
& $gxx -std=c++17 -O2 -fno-exceptions -fno-rtti -fno-builtin -Wall -Wextra `
    -Wno-sized-deallocation -I core -I third_party -DNEFU_LVGL_DEMO `
    -I third_party\lvgl_conf -I third_party\lvgl_src\lvgl-9.2.0 `
    -D "LV_CONF_PATH=$conf" --param ggc-min-expand=10 --param ggc-min-heapsize=65536 `
    -c $Src -o $Obj 2> $err
$code = $LASTEXITCODE
if ($code -ne 0) {
    Write-Output "COMPILE FAILED (exit $code)"
    Get-Content -LiteralPath $err -Encoding utf8 | Select-String -Pattern ': error' | Select-Object -First 10 |
        ForEach-Object { $_.Line }
} else {
    Write-Output ("COMPILE OK -> " + $Obj + " " + (Get-Item -LiteralPath $Obj).Length + " B")
    $w = Get-Content -LiteralPath $err -Encoding utf8 | Select-String -Pattern ': warning' | Measure-Object
    Write-Output ("warnings: " + $w.Count)
}
