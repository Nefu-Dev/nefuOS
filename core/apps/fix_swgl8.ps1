$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$lines = (Get-Content $p -Encoding UTF8)
$start = -1; $end = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'else if \(!strcmp\(c, "sh"\)\)') { $start = $i; break }
}
if ($start -ge 0) {
    $depth = 0
    for ($i = $start; $i -lt $lines.Count; $i++) {
        # count braces on the line
        foreach ($ch in $lines[$i].ToCharArray()) { if ($ch -eq '{') { $depth++ } elseif ($ch -eq '}') { $depth-- } }
        if ($depth -eq 0 -and $i -gt $start) { $end = $i; break }
    }
    Write-Output "sh branch lines $start..$end"
    $newBranch = @'
    } else if (!strcmp(c, "sh")) {    // shaderSource: sid,type,src (src = rest of string, may contain commas)
        const char* p = v;
        while (*p && *p != ',') p++; if (*p) p++;
        const char* t0 = p;
        while (*p && *p != ',') p++; if (*p) p++;
        const char* src = p;
        char tbuf[24];
        int tn = 0;
        while (t0[tn] && t0[tn] != ',' && tn < 23) { tbuf[tn] = t0[tn]; tn++; }
        tbuf[tn] = 0;
        unsigned sid = sw_atoi(v);
        int type = sw_atoi(tbuf);
        SwShader* sh = sw_shader_of(*cv, type, sid);
        if (!sh) {
            for (int i = 0; i < SW_MAX_SH; i++) {
                if (!cv->sh[i].used) {
                    cv->sh[i].used = true;
                    cv->sh[i].sid = sid;
                    cv->sh[i].type = type;
                    sh = &cv->sh[i];
                    break;
                }
            }
        }
        if (sh) {
            static char srcbuf[8192];
            int o = 0;
            for (const char* q = src; *q && o < 8191; q++) srcbuf[o++] = *q;
            srcbuf[o] = 0;
            sw_sh_compile(*sh, srcbuf);
            SWDBG("sh sid=%u type=%d compiled=%d err=%s src=%.60s\n", sid, type, sh->compiled, sh->err, srcbuf);
        }
'@
    $out = New-Object System.Collections.Generic.List[string]
    $out.AddRange([string[]]$lines[0..($start-1)])
    $out.Add($newBranch)
    $out.AddRange([string[]]$lines[($end+1)..($lines.Count-1)])
    [IO.File]::WriteAllText($p, ($out -join "`n"), $enc)
    Write-Output "sh branch replaced"
} else { Write-Output "sh branch start NOT FOUND" }
