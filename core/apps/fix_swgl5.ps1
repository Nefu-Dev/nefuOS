# Move sw_mix/sw_step/sw_smooth after sw_val() definition
$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$s = [IO.File]::ReadAllText($p, $enc)
$lines = $s -split "`n"

$start = -1
for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^// mix/step/smoothstep') { $start = $i; break } }
$end = $start
$depth = 0
for ($i = $start; $i -lt $lines.Count; $i++) {
    $end = $i
    if ($lines[$i] -match '^static SwVal sw_smooth') { $depth = 1; continue }
    if ($depth -gt 0) { if ($lines[$i] -match '^}') { $depth-- }; if ($depth -eq 0) { $end = $i; break } }
}
$start2 = $start
if ($start2 -gt 0 -and $lines[$start2 - 1].Trim() -eq '') { $start2-- }
Write-Output "helper block $start2..$end"
$block = ($lines[$start2..$end] -join "`n") + "`n"

$ins = -1
for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^static SwVal sw_val') { $ins = $i; break } }
for ($i = $ins + 1; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^}') { $ins = $i; break } }
Write-Output "insert after line $ins ($($lines[$ins]))"

$out = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($i -ge $start2 -and $i -le $end) { continue }
    $out.Add($lines[$i])
    if ($i -eq $ins) { $out.Add($block.TrimEnd("`n")) }
}
[IO.File]::WriteAllText($p, ($out -join "`n"), $enc)
Write-Output "done"
