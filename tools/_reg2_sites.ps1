# Long-wait real-site regression (85s shot delay)
param([int]$DelayMs = 85000, [int]$KillAfterSec = 100)
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$outDir = "D:\mycppos1\nefuOS\verify_out"
$sites = @(
  @("https://www.baidu.com/", "reg2_baidu"),
  @("https://www.ifeng.com/", "reg2_ifeng"),
  @("https://www.bilibili.com/", "reg2_bili"),
  @("https://www.sina.com.cn/", "reg2_sina")
)
foreach ($s in $sites) {
  $bmp = Join-Path $outDir ($s[1] + ".bmp")
  Remove-Item $bmp -ErrorAction SilentlyContinue
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $exe
  $psi.Arguments = "--shot 17 `"$bmp`" --url `"$($s[0])`" --shotdelay $DelayMs"
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.WorkingDirectory = Split-Path $exe
  $p = New-Object System.Diagnostics.Process
  $p.StartInfo = $psi
  $null = $p.Start()
  $null = $p.StandardOutput.ReadToEndAsync()
  $null = $p.StandardError.ReadToEndAsync()
  $deadline = (Get-Date).AddSeconds($KillAfterSec)
  while ((Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 400; if ($p.HasExited) { break } }
  if (-not $p.HasExited) { try { $p.Kill() } catch {} }
  try { $p.WaitForExit(3000) | Out-Null } catch {}
  Write-Output "$($s[1]): $(Test-Path $bmp)"
}
Write-Output "REG2 COMPLETE"
