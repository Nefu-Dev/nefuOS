# Apply protocol-alignment and bug fixes to swgl.cpp (ASCII-only replacements)
$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$s = [IO.File]::ReadAllText($p, $enc)
$orig = $s
$n = 0

function Apply($old, $new) {
    $script:count = ([regex]::Matches($script:s, [regex]::Escape($old))).Count
    if ($count -eq 1) {
        $script:s = $script:s.Replace($old, $new)
        $script:n++
    } elseif ($count -eq 0) {
        Write-Output "NOT FOUND: $($old.Substring(0, [Math]::Min(60, $old.Length)))"
    } else {
        Write-Output "AMBIGUOUS($count): $($old.Substring(0, [Math]::Min(60, $old.Length)))"
    }
}

# R1 def_vao
Apply @'
    int cur_vao;     // canvas VAO index (-1 = default)
    int active_unit;
'@ @'
    int cur_vao;     // canvas VAO index (-1 = default VAO)
    SwVao def_vao;   // the default vertex array (used when cur_vao == -1)
    int active_unit;
'@

# R2 remove sw_map
Apply @'
// component-wise apply to scalars and vectors
static SwVal sw_map(SwVal a, SwVal b, float (*fn)(float, float)) {
    int n = sw_maxf((float)sw_tsize(a.t), (float)sw_tsize(b.t));
    if (n > 4) n = 4;
    SwVal r = sw_val(n <= 2 ? (n == 2 ? T_V2 : T_F) : (n == 3 ? T_V3 : T_V4));
    if (n == 1) r.t = T_F;
    for (int i = 0; i < n; i++) {
        float x = sw_tsize(a.t) > 1 ? a.f[i] : a.f[0];
        float y = sw_tsize(b.t) > 1 ? b.f[i] : b.f[0];
        r.f[i] = fn(x, y);
    }
    return r;
}

'@ ''

# R3 remove dead ctor line
Apply @'
    if (!strcmp(name, "ctor")) return sw_constructor(args[0].t != T_VOID ? (int)args[0].t : T_F, 0, 0);
'@ ''

# R4 fetch_attr bounds + default stride
Apply @'
    int idx = a.divisor > 0 ? dc.inst / a.divisor : vi;
    long off = (long)a.offset + (long)idx * (a.stride > 0 ? a.stride : a.size * 4);
    const uint8_t* p = b.data + off;
    int n = a.size > 4 ? 4 : a.size;
'@ @'
    int idx = a.divisor > 0 ? dc.inst / a.divisor : vi;
    int esz = a.type == SW_FLOAT ? 4 : (a.type == SW_UNSIGNED_SHORT || a.type == SW_SHORT) ? 2 : 1;
    int stride = a.stride > 0 ? a.stride : a.size * esz;
    long off = (long)a.offset + (long)idx * stride;
    if (off < 0 || off + (long)a.size * esz > b.bytes) return v;
    const uint8_t* p = b.data + off;
    int n = a.size > 4 ? 4 : a.size;
'@

