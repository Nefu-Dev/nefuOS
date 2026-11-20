# bloxd.io privacy-popup click test: launch with --click at the Agree button,
# capture screenshots before/after the click window.
param(
    [int]$DelayMs = 60000,
    [int]$KillAfterSec = 90,
    [int]$Cx = -1,
    [int]$Cy = -1,
    [int]$Cx2 = -1,
    [int]$Cy2 = -1,
    [string]$Keys = "",
    [int]$NfuClick = -1,
    [string]$Exe = "nefuOS.exe"
)
$exe = Join-Path $env:TEMP "nefu_fix_test\$Exe"
$outDir = "D:\mycppos1\nefuOS\verify_out"
$bmp = Join-Path $outDir "bloxd2.bmp"
$logOut = Join-Path $outDir "bloxd2.out"
$logErr = Join-Path $outDir "bloxd2.err"
Remove-Item $bmp, $logOut, $logErr -ErrorAction SilentlyContinue

$args2 = "--shot 17 `"$bmp`" --url `"https://bloxd.io`" --shotdelay $DelayMs"
if ($Cx -ge 0) { $args2 += " --click $Cx $Cy" }
if ($Cx2 -ge 0) { $args2 += " --click2 $Cx2 $Cy2" }
if ($Keys -ne "") { $args2 += " --key `"$Keys`"" }
if ($NfuClick -ge 0) { $args2 += " --nfuclick $NfuClick" }
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
    Start-Sleep -Milliseconds 400
    if ($p.HasExited) { break }
}
if ($p.HasExited) { "exited code=$($p.ExitCode)" }
else { try { $p.Kill() } catch {}; "killed after $KillAfterSec s" }
try { $p.WaitForExit(5000) | Out-Null } catch {}
try { [System.IO.File]::WriteAllText($logOut, $tOut.Result) } catch {}
try { [System.IO.File]::WriteAllText($logErr, $tErr.Result) } catch {}
"bmp=" + (Test-Path $bmp)
"stdout=$logOut"
