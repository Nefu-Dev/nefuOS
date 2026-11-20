# Headless-ish smoke test for bloxd.io: run the host build with --shot, capture
# stdout/stderr traces, then stop the process (the shot loop falls through into
# the normal message loop and never exits on its own).
# Uses System.Diagnostics.Process directly: Start-Process trips over an
# environment block that carries both HTTPS_PROXY and https_proxy.
param(
    [string]$Url = "https://bloxd.io",
    [int]$DelayMs = 35000,
    [int]$KillAfterSec = 50,
    [string]$Tag = "bloxd",
    [string]$Actions = "",
    [string]$EnvDumpDom = "",
    [string]$Drag = ""     # "x1 y1 x2 y2": scripted press-move-release test
)

$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$outDir = "D:\mycppos1\nefuOS\verify_out"
$bmp = Join-Path $outDir "$Tag.bmp"
$logOut = Join-Path $outDir "$Tag.out"
$logErr = Join-Path $outDir "$Tag.err"

if (-not (Test-Path $exe)) { "MISSING EXE: $exe"; exit 1 }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Remove-Item $bmp, $logOut, $logErr -ErrorAction SilentlyContinue

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = "--shot 17 `"$bmp`" --url `"$Url`" --shotdelay $DelayMs"
if ($Actions -ne "") { $psi.Arguments += " --actions `"$Actions`"" }
if ($Drag -ne "") {
    $d = $Drag -split '\s+' | ForEach-Object { [int]$_ }
    if ($d.Count -eq 4) { $psi.Arguments += " --drag $($d[0]) $($d[1]) $($d[2]) $($d[3])" }
}
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
if ($EnvDumpDom -ne "") { $env:NF_DUMP_DOM = $EnvDumpDom }
$psi.WorkingDirectory = Split-Path $exe

$p = New-Object System.Diagnostics.Process
$p.StartInfo = $psi
$null = $p.Start()

# Drain both pipes on background tasks so a full pipe buffer cannot wedge the
# GUI process while it is still loading the page.
$sbOut = New-Object System.Text.StringBuilder
$sbErr = New-Object System.Text.StringBuilder
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
$outText = ""
$errText = ""
try { $outText = $tOut.Result } catch {}
try { $errText = $tErr.Result } catch {}
[System.IO.File]::WriteAllText($logOut, $outText)
[System.IO.File]::WriteAllText($logErr, $errText)

"bmp=" + (Test-Path $bmp)
"stdout=$logOut (" + $outText.Length + " chars)"
"stderr=$logErr (" + $errText.Length + " chars)"
