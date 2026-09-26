$ErrorActionPreference = 'Continue'
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$dir = "D:\mycppos1\nefuOS\tools\bgtests"
$srv = Start-Process -FilePath "python" -ArgumentList "-m","http.server","8123","--directory",$dir -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
$script:last_alive = $false

function Run-Check($name, $url, $wait) {
  $log = Join-Path $dir "nefu_final_$name.txt"
  if (Test-Path $log) { Remove-Item $log }
  $p = Start-Process -FilePath $exe -ArgumentList "--app","17","--url",$url -PassThru -RedirectStandardOutput $log
  Start-Sleep -Seconds $wait
  $script:last_alive = -not $p.HasExited
  Stop-Process -Name "nefuOS" -Force -ErrorAction SilentlyContinue
  return $log
}

$l = Run-Check "unreach" "http://127.0.0.1:9/" 8
Write-Output "=== unreach alive=$script:last_alive ==="
Select-String -Path $l -Pattern "browser: load failed|load failed" | Select-Object -First 2 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(110, $_.Line.Length)) }

$l = Run-Check "jsonly" "http://127.0.0.1:8123/jsonly.html" 10
Write-Output "=== jsonly alive=$script:last_alive ==="
Select-String -Path $l -Pattern "js: ua" | Select-Object -First 1 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(110, $_.Line.Length)) }
"probe-residual: " + (Select-String -Path $l -Pattern "texim argc|px arg object|px off|tex-store|reclaim probe|ensure rt|ensure ctx|pre-eval|post-eval" -ErrorAction SilentlyContinue | Measure-Object).Count

$l = Run-Check "reclaim" "http://127.0.0.1:8123/tall50c.html" 12
Write-Output "=== reclaim alive=$script:last_alive ==="
"reclaimed: " + (Select-String -Path $l -Pattern "reclaimed offscreen" -ErrorAction SilentlyContinue | Measure-Object).Count

Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue
"ALL-DONE"