# R5 run_fragment color_mask
Apply @'
    uint8_t* px = cv.fb + ((size_t)y * cv.w + x) * 4;
    if (!sw_depth_test(cv, z)) return;
    if (cv.blend) {
        sw_blend_pixel(cv, px, c);
    } else {
'@ @'
    uint8_t* px = cv.fb + ((size_t)y * cv.w + x) * 4;
    if (!sw_depth_test(cv, z)) return;
    float cm[4] = { c[0], c[1], c[2], c[3] };
    if (!cv.color_mask[0]) cm[0] = px[0] / 255.0f;
    if (!cv.color_mask[1]) cm[1] = px[1] / 255.0f;
    if (!cv.color_mask[2]) cm[2] = px[2] / 255.0f;
    if (!cv.color_mask[3]) cm[3] = px[3] / 255.0f;
    if (cv.blend) {
        sw_blend_pixel(cv, px, cm);
    } else {
        c = cm;
'@

# R6 emit_prim lines
Apply @'
    if (mode == SW_LINES || mode == SW_LINE_STRIP || mode == SW_LINE_LOOP) {
        for (int i = 0; i < n; i += 2) {
            if (mode == SW_LINES) { if (i + 1 < n) sw_raster_line(dc, vs[base + i], vs[base + i + 1]); }
            else sw_raster_line(dc, vs[base + i], vs[base + (i + 1) % n]);
        }
        return;
    }
'@ @'
    if (mode == SW_LINES) {
        for (int i = 0; i + 1 < n; i += 2) sw_raster_line(dc, vs[base + i], vs[base + i + 1]);
        return;
    }
    if (mode == SW_LINE_STRIP || mode == SW_LINE_LOOP) {
        int m = mode == SW_LINE_STRIP ? n - 1 : n;
        for (int i = 0; i < m; i++) sw_raster_line(dc, vs[base + i], vs[base + (i + 1) % n]);
        return;
    }
'@

# R7 do_draw: heap caches, default VAO, byte offset, count cap
Apply @'
static void sw_do_draw(SwCanvas& cv, int mode, int count, int first, int instCount, bool indexed,
                       int idxType, int idxOffset) {
    SwProg* prog = cv.cur_prog >= 0 ? &cv.pr[cv.cur_prog] : 0;
    if (!prog || !prog->linked) return;
    SwVao* vao = cv.cur_vao >= 0 ? &cv.va[cv.cur_vao] : &cv.def_vao;
    if (count <= 0 || count > 8192) return;
    SwDrawCtx dc;
    dc.cv = cv;
    dc.prog = *prog;
    dc.vao = vao;
    SwVtx* vcache = (SwVtx*)kalloc((size_t)count * sizeof(SwVtx));
    if (!vcache) return;
    int* idx = (int*)kalloc((size_t)count * sizeof(int));
    if (!idx) { kfree(vcache); return; }
    for (int inst = 0; inst < instCount; inst++) {
        dc.inst = inst;
        int n = count;
        for (int i = 0; i < n; i++) {
            int vi = first + i;
            if (indexed) {
                SwBuf_* eb = cv.cur_elem >= 0 ? &cv.bu[cv.cur_elem] : 0;
                if (!eb || !eb->data) { kfree(vcache); kfree(idx); return; }
                const uint8_t* p = eb->data + idxOffset;
                if (idxType == SW_UNSIGNED_SHORT) vi = (idxOffset + (long)i * 2 + 2 <= (long)eb->bytes) ? ((const uint16_t*)p)[i] : 0;
                else if (idxType == SW_UNSIGNED_INT) vi = (idxOffset + (long)i * 4 + 4 <= (long)eb->bytes) ? (int)((const uint32_t*)p)[i] : 0;
                else vi = (idxOffset + (long)i + 1 <= (long)eb->bytes) ? p[i] : 0;
            }
            idx[i] = vi;
        }
        for (int i = 0; i < n; i++) {
            int vi = idx[i];
            if (vi < 0) vi = 0;
            sw_run_vertex(dc, vi, vcache[i]);
        }
        sw_emit_prim(dc, mode, vcache, n, 0, -1, -1, -1);
    }
    kfree(vcache);
    kfree(idx);
}
'@

# R8 attptr default VAO
Apply @'
        SwVao* vao = cv->cur_vao >= 0 ? &cv->va[cv->cur_vao] : 0;
        if (!vao) {
            // default VAO storage
            static SwVao s_default_vao;
            if (cv->cur_vao != -2) { memset(&s_default_vao, 0, sizeof(s_default_vao)); cv->cur_vao = -2; }
            vao = &s_default_vao;
        }
        vao->attrs[slot].enabled = true;
'@ @'
        SwVao* vao = cv->cur_vao >= 0 ? &cv->va[cv->cur_vao] : &cv->def_vao;
        vao->attrs[slot].enabled = true;
'@

# R9 remove attrib en/dis branches
Apply @'
    } else if (!strcmp(c, "en")) {    // enableVertexAttribArray
        int loc = sw_atoi(a[0]);
        int slot = loc;
        if (cv->a_loc[loc & 0xFF]) slot = (int)cv->a_loc[loc & 0xFF] - 1;
        if (slot >= 0 && slot < SW_MAX_ATTRIBS) {
            SwVao* vao = cv->cur_vao >= 0 ? &cv->va[cv->cur_vao] : &s_default_vao(cv);
            vao->attrs[slot].enabled = true;
        }
    } else if (!strcmp(c, "dis")) {   // disableVertexAttribArray
        int loc = sw_atoi(a[0]);
        int slot = loc;
        if (cv->a_loc[loc & 0xFF]) slot = (int)cv->a_loc[loc & 0xFF] - 1;
        if (slot >= 0 && slot < SW_MAX_ATTRIBS) {
            SwVao* vao = cv->cur_vao >= 0 ? &cv->va[cv->cur_vao] : &s_default_vao(cv);
            vao->attrs[slot].enabled = false;
        }
'@ ''

# R11 bindbuf
Apply @'
    } else if (!strcmp(c, "bindbuf")) { // bindBuffer
        int target = sw_atoi(a[0]);
        unsigned sid = sw_parse_sid(a[1]);
'@ @'
    } else if (!strcmp(c, "bindbuf")) { // bindBuffer: bid,target (JS ids)
        unsigned sid = sw_parse_sid(a[0]);
        int target = sw_atoi(a[1]);
'@

# R12 uloc/aloc order
Apply @'
    } else if (!strcmp(c, "uloc")) {  // getUniformLocation proxy: uid,name
        unsigned uid = sw_parse_sid(a[0]);
        unsigned pid = sw_parse_sid(a[1]);
        const char* name = a[2];
'@ @'
    } else if (!strcmp(c, "uloc")) {  // getUniformLocation: pid,uid,name
        unsigned pid = sw_parse_sid(a[0]);
        unsigned uid = sw_parse_sid(a[1]);
        const char* name = a[2];
'@
Apply @'
    } else if (!strcmp(c, "aloc")) {  // getAttribLocation proxy: aid,pid,name
        unsigned aid = sw_parse_sid(a[0]);
        unsigned pid = sw_parse_sid(a[1]);
        const char* name = a[2];
'@ @'
    } else if (!strcmp(c, "aloc")) {  // getAttribLocation: pid,aid,name
        unsigned pid = sw_parse_sid(a[0]);
        unsigned aid = sw_parse_sid(a[1]);
        const char* name = a[2];
'@

# R13 att pid,sid
Apply @'
    } else if (!strcmp(c, "att")) {   // attachShader
        unsigned pid = sw_parse_sid(a[0]), sid = sw_parse_sid(a[1]);
        int type = sw_atoi(a[2]);
        SwProg* p = sw_prog_of(*cv, pid);
        SwShader* sh = sw_shader_of(*cv, type, sid);
        if (p && sh) {
            if (type == SW_VERTEX_SHADER) p->vs = sw_shader_of(*cv, type, sid) - cv->sh;
            else p->fs = sw_shader_of(*cv, type, sid) - cv->sh;
        }
'@ @'
    } else if (!strcmp(c, "att")) {   // attachShader: pid,sid (JS ids)
        unsigned pid = sw_parse_sid(a[0]);
        unsigned sid = sw_parse_sid(a[1]);
        SwProg* p = sw_prog_of(*cv, pid);
        SwShader* sh = 0;
        for (int i = 0; i < SW_MAX_SH; i++)
            if (cv->sh[i].used && cv->sh[i].sid == sid) { sh = &cv->sh[i]; break; }
        if (p && sh) {
            int si = (int)(sh - cv->sh);
            if (sh->type == SW_VERTEX_SHADER) p->vs = si;
            else p->fs = si;
        }
'@

# R14 add "tx" createTexture after vao branch
Apply @'
    } else if (!strcmp(c, "vao")) {   // createVertexArray
        unsigned sid = sw_parse_sid(a[0]);
        if (!sw_vao_of(*cv, sid))
            for (int i = 0; i < SW_MAX_VAO; i++)
                if (!cv->va[i].used) { cv->va[i].used = true; cv->va[i].sid = sid; cv->va[i].elem_buf = -1; break; }
'@ @'
    } else if (!strcmp(c, "vao")) {   // createVertexArray
        unsigned sid = sw_parse_sid(a[0]);
        if (!sw_vao_of(*cv, sid))
            for (int i = 0; i < SW_MAX_VAO; i++)
                if (!cv->va[i].used) { cv->va[i].used = true; cv->va[i].sid = sid; cv->va[i].elem_buf = -1; break; }
    } else if (!strcmp(c, "tx")) {    // createTexture
        unsigned sid = sw_parse_sid(a[0]);
        if (!sw_tex_of(*cv, sid))
            for (int i = 0; i < SW_MAX_TEX; i++)
                if (!cv->tx[i].used) { cv->tx[i].used = true; cv->tx[i].sid = sid; cv->tx[i].target = SW_TEXTURE_2D; cv->tx[i].min_filter = SW_NEAREST; cv->tx[i].mag_filter = SW_NEAREST; cv->tx[i].wrap_s = SW_REPEAT; cv->tx[i].wrap_t = SW_REPEAT; break; }
'@

# R15 div default VAO
Apply @'
        int slot = loc;
        if (cv->a_loc[loc & 0xFF]) slot = (int)cv->a_loc[loc & 0xFF] - 1;
        if (slot >= 0 && slot < SW_MAX_ATTRIBS) {
            SwVao* vao = cv->cur_vao >= 0 ? &cv->va[cv->cur_vao] : 0;
            if (vao) vao->attrs[slot].divisor = divisor;
        }
'@ @'
        int slot = loc;
        if (cv->a_loc[loc & 0xFF]) slot = (int)cv->a_loc[loc & 0xFF] - 1;
        if (slot >= 0 && slot < SW_MAX_ATTRIBS) {
            SwVao* vao = cv->cur_vao >= 0 ? &cv->va[cv->cur_vao] : &cv->def_vao;
            vao->attrs[slot].divisor = divisor;
        }
'@

[IO.File]::WriteAllText($p, $s, $enc)
"applied $n replacements; length $($orig.Length) -> $($s.Length)"
