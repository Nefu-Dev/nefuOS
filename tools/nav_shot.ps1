#Requires -Version 5.1
<#
    nav_shot.ps1 — one-page screenshot run of the nefuOS host browser.

    The --shot mode of the host build writes a raw framebuffer .bmp after
    --shotdelay and is supposed to exit; in practice a page whose JS keeps the
    worker alive can leave the process running, and every later build then
    fails to overwrite dist\nefuOS.exe ("being used by another process").
    This wrapper starts it, waits for the bmp, and kills the process either
    way, so the loop build -> shot -> look never wedges.

    Usage:
      .\tools\nav_shot.ps1 -Url https://www.baidu.com/ -Out verify_out\bd.bmp `
          -Delay 70000 -Env @{ NF_DUMP_DOM = 'verify_out\bd_dom.html' }
#>
param(
    [Parameter(Mandatory = $true)][string]$Url,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$Delay = 60000,
    [string]$Exe = 'D:\mycppos1\nefuOS\dist\nefuOS.exe',
    [hashtable]$Env = @{},
    [string]$ErrFile = ''
)

$env:NF_QUIET = '1'
foreach ($k in $Env.Keys) { Set-Item -Path ("Env:" + $k) -Value $Env[$k] }

if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Force }
$sp = @{
    FilePath     = $Exe
    ArgumentList = @('--shot', '17', $Out, '--url', $Url, '--shotdelay', "$Delay")
    PassThru     = $true
    WindowStyle  = 'Hidden'
}
# klogf goes to stdout, diagnostics to stderr: capture BOTH or the useful
# lines (commit counts, css rules, layout_page) silently vanish.
if ($ErrFile) {
    $sp['RedirectStandardError']  = $ErrFile
    $sp['RedirectStandardOutput'] = ($ErrFile -replace '\.err$', '.out')
}
$p = Start-Process @sp

$deadline = (Get-Date).AddMilliseconds($Delay + 45000)
$got = $false
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    if (Test-Path -LiteralPath $Out) {
        $len = (Get-Item -LiteralPath $Out).Length
        if ($len -gt 100000) { $got = $true; break }
    }
}
Start-Sleep -Milliseconds 1200
try { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } } catch {}
if (-not $got) { Write-Output "NO SHOT: $Url (see $ErrFile)" } else { Write-Output "SHOT OK: $Out" }
foreach ($k in $Env.Keys) { Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue }
