Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

function Snap([string]$path, [IntPtr]$hwnd) {
    $bmp = New-Object System.Drawing.Bitmap(820, 620)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try { $g.CopyFromScreen(0, 0, 0, 0, $bmp.Size) } catch {}
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$out = "D:\mycppos1\nefuOS\verify_out\ws_shot.png"
$p = Start-Process -FilePath $exe -ArgumentList "--app","17","--url","file:///D:/mycppos1/nefuOS/verify_out/ws_test.html" -PassThru
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 500
    $p.Refresh()
    if ($p.MainWindowHandle -ne 0) { $hwnd = $p.MainWindowHandle; break }
}
"hwnd=$hwnd"
Start-Sleep -Seconds 14
$p.Refresh()
if ($p.HasExited) { "EXITED $($p.ExitCode)" } else { "alive" }
Snap $out $hwnd
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
"shot=$out"
