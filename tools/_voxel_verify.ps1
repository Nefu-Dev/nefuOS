# One-shot verification for verify_out\voxel_play.html.
#
# This exists because the playground was written but never actually executed: the
# shell channel was exhausted (background shell task limit reached) and could not
# produce a single frame. Run this the moment the channel is back.
#
# What counts as a pass:
#   * console shows [VX] start / terrain verts=... / first frame drawn
#   * the screenshot contains both grass green and sky blue patches, which rules
#     out the two failure modes we hit before: a blank canvas and a world with
#     its vertical faces culled away (only floating grass tiles).
#
# Usage:  .\tools\_voxel_verify.ps1
#         .\tools\_voxel_verify.ps1 -Tag vx2 -DelayMs 20000

param(
    [string]$Tag = "vx1",
    [int]$DelayMs = 12000,
    [int]$KillAfterSec = 22
)

$ErrorActionPreference = 'Stop'
Set-Location 'D:\mycppos1\nefuOS'

$url = 'file:///D:/mycppos1/nefuOS/verify_out/voxel_play.html'
& '.\tools\_bloxd_run2.ps1' -Tag $Tag -Url $url -DelayMs $DelayMs -KillAfterSec $KillAfterSec *> "verify_out\run_$Tag.log" 2>&1 | Out-Null

# --- console side ---------------------------------------------------------
$err = "verify_out\$Tag.err"
$ok = $false
if (Test-Path -LiteralPath $err) {
    $lines = Get-Content -LiteralPath $err | Where-Object { $_ -match '\[VX\]|SHADER|LINK ' }
    if ($lines) { $lines | Select-Object -First 14 | ForEach-Object { $_ } }
    else { "no [VX] lines - the page probably did not run" }
    if (($lines | Where-Object { $_ -match 'first frame drawn' }).Count -gt 0) { $ok = $true }
} else {
    "no console log - browser never attached"
}

# --- pixel side ----------------------------------------------------------
# Count colours instead of sampling fixed points: the camera moves, so a fixed
# coordinate proves nothing. Grass green means the terrain rasterised; sky blue
# means the clear colour reached the surface instead of a black frame.
$bmp = "verify_out\$Tag.bmp"
if (Test-Path -LiteralPath $bmp) {
    $py = @"
from PIL import Image
im = Image.open(r'verify_out\$Tag.bmp').convert('RGB')
w, h = im.size
g = s = 0
for r, gg, b in im.getdata():
    if gg > 100 and r < 110 and b < 110:
        g += 1
    elif b > 180 and gg > 140 and r < 160:
        s += 1
print('size', w, h, 'grass_px', g, 'sky_px', s)
print('terrainVisible', g > 20000)
print('skyVisible', s > 100000)
"@
    $py | python -
    python -c "from PIL import Image; Image.open(r'verify_out\$Tag.bmp').save(r'verify_out\$Tag.png')"
    "png=" + (Test-Path -LiteralPath "verify_out\$Tag.png")
} else {
    "no bitmap - the browser never produced a frame"
}

"firstFrameOK=" + $ok
