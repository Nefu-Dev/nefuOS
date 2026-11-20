# build and run one URL through the nefuOS browser, then convert to png
# rebuild_shot.ps1 -Url <url> -Name <name> [-Delay ms] [-Env @{...}]
param(
    [string]$Url,
    [string]$Name = 'shot',
    [int]$Delay = 30000,
    [hashtable]$Env = @{},
    [switch]$SkipBuild
)
$root = 'D:\mycppos1\nefuOS'
Get-Process nefuOS -ErrorAction SilentlyContinue | Stop-Process -Force
if (-not $SkipBuild) {
    pwsh -NoProfile -Command "[Environment]::CurrentDirectory='$root'; Set-Location '$root'; & '$root\tools\build.ps1' -Target host" 2>&1 |
        Select-Object -Last 1 | Write-Output
}
$bmp = Join-Path $root "verify_out\$Name.bmp"
$err = Join-Path $root "verify_out\$Name.err"
& (Join-Path $root 'tools\nav_shot.ps1') -Url $Url -Out $bmp -Delay $Delay -Env $Env -ErrFile $err
& "C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe" (Join-Path $root 'tools\shot2png.py') $bmp (Join-Path $root "verify_out\$Name.png") 1200 1000 800
