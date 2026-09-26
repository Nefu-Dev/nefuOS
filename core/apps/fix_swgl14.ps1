$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$lines = Get-Content $p -Encoding UTF8
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'if \(argc >= 2\) \{' -and $i -gt 1683 -and $i -lt 1695) {
        $dbg = '            SWDBG("  tex() unit=%d u=%.4f v=%.4f arg0=(%.3f,%.3f,%.3f,%.3f) w=%d h=%d\n", unit, u, v, args[0].f[0], args[0].f[1], args[0].f[2], args[0].f[3], cv.w, cv.h);'
        $lines[$i] = $lines[$i] + "`n" + $dbg
        break
    }
}
[IO.File]::WriteAllText($p, ($lines -join "`n"), $enc)
"tex trace added"