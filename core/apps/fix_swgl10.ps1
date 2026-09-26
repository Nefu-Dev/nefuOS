$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$lines = Get-Content $p -Encoding UTF8
# find the postfix loop's 'break;' at end (line with '        break;\n    }\n    return n;' right after the '.' swizzle block)
$anchor = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'if \(lx\.k == TK_OP && lx\.txt\[0\] == ''.''\)' -and $i -gt 600) { $anchor = $i; break }
}
"swizzle block at $($anchor+1)"
# find the '        break;' closing the postfix for-loop (first break after swizzle block end)
$bp = -1
for ($i = $anchor; $i -lt $lines.Count; $i++) {
    if ($lines[$i].Trim() -eq 'break;' -and $i -gt $anchor + 20) { $bp = $i; break }
}
"postfix break at $($bp+1)"
$post = @(
'        if (lx.k == TK_OP && (lx.txt[0] == ''+'' || lx.txt[0] == ''-'') && lx.txt[1] == lx.txt[0]) {',
'            // postfix ++ / --  =>  n = n + 1 / n = n - 1',
'            char opc = lx.txt[0];',
'            sw_lex_next(lx);',
'            SwNode* dup = sw_anew(ps.arena);',
'            SwNode* one = sw_anew(ps.arena);',
'            SwNode* rhs = sw_anew(ps.arena);',
'            SwNode* asn = sw_anew(ps.arena);',
'            if (dup && one && rhs && asn) {',
'                *dup = *n;',
'                one->k = N_FLOAT; one->fv = 1.0f;',
'                rhs->k = N_BIN;',
'                rhs->op = opc == ''+'' ? OP_ADD : OP_SUB;',
'                sw_set_child(rhs, 0, dup);',
'                sw_set_child(rhs, 1, one);',
'                asn->k = N_ASSIGN;',
'                asn->op = OP_ASSIGN;',
'                sw_set_child(asn, 0, n);',
'                sw_set_child(asn, 1, rhs);',
'                n = asn;',
'            }',
'            continue;',
'        }'
)
$out = New-Object System.Collections.Generic.List[string]
$out.AddRange([string[]]$lines[0..($bp-1)])
foreach ($l in $post) { $out.Add($l) }
$out.AddRange([string[]]$lines[$bp..($lines.Count-1)])
[IO.File]::WriteAllText($p, ($out -join "`n"), $enc)
"postfix ++/-- added before line $($bp+1)"