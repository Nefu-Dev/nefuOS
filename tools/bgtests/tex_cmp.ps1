$ErrorActionPreference = 'Continue'
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$dir = "D:\mycppos1\nefuOS\tools\bgtests"
$srv = Start-Process -FilePath "python" -ArgumentList "-m","http.server","8123","--directory",$dir -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
foreach ($case in @(@("nourl","--app","17"), @("wgl","--app","17","--url","http://127.0.0.1:8123/wgl_test.html"))) {
  $name = $case[0]
  $args = $case[1..($case.Count-1)]
  $log = Join-Path $dir "nefu_cmp_$name.txt"
  if (Test-Path $log) { Remove-Item $log }
  $p = Start-Process -FilePath $exe -ArgumentList $args -PassThru -RedirectStandardOutput $log
  Start-Sleep -Seconds 12
  if ($p.HasExited) { Write-Output "$name : EXITED" } else { Write-Output "$name : alive"; Stop-Process -Name "nefuOS" -Force -ErrorAction SilentlyContinue }
  $lines = Get-Content $log -Encoding UTF8
  "  lines=" + $lines.Count
  $lines | Select-Object -Last 3 | ForEach-Object { "  " + $(if ($_.Length -gt 130) { $_.Substring(0,130) } else { $_ }) }
}
Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue
"done"