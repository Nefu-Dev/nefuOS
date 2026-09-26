$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$lines = Get-Content $p -Encoding UTF8
for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match 'static void sw_parse_err') { break } }
"sw_parse_err at $($i+1)"
$dbgLine = '    if (ps.lx.p) { char ctx[64]; int m = 0; const char* q = ps.lx.p; while (*q && *q != 10 && m < 63) { ctx[m++] = *q; q++; } ctx[m] = 0; SWDBG("  parse_err at: %s\n", ctx); }'
$lines[$i+1] = $lines[$i+1] + "`n" + $dbgLine
[IO.File]::WriteAllText($p, ($lines -join "`n"), $enc)
"err context debug added at $($i+2)"