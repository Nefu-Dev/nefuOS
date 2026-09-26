$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$s = [IO.File]::ReadAllText($p, $enc)
$ok = 0

# 1) bump arg split cap 12 -> 40 (uniformMatrix4fv needs 17)
$old = @'
    const char* a[12];
    int an = sw_split(v, a, 12);
'@
$new = @'
    const char* a[40];
    int an = sw_split(v, a, 40);
'@
if ($s.Contains($old)) { $s = $s.Replace($old, $new); $ok++ } else { Write-Output "split cap NOT FOUND" }

# 2) sh branch: take the REST of the decoded string (may contain commas), like host
$old = @'
    } else if (!strcmp(c, "sh")) {    // shaderSource
        unsigned sid = sw_parse_sid(a[0]);
        int type = sw_atoi(a[1]);
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
            // source may contain commas? our protocol uses url-encoded source so
            // the whole source is one arg; but sources with commas break the
            // split. We re-join args 2..an-1 with commas.
            static char srcbuf[8192];
            int o = 0;
            for (int i = 2; i < an; i++) {
                for (const char* p = a[i]; *p && o < 8191; p++) srcbuf[o++] = *p;
                if (i + 1 < an && o < 8191) srcbuf[o++] = ',';
            }
            srcbuf[o] = 0;
            sw_sh_compile(*sh, srcbuf);
            SWDBG("sh sid=%u type=%d compiled=%d err=%s src=%.60s\n", sid, type, sh->compiled, sh->err, srcbuf);
        }
'@
$new = @'
    } else if (!strcmp(c, "sh")) {    // shaderSource: sid,type,src (src = rest of string, may contain commas)
        // walk the raw decoded string field-by-field like the host does
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
if ($s.Contains($old)) { $s = $s.Replace($old, $new); $ok++ } else { Write-Output "sh branch NOT FOUND" }

[IO.File]::WriteAllText($p, $s, $enc)
Write-Output "applied $ok/2"
