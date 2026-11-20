# Local-page layout test: --shot with a file:// URL (host pages must use
# file://; 127.0.0.1/localhost never works with WinInet).
param(
    [string]$Url = "file:///D:/mycppos1/nefuOS/verify_out/css_test2.html",
    [int]$DelayMs = 3000,
    [int]$KillAfterSec = 15,
    [string]$Tag = "css_t2"
)
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$outDir = "D:\mycppos1\nefuOS\verify_out"
$bmp = Join-Path $outDir "$Tag.bmp"
$logOut = Join-Path $outDir "$Tag.out"
$logErr = Join-Path $outDir "$Tag.err"
Remove-Item $bmp, $logOut, $logErr -ErrorAction SilentlyContinue

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = "--shot 17 `"$bmp`" --url `"$Url`" --shotdelay $DelayMs"
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$p = New-Object System.Diagnostics.Process
$p.StartInfo = $psi
$null = $p.Start()
$tOut = $p.StandardOutput.ReadToEndAsync()
$tErr = $p.StandardError.ReadToEndAsync()
$deadline = (Get-Date).AddSeconds($KillAfterSec)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 300
    if ($p.HasExited) { break }
}
if (-not $p.HasExited) { try { $p.Kill() } catch {} }
try { $p.WaitForExit(3000) | Out-Null } catch {}
try { [IO.File]::WriteAllText($logOut, $tOut.Result) } catch {}
try { [IO.File]::WriteAllText($logErr, $tErr.Result) } catch {}
"bmp=" + (Test-Path $bmp)
