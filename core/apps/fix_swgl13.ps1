$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$lines = Get-Content $p -Encoding UTF8
# 1) lexer ops2: add "++" "--"
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '"\+=", "-="') {
        $lines[$i] = $lines[$i].Replace('"+=", "-="', '"+=", "-=", "++", "--"')
        break
    }
}
# 2) swizzle loop: replace token-loop with char-loop + correct ranges
$old1 = '                while (lx.k == TK_ID && sw->swz_n < 4) {'
$new1 = @'
                if (lx.k == TK_ID) {
                    const char* sc = lx.txt;
                    for (int ci = 0; sc[ci] && sw->swz_n < 4; ci++) {
                        char c = sc[ci];
                        int comp = -1;
                        if (c == 'x' || c == 'r' || c == 's') comp = 0;
                        else if (c == 'y' || c == 'g' || c == 't') comp = 1;
                        else if (c == 'z' || c == 'b' || c == 'p') comp = 2;
                        else if (c == 'w' || c == 'a' || c == 'q') comp = 3;
                        if (comp < 0) break;
                        sw->swz[sw->swz_n++] = comp;
                    }
                    sw_lex_next(lx);
                }
                if (false) while (0) {
'@
$done1 = $false
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'while \(lx\.k == TK_ID && sw->swz_n < 4\) \{') {
        $tail = $i
        while ($lines[$tail].Trim() -ne '}') { $tail++ }
        # find block end: the inner while's closing brace
        $body = @()
        for ($j = $i; $j -le $tail; $j++) { $body += $lines[$j] }
        # new body
        $block = @(
'                if (lx.k == TK_ID) {',
'                    const char* sc = lx.txt;',
'                    for (int ci = 0; sc[ci] && sw->swz_n < 4; ci++) {',
'                        char c = sc[ci];',
'                        int comp = -1;',
"                        if (c == 'x' || c == 'r' || c == 's') comp = 0;",
"                        else if (c == 'y' || c == 'g' || c == 't') comp = 1;",
"                        else if (c == 'z' || c == 'b' || c == 'p') comp = 2;",
"                        else if (c == 'w' || c == 'a' || c == 'q') comp = 3;",
'                        if (comp < 0) break;',
'                        sw->swz[sw->swz_n++] = comp;',
'                    }',
'                    sw_lex_next(lx);',
'                }'
        )
        $out = New-Object System.Collections.Generic.List[string]
        $out.AddRange([string[]]$lines[0..($i-1)])
        foreach ($b in $block) { $out.Add($b) }
        $out.AddRange([string[]]$lines[($tail+1)..($lines.Count-1)])
        $lines = $out.ToArray()
        $done1 = $true
        break
    }
}
"swizzle replaced=$done1"
[IO.File]::WriteAllText($p, ($lines -join "`n"), $enc)
"lexer ops2 + swizzle fixed"