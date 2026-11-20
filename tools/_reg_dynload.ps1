# Post-dynamic-script/link-loading regression: offline pages + key real sites
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$outDir = "D:\mycppos1\nefuOS\verify_out"

function Shot($url, $tag, $delayMs = 16000, $kill = 24) {
  $bmp = Join-Path $outDir "$tag.bmp"
  Remove-Item $bmp, (Join-Path $outDir "$tag.out"), (Join-Path $outDir "$tag.err") -ErrorAction SilentlyContinue
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $exe
  $psi.Arguments = "--shot 17 `"$bmp`" --url `"$url`" --shotdelay $delayMs"
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.WorkingDirectory = Split-Path $exe
  $p = New-Object System.Diagnostics.Process
  $p.StartInfo = $psi
  $null = $p.Start()
  $null = $p.StandardOutput.ReadToEndAsync()
  $null = $p.StandardError.ReadToEndAsync()
  $deadline = (Get-Date).AddSeconds($kill)
  while ((Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 300; if ($p.HasExited) { break } }
  if (-not $p.HasExited) { try { $p.Kill() } catch {} }
  try { $p.WaitForExit(3000) | Out-Null } catch {}
  $ok = (Test-Path $bmp) -and ((Get-Item $bmp).Length -gt 100000)
  Write-Output "$tag : $(if ($ok) { 'BMP OK' } else { 'MISSING/SMALL' })"
}

# offline pages
Shot "file:///D:/mycppos1/nefuOS/verify_out/_float_test.html"  "reg_float"
Shot "file:///D:/mycppos1/nefuOS/verify_out/_grid_test.html"   "reg_grid"
Shot "file:///D:/mycppos1/nefuOS/verify_out/_css_features.html" "reg_css"
Shot "file:///D:/mycppos1/nefuOS/verify_out/controls_test.html" "reg_ctrl"
Shot "file:///D:/mycppos1/nefuOS/verify_out/_scroll_test.html"  "reg_scroll"
# real sites (dynamic script/link loading stress)
Shot "https://www.qq.com/"      "reg_qq"     36000 46
Shot "https://www.baidu.com/"   "reg_baidu"  36000 46
Shot "https://www.ifeng.com/"   "reg_ifeng"  36000 46
Shot "https://www.bilibili.com/" "reg_bili"  36000 46
Write-Output "REGRESSION COMPLETE"
