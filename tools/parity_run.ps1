# Batch parity run: for each URL take an Edge reference and a nefuOS shot.
# parity_run.ps1 -List <sites.txt> -OutDir verify_out\matrix -Delay 30000
param(
    [string]$List = 'D:\mycppos1\nefuOS\tools\sites.txt',
    [string]$OutDir = 'D:\mycppos1\nefuOS\verify_out\matrix',
    [int]$Delay = 30000,
    [switch]$SkipEdge,
    [switch]$SkipNefu,
    [string]$Only = ''
)
$root = 'D:\mycppos1\nefuOS'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$py = 'C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe'
$lines = Get-Content -LiteralPath $List -Encoding utf8 | Where-Object { $_ -match '^\s*[a-z0-9]' }
$i = 0
foreach ($line in $lines) {
    $i++
    $parts = $line.Trim() -split '\s+', 2
    $slug = $parts[0]
    $url = $parts[1]
    if ($Only -and $slug -notmatch $Only) { continue }
    if (-not $SkipEdge) {
        $edge = Join-Path $OutDir "$slug.edge.png"
        if (-not (Test-Path -LiteralPath $edge)) {
            & (Join-Path $root 'tools\edge_shot.ps1') -Url $url -Out $edge -Width 755 -Height 480 2>&1 | Out-Null
        }
    }
    if (-not $SkipNefu) {
        $png = Join-Path $OutDir "$slug.nefu.png"
        if (Test-Path -LiteralPath $png) { Write-Output ("[{0}/{1}] {2} (skip)" -f $i, $lines.Count, $slug); continue }
        $bmp = Join-Path $OutDir "$slug.nefu.bmp"
        $log = Join-Path $OutDir "$slug.nefu.out"
        $err = Join-Path $OutDir "$slug.nefu.err"
        $lay = Join-Path $OutDir "$slug.layout.txt"
        Get-Process nefuOS -ErrorAction SilentlyContinue | Stop-Process -Force
        & (Join-Path $root 'tools\nav_shot.ps1') -Url $url -Out $bmp -Delay $Delay `
            -Env @{ NF_DUMP_LAYOUT = $lay } -ErrFile $err | Out-Null
        if (Test-Path -LiteralPath $bmp) {
            & $py (Join-Path $root 'tools\shot2png.py') $bmp (Join-Path $OutDir "$slug.nefu.png") 1200 1000 800 | Out-Null
        }
    }
    Write-Output ("[{0}/{1}] {2}" -f $i, $lines.Count, $slug)
}
Write-Output 'MATRIX DONE'
