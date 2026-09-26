$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$lines = Get-Content $p -Encoding UTF8
$anchor = -1
for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match 'static SwNode\* sw_parse_stmt\(SwParser& ps\) \{') { $anchor = $i; break } }
$dbg1 = '    SWDBG("stmt enter tok=%s\n", lx.txt);'
$lines[$anchor+1] = $lines[$anchor+1] + "`n" + $dbg1
$anchor2 = -1
for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match 'sw_parse_err\(ps, "unexpected token"\)') { $anchor2 = $i; break } }
$dbg2 = '    SWDBG("unexpected token at: %s\n", lx.txt);'
$lines[$anchor2-1] = $dbg2 + "`n" + $lines[$anchor2-1]
[IO.File]::WriteAllText($p, ($lines -join "`n"), $enc)
"instrumented stmt + unexpected token"