#Requires -Version 5.1
<#
    edge_shot.ps1 — reference screenshot of a URL from real Edge (headless).

    Used as the ground truth the nefuOS browser render is compared against:
    same 1000x800 logical viewport, same URL, so a missing search box or a
    white page is a visible difference rather than a matter of opinion.
#>
param(
    [Parameter(Mandatory = $true)][string]$Url,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$Width = 1000,
    [int]$Height = 800,
    [int]$BudgetMs = 12000
)
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$profile = Join-Path $env:TEMP ('edge_shot_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Force }
# Headless Edge can hang on a heavy page (jd/taobao keep sockets open), and a
# bare call would block the whole batch run, so bound it and kill the tree.
$p = Start-Process -FilePath $edge -PassThru -WindowStyle Hidden -ArgumentList @(
    '--headless=new', '--disable-extensions', '--disable-background-networking',
    '--hide-scrollbars', '--mute-audio', '--no-first-run', '--disable-sync',
    "--user-data-dir=$profile", "--window-size=$Width,$Height",
    "--virtual-time-budget=$BudgetMs", "--screenshot=$Out", $Url)
$deadline = (Get-Date).AddMilliseconds($BudgetMs + 18000)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 400
    if (Test-Path -LiteralPath $Out) { break }
}
if ($p -and -not $p.HasExited) { try { Stop-Process -Id $p.Id -Force } catch {} }
Start-Sleep -Milliseconds 300
if (Test-Path -LiteralPath $profile) { Remove-Item -LiteralPath $profile -Recurse -Force -ErrorAction SilentlyContinue }
if (Test-Path -LiteralPath $Out) { Write-Output ("EDGE OK: " + $Out) } else { Write-Output ("EDGE FAIL: " + $Url) }
