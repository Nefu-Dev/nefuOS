# Run the host build against bloxd.io with scripted page actions and an
# in-page probe, then dump both log streams. Used to watch the texture-pack
# pipeline (atlas decode -> 2D canvas -> getImageData) advance the loading bar.
param(
    [string]$Url = "https://bloxd.io",
    [int]$DelayMs = 90000,
    [int]$KillAfterSec = 150,
    [string]$Tag = "bx32",
    [string]$Actions = "",
    [string]$ProbeJs = "",
    [string]$ProbeFile = "",
    [int]$ProbeMs = 4000,
    [hashtable]$ExtraEnv = @{}
)
# A long probe cannot survive shell quoting, so allow it to be read from a
# file. Newlines are folded to spaces because the runtime embeds the snippet
# inside a single generated statement.
if ($ProbeFile -ne "") {
    $ProbeJs = ((Get-Content -LiteralPath $ProbeFile -Raw) -replace "`r`n", " " -replace "`n", " ")
}
$src = "D:\mycppos1\nefuOS\build\nefuOS.exe"
$dir = Join-Path $env:TEMP "nefu_fix_test"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Get-Process nefuOS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600
$exe = Join-Path $dir "nefuOS.exe"
Copy-Item -LiteralPath $src -Destination $exe -Force

$outDir = "D:\mycppos1\nefuOS\verify_out"
$bmp = Join-Path $outDir "$Tag.bmp"
$logOut = Join-Path $outDir "$Tag.out"
$logErr = Join-Path $outDir "$Tag.err"
Remove-Item $bmp, $logOut, $logErr -ErrorAction SilentlyContinue

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = "--shot 17 `"$bmp`" --url `"$Url`" --shotdelay $DelayMs"
if ($Actions -ne "") { $psi.Arguments += " --actions `"$Actions`"" }
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.WorkingDirectory = $dir
if ($ProbeJs -ne "") {
    $psi.EnvironmentVariables["NF_PROBE_JS"] = $ProbeJs
    $psi.EnvironmentVariables["NF_PROBE_MS"] = [string]$ProbeMs
}
foreach ($k in $ExtraEnv.Keys) { $psi.EnvironmentVariables[$k] = [string]$ExtraEnv[$k] }

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
if ($p.HasExited) { "exited code=$($p.ExitCode)" } else { try { $p.Kill() } catch {}; "killed after $KillAfterSec s" }
try { $p.WaitForExit(5000) | Out-Null } catch {}
try { [System.IO.File]::WriteAllText($logOut, $tOut.Result) } catch {}
try { [System.IO.File]::WriteAllText($logErr, $tErr.Result) } catch {}
"bmp=" + (Test-Path $bmp)
