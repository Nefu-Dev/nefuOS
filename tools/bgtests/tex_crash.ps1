$ErrorActionPreference = 'Continue'
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$dir = "D:\mycppos1\nefuOS\tools\bgtests"
$srv = Start-Process -FilePath "python" -ArgumentList "-m","http.server","8123","--directory",$dir -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
$log = Join-Path $dir "nefu_tex_log3.txt"
if (Test-Path $log) { Remove-Item $log }
$p = Start-Process -FilePath $exe -ArgumentList "--app","17","--url","http://127.0.0.1:8123/tex_test.html" -PassThru -RedirectStandardOutput $log
Start-Sleep -Seconds 20
if ($p.HasExited) {
  "exited code=0x{0:X8}" -f $p.ExitCode
} else {
  "alive"
  Stop-Process -Name "nefuOS" -Force -ErrorAction SilentlyContinue
}
Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue
"--- log tail ---"
Get-Content $log -Encoding UTF8 | Select-Object -Last 8 | ForEach-Object { if ($_.Length -gt 150) { $_.Substring(0,150) } else { $_ } }
"--- texupload lines ---"
Select-String -Path $log -Pattern "texupload|GL:|js:" | Select-Object -First 6 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(150, $_.Line.Length)) }