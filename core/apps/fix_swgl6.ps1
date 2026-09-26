$p = 'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
$enc = New-Object System.Text.UTF8Encoding($false)
$s = [IO.File]::ReadAllText($p, $enc)
$ok = 0

# 1) clearColor / clear: wire names are "glc"/"glclr" -> after strip -> "c"/"clr"
$old = @'
    if (!strcmp(c, "glc")) {          // clearColor
'@
$new = @'
    if (!strcmp(c, "c")) {            // clearColor (wire "glc")
'@
if ($s.Contains($old)) { $s = $s.Replace($old, $new); $ok++ } else { Write-Output "clearColor branch NOT FOUND" }

$old = @'
    } else if (!strcmp(c, "glclr")) { // clear
'@
$new = @'
    } else if (!strcmp(c, "clr")) {   // clear (wire "glclr")
'@
if ($s.Contains($old)) { $s = $s.Replace($old, $new); $ok++ } else { Write-Output "clear branch NOT FOUND" }

# 2) draw: wire = mode,count (first dropped in minijs)
$old = @'
    } else if (!strcmp(c, "draw")) {  // drawArrays: mode,first,count
        int mode = sw_atoi(a[0]);
        int first = sw_atoi(a[1]);
        int count = sw_atoi(a[2]);
        sw_do_draw(*cv, mode, count, first, 1, false, 0, 0);
'@
$new = @'
    } else if (!strcmp(c, "draw")) {  // drawArrays: mode,count (minijs drops first)
        int mode = sw_atoi(a[0]);
        int count = sw_atoi(a[1]);
        sw_do_draw(*cv, mode, count, 0, 1, false, 0, 0);
'@
if ($s.Contains($old)) { $s = $s.Replace($old, $new); $ok++ } else { Write-Output "draw branch NOT FOUND" }

# 3) attptr: wire = loc,size,stride,offset (type not sent; always FLOAT)
$old = @'
    } else if (!strcmp(c, "attptr")) { // vertexAttribPointer: loc,size,type,stride,offset
        int loc = sw_atoi(a[0]);
        int size = sw_atoi(a[1]);
        int type = sw_atoi(a[2]);
        int stride = sw_atoi(a[3]);
        int offset = sw_atoi(a[4]);
'@
$new = @'
    } else if (!strcmp(c, "attptr")) { // vertexAttribPointer: loc,size,stride,offset (minijs omits type)
        int loc = sw_atoi(a[0]);
        int size = sw_atoi(a[1]);
        int stride = sw_atoi(a[2]);
        int offset = sw_atoi(a[3]);
        int type = SW_FLOAT;
'@
if ($s.Contains($old)) { $s = $s.Replace($old, $new); $ok++ } else { Write-Output "attptr branch NOT FOUND" }

[IO.File]::WriteAllText($p, $s, $enc)
Write-Output "applied $ok/4"
