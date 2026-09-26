$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$s = [IO.File]::ReadAllText($p, $enc)
$ok = 0

$old1 = @'
    if (!strcmp(name, "mix") && argc == 3) {
        return sw_math2(args[0], args[2], [&](float x, float t) {
            return x * (1.0f - t) + args[1].f[0] * t;
        });
    }
    if (!strcmp(name, "step") && argc == 2)
        return sw_math2(args[0], args[1], [](float e, float x) { return x < e ? 0.0f : 1.0f; });
    if (!strcmp(name, "smoothstep") && argc == 3) {
        return sw_math1(args[2], [&](float x) {
            float t = sw_clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        });
    }
'@
$new1 = @'
    if (!strcmp(name, "mix") && argc == 3)
        return sw_mix(args[0], args[1], args[2]);
    if (!strcmp(name, "step") && argc == 2)
        return sw_step(args[0], args[1]);
    if (!strcmp(name, "smoothstep") && argc == 3)
        return sw_smooth(args[0], args[1], args[2]);
'@
if ($s.Contains($old1)) { $s = $s.Replace($old1, $new1); $ok++ } else { Write-Output "old1 NOT FOUND" }

$old2 = @'
struct SwDrawCtx {
    SwCanvas& cv;
    SwProg& prog;
    SwVao* vao;
'@
$new2 = @'
struct SwDrawCtx {
    SwDrawCtx(SwCanvas& c, SwProg& p) : cv(c), prog(p) { vao = 0; inst = 0; }
    SwCanvas& cv;
    SwProg& prog;
    SwVao* vao;
'@
if ($s.Contains($old2)) { $s = $s.Replace($old2, $new2); $ok++ } else { Write-Output "old2 NOT FOUND" }

$old3 = @'
    SwDrawCtx dc;
    dc.cv = cv;
    dc.prog = *prog;
    dc.vao = vao;
'@
$new3 = @'
    SwDrawCtx dc(cv, *prog);
    dc.vao = vao;
'@
if ($s.Contains($old3)) { $s = $s.Replace($old3, $new3); $ok++ } else { Write-Output "old3 NOT FOUND" }

[IO.File]::WriteAllText($p, $s, $enc)
Write-Output "applied $ok/3"
