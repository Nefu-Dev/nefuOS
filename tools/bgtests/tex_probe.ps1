$ErrorActionPreference = 'Continue'
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
"exe size: " + (Get-Item $exe).Length
$dir = "D:\mycppos1\nefuOS\tools\bgtests"
$srv = Start-Process -FilePath "python" -ArgumentList "-m","http.server","8123","--directory",$dir -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
$log = Join-Path $dir "nefu_probe.txt"
if (Test-Path $log) { Remove-Item $log }
$p = Start-Process -FilePath $exe -ArgumentList "--app","17","--url","http://127.0.0.1:8123/wgl_test.html" -PassThru -RedirectStandardOutput $log
Start-Sleep -Seconds 10
if ($p.HasExited) {
  "exited code=" + $p.ExitCode
} else {
  "alive"
  Stop-Process -Name "nefuOS" -Force -ErrorAction SilentlyContinue
}
"--- log head ---"
Get-Content $log -Encoding UTF8 | Select-Object -First 20 | ForEach-Object { if ($_.Length -gt 140) { $_.Substring(0,140) } else { $_ } }
Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue