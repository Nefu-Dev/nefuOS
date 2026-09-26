# nefuOS: sweep-launch every built-in app under --shot to detect crashes/errors.
# Usage: powershell -ExecutionPolicy Bypass -File tools\sweep_apps.ps1 <startId> <count> <workDir>
$ErrorActionPreference = "Continue"
$exe = "C:\Users\huawei\AppData\Local\Temp\nefu_fix_test\nefuOS.exe"
$root = "D:\mycppos1\nefuOS"
$start = [int]$args[0]
$count = [int]$args[1]
$work = $args[2]
New-Item -ItemType Directory -Force -Path $work | Out-Null
Copy-Item "$root\nefuos.fs" "$work\nefuos.fs" -Force -ErrorAction SilentlyContinue
Set-Location $work
$summary = @()
for ($id = $start; $id -lt $start + $count; $id++) {
    $log = Join-Path $work ("app_{0}.log" -f $id)
    $bmp = Join-Path $work ("app_{0}.bmp" -f $id)
    $crashBefore = 0
    if (Test-Path "$work\crash.txt") { $crashBefore = (Get-Item "$work\crash.txt").Length }
    Remove-Item $bmp -Force -ErrorAction SilentlyContinue
    & $exe --shot $id $bmp > $log 2>&1
    $code = $LASTEXITCODE
    $crashDelta = 0
    if (Test-Path "$work\crash.txt") { $crashDelta = (Get-Item "$work\crash.txt").Length - $crashBefore }
    $hasBmp = Test-Path $bmp
    $bmpSize = 0
    if ($hasBmp) { $bmpSize = (Get-Item $bmp).Length }
    $flag = "OK"
    if ($code -ne 0) { $flag = "EXIT$code" }
    elseif (-not $hasBmp) { $flag = "NOBMP" }
    elseif ($crashDelta -gt 0) { $flag = "CRASH" }
    $summary += ("{0}:{1}" -f $id, $flag)
    Write-Output ("app {0} -> {1} (exit={2} bmp={3} crash+{4})" -f $id, $flag, $code, $bmpSize, $crashDelta)
}
$summary | Out-File (Join-Path $work "summary.txt") -Encoding utf8
Write-Output "DONE $work"
