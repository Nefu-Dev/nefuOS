$ErrorActionPreference = 'Continue'
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$dir = "D:\mycppos1\nefuOS\tools\bgtests"
$srv = Start-Process -FilePath "python" -ArgumentList "-m","http.server","8123","--directory",$dir -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
foreach ($u in @("tex_nodraw.html","tex_draw.html","wgl_test.html")) {
  $url = "http://127.0.0.1:8123/$u"
  $log = Join-Path $dir "nefu_bin_$($u.Replace('.html','')).txt"
  if (Test-Path $log) { Remove-Item $log }
  $p = Start-Process -FilePath $exe -ArgumentList "--app","17","--url",$url -PassThru -RedirectStandardOutput $log
  Start-Sleep -Seconds 10
  if ($p.HasExited) {
    Write-Output "$u : EXITED code=0x{0:X8}" -f $p.ExitCode
  } else {
    Write-Output "$u : alive"
    Stop-Process -Name "nefuOS" -Force -ErrorAction SilentlyContinue
  }
  $last = (Get-Content $log -Encoding UTF8 | Select-Object -Last 2)
  foreach ($l in $last) { if ($l.Length -gt 140) { $l.Substring(0,140) } else { $l } }
}
Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue
"done"