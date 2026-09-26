$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$lines = Get-Content $p -Encoding UTF8
# lines 631..633 (0-based 630..632): swap SWDBG out of the dead zone
# current: [630] return n;  [631] SWDBG(...);  [632] }  [633] sw_parse_err(...)
$dead = '    SWDBG("unexpected token at: %s\n", lx.txt);'
if ($lines[631].Trim() -eq 'SWDBG("unexpected token at: %s\n", lx.txt);' -and $lines[632].Trim() -eq '}') {
    $lines[631] = '    }'
    $lines[632] = '    SWDBG("unexpected token at: %s\n", lx.txt);'
}
[IO.File]::WriteAllText($p, ($lines -join "`n"), $enc)
"debug moved"