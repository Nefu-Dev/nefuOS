$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$s = [IO.File]::ReadAllText($p, $enc)
$anchor = "static float sw_signf(float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }"
$helper = @'
static float sw_signf(float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }

// mix/step/smoothstep (component-wise; no captured lambdas)
static SwVal sw_mix(const SwVal& a, const SwVal& b, const SwVal& t) {
    bool av = a.t >= T_V2 && a.t <= T_V4;
    bool bv = b.t >= T_V2 && b.t <= T_V4;
    bool tv = t.t >= T_V2 && t.t <= T_V4;
    int n = av ? sw_tsize(a.t) : (bv ? sw_tsize(b.t) : (tv ? sw_tsize(t.t) : 1));
    SwVal r = av ? a : sw_scalar(a.f[0]);
    for (int i = 0; i < n; i++) {
        float x = av ? a.f[i] : a.f[0];
        float y = bv ? b.f[i] : b.f[0];
        float w = tv ? t.f[i] : t.f[0];
        r.f[i] = x * (1.0f - w) + y * w;
    }
    return r;
}
static SwVal sw_step(const SwVal& e, const SwVal& x) {
    bool ev = e.t >= T_V2 && e.t <= T_V4;
    bool xv = x.t >= T_V2 && x.t <= T_V4;
    int n = ev ? sw_tsize(e.t) : (xv ? sw_tsize(x.t) : 1);
    SwVal r = ev ? e : (xv ? x : sw_scalar(e.f[0]));
    for (int i = 0; i < n; i++) {
        float ei = ev ? e.f[i] : e.f[0];
        float xi = xv ? x.f[i] : x.f[0];
        r.f[i] = xi < ei ? 0.0f : 1.0f;
    }
    return r;
}
static SwVal sw_smooth(const SwVal& e0, const SwVal& e1, const SwVal& x) {
    bool a0 = e0.t >= T_V2 && e0.t <= T_V4;
    bool a1 = e1.t >= T_V2 && e1.t <= T_V4;
    bool xv = x.t >= T_V2 && x.t <= T_V4;
    int n = a0 ? sw_tsize(e0.t) : (a1 ? sw_tsize(e1.t) : (xv ? sw_tsize(x.t) : 1));
    SwVal r = a0 ? e0 : (xv ? x : sw_scalar(e0.f[0]));
    for (int i = 0; i < n; i++) {
        float a = a0 ? e0.f[i] : e0.f[0];
        float b = a1 ? e1.f[i] : e1.f[0];
        float v = xv ? x.f[i] : x.f[0];
        float t = sw_clampf((v - a) / (b - a), 0.0f, 1.0f);
        r.f[i] = t * t * (3.0f - 2.0f * t);
    }
    return r;
}
'@
if ($s.Contains($anchor)) { $s = $s.Replace($anchor, $helper); [IO.File]::WriteAllText($p, $s, $enc); Write-Output "helpers inserted" } else { Write-Output "anchor NOT FOUND" }
