# tools/_bloxd_actions.ps1
# Run the host build against bloxd.io with a scripted page-action list and
# capture the console output.
#
# The host build runs the same browser.cpp as the bare kernel but prints its
# log live, so a page-action sequence can be checked in seconds instead of
# waiting for a QEMU boot.
#
# Usage:
#   powershell -File tools\_bloxd_actions.ps1 `
#       -Actions "key:Escape,class:PromptGenericPopupBodyButton,text:Play" `
#       -DelaySec 75 -KillAfterSec 140
param(
    [string]$Url = "https://bloxd.io/game/survival",
    [string]$Actions = "key:Escape,class:PromptGenericPopupBodyButton,text:Play",
    [int]$DelaySec = 75,
    [int]$KillAfterSec = 150,
    [string]$Shot = ""
)
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
if (-not (Test-Path $exe)) { "NO EXE: $exe"; exit 1 }
$outDir = "D:\mycppos1\nefuOS\verify_out"
$logOut = Join-Path $outDir "bloxd_act.out"
$logErr = Join-Path $outDir "bloxd_act.err"
Remove-Item $logOut, $logErr -ErrorAction SilentlyContinue

$shotArg = ""
if ($Shot -ne "") { $shotArg = "`"$Shot`"" }

$args2 = "--shot 17 `"$shotArg`" --url `"$Url`" --shotdelay $($DelaySec * 1000) --actions `"$Actions`""
"CMD: $args2"

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = $args2
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.WorkingDirectory = Split-Path $exe

$p = New-Object System.Diagnostics.Process
$p.StartInfo = $psi
$null = $p.Start()
$tOut = $p.StandardOutput.ReadToEndAsync()
$tErr = $p.StandardError.ReadToEndAsync()

$deadline = (Get-Date).AddSeconds($KillAfterSec)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    if ($p.HasExited) { break }
}
if ($p.HasExited) { "exited code=$($p.ExitCode)" }
else { "still running - killing"; $p.Kill() }

Start-Sleep -Milliseconds 400
$o = $tOut.Result
$e = $tErr.Result
Set-Content -Path $logOut -Value $o -Encoding utf8
Set-Content -Path $logErr -Value $e -Encoding utf8

"=== action lines ==="
($o -split "`r?`n") | Where-Object { $_ -match "\[ACT|\[KEY|\[CLICK|ACTDONE|act script|page action" } | ForEach-Object { "  $_" }
"=== errors ==="
($e -split "`r?`n") | Where-Object { $_ -match "not configurable|ERR|Error|exc" } | Select-Object -First 20 | ForEach-Object { "  $_" }
