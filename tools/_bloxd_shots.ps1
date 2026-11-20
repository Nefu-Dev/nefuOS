# Run bloxd inside the nefuOS host and capture frames from the outside.
# The in-app --shot path serialises the whole 5.4 MB bitmap through the emulated
# VFS, stalling the frame loop long enough that bloxd's websocket heartbeat
# misses and the server drops the connection. Grabbing the Win32 window with
# PrintWindow never touches the emulator, so the game keeps running.
#
# Usage: _bloxd_shots.ps1 -Tag bd448 -Url https://bloxd.io/play/survival -GrabStart 105 -GrabInterval 7 -GrabCount 7 -KillAfterSec 165
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Url = 'https://bloxd.io/play/survival',
    [int]$GrabStart = 100,
    [int]$GrabInterval = 8,
    [int]$GrabCount = 6,
    [int]$KillAfterSec = 160,
    [string]$Key = '',
    [int]$ShotDelay = 0
)

$dir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exe = Join-Path $dir 'build\nefuOS.exe'
$out = Join-Path $dir 'verify_out'
$logOut = Join-Path $out ($Tag + '.out')
$logErr = Join-Path $out ($Tag + '.err')
$py = 'C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe'

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
# --app 17 runs the browser test mode: it unlocks the session and opens the url,
# but (unlike --shot) it skips the blocking shot loop, so the window is shown and
# the message loop keeps pumping while we grab frames from outside.
$psi.Arguments = '--app 17 --url "' + $Url + '"'
if ($ShotDelay -gt 0) {
    $psi.Arguments = '--shot 17 "' + (Join-Path $out ($Tag + '_unlock.bmp')) + '" --shotdelay ' + $ShotDelay
    $psi.Arguments += ' --url "' + $Url + '"'
    if ($Key -ne '') { $psi.Arguments += ' --key "' + $Key + '"' }
}
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.WorkingDirectory = $dir
$psi.EnvironmentVariables['NEFU_GENCOOK_FILE'] = Join-Path $out 'gencook.json'
$psi.EnvironmentVariables['NEFU_MCOOK_LIVE'] = '1'
$psi.EnvironmentVariables['NEFU_COOKIE_FILE'] = Join-Path $out 'cookies_live.txt'
$psi.EnvironmentVariables['NEFU_TRAFFIC_CODE_FILE'] = Join-Path $out 'traffic_code.txt'
$psi.EnvironmentVariables['NEFU_WASM_FILE'] = Join-Path $out 'bloxd_wasm_bg.wasm'

$p = New-Object System.Diagnostics.Process
$p.StartInfo = $psi
$null = $p.Start()
$tOut = $p.StandardOutput.ReadToEndAsync()
$tErr = $p.StandardError.ReadToEndAsync()

$grabOut = Join-Path $out ($Tag + '.grab.out')
# A 1520x896 software framebuffer is slow enough that bloxd's websocket
# heartbeat misses during gameplay; a smaller window keeps the frame time low.
if (-not $env:NFU_RESIZE) { $env:NFU_RESIZE = '780x620' }
$grab = Start-Process -FilePath $py -ArgumentList @('-X', 'utf8', (Join-Path $out 'x_win_grab.py'),
    (Join-Path $out $Tag), [string]$GrabStart, [string]$GrabInterval, [string]$GrabCount) `
    -PassThru -NoNewWindow -RedirectStandardOutput $grabOut

$deadline = (Get-Date).AddSeconds($KillAfterSec)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    if ($p.HasExited) { break }
}
if ($p.HasExited) { 'exited code=' + $p.ExitCode } else { try { $p.Kill() } catch {}; 'killed after ' + $KillAfterSec + ' s' }
try { $p.WaitForExit(5000) | Out-Null } catch {}
try { $grab.WaitForExit(20000) | Out-Null } catch { try { $grab.Kill() } catch {} }
try { [System.IO.File]::WriteAllText($logOut, $tOut.Result) } catch {}
try { [System.IO.File]::WriteAllText($logErr, $tErr.Result) } catch {}
'shots=' + ((Get-ChildItem -LiteralPath $out -Filter ($Tag + '_*.png') -ErrorAction SilentlyContinue).Count)
