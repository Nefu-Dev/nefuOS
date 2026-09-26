$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$s = [IO.File]::ReadAllText($p, $enc)

$old = @'
    if (!strcmp(name, "mix") && argc == 3) {
        return sw_math2(args[0], args[2], [&](float x, float t) {
            return x * (1.0f - t) + args[1].f[0] * t;
        });
    }
    if (!strcmp(name, "step") && argc == 2)
        return sw_math2(args[0], args[1], [](float e, float x) { return x < e ? 0.0f : 1.0f; });
    if (!strcmp(name, "smoothstep") && argc == 3) {
        float e0 = args[0].f[0], e1 = args[1].f[0];
        return sw_math1(args[2], [&](float x) {
            float t = sw_clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        });
    }
'@
$new = @'
    if (!strcmp(name, "mix") && argc == 3)
        return sw_mix(args[0], args[1], args[2]);
    if (!strcmp(name, "step") && argc == 2)
        return sw_step(args[0], args[1]);
    if (!strcmp(name, "smoothstep") && argc == 3)
        return sw_smooth(args[0], args[1], args[2]);
'@
if ($s.Contains($old)) { $s = $s.Replace($old, $new); Write-Output "mix/step/smoothstep fixed" } else { Write-Output "NOT FOUND" }
[IO.File]::WriteAllText($p, $s, $enc)